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

print(f"\n{CHECKS - FAILS}/{CHECKS} checks passed")
sys.exit(1 if FAILS else 0)
