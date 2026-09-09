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


def resolve_subject(ref, kind, names_by_cluster, clusters_by_name):
    """(kind, resolved) where resolved is a cluster id when the subject is a known person.

    Accepts the three shapes that reach it: the oracle's "owner"/"partner", a model's cluster id as
    a string, and a name. An unknown name resolves to the lower-cased name itself, so two sides that
    both say an unknown name still match while neither is silently turned into a person.
    """
    if kind != "person":
        return kind, (str(ref).strip().lower() if ref is not None else None)
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
    subj = cand.get("subject") or {}
    kind, resolved = resolve_subject(subj.get("ref"), subj.get("kind"),
                                     names_by_cluster, clusters_by_name)
    return (cand.get("predicate_id"), kind, resolved,
            (cand.get("object_norm") or "").strip().lower(),
            frozenset(cand.get("span_ids") or ()))


def match(pred, gold, names_by_cluster=None, clusters_by_name=None) -> bool:
    """The pre-registered match: predicate, resolved subject, exact object_norm, >= 1 shared span."""
    p = _key(pred, names_by_cluster, clusters_by_name)
    g = _key(gold, names_by_cluster, clusters_by_name)
    return p[:4] == g[:4] and bool(p[4] & g[4])


def lenient_match(pred, gold, names_by_cluster=None, clusters_by_name=None) -> bool:
    """As `match`, but either normalised object may contain the other. REPORTED, never banded."""
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

    rel_gold = [c for c in golds if c.get("predicate_id") == "person.relation_to"]
    rel_hit = sum(1 for i, j in strict if golds[j].get("predicate_id") == "person.relation_to")
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
        "relation_recall": (rel_hit / len(rel_gold)) if rel_gold else 0.0,
        "relation_gold": len(rel_gold), "relation_matched": rel_hit,
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
