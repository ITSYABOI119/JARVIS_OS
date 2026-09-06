"""Candidate validation — the gate between what the extractor may say and what the store writes.

The design's §4.2. A candidate is a plain dict; validation returns (True, None) or (False, reason)
and the FIRST failure wins, in the fixed order below, so a reason is stable enough to be written
into an audit row and compared in a test.

Two properties are load-bearing rather than cosmetic:

  * `stated_*` candidates are speaker-checked against EVERY span they cite. A candidate claiming the
    owner said something, citing a span spoken by another cluster, is a mis-attribution — the one
    error that would let another voice's statement acquire the owner's source rank and supersede a
    real owner statement under R2.
  * `ended` is COERCED to False on an inferred candidate rather than rejected. R4 honours an ending
    only from a speaker; an inference that something stopped is still evidence worth keeping, so the
    candidate is written with its flag cleared instead of being thrown away.
"""
from .registry import PREDICATES, RELATIONS, SOURCE_RANK, EDGE_PREDICATE, PREFERENCE_PREDICATE

STATED = ("stated_owner", "stated_other")


def validate(cand: dict, span_cluster: dict) -> tuple:
    """Validate a candidate against the registry and the spans it cites.

    span_cluster maps a KNOWN span id to the cluster that spoke it. A span id absent from that map
    does not exist in the store, which is a harder failure than a wrong cluster: the candidate
    points at evidence that is not there.

    Mutates `cand` in exactly one way: clears `ended` on an inferred candidate (see the module
    docstring). Returns (ok, reason).
    """
    pid = cand.get("predicate_id")
    if pid not in PREDICATES:
        return False, "unknown predicate"

    span_ids = cand.get("span_ids") or []
    if not span_ids:
        return False, "no span ids"

    for sid in span_ids:
        if sid not in span_cluster:
            return False, f"unknown span {sid}"

    source_kind = cand.get("source_kind")
    if source_kind in STATED:
        speaker = cand.get("speaker_cluster")
        for sid in span_ids:
            if span_cluster[sid] != speaker:
                return False, (f"stated candidate speaker {speaker} differs from span {sid} "
                               f"cluster {span_cluster[sid]}")

    if pid == EDGE_PREDICATE:
        rel = cand.get("relation_id")
        if rel is None:
            return False, "relation_id required"
        if rel not in RELATIONS:
            return False, f"unknown relation {rel}"

    if pid == PREFERENCE_PREDICATE and cand.get("polarity") is None:
        return False, "polarity required"

    if source_kind not in SOURCE_RANK:
        return False, f"bad source_kind {source_kind}"

    if source_kind == "inferred" and cand.get("ended"):
        cand["ended"] = False

    return True, None
