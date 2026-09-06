"""The write path — rules R1 to R4 as one pure decision over a candidate and its slot.

The design's §4.3. `decide` reads the candidate and the CURRENT rows on its slot and returns what to
write; it touches no database and no model, so the store can apply the whole result inside a single
transaction and a test can assert every branch without one.

  R1 append only      nothing is updated in place; a supersession is a NEW row plus a
                      superseded_by pointer on the old one, and the old row keeps its spans.
  R2 source rank      stated_owner > stated_other > inferred. An inferred candidate never
                      supersedes a stated row - it is appended BESIDE it.
  R3 freshness        among equal-ranked rows the newest valid_from wins, by comparison
                      (freshness.newer), never by a model's judgement.
  R4 multi-valued     accumulate; only an explicit speaker statement that a value has ended
                      closes a row.

The one branch worth reading twice is `history`. A candidate that LOSES to an existing row is still
written — appended already closed, pointing at its winner — because the store is append-only and a
late-arriving older statement is evidence, not noise. Such a candidate closes NOTHING, including a
lower-ranked row it would otherwise have outranked: it is not the current value, so it has no
standing to end anything. Any closes collected before the loss was discovered are dropped.
"""
from .registry import SOURCE_RANK, arity
from .freshness import newer, valid_from as _valid_from

STATED = ("stated_owner", "stated_other")


def _rank(source_kind: str) -> int:
    return SOURCE_RANK.get(source_kind, 0)


def _build_row(cand: dict, recorded_at: str, valid_to=None, superseded_by=None) -> dict:
    row = dict(cand)
    row["valid_from"] = _valid_from(cand)
    row["recorded_at"] = recorded_at
    row["valid_to"] = valid_to
    row["superseded_by"] = superseded_by
    return row


def _result(outcome, new_row=None, close=None, audit=None) -> dict:
    return {"outcome": outcome, "new_row": new_row, "close": close or [], "audit": audit or []}


def decide(cand: dict, existing_current: list, recorded_at: str) -> dict:
    """Decide what a candidate does to its slot.

    `existing_current` are the rows on the SAME slot with valid_to still null — same subject and
    predicate (and, for an edge, the same to_person; for a preference, the same topic_norm). The
    caller selects the slot; this function never guesses it.
    """
    existing_current = list(existing_current or [])
    vf = _valid_from(cand)

    # ---------------------------------------------------------------- R4, multi-valued
    if arity(cand["predicate_id"]) == "multi":
        honoured = bool(cand.get("ended")) and cand.get("source_kind") in STATED
        if honoured:
            for e in existing_current:
                if e.get("object_norm") == cand.get("object_norm"):
                    return _result(
                        "close",
                        new_row=None,
                        close=[{"row_id": e["id"], "valid_to": vf}],
                        audit=[{"op": "close", "rule": "R4", "loser": e["id"],
                                "note": f"ended by {cand.get('source_kind')} on {vf}"}],
                    )
            # Nothing matched: the statement that something ended is still evidence, so the
            # candidate is appended rather than dropped.
        return _result("coexist", new_row=_build_row(cand, recorded_at))

    # -------------------------------------------------------------- single-valued, empty slot
    if not existing_current:
        return _result("append", new_row=_build_row(cand, recorded_at))

    # -------------------------------------------------------------- single-valued, occupied slot
    r_c = _rank(cand.get("source_kind"))
    close = []
    audit = []
    lost_to = None

    for e in existing_current:
        r_e = _rank(e.get("source_kind"))
        if r_c > r_e:
            close.append({"row_id": e["id"], "valid_to": vf})
            audit.append({"op": "supersede", "rule": "R2", "loser": e["id"],
                          "note": f"{cand.get('source_kind')} outranks {e.get('source_kind')}"})
        elif r_c == r_e:
            if newer(cand, e):
                close.append({"row_id": e["id"], "valid_to": vf})
                audit.append({"op": "supersede", "rule": "R3", "loser": e["id"],
                              "note": f"newer valid_from {vf} beats {_valid_from(e)}"})
            elif lost_to is None:
                lost_to = e
        # r_c < r_e: the higher-ranked row stands and the candidate sits beside it.

    if lost_to is not None:
        # A losing candidate closes nothing at all; drop whatever was collected above.
        return _result(
            "history",
            new_row=_build_row(cand, recorded_at,
                               valid_to=_valid_from(lost_to), superseded_by=lost_to["id"]),
            close=[],
            audit=[{"op": "supersede", "rule": "R3", "loser": "new",
                    "note": f"older than row {lost_to['id']}; appended as history"}],
        )

    if close:
        return _result("supersede", new_row=_build_row(cand, recorded_at), close=close, audit=audit)

    return _result("beside", new_row=_build_row(cand, recorded_at))
