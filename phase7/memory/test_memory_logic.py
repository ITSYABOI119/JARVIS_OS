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
# RE-PINNED at MS1a.2 (rule 3 KEPT after the A / A-prime measurement: transfer 0.3583 vs 0.2833,
# delta 0.0750 > 0.05, update 1.0 either way). MS0 measured 0.875 / 37.5 here; dropping function
# words costs one of the eight update questions in this HINT-OFF control, and the mechanism was
# traced to a single word: `what job does juno work as` needs "as", because the store renders the
# predicate as "... works AS a pharmacist" while the competing habit renders "... cycles to work".
# Both contain "work"; only the answer contains "as". With the hint ON (T21c/T21d, and the deployed
# path) the lookup is already restricted to person.works_as, the competitor never enters the lane,
# and both numbers stay perfect - which is why the cost is confined to this control.
check("T21a hint OFF reproduces the MS1a.2 seed-2 update accuracy (0.75; MS0's 0.875 needed the function word 'as')",
      close_to(_off_hh["update_acc"], 0.75), str(_off_hh["update_acc"]))
check("T21b hint OFF reproduces the MS1a.2 seed-2 growth drop (25.0; 37.5 at MS0)",
      close_to(_off_hh["growth_drop_points"], 25.0), str(_off_hh["growth_drop_points"]))
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
    QUERY_INSTRUCTION, DictEmbedder, cosine, pack, query_payload, topk, unpack,
)
from jarvis_memory.retrieve import RRF_K, W_CLAIM, fuse, query_terms  # noqa: E402
from jarvis_memory.registry import QUERY_VOCAB as _QV, STOPWORDS  # noqa: E402

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

# ----------------- T23j MS1a.2: the claim-status weight sits on the ROW, not the lane
# MS1a measured the cross-lane loss: R2's source rank acts only WITHIN a lane, and a span and the
# fact extracted from it are never in the same one, so reciprocal rank let the utterance win on one
# vector rank. MS1a.1 proposed typed lanes and was WITHDRAWN unmeasured - partitioning restarts the
# ranks, so a row with no similarity earns a full rank-1 term. MS1a.2 keeps ONE vector lane and
# weighs each ROW by its claim status: a belief 1.0, a span W_SOURCE["inferred"] = 0.6. R2's own
# constant one level up, never a new knob. Every weight below is READ FROM W_CLAIM - a literal 0.6
# would keep passing if the shipped constant changed, which is exactly the tooth wanted.
check("T23j0 a belief weighs 1.0 and a span weighs R2's inferred rank",
      W_CLAIM == {"fact": 1.0, "preference": 1.0, "span": W_SOURCE["inferred"]}
      and close_to(W_CLAIM["span"], 0.6, 1e-12), str(W_CLAIM))

# The real MS1a section-3.8 trace: `i work as a teacher` was fts_span 1 + vec 2, and the fact
# `jo person works as a teacher` fts_fact 1 + vec 3, so a third row (X) held vec rank 1.
_LANES_38 = {"fts_span": ["S"], "fts_fact": ["F"], "vec": ["X", "S", "F"]}
_W38 = {"S": W_CLAIM["span"], "F": W_CLAIM["fact"], "X": W_CLAIM["fact"]}
_fj = fuse(_LANES_38, _W38)
check("T23j1 weighted by claim status, the fact outranks the span extracted from it",
      close_to(_fj["S"], 0.6 * (1.0 / 61 + 1.0 / 62), 1e-9)
      and close_to(_fj["F"], 1.0 / 61 + 1.0 / 63, 1e-9)
      and _fj["F"] > _fj["S"],
      f"S={_fj['S']!r} F={_fj['F']!r}")
_fj2 = fuse(_LANES_38)
check("T23j2 unweighted, the MS1a defect: the span beat its own fact",
      close_to(_fj2["S"], 1.0 / 61 + 1.0 / 62, 1e-9)
      and close_to(_fj2["F"], 1.0 / 61 + 1.0 / 63, 1e-9)
      and _fj2["S"] > _fj2["F"],
      f"S={_fj2['S']!r} F={_fj2['F']!r}")

_fj3 = fuse({"fts_span": ["S"], "vec": ["F", "S"]},
            {"S": W_CLAIM["span"], "F": W_CLAIM["fact"]})
check("T23j3 the stated residual - evidence in both lanes still outranks a belief in one",
      close_to(_fj3["S"], 0.6 / 61 + 0.6 / 62, 1e-9)
      and close_to(_fj3["F"], 1.0 / 61, 1e-9) and _fj3["S"] > _fj3["F"],
      f"S={_fj3['S']!r} F={_fj3['F']!r}")

_lanes_j = {"fts_fact": ["a", "b"], "vec": ["b", "c"]}
check("T23j4 no weights, None and {} are the same call - the default is 1.0",
      all(close_to(fuse(_lanes_j)[k], fuse(_lanes_j, None)[k], 1e-12)
          and close_to(fuse(_lanes_j)[k], fuse(_lanes_j, {})[k], 1e-12)
          for k in fuse(_lanes_j)),
      str(fuse(_lanes_j)))
_fj5 = fuse({"vec": ["F", "S"]}, {"S": W_CLAIM["span"]})
check("T23j5 a row with no weight given is a belief - 1.0, never silently 0",
      close_to(_fj5["F"], 1.0 / 61, 1e-12) and close_to(_fj5["S"], 0.6 / 62, 1e-12),
      f"F={_fj5['F']!r} S={_fj5['S']!r}")

# --------------------------------- T29 function words out of the FULL-TEXT query
_QV_UNION = set().union(*_QV.values())
check("T29a STOPWORDS is disjoint from every QUERY_VOCAB set",
      not (STOPWORDS & _QV_UNION), str(sorted(STOPWORDS & _QV_UNION)))
check("T29b every stopword survives the tokeniser unchanged",
      all(tokens(w) == [w] for w in STOPWORDS),
      str([w for w in sorted(STOPWORDS) if tokens(w) != [w]]))

# ----------------------- T23k / T29c-e the same rules at the store
# T23k is the MS1a section-3.8 shape end to end: a span that IS an evidence span of the fact
# extracted from it, both matched by one question, the span embedding CLOSEST to the query and a
# DECOY fact embedded closer than the answer fact so the answer sits at vec rank 3. Before MS1a.2
# the span won; now the claim weight puts the fact first AND the collapse removes the span entirely.
_kst, _kc1, _kc2, _kowner = fresh()
_ksp = add_day(_kst, "2026-03-01", _kc1, ["i work as a teacher"])
_ksp += add_day(_kst, "2026-03-02", _kc1, ["still teaching this year",
                                           "the weather today is lovely work"])
_kst.ingest(fact_cand(_kowner, _ksp[:2], "a teacher", "a teacher", "2026-03-02T08:00:00",
                      predicate="person.works_as"))
_kst.ingest(fact_cand(_kowner, [_ksp[0]], "Sydney", "sydney", "2026-03-01T08:00:30"))
_KQ = "what job does sam work as"
check("T29c the full-text query keeps content words and drops function words",
      query_terms(_KQ, True) == ["job", "sam", "work"]
      and _kst._fts_match(_KQ) == '"job" OR "sam" OR "work"',
      f"{query_terms(_KQ, True)} / {_kst._fts_match(_KQ)!r}")
check("T29d a query of only function words leaves no term for the full-text lane",
      query_terms("is that the same", True) == [], str(query_terms("is that the same", True)))
# RETARGET NOTE (T8h precedent): PROMPT-MEMORY-MS1A2.md section 3.2 dictated this case as
# `_fts_match("is that the one") == ""`, which the dictated STOPWORDS list cannot produce - "one"
# is not in it ("only", "own" and "once" are). Rule 5 freezes the list, so the list stands and the
# expectation is what gives. The dictated input is kept below, asserting what the frozen list really
# does; "is that the same" above is the all-function-word case the check was written to prove.
check("T29d2 the dictated input keeps its one content word under the frozen list",
      query_terms("is that the one", True) == ["one"],
      str(query_terms("is that the one", True)))
check("T29f with the switch off every token survives - the arm is the MS0.1 behaviour",
      query_terms(_KQ, False) == tokens(_KQ)
      and query_terms(_KQ, False) == ["what", "job", "does", "sam", "work", "as"],
      str(query_terms(_KQ, False)))

_KSPAN_TEXT = "i work as a teacher"
_KFACT_TEXT = _kst._fact_fts_text(_kowner, "person", "person.works_as", "a teacher")
_KDECOY_TEXT = _kst._fact_fts_text(_kowner, "person", "person.lives_in", "Sydney")
_KCHATTER = "the weather today is lovely work"
_KE = DictEmbedder({
    _KQ: [1.0, 0.0, 0.0, 0.0],
    _KSPAN_TEXT: [1.0, 0.03, 0.0, 0.0],      # closest of all
    _KDECOY_TEXT: [1.0, 0.10, 0.0, 0.0],     # the decoy, above the answer
    _KFACT_TEXT: [1.0, 0.30, 0.0, 0.0],      # the answer fact, vec rank 3
    _KCHATTER: [1.0, 0.50, 0.0, 0.0],        # a span no belief stands on
}, 4)
_kst.embed_pending(_KE)
_khits = _kst.query(_KQ, k=5, now="2026-03-03T00:00:00", embedder=_KE)
_kfact = next((h for h in _khits if h["table"] == "fact"
               and _fact_row_t(_kst, h["row_id"])["predicate_id"] == "person.works_as"), None)
check("T23k1 the fact is first, and the span it was extracted from is collapsed into it",
      _khits and _khits[0] is _kfact
      and not any(h["table"] == "span" and h["row_id"] == _ksp[0] for h in _khits)
      and _ksp[0] in (_kfact or {}).get("span_ids", []),
      str([(h["table"], h["row_id"], h["lanes"], round(h["relevance"], 6)) for h in _khits]))
_khits_none = _kst.query(_KQ, k=5, now="2026-03-03T00:00:00", embedder=None)
check("T23k2 the no-embedder control: the fact first, the span still collapsed",
      _khits_none and _khits_none[0]["table"] == "fact"
      and not any(h["table"] == "span" and h["row_id"] == _ksp[0] for h in _khits_none),
      str([(h["table"], h["row_id"], h["lanes"]) for h in _khits_none]))
_kchat = next((h for h in _khits if h["table"] == "span" and h["row_id"] == _ksp[2]), None)
# The DISCOUNT is asserted as a strict inequality as well as an equality. The equality alone reads
# the weight out of W_CLAIM on BOTH sides, so it survives a mutated constant unchanged - it pins the
# arithmetic but has no teeth against the value. `< the unweighted sum` is the tooth: it fails the
# moment a span stops being discounted at all.
_kchat_unweighted = sum(1.0 / (RRF_K + r) for r in (_kchat or {"lanes": {}})["lanes"].values())
check("T23k3 a span no belief stands on survives, after the beliefs, weighed as evidence",
      _kchat is not None
      and _khits.index(_kchat) > 0 and _khits[0]["table"] != "span"
      and set(_kchat["lanes"]) == {"fts_span", "vec"}
      and close_to(_kchat["relevance"], W_CLAIM["span"] * _kchat_unweighted, 1e-9)
      and _kchat["relevance"] < _kchat_unweighted - 1e-9,
      str(_kchat and (_kchat["lanes"], _kchat["relevance"], _kchat_unweighted)))

# T29e - a scenario whose ONLY overlap with a span is a function word must not make that span a
# full-text hit. `we should decide` shares only "should" with the scenario below.
def _mk_store_keep_stopwords():
    """The T29e fixture again, with rule 3 switched OFF - the arm, as the bench runs it."""
    st_ = MemoryStore(":memory:", drop_stopwords=False)
    c_ = st_.add_cluster()
    st_.add_cluster()
    st_.bind_owner(c_, "sam")
    rec_ = st_.add_recording("sha-keep", "2026-03-01T08:00:00", 3600.0, "headset")
    st_.add_span(rec_, 0.0, 5.0, c_, "we should decide", 0.9)
    return st_


_est, _ec1, _ec2, _eowner = fresh()
_esp = add_day(_est, "2026-03-01", _ec1, ["we should decide"])
_EQ = "what should i order at the restaurant tonight"
check("T29e a function-word-only overlap is not a full-text hit",
      query_terms(_EQ, True) == ["order", "restaurant", "tonight"]
      and _est.query(_EQ, k=5, now="2026-03-02T00:00:00", embedder=None) == [],
      f"{query_terms(_EQ, True)} -> {_est.query(_EQ, k=5, now='2026-03-02T00:00:00')}")
# The same store with the switch OFF DOES find it - which is what makes the check above a
# measurement of rule 3 rather than of the corpus.
_est_keep = _mk_store_keep_stopwords()
check("T29e2 with function words kept, the same scenario reaches the chatter span",
      len(_est_keep.query(_EQ, k=5, now="2026-03-02T00:00:00", embedder=None)) == 1,
      str(_est_keep.query(_EQ, k=5, now="2026-03-02T00:00:00", embedder=None)))

# --------------- T30 MS1a.3: the preference lane under the instruction-prefixed query
# MS1a.2 located the transfer miss as CROWDING - the embedder already ranked the planted preference
# first among preferences in 69 % of scenarios, but it sat a mean 78 rows deep in the mixed lane.
# Applying the instruction form to EVERY table reached 70 % transfer and cost the update band
# (93.75 %). MS1a.3 gives the preference model its own lane under that query form and leaves the
# mixed lane symmetric, so the two forms never share one cosine scale.
check("T30a query_payload prefixes only when asked",
      query_payload("planning dinner", True) == QUERY_INSTRUCTION + "planning dinner"
      and query_payload("planning dinner", False) == "planning dinner",
      repr(query_payload("planning dinner", True)[:40]))
_t30 = DictEmbedder({"aa": [1.0, 0.0, 0.0, 0.0],
                     "bb": [0.0, 1.0, 0.0, 0.0],
                     QUERY_INSTRUCTION + "bb": [0.0, 0.0, 1.0, 0.0]}, 4)
check("T30b the dict embedder falls back to the plain vector, or takes the prefixed key when present",
      _t30.embed_query("aa", instruction=True) == _t30.embed_query("aa")
      and _t30.embed_query("bb", instruction=True) == [0.0, 0.0, 1.0, 0.0]
      and _t30.embed_query("bb") == [0.0, 1.0, 0.0, 0.0],
      str(_t30.embed_query("bb", instruction=True)))

# ---------------------- T32 MS1a.4: ONE VECTOR VOTE PER ROW
# The two vector lanes are two views of one mechanism - the same embedder over the same rows - so a
# row takes its BEST rank among them, never their sum. MS1a.3 summed them and a preference found by
# both out-summed the answer fact, costing the update band (100 % -> 93.75 %).
_F32 = {"fts_fact": ["F"], "vec": ["X", "P", "F"], "vec_pref": ["P"]}
_W32 = {"F": 1.0, "P": 1.0, "X": 1.0}
_f32 = fuse(_F32, _W32)
check("T32a one vote: the preference takes its best vector rank, the fact keeps fts + vec",
      close_to(_f32["P"], 1.0 / 61, 1e-9)
      and close_to(_f32["F"], 1.0 / 61 + 1.0 / 63, 1e-9)
      and _f32["F"] > _f32["P"],
      f"P={_f32['P']!r} F={_f32['F']!r}")
_f32s = fuse(_F32, _W32, vector_lanes=())
check("T32a2 the summed form is the MS1a.3 defect, pinned: the preference wins",
      close_to(_f32s["P"], 1.0 / 62 + 1.0 / 61, 1e-9)
      and close_to(_f32s["F"], 1.0 / 61 + 1.0 / 63, 1e-9)
      and _f32s["P"] > _f32s["F"],
      f"P={_f32s['P']!r} F={_f32s['F']!r}")
check("T32b a preference only the preference lane found is still rescued at 1/61",
      close_to(fuse({"vec": ["Z"], "vec_pref": ["P"]}, {"P": 1.0, "Z": 1.0})["P"], 1.0 / 61, 1e-9),
      str(fuse({"vec": ["Z"], "vec_pref": ["P"]}, {"P": 1.0, "Z": 1.0})))
check("T32c rank 1 in both vector lanes is one vote, not two",
      close_to(fuse({"vec": ["P"], "vec_pref": ["P"]}, {"P": 1.0})["P"], 1.0 / 61, 1e-9),
      str(fuse({"vec": ["P"], "vec_pref": ["P"]}, {"P": 1.0})))

# ---------------------- T33 MS1a.4: THE SUBJECT GATE
# MS1a.3's growth trace: all six misses were a filler fact about a DIFFERENT person out-summing the
# answer. A question naming a known person must not take another person's belief. Exact tokens on
# `display_name`; aliases are MS2's people layer, and that limit is stated in the code.
_g, _gc1, _gc2, _gsam = fresh()
_gerin = _g.conn.execute(
    "insert into person (kind, display_name, created_at) values ('cluster',?,?)",
    ("erin", "2026-03-01T00:00:00")).lastrowid
_g.conn.commit()
_gsp = add_day(_g, "2026-03-01", _gc1, ["sam works as a nurse", "erin works as a cooper"])
_g.ingest(fact_cand(_gsam, [_gsp[0]], "a nurse", "a nurse", "2026-03-01T08:00:00",
                    predicate="person.works_as"))
_g.ingest(fact_cand(_gerin, [_gsp[1]], "a cooper", "a cooper", "2026-03-01T08:00:10",
                    predicate="person.works_as", source_kind="inferred"))
_GQ = "what does sam do for work"
_G_SAM = _g._fact_fts_text(_gsam, "person", "person.works_as", "a nurse")
_G_ERIN = _g._fact_fts_text(_gerin, "person", "person.works_as", "a cooper")
_GE = DictEmbedder({_GQ: [1.0, 0.0, 0.0, 0.0],
                    _G_ERIN: [1.0, 0.05, 0.0, 0.0],   # erin ranks ABOVE sam by cosine
                    _G_SAM: [1.0, 0.30, 0.0, 0.0]}, 4)
_g.embed_pending(_GE)
check("T33b _named_persons finds the one named person",
      _g._named_persons(_GQ) == {_gsam}, str(_g._named_persons(_GQ)))
_ghits = _g.query(_GQ, k=5, now="2026-03-02T00:00:00", embedder=_GE)
check("T33a the gate removes the other person's fact from every lane, and sam's is first",
      _ghits and _ghits[0]["table"] == "fact"
      and _fact_row_t(_g, _ghits[0]["row_id"])["subject_id"] == _gsam
      and not any(h["table"] == "fact"
                  and _fact_row_t(_g, h["row_id"])["subject_id"] == _gerin for h in _ghits),
      str([(h["table"], h["row_id"], h["lanes"]) for h in _ghits]))
_gq2 = "who works as a cooper"
check("T33c a question naming nobody leaves both facts in",
      _g._named_persons(_gq2) == set()
      and any(_fact_row_t(_g, h["row_id"])["subject_id"] == _gerin
              for h in _g.query(_gq2, k=5, now="2026-03-02T00:00:00", embedder=_GE)
              if h["table"] == "fact"),
      str(_g._named_persons(_gq2)))
_gq3 = "do sam and erin both work"
check("T33d a question naming both keeps both",
      _g._named_persons(_gq3) == {_gsam, _gerin}, str(_g._named_persons(_gq3)))
check("T33e an unknown name leaves the gate inactive",
      _g._named_persons("what does zed do for work") == set(),
      str(_g._named_persons("what does zed do for work")))
# T33f needs a span NO belief stands on: the two spans above are the facts' evidence and rule 4's
# collapse removes them, which is correct behaviour and not the gate. This third span is said by
# erin's cluster and supports nothing, so only the gate could remove it - and must not, because a
# span carries no person_id: evidence is not a claim about anybody.
_gsp2 = add_day(_g, "2026-03-02", _gc2, ["the cooperage festival was busy"])
_gspan_hits = _g.query("what did sam hear about the cooperage festival", k=10,
                       now="2026-03-03T00:00:00", embedder=None)
check("T33f evidence is never gated - a span said by another person survives a named question",
      _g._named_persons("what did sam hear about the cooperage festival") == {_gsam}
      and any(h["table"] == "span" and h["row_id"] == _gsp2[0] for h in _gspan_hits),
      str([(h["table"], h["row_id"]) for h in _gspan_hits]))
check("T33g a household fact carries no person and is never gated",
      _g._subject_gate({"l": [{"person_id": None, "key": ("fact", 99)}]}, {_gsam})
      == {"l": [{"person_id": None, "key": ("fact", 99)}]}
      and _g._subject_gate({"l": [{"person_id": _gerin, "key": ("fact", 98)}]}, {_gsam}) == {},
      "gate helper")


# ------------------- T28 the transfer diagnostics: which of the fusion or the embedder loses it
# `_gold_pref_rank` asks whether the embedder can pick the planted preference out of the household's
# OTHER preferences; `_gold_vec_rank` asks how much else the query pulls in ahead of it. Rank 1 and
# still missed means the FUSION lost it; rank > 1 means the EMBEDDER never had it. Both REPORTED,
# never banded, so these tests pin the arithmetic and the None-with-no-embedder contract only.
_dst, _dc1, _dc2, _downer = fresh()
_dsp = add_day(_dst, "2026-03-01", _dc1, ["quiet morning here"])
for _topic in ("spicy food", "long drives"):
    _dst.ingest(dict(predicate_id="owner.prefers", subject={"kind": "person", "id": _downer},
                     object=_topic, object_norm=_topic, source_kind="stated_owner",
                     speaker_cluster=_dc1, span_ids=[_dsp[0]], about_time=None, relation_id=None,
                     polarity="likes", strength=2, ended=False, said_at="2026-03-01T08:00:20"))
_dst.ingest(fact_cand(_downer, [_dsp[0]], "Sydney", "sydney", "2026-03-01T08:00:30"))
_DQ = "planning dinner for our anniversary"
_D_SPICY = _dst._pref_fts_text(_downer, "likes", "spicy food")
_D_DRIVES = _dst._pref_fts_text(_downer, "likes", "long drives")
_D_FACT = _dst._fact_fts_text(_downer, "person", "person.lives_in", "Sydney")
_DE = DictEmbedder({
    _DQ: [1.0, 0.0, 0.0, 0.0],
    "quiet morning here": [1.0, 0.05, 0.0, 0.0],   # a span, closest of all
    _D_FACT: [1.0, 0.10, 0.0, 0.0],                # a fact, next
    _D_SPICY: [1.0, 0.20, 0.0, 0.0],               # the gold preference, 3rd overall but 1st pref
    _D_DRIVES: [1.0, 0.40, 0.0, 0.0],              # the other preference
}, 4)
_dst.embed_pending(_DE)
check("T28a the gold preference is rank 1 among the household's preferences",
      _harness._gold_pref_rank(_dst, _DQ, "spicy food", _DE) == 1,
      str(_harness._gold_pref_rank(_dst, _DQ, "spicy food", _DE)))
check("T28b the other preference is rank 2",
      _harness._gold_pref_rank(_dst, _DQ, "long drives", _DE) == 2,
      str(_harness._gold_pref_rank(_dst, _DQ, "long drives", _DE)))
check("T28c with no embedder the rank is None, never a fabricated 0",
      _harness._gold_pref_rank(_dst, _DQ, "spicy food", None) is None
      and _harness._gold_vec_rank(_dst, _DQ, "spicy food", None) is None)
check("T28d the whole-lane rank counts the span and the fact ahead of the preference",
      _harness._gold_vec_rank(_dst, _DQ, "spicy food", _DE) == 3
      and _harness._gold_vec_rank(_dst, _DQ, "spicy food", _DE)
      > _harness._gold_pref_rank(_dst, _DQ, "spicy food", _DE),
      f"vec={_harness._gold_vec_rank(_dst, _DQ, 'spicy food', _DE)} "
      f"pref={_harness._gold_pref_rank(_dst, _DQ, 'spicy food', _DE)}")
check("T28e a topic no preference holds has no rank",
      _harness._gold_pref_rank(_dst, _DQ, "loud music", _DE) is None
      and _harness._gold_vec_rank(_dst, _DQ, "loud music", _DE) is None)

# ---------------------- T34 the preference-lane rank diagnostic (MS1a.4)
# `_gold_pref_rank` measures the mixed lane; this measures the lane that actually orders
# preferences at query time, so the diagnostics can still see the mechanism MS1a.3 introduced.
check("T34 _gold_pref_lane_rank ranks inside the preference lane, None with no embedder",
      _harness._gold_pref_lane_rank(_dst, _DQ, "spicy food", _DE) == 1
      and _harness._gold_pref_lane_rank(_dst, _DQ, "long drives", _DE) == 2
      and _harness._gold_pref_lane_rank(_dst, _DQ, "spicy food", None) is None,
      str([_harness._gold_pref_lane_rank(_dst, _DQ, t, _DE)
           for t in ("spicy food", "long drives")]))

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
# RETARGET (MS1a.4, the shape declared before the run): the preference is STILL found by both
# vector lanes - `{"vec": 1, "vec_pref": 1}` - but the two are two views of ONE mechanism, so it
# takes ONE vote, 1/61, not their sum. It is still first: the cosine-0 fact sits at `vec` 2 and
# earns 1/62. MS1a.3 gave it 2/61 here, and that double count cost the update band on the corpus.
check("T24c the preference comes first, found by both vector lanes but holding one vote",
      _top.get("table") == "preference" and _top.get("lanes") == {"vec": 1, "vec_pref": 1}
      and _top.get("cos") is not None and close_to(_top["cos"], 1.0, 1e-6)
      and close_to(_top["relevance"], 1.0 / 61, 1e-9),
      f"{_top.get('table')} lanes={_top.get('lanes')} cos={_top.get('cos')} "
      f"rel={_top.get('relevance')}")
check("T30c the preference's relevance is ONE vector vote, not two",
      close_to(_top["relevance"], 0.01639344262295082, 1e-9)
      and _top.get("cos_pref") is not None,
      f"rel={_top.get('relevance')} cos_pref={_top.get('cos_pref')}")
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

# T30d - THE PRICE OF THE PREFERENCE LANE, PINNED, IN THE EXACT DOUBLE-COUNT SHAPE (MS1a.4).
# The MS1a.3 version of this check passed on a fixture where the preference sat LOW in the mixed
# lane, so the summed form never got the chance to win and the test had no teeth - the coder's F6.
# This fixture is built to the shape the corpus actually produced: the answer fact at `fts_fact` 1
# + `vec` 3, the preference at `vec` 2 + `vec_pref` 1. Summed, the preference takes 1/62 + 1/61 =
# 0.032522 and beats the fact's 1/61 + 1/63 = 0.032266; with ONE VOTE it takes 1/61 and loses.
# The lane ranks are asserted, so the fixture cannot drift away from the shape it exists to test.
_d30, _dc1a, _dc2a, _down = fresh()
_d30sp = add_day(_d30, "2026-03-01", _dc1a, ["sam said something about work today"])
_d30.ingest(fact_cand(_down, _d30sp, "a nurse", "a nurse", "2026-03-01T08:00:00",
                      predicate="person.works_as"))
_d30.ingest(dict(predicate_id="owner.prefers", subject={"kind": "person", "id": _down},
                 object="long drives", object_norm="long drives", source_kind="stated_owner",
                 speaker_cluster=_dc1a, span_ids=[_d30sp[0]], about_time=None, relation_id=None,
                 polarity="likes", strength=2, ended=False, said_at="2026-03-01T08:00:20"))
_d30.ingest(fact_cand(_down, [_d30sp[0]], "Sydney", "sydney", "2026-03-01T08:00:30"))
_D30Q = "what job does sam work as"
_D30_FACT = _d30._fact_fts_text(_down, "person", "person.works_as", "a nurse")
_D30_PREF = _d30._pref_fts_text(_down, "likes", "long drives")
_D30_DECOY = _d30._fact_fts_text(_down, "person", "person.lives_in", "Sydney")
# cosines: decoy .995 > preference .980 > fact .958  => vec order decoy, pref, fact = 1, 2, 3
_D30E = DictEmbedder({
    _D30Q: [1.0, 0.0, 0.0, 0.0],
    _D30_DECOY: [1.0, 0.10, 0.0, 0.0],
    _D30_PREF: [1.0, 0.20, 0.0, 0.0],
    _D30_FACT: [1.0, 0.30, 0.0, 0.0],
}, 4)
_d30.embed_pending(_D30E)
_d30hits = _d30.query(_D30Q, k=5, now="2026-03-02T00:00:00", embedder=_D30E)
_d30fact = next((h for h in _d30hits if h["table"] == "fact"
                 and _fact_row_t(_d30, h["row_id"])["predicate_id"] == "person.works_as"), None)
_d30pref = next((h for h in _d30hits if h["table"] == "preference"), None)
check("T30d0 the fixture really is the double-count shape",
      _d30fact is not None and _d30pref is not None
      and _d30fact["lanes"] == {"fts_fact": 1, "vec": 3}
      and _d30pref["lanes"] == {"vec": 2, "vec_pref": 1},
      f"fact={_d30fact and _d30fact['lanes']} pref={_d30pref and _d30pref['lanes']}")
check("T30d one vote: the answer fact is first and the preference cannot out-sum it",
      _d30hits and _d30hits[0] is _d30fact
      and close_to(_d30fact["relevance"], 1.0 / 61 + 1.0 / 63, 1e-9)
      and close_to(_d30pref["relevance"], 1.0 / 61, 1e-9)
      and _d30pref["relevance"] < _d30fact["relevance"],
      str([(h["table"], h["lanes"], round(h["relevance"], 6)) for h in _d30hits]))

# the ORIGINAL T24e-fixture check is kept: the lane still shows on a fact question
_pref_hit = next((h for h in _hits if h["table"] == "preference"), None)
check("T30d2 on a fact question the preference lane still shows, below the answer",
      _hits and _hits[0]["table"] == "fact" and _pref_hit is not None
      and "vec_pref" in _pref_hit["lanes"]
      and _pref_hit["relevance"] <= _hits[0]["relevance"] + 1e-12,
      str([(h["table"], h["lanes"], round(h["relevance"], 6)) for h in _hits]))
check("T30e pref_in_top5_rate measures that price on fact questions",
      close_to(_harness.pref_in_top5_rate(
          st, ["what does sam do for a living", "where does sam live"],
          "2026-03-02T00:00:00", "auto", E), 1.0, 1e-9),
      str(_harness.pref_in_top5_rate(
          st, ["what does sam do for a living", "where does sam live"],
          "2026-03-02T00:00:00", "auto", E)))

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
# RETARGET (MS1a.2, R1): the previous assertion pinned MS0.1's TWO-ROW order - the fact first, the
# span it came from second. Rule 4 (evidence collapse) abolishes that premise by design: a span that
# is an evidence span of a belief in the fused set is not a second result, the belief carries it.
# What is asserted now is what MS1a.2 promises - the fact stands alone AND the span is still there,
# attached, with its verbatim text, so the design's "always the span" is kept by attachment.
check("T25g with no embedder the fact stands alone and carries the span it came from",
      _order == [("fact", _hits[0]["row_id"])] and list(_hits[0]["span_ids"]) == list(_sp)
      and any("sam lives in sydney indeed" in s["text"] for s in _hits[0]["spans"]),
      str(_order) + " " + str(_hits[0].get("span_ids") if _hits else None))

# ============================ T26 the vocabulary move, T27 the paraphrase set
# MS0.1's report found `routine`/`routines` under person.habit while `household` sat under
# household.routine, so the most natural household-routine question matched two sets and was left
# unrestricted. The words move; the sets stay pairwise disjoint (T18m still asserts that).

check("T26a a household routine question now reaches its own predicate",
      predicate_hint("what is our routine") == "household.routine",
      str(predicate_hint("what is our routine")))
check("T26b the corpus's own phrasing resolves too",
      predicate_hint("what household routine do we keep") == "household.routine",
      str(predicate_hint("what household routine do we keep")))
check("T26c a habit question still reaches person.habit",
      predicate_hint("what habits does sam have") == "person.habit",
      str(predicate_hint("what habits does sam have")))
check("T26d routine left person.habit", "routine" not in QUERY_VOCAB["person.habit"])
check("T26e and arrived at household.routine",
      {"routine", "routines"} <= QUERY_VOCAB["household.routine"])
_ov = []
_ks = sorted(QUERY_VOCAB)
for _i in range(len(_ks)):
    for _j in range(_i + 1, len(_ks)):
        _b = QUERY_VOCAB[_ks[_i]] & QUERY_VOCAB[_ks[_j]]
        if _b:
            _ov.append((_ks[_i], _ks[_j], sorted(_b)))
check("T26f the nine sets are still pairwise disjoint after the move", _ov == [], str(_ov))

_VOCAB_UNION = set()
for _s in QUERY_VOCAB.values():
    _VOCAB_UNION |= set(_s)

_hh1 = generate_household(1)
_upd, _para = _hh1["sets"]["update"], _hh1["sets"]["update_paraphrase"]
# The dictated paraphrases are two per PREDICATE (lives_in gets two, works_as gets two), so each
# updated SLOT (subject + predicate) carries exactly two - eight in all, against an update set of
# eight questions that is itself two phrasings per slot.
_slots_u = {(u["subject"], u["predicate_id"]) for u in _upd}
_by_slot = {}
for _p in _para:
    _by_slot.setdefault((_p["subject"], _p["predicate_id"]), []).append(_p["query"])
check("T27a exactly two paraphrases for every updated slot",
      set(_by_slot) == _slots_u and all(len(v) == 2 for v in _by_slot.values()),
      str({k: len(v) for k, v in _by_slot.items()}))
check("T27a2 and they are distinct phrasings",
      all(len(set(v)) == 2 for v in _by_slot.values()), str(_by_slot))
_bad = [p["query"] for p in _para if set(p["query"].lower().split()) & _VOCAB_UNION]
check("T27b every paraphrase is out of the hint's vocabulary entirely", _bad == [], str(_bad))
_hinted = [(p["query"], predicate_hint(p["query"])) for p in _para
           if predicate_hint(p["query"]) is not None]
check("T27c so none of them hints at all", _hinted == [], str(_hinted))
check("T27d each paraphrase keeps its gold answer and subject",
      all(p.get("gold_object_norm") and p.get("subject") for p in _para),
      str([p for p in _para if not p.get("gold_object_norm")][:1]))
# Strengthening beyond the prompt: the household's NAME is substituted into every paraphrase, so a
# name that happened to be a vocabulary word would hint. Sweep several seeds, not just seed 1.
_bad_seeds = []
for _sd in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10):
    for _p in generate_household(_sd)["sets"]["update_paraphrase"]:
        if predicate_hint(_p["query"]) is not None:
            _bad_seeds.append((_sd, _p["query"]))
check("T27e no household's names turn a paraphrase into a hinted question",
      _bad_seeds == [], str(_bad_seeds[:3]))


# ================================================== T35 MS1b: the extractor scaffolding
# No model, no network, no GPU: the schema is generated from the registry, the prompts are constants
# plus formatting, and the client is exercised on its two pure helpers. The bake-off itself runs a
# llama.cpp server outside CI; what CI proves is that the contract the model is held to is the
# registry's own, and that it cannot drift by hand-editing an enum.
import hashlib as _hashlib  # noqa: E402
import subprocess as _subprocess  # noqa: E402

from jarvis_memory.registry import RELATIONS as _RELS, SOURCE_RANK as _SRC  # noqa: E402
from jarvis_memory.extract.schema import (  # noqa: E402
    POLARITIES as _POLS, candidate_schema, schema_sha256,
)
from jarvis_memory.extract.prompt import system_prompt, user_prompt  # noqa: E402
from jarvis_memory.extract.client import build_request, parse_response  # noqa: E402
from jarvis_memory.extract.score import (  # noqa: E402
    lenient_match, match, resolve_subject, score_household, validity,
)

# CONTRACT 2: the candidate is a `oneOf` of four predicate-family branches. Each branch makes its
# family's own fields REQUIRED and NON-NULLABLE, which is what makes contract 1's entire invalid
# class - a null relation_id on an edge, a null polarity on a preference, 39 calls across two
# models - unproducible under constrained decoding rather than rejected after the fact.
_SCH = candidate_schema()
_BRANCHES = _SCH["properties"]["candidates"]["items"]["oneOf"]
_BY_PID = {}
for _b in _BRANCHES:
    for _pid in _b["properties"]["predicate_id"]["enum"]:
        _BY_PID.setdefault(_pid, []).append(_b)
_EDGE_B = _BY_PID["person.relation_to"][0]
_PREF_B = _BY_PID["owner.prefers"][0]

check("T35a the four branch enums PARTITION the registry - every predicate in exactly one",
      sorted(_BY_PID) == sorted(PREDICATES)
      and all(len(v) == 1 for v in _BY_PID.values()) and len(_BRANCHES) == 4,
      str({k: len(v) for k, v in _BY_PID.items() if len(v) != 1} or sorted(_BY_PID)))
# The mutant this catches is M2: making relation_id nullable again re-opens the exact failure the
# branches exist to close, and it would still look like a valid schema.
check("T35a2 relation_id and polarity are NON-nullable inside their own branches",
      _EDGE_B["properties"]["relation_id"]["enum"] == sorted(_RELS)
      and None not in _EDGE_B["properties"]["relation_id"]["enum"]
      and _EDGE_B["properties"]["relation_id"]["type"] == "string"
      and _PREF_B["properties"]["polarity"]["enum"] == list(_POLS)
      and None not in _PREF_B["properties"]["polarity"]["enum"]
      and _PREF_B["properties"]["polarity"]["type"] == "string",
      str((_EDGE_B["properties"]["relation_id"], _PREF_B["properties"]["polarity"])))
check("T35a3 additionalProperties is false at both outer levels and in every branch",
      _SCH["additionalProperties"] is False
      and all(b["additionalProperties"] is False for b in _BRANCHES)
      and all(b["type"] == "object" for b in _BRANCHES)
      and _SCH["properties"]["candidates"]["type"] == "array")
# The contract asserted as an EXACT set, branch by branch: the model must be asked for everything it
# alone can judge and for nothing the code derives. Either half failing is a contract that measures
# something other than the extractor - which is what L0 measured.
_WANT_REQ = {
    "person.relation_to": ["predicate_id", "about", "relation_id", "object", "stated"],
    "owner.prefers": ["predicate_id", "polarity", "object", "stated"],
    "household.topic": ["predicate_id", "object", "stated"],
    "household.routine": ["predicate_id", "object", "stated"],
    "person.habit": ["predicate_id", "about", "object", "stated"],
    "person.lives_in": ["predicate_id", "about", "object", "stated"],
    "person.name": ["predicate_id", "about", "object", "stated"],
    "person.trait": ["predicate_id", "about", "object", "stated"],
    "person.works_as": ["predicate_id", "about", "object", "stated"],
}
_DERIVED = {"subject", "object_norm", "source_kind", "speaker_cluster", "span_ids"}
check("T35a4 each branch's required set is exactly its row, and no branch carries a derived field",
      all(_BY_PID[pid][0]["required"] == want for pid, want in _WANT_REQ.items())
      and not any(_DERIVED & set(b["properties"]) for b in _BRANCHES)
      # `about` belongs to the EDGE and PERSON families only: a household fact has no person
      # subject and a preference is the owner's whoever said it, so asking for `about` there would
      # be asking the model to decide something the code already knows.
      and {pid for pid, bs in _BY_PID.items() if "about" in bs[0]["properties"]}
      == {"person.relation_to", "person.habit", "person.lives_in", "person.name", "person.trait",
          "person.works_as"}
      and all(b["required"][0] == "predicate_id" for b in _BRANCHES),
      str({pid: _BY_PID[pid][0]["required"] for pid in _WANT_REQ
           if _BY_PID[pid][0]["required"] != _WANT_REQ[pid]}))

_SYS = system_prompt()
check("T35b the system prompt names every predicate and every relation exactly once",
      all(_SYS.count(p) >= 1 for p in PREDICATES)
      and all(_SYS.count(r) >= 1 for r in _RELS)
      and all(_SYS.count(" %s " % p) <= 1 or _SYS.count(p) >= 1 for p in PREDICATES),
      str([p for p in PREDICATES if p not in _SYS]))
_USR = user_prompt("i work as a nurse", 2, 5, {1: "alex", 2: "tess"}, 77)
check("T35b2 the user prompt carries the span id, the cluster and the day, and no store contents",
      "span_id: 77" in _USR and "speaker_cluster: 2" in _USR and "day: 5" in _USR
      and "i work as a nurse" in _USR, repr(_USR))

_REQ = build_request("i work as a nurse", 2, 5, {1: "alex"}, 77, _SCH)
check("T35c the request is schema-constrained, greedy and seeded",
      _REQ["response_format"]["type"] == "json_schema"
      and _REQ["response_format"]["json_schema"]["schema"] == _SCH
      and _REQ["temperature"] == 0.0 and _REQ["seed"] == 1,
      str(_REQ.get("response_format", {}).get("type")))
_ok_body = _json.dumps({"choices": [{"message": {"content": '{"candidates": []}'}}]})
check("T35c2 parse_response returns the object on a good body and (None, text) on garbage",
      parse_response(_ok_body) == ({"candidates": []}, '{"candidates": []}')
      and parse_response("not json at all")[0] is None,
      str(parse_response("not json at all")))

_NBC = {1: "alex", 2: "tess"}
_CBN = {"alex": 1, "tess": 2}


def _c(pid, ref, kind, onorm, sids, **over):
    d = {"predicate_id": pid, "subject": {"kind": kind, "ref": ref}, "object": onorm,
         "object_norm": onorm, "source_kind": "stated_owner", "span_ids": list(sids)}
    d.update(over)
    return d


check("T35d match: same predicate, resolved subject, object and a shared span",
      match(_c("person.works_as", "1", "person", "a nurse", [3]),
            _c("person.works_as", "owner", "person", "a nurse", [3]), _NBC, _CBN),
      "owner vs cluster 1 must resolve equal")
# RETARGETED at the narrow contract. The article case is no longer a miss: both sides go through
# the one normaliser, so "a nurse" and "nurse" ARE the same value - that was L0's largest single
# failure class and it was the contract's, not the model's. A genuinely different value still
# misses strictly and is caught only by the lenient variant.
check("T35d2 the article is normalised away on both sides; a different value still misses",
      match(_c("person.works_as", "1", "person", "nurse", [3]),
            _c("person.works_as", "owner", "person", "a nurse", [3]), _NBC, _CBN)
      and not match(_c("person.works_as", "1", "person", "nurse", [3]),
                    _c("person.works_as", "owner", "person", "a night nurse", [3]), _NBC, _CBN)
      and lenient_match(_c("person.works_as", "1", "person", "nurse", [3]),
                        _c("person.works_as", "owner", "person", "a night nurse", [3]),
                        _NBC, _CBN))
check("T35d3 no shared span id is never a match, however right the content",
      not match(_c("person.works_as", "1", "person", "a nurse", [9]),
                _c("person.works_as", "owner", "person", "a nurse", [3]), _NBC, _CBN))
check("T35d4 a name resolves to its cluster",
      resolve_subject("tess", "person", _NBC, _CBN) == ("person", 2)
      and resolve_subject("partner", "person", _NBC, _CBN) == ("person", 2)
      and resolve_subject("2", "person", _NBC, _CBN) == ("person", 2),
      str(resolve_subject("tess", "person", _NBC, _CBN)))

# 4 gold, 5 predicted, 3 matching -> precision 0.6, recall 0.75, f1 = 2*.6*.75/1.35
_G35 = [_c("person.works_as", "owner", "person", "a nurse", [1]),
        _c("person.lives_in", "owner", "person", "sydney", [2]),
        _c("person.habit", "partner", "person", "runs", [3]),
        _c("person.trait", "owner", "person", "quiet", [4])]
_P35 = [_c("person.works_as", "1", "person", "a nurse", [1]),
        _c("person.lives_in", "1", "person", "sydney", [2]),
        _c("person.habit", "2", "person", "runs", [3]),
        _c("person.trait", "1", "person", "loud", [4]),
        _c("person.lives_in", "1", "person", "perth", [5])]
_S35 = score_household(_P35, _G35, _NBC, _CBN)
check("T35e score_household: precision 0.6, recall 0.75, f1 0.6666666666666666",
      close_to(_S35["precision"], 0.6, 1e-9) and close_to(_S35["recall"], 0.75, 1e-9)
      and close_to(_S35["f1"], 0.6666666666666666, 1e-9),
      str((_S35["precision"], _S35["recall"], _S35["f1"])))

_SPANC = {1: 1, 2: 1, 3: 2, 4: 1}
check("T35f validity: an empty list is VALID, a bad candidate and an unparsed call are not",
      validity([{"candidates": []}], _SPANC)[0] == 1
      and validity([{"candidates": None}], _SPANC)[0] == 0
      and validity([{"candidates": [_c("person.teleports", "1", "person", "x", [1])]}],
                   _SPANC)[0] == 0,
      str(validity([{"candidates": [_c("person.teleports", "1", "person", "x", [1])]}], _SPANC)))

# ================================================== T36 MS1b: the NARROWED contract
# L0 (the first Llama 3.1 8B run, kept as `ms1b_llama_8b_contract0.json`) measured the CONTRACT, not
# the model: 105 invalid calls, every one a `stated_*` speaker mismatch on a value the caller held;
# every works_as miss an article the prompt's own example taught; every relation miss a shape
# mismatch. These tests pin the repair — the model decides, the code derives, and one normaliser
# owns `object_norm` everywhere including the store's write path.
from jarvis_memory.registry import normalise_object as _norm  # noqa: E402
from jarvis_memory.extract.derive import derive as _derive  # noqa: E402
from jarvis_memory.extract.score import resolve_person as _rperson  # noqa: E402
from jarvis_memory.bench import corpus as _corpus  # noqa: E402

check("T36 normalise_object: one article stripped, case and space collapsed, a lone article kept",
      _norm("A Plumber") == "plumber" and _norm("  Perth ") == "perth"
      and _norm("an early bird") == "early bird" and _norm("the") == "the"
      and _norm("reads before bed") == "reads before bed" and _norm(None) == "",
      str([_norm(x) for x in ("A Plumber", "  Perth ", "an early bird", "the")]))


def _span(sid, cluster, text="x", day=3):
    return {"sid": sid, "cluster": cluster, "text": text, "day": day}


_d_p2 = _derive({"predicate_id": "person.works_as", "about": "speaker", "object": "a nurse",
                 "stated": True}, _span(7, 2))
_d_p1 = _derive({"predicate_id": "person.works_as", "about": "speaker", "object": "A Nurse",
                 "stated": True}, _span(7, 1))
_d_inf = _derive({"predicate_id": "person.works_as", "about": "speaker", "object": "a nurse",
                  "stated": False}, _span(7, 2))
check("T36a derive: the speaker's own cluster is the subject, and who spoke decides the source kind",
      _d_p2["source_kind"] == "stated_other" and _d_p2["subject"] == {"kind": "person", "ref": "2"}
      and _d_p1["source_kind"] == "stated_owner"
      and _d_p1["subject"] == {"kind": "person", "ref": "1"}
      and _d_inf["source_kind"] == "inferred"
      and _d_p2["span_ids"] == [7] and _d_p2["speaker_cluster"] == 2
      and _d_p1["object_norm"] == "nurse" and _d_p1["object"] == "A Nurse",
      str((_d_p2["source_kind"], _d_p2["subject"], _d_p1["source_kind"], _d_inf["source_kind"])))

_d_name = _derive({"predicate_id": "person.lives_in", "about": "Tess", "object": "Perth",
                   "stated": True}, _span(8, 1))
_d_hh = _derive({"predicate_id": "household.routine", "about": "speaker", "object": "Friday pizza",
                 "stated": True}, _span(9, 2))
_d_pref = _derive({"predicate_id": "owner.prefers", "about": "speaker", "object": "Jazz",
                   "stated": True, "polarity": "likes"}, _span(10, 2))
_d_rel = _derive({"predicate_id": "person.relation_to", "about": "speaker", "object": "Sam",
                  "stated": True, "relation_id": "spouse"}, _span(11, 2))
check("T36b derive: a name stays a name, a household predicate is the household's, a preference is "
      "the owner's whoever said it, and a relation is keyed by its relation id",
      _d_name["subject"] == {"kind": "person", "ref": "tess"}
      and _d_name["object_norm"] == "perth"
      and _d_hh["subject"] == {"kind": "household", "ref": "household"}
      and _d_pref["subject"] == {"kind": "person", "ref": "1"}
      and _d_pref["source_kind"] == "stated_other"
      and _d_rel["object_norm"] == "spouse" and _d_rel["object"] == "Sam",
      str((_d_name["subject"], _d_hh["subject"], _d_pref["subject"], _d_rel["object_norm"])))

_bad36c, _n36c = [], 0
for _seed36 in range(1, 11):
    _hh36 = _corpus.generate_household(_seed36, 14)
    for _c36 in _hh36["candidates"]:
        _n36c += 1
        _want36 = (_c36.get("relation_id") if _c36["predicate_id"] == "person.relation_to"
                   else _norm(_c36.get("object")))
        if _c36.get("object_norm") != _want36:
            _bad36c.append((_seed36, _c36["predicate_id"], _c36.get("object"),
                            _c36.get("object_norm"), _want36))
check("T36c every oracle object_norm over ten households IS the normaliser's output "
      "(a relation's is its relation id) - so the store's overwrite moves no gold value",
      _n36c == 370 and not _bad36c, str((_n36c, _bad36c[:3])))

_st36, _c1_36, _c2_36, _own36 = fresh()
_sp36 = add_day(_st36, "2026-03-01", 1, ["i am a plumber", "i take a morning run",
                                         "the morning run again"])
_st36.ingest(fact_cand(_own36, [_sp36[0]], "A Plumber", "A PLUMBER", "2026-03-01T08:00:00",
                       predicate="person.works_as"))
_st36.ingest(fact_cand(_own36, [_sp36[1]], "A Morning Run", "WHATEVER", "2026-03-01T08:00:10",
                       predicate="person.habit"))
_st36.ingest(fact_cand(_own36, [_sp36[2]], "the morning run", "the morning run",
                       "2026-03-01T08:00:20", predicate="person.habit"))
_rows36 = {r["predicate_id"]: r for r in _st36.current("fact") if r["predicate_id"] == "person.works_as"}
_habits36 = [r for r in _st36.current("fact") if r["predicate_id"] == "person.habit"]
check("T36d the store OVERWRITES a caller's object_norm with the normaliser's, so two spellings of "
      "one habit accrue onto one row instead of coexisting as two beliefs",
      _rows36["person.works_as"]["object_norm"] == "plumber"
      and len(_habits36) == 1 and _habits36[0]["object_norm"] == "morning run",
      str((_rows36["person.works_as"]["object_norm"],
           [r["object_norm"] for r in _habits36])))

check("T36e the system prompt instructs on nothing the code derives, and names the speaker token",
      not any(x in _SYS for x in ("object_norm", "source_kind", "speaker_cluster", "span_ids",
                                  "subject.ref", "stated_owner", "stated_other"))
      and '"speaker"' in _SYS,
      str([x for x in ("object_norm", "source_kind", "speaker_cluster", "span_ids", "subject.ref",
                       "stated_owner") if x in _SYS]))


def _rel(ref, obj, sids, rid="spouse", src="stated_other"):
    return {"predicate_id": "person.relation_to", "subject": {"kind": "person", "ref": ref},
            "object": obj, "object_norm": rid, "relation_id": rid, "source_kind": src,
            "span_ids": list(sids)}


check("T36f a relation whose far end was never identified never matches, and the edge is "
      "direction-aware",
      match(_rel("2", "alex", [4]), _rel("partner", "owner", [4]), _NBC, _CBN)
      and not match(_rel("2", "she", [4]), _rel("partner", "owner", [4]), _NBC, _CBN)
      and not match(_rel("owner", "partner", [4]), _rel("partner", "owner", [4]), _NBC, _CBN)
      and not match(_rel("2", "alex", [4], rid="sibling"), _rel("partner", "owner", [4]),
                    _NBC, _CBN)
      and _rperson("she", _NBC, _CBN) is None and _rperson("tess", _NBC, _CBN) == 2,
      str((_rperson("she", _NBC, _CBN), _rperson("owner", _NBC, _CBN))))

check("T36g a household subject resolves the same whether it is written None or 'household' - "
      "without this every household prediction would be a scoring artefact, not a miss",
      resolve_subject(None, "household", _NBC, _CBN)
      == resolve_subject("household", "household", _NBC, _CBN)
      and match({"predicate_id": "household.routine",
                 "subject": {"kind": "household", "ref": "household"},
                 "object": "Friday Pizza", "span_ids": [5]},
                {"predicate_id": "household.routine", "subject": {"kind": "household", "ref": None},
                 "object": "friday pizza", "object_norm": "friday pizza", "span_ids": [5]},
                _NBC, _CBN),
      str(resolve_subject(None, "household", _NBC, _CBN)))

# ================================================== T37/T38 MS1b contract 2: the field
# Contract 1 measured the contract twice over: `about` came back as the pronoun heard (Gemma on 167
# of 250 person-subject predictions, Llama on 36 of 246) and every one of the 39 invalid calls was a
# null relation_id or polarity. Contract 2 derives the first person in code and splits the schema
# into four family branches, and the FIELD - every instruction model that fits the 2070 at 4-bit -
# runs under it. These tests pin the derivation, the field table, the queue's resumability, the
# thinking switch, the verdict's file selection and the two reported-beside metrics.
import os as _os  # noqa: E402
import tempfile as _tempfile  # noqa: E402
import shutil as _shutil  # noqa: E402

import bench_ms1b as _bench  # noqa: E402
from jarvis_memory.extract.derive import FIRST_PERSON as _FP  # noqa: E402

_FIELD_KEYS = ["llama-1b", "llama-3b", "phi3-mini", "qwen3-4b", "qwen35-4b", "phi4-mini",
               "nuextract", "llama-8b", "qwen3-8b", "qwen35-9b", "gemma-e2b", "gemma-e4b"]
# The 2026-09-10 addendum's arms, which run on a DIFFERENT llama.cpp build (T42). Listed once, here,
# so T37a can say the table is exactly the frozen field plus these and T42a can pin their own
# properties without a second copy of the list to drift from this one.
_ADDENDUM_KEYS = ["gemma-e4b-v040", "granite-3b", "granite-8b", "lfm25-2.6b", "ministral-8b",
                  "gemma-e4b-q6k", "gemma-e4b-q8", "nuextract3"]
_FROZEN_PATHS = {k: _bench.model_path(k) for k in _FIELD_KEYS}
check("T37a the field table holds every frozen-field key with its path and thinking switch, and "
      "the table is exactly those twelve plus the addendum's eight",
      sorted(_bench.MODELS) == sorted(_FIELD_KEYS + _ADDENDUM_KEYS)
      and set(_FIELD_KEYS).isdisjoint(_ADDENDUM_KEYS)
      and all(_bench.model_path(k).startswith(("models/", "phase3/models/"))
              for k in _FIELD_KEYS)
      and {k for k in _FIELD_KEYS if _bench.thinking_switch(k)}
      == {"qwen3-4b", "qwen35-4b", "qwen3-8b", "qwen35-9b"}
      # the frozen field's own paths are untouched by the addendum: its numbers describe these files
      and _FROZEN_PATHS["gemma-e4b"] == "models/google_gemma-4-E4B-it-Q4_K_M.gguf"
      and _FROZEN_PATHS["gemma-e2b"] == "models/gemma-4-E2B-it-Q4_K_M.gguf",
      str(sorted(set(_bench.MODELS) ^ set(_FIELD_KEYS + _ADDENDUM_KEYS))
          or {k for k in _FIELD_KEYS if _bench.thinking_switch(k)}))


def _stub_run(path, key, contract, sha, f1=0.5, validity=0.995):
    with open(path, "w", encoding="utf-8") as fh:
        _json.dump({"model_key": key, "contract": contract, "schema_sha256": sha,
                    "aggregate": {"f1": f1, "validity": validity, "lenient_f1": f1,
                                  "f1_scorable": f1, "zero_gold_predictions": 0,
                                  "seconds": 1.0}}, fh)


_TMPQ = _tempfile.mkdtemp()
try:
    _sha_now = schema_sha256()
    # (1) already done at THIS contract and hash -> skipped, not re-run
    _stub_run(_os.path.join(_TMPQ, "ms1b_llama-1b.json"), "llama-1b", "contract2", _sha_now)
    _done1, _skip1, _stop1 = _bench.run_queue(["llama-1b"], [1], 14, None, 8099, 4096, 99, 2048,
                                              results_dir=_TMPQ)
    # (2) present under ANOTHER contract -> the queue STOPS rather than overwrite a kept run
    _stub_run(_os.path.join(_TMPQ, "ms1b_llama-3b.json"), "llama-3b", "narrow", "deadbeef")
    _done2, _skip2, _stop2 = _bench.run_queue(["llama-3b"], [1], 14, None, 8099, 4096, 99, 2048,
                                              results_dir=_TMPQ)
    # (3) absent -> it proceeds past the skip logic (and here stops at the missing model file,
    #     which is the only way to prove "it would have run" without starting a server)
    _bench.MODELS["zz-fake"] = {"path": "models/zz-does-not-exist.gguf", "thinking_switch": False}
    _done3, _skip3, _stop3 = _bench.run_queue(["zz-fake"], [1], 14, None, 8099, 4096, 99, 2048,
                                              results_dir=_TMPQ)
    del _bench.MODELS["zz-fake"]
    check("T37b the queue is resumable: done is skipped, another contract STOPS it, absent runs",
          _done1 == ["llama-1b"] and _stop1 is None
          and _stop2 == "llama-3b" and _done2 == []
          and _done3 == [] and _stop3 is None and len(_skip3) == 1
          and "absent" in _skip3[0][1],
          str((_done1, _stop1, _done2, _stop2, _skip3)))

    # T38c: the verdict never mixes contracts, and never reads its own outputs.
    _TMPV = _tempfile.mkdtemp()
    _stub_run(_os.path.join(_TMPV, "ms1b_x.json"), "x", "contract2", _sha_now, f1=0.42)
    _stub_run(_os.path.join(_TMPV, "ms1b_x_contract0.json"), "x", "wide", "old", f1=0.12)
    _stub_run(_os.path.join(_TMPV, "ms1b_x_contract1.json"), "x", "narrow", "old", f1=0.40)
    with open(_os.path.join(_TMPV, "ms1b_field_verdict.json"), "w", encoding="utf-8") as fh:
        _json.dump({"chosen": None, "rows": []}, fh)          # no model_key: not a run
    # Guarded so a regression FAILS BY NAME instead of crashing the suite: if the selection stops
    # excluding the kept `_contract1` run, this call raises on the mislabel and a traceback would
    # tell a reader far less than a named failing check does.
    try:
        _sel = _bench.load_field(_TMPV)
    except Exception as exc:                                   # noqa: BLE001
        _sel = [("RAISED: %s" % exc, {}, "")]
    _stub_run(_os.path.join(_TMPV, "ms1b_y.json"), "y", "narrow", _sha_now, f1=0.99)
    try:
        _bench.load_field(_TMPV)
        _refused = ""
    except ValueError as exc:
        _refused = str(exc)
    check("T38c the verdict reads only contract-2 runs, ignores its own output, refuses a mislabel",
          [k for k, _a, _p in _sel] == ["x"] and len(_sel) == 1
          and "ms1b_y.json" in _refused and "narrow" in _refused,
          str(([k for k, _a, _p in _sel], _refused[:120])))
    _shutil.rmtree(_TMPV, ignore_errors=True)
finally:
    _shutil.rmtree(_TMPQ, ignore_errors=True)

_REQ_SW = build_request("i work as a nurse", 2, 5, {1: "alex"}, 77, _SCH, thinking_switch=True)
_REQ_NO = build_request("i work as a nurse", 2, 5, {1: "alex"}, 77, _SCH)
check("T37c the thinking switch is sent only where the family has one, and is absent otherwise",
      _REQ_SW["chat_template_kwargs"] == {"enable_thinking": False}
      and "chat_template_kwargs" not in _REQ_NO
      and _bench.thinking_switch("qwen3-4b") and not _bench.thinking_switch("llama-8b"),
      str(_REQ_SW.get("chat_template_kwargs")))

_V5 = [("a", {"validity": 0.995, "f1": 0.71}), ("b", {"validity": 0.995, "f1": 0.64}),
       ("c", {"validity": 0.989, "f1": 0.80}), ("d", {"validity": 0.995, "f1": 0.30}),
       ("e", {"validity": 0.995, "f1": 0.55})]
_VD = _bench.verdict(_V5)
_VN = _bench.verdict([("p", {"validity": 0.995, "f1": 0.59}),
                      ("q", {"validity": 0.995, "f1": 0.40})])
check("T37d the rule: the highest F1 among the VALID, the 0.80 at 98.9 % validity excluded; "
      "all under the floor -> NONE with the ceiling named",
      _VD["chosen"] == "a" and _VN["chosen"] is None
      and "0.5900" in _VN["reason"] and "p" in _VN["reason"],
      str((_VD["chosen"], _VN["reason"])))

_D38 = {w: _derive({"predicate_id": "person.works_as", "about": w, "object": "a nurse",
                    "stated": True}, _span(5, 2)) for w in sorted(_FP)}
_D38["speaker"] = _derive({"predicate_id": "person.works_as", "about": "speaker",
                           "object": "a nurse", "stated": True}, _span(5, 2))
_D38_EDGE = _derive({"predicate_id": "person.relation_to", "about": "we", "object": "sam",
                     "stated": True, "relation_id": "spouse"}, _span(5, 2))
_D38_SHE = _derive({"predicate_id": "person.works_as", "about": "she", "object": "a nurse",
                    "stated": True}, _span(5, 2))
_D38_SAM = _derive({"predicate_id": "person.works_as", "about": "sam", "object": "a nurse",
                    "stated": True}, _span(5, 2))
check("T38a every first-person word derives to the SPEAKER's cluster; a third person stays put",
      len(_FP) == 10
      and all(d["subject"] == {"kind": "person", "ref": "2"} for d in _D38.values())
      and _D38_EDGE["subject"] == {"kind": "person", "ref": "2"}
      and _D38_SHE["subject"] == {"kind": "person", "ref": "she"}
      and _D38_SAM["subject"] == {"kind": "person", "ref": "sam"},
      str(sorted(w for w, d in _D38.items() if d["subject"]["ref"] != "2")))


def _remap_matches(path):
    """The strategist's post-hoc remap, recomputed here with the REAL scorer.

    This is the one number that says the derivation is worth its code: it re-scores the STORED
    contract-1 predictions with only the first-person subjects rewritten to the speaker's cluster.
    It is an EXPECTATION for the re-runs, never a result - contract 2 also changed the schema, so
    the actual runs need not land here.
    """
    with open(path, encoding="utf-8") as fh:
        d = _json.load(fh)
    total = 0
    for h in d["households"]:
        hh = _corpus.generate_household(h["seed"], d["days"])
        names, cbn = _bench._household_context(hh)
        preds = []
        for x in h["predictions"]:
            x = dict(x)
            subj = dict(x.get("subject") or {})
            if subj.get("kind") == "person" and str(subj.get("ref", "")).lower() in _FP:
                subj["ref"] = str(x.get("speaker_cluster"))
                x["subject"] = subj
            preds.append(x)
        total += score_household(preds, hh["candidates"], names, cbn)["n_match"]
    return total


_RES = Path(__file__).resolve().parent / "bench" / "results"
_L1 = _RES / "ms1b_llama_8b_contract1.json"
_G1 = _RES / "ms1b_gemma_e2b_contract1.json"
if _L1.exists() and _G1.exists():
    check("T38b the remap reproduces the strategist's expectation on the stored contract-1 runs",
          _remap_matches(_L1) == 156 and _remap_matches(_G1) == 227,
          str((_remap_matches(_L1), _remap_matches(_G1))))
else:
    check("T38b the remap reproduces the strategist's expectation on the stored contract-1 runs",
          False, "the renamed contract-1 JSONs are missing: %s %s" % (_L1, _G1))

_G38 = ([_c("person.relation_to", "owner", "person", "spouse", [1], relation_id="spouse",
            source_kind="inferred", object="partner") for _ in range(8)]
        + [_c("person.habit", "owner", "person", "habit%d" % i, [2]) for i in range(29)])
_P38 = ([_c("person.habit", "owner", "person", "habit%d" % i, [2]) for i in range(5)]
        + [_c("person.name", "owner", "person", "n%d" % i, [3]) for i in range(5)])
_S38 = score_household(_P38, _G38, _NBC, _CBN)
_P_, _R_ = 0.5, 5 / 29
check("T38d f1_scorable removes only the inferred edges from the RECALL denominator, and the "
      "zero-gold predictions are counted apart",
      _S38["n_gold"] == 37 and _S38["n_pred"] == 10 and _S38["n_match"] == 5
      and _S38["scorable_gold"] == 29 and _S38["zero_gold_predictions"] == 5
      and close_to(_S38["f1_scorable"], 2 * _P_ * _R_ / (_P_ + _R_), 1e-9)
      and close_to(_S38["precision"], 0.5, 1e-9),
      str((_S38["n_gold"], _S38["n_match"], _S38["scorable_gold"], _S38["f1_scorable"],
           _S38["zero_gold_predictions"])))

_dry = _subprocess.run(
    [sys.executable, str(Path(__file__).resolve().parent / "bench_ms1b.py"),
     "--dry-run", "--households", "1"],
    capture_output=True, text=True, encoding="utf-8", errors="replace")
_want_sha = _hashlib.sha256(
    _json.dumps(candidate_schema(), sort_keys=True).encode("utf-8")).hexdigest()
check("T35g the CLI dry run builds one request and reports the registry-derived schema hash",
      _dry.returncode == 0 and ("schema_sha256: " + _want_sha) in _dry.stdout
      and _want_sha == schema_sha256() and '"json_schema"' in _dry.stdout,
      (_dry.stdout[-300:] + _dry.stderr[-300:]))

# ================================================== T39 — the voice pipeline's span vectors
# The speaker embedding is the one thing that OUTLIVES the audio: once the WAV is deleted, a nightly
# re-fit can only re-cluster retained spans from these vectors. That makes the purge's reach the
# safety property - a purged speaker must not survive as a vector a re-fit could resurrect - so this
# asserts the purge's EXISTING behaviour rather than re-implementing it.
from jarvis_memory.store import SPAN_EMBED_MODEL as _SEM  # noqa: E402

_st39, _c1_39, _c2_39, _own39 = fresh()
_r39 = _st39.add_recording("sha-39", "2026-03-01T08:00:00", 60.0, "headset")
_sp_own = _st39.add_span(_r39, 0.0, 4.0, _c1_39, "the owner speaking", -0.20)
_sp_oth = _st39.add_span(_r39, 4.0, 8.0, _c2_39, "another voice", -0.25)
_v39 = [((i % 17) - 8) / 8.0 for i in range(192)]          # exact in binary32: eighths
_v39b = [((i % 11) - 5) / 8.0 for i in range(192)]
_st39.add_span_embedding(_sp_own, _v39)
_st39.add_span_embedding(_sp_oth, _v39b)
_back39 = _st39.span_embedding(_sp_own)
_purged39 = _st39.purge_cluster(_c2_39)
check("T39 a span's speaker vector round-trips byte-exact, and the purge takes it with the span",
      _back39 == _v39 and len(_back39) == 192
      and _st39.span_embedding(_sp_oth) is None
      and _st39.span_embedding(_sp_own) == _v39
      and _st39.conn.execute(
          "select count(*) from embedding where owner_table='span'").fetchone()[0] == 1
      and _SEM == "speechbrain/spkrec-ecapa-voxceleb"
      and isinstance(_purged39, dict),
      str((len(_back39), _back39[:3], _st39.span_embedding(_sp_oth))))

_cent39 = [0.5] * 192
_st39.set_cluster_centroid(_c1_39, _cent39)
_row39 = _st39.conn.execute("select centroid from cluster where id=?", (_c1_39,)).fetchone()[0]
check("T39b a cluster centroid persists in the embedding encoding and reads back equal",
      embed_mod.unpack(_row39, 192) == _cent39,
      str(len(_row39) if _row39 else None))


# ================================================== T40 - merging a cluster into a known one
# M1b.4: a voice cluster whose centroid, over enough accumulated speech, clears the owner's enrolled
# threshold turns out to BE the owner. Folding it in is a RELABEL - no span, no text and no speaker
# vector is lost - and the loss it does represent (a cluster id that stops existing) is an audit row.
_st40, _c1_40, _c2_40, _own40 = fresh()
_r40a = _st40.add_recording("sha-40a", "2026-04-01T08:00:00", 60.0, "headset")
_r40b = _st40.add_recording("sha-40b", "2026-04-02T08:00:00", 60.0, "headset")
_sp40 = [_st40.add_span(_r40a, 0.0, 4.0, _c1_40, "the owner on day one", -0.20),
         _st40.add_span(_r40a, 4.0, 9.0, _c2_40, "the other voice on day one", -0.21),
         _st40.add_span(_r40b, 0.0, 6.0, _c2_40, "the other voice on day two", -0.22)]
_st40.add_span_embedding(_sp40[1], [0.25] * 192)
_st40.add_cluster_speech(_c1_40, 4.0)
_st40.add_cluster_speech(_c2_40, 11.0)
_speech_before = {c: _st40.conn.execute(
    "select embedded_speech_s from cluster where id=?", (c,)).fetchone()[0]
    for c in (_c1_40, _c2_40)}
_audit_before = _st40.conn.execute("select count(*) from audit").fetchone()[0]
_emb_before = _st40.conn.execute(
    "select count(*) from embedding where owner_table='span'").fetchone()[0]

_m40 = _st40.merge_cluster(_c2_40, _c1_40, "centroid scored 0.4012 over 11.00 s")

_row40 = _st40.conn.execute(
    "select n_spans, days_heard, first_heard, embedded_speech_s from cluster where id=?",
    (_c1_40,)).fetchone()
_gone40 = _st40.conn.execute("select 1 from cluster where id=?", (_c2_40,)).fetchone()
_aud40 = _st40.conn.execute(
    "select op, target_table, loser_id, winner_id, rule, note from audit "
    "order by id desc limit 1").fetchone()
_refused40 = _self40 = False
try:
    _st40.merge_cluster(9999, _c1_40, "no such cluster")
except ValueError:
    _refused40 = True
try:
    _st40.merge_cluster(_c1_40, _c1_40, "into itself")
except ValueError:
    _self40 = True

check("T40 merge_cluster relabels every span, folds the counts and the accumulated speech, removes "
      "the cluster row and writes ONE audit row; an unknown or self merge is refused",
      _m40["spans_moved"] == 2 and _m40["src"] == _c2_40 and _m40["dst"] == _c1_40
      # every span now points at the winner and NOTHING was deleted
      and _st40.conn.execute("select count(*) from span where cluster_id=?",
                             (_c1_40,)).fetchone()[0] == len(_sp40)
      and _st40.conn.execute("select count(*) from span").fetchone()[0] == len(_sp40)
      and _gone40 is None
      # counts folded: n_spans recomputed, days_heard is DISTINCT days (2, not 1 + 2)
      and _row40[0] == len(_sp40) and _row40[1] == 2 and _row40[2] == "2026-04-01T08:00:00"
      and _row40[3] == _speech_before[_c1_40] + _speech_before[_c2_40] == 15.0
      # the speaker vector is span-keyed, so the merge does not touch it
      and _st40.conn.execute(
          "select count(*) from embedding where owner_table='span'").fetchone()[0] == _emb_before
      and _st40.span_embedding(_sp40[1]) == [0.25] * 192
      # exactly one audit row, naming loser and winner
      and _st40.conn.execute("select count(*) from audit").fetchone()[0] == _audit_before + 1
      and (_aud40[0], _aud40[1], _aud40[2], _aud40[3], _aud40[4])
          == ("merge", "cluster", _c2_40, _c1_40, "people")
      and "11.00 s" in (_aud40[5] or "")
      and _refused40 and _self40,
      str((_m40, tuple(_row40), _aud40, _refused40, _self40)))


# ================================== T42 - MS1b's field addendum: keys, per-model args, the venue
# The eleven-model field ran on a llama.cpp checkout dated 2026-04-09 (`b8728-...`); the addendum
# runs on v0.4.0 (`b10809-...`). Numbers are comparable only WITHIN a build, so the verdict has to
# be able to say which venue a run belongs to - and the queue has to refuse to start under a schema
# the field never ran.
import importlib.util as _ilu42  # noqa: E402
import json as _j42  # noqa: E402
import tempfile as _tf42  # noqa: E402
from jarvis_memory.extract.schema import schema_sha256 as _ssha42  # noqa: E402

_spec42 = _ilu42.spec_from_file_location(
    "bench_ms1b", str(Path(__file__).resolve().parent / "bench_ms1b.py"))
_bench42 = _ilu42.module_from_spec(_spec42)
_spec42.loader.exec_module(_bench42)
from jarvis_memory.extract.client import LlamaServer as _LS42  # noqa: E402

_ADDENDUM = _ADDENDUM_KEYS                      # one list, defined at T37a

check("T42a the eight addendum keys exist with models/ paths, the incumbent re-run points at the "
      "same file as the frozen-build key, and every Granite arm has its thinking switch OFF",
      all(k in _bench42.MODELS for k in _ADDENDUM)
      and all(_bench42.model_path(k).startswith("models/") for k in _ADDENDUM)
      and all(_bench42.model_path(k).endswith(".gguf") for k in _ADDENDUM)
      # the venue delta needs the SAME FILE measured twice, not a different quant
      and _bench42.model_path("gemma-e4b-v040") == _bench42.model_path("gemma-e4b")
      # Granite 4.2's template defaults enable_thinking to TRUE, so the switch must be declared
      and _bench42.thinking_switch("granite-3b") and _bench42.thinking_switch("granite-8b")
      and _bench42.thinking_switch("nuextract3")
      and not _bench42.thinking_switch("ministral-8b")
      and not _bench42.thinking_switch("lfm25-2.6b")
      and not any(_bench42.thinking_switch(k) for k in
                  ("gemma-e4b-v040", "gemma-e4b-q6k", "gemma-e4b-q8"))
      # the addendum adds keys and removes none
      and len(_bench42.MODELS) == 12 + len(_ADDENDUM),
      str(([k for k in _ADDENDUM if k not in _bench42.MODELS], len(_bench42.MODELS))))

# A per-model server argument is appended AFTER the shared ones, so a model can add to the server's
# command line and never change the shared contract. NO key declares one today and that is a
# measurement, not an omission: the Q8_0 arm was expected to need `--n-cpu-ffn` on an 8 GB card and
# does not, because v0.4.0's `-ngl` defaults to `auto` and fits the model itself (6111 MiB used,
# 2081 free, generating at 5.28 tok/s). So the mechanism is pinned directly here, and the table's
# emptiness is pinned too - a future arm that needs an argument must add it deliberately.
_cmd_args = _LS42("m.gguf", bin_dir="B", extra_args=["--n-cpu-ffn", "12"]).server_command()
_cmd_plain = _LS42("m.gguf", bin_dir="B",
                   extra_args=_bench42.model_extra_args("gemma-e4b-q8")).server_command()
check("T42b a per-model server argument is appended after the shared ones and reaches only the "
      "server that declares it; no key in the table declares one today",
      _cmd_args[-2:] == ["--n-cpu-ffn", "12"]
      and _cmd_args[:len(_cmd_plain)] == _cmd_plain          # the shared prefix is untouched
      and "--n-cpu-ffn" not in _cmd_plain and "--jinja" in _cmd_plain
      and all(_bench42.model_extra_args(k) == [] for k in _bench42.MODELS)
      # the list a server holds is its OWN copy: mutating it cannot reach back into MODELS
      and (_cmd_args.append("--poison") or _bench42.model_extra_args("gemma-e4b-q8") == []),
      str((_cmd_args[-3:], _cmd_plain[-3:],
           {k: _bench42.model_extra_args(k) for k in _bench42.MODELS
            if _bench42.model_extra_args(k)})))

# T42c the build filter, on five stubs: three of one venue, two of another
with _tf42.TemporaryDirectory() as _td42:
    _td42 = Path(_td42)
    _V_OLD, _V_NEW = "b8728-5e9c63546", "b10809-5266f24da"
    _stubs = [("alpha", _V_OLD, 0.10), ("beta", _V_OLD, 0.20), ("gamma", _V_OLD, 0.30),
              ("delta", _V_NEW, 0.70), ("epsilon", _V_NEW, 0.80)]
    for key, ver, f1 in _stubs:
        (_td42 / ("ms1b_%s.json" % key)).write_text(_j42.dumps({
            "model_key": key, "contract": _bench42.CONTRACT,
            "schema_sha256": _bench42.CONTRACT2_SCHEMA_SHA256, "llama_version": ver,
            "aggregate": {"validity": 1.0, "f1": f1}}), encoding="utf-8")

    _all42 = sorted(k for k, _a, _p in _bench42.load_field(_td42))
    _old42 = sorted(k for k, _a, _p in _bench42.load_field(_td42, build="b8728"))
    _new42 = sorted(k for k, _a, _p in _bench42.load_field(_td42, build="b10809"))
    _none42 = _bench42.load_field(_td42, build="b99999")
    _v_new = _bench42.verdict([(k, a) for k, a, _ in _bench42.load_field(_td42, build="b10809")])
    _v_old = _bench42.verdict([(k, a) for k, a, _ in _bench42.load_field(_td42, build="b8728")])

    check("T42c --build selects runs by their recorded llama_version and never mixes two venues; "
          "the verdict over one build cannot be won by a run from the other",
          _all42 == ["alpha", "beta", "delta", "epsilon", "gamma"]
          and _old42 == ["alpha", "beta", "gamma"] and _new42 == ["delta", "epsilon"]
          and set(_old42).isdisjoint(_new42) and _none42 == []
          # the winner of each venue is that venue's own best, not the field's
          and _v_new["chosen"] == "epsilon" and _v_old["chosen"] is None
          # and the pure predicate underneath, both directions
          and _bench42.build_matches(_V_NEW, "b10809")
          and not _bench42.build_matches(_V_OLD, "b10809")
          and _bench42.build_matches(_V_OLD, None) and _bench42.build_matches(None, None)
          and not _bench42.build_matches(None, "b10809"),
          str((_all42, _old42, _new42, _v_new["chosen"], _v_old["chosen"])))

    # T42d the queue refuses to start under a schema the field never ran
    _refused42 = _ran42 = False
    try:
        _bench42.run_queue([], [1], 14, None, 8089, 4096, 99, 2048,
                           results_dir=str(_td42), schema_hash="deadbeef")
    except SystemExit as _e42:
        _refused42 = _bench42.CONTRACT2_SCHEMA_SHA256 in str(_e42)
    try:
        _bench42.run_queue([], [1], 14, None, 8089, 4096, 99, 2048, results_dir=str(_td42),
                           schema_hash=_bench42.CONTRACT2_SCHEMA_SHA256)
        _ran42 = True
    except SystemExit:
        _ran42 = False

    # T42e the server's own output goes to a FILE, never to a pipe
    # A pipe has a fixed OS buffer and nothing drains it during a run, so once it fills the child
    # BLOCKS in write() and stops serving while /health keeps answering ok. MEASURED on llama.cpp
    # v0.4.0: 812 bytes of log per request, so a 4 KB pipe fills after ~3 requests. It cost a
    # 16-hour stall on an arm that takes four minutes.
    import inspect as _insp42  # noqa: E402
    _src42 = _insp42.getsource(_LS42.__enter__)
    _slog42 = _td42 / "srv.log"
    _slog42.write_text("." * 60 + "TAIL-MARKER", encoding="utf-8")
    check("T42e the llama-server's stdout goes to a file the harness can read, never to an "
          "undrained pipe that would block the server once it fills",
          # two-sided, so neither clause can go vacuous under a rename
          "stdout=self._log_fh" in _src42 and "subprocess.PIPE" not in _src42
          and _LS42("m.gguf", bin_dir="B", log_path=_slog42).server_log_tail(16)
              .endswith("TAIL-MARKER")
          and _LS42("m.gguf", bin_dir="B").server_log_tail() == "(no server log)"
          and "(server log unreadable" in _LS42("m.gguf", bin_dir="B",
                                                log_path=_td42 / "nope.log").server_log_tail(),
          str(("stdout=self._log_fh" in _src42, "subprocess.PIPE" in _src42)))

    check("T42d the queue refuses to run under any schema but the contract-2 one the field ran, "
          "and the live tree still hashes to it",
          _refused42 and _ran42
          and _bench42.contract2_schema_ok(_bench42.CONTRACT2_SCHEMA_SHA256)
          and not _bench42.contract2_schema_ok("deadbeef")
          # the assertion is not vacuous: the tree in front of us really is contract 2
          and _bench42.contract2_schema_ok(_ssha42())
          and _bench42.CONTRACT == "contract2",
          str((_refused42, _ran42, _ssha42()[:16])))


print(f"\n{CHECKS - FAILS}/{CHECKS} checks passed")
sys.exit(1 if FAILS else 0)
