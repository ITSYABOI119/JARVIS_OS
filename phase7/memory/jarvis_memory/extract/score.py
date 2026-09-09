"""Scoring an extractor against the oracle: the pre-registered match, F1, and JSON validity.

The match is deliberately STRICT (design §4.2, the MS1b bands): same predicate, same RESOLVED
subject, `object_norm` exactly equal, and at least one shared span id. Subject resolution is what
makes the comparison fair rather than lenient by accident — the oracle says "owner"/"partner" and a
model says "1" or "sam", and all three must land on the same cluster before anything is compared.

A LENIENT object match (either normalised object contained in the other) is computed beside it and
REPORTED, never banded: it says how much of a miss is the object's wording rather than the
extraction, which is a different problem with a different fix.
"""
from ..candidate import validate
from ..registry import EDGE_PREDICATE, normalise_object


def resolve_person(ref, names_by_cluster=None, clusters_by_name=None):
    """A cluster id, or None when the reference cannot be resolved to a person.

    Deliberately NOT `resolve_subject`'s fallback-to-the-name behaviour. This resolves the OBJECT of
    a relation, where an unresolvable word must never match: "she" and "my wife" are references to
    somebody, but a relation edge whose far end is unknown is not the same edge as one whose far
    end is the partner, and scoring them equal would credit an extraction that never identified a
    person. Unresolvable in, None out, and None never equals None in `match`.
    """
    if ref is None:
        return None
    s = str(ref).strip().lower()
    if s == "owner":
        return 1
    if s == "partner":
        return 2
    try:
        return int(s)
    except (TypeError, ValueError):
        pass
    return (clusters_by_name or {}).get(s)


def resolve_subject(ref, kind, names_by_cluster, clusters_by_name):
    """(kind, resolved) where resolved is a cluster id when the subject is a known person.

    Accepts the three shapes that reach it: the oracle's "owner"/"partner", a model's cluster id as
    a string, and a name. An unknown name resolves to the lower-cased name itself, so two sides that
    both say an unknown name still match while neither is silently turned into a person.

    For a NON-person subject the resolution is total on purpose: the oracle writes a household
    candidate's ref as None and `derive` writes "household", and without folding both onto the kind
    every household.topic and household.routine prediction would be a guaranteed non-match — a
    scoring artefact, not an extraction failure. Two of nine predicates and 80 of the corpus's 370
    gold candidates ride on this line.
    """
    if kind != "person":
        s = str(ref).strip().lower() if ref is not None else ""
        return kind, (kind if (not s or s == kind) else s)
    s = str(ref).strip().lower() if ref is not None else ""
    if s == "owner":
        return kind, 1
    if s == "partner":
        return kind, 2
    try:
        return kind, int(s)
    except (TypeError, ValueError):
        pass
    if s in (clusters_by_name or {}):
        return kind, clusters_by_name[s]
    return kind, s or None


def _key(cand, names_by_cluster, clusters_by_name):
    """(predicate, subject kind, resolved subject, value, spans).

    The VALUE is computed with the ONE normaliser, from the object AS SAID, on BOTH sides — never
    read from a caller's `object_norm`. That is the narrowed contract's whole point: the article in
    "a plumber" is the code's problem, not a difference between two extractions. The oracle's own
    `object_norm` agrees with the normaliser for all 370 candidates over ten households (T36c), so
    computing it here changes no gold value; it only stops a prediction being judged on a field the
    model is no longer asked to produce.
    """
    subj = cand.get("subject") or {}
    kind, resolved = resolve_subject(subj.get("ref"), subj.get("kind"),
                                     names_by_cluster, clusters_by_name)
    raw = cand.get("object")
    value = normalise_object(raw if raw is not None else cand.get("object_norm"))
    return (cand.get("predicate_id"), kind, resolved, value,
            frozenset(cand.get("span_ids") or ()))


def _relation_key(cand, names_by_cluster, clusters_by_name):
    """A relation is (subject person, object person, relation id) — direction-aware.

    The object is resolved to a PERSON, not normalised as text: the oracle says "partner" and a
    model says the partner's name, and those are the same edge. An unresolvable object ("she")
    yields None and can never match, which is the honest outcome — the model referred to somebody
    without identifying them, and the people layer is what will one day close that gap (MS2).
    """
    subj = cand.get("subject") or {}
    _, subject = resolve_subject(subj.get("ref"), subj.get("kind"),
                                 names_by_cluster, clusters_by_name)
    target = resolve_person(cand.get("object"), names_by_cluster, clusters_by_name)
    return (subject, target, cand.get("relation_id"),
            frozenset(cand.get("span_ids") or ()))


def match(pred, gold, names_by_cluster=None, clusters_by_name=None) -> bool:
    """The pre-registered match: predicate, resolved subject, value, >= 1 shared span.

    `person.relation_to` is matched on the resolved OBJECT PERSON and the relation id instead of on
    an object string, because the two sides legitimately spell the far end differently and the
    corpus's own convention keys a relation candidate by its relation id.
    """
    if pred.get("predicate_id") != gold.get("predicate_id"):
        return False
    if gold.get("predicate_id") == EDGE_PREDICATE:
        p = _relation_key(pred, names_by_cluster, clusters_by_name)
        g = _relation_key(gold, names_by_cluster, clusters_by_name)
        return (p[0] is not None and p[1] is not None and p[2] is not None
                and p[:3] == g[:3] and bool(p[3] & g[3]))
    p = _key(pred, names_by_cluster, clusters_by_name)
    g = _key(gold, names_by_cluster, clusters_by_name)
    return p[:4] == g[:4] and bool(p[4] & g[4])


def lenient_match(pred, gold, names_by_cluster=None, clusters_by_name=None) -> bool:
    """As `match`, but either normalised object may contain the other. REPORTED, never banded.

    A relation has no text object to be lenient about — its far end is a person or it is nothing —
    so it delegates to the strict match rather than inventing a second, softer edge rule.
    """
    if gold.get("predicate_id") == EDGE_PREDICATE:
        return match(pred, gold, names_by_cluster, clusters_by_name)
    p = _key(pred, names_by_cluster, clusters_by_name)
    g = _key(gold, names_by_cluster, clusters_by_name)
    if p[:3] != g[:3] or not (p[4] & g[4]):
        return False
    a, b = p[3], g[3]
    return bool(a) and bool(b) and (a in b or b in a)


def _f1(precision, recall):
    return (2 * precision * recall / (precision + recall)) if (precision + recall) else 0.0


def _pr(matched, n_pred, n_gold):
    precision = (matched / n_pred) if n_pred else 0.0
    recall = (matched / n_gold) if n_gold else 0.0
    return precision, recall, _f1(precision, recall)


def _greedy(preds, golds, fn, names_by_cluster, clusters_by_name):
    """One gold may be claimed once. Greedy is exact here: a prediction matching two golds would
    have to differ from neither on predicate, subject, object and span, i.e. be a duplicate."""
    used, pairs = set(), []
    for i, p in enumerate(preds):
        for j, g in enumerate(golds):
            if j in used:
                continue
            if fn(p, g, names_by_cluster, clusters_by_name):
                used.add(j)
                pairs.append((i, j))
                break
    return pairs


def score_household(preds, golds, names_by_cluster=None, clusters_by_name=None) -> dict:
    """Precision / recall / F1 overall, per predicate, plus the relation and preference reports."""
    preds = list(preds or ())
    golds = list(golds or ())
    strict = _greedy(preds, golds, match, names_by_cluster, clusters_by_name)
    lenient = _greedy(preds, golds, lenient_match, names_by_cluster, clusters_by_name)
    precision, recall, f1 = _pr(len(strict), len(preds), len(golds))
    lp, lr, lf1 = _pr(len(lenient), len(preds), len(golds))

    per = {}
    pids = {c.get("predicate_id") for c in preds} | {c.get("predicate_id") for c in golds}
    matched_pred_idx = {i for i, _ in strict}
    matched_gold_idx = {j for _, j in strict}
    for pid in sorted(x for x in pids if x):
        np_ = sum(1 for c in preds if c.get("predicate_id") == pid)
        ng = sum(1 for c in golds if c.get("predicate_id") == pid)
        nm = sum(1 for i, j in strict if preds[i].get("predicate_id") == pid)
        p_, r_, f_ = _pr(nm, np_, ng)
        per[pid] = {"precision": p_, "recall": r_, "f1": f_,
                    "n_pred": np_, "n_gold": ng, "n_match": nm}

    # The relation recall is reported SPLIT, because the two halves measure different things and an
    # average of them measures neither. The STATED half ("my husband <name>", spoken by cluster 2)
    # is an extraction task: the edge is on the page. The INFERRED half is the corpus's pronoun
    # hints ("she picked the kids up"), which is the people layer's job to accrue over days and is
    # expected to be low here — MS1b has no people layer, and a model that scored well on it would
    # be guessing. Reported, never banded, and never merged into one number.
    rel_gold = [c for c in golds if c.get("predicate_id") == EDGE_PREDICATE]
    rel_hit = sum(1 for i, j in strict if golds[j].get("predicate_id") == EDGE_PREDICATE)
    rel_stated_gold = [c for c in rel_gold if str(c.get("source_kind", "")).startswith("stated")]
    rel_inf_gold = [c for c in rel_gold if not str(c.get("source_kind", "")).startswith("stated")]
    rel_stated_hit = sum(1 for i, j in strict
                         if golds[j].get("predicate_id") == EDGE_PREDICATE
                         and str(golds[j].get("source_kind", "")).startswith("stated"))
    rel_inf_hit = rel_hit - rel_stated_hit
    pref_gold = [c for c in golds if c.get("predicate_id") == "owner.prefers"]
    pref_pred = [c for c in preds if c.get("predicate_id") == "owner.prefers"]
    pref_pairs = [(i, j) for i, j in strict if golds[j].get("predicate_id") == "owner.prefers"]
    pol_ok = sum(1 for i, j in pref_pairs
                 if (preds[i].get("polarity") or None) == (golds[j].get("polarity") or None))
    pp, prc, pf = _pr(len(pref_pairs), len(pref_pred), len(pref_gold))

    # NOT rounded: these are data, and a rounded metric cannot be compared against a
    # pre-registered value at full precision (T35e). The bench rounds when it PRINTS.
    return {
        "n_pred": len(preds), "n_gold": len(golds), "n_match": len(strict),
        "precision": precision, "recall": recall, "f1": f1,
        "lenient_precision": lp, "lenient_recall": lr, "lenient_f1": lf1,
        "per_predicate": per,
        # REPORTED beside the band, never in it (MS1b contract 2).
        # `f1_scorable` answers a different question from `f1`: what does a PER-SPAN extractor get
        # over the gold it can actually reach? The 80 inferred edges in this corpus are pronoun
        # hints the people layer accrues over days; no single-span call can produce them, so they
        # sit in the recall denominator of `f1` as a permanent, structural deduction. Precision is
        # deliberately UNCHANGED - a prediction is still right or wrong against the whole oracle.
        # The band remains `f1`; this is the honest companion, not a softer scoring.
        "scorable_gold": len(golds) - len(rel_inf_gold),
        "f1_scorable": _f1(precision,
                           (len(strict) / (len(golds) - len(rel_inf_gold)))
                           if (len(golds) - len(rel_inf_gold)) else 0.0),
        # Predictions on predicates this household's oracle has NO gold for at all (person.name,
        # person.trait in this corpus). They can only ever be false positives, so counting them
        # separately says how much of a model's precision loss is "invented a predicate the corpus
        # never uses" rather than "got this fact wrong".
        "zero_gold_predictions": sum(
            1 for c in preds
            if not any(g.get("predicate_id") == c.get("predicate_id") for g in golds)),
        "relation_recall": (rel_hit / len(rel_gold)) if rel_gold else 0.0,
        "relation_gold": len(rel_gold), "relation_matched": rel_hit,
        "relation_stated_recall": ((rel_stated_hit / len(rel_stated_gold))
                                   if rel_stated_gold else 0.0),
        "relation_stated_gold": len(rel_stated_gold), "relation_stated_matched": rel_stated_hit,
        "relation_inferred_recall": ((rel_inf_hit / len(rel_inf_gold)) if rel_inf_gold else 0.0),
        "relation_inferred_gold": len(rel_inf_gold), "relation_inferred_matched": rel_inf_hit,
        "preference_precision": pp, "preference_recall": prc, "preference_f1": pf,
        "preference_polarity_agreement": (pol_ok / len(pref_pairs)) if pref_pairs else 0.0,
        "false_positive_idx": [i for i in range(len(preds)) if i not in matched_pred_idx],
        "false_negative_idx": [j for j in range(len(golds)) if j not in matched_gold_idx],
    }


def validity(results, span_cluster) -> tuple:
    """(valid, total, reasons). A call is valid iff it parsed AND every candidate validates.

    An EMPTY candidates list is VALID - most utterances are small talk, and the contract says the
    correct answer there is to say nothing. Counting an empty list as invalid would make the band
    reward hallucination.
    """
    valid = 0
    reasons = {}
    for r in results:
        cands = r.get("candidates")
        if cands is None:
            reasons["unparsed"] = reasons.get("unparsed", 0) + 1
            continue
        ok = True
        for c in cands:
            good, why = validate(dict(c), span_cluster)
            if not good:
                key = str(why).split(" ")[0] if why else "invalid"
                reasons[key] = reasons.get(key, 0) + 1
                ok = False
                break
        if ok:
            valid += 1
    return valid, len(list(results)), reasons
