#!/usr/bin/env python3
"""
cm0_bench.py -- Phase C / C/M0: the OFF-BOX embedder scoring experiment.

Scores two off-the-shelf embedders on OUR OWN labeled data (NOT MTEB -- our
domain is narrow/technical) for the SEMANTIC RECALL use case (the first Phase-C
lane), plus a routing-relevant intent-clustering read. Its OUTPUT gates the whole
arc: it picks the embedder AND decides whether any training is needed.

Two candidates, head to head:
  - google/embeddinggemma-300m -- the STRONG-REUSE candidate (Gemma-arch decoder;
    the box already runs the Gemma path, so integrating it is near-zero new engine
    code). Instruction-tuned -> uses its OFFICIAL task prompts via sentence-
    transformers (encode_query / encode_document / a Clustering prompt). MAY be
    HF-license-gated -> the harness reports that clearly and still runs the baseline.
  - BAAI/bge-small-en-v1.5 -- the CHEAP BASELINE it must beat (33M BERT encoder,
    CLS pooling). It would cost a NET-NEW BERT engine on the box, so it is only
    worth that cost if DRAMATICALLY better on our data.

We use each model's OFFICIAL sentence-transformers loading (pooling/prefix/L2-norm
handled by the library) -- NO hand-rolled pooling in C/M0. The C-engine replication
of that exact pooling is C/M1's parity job; the golden vectors (saved separately by
cm0_golden.py for the winner) are how it's checked.

Run:  py -3 phase3/scripts/embed/cm0_bench.py            # both models, GPU if free
      py -3 phase3/scripts/embed/cm0_bench.py --cpu      # force CPU (tiny workload)
      py -3 phase3/scripts/embed/cm0_bench.py --models BAAI/bge-small-en-v1.5
"""
import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent.parent          # phase3/scripts/embed -> repo root
ROUTING_H = REPO / "phase3" / "src" / "ai" / "routing_suite.h"
RECALL_JSON = HERE / "cm0_recall_set.json"

# The HELDOUT INFER-FP family (the conceptual-question-with-metric-noun cases the
# 6-6 keyword router got wrong before the anchor fix). An embedder SHOULD cluster
# these with INFER, not with the DECLINE metric-status class -- tracked specially.
INFER_FP_HELDOUT = {
    "why doesn't adding more cpu cores speed up a single-threaded program?",
    "how does a cpu pipeline work?",
    "what is disk fragmentation?",
    "how does virtual memory work?",
    "why does high temperature slow a processor down?",
    "how much time does quicksort take?",
}

# The recall decision threshold used only to REPORT clean separation (not a tuned
# knob -- C/M2 picks a real threshold on-box). A positive should sit above it and a
# negative's best match below it.
REPORT_THRESHOLD = 0.60


# ---------------------------------------------------------------- data loading ---
def parse_routing_suite(path):
    """Extract (text, label, split) from routing_suite.h. label = the SF_* field
    for SYSFACTS, else the ROUTE class name (INFER / DECLINE). Single source of
    truth -- no second copy to drift."""
    txt = path.read_text(encoding="utf-8", errors="replace")
    # split into the DEV array body and the HELDOUT array body
    def body(name):
        m = re.search(name + r"\[\]\s*=\s*\{(.*?)\n\};", txt, re.S)
        if not m:
            raise SystemExit("could not find %s[] in %s" % (name, path))
        return m.group(1)
    row = re.compile(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*(ROUTE_\w+)\s*,\s*(SF_\w+)\s*\}')
    out = {}
    for split, name in (("DEV", "ROUTING_DEV"), ("HELDOUT", "ROUTING_HELDOUT")):
        rows = []
        for m in row.finditer(body(name)):
            text = m.group(1).encode().decode("unicode_escape")
            route, field = m.group(2), m.group(3)
            label = field.replace("SF_", "") if route == "ROUTE_SYSFACTS" \
                else route.replace("ROUTE_", "")   # INFER / DECLINE
            rows.append({"text": text, "label": label, "route": route})
        out[split] = rows
    return out


def load_recall(path):
    d = json.loads(path.read_text(encoding="utf-8"))
    return (d["distinct_positives"], d["adversarial_positives"],
            [n["later"] for n in d["negatives"]])


# ------------------------------------------------------------------- encoders ---
# The C/M2 recall task is the SEMANTIC extension of 6-5's exact-key-over-the-QUERY
# match: a NEW control-IN query matched against PRIOR control-IN queries (then the
# prior ANSWER is injected). Both sides are QUERIES -> it is a SYMMETRIC query-to-
# query task, NOT asymmetric query->document retrieval. Using the wrong mode
# silently tanks an instruction-tuned embedder (proven in the C/M0 diagnostic:
# EmbeddingGemma flips mutex->copy-on-write under the document prompt). So the recall
# metric sweeps the sensible SYMMETRIC prompt strategies per model + the asymmetric
# one for reference, and the headline uses each model's principled symmetric choice.

BGE_Q = "Represent this sentence for searching relevant passages: "


def _raw_encoder(model):
    prompts = getattr(model, "prompts", None) or {}

    def enc(texts, prompt_name=None, prefix=None):
        texts = list(texts)
        if prefix:
            texts = [prefix + t for t in texts]
        kw = dict(normalize_embeddings=True, convert_to_numpy=True,
                  show_progress_bar=False, batch_size=32)
        if prompt_name and prompt_name in prompts:
            kw["prompt_name"] = prompt_name
        return np.asarray(model.encode(texts, **kw), dtype=np.float32)
    return enc, prompts


def recall_strategies(model, kind):
    """Return a list of (name, side_a_encoder, side_b_encoder). For symmetric
    strategies side_a == side_b (same prompt both sides). The last entry is the
    asymmetric setup, kept only for reference/contrast."""
    enc, prompts = _raw_encoder(model)
    if kind == "gemma":
        strat = []
        if "query" in prompts:
            f = lambda t: enc(t, prompt_name="query"); strat.append(("sym:query", f, f))
        if "STS" in prompts:
            f = lambda t: enc(t, prompt_name="STS"); strat.append(("sym:STS", f, f))
        fp = lambda t: enc(t); strat.append(("sym:none", fp, fp))
        if "query" in prompts and "document" in prompts:
            strat.append(("asym:query/doc",
                          lambda t: enc(t, prompt_name="query"),
                          lambda t: enc(t, prompt_name="document")))
        return strat
    # bge: symmetric s2s = NO instruction both sides (the model card: the instruction
    # is only for asymmetric s2p retrieval). Contrast with instruction-both + asym.
    fp = lambda t: enc(t)
    fq = lambda t: enc(t, prefix=BGE_Q)
    return [("sym:none", fp, fp),
            ("sym:instr", fq, fq),
            ("asym:instr/plain", fq, fp)]


def cluster_encoder(model, kind):
    enc, prompts = _raw_encoder(model)
    if kind == "gemma":
        name = "Clustering" if "Clustering" in prompts else ("query" if "query" in prompts else None)
        return lambda t: enc(t, prompt_name=name)
    return lambda t: enc(t)   # bge symmetric, plain


# -------------------------------------------------------------------- metrics ---
def recall_metrics(embed_query, embed_doc, positives, negatives, verbose=False):
    stored = [p["stored"] for p in positives]
    later = [p["later"] for p in positives]
    docs = embed_doc(stored)                       # the corpus (stored turns)
    q = embed_query(later)                          # the later queries
    sims = q @ docs.T                               # cosine (both L2-normed)

    n = len(positives)
    top1 = top3 = 0
    margins, intended_cos = [], []
    for i in range(n):
        order = np.argsort(-sims[i])
        hit1 = order[0] == i
        if hit1:
            top1 += 1
        if i in order[:3]:
            top3 += 1
        best_distractor = max(sims[i][j] for j in range(n) if j != i)
        margins.append(float(sims[i][i] - best_distractor))
        intended_cos.append(float(sims[i][i]))
        if verbose and not hit1:
            got = order[0]
            print("    MISS  q=%-58s" % ('"' + later[i][:56] + '"'))
            print("          intended=%-40s (%.3f)" % ('"' + stored[i][:38] + '"', sims[i][i]))
            print("          got     =%-40s (%.3f)" % ('"' + stored[got][:38] + '"', sims[i][got]))

    # false recall: for each unrelated negative, its BEST cosine to ANY stored turn.
    negq = embed_query(negatives)
    negsims = negq @ docs.T
    neg_best = [float(negsims[k].max()) for k in range(len(negatives))]

    return {
        "n_pos": n,
        "top1": top1, "top1_acc": top1 / n,
        "top3": top3, "top3_acc": top3 / n,
        "margin_mean": float(np.mean(margins)),
        "intended_min": float(np.min(intended_cos)),
        "intended_mean": float(np.mean(intended_cos)),
        "neg_best_max": float(np.max(neg_best)),
        "neg_best_mean": float(np.mean(neg_best)),
        "clean_separation": bool(np.min(intended_cos) > np.max(neg_best)),
    }


def intent_metrics(embed_cluster, dev, heldout):
    dev_txt = [r["text"] for r in dev]
    hel_txt = [r["text"] for r in heldout]
    dev_v = embed_cluster(dev_txt)
    hel_v = embed_cluster(hel_txt)

    labels = sorted(set(r["label"] for r in dev))
    cents = {}
    for lab in labels:
        idx = [i for i, r in enumerate(dev) if r["label"] == lab]
        c = dev_v[idx].mean(axis=0)
        c = c / (np.linalg.norm(c) + 1e-9)
        cents[lab] = c
    C = np.stack([cents[l] for l in labels])          # [L, d]

    pred_idx = np.argmax(hel_v @ C.T, axis=1)
    preds = [labels[j] for j in pred_idx]

    correct = sum(1 for r, p in zip(heldout, preds) if r["label"] == p)
    # the INFER-FP family specifically: do they land in INFER (correct)?
    fp_total = fp_infer = 0
    for r, p in zip(heldout, preds):
        if r["text"].strip().lower() in INFER_FP_HELDOUT:
            fp_total += 1
            if p == "INFER":
                fp_infer += 1
    # confusion for the report
    conf = {}
    for r, p in zip(heldout, preds):
        conf.setdefault(r["label"], {}).setdefault(p, 0)
        conf[r["label"]][p] += 1
    return {
        "n_heldout": len(heldout),
        "acc": correct / len(heldout),
        "correct": correct,
        "labels": labels,
        "fp_total": fp_total, "fp_infer": fp_infer,
        "fp_infer_pct": (fp_infer / fp_total) if fp_total else float("nan"),
        "confusion": conf,
    }


# ----------------------------------------------------------------------- main ---
def run_model(model_id, device, data, positives, adversarial, negatives, verbose=False):
    from sentence_transformers import SentenceTransformer
    kind = "gemma" if "embeddinggemma" in model_id.lower() else "bge"
    print("\n" + "=" * 78)
    print("MODEL: %s   (%s)" % (model_id, kind))
    print("=" * 78)
    try:
        model = SentenceTransformer(model_id, device=device)
    except Exception as e:
        msg = str(e)
        gated = any(s in msg.lower() for s in
                    ("gated", "401", "403", "awaiting", "access to model",
                     "restricted", "you are trying to access"))
        print("  !! FAILED to load: %s" % msg.splitlines()[0][:200])
        if gated or kind == "gemma":
            print("  !! This model is likely HF-LICENSE-GATED. To enable it:")
            print("     1) accept the license at https://huggingface.co/%s" % model_id)
            print("     2) `py -3 -m huggingface_hub.commands.huggingface_cli login`  (paste a read token)")
            print("     then re-run this harness. The baseline model still ran.")
        return None
    try:
        dim = model.get_sentence_embedding_dimension()
        prompts = list((getattr(model, "prompts", None) or {}).keys())
        print("  loaded: dim=%d  device=%s  prompts=%s" % (dim, device, prompts or "(none)"))

        # --- recall on the DISTINCT set (the GATE): sweep symmetric prompt strategies ---
        print("  -- recall on the DISTINCT set (the GATE) across prompt strategies (sym = query-to-query) --")
        print("     %-18s %8s %8s %7s %6s" % ("strategy", "top1", "top3", "margin", "sep?"))
        sweep = []
        for name, fa, fb in recall_strategies(model, kind):
            r = recall_metrics(fa, fb, positives, negatives, verbose=False)
            sweep.append((name, r))
            print("     %-18s %7.1f%% %7.1f%% %+6.3f %6s"
                  % (name, 100 * r["top1_acc"], 100 * r["top3_acc"], r["margin_mean"],
                     "CLEAN" if r["clean_separation"] else "over"))
        sym = [(n, r) for n, r in sweep if n.startswith("sym")]
        best_name, best = max(sym, key=lambda nr: (nr[1]["top1_acc"], nr[1]["top3_acc"], nr[1]["margin_mean"]))
        print("     -> chosen (symmetric): %s" % best_name)
        fa, fb = next((a, b) for n, a, b in recall_strategies(model, kind) if n == best_name)
        if verbose:
            print("  -- DISTINCT-set misses under %s --" % best_name)
            recall_metrics(fa, fb, positives, negatives, verbose=True)
        best["dim"] = dim
        best["strategy"] = best_name

        # --- the ADVERSARIAL near-synonym subset (a disambiguation STRESS test, NOT the gate) ---
        adv = recall_metrics(fa, fb, adversarial, negatives, verbose=False)
        print("  -- ADVERSARIAL near-synonym subset (disambiguation stress, same strategy): "
              "top1=%.1f%% top3=%.1f%% margin=%+.3f" %
              (100 * adv["top1_acc"], 100 * adv["top3_acc"], adv["margin_mean"]))

        intent = intent_metrics(cluster_encoder(model, kind), data["DEV"], data["HELDOUT"])
        return {"model": model_id, "kind": kind, "dim": dim,
                "recall": best, "recall_adversarial": adv,
                "recall_sweep": {n: r for n, r in sweep}, "intent": intent}
    finally:
        del model
        try:
            import torch, gc
            gc.collect()
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cpu", action="store_true", help="force CPU (tiny workload)")
    ap.add_argument("--models", nargs="*", default=[
        "BAAI/bge-small-en-v1.5", "google/embeddinggemma-300m"])
    ap.add_argument("--out", default=str(HERE / "cm0_results.json"))
    ap.add_argument("--verbose", action="store_true", help="dump recall misses")
    args = ap.parse_args()

    device = "cpu"
    if not args.cpu:
        try:
            import torch
            device = "cuda" if torch.cuda.is_available() else "cpu"
        except Exception:
            device = "cpu"
    print("device: %s" % device)

    data = parse_routing_suite(ROUTING_H)
    positives, adversarial, negatives = load_recall(RECALL_JSON)
    print("intent corpus: DEV=%d HELDOUT=%d (labels=%s)"
          % (len(data["DEV"]), len(data["HELDOUT"]),
             sorted(set(r["label"] for r in data["DEV"]))))
    print("recall corpus: %d DISTINCT pairs (the gate) + %d adversarial near-synonym pairs, %d hard negatives"
          % (len(positives), len(adversarial), len(negatives)))

    results = []
    for mid in args.models:
        r = run_model(mid, device, data, positives, adversarial, negatives, verbose=args.verbose)
        if r:
            results.append(r)

    # ------- comparison table -------
    print("\n" + "#" * 78)
    print("# C/M0 COMPARISON  (metric 1 = the C/M2 GATE; metric 2 = routing-relevant)")
    print("#" * 78)
    hdr = ("model", "dim", "GATE@1", "GATE@3", "adv@1", "sep?", "intent-HO", "FP->INFER")
    print("%-26s %4s %7s %7s %6s %5s %8s %10s" % hdr)
    for r in results:
        rec, it, adv = r["recall"], r["intent"], r["recall_adversarial"]
        print("%-26s %4d %6.1f%% %6.1f%% %5.1f%% %5s %7.1f%% %8d/%d  [%s]" % (
            r["model"].split("/")[-1], r["dim"],
            100 * rec["top1_acc"], 100 * rec["top3_acc"], 100 * adv["top1_acc"],
            "YES" if rec["clean_separation"] else "no",
            100 * it["acc"], it["fp_infer"], it["fp_total"], rec.get("strategy", "?")))
    print("  GATE = the topically-DISTINCT recall set (fair ground truth); adv = the near-synonym")
    print("  disambiguation stress; sep? = clean separation of true pairs vs unrelated queries.")
    print("\nseparation detail (recall gate): a model PASSES clean separation when the")
    print("weakest intended match still beats every unrelated query's best match:")
    for r in results:
        rec = r["recall"]
        print("  %-28s intended_min=%.3f  neg_best_max=%.3f  -> %s"
              % (r["model"].split("/")[-1], rec["intended_min"], rec["neg_best_max"],
                 "CLEAN" if rec["clean_separation"] else "OVERLAP"))

    Path(args.out).write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("\nwrote %s" % args.out)
    if not results:
        print("\nNO MODEL RAN. If EmbeddingGemma is gated, accept its license + hf login, then re-run.")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
