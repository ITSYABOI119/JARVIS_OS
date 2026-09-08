#!/usr/bin/env python3
"""Standard-library-only tests for jarvis_memory (Phase 7 goal 8, memory store MS0).

Run: python3 phase7/memory/test_memory_logic.py  -> PASS/FAIL per check, exit non-zero on any FAIL.
No numpy, no torch, no model and no GPU is imported here or by the modules under test. The store
tests all run on an in-memory SQLite database; nothing is written outside a temp directory and
nothing about the owner is ever read.

The spec is phase7/docs/PHASE_7_MEMORY_DESIGN.md; every constant here is copied from it, never tuned.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from jarvis_memory.registry import PREDICATES, RELATIONS, SOURCE_RANK, is_known, arity  # noqa: E402
from jarvis_memory.candidate import validate  # noqa: E402
from jarvis_memory.confidence import (  # noqa: E402
    TAU_DAYS, SURFACE_THRESHOLD, confidence, distinct_days,
)
from jarvis_memory.freshness import key, newer  # noqa: E402
from jarvis_memory.rules import decide  # noqa: E402
from jarvis_memory.retrieve import (  # noqa: E402
    W_SOURCE, HALF_LIFE_DAYS, recency_weight, score, rank, lane_order,
)
from jarvis_memory.people import MIN_DAYS, MIN_SPANS_PER_DAY, is_person, purge_plan  # noqa: E402

FAILS = 0
CHECKS = 0


def check(name, cond, detail=""):
    global FAILS, CHECKS
    CHECKS += 1
    if cond:
        print(f"PASS {name}")
    else:
        FAILS += 1
        print(f"FAIL {name} {detail}")


def close_to(a, b, tol=1e-4):
    return abs(a - b) <= tol


# ---------------------------------------------------------------- T1 registry

check("T1a nine predicates", len(PREDICATES) == 9, str(sorted(PREDICATES)))
check("T1b person.lives_in is single", arity("person.lives_in") == "single", arity("person.lives_in"))
check("T1c person.habit is multi", arity("person.habit") == "multi", arity("person.habit"))
check("T1d owner.style is not a predicate", is_known("owner.style") is False)
check("T1e spouse in RELATIONS", "spouse" in RELATIONS)
check("T1f stated_owner outranks inferred", SOURCE_RANK["stated_owner"] > SOURCE_RANK["inferred"])
try:
    arity("nope.nope")
    check("T1g arity raises KeyError on unknown", False, "no exception")
except KeyError:
    check("T1g arity raises KeyError on unknown", True)

# --------------------------------------------------------------- T2 validate

SPAN_CLUSTER = {1: 1, 2: 2}


def cand(**over):
    base = dict(
        predicate_id="person.lives_in",
        subject={"kind": "person", "id": 1},
        object="Brisbane",
        object_norm="brisbane",
        source_kind="stated_owner",
        speaker_cluster=1,
        span_ids=[1],
        about_time=None,
        relation_id=None,
        polarity=None,
        strength=None,
        ended=False,
        said_at="2026-03-01T10:00:00",
    )
    base.update(over)
    return base


ok, reason = validate(cand(), SPAN_CLUSTER)
check("T2a good candidate", (ok, reason) == (True, None), f"{ok} {reason}")

ok, reason = validate(cand(predicate_id="person.teleports"), SPAN_CLUSTER)
check("T2b unknown predicate", (ok is False) and reason == "unknown predicate", f"{ok} {reason}")

ok, reason = validate(cand(span_ids=[]), SPAN_CLUSTER)
check("T2c no span ids", (ok is False) and reason == "no span ids", f"{ok} {reason}")

ok, reason = validate(cand(span_ids=[9]), SPAN_CLUSTER)
check("T2d unknown span", (ok is False) and str(reason).startswith("unknown span"), f"{ok} {reason}")

ok, reason = validate(cand(span_ids=[2]), SPAN_CLUSTER)
check("T2e stated speaker differs from span cluster",
      (ok is False) and str(reason).startswith("stated candidate speaker"), f"{ok} {reason}")

ok, reason = validate(cand(span_ids=[2], source_kind="inferred"), SPAN_CLUSTER)
check("T2f inferred candidate is not speaker-checked", (ok, reason) == (True, None), f"{ok} {reason}")

ok, reason = validate(cand(predicate_id="person.relation_to", object="partner",
                           object_norm="partner", relation_id=None), SPAN_CLUSTER)
check("T2g relation_id required", (ok is False) and reason == "relation_id required", f"{ok} {reason}")

ok, reason = validate(cand(predicate_id="person.relation_to", object="partner",
                           object_norm="partner", relation_id="nemesis"), SPAN_CLUSTER)
check("T2h unknown relation", (ok is False) and str(reason).startswith("unknown relation"), f"{ok} {reason}")

ok, reason = validate(cand(predicate_id="owner.prefers", object="spicy food",
                           object_norm="spicy food", polarity=None), SPAN_CLUSTER)
check("T2i polarity required", (ok is False) and reason == "polarity required", f"{ok} {reason}")

ok, reason = validate(cand(source_kind="rumour"), SPAN_CLUSTER)
check("T2j bad source_kind", (ok is False) and str(reason).startswith("bad source_kind"), f"{ok} {reason}")

c = cand(source_kind="inferred", ended=True)
ok, reason = validate(c, SPAN_CLUSTER)
check("T2k inferred ended is coerced to False, not rejected",
      (ok, reason) == (True, None) and c["ended"] is False, f"{ok} {reason} ended={c['ended']}")

# ------------------------------------------------------------- T3 confidence

check("T3a tau is 3 days", close_to(TAU_DAYS, 3.0))
check("T3b surface threshold is 0.80", close_to(SURFACE_THRESHOLD, 0.80))
# (7,0) is 1 - exp(-7/3) = 0.9030280..., i.e. 0.9030 to four places - the design's "seven give 0.90".
# The MS0 prompt's table printed 0.9029, which is 1.28e-4 away and outside its own 1e-4 tolerance;
# the vector is corrected here and the formula is untouched. Every other vector in that table is exact.
for ds, dc, want in [(3, 0, 0.6321), (5, 0, 0.8111), (7, 0, 0.9030), (4, 0, 0.7364),
                     (5, 1, 0.7364), (2, 2, 0.0), (1, 3, 0.0), (0, 0, 0.0)]:
    got = confidence(ds, dc)
    check(f"T3 confidence({ds},{dc}) == {want}", close_to(got, want), f"got {got!r}")
check("T3c confidence(5,0) reaches the surfacing threshold", confidence(5, 0) >= SURFACE_THRESHOLD)
check("T3d confidence(4,0) does not", confidence(4, 0) < SURFACE_THRESHOLD)
check("T3e distinct_days collapses same-date timestamps",
      distinct_days(["2026-09-06T01:00:00", "2026-09-06T23:00:00", "2026-09-07T00:00:00"]) == 2,
      str(distinct_days(["2026-09-06T01:00:00", "2026-09-06T23:00:00", "2026-09-07T00:00:00"])))
check("T3f distinct_days([]) == 0", distinct_days([]) == 0)

# -------------------------------------------------------------- T4 freshness

check("T4a later valid_from is newer",
      newer({"valid_from": "2026-03-02"}, {"valid_from": "2026-03-01"}) is True)
check("T4b equal valid_from, said_at decides",
      newer({"valid_from": "2026-03-01", "said_at": "2026-03-05T09:00:00"},
            {"valid_from": "2026-03-01", "said_at": "2026-03-04T09:00:00"}) is True)
check("T4c equal valid_from and said_at, recorded_at decides",
      newer({"valid_from": "2026-03-01", "said_at": "2026-03-05T09:00:00", "recorded_at": "2026-03-06T00:00:00"},
            {"valid_from": "2026-03-01", "said_at": "2026-03-05T09:00:00", "recorded_at": "2026-03-05T00:00:00"}) is True)
_ident = {"valid_from": "2026-03-01", "said_at": "2026-03-01T00:00:00", "recorded_at": "2026-03-01T00:00:00"}
check("T4d identical keys are not newer either way",
      newer(_ident, dict(_ident)) is False and newer(dict(_ident), _ident) is False)
check("T4e key falls back to about_time then said_at",
      key({"about_time": "2026-02-02", "said_at": "2026-03-03T00:00:00"})[0] == "2026-02-02"
      and key({"said_at": "2026-03-03T00:00:00"})[0] == "2026-03-03T00:00:00",
      str(key({"about_time": "2026-02-02", "said_at": "2026-03-03T00:00:00"})))

# ------------------------------------------------------------------ T5 rules

REC = "2026-09-06T00:00:00"


def existing(row_id, source_kind, valid_from, object_norm="brisbane",
             said_at=None, recorded_at="2026-01-01T00:00:00"):
    return {"id": row_id, "source_kind": source_kind, "valid_from": valid_from,
            "said_at": said_at if said_at is not None else valid_from + "T00:00:00",
            "recorded_at": recorded_at, "object_norm": object_norm}


E = existing(7, "stated_owner", "2026-01-10")

r = decide(cand(said_at="2026-01-10T00:00:00"), [], REC)
check("T5a no existing -> append",
      r["outcome"] == "append" and r["close"] == [] and r["audit"] == [], str(r["outcome"]))

r = decide(cand(object_norm="sydney", about_time="2026-05-01", said_at="2026-05-01T00:00:00"), [E], REC)
check("T5b newer stated_owner -> supersede",
      r["outcome"] == "supersede"
      and r["close"] == [{"row_id": 7, "valid_to": "2026-05-01"}]
      and len(r["audit"]) == 1
      and (r["audit"][0]["op"], r["audit"][0]["rule"], r["audit"][0]["loser"]) == ("supersede", "R3", 7),
      f"{r['outcome']} {r['close']} {r['audit']}")

r = decide(cand(object_norm="perth", about_time="2025-12-01", said_at="2025-12-01T00:00:00"), [E], REC)
check("T5c older stated_owner -> history closed against the winner",
      r["outcome"] == "history"
      and r["new_row"]["valid_to"] == "2026-01-10"
      and r["new_row"]["superseded_by"] == 7
      and r["close"] == []
      and len(r["audit"]) == 1 and r["audit"][0]["loser"] == "new",
      f"{r['outcome']} {r.get('new_row')} {r['close']} {r['audit']}")

r = decide(cand(source_kind="inferred", object_norm="cairns", about_time="2026-05-01",
                said_at="2026-05-01T00:00:00"), [E], REC)
check("T5d inferred beside a stated row",
      r["outcome"] == "beside" and r["close"] == [] and r["audit"] == [],
      f"{r['outcome']} {r['close']} {r['audit']}")

I8 = existing(8, "inferred", "2026-06-01")
r = decide(cand(object_norm="sydney", about_time="2026-05-01", said_at="2026-05-01T00:00:00"), [I8], REC)
check("T5e stated outranks a newer inferred row (R2)",
      r["outcome"] == "supersede"
      and r["close"] == [{"row_id": 8, "valid_to": "2026-05-01"}]
      and len(r["audit"]) == 1
      and (r["audit"][0]["op"], r["audit"][0]["rule"], r["audit"][0]["loser"]) == ("supersede", "R2", 8),
      f"{r['outcome']} {r['close']} {r['audit']}")

H = existing(9, "stated_owner", "2026-01-01", object_norm="runs at 7")
r = decide(cand(predicate_id="person.habit", object="swims", object_norm="swims",
                about_time="2026-03-10", said_at="2026-03-10T00:00:00"), [H], REC)
check("T5f multi-valued predicate coexists",
      r["outcome"] == "coexist" and r["close"] == [], f"{r['outcome']} {r['close']}")

r = decide(cand(predicate_id="person.habit", object="runs at 7", object_norm="runs at 7",
                ended=True, about_time="2026-03-10", said_at="2026-03-10T00:00:00"), [H], REC)
check("T5g ended closes the matching multi-valued row (R4)",
      r["outcome"] == "close"
      and r["close"] == [{"row_id": 9, "valid_to": "2026-03-10"}]
      and r["new_row"] is None
      and len(r["audit"]) == 1
      and (r["audit"][0]["op"], r["audit"][0]["rule"], r["audit"][0]["loser"]) == ("close", "R4", 9),
      f"{r['outcome']} {r['close']} {r.get('new_row')} {r['audit']}")

r = decide(cand(predicate_id="person.habit", object="cycles", object_norm="cycles",
                ended=True, about_time="2026-03-10", said_at="2026-03-10T00:00:00"), [H], REC)
check("T5h ended with nothing matching is still evidence",
      r["outcome"] == "coexist" and r["new_row"] is not None, f"{r['outcome']} {r.get('new_row')}")

r = decide(cand(object_norm="sydney", about_time="2026-01-10", said_at="2026-01-10T09:00:00"), [E], REC)
check("T5i1 tie on valid_from, later said_at wins", r["outcome"] == "supersede", r["outcome"])
r = decide(cand(object_norm="sydney", about_time="2026-01-10", said_at="2026-01-09T09:00:00"), [E], REC)
check("T5i2 tie on valid_from, earlier said_at loses", r["outcome"] == "history", r["outcome"])

S10 = existing(10, "stated_other", "2026-01-01")
I11 = existing(11, "inferred", "2026-03-01")
r = decide(cand(source_kind="stated_other", speaker_cluster=2, object_norm="perth",
                about_time="2025-12-01", said_at="2025-12-01T00:00:00"), [S10, I11], REC)
check("T5j a history candidate closes nothing, not even a lower-ranked row",
      r["outcome"] == "history" and r["close"] == [] and len(r["audit"]) == 1,
      f"{r['outcome']} {r['close']} {r['audit']}")

r = decide(cand(source_kind="stated_other", speaker_cluster=2, object_norm="perth",
                about_time="2026-02-01", said_at="2026-02-01T00:00:00"), [S10, I11], REC)
_closed = sorted(c["row_id"] for c in r["close"])
_rules = sorted((a["loser"], a["rule"]) for a in r["audit"])
check("T5k a winning candidate closes the equal-rank older row and the lower-ranked one",
      r["outcome"] == "supersede" and _closed == [10, 11] and len(r["audit"]) == 2
      and _rules == [(10, "R3"), (11, "R2")],
      f"{r['outcome']} {_closed} {_rules}")

# ------------------------------------------------------------- T6 purge_plan

plan = purge_plan({1, 2, 3}, [
    {"table": "fact", "row_id": 1, "span_ids": {1}},
    {"table": "fact", "row_id": 2, "span_ids": {2, 7}},
    {"table": "fact", "row_id": 3, "span_ids": {8}},
    {"table": "fact", "row_id": 4, "span_ids": set()},
])
check("T6a wholly-owned rows are deleted", plan["delete"] == [("fact", 1)], str(plan["delete"]))
check("T6b partly-owned rows are recomputed", plan["recompute"] == [("fact", 2)], str(plan["recompute"]))
check("T6c unrelated and span-less rows are untouched",
      ("fact", 3) not in plan["delete"] + plan["recompute"]
      and ("fact", 4) not in plan["delete"] + plan["recompute"], str(plan))

# -------------------------------------------------------------- T7 is_person

check("T7a three full days is a person", is_person({"d1": 5, "d2": 5, "d3": 5}) is True)
check("T7b a short third day is not", is_person({"d1": 5, "d2": 5, "d3": 4}) is False)
check("T7c two heavy days is not", is_person({"d1": 9, "d2": 9}) is False)
check("T7d no days is not", is_person({}) is False)
check("T7e the rule constants are the design's", MIN_DAYS == 3 and MIN_SPANS_PER_DAY == 5)

# ---------------------------------------------------------------- T8 ranker

check("T8a recency_weight(0, decays) == 1.0", close_to(recency_weight(0, True), 1.0, 1e-9))
check("T8b recency_weight(90, decays) == 0.5", close_to(recency_weight(90, True), 0.5, 1e-9))
check("T8c recency_weight(180, decays) == 0.25", close_to(recency_weight(180, True), 0.25, 1e-9))
check("T8d recency_weight(400, no decay) == 1.0", close_to(recency_weight(400, False), 1.0, 1e-9))
check("T8e the half-life is 90 days", close_to(HALF_LIFE_DAYS, 90.0))
check("T8f the source weights are the design's",
      W_SOURCE["stated_owner"] == 1.0 and W_SOURCE["stated_other"] == 0.8 and W_SOURCE["inferred"] == 0.6)
check("T8g a stated profile fact does not decay at 400 days",
      close_to(score(1.0, "stated_owner", 400.0, 1.0, True), 1.0, 1e-9),
      str(score(1.0, "stated_owner", 400.0, 1.0, True)))

NOW = "2026-09-06T00:00:00"


def rrow(row_id, source_kind, age_days, table="fact", lane_score=1.0, conf=1.0, recorded_at=None):
    import datetime as _dt
    ts = _dt.datetime.fromisoformat(NOW) - _dt.timedelta(days=age_days)
    return {"row_id": row_id, "table": table, "lane_score": lane_score, "source_kind": source_kind,
            "newest_span_at": ts.isoformat(), "confidence": conf,
            "recorded_at": recorded_at or ts.isoformat()}


# T8h-T8j originally drove `rank` with equal lane_scores and asserted that the source and recency
# weights decided the order. MS1a moved that behaviour into `lane_order` by design (the weights now
# order a lane's own candidates; `rank` merges the fused relevance and multiplies by nothing), so
# these are retargeted to lane_order with the same intent. See T23h/T23i for the new contract.
_lo = lane_order([rrow(1, "stated_owner", 0), rrow(2, "inferred", 0)], NOW)
check("T8h inside a lane, a stated row leads an inferred one below it",
      [r["row_id"] for r in _lo] == [1, 2] and close_to(_lo[0]["wscore"], 1.0, 1e-9)
      and close_to(_lo[1]["wscore"], 0.3, 1e-9),
      str([(r["row_id"], round(r["wscore"], 4)) for r in _lo]))

_lo = lane_order([rrow(1, "inferred", 180), rrow(2, "inferred", 0)], NOW)
check("T8i recency can lift a fresher row past one rank of decay",
      [r["row_id"] for r in _lo] == [2, 1]
      and close_to(_lo[0]["wscore"], 0.3, 1e-9) and close_to(_lo[1]["wscore"], 0.15, 1e-9),
      str([(r["row_id"], round(r["wscore"], 4)) for r in _lo]))

_lo = lane_order([rrow(1, "stated_owner", 400)], NOW)
check("T8j a stated fact does not decay, even at 400 days",
      close_to(_lo[0]["wscore"], 1.0, 1e-9), str(_lo[0]["wscore"]))

# ================================================================ the store
# Every store check runs on an in-memory SQLite database. Nothing is written to disk and no
# recording, transcript or embedding exists at MS0 - the spans are literal strings written here.

from jarvis_memory import store as store_mod  # noqa: E402
from jarvis_memory.store import MemoryStore  # noqa: E402

STORE_TABLES = ("recording", "span", "cluster", "person", "event", "event_span", "fact",
                "fact_span", "edge", "edge_span", "preference", "preference_span",
                "style_snapshot", "audit", "embedding", "fact_fts", "span_fts")


def fresh():
    """A store with the owner (cluster 1) bound and a second cluster present."""
    st = MemoryStore(":memory:")
    c1 = st.add_cluster()
    c2 = st.add_cluster()
    owner = st.bind_owner(c1, "sam")
    return st, c1, c2, owner


def add_day(st, date, cluster, texts):
    """One recording per day, so said_at lands on the date asked for."""
    rec = st.add_recording(f"sha-{date}-{cluster}", f"{date}T08:00:00", 3600.0, "headset")
    return [st.add_span(rec, i * 10.0, i * 10.0 + 5.0, cluster, t, 0.9)
            for i, t in enumerate(texts)]


def fact_cand(owner, span_ids, object_text, object_norm, said_at, predicate="person.lives_in",
              source_kind="stated_owner", speaker=1, **over):
    c = dict(predicate_id=predicate, subject={"kind": "person", "id": owner},
             object=object_text, object_norm=object_norm, source_kind=source_kind,
             speaker_cluster=speaker, span_ids=list(span_ids), about_time=None,
             relation_id=None, polarity=None, strength=None, ended=False, said_at=said_at)
    c.update(over)
    return c


def _fact_row_t(st, row_id):
    """The fact row behind a query hit — query results carry the row id, not the columns."""
    r = st.conn.execute(
        "select subject_id, object_text, object_norm, predicate_id from fact where id=?",
        (row_id,)).fetchone()
    return dict(r) if r else None


# ---------------------------------------------------------------- T9 schema
st, c1, c2, owner = fresh()
have = {r[0] for r in st.conn.execute(
    "select name from sqlite_master where type in ('table','view')").fetchall()}
missing = [t for t in STORE_TABLES if t not in have]
check("T9a every table of the design exists", missing == [], f"missing {missing}")
check("T9b the owner person is kind owner",
      st.conn.execute("select kind from person where id=?", (owner,)).fetchone()[0] == "owner")

_real = store_mod.fts5_available
try:
    store_mod.fts5_available = lambda conn: False
    try:
        MemoryStore(":memory:")
        check("T9c a build without FTS5 is refused by name", False, "no exception raised")
    except Exception as exc:  # noqa: BLE001 - the message is the thing under test
        check("T9c a build without FTS5 is refused by name", "ENABLE_FTS5" in str(exc), str(exc))
finally:
    store_mod.fts5_available = _real
check("T9d the real build still opens", MemoryStore(":memory:") is not None)

# ------------------------------------------------------- T10 ingest supersede
st, c1, c2, owner = fresh()
s_d1 = add_day(st, "2026-03-01", c1, ["we live in brisbane now"])
s_d9 = add_day(st, "2026-03-09", c1, ["we moved to sydney"])
r1 = st.ingest(fact_cand(owner, s_d1, "Brisbane", "brisbane", "2026-03-01T08:00:00"))
r2 = st.ingest(fact_cand(owner, s_d9, "Sydney", "sydney", "2026-03-09T08:00:00"))
check("T10a first fact appends", r1["outcome"] == "append", str(r1))
check("T10b the later fact supersedes", r2["outcome"] == "supersede", str(r2))
cur = st.current("fact", subject_kind="person", subject_id=owner, predicate_id="person.lives_in")
check("T10c exactly one current row, the new one",
      len(cur) == 1 and cur[0]["object_norm"] == "sydney", str([c["object_norm"] for c in cur]))
old = st.conn.execute("select valid_to, superseded_by from fact where id=?", (r1["row_id"],)).fetchone()
check("T10d the superseded row carries valid_to and superseded_by",
      old[0] is not None and old[1] == r2["row_id"], str(old))
aud = st.conn.execute(
    "select op, loser_id, rule from audit where target_table='fact' and op='supersede'").fetchall()
check("T10e exactly one supersede audit row naming the loser",
      len(aud) == 1 and aud[0][1] == r1["row_id"], str(aud))

# --------------------------------------------------------- T11 audit walker
check("T11a a clean store has no audit violations", st.audit_violations() == [],
      str(st.audit_violations()))
st.conn.execute("update fact set valid_to='2026-04-01' where id=?", (r2["row_id"],))
st.conn.commit()
v = st.audit_violations()
check("T11b a row closed behind the audit trail is caught",
      len(v) == 1 and v[0]["row_id"] == r2["row_id"] and v[0]["table"] == "fact", str(v))

# --------------------------------------------------------- T12 purge cascade
st, c1, c2, owner = fresh()
o_spans = add_day(st, "2026-03-01", c1, ["morning"])
p_spans = add_day(st, "2026-03-02", c2, ["one", "two"])
st.promote_persons()  # cluster 2 is not a person yet; the facts below are on the owner
f_only_c2 = st.ingest(fact_cand(owner, [p_spans[0]], "Cairns", "cairns", "2026-03-02T08:00:00",
                                predicate="person.habit", source_kind="inferred"))
f_mixed = st.ingest(fact_cand(owner, [p_spans[1], o_spans[0]], "Perth", "perth",
                              "2026-03-02T08:00:10", predicate="person.habit", source_kind="inferred"))
f_owner = st.ingest(fact_cand(owner, [o_spans[0]], "Hobart", "hobart", "2026-03-01T08:00:00",
                              predicate="person.habit", source_kind="inferred"))
res = st.purge_cluster(c2)
alive = {r[0] for r in st.conn.execute("select id from fact").fetchall()}
check("T12a the wholly-owned row is deleted", f_only_c2["row_id"] not in alive, str(sorted(alive)))
check("T12b the partly-owned row survives", f_mixed["row_id"] in alive, str(sorted(alive)))
check("T12c the unrelated row survives", f_owner["row_id"] in alive, str(sorted(alive)))
check("T12d the purged spans are gone from span",
      st.conn.execute("select count(*) from span where id in (?,?)", tuple(p_spans)).fetchone()[0] == 0)
check("T12e the purged spans are gone from span_fts",
      st.conn.execute("select count(*) from span_fts where rowid in (?,?)",
                      tuple(p_spans)).fetchone()[0] == 0)
check("T12f the purge is audited", len(res["audit_ids"]) >= 2, str(res))
check("T12g the purge leaves no audit violation", st.audit_violations() == [],
      str(st.audit_violations()))
check("T12h the return names spans, deletions and recomputations",
      res["spans"] == 2 and res["deleted"].get("fact") == 1 and res["recomputed"].get("fact") == 1,
      str(res))

# -------------------------------------------------------------- T13 promote
st, c1, c2, owner = fresh()
for d in ("2026-03-01", "2026-03-02", "2026-03-03"):
    add_day(st, d, c2, ["a", "b", "c", "d", "e"])
new_ids = st.promote_persons()
check("T13a three full days promotes exactly one cluster", len(new_ids) == 1, str(new_ids))
check("T13b the cluster now points at its person",
      st.conn.execute("select person_id from cluster where id=?", (c2,)).fetchone()[0] == new_ids[0])
check("T13c promotion is idempotent", st.promote_persons() == [])
st2, d1, d2, _own2 = fresh()
for d in ("2026-03-01", "2026-03-02"):
    add_day(st2, d, d2, ["a", "b", "c", "d", "e"])
check("T13d two full days is not enough", st2.promote_persons() == [])

# ---------------------------------------------------------------- T14 query
st, c1, c2, owner = fresh()
sp = add_day(st, "2026-03-01", c1, ["s1", "s2", "s3"])
st.ingest(fact_cand(owner, [sp[0]], "Brisbane", "brisbane", "2026-03-01T08:00:00"))
st.ingest(fact_cand(owner, [sp[1]], "Sydney", "sydney", "2026-03-01T08:00:10"))
st.ingest(fact_cand(owner, [sp[2]], "a nurse", "a nurse", "2026-03-01T08:00:20",
                    predicate="person.works_as"))
hits = st.query("where does sam live", k=5, now="2026-03-02T00:00:00")
top = hits[0] if hits else {}
check("T14a the top hit is the current lives_in fact",
      top.get("table") == "fact" and "sydney" in top.get("text", "").lower(),
      str([(h["table"], h["text"]) for h in hits]))
check("T14b the hit carries its span ids and their text",
      bool(top.get("span_ids")) and bool(top.get("spans")) and "text" in top["spans"][0],
      str(top.get("spans")))
check("T14c the superseded row is absent",
      not any("brisbane" in h.get("text", "").lower() and h["table"] == "fact" for h in hits),
      str([h["text"] for h in hits]))
# T14c alone is satisfied by the "valid_to is null" join filter, so it would still pass if the
# index were never pruned; a mutation run proved exactly that. This checks the OTHER mechanism -
# the superseded row really leaves the contentless FTS index - so both are held independently.
check("T14c2 the superseded row is gone from the full-text index itself",
      st.conn.execute("select count(*) from fact_fts where fact_fts match 'brisbane'"
                      ).fetchone()[0] == 0,
      str(st.conn.execute("select count(*) from fact_fts where fact_fts match 'brisbane'").fetchone()[0]))
hits2 = st.query("sam nurse", k=5, now="2026-03-02T00:00:00")
check("T14d a different question finds the works_as row",
      any(h["table"] == "fact" and "nurse" in h["text"].lower() for h in hits2),
      str([(h["table"], h["text"]) for h in hits2]))

# ------------------------------------------------------- T15 R5 from spans
st, c1, c2, owner = fresh()
dates = ["2026-03-0%d" % d for d in (1, 2, 3, 4, 5)]
per_day = {d: add_day(st, d, c2, ["a", "b", "c", "d", "e"]) for d in dates}
partner = st.promote_persons()[0]
support = [per_day[d][0] for d in dates]
edge_cand = dict(predicate_id="person.relation_to", subject={"kind": "person", "id": owner},
                 object=partner, object_norm="spouse", source_kind="inferred", speaker_cluster=c1,
                 span_ids=support, about_time=None, relation_id="spouse", polarity=None,
                 strength=None, ended=False, said_at="2026-03-05T08:00:00")
r = st.ingest(edge_cand)
check("T15a five supporting days give 0.8111",
      close_to(r.get("confidence", -1), 0.8111), str(r))
contra = add_day(st, "2026-03-06", c2, ["x", "y", "z", "p", "q"])
c2nd = dict(edge_cand)
c2nd["span_ids"] = [support[0]]
c2nd["contradicts"] = [contra[0]]
c2nd["said_at"] = "2026-03-06T08:00:00"
r2 = st.ingest(c2nd)
got = st.recompute_confidence("edge", r2["row_id"])
check("T15b one contradicting day drops it to 0.7364", close_to(got, 0.7364), str(got))
check("T15c the contradiction merged onto the same edge row",
      r2["row_id"] == r["row_id"], f"{r['row_id']} vs {r2['row_id']}")

# ------------------------------------------------- T16 preference routing
st, c1, c2, owner = fresh()
p1 = add_day(st, "2026-03-01", c1, ["i love spicy food"])
p2 = add_day(st, "2026-03-08", c1, ["spicy food is too much for me now"])
pref = dict(predicate_id="owner.prefers", subject={"kind": "person", "id": owner},
            object="spicy food", object_norm="spicy food", source_kind="stated_owner",
            speaker_cluster=c1, span_ids=p1, about_time=None, relation_id=None,
            polarity="likes", strength=2, ended=False, said_at="2026-03-01T08:00:00")
rp1 = st.ingest(pref)
check("T16a a preference is routed to the preference table",
      rp1["table"] == "preference" and st.conn.execute(
          "select count(*) from preference").fetchone()[0] == 1, str(rp1))
pref2 = dict(pref)
pref2.update(polarity="dislikes", span_ids=p2, said_at="2026-03-08T08:00:00")
rp2 = st.ingest(pref2)
cur = st.current("preference", person_id=owner, topic_norm="spicy food")
check("T16b the opposite polarity coexists rather than overwriting",
      rp2["outcome"] == "coexist" and len(cur) == 2,
      f"{rp2['outcome']} {[c['polarity'] for c in cur]}")
pref3 = dict(pref)
pref3.update(ended=True, span_ids=p2, said_at="2026-03-08T09:00:00")
rp3 = st.ingest(pref3)
cur = st.current("preference", person_id=owner, topic_norm="spicy food")
check("T16c an owner statement that it ended closes just that row",
      rp3["outcome"] == "close" and [c["polarity"] for c in cur] == ["dislikes"],
      f"{rp3['outcome']} {[c['polarity'] for c in cur]}")
check("T16d the close is audited",
      st.conn.execute("select count(*) from audit where target_table='preference' and op='close'"
                      ).fetchone()[0] == 1)
check("T16e no audit violations after the preference sequence",
      st.audit_violations() == [], str(st.audit_violations()))

# ======================================================= T17 the benchmark corpus
# The corpus is what every MS0 number is measured on, so its determinism and the transfer set's
# word-disjointness are checks, not assumptions: a scenario sharing one word with its preference
# would let the full-text lane score a hit for the wrong reason and quietly inflate the number the
# design says only the embedding lane can earn.

import json as _json  # noqa: E402

from jarvis_memory.bench.corpus import (  # noqa: E402
    SYNONYM_SCENARIOS, generate_household,
)

hh_a = generate_household(1)
hh_b = generate_household(1)
check("T17a the corpus is deterministic for a seed",
      _json.dumps(hh_a, sort_keys=True) == _json.dumps(hh_b, sort_keys=True))
check("T17b different seeds give different households",
      _json.dumps(generate_household(2), sort_keys=True) != _json.dumps(hh_a, sort_keys=True))

sets_a = hh_a["sets"]
check("T17c the update set has at least six questions", len(sets_a["update"]) >= 6,
      str(len(sets_a["update"])))
check("T17d the coexisting set has at least three", len(sets_a["coexist"]) >= 3,
      str(len(sets_a["coexist"])))
check("T17e the transfer set has at least eight", len(sets_a["transfer"]) >= 8,
      str(len(sets_a["transfer"])))
check("T17f the relation set names the spouse edge both ways",
      len(sets_a["relations"]) >= 2
      and {(r["from"], r["to"]) for r in sets_a["relations"]} == {("owner", "partner"),
                                                                  ("partner", "owner")},
      str(sets_a["relations"]))
check("T17g the growth filler is 30x the gold candidates",
      len(sets_a["growth_filler"]) == 30 * len(hh_a["candidates"]),
      f"{len(sets_a['growth_filler'])} vs 30*{len(hh_a['candidates'])}")

bad_pairs = []
for _topic, _scenarios in SYNONYM_SCENARIOS.items():
    tw = set(_topic.lower().split())
    for _q in _scenarios:
        shared = tw & set(_q.lower().split())
        if shared:
            bad_pairs.append((_topic, _q, sorted(shared)))
check("T17h every transfer scenario shares no word with its preference",
      bad_pairs == [], str(bad_pairs))
check("T17i the coexisting set carries a value the owner ended",
      any(c["ended_object_norm"] for c in sets_a["coexist"]),
      str([c["ended_object_norm"] for c in sets_a["coexist"]]))

_days_by_cluster = {}
for _sp in hh_a["spans"]:
    _days_by_cluster.setdefault(_sp["cluster"], {}).setdefault(_sp["day"], 0)
    _days_by_cluster[_sp["cluster"]][_sp["day"]] += 1
_partner_full = [d for d, n in _days_by_cluster.get(2, {}).items() if n >= 5]
check("T17j the partner speaks five or more times on six or more days",
      len(_partner_full) >= 6, str(sorted(_partner_full)))
check("T17k the visitor is heard on exactly two days",
      len(_days_by_cluster.get(3, {})) == 2, str(sorted(_days_by_cluster.get(3, {}))))

# ============================================ MS0.1 the predicate-aware lane
# The hint is a RULE over a static, human-reviewed vocabulary - the K-b instinct applied to
# retrieval: it selects a predicate the registry already types, and never invents one. Its whole
# job is to stop a diluted predicate word from letting the shorter of two facts about the same
# person win, which is what cost MS0 its growth band.

from jarvis_memory.registry import QUERY_VOCAB  # noqa: E402
from jarvis_memory.retrieve import predicate_hint, tokens  # noqa: E402

# ------------------------------------------------------------------ T18 the hint
for _q, _want in (
    ("where does alex live", "person.lives_in"),
    ("which city does alex live in", "person.lives_in"),
    ("what does alex do for work", "person.works_as"),
    ("what job does alex work as", "person.works_as"),
    ("what habits does sam have", "person.habit"),
    ("what does sam like", "owner.prefers"),
    ("who is alex married to", "person.relation_to"),
    ("tell me about alex", None),
    ("does alex live near where he works", None),      # two predicates -> unrestricted
    ("", None),
    ("WHERE DOES ALEX LIVE?", "person.lives_in"),      # case and punctuation
):
    _got = predicate_hint(_q)
    check(f"T18 hint({_q!r}) -> {_want}", _got == _want, f"got {_got!r}")

check("T18l every vocabulary key is a real predicate",
      all(k in PREDICATES for k in QUERY_VOCAB), str(sorted(set(QUERY_VOCAB) - set(PREDICATES))))
_overlaps = []
_keys = sorted(QUERY_VOCAB)
for _i in range(len(_keys)):
    for _j in range(_i + 1, len(_keys)):
        _both = QUERY_VOCAB[_keys[_i]] & QUERY_VOCAB[_keys[_j]]
        if _both:
            _overlaps.append((_keys[_i], _keys[_j], sorted(_both)))
check("T18m the nine vocabulary sets are pairwise disjoint", _overlaps == [], str(_overlaps))
check("T18n tokens lower-cases and drops punctuation",
      tokens("Where does Alex live?") == ["where", "does", "alex", "live"],
      str(tokens("Where does Alex live?")))

# --------------------------------- T19 the MS0 F2 collision, resolved in the store
st, c1, c2, owner = fresh()
_h = add_day(st, "2026-03-01", c1, ["i cycle in most days"])
_w = add_day(st, "2026-03-03", c1, ["i am a pharmacist now"])
st.ingest(fact_cand(owner, _h, "cycles to work", "cycles to work", "2026-03-01T08:00:00",
                    predicate="person.habit"))
st.ingest(fact_cand(owner, _w, "pharmacist", "pharmacist", "2026-03-03T08:00:00",
                    predicate="person.works_as"))
_hits = st.query("what does sam do for work", k=5, now="2026-03-04T00:00:00")
_top = _hits[0] if _hits else {}
_row = _fact_row_t(st, _top["row_id"]) if _top.get("table") == "fact" else None
_off = st.query("what does sam do for work", k=5, now="2026-03-04T00:00:00", predicate_hint=None)
_offtop = _off[0] if _off else {}
_offrow = _fact_row_t(st, _offtop["row_id"]) if _offtop.get("table") == "fact" else None
check("T19 a work question reaches the works_as fact, not the habit that says 'work'",
      _row is not None and _row["object_text"] == "pharmacist",
      f"hint-on top={_top.get('table')} {_row and _row['object_text']!r} | "
      f"REPORTED hint-off top={_offtop.get('table')} {_offrow and _offrow['object_text']!r}")

# ------------------------------------------------- T20 preferences in the index
st, c1, c2, owner = fresh()
_p1 = add_day(st, "2026-03-01", c1, ["i really do enjoy spicy food"])
_p2 = add_day(st, "2026-03-08", c1, ["not any more"])


def _pref(span_ids, said_at, **over):
    d = dict(predicate_id="owner.prefers", subject={"kind": "person", "id": owner},
             object="spicy food", object_norm="spicy food", source_kind="stated_owner",
             speaker_cluster=c1, span_ids=list(span_ids), about_time=None, relation_id=None,
             polarity="likes", strength=2, ended=False, said_at=said_at)
    d.update(over)
    return d


_rp = st.ingest(_pref(_p1, "2026-03-01T08:00:00"))
_hits = st.query("what does sam like", k=5, now="2026-03-02T00:00:00")
_top = _hits[0] if _hits else {}
check("T20a a preference is found by a preference question and comes first",
      _top.get("table") == "preference" and "spicy food" in _top.get("text", "").lower()
      and bool(_top.get("span_ids")),
      f"{_top.get('table')} {_top.get('text')!r} spans={_top.get('span_ids')}")
check("T20b it is in the preference index",
      st.conn.execute("select count(*) from pref_fts where pref_fts match 'spicy'"
                      ).fetchone()[0] == 1)
st.ingest(_pref(_p2, "2026-03-08T08:00:00", ended=True))
_hits = st.query("what does sam like", k=5, now="2026-03-09T00:00:00")
check("T20c the ended preference is gone from the results",
      not any(h["table"] == "preference" for h in _hits),
      str([(h["table"], h["text"]) for h in _hits]))
check("T20d and gone from the index itself (the T14c2 lesson)",
      st.conn.execute("select count(*) from pref_fts where pref_fts match 'spicy'"
                      ).fetchone()[0] == 0,
      str(st.conn.execute("select count(*) from pref_fts where pref_fts match 'spicy'").fetchone()[0]))
check("T20e no audit violations before the purge", st.audit_violations() == [],
      str(st.audit_violations()))
st.purge_cluster(c1)
check("T20f the purge removes the preference row",
      st.conn.execute("select count(*) from preference").fetchone()[0] == 0)
check("T20g and its index entry",
      st.conn.execute("select count(*) from pref_fts").fetchone()[0] == 0)
check("T20h no audit violations after the purge", st.audit_violations() == [],
      str(st.audit_violations()))

# T20i - the same hazard one table over, latent since MS0: purging a SUPERSEDED fact used to ask
# FTS5 to delete a rowid that close had already removed, which corrupts a contentless index
# ("database disk image is malformed"). MS0's T12 only ever purged current facts.
st, c1, c2, owner = fresh()
_s1 = add_day(st, "2026-03-01", c1, ["brisbane for now"])
_s2 = add_day(st, "2026-03-05", c1, ["sydney now"])
st.ingest(fact_cand(owner, _s1, "Brisbane", "brisbane", "2026-03-01T08:00:00"))
st.ingest(fact_cand(owner, _s2, "Sydney", "sydney", "2026-03-05T08:00:00"))
try:
    _res = st.purge_cluster(c1)
    _ok = (st.conn.execute("select count(*) from fact").fetchone()[0] == 0
           and st.conn.execute("select count(*) from fact_fts").fetchone()[0] == 0
           and st.query("where does sam live", k=5, now="2026-03-06T00:00:00") == [])
    check("T20i purging a superseded fact leaves a readable index", _ok, str(_res))
except Exception as _exc:  # noqa: BLE001 - a corrupt index raises here
    check("T20i purging a superseded fact leaves a readable index", False, repr(_exc))

# --------------------------------------------- T21 the negative control, in process
from jarvis_memory.bench import harness as _harness  # noqa: E402

_off_hh = _harness.run_household(2, 14, predicate_hint=False)
_on_hh = _harness.run_household(2, 14, predicate_hint=True)
check("T21a hint OFF reproduces MS0's seed-2 update accuracy (0.875)",
      close_to(_off_hh["update_acc"], 0.875), str(_off_hh["update_acc"]))
check("T21b hint OFF reproduces MS0's seed-2 growth drop (37.5)",
      close_to(_off_hh["growth_drop_points"], 37.5), str(_off_hh["growth_drop_points"]))
check("T21c hint ON lifts seed-2 update accuracy to 1.0",
      close_to(_on_hh["update_acc"], 1.0), str(_on_hh["update_acc"]))
check("T21d hint ON removes the growth drop entirely",
      close_to(_on_hh["growth_drop_points"], 0.0), str(_on_hh["growth_drop_points"]))
check("T21e coexisting recall is 1.0 either way",
      close_to(_off_hh["coexist_recall"], 1.0) and close_to(_on_hh["coexist_recall"], 1.0),
      f"{_off_hh['coexist_recall']} {_on_hh['coexist_recall']}")
check("T21f the hint touches retrieval only, never the write path",
      _off_hh["spouse_surfaced_day"] == _on_hh["spouse_surfaced_day"],
      f"{_off_hh['spouse_surfaced_day']} vs {_on_hh['spouse_surfaced_day']}")

# ================================================== MS1a the embedding lane
# Everything here runs on a DICTIONARY embedder with 4-dimensional vectors: no torch, no GPU, no
# model download, so CI's bare python3 runs it. The real Qwen embedder is exercised only by the
# benchmark runs, which are recorded in the log rather than asserted here.

from jarvis_memory import embed as embed_mod  # noqa: E402
from jarvis_memory.embed import (  # noqa: E402
    DictEmbedder, cosine, pack, topk, unpack,
)
from jarvis_memory.retrieve import RRF_K, fuse  # noqa: E402

# ---------------------------------------------------------------- T22 vectors
_v = [0.5, -0.25, 1.0, 0.0]
_blob = pack(_v)
check("T22a pack is 4 bytes per float", len(_blob) == 16, str(len(_blob)))
check("T22b pack/unpack round-trips", all(close_to(a, b, 1e-6) for a, b in zip(unpack(_blob, 4), _v)),
      str(unpack(_blob, 4)))
check("T22c cosine of a vector with itself is 1", close_to(cosine([1, 0, 0, 0], [1, 0, 0, 0]), 1.0, 1e-9))
check("T22d cosine of orthogonal vectors is 0", close_to(cosine([1, 0, 0, 0], [0, 1, 0, 0]), 0.0, 1e-9))
check("T22e a zero vector cosines to 0, never divides by zero",
      close_to(cosine([1, 0, 0, 0], [0, 0, 0, 0]), 0.0, 1e-9))
_tk = topk([1, 0, 0, 0], [("a", [1, 0, 0, 0]), ("b", [0.6, 0.8, 0, 0]), ("c", [0, 1, 0, 0])], 2)
check("T22f topk returns the two best by cosine, in order",
      len(_tk) == 2 and _tk[0][0] == "a" and _tk[1][0] == "b"
      and close_to(_tk[0][1], 1.0, 1e-6) and close_to(_tk[1][1], 0.6, 1e-6), str(_tk))
try:
    import numpy as _np  # noqa: F401
    _rows = [(i, [(i % 7) / 7.0, (i % 5) / 5.0, (i % 3) / 3.0, 1.0]) for i in range(40)]
    _q = [0.3, 0.5, 0.2, 0.8]
    _pure = embed_mod._topk_pure(_q, _rows, 10)
    _fast = topk(_q, _rows, 10)
    check("T22g the numpy path agrees with the pure path to 1e-6",
          [o for o, _ in _pure] == [o for o, _ in _fast]
          and all(close_to(a, b, 1e-6) for (_, a), (_, b) in zip(_pure, _fast)),
          f"{_pure[:3]} vs {_fast[:3]}")
except ImportError:
    print("NOTE T22g skipped - numpy is not importable in this interpreter (expected in CI)")

# ------------------------------------------------------------------- T23 fuse
check("T23a the RRF constant is the published 60", RRF_K == 60, str(RRF_K))
_f = fuse({"fts": ["a", "b"], "vec": ["b", "c"]})
check("T23b a row in two lanes sums its terms",
      close_to(_f["b"], 1.0 / 61 + 1.0 / 62, 1e-12), str(_f.get("b")))
check("T23c a row in one lane at rank 1", close_to(_f["a"], 1.0 / 61, 1e-12), str(_f.get("a")))
check("T23d a row in one lane at rank 2", close_to(_f["c"], 1.0 / 62, 1e-12), str(_f.get("c")))
check("T23e the two-lane row outranks both single-lane rows",
      _f["b"] > _f["a"] > _f["c"], str(sorted(_f.items(), key=lambda kv: -kv[1])))
check("T23f fuse of nothing is nothing", fuse({}) == {})
check("T23g an empty lane contributes nothing",
      fuse({"fts": ["a"], "vec": []}) == {"a": 1.0 / 61})

# ------------------------------------- T23h lane_order: the weights act INSIDE a lane
# The correction after MS1a's first attempt: R2's source rank and R6's decay order a lane's own
# candidates, where every competitor answers the same question. RRF then merges the ORDERINGS and
# is multiplied by nothing.
def _member(key, table, source_kind, newest_span_at, confidence=1.0, recorded_at="2026-03-01T00:00:00"):
    return {"key": key, "table": table, "source_kind": source_kind, "confidence": confidence,
            "recorded_at": recorded_at, "newest_span_at": newest_span_at}


_NOW23 = "2026-03-01T00:00:00"
_lane = lane_order([
    _member("A", "fact", "stated_other", _NOW23),
    _member("B", "fact", "stated_other", _NOW23),
    _member("C", "fact", "stated_other", _NOW23),
    _member("D", "fact", "stated_other", _NOW23),
    _member("E", "fact", "stated_owner", _NOW23),
], _NOW23)
check("T23h1 the rank-1 stated_other keeps the lane's top",
      _lane[0]["key"] == "A" and close_to(_lane[0]["wscore"], 0.8, 1e-9),
      str([(r["key"], round(r["wscore"], 4)) for r in _lane]))
check("T23h2 the rank-5 stated_owner scores 0.2 and does not jump the correct answer",
      [r["key"] for r in _lane][-1] == "E" and close_to(_lane[-1]["wscore"], 0.2, 1e-9),
      str([(r["key"], round(r["wscore"], 4)) for r in _lane]))
_lane2 = lane_order([
    _member("S", "span", "inferred", _NOW23),
    _member("F", "fact", "stated_owner", _NOW23),
], _NOW23)
check("T23h3 MS0.1's within-lane behaviour is preserved: the rank-1 span still leads a rank-2 fact",
      [r["key"] for r in _lane2] == ["S", "F"]
      and close_to(_lane2[0]["wscore"], 0.6, 1e-9) and close_to(_lane2[1]["wscore"], 0.5, 1e-9),
      str([(r["key"], round(r["wscore"], 4)) for r in _lane2]))

# ------------------------------------------------- T23i rank: relevance first, then the tiebreak
_r = rank([{"row_id": 1, "table": "span", "relevance": 1.0 / 61, "tiebreak": 0.6,
            "recorded_at": "2026-03-01T00:00:00"},
           {"row_id": 2, "table": "fact", "relevance": 1.0 / 61, "tiebreak": 1.0,
            "recorded_at": "2026-03-01T00:00:00"}], _NOW23)
check("T23i1 equal relevance breaks by the within-lane weighted score",
      [x["row_id"] for x in _r] == [2, 1], str([(x["row_id"], x["score"]) for x in _r]))
check("T23i2 rank publishes the fused relevance as the score, multiplied by nothing",
      close_to(_r[0]["score"], 1.0 / 61, 1e-12), str(_r[0]["score"]))
_r = rank([{"row_id": 1, "table": "fact", "relevance": 1.0 / 61, "tiebreak": 1.0,
            "recorded_at": "2026-03-01T00:00:00"},
           {"row_id": 2, "table": "fact", "relevance": 1.0 / 61, "tiebreak": 1.0,
            "recorded_at": "2026-03-05T00:00:00"}], _NOW23)
check("T23i3 equal relevance and tiebreak break by recorded_at, newest first",
      [x["row_id"] for x in _r] == [2, 1], str([x["row_id"] for x in _r]))
_r = rank([{"row_id": 1, "table": "fact", "relevance": 1.0 / 62, "tiebreak": 1.0,
            "recorded_at": "2026-03-01T00:00:00"},
           {"row_id": 2, "table": "span", "relevance": 1.0 / 61, "tiebreak": 0.6,
            "recorded_at": "2026-03-01T00:00:00"}], _NOW23)
check("T23i4 relevance outranks the tiebreak - the fused order is the answer",
      [x["row_id"] for x in _r] == [2, 1], str([x["row_id"] for x in _r]))

# ------------------------------------------- T24 the vector lane in the store
st, c1, c2, owner = fresh()
_sp = add_day(st, "2026-03-01", c1, ["quiet morning", "nothing much"])
st.ingest(fact_cand(owner, [_sp[0]], "Sydney", "sydney", "2026-03-01T08:00:00"))
st.ingest(fact_cand(owner, [_sp[1]], "a nurse", "a nurse", "2026-03-01T08:00:10",
                    predicate="person.works_as"))
st.ingest(dict(predicate_id="owner.prefers", subject={"kind": "person", "id": owner},
               object="spicy food", object_norm="spicy food", source_kind="stated_owner",
               speaker_cluster=c1, span_ids=[_sp[0]], about_time=None, relation_id=None,
               polarity="likes", strength=2, ended=False, said_at="2026-03-01T08:00:20"))

_PREF_TEXT = st._pref_fts_text(owner, "likes", "spicy food")
_LIVES_TEXT = st._fact_fts_text(owner, "person", "person.lives_in", "Sydney")
_WORKS_TEXT = st._fact_fts_text(owner, "person", "person.works_as", "a nurse")
_TABLE = {
    _PREF_TEXT: [1.0, 0.0, 0.0, 0.0],
    _LIVES_TEXT: [0.0, 1.0, 0.0, 0.0],
    _WORKS_TEXT: [0.0, 0.0, 1.0, 0.0],
    "planning dinner for our anniversary": [1.0, 0.0, 0.0, 0.0],
    "what does sam do for a living": [0.0, 0.0, 1.0, 0.0],
}
E = DictEmbedder(_TABLE, 4)

_res = st.embed_pending(E)
check("T24a embed_pending embeds every current belief and every span",
      _res["fact"] == 2 and _res["preference"] == 1 and _res["span"] == len(_sp),
      str(_res))
_again = st.embed_pending(E)
check("T24b a second pass embeds nothing new",
      _again["fact"] == 0 and _again["preference"] == 0 and _again["span"] == 0, str(_again))

_hits = st.query("planning dinner for our anniversary", k=5, now="2026-03-02T00:00:00", embedder=E)
_top = _hits[0] if _hits else {}
check("T24c the preference comes first, found only by the vector lane",
      _top.get("table") == "preference" and _top.get("lanes") == {"vec": 1}
      and _top.get("cos") is not None and close_to(_top["cos"], 1.0, 1e-6),
      f"{_top.get('table')} lanes={_top.get('lanes')} cos={_top.get('cos')}")
check("T24d without the embedder that question finds nothing",
      st.query("planning dinner for our anniversary", k=5, now="2026-03-02T00:00:00",
               embedder=None) == [],
      str(st.query("planning dinner for our anniversary", k=5, now="2026-03-02T00:00:00")))

# T24e - the hint restricts the full-text lane, never the vector lane
check("T24e0 the question really does hint lives_in",
      predicate_hint("what does sam do for a living") == "person.lives_in",
      str(predicate_hint("what does sam do for a living")))
_hits = st.query("what does sam do for a living", k=5, now="2026-03-02T00:00:00", embedder=E)
_works = [h for h in _hits if h["table"] == "fact"
          and _fact_row_t(st, h["row_id"])["predicate_id"] == "person.works_as"]
check("T24e the works_as row still arrives, by meaning, past a hint that excluded it",
      len(_works) == 1 and "vec" in _works[0]["lanes"],
      str([(h["table"], h.get("lanes")) for h in _hits]))

# ------------------------------------------------------------- T25 lifecycle
_sp2 = add_day(st, "2026-03-09", c1, ["we moved"])
_r2 = st.ingest(fact_cand(owner, _sp2, "Brisbane", "brisbane", "2026-03-09T08:00:00"))
check("T25a the superseding fact closed the old one", _r2["outcome"] == "supersede", str(_r2))
_closed_id = _r2["closed"][0]
check("T25b the closed row's embedding is gone",
      st.conn.execute("select count(*) from embedding where owner_table='fact' and owner_id=?",
                      (_closed_id,)).fetchone()[0] == 0)
_hits = st.query("what does sam do for a living", k=5, now="2026-03-10T00:00:00", embedder=E)
check("T25c the closed row is absent from the results",
      not any(h["table"] == "fact" and h["row_id"] == _closed_id for h in _hits),
      str([(h["table"], h["row_id"]) for h in _hits]))
check("T25d no audit violations", st.audit_violations() == [], str(st.audit_violations()))
_before = st.conn.execute("select count(*) from embedding").fetchone()[0]
st.purge_cluster(c1)
check("T25e the purge removed the embeddings with the rows",
      st.conn.execute("select count(*) from embedding").fetchone()[0] < _before
      and st.conn.execute("select count(*) from embedding where owner_table='span'"
                          ).fetchone()[0] == 0,
      f"before {_before} after {st.conn.execute('select count(*) from embedding').fetchone()[0]}")
check("T25f no audit violations after the purge", st.audit_violations() == [],
      str(st.audit_violations()))

# --------------------------------- T25g the no-embedder path keeps MS0.1's order
st, c1, c2, owner = fresh()
_sp = add_day(st, "2026-03-01", c1, ["sam lives in sydney indeed"])
st.ingest(fact_cand(owner, _sp, "Sydney", "sydney", "2026-03-01T08:00:00"))
_hits = st.query("where does sam live", k=5, now="2026-03-02T00:00:00", embedder=None)
_order = [(h["table"], h["row_id"]) for h in _hits]
check("T25g with no embedder the fact still outranks the span it came from",
      len(_order) >= 2 and _order[0][0] == "fact" and _order[1][0] == "span", str(_order))

print(f"\n{CHECKS - FAILS}/{CHECKS} checks passed")
sys.exit(1 if FAILS else 0)
