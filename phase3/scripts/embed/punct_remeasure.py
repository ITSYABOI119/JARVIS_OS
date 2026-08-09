#!/usr/bin/env python3
"""Does semantic recall's measured behaviour survive REAL punctuation? MEASURE ONLY.

WHY THIS EXISTS. Two deployed claims about a default-ON feature were measured on CLEAN,
UNPUNCTUATED eval strings:

  * "about half of paraphrases recall — 19 of 36 at the 0.55 floor"  (a DOCUMENTATION claim,
    and one the console shows a human)
  * "0 false recalls observed on 36"                                  (a SAFETY claim — false
    recall is the risk cosine-topk introduced that exact-key structurally could not have)

Real traffic is not clean: measured on the box's own control-IN store, 24.5% of distinct
operator queries carry a trailing '?'. And a single '?' was already measured to move cosine by
0.038 — 68% of the 0.056 that made the first C/M2b G2 attempt miss. So the question is whether
those two numbers describe the deployed feature or only the eval set.

WHAT THIS DOES NOT DO, deliberately:
  * it does NOT normalise before embedding. The 0.55 floor was MEASURED on un-normalised
    strings, so normalising would invalidate the very number it appears to rescue.
  * it does NOT re-measure or propose a floor. That is a separate decision.
  * it does NOT reimplement the pipeline. `prepare()` and `op_sweep()` are IMPORTED from
    cm2_floor so this cannot drift from the deployed 128 / mean-projected / 0.55 config —
    the C/M2b precedent, and the reason the C/M4 wrong-space bug could happen at all.

ARM A IS A HARD CONTROL. It must reproduce useful=19 / false=0. If it does not, the harness
has diverged from the recorded measurement and nothing downstream means anything.

Model-gated-local (needs Qwen/Qwen3-Embedding-0.6B). No CI step — the cm2_floor.py precedent.

Usage:  py -3 phase3/scripts/embed/punct_remeasure.py [--cpu]
"""

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import cm2_floor  # noqa: E402  — the pipeline and the metric, IMPORTED not reimplemented

RECALL_JSON = HERE / "cm0_recall_set.json"
GOLDEN_NPZ = HERE / "golden_vectors.npz"
OUT_JSON = HERE / "punct_remeasure_results.json"

# The DEPLOYED configuration. Not swept — this asks whether the deployed point survives.
DIM = 128
PROJ = "meanproj"
FLOOR = 0.55            # `>=`, matching g3_select_semantic. `>` would measure a different rule.

# ---------------------------------------------------------------------------------------
# SHAPING RULES — derived from the box's own control-IN store (49 distinct real queries,
# read via parse_episodic.py at LBA 21,140,000), NOT invented to taste. Frequencies measured:
#
#   trailing '?'        12/49  (24.5%)   -> R1, APPLIED
#   leading capital      4/49  ( 8.2%)   -> R2, APPLIED
#   contraction          5/49  (10.2%)   -> CONSIDERED, EXCLUDED (see below)
#   trailing '.'         0/49  ( 0.0%)   -> EXCLUDED: never observed, so it would be INVENTED
#
# Both applied rules are PURELY ADDITIVE — they add a character, they do not rewrite words.
# Contractions are excluded because they are a WORD-LEVEL REWRITE ("does not" -> "doesn't"),
# which is a different experiment from "the same words with punctuation"; the evidence is also
# thinner, and the corpus already contains a natural contraction ("isn't") so the clean/shaped
# distinction would be muddied. Recorded rather than silently dropped.
# ---------------------------------------------------------------------------------------

INTERROGATIVE = re.compile(
    r"^\s*(what|how|why|who|when|where|which|whose|is|are|was|were|do|does|did|can|could|"
    r"should|would|will|has|have|had|may|might)\b", re.I)


def shape(s):
    """R1 + R2. Additive only: a '?' if it reads as a question, and a leading capital."""
    out = s
    if INTERROGATIVE.match(out) and not out.rstrip().endswith("?"):
        out = out.rstrip() + "?"                      # R1
    if out[:1].islower():
        out = out[:1].upper() + out[1:]               # R2
    return out


def shape_q_only(s):
    """R1 alone — isolates the dominant term (the measured 0.038-per-'?' hazard)."""
    if INTERROGATIVE.match(s) and not s.rstrip().endswith("?"):
        return s.rstrip() + "?"
    return s


def content_aware_false(sim, stored_txt, n_pos, floor):
    """SUPPLEMENTARY, and it does NOT replace op_sweep's pre-registered number.

    op_sweep scores correctness by INDEX identity (`top_idx == arange(n_pos)`). This corpus
    contains stored texts that are DUPLICATED BY CONSTRUCTION — the adversarial positives are
    harder paraphrases of questions that also appear in the distinct set, so 'what is a mutex'
    sits at index 11 AND 36, and 'what is a page fault' at 0 AND 39. When argmax lands on the
    OTHER copy of byte-identical text, op_sweep records a false recall even though the injected
    context is character-for-character what the correct record holds — a tie broken by
    first-wins, not a wrong answer reaching the user.

    This counts a false recall only when the winning record's TEXT differs from the correct
    record's TEXT, i.e. what a human would actually experience as wrong context. Reported
    ALONGSIDE the pre-registered metric so the artifact is visible and quantified rather than
    silently corrected away.
    """
    top = np.argmax(sim, axis=1)
    topv = sim[np.arange(n_pos), top]
    out = []
    for i in range(n_pos):
        if top[i] != i and topv[i] >= floor:
            same_text = stored_txt[top[i]] == stored_txt[i]
            out.append({"query_index": i, "won_index": int(top[i]), "cos": round(float(topv[i]), 4),
                        "correct_cos": round(float(sim[i, i]), 4),
                        "injected_text_identical_to_correct": bool(same_text)})
    return out


def run_arm(name, later_txt, stored_txt, neg_txt, enc, mu, n_pos, n_distinct):
    L = cm2_floor.prepare(enc(later_txt), mu, DIM, PROJ)
    S = cm2_floor.prepare(enc(stored_txt), mu, DIM, PROJ)
    N = (cm2_floor.prepare(enc(neg_txt), mu, DIM, PROJ)
         if neg_txt else np.zeros((0, DIM), dtype=np.float64))
    sim = L @ S.T
    sweep = cm2_floor.op_sweep(sim, N, S, n_pos, n_distinct)
    row = next(r for r in sweep if abs(r["floor"] - FLOOR) < 1e-9)
    diag = sim[np.arange(n_pos), np.arange(n_pos)]      # the true-pair cosines
    flagged = content_aware_false(sim, stored_txt, n_pos, FLOOR)
    return {"arm": name, "operational_at_floor": row, "full_sweep": sweep,
            "flagged_positives": flagged,
            "content_aware_false_on_positives":
                sum(1 for f in flagged if not f["injected_text_identical_to_correct"]),
            "duplicate_text_artifacts":
                sum(1 for f in flagged if f["injected_text_identical_to_correct"]),
            "_diag": diag, "_sim": sim}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpu", action="store_true")
    a = ap.parse_args()

    from sentence_transformers import SentenceTransformer

    rs = json.loads(RECALL_JSON.read_text(encoding="utf-8"))
    dpos, apos, neg = rs["distinct_positives"], rs["adversarial_positives"], rs["negatives"]
    pos = dpos + apos
    later_clean = [p["later"] for p in pos]
    stored_clean = [p["stored"] for p in pos]
    neg_clean = [n["later"] for n in neg]
    n_pos, n_distinct = len(pos), len(dpos)

    mu = np.load(GOLDEN_NPZ)["mu"].astype(np.float64)

    print("loading %s ..." % cm2_floor.MODEL)
    m = SentenceTransformer(cm2_floor.MODEL, device=("cpu" if a.cpu else None))

    def enc(t):
        return np.asarray(m.encode(t, normalize_embeddings=True, batch_size=16,
                                   show_progress_bar=False), dtype=np.float64)

    later_shaped = [shape(s) for s in later_clean]
    stored_shaped = [shape(s) for s in stored_clean]
    neg_shaped = [shape(s) for s in neg_clean]
    later_q = [shape_q_only(s) for s in later_clean]

    n_changed_l = sum(1 for a_, b_ in zip(later_clean, later_shaped) if a_ != b_)
    n_changed_s = sum(1 for a_, b_ in zip(stored_clean, stored_shaped) if a_ != b_)
    print("shaping changed %d/%d later and %d/%d stored strings"
          % (n_changed_l, len(later_clean), n_changed_s, len(stored_clean)))

    arms = [
        ("A_clean_clean",       later_clean,  stored_clean),
        ("B_shaped_shaped",     later_shaped, stored_shaped),
        ("C1_storedClean_queryShaped", later_shaped, stored_clean),
        ("C2_storedShaped_queryClean", later_clean,  stored_shaped),
        ("D_qmark_only_query",  later_q,      stored_clean),
    ]
    negs_for = {"A_clean_clean": neg_clean, "B_shaped_shaped": neg_shaped,
                "C1_storedClean_queryShaped": neg_shaped,
                "C2_storedShaped_queryClean": neg_clean,
                "D_qmark_only_query": [shape_q_only(s) for s in neg_clean]}

    results = []
    for name, lt, st in arms:
        print("  arm %s ..." % name)
        results.append(run_arm(name, lt, st, negs_for[name], enc, mu, n_pos, n_distinct))

    base = results[0]
    print("")
    hdr = ("%-30s %7s %7s %9s %9s | %11s %10s"
           % ("arm", "useful", "missed", "TOTfalse", "falseNeg", "CONTENT-f", "dup-artif"))
    print(hdr); print("-" * len(hdr))
    for r in results:
        o = r["operational_at_floor"]
        print("%-30s %7d %7d %9d %9d | %11d %10d"
              % (r["arm"], o["useful_recall_queries"], o["missed_recall_queries"],
                 o["total_false_recalls"], o["false_recall_on_negatives"],
                 r["content_aware_false_on_positives"] + o["false_recall_on_negatives"],
                 r["duplicate_text_artifacts"]))
    print("  TOTfalse  = the PRE-REGISTERED metric (op_sweep, index identity)")
    print("  CONTENT-f = supplementary: only counts a win whose TEXT differs from the correct "
          "record's — what a human would experience as wrong context")

    print("")
    print("=== true-pair cosine deltas vs arm A (the magnitude, not just the consequence) ===")
    for r in results[1:]:
        d = r["_diag"] - base["_diag"]
        print("  %-30s mean %+0.4f  median %+0.4f  min %+0.4f  max %+0.4f  |d|>0.02: %d/%d"
              % (r["arm"], d.mean(), np.median(d), d.min(), d.max(),
                 int((np.abs(d) > 0.02).sum()), len(d)))

    ctrl = results[0]["operational_at_floor"]
    ok = (ctrl["useful_recall_queries"] == 19 and ctrl["total_false_recalls"] == 0)
    print("")
    print("ARM A CONTROL: useful=%d false=%d -> %s"
          % (ctrl["useful_recall_queries"], ctrl["total_false_recalls"],
             "REPRODUCES the recorded 19/0" if ok else "*** DIVERGES — STOP ***"))

    out = {
        "_comment": ("Does semantic recall survive real punctuation? MEASURE ONLY. Arm A is a "
                     "hard control against the recorded C/M2b numbers (useful=19, false=0 at "
                     "128/meanproj/0.55). Pipeline and metric are IMPORTED from cm2_floor.py, "
                     "never reimplemented. NOTHING here proposes a fix: normalising before "
                     "embedding would invalidate the floor it appears to rescue."),
        "model": cm2_floor.MODEL,
        "config": {"dim": DIM, "projection": PROJ, "floor": FLOOR, "comparison": ">="},
        "corpus": {"source": "cm0_recall_set.json", "distinct_positives": n_distinct,
                   "adversarial_positives": len(apos), "negatives": len(neg),
                   "n_positive_queries_scored": n_pos},
        "shaping_rules": {
            "evidence_source": ("the box's own control-IN episodic store @ LBA 21,140,000, read "
                                "via parse_episodic.py: 99 records, 49 DISTINCT queries"),
            "applied": [
                {"rule": "R1 trailing '?' when the string reads as a question",
                 "evidence": "12/49 distinct real queries (24.5%) carry a trailing '?'",
                 "kind": "additive"},
                {"rule": "R2 leading capital",
                 "evidence": "4/49 distinct real queries (8.2%) begin with a capital",
                 "kind": "additive"}],
            "excluded": [
                {"rule": "trailing '.'", "reason": "0/49 observed — applying it would be INVENTED"},
                {"rule": "contractions (does not -> doesn't)",
                 "reason": ("5/49 (10.2%) but it is a WORD-LEVEL REWRITE, not punctuation; a "
                            "different experiment, and the corpus already contains a natural "
                            "contraction (isn't) which would muddy the clean/shaped split")}],
            "strings_changed": {"later": n_changed_l, "stored": n_changed_s,
                                "of": len(later_clean)},
        },
        "arm_A_control_reproduces_recorded": bool(ok),
        "_metric_note": ("total_false_recalls is the PRE-REGISTERED metric and is reported "
                         "unchanged. content_aware_false_on_positives is SUPPLEMENTARY: this "
                         "corpus duplicates two stored texts BY CONSTRUCTION ('what is a mutex' "
                         "at 11 and 36, 'what is a page fault' at 0 and 39, because the "
                         "adversarial positives are harder paraphrases of distinct-set "
                         "questions), so argmax landing on the other copy of byte-identical "
                         "text scores as a false recall while the injected context is exactly "
                         "what the correct record holds. Both numbers are reported; neither "
                         "replaces the other, and the disposition is the strategist's."),
        "results": [],
    }
    for r in results:
        d = (r["_diag"] - base["_diag"])
        out["results"].append({
            "arm": r["arm"],
            "operational_at_floor_0.55": r["operational_at_floor"],
            "full_floor_sweep": r["full_sweep"],
            "content_aware_false_on_positives": r["content_aware_false_on_positives"],
            "duplicate_text_artifacts": r["duplicate_text_artifacts"],
            "flagged_positives": r["flagged_positives"],
            "true_pair_cosine_delta_vs_A": {
                "mean": round(float(d.mean()), 5), "median": round(float(np.median(d)), 5),
                "min": round(float(d.min()), 5), "max": round(float(d.max()), 5),
                "n_abs_gt_0.02": int((np.abs(d) > 0.02).sum()), "n": int(len(d))},
        })
    OUT_JSON.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    print("wrote %s" % OUT_JSON)
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
