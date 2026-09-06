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
    W_SOURCE, HALF_LIFE_DAYS, recency_weight, score, rank,
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


ranked = rank([rrow(1, "inferred", 0), rrow(2, "stated_owner", 0)], NOW)
check("T8h stated outranks inferred at equal lane score",
      [r["row_id"] for r in ranked] == [2, 1], str([r["row_id"] for r in ranked]))

ranked = rank([rrow(1, "inferred", 90), rrow(2, "inferred", 0)], NOW)
check("T8i a fresher non-stated row outranks an older one",
      [r["row_id"] for r in ranked] == [2, 1], str([r["row_id"] for r in ranked]))

ranked = rank([rrow(1, "stated_owner", 400), rrow(2, "inferred", 0)], NOW)
check("T8j an old stated fact still outranks a fresh inferred one (no decay)",
      [r["row_id"] for r in ranked] == [1, 2], str([r["row_id"] for r in ranked]))

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

print(f"\n{CHECKS - FAILS}/{CHECKS} checks passed")
sys.exit(1 if FAILS else 0)
