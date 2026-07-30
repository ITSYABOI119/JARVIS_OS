#!/usr/bin/env python3
"""
cm2_floor.py -- Phase C / C/M2 host half: MEASURE the two numbers C/M2 would otherwise guess.

WHY THIS EXISTS. C/M2 replaces g3_select_exact_only with cosine-topk. That swap introduces a risk
nothing else in the arc has: g3_select_exact_only STRUCTURALLY cannot return a wrong record (the
FNV-1a key matches or nothing is selected), whereas cosine-topk can. And C/M0.5 measured every
candidate embedder as strongly ANISOTROPIC -- unrelated-pair cosine sat around 0.5-0.7, not near 0 --
so without a floor an unrelated record scores "similar" and the box injects a wrong prior answer as
context. A false recall is WORSE than no recall (f1fc84b and the EPI_RESP_MAX work both established
that a bad preamble degrades an answer the box would otherwise get right).

So the floor must be MEASURED, and the vector DIMENSION decides the storage layout:
    dim 1024 -> 4096 B/record -> 16 MiB -> 32,769 sectors
    dim  128 ->  512 B/record ->  2 MiB ->  4,097 sectors   (exactly one sector per vector)
Nobody has measured whether 128 holds C/M0.5's 97.2% recall@1. Until now it was a guess.

DELIBERATELY REUSES THE EXISTING DATA -- cm0_recall_set.json and golden_vectors.npz's frozen mu.
C/M0.5's 97.2% came from this set; changing the set would make the comparison meaningless, the same
trap as re-running a bench against a different question set.

PIPELINE ORDER, stated because it is a real choice and not the only one:
    embed at full 1024  ->  [optional mean-projection in 1024]  ->  truncate to dim  ->  L2 renormalise
mu is a FROZEN 1024-dim artifact in golden_vectors.npz, so the projection is applied in the space mu
was estimated in, and Matryoshka truncation happens after. Projecting in a truncated space with a
truncated mu is a different transform; it is not what was measured here.

Strategy is sym:none -- symmetric query-to-query with no prompt, which is what golden_meta.json
records for the winner. C/M0.5's headline finding was that fixing asym->symmetric was what moved
this model from 16.7% to 50% on a misuse, so the strategy is not a free knob.

Run:  py -3 phase3/scripts/embed/cm2_floor.py            # GPU if available
      py -3 phase3/scripts/embed/cm2_floor.py --cpu
"""
import argparse
import json
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
RECALL_JSON = HERE / "cm0_recall_set.json"
GOLDEN_NPZ = HERE / "golden_vectors.npz"
OUT_JSON = HERE / "cm2_floor_results.json"

MODEL = "Qwen/Qwen3-Embedding-0.6B"   # the C/M0.5 winner (97.2% recall@1 off-the-shelf)
DIMS = [1024, 128]
PROJECTIONS = ["raw", "meanproj"]


def l2(a, axis=-1):
    n = np.linalg.norm(a, axis=axis, keepdims=True)
    n = np.where(n == 0.0, 1.0, n)
    return a / n


def mean_project(e, mu):
    """e' = normalize(e - (e.mu) mu). mu is unit-norm and FROZEN (golden_vectors.npz)."""
    mu = mu / np.linalg.norm(mu)
    return l2(e - np.outer(e @ mu, mu))


def prepare(emb_full, mu, dim, proj):
    """Apply the pipeline in the documented order, then truncate + renormalise."""
    e = emb_full
    if proj == "meanproj":
        e = mean_project(e, mu)
    e = e[:, :dim]
    return l2(e)


def op_sweep(sim, N, S, n_pos, n_distinct):
    """THE METRIC THAT ACTUALLY DECIDES DEPLOYMENT, and it is not the pair-count sweep.

    g3_select_semantic returns its TOP match(es) above the floor, so an off-diagonal pair scoring
    high is harmless unless it BEATS the correct record. The pair-count sweep therefore overstates
    the damage. What matters per query is:

      positive query -> top1 is the WRONG record AND above floor   => FALSE RECALL (injects wrong context)
      positive query -> top1 correct but BELOW floor               => missed recall (safe, just no help)
      NEGATIVE query -> anything above floor                       => FALSE RECALL, and this is the
                                                                      worst case: an off-topic question
                                                                      has NO correct stored answer, so
                                                                      any selection at all is wrong.
    """
    top_idx = np.argmax(sim, axis=1)
    top_val = sim[np.arange(n_pos), top_idx]
    correct = top_idx == np.arange(n_pos)
    neg_top = (N @ S.T).max(axis=1) if len(N) else np.array([], dtype=np.float64)

    out = []
    for f in [0.50, 0.55, 0.60, 0.65, 0.70, 0.75]:
        false_pos_q = int(((~correct) & (top_val >= f)).sum())     # wrong record injected
        missed_q = int((correct & (top_val < f)).sum())            # correct record suppressed
        got_q = int((correct & (top_val >= f)).sum())              # useful recall
        neg_false = int((neg_top >= f).sum()) if len(neg_top) else 0
        out.append({
            "floor": f,
            "useful_recall_queries": got_q,
            "missed_recall_queries": missed_q,
            "false_recall_on_positives": false_pos_q,
            "false_recall_on_negatives": neg_false,
            "total_false_recalls": false_pos_q + neg_false,
            "false_recall_rate_pct": round(100.0 * (false_pos_q + neg_false) / (n_pos + max(len(neg_top), 0)), 1),
        })
    return out


def evaluate(later, stored, neg, dim, proj, mu, n_distinct):
    """Return recall@k over the positives plus the full separation picture.

    Corpus = every `stored` item. A probe is a `later` query whose correct answer is the stored item
    at the same index. Negatives have NO correct answer and contribute only unrelated pairs.
    """
    L = prepare(later, mu, dim, proj)
    S = prepare(stored, mu, dim, proj)
    N = prepare(neg, mu, dim, proj) if len(neg) else np.zeros((0, dim), np.float32)

    sim = L @ S.T                       # cosine == dot for L2-normalised vectors
    n_pos = len(L)

    order = np.argsort(-sim, axis=1)
    hit1 = np.array([order[i, 0] == i for i in range(n_pos)])
    hit3 = np.array([i in order[i, :3] for i in range(n_pos)])

    # recall@1 over the DISTINCT positives is the figure C/M0.5's 97.2% refers to; the adversarial
    # tail is reported separately so a change in one is not hidden by the other.
    r1_distinct = float(hit1[:n_distinct].mean())
    r3_distinct = float(hit3[:n_distinct].mean())
    r1_all = float(hit1.mean())
    r3_all = float(hit3.mean())

    true_cos = np.array([sim[i, i] for i in range(n_pos)], dtype=np.float64)

    # UNRELATED = every probe against every NON-matching stored item, plus every negative against
    # every stored item. This is the distribution a floor has to hold back.
    off = ~np.eye(n_pos, len(S), dtype=bool)
    unrel = sim[off].astype(np.float64)
    if len(N):
        unrel = np.concatenate([unrel, (N @ S.T).ravel().astype(np.float64)])

    margin = float(true_cos.min() - unrel.max())

    # ---- RECONCILIATION + how-bad-is-it, because "no PERFECT threshold" is not the same claim as
    # ---- "no USABLE threshold", and a STOP report has to be specific about which one it is.
    #
    # C/M0.5 recorded "CLEAN separation ... the ONLY model with a positive margin". That was almost
    # certainly a different STATISTIC, not a different result: the operationally relevant unrelated
    # set is EVERY stored record a live query gets compared against (all off-diagonal pairs), which
    # is thousands of chances to score high. Scoring against only the 14 hand-authored negatives, or
    # comparing MEANS instead of worst cases, both look far healthier. Compute all three so the
    # discrepancy is explained rather than asserted.
    neg_only = (N @ S.T).ravel().astype(np.float64) if len(N) else np.array([], dtype=np.float64)
    recon = {
        "margin_vs_ALL_unrelated_pairs": margin,
        "margin_vs_hand_authored_negatives_only":
            float(true_cos.min() - neg_only.max()) if len(neg_only) else None,
        "gap_between_MEANS_true_minus_unrelated": float(true_cos.mean() - unrel.mean()),
        "note": ("the deployed selector compares a query against EVERY stored record, so the "
                 "ALL-unrelated-pairs max is the statistic a floor must actually hold back"),
    }

    # A floor sweep: at each candidate, what does it cost and what does it let through? This is what
    # makes the finding actionable instead of just negative.
    sweep = []
    for f in [0.50, 0.55, 0.60, 0.65, 0.70, 0.75, 0.80, 0.85, 0.90]:
        rej = int((true_cos < f).sum())
        adm = int((unrel >= f).sum())
        sweep.append({
            "floor": f,
            "true_pairs_rejected": rej,
            "true_pairs_rejected_pct": round(100.0 * rej / n_pos, 1),
            "unrelated_admitted": adm,
            "unrelated_admitted_pct": round(100.0 * adm / len(unrel), 2),
        })

    res = {
        "dim": dim, "projection": proj,
        "recall@1_distinct": r1_distinct, "recall@3_distinct": r3_distinct,
        "recall@1_all_positives": r1_all, "recall@3_all_positives": r3_all,
        "n_distinct": int(n_distinct), "n_positives": int(n_pos), "n_unrelated_pairs": int(len(unrel)),
        "true_pair_cos": {"min": float(true_cos.min()), "mean": float(true_cos.mean()),
                          "max": float(true_cos.max())},
        "unrelated_pair_cos": {"min": float(unrel.min()), "mean": float(unrel.mean()),
                               "max": float(unrel.max())},
        "margin_min_true_minus_max_unrelated": margin,
        "separable_by_a_single_threshold": bool(margin > 0.0),
        "reconciliation": recon,
        "floor_sweep": sweep,
        "operational": op_sweep(sim, N, S, n_pos, n_distinct),
    }

    if margin > 0.0:
        # Sit the floor in the middle of the gap. Reported WITH its cost on both sides, because a
        # floor that rejects true pairs is not free.
        floor = float(unrel.max() + margin / 2.0)
        res["proposed_floor"] = round(floor, 4)
        res["floor_cost"] = {
            "true_pairs_rejected": int((true_cos < floor).sum()),
            "unrelated_pairs_admitted": int((unrel >= floor).sum()),
        }
    else:
        # NO midpoint is proposed. A threshold in a negative gap is wrong in both directions at once,
        # and pretending otherwise would be exactly the "report a floor as measured when it was
        # chosen" failure this task forbids.
        res["proposed_floor"] = None
        res["floor_cost"] = None
        # How bad is it? Count the overlap so the report can be specific.
        res["overlap"] = {
            "unrelated_above_min_true": int((unrel > true_cos.min()).sum()),
            "true_below_max_unrelated": int((true_cos < unrel.max()).sum()),
        }
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpu", action="store_true")
    a = ap.parse_args()

    from sentence_transformers import SentenceTransformer

    rs = json.loads(RECALL_JSON.read_text(encoding="utf-8"))
    dpos, apos, neg = rs["distinct_positives"], rs["adversarial_positives"], rs["negatives"]
    pos = dpos + apos
    later_txt = [p["later"] for p in pos]
    stored_txt = [p["stored"] for p in pos]
    neg_txt = [n["later"] for n in neg]

    mu = np.load(GOLDEN_NPZ)["mu"].astype(np.float64)

    dev = "cpu" if a.cpu else None
    print(f"loading {MODEL} ...")
    m = SentenceTransformer(MODEL, device=dev)

    def enc(t):
        return np.asarray(m.encode(t, normalize_embeddings=True,
                                   batch_size=16, show_progress_bar=False), dtype=np.float64)

    print(f"embedding {len(later_txt)} probes + {len(stored_txt)} stored + {len(neg_txt)} negatives "
          f"at full dim (sym:none) ...")
    L, S, N = enc(later_txt), enc(stored_txt), enc(neg_txt) if neg_txt else np.zeros((0, 1024))
    assert L.shape[1] == 1024, f"expected 1024-dim, got {L.shape[1]}"

    rows = []
    for dim in DIMS:
        for proj in PROJECTIONS:
            rows.append(evaluate(L, S, N, dim, proj, mu, len(dpos)))

    hdr = f"{'dim':>5} {'proj':>9} {'r@1':>7} {'r@3':>7} {'minTrue':>8} {'maxUnrel':>9} {'margin':>9} {'floor':>7}"
    print("\n" + hdr); print("-" * len(hdr))
    for r in rows:
        f = r["proposed_floor"]
        print(f"{r['dim']:>5} {r['projection']:>9} {r['recall@1_distinct']:>7.3f} "
              f"{r['recall@3_distinct']:>7.3f} {r['true_pair_cos']['min']:>8.4f} "
              f"{r['unrelated_pair_cos']['max']:>9.4f} {r['margin_min_true_minus_max_unrelated']:>9.4f} "
              f"{('%.4f' % f) if f is not None else 'NONE':>7}")

    print("\n=== RECONCILIATION with C/M0.5's 'clean separation' claim ===")
    for r in rows:
        c = r["reconciliation"]
        print(f"  dim={r['dim']:>4} {r['projection']:>9}  all-pairs={c['margin_vs_ALL_unrelated_pairs']:+.4f}  "
              f"negatives-only={c['margin_vs_hand_authored_negatives_only']:+.4f}  "
              f"mean-gap={c['gap_between_MEANS_true_minus_unrelated']:+.4f}")

    print("\n=== FLOOR SWEEP (dim=1024 raw) — what any threshold actually costs ===")
    sw = next(r for r in rows if r["dim"] == 1024 and r["projection"] == "raw")["floor_sweep"]
    print(f"  {'floor':>6} {'true rejected':>16} {'unrelated admitted':>20}")
    for s in sw:
        print(f"  {s['floor']:>6.2f} {str(s['true_pairs_rejected'])+' ('+str(s['true_pairs_rejected_pct'])+'%)':>16}"
              f" {str(s['unrelated_admitted'])+' ('+str(s['unrelated_admitted_pct'])+'%)':>20}")

    print("\n=== OPERATIONAL false-recall rate (what the deployed selector would actually do) ===")
    for r in rows:
        print(f"  --- dim={r['dim']} {r['projection']} ---")
        print(f"  {'floor':>6} {'useful':>7} {'missed':>7} {'false(pos)':>11} {'false(neg)':>11} {'FALSE RATE':>11}")
        for o in r["operational"]:
            print(f"  {o['floor']:>6.2f} {o['useful_recall_queries']:>7} {o['missed_recall_queries']:>7}"
                  f" {o['false_recall_on_positives']:>11} {o['false_recall_on_negatives']:>11}"
                  f" {str(o['false_recall_rate_pct'])+'%':>11}")

    OUT_JSON.write_text(json.dumps({
        "_comment": ("Phase C / C/M2 host half. MEASURED dim + similarity-floor numbers for "
                     "cosine-topk semantic recall, over the SAME cm0_recall_set.json C/M0.5's 97.2% "
                     "came from. Pipeline: embed 1024 -> [mean-project with the frozen mu] -> "
                     "truncate -> L2 renormalise. Strategy sym:none. A NEGATIVE margin means no "
                     "single threshold separates true from unrelated pairs, and no floor is proposed."),
        "model": MODEL, "strategy": "sym:none",
        "pipeline_order": "embed(1024) -> [meanproj in 1024] -> truncate(dim) -> l2",
        "corpus": {"distinct_positives": len(dpos), "adversarial_positives": len(apos),
                   "negatives": len(neg)},
        "results": rows,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"\nwrote {OUT_JSON}")


if __name__ == "__main__":
    main()
