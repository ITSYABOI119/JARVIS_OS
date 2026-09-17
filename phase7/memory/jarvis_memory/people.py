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

import re

from .confidence import SURFACE_THRESHOLD
from .registry import (
    CONTRA_CUES, EDGE_PREDICATE, FIRST_PERSON_PLURAL, HOUSEHOLD_CUES, KIN_CUES,
    RELATION_PRECEDENCE, THIRD_PERSON_PRONOUNS,
)

MIN_DAYS = 3
MIN_SPANS_PER_DAY = 5

# The evidence rules' two thresholds (MS2a-2; the design's §5). ER1 is co-presence: a date on which
# the owner and one other cluster each speak at least this many spans. The pronoun window is how far
# back the resolver may look when nobody was heard on the span's own day.
ER1_MIN_SPANS = 3
PRONOUN_WINDOW_DAYS = 3

_WORD_RE = re.compile(r"[a-z']+")


def words(text) -> list:
    """Lower-cased word tokens, in order, apostrophes kept.

    In ORDER because a CONTRA cue is a phrase - `staying with us` has to match three whole words in
    sequence, and a set would match them scattered across an unrelated sentence.
    """
    return _WORD_RE.findall(str(text if text is not None else "").lower())


def _phrase_present(toks, phrase) -> bool:
    """Whether `phrase` appears in `toks` as a whole-word sequence."""
    parts = str(phrase or "").split()
    if not parts:
        return False
    n = len(parts)
    return any(toks[i:i + n] == parts for i in range(len(toks) - n + 1))


def resolve_pronoun(day, heard_by_day, window_days=PRONOUN_WINDOW_DAYS):
    """Which non-owner cluster a third-person pronoun means, or None (the design's §5, amended).

    `day` is an INT - a date ordinal in the store, a corpus day number in the harness - and
    `heard_by_day` maps such an int to the set of non-owner clusters heard that day.

    The rule, and each clause earns its place: the UNIQUE non-owner cluster heard on the span's own
    day; if two or more were heard it is unresolved and does NOT fall back, because the ambiguity is
    on the day itself; if NOBODY was heard, the most recent day within the previous `window_days` on
    which any non-owner cluster was heard decides, and only when that day holds exactly one.

    A UNION over the window would be wrong and the corpus says so: on seed 1, day 11 has nobody,
    day 10 holds the partner alone and day 9 holds the partner and the visitor - so the union is
    ambiguous while the most-recent-day rule resolves, which is the difference between the day-11
    contradiction landing and being thrown away as a reject.
    """
    heard_by_day = heard_by_day or {}
    same = set(heard_by_day.get(day) or ())
    if same:
        return next(iter(same)) if len(same) == 1 else None
    for back in range(1, int(window_days) + 1):
        got = set(heard_by_day.get(day - back) or ())
        if got:
            return next(iter(got)) if len(got) == 1 else None
    return None


def er1_clusters(counts_by_cluster, owner_cluster) -> set:
    """ER1 co-presence: the non-owner clusters sharing a date with the owner, both speaking enough.

    The owner's own count gates the whole set: a day on which the owner barely spoke is not a day
    the household was observed together, whatever anyone else did.
    """
    counts = dict(counts_by_cluster or {})
    if counts.get(owner_cluster, 0) < ER1_MIN_SPANS:
        return set()
    return {c for c, n in counts.items() if c != owner_cluster and n >= ER1_MIN_SPANS}


def span_evidence(text, day, heard_by_day, er1) -> dict:
    """What ONE owner span contributes: its target, which rules fire, and whether it is a reject.

    Targets (design §5, amended (2)): a span carrying a third-person pronoun targets whatever that
    pronoun RESOLVES to and nothing otherwise - it never falls through to the ER1 cluster, because
    falling through would answer a question the speaker left ambiguous. A span with no third-person
    pronoun targets the unique ER1 cluster of the day, if there is exactly one.

    ER2 needs a household cue and either a first-person plural or a third-person pronoun; ER3 needs
    a kin cue; ER-C needs a CONTRA phrase AND a pronoun-resolved target specifically, so a
    contradiction about nobody in particular contradicts nobody in particular.

    `reject` is the span-level half of the Rejects rule: a cue fired with no target to give it to.
    The store owns the other half (an ER-C with no edge to link). Pure - the caller writes the rows.
    """
    toks = words(text)
    ws = set(toks)
    er1 = set(er1 or ())

    has_third = bool(ws & THIRD_PERSON_PRONOUNS)
    resolved = resolve_pronoun(day, heard_by_day) if has_third else None
    if has_third:
        target = resolved
    else:
        target = next(iter(er1)) if len(er1) == 1 else None

    er2_cue = bool(ws & HOUSEHOLD_CUES) and (bool(ws & FIRST_PERSON_PLURAL) or has_third)
    er3_cue = bool(ws & KIN_CUES)
    erc_cue = any(_phrase_present(toks, c) for c in CONTRA_CUES)
    # ER-C's target is the pronoun-resolved cluster ONLY: never the ER1 fallback.
    erc_target = resolved if has_third else None

    in_er1 = target is not None and target in er1
    return {
        "target": target,
        "partner": bool(er2_cue and in_er1),
        "spouse": bool(er3_cue and in_er1),
        "contra": bool(erc_cue and erc_target is not None),
        "reject": bool(((er2_cue or er3_cue) and target is None)
                       or (erc_cue and erc_target is None)),
    }


def finest_surfaced(edges):
    """The finest SURFACED edge among rows of {relation_id, confidence}, or None.

    Finest by RELATION_PRECEDENCE; a relation the precedence does not name sorts last rather than
    raising, so an edge written by a future predicate can never make this function the thing that
    fails. Rows below the surfacing threshold are not candidates at all.
    """
    best = None
    for e in edges or ():
        conf = e.get("confidence")
        if conf is None or conf < SURFACE_THRESHOLD:
            continue
        rid = e.get("relation_id")
        rank = (RELATION_PRECEDENCE.index(rid) if rid in RELATION_PRECEDENCE
                else len(RELATION_PRECEDENCE))
        if best is None or rank < best[0]:
            best = (rank, e)
    return best[1] if best else None


def score_pairs(surfaced, gold_by_pair) -> dict:
    """Score the FINEST surfaced edge per ordered (from, to) pair (design §5, amended (5)).

    One pair contributes one judgement however many relations it surfaced, which is the whole point:
    the rules add a `partner` edge beside an oracle `spouse` edge, and counting both would penalise
    the layer for being more specific about the same relationship.

    fine = the pair's gold relation; coarse = `partner` where the gold is `spouse` (a coarser TRUE
    statement); wrong = anything else, a surfaced pair with no gold included - surfacing a
    relationship that was never planted is an over-claim, not a neutral act.
    """
    by_pair = {}
    for r in surfaced or ():
        by_pair.setdefault((r.get("from_person"), r.get("to_person")), []).append(r)
    out = {"pairs": 0, "fine": 0, "coarse": 0, "wrong": 0}
    for pair, rows in by_pair.items():
        finest = finest_surfaced(rows)
        if finest is None:
            continue
        out["pairs"] += 1
        gold = (gold_by_pair or {}).get(pair)
        rid = finest.get("relation_id")
        if gold is not None and rid == gold:
            out["fine"] += 1
        elif gold == "spouse" and rid == "partner":
            out["coarse"] += 1
        else:
            out["wrong"] += 1
    return out


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
