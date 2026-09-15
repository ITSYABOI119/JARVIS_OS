"""The people layer's two pure rules: personhood, and R7's purge cascade.

The design's §3.4 and §4.3 R7. Clustering itself needs ECAPA embeddings and lands at MS2; what is
here is the part that decides, and it is deliberately free of numpy so the tests and CI can run it.

**Personhood** is a rule, not a judgement: a cluster becomes a person once heard on >= 3 distinct
days with >= 5 spans on each of those days. Per the design's amended §3.4, personhood gates only the
EXISTENCE of the person row and its edges, never the evidence — when an edge is first computed it
counts every retained span of that cluster, including the ones recorded before personhood.

**The purge** is the only delete in the system and it is the owner's alone. It runs on SPANS and
cascades: a derived row resting only on removed spans goes with them; a row that also rests on
surviving spans is KEPT with its surviving evidence and its confidence recomputed. That distinction
is the whole point — a purge must not silently erase a belief the household still supports.
"""

from .registry import EDGE_PREDICATE

MIN_DAYS = 3
MIN_SPANS_PER_DAY = 5


def stated_allowed(cand, cluster_of_ref) -> bool:
    """May this candidate keep its STATED source rank? (Contract 3 f; the design's §4.2, §5 item 4.)

    A `person.relation_to` candidate is a SELF-DESCRIPTION only when its subject is the speaker
    themself - "my husband X and i ...". A claim about two OTHER people, or about the speaker made
    by someone else, is HEARSAY: true or not, the speaker is not the source for it, so it accrues by
    day like any inference instead of arriving at confidence 1.0 and surfacing at once. The measured
    reason: on the corpus's hint spans the chosen extractor produced 2 pronoun edges and 7 name
    edges with the direction REVERSED and `stated` true, which at 1.0 would have surfaced wrong
    immediately.

    `cluster_of_ref` maps a subject ref to a cluster id, and must carry BOTH forms a ref can take:
    the cluster id as a STRING - which is what `derive._subject` writes for a first-person `about` -
    and each household member's lower-cased name, because the extractor names a speaker by their own
    name. An unknown ref is NOT the speaker: it cannot be shown to be a self-description, so it is
    demoted. Pure; the caller decides what to do with the answer.
    """
    if not cand or cand.get("predicate_id") != EDGE_PREDICATE:
        return True
    if not str(cand.get("source_kind") or "").startswith("stated"):
        return True
    ref = (cand.get("subject") or {}).get("ref")
    if ref is None:
        return False
    return (cluster_of_ref or {}).get(str(ref).strip().lower()) == cand.get("speaker_cluster")


def is_person(day_counts: dict) -> bool:
    """True iff at least MIN_DAYS dates carry at least MIN_SPANS_PER_DAY spans each.

    day_counts maps a date string to the number of spans that cluster spoke on it. A heavy two-day
    visitor is not a person; three quiet days are not either.
    """
    if not day_counts:
        return False
    full = sum(1 for n in day_counts.values() if n >= MIN_SPANS_PER_DAY)
    return full >= MIN_DAYS


def purge_plan(cluster_span_ids: set, derived: list) -> dict:
    """Plan R7's cascade over derived rows.

    `derived` rows are {'table', 'row_id', 'span_ids': set}. A row is

      deleted    iff its spans are non-empty and wholly inside the purged cluster's spans;
      recomputed iff it overlaps them partly (some evidence survives);
      untouched  otherwise, including a row carrying no spans at all — such a row rests on no
                 evidence from this cluster, so the purge has nothing to say about it.

    Returns {'delete': [(table, row_id), ...], 'recompute': [(table, row_id), ...]} in input order,
    so the store's audit rows come out deterministic.
    """
    cluster_span_ids = set(cluster_span_ids or ())
    delete, recompute = [], []
    for row in derived or ():
        spans = set(row.get("span_ids") or ())
        if not spans:
            continue
        overlap = spans & cluster_span_ids
        if not overlap:
            continue
        if spans <= cluster_span_ids:
            delete.append((row["table"], row["row_id"]))
        else:
            recompute.append((row["table"], row["row_id"]))
    return {"delete": delete, "recompute": recompute}
