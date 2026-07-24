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
GENERIC_TXT = HERE / "cm0_generic_corpus.txt"
GOLDEN_NPZ = HERE / "golden_vectors.npz"
GOLDEN_META = HERE / "golden_meta.json"

# A fixed probe set for the winner's golden vectors (the C/M1 box-parity reference). Kept small +
# varied: stored turns, later queries, a near-synonym, negatives, and control-IN-style status queries.
PROBE_TEXTS = [
    "what is a page fault", "how does dns work", "what is a mutex",
    "how do you find a value in a sorted array in logarithmic time",
    "what lightweight text format stores structured data as key-value pairs",
    "how do you protect a critical section so only one thread runs it",
    "what's the capital of France", "how do you bake sourdough bread",
    "how long have you been up", "what model are you running",
    "why doesn't adding more cpu cores speed up a single-threaded program",
    "what is a hash table", "explain how tcp handshake works",
    "what is public-key encryption", "what does DMA do",
]

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
MXBAI_Q = "Represent this sentence for searching relevant passages: "

# C/M0.5 candidate KINDS (the bases that fine-tune to the BEST recall, not the cheapest):
#   qwen3 -- Qwen3-Embedding-0.6B: a strong DECODER embedder, contrastively post-trained (far less
#            anisotropic than raw Gemma), instruction-aware, PARTIAL box reuse (the Qwen3 path exists
#            from the model bench-off). Run WITH its instruction prompt.
#   gte / mxbai -- strong STS ENCODERS (gte-large-en-v1.5 / mxbai-embed-large-v1): encoder uniformity
#            training gives the BEST separation; a NET-NEW BERT engine on the box, acceptable ONLY if
#            separation clearly wins over reuse.
#   gemma -- EmbeddingGemma-300M RE-EVAL with its official symmetric/STS prompt on BOTH sides (the
#            near-zero-new-engine base; the C/M0 50% is re-checked here on the enlarged set).
#   bge   -- REFERENCE ONLY (the 56% / 81% baselines to beat), never a ship candidate.
def model_kind(model_id):
    m = model_id.lower()
    if "embeddinggemma" in m:    return "gemma"
    if "qwen3-embedding" in m:   return "qwen3"
    if "gte" in m:               return "gte"
    if "mxbai" in m:             return "mxbai"
    return "bge"


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
    """Return [(name, side_a_encoder, side_b_encoder)]. The recall use case is SYMMETRIC
    query-to-query, so symmetric strategies have side_a == side_b (same prompt both sides);
    asym entries are kept only for reference/contrast."""
    enc, prompts = _raw_encoder(model)
    fp = lambda t: enc(t)                                   # plain (no prompt/prefix)
    fbge = lambda t: enc(t, prefix=BGE_Q)
    fmx = lambda t: enc(t, prefix=MXBAI_Q)
    fq = lambda t: enc(t, prompt_name="query")
    fsts = lambda t: enc(t, prompt_name="STS")
    if kind == "gemma":
        strat = []
        if "query" in prompts: strat.append(("sym:query", fq, fq))
        if "STS" in prompts:   strat.append(("sym:STS", fsts, fsts))
        strat.append(("sym:none", fp, fp))
        if "query" in prompts and "document" in prompts:
            strat.append(("asym:query/doc", fq, lambda t: enc(t, prompt_name="document")))
        return strat
    if kind == "qwen3":
        strat = []
        if "query" in prompts: strat.append(("sym:query", fq, fq))
        strat.append(("sym:none", fp, fp))
        return strat
    if kind == "gte":
        return [("sym:none", fp, fp)]                       # mean-pooled STS encoder, no prompt
    if kind == "mxbai":
        return [("sym:none", fp, fp), ("sym:instr", fmx, fmx)]
    # bge (reference)
    return [("sym:none", fp, fp), ("sym:instr", fbge, fbge), ("asym:instr/plain", fbge, fp)]


def cluster_encoder(model, kind):
    enc, prompts = _raw_encoder(model)
    if kind == "gemma":
        name = "Clustering" if "Clustering" in prompts else ("query" if "query" in prompts else None)
        return lambda t: enc(t, prompt_name=name)
    if kind == "qwen3" and "query" in prompts:
        return lambda t: enc(t, prompt_name="query")
    return lambda t: enc(t)


# --------------------------------------------------- mean-projection (C/M0.5 §3) ---
# Single-mean-DIRECTION removal (NOT full whitening / all-but-top-K, which over-corrects modern
# contrastive models and is unstable at our corpus size). mu is estimated OFF-BOX on a MODEST
# generic corpus + our own data, then FROZEN. e' = normalize(e - (e.mu) mu). A stackable few-point
# bonus for whichever base we fine-tune (esp. a decoder), reported raw-vs-+meanproj per candidate.
def load_mu_texts():
    generic = [ln.strip() for ln in GENERIC_TXT.read_text(encoding="utf-8").splitlines()
               if ln.strip() and not ln.lstrip().startswith("#")]
    pos, adv, neg = load_recall(RECALL_JSON)
    ours = ([p["stored"] for p in pos] + [p["later"] for p in pos]
            + [p["stored"] for p in adv] + [p["later"] for p in adv])
    d = parse_routing_suite(ROUTING_H)
    ours += [r["text"] for r in d["DEV"]] + [r["text"] for r in d["HELDOUT"]]
    return generic + ours


def fit_mu(encoder, texts):
    V = encoder(texts)                      # already L2-normed
    mu = V.mean(axis=0)
    return (mu / (np.linalg.norm(mu) + 1e-9)).astype(np.float32)


def mean_project(V, mu):
    W = V - (V @ mu)[:, None] * mu[None, :]
    W = W / (np.linalg.norm(W, axis=1, keepdims=True) + 1e-9)
    return W.astype(np.float32)


def mp_wrap(base_encoder, mu):
    return lambda t: mean_project(base_encoder(t), mu)


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
def _load_st(model_id, device):
    """Load a SentenceTransformer, retrying with trust_remote_code=True for models whose
    architecture ships custom code (e.g. gte-large-en-v1.5's NewModel)."""
    from sentence_transformers import SentenceTransformer
    try:
        return SentenceTransformer(model_id, device=device)
    except Exception as e:
        if "trust_remote_code" in str(e).lower() or "custom code" in str(e).lower():
            print("  (retrying with trust_remote_code=True for %s)" % model_id)
            return SentenceTransformer(model_id, device=device, trust_remote_code=True)
        raise


def run_model(model_id, device, data, positives, adversarial, negatives,
              mu_texts=None, ref=False, verbose=False):
    kind = model_kind(model_id)
    print("\n" + "=" * 78)
    print("MODEL: %s   (%s)%s" % (model_id, kind, "   [REFERENCE ONLY]" if ref else ""))
    print("=" * 78)
    try:
        model = _load_st(model_id, device)
    except Exception as e:
        msg = str(e)
        gated = any(s in msg.lower() for s in
                    ("gated", "401", "403", "awaiting", "access to model",
                     "restricted", "you are trying to access"))
        print("  !! FAILED to load: %s" % msg.splitlines()[0][:200])
        if gated:
            print("  !! HF-LICENSE-GATED: accept the license at https://huggingface.co/%s" % model_id)
            print("     then `hf auth login` with a read token and re-run. Other models still ran.")
        else:
            print("  !! (not a gating error -- check the model id / trust_remote_code / torch version)")
        return None
    try:
        dim = model.get_sentence_embedding_dimension()
        prompts = list((getattr(model, "prompts", None) or {}).keys())
        print("  loaded: dim=%d  device=%s  prompts=%s" % (dim, device, prompts or "(none)"))

        # --- recall on the DISTINCT set (the GATE): sweep symmetric prompt strategies (RAW) ---
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
            print("  -- DISTINCT-set misses under %s (raw) --" % best_name)
            recall_metrics(fa, fb, positives, negatives, verbose=True)
        best["dim"] = dim
        best["strategy"] = best_name

        # --- MEAN-PROJECTION ablation on the chosen strategy (fit mu OFF the recall set, frozen) ---
        mp = None
        mu = None
        if mu_texts is not None:
            mu = fit_mu(fa, mu_texts)                 # fit on the chosen encoder over the mu-corpus
            fam = mp_wrap(fa, mu)
            mp = recall_metrics(fam, fam, positives, negatives, verbose=False)
            mp["strategy"] = best_name + "+meanproj"
            print("  -- +mean-projection (single frozen mean dir, N_mu=%d): top1=%.1f%% top3=%.1f%% "
                  "margin=%+.3f %s" % (len(mu_texts), 100 * mp["top1_acc"], 100 * mp["top3_acc"],
                  mp["margin_mean"], "CLEAN" if mp["clean_separation"] else "over"))

        # --- ADVERSARIAL near-synonym subset (disambiguation STRESS, NOT the gate; raw strategy) ---
        adv = recall_metrics(fa, fb, adversarial, negatives, verbose=False)
        print("  -- ADVERSARIAL near-synonym subset: top1=%.1f%% top3=%.1f%% margin=%+.3f" %
              (100 * adv["top1_acc"], 100 * adv["top3_acc"], adv["margin_mean"]))

        intent = intent_metrics(cluster_encoder(model, kind), data["DEV"], data["HELDOUT"])
        return {"model": model_id, "kind": kind, "dim": dim, "ref": ref,
                "recall": best, "recall_meanproj": mp, "recall_adversarial": adv,
                "recall_sweep": {n: r for n, r in sweep}, "intent": intent,
                "_mu": mu, "_best_strategy": best_name}
    finally:
        del model
        try:
            import torch, gc
            gc.collect()
            if torch.cuda.is_available():
                torch.cuda.empty_cache()
        except Exception:
            pass


# reference baselines (to beat, NOT ship candidates) + the C/M0.5 fine-tune-base candidates.
REF_MODELS = ["BAAI/bge-small-en-v1.5", "BAAI/bge-large-en-v1.5"]
CANDIDATE_MODELS = ["Qwen/Qwen3-Embedding-0.6B", "Alibaba-NLP/gte-large-en-v1.5",
                    "mixedbread-ai/mxbai-embed-large-v1", "google/embeddinggemma-300m"]
# box-reuse cost per kind (for the pick's tie-break; lower = cheaper on the box):
BOX_COST = {"gemma": "near-zero new engine (reuses the Gemma path)",
            "qwen3": "partial reuse (Qwen3 path exists from the bench-off)",
            "gte": "NET-NEW BERT engine", "mxbai": "NET-NEW BERT engine",
            "bge": "NET-NEW BERT engine"}


def best_config(r):
    """The stronger of a model's raw vs +meanproj recall (by top1, then margin)."""
    raw = dict(r["recall"]); raw["_mp"] = False
    if r.get("recall_meanproj"):
        mp = dict(r["recall_meanproj"]); mp["_mp"] = True
        return max([raw, mp], key=lambda c: (c["top1_acc"], c["margin_mean"]))
    return raw


def save_golden(model_id, device, strategy, use_meanproj, mu):
    """Embed the fixed PROBE_TEXTS with the winner's chosen config and save as the C/M1 box-parity
    golden reference (text -> float[dim]) + a meta header so C/M1 replicates it EXACTLY."""
    kind = model_kind(model_id)
    model = _load_st(model_id, device)
    try:
        fa = next(a for n, a, b in recall_strategies(model, kind) if n == strategy)
        enc = mp_wrap(fa, mu) if use_meanproj else fa
        V = enc(PROBE_TEXTS)
        np.savez(GOLDEN_NPZ, texts=np.array(PROBE_TEXTS, dtype=object), vectors=V,
                 mu=(mu if use_meanproj else np.zeros(V.shape[1], np.float32)))
        meta = {"model": model_id, "kind": kind, "strategy": strategy,
                "mean_projection": bool(use_meanproj), "dim": int(V.shape[1]),
                "n_probe": len(PROBE_TEXTS), "l2_normalized": True,
                "note": ("C/M1 box parity: the C engine must reproduce these vectors to 1e-3 using "
                         "the SAME pooling + prompt (%s) + L2-norm%s. mu (if mean_projection) is the "
                         "frozen single mean direction; apply e'=normalize(e-(e.mu)mu) AFTER pooling."
                         % (strategy, " + mean-projection" if use_meanproj else ""))}
        GOLDEN_META.write_text(json.dumps(meta, indent=2), encoding="utf-8")
        print("  saved golden: %s (%d texts x %d dim, meanproj=%s) + %s"
              % (GOLDEN_NPZ.name, len(PROBE_TEXTS), V.shape[1], use_meanproj, GOLDEN_META.name))
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
    ap.add_argument("--models", nargs="*", default=None,
                    help="override the model list (default = the C/M0.5 ref + candidate set)")
    ap.add_argument("--no-meanproj", action="store_true", help="skip the mean-projection ablation")
    ap.add_argument("--no-golden", action="store_true", help="do not save golden vectors for the winner")
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
    print("intent corpus: DEV=%d HELDOUT=%d" % (len(data["DEV"]), len(data["HELDOUT"])))
    print("recall corpus: %d DISTINCT pairs (the gate) + %d adversarial + %d hard negatives"
          % (len(positives), len(adversarial), len(negatives)))
    mu_texts = None if args.no_meanproj else load_mu_texts()
    if mu_texts is not None:
        print("mean-projection mu corpus: %d texts (generic + our data), frozen per model" % len(mu_texts))

    if args.models is not None:
        run_list = [(m, model_kind(m) == "bge") for m in args.models]
    else:
        run_list = [(m, True) for m in REF_MODELS] + [(m, False) for m in CANDIDATE_MODELS]

    results = []
    for mid, is_ref in run_list:
        r = run_model(mid, device, data, positives, adversarial, negatives,
                      mu_texts=mu_texts, ref=is_ref, verbose=args.verbose)
        if r:
            results.append(r)

    # ------- comparison table -------
    print("\n" + "#" * 78)
    print("# C/M0.5 BASE-SELECTION  (GATE = distinct recall; raw + mean-projection; * = reference only)")
    print("#" * 78)
    print("%-24s %5s %6s %6s %8s %6s %8s %8s" %
          ("model", "dim", "raw@1", "mp@1", "raw@3/mp@3", "adv@1", "intent", "sep(raw/mp)"))
    for r in results:
        rec, it, adv = r["recall"], r["intent"], r["recall_adversarial"]
        mp = r.get("recall_meanproj")
        star = "*" if r.get("ref") else " "
        print("%s%-23s %5d %5.1f%% %5s %6.1f%%/%-5s %5.1f%% %6.1f%% %4s/%-4s [%s]" % (
            star, r["model"].split("/")[-1][:23], r["dim"],
            100 * rec["top1_acc"],
            ("%.1f%%" % (100 * mp["top1_acc"])) if mp else "-",
            100 * rec["top3_acc"], ("%.1f%%" % (100 * mp["top3_acc"])) if mp else "-",
            100 * adv["top1_acc"], 100 * it["acc"],
            "CLN" if rec["clean_separation"] else "over",
            (("CLN" if mp["clean_separation"] else "over") if mp else "-"),
            rec.get("strategy", "?")))
    print("  * = reference baseline (to beat, not a ship candidate). raw@1/mp@1 = distinct recall@1")
    print("  raw vs +mean-projection. sep = clean true-vs-unrelated separation. adv = near-synonym stress.")
    print("\nseparation detail (chosen raw strategy):")
    for r in results:
        rec = r["recall"]
        print("  %-24s intended_min=%.3f  neg_best_max=%.3f  -> %s  | box: %s"
              % (r["model"].split("/")[-1][:24], rec["intended_min"], rec["neg_best_max"],
                 "CLEAN" if rec["clean_separation"] else "OVERLAP", BOX_COST.get(r["kind"], "?")))

    # ------- pick the winning BASE (best CANDIDATE by top1 then margin; box cost noted) -------
    cands = [r for r in results if not r.get("ref")]
    winner = None
    if cands:
        winner = max(cands, key=lambda r: (best_config(r)["top1_acc"], best_config(r)["margin_mean"]))
        bc = best_config(winner)
        print("\n#### WINNING BASE (highest candidate recall@1 + margin) ####")
        print("  %s  [%s]  recall@1=%.1f%% top3=%.1f%% margin=%+.3f  sep=%s  box=%s"
              % (winner["model"], bc["strategy"], 100 * bc["top1_acc"], 100 * bc["top3_acc"],
                 bc["margin_mean"], "CLEAN" if bc["clean_separation"] else "OVERLAP",
                 BOX_COST.get(winner["kind"], "?")))
        print("  (this off-the-shelf score is the C/M1a BASELINE the 2070 fine-tune must BEAT.)")
        if not args.no_golden:
            save_golden(winner["model"], device, winner["_best_strategy"],
                        bc.get("_mp", False), winner.get("_mu"))

    # serialize (drop the numpy mu from the JSON)
    ser = []
    for r in results:
        rr = {k: v for k, v in r.items() if not k.startswith("_")}
        ser.append(rr)
    Path(args.out).write_text(json.dumps(ser, indent=2), encoding="utf-8")
    print("\nwrote %s" % args.out)
    if not results:
        print("\nNO MODEL RAN. Handle HF gating (license accept + hf login) and re-run.")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
