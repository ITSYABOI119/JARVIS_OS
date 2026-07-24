#!/usr/bin/env python3
"""
cm1a_golden.py -- Phase C / C/M1a: build the TWO-GOLDEN parity foundation for the deployed C engine.

The C/M1a gate before any box work is PARITY: the box C engine must reproduce reference embeddings for
Qwen3-Embedding-0.6B. Comparing against ONE golden conflates a PORT bug with quant error, so we build TWO:

  1. GGUF golden (engine-vs-engine, the TIGHT gate ~1e-4): the OFFICIAL Qwen GGUF at the box quant
     (Q8_0), embedded via llama.cpp (llama-cpp-python) in last-token embedding mode. The C engine runs
     the SAME quant, so a C-vs-GGUF gap is a PORT bug, isolated from quant error. We also dump llama.cpp's
     exact TOKEN-ID sequence per probe -- the token-parity reference (a single differing id => the
     tokenizer is wrong; fix before comparing vectors).
  2. F32 golden (loose end-to-end sanity, at a MEASURED tolerance): the sentence-transformers F32 vectors
     (sym:none, last-token pool, L2 -- NO mean-projection; that is a downstream C/M2 transform). The
     honest F32 tolerance is the MEASURED GGUF-vs-ST cosine gap (the quant floor), NOT a guessed 1e-3.

Config nailed from the GGUF metadata: pooling=LAST(3); add_bos=false; add_eos=true, EOS=151643
(<|endoftext|>) -- appended and pooled; pre-tokenizer = qwen2 (GPT-2-style). sym:none = NO instruction
prefix (matches golden_meta / the C/M0.5 winner).

Outputs (committed, small; models NOT committed):
  gguf_golden.npz     -- texts, vectors_raw (Q8_0 last-pool + L2), token_ids (incl. EOS)  [tight target]
  golden_vectors.npz  -- texts, vectors_raw (F32 sym:none pool + L2), mu (frozen mean dir) [F32 target + C/M2 mu]
  golden_meta.json    -- model/quant/prefix/pooling/EOS + per-probe token-ids + the MEASURED quant floor

Run:  "$USERPROFILE/miniconda3/python.exe" phase3/scripts/embed/cm1a_golden.py
      (torch>=2.6 for the ST Qwen3 load; llama-cpp-python for the GGUF; EmbeddingGemma-style HF login N/A
       -- Qwen GGUF + ST model are ungated.)
"""
import json
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
GOLDEN_NPZ = HERE / "golden_vectors.npz"
GGUF_NPZ = HERE / "gguf_golden.npz"
GOLDEN_META = HERE / "golden_meta.json"

ST_MODEL = "Qwen/Qwen3-Embedding-0.6B"
GGUF_REPO = "Qwen/Qwen3-Embedding-0.6B-GGUF"
GGUF_FILE = "Qwen3-Embedding-0.6B-Q8_0.gguf"       # Q8_0 = near-lossless, the box quant target
EOS_ID = 151643                                     # <|endoftext|> -- appended + pooled (add_eos_token=true)

# the fixed probe set (matches PROBE_TEXTS in cm0_bench.py -- the C/M1 box-parity inputs)
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


def l2(v):
    v = np.asarray(v, dtype=np.float32)
    return v / (np.linalg.norm(v) + 1e-9)


def build_gguf_golden():
    from huggingface_hub import hf_hub_download
    import llama_cpp
    from llama_cpp import Llama
    path = hf_hub_download(GGUF_REPO, GGUF_FILE)
    llm = Llama(model_path=path, embedding=True, n_ctx=512,
                pooling_type=llama_cpp.LLAMA_POOLING_TYPE_LAST, verbose=False)
    vecs, token_ids = [], []
    for t in PROBE_TEXTS:
        ids = list(llm.tokenize(t.encode("utf-8"), add_bos=False, special=False))
        ids.append(EOS_ID)                          # add_eos_token=true: EOS appended + last-pooled
        token_ids.append(ids)
        vecs.append(l2(llm.embed(t)))               # llama.cpp last-token pool, then L2 here
    dim = len(vecs[0])
    return np.stack(vecs).astype(np.float32), token_ids, dim, Path(path).name


def build_f32_golden():
    from sentence_transformers import SentenceTransformer
    m = SentenceTransformer(ST_MODEL)
    V = m.encode(PROBE_TEXTS, normalize_embeddings=True, convert_to_numpy=True,
                 show_progress_bar=False)           # sym:none (no prompt), pooled + L2
    return np.asarray(V, dtype=np.float32)


def main():
    print("building GGUF (Q8_0) golden via llama.cpp ...")
    gg, token_ids, gdim, gname = build_gguf_golden()
    print("  GGUF golden: %d probes x %d dim  (%s)" % (gg.shape[0], gg.shape[1], gname))

    print("building F32 golden via sentence-transformers ...")
    f32 = build_f32_golden()
    print("  F32 golden : %d probes x %d dim" % (f32.shape[0], f32.shape[1]))
    assert gg.shape == f32.shape, (gg.shape, f32.shape)

    # --- MEASURE the quant floor = per-probe cosine(GGUF Q8_0, ST F32) ---
    cos = [float(np.dot(gg[i], f32[i])) for i in range(len(PROBE_TEXTS))]
    gap = [1.0 - c for c in cos]
    print("\n== QUANT FLOOR (GGUF Q8_0 vs ST F32, per probe) ==")
    for i, t in enumerate(PROBE_TEXTS):
        print("  cos=%.5f  gap=%.2e  %s" % (cos[i], gap[i], t[:46]))
    qfloor_max = max(gap)
    print("  cosine: min=%.5f mean=%.5f | quant-gap max=%.2e mean=%.2e"
          % (min(cos), sum(cos) / len(cos), qfloor_max, sum(gap) / len(gap)))
    # honest F32 tolerance = the measured max quant gap, with a little headroom (NOT a guessed 1e-3)
    f32_tol = float(max(qfloor_max * 1.5, 5e-4))
    print("  -> honest F32 end-to-end tolerance for C/M1a = %.2e (measured, not guessed)" % f32_tol)

    # --- keep the frozen mu from the existing C/M0.5 golden (fit on the mu-corpus, probe-independent).
    # allow_pickle=False (safe): we read ONLY the float 'mu' array, never an object array. ---
    mu = None
    if GOLDEN_NPZ.exists():
        old = np.load(GOLDEN_NPZ, allow_pickle=False)
        if "mu" in old.files and np.any(old["mu"]):
            mu = np.asarray(old["mu"], dtype=np.float32)
            print("  kept frozen mu from the C/M0.5 golden (dim %d)" % mu.shape[0])
    if mu is None:
        mu = np.zeros(gg.shape[1], dtype=np.float32)
        print("  WARNING: no mu found; stored zeros (re-run cm0_bench.py to refit)")

    # --- save the two goldens as PURE FLOAT arrays (no object dtype -> no pickle on load; texts +
    # token-ids live in golden_meta.json instead, which is JSON and C-reader-friendly). ---
    np.savez(GGUF_NPZ, vectors_raw=gg)
    np.savez(GOLDEN_NPZ, vectors_raw=f32, mu=mu)
    print("\nsaved %s (tight engine-vs-engine target) + %s (F32 target + mu)"
          % (GGUF_NPZ.name, GOLDEN_NPZ.name))

    meta = {
        "model": ST_MODEL,
        "gguf_repo": GGUF_REPO, "gguf_file": GGUF_FILE, "quant": "Q8_0",
        "kind": "qwen3", "dim": int(gg.shape[1]),
        "prompt": "sym:none (NO instruction prefix)",
        "pooling": "last-token (post-final-norm state at llama_quant.c:1801, pre-LM-head)",
        "eos_appended": EOS_ID, "add_bos": False, "add_eos": True,
        "pre_tokenizer": "qwen2 (GPT-2-style byte-level rank-BPE)",
        "l2_normalized": True,
        "mean_projection": {"applied_at": "C/M2 downstream (NOT in the parity gate)",
                            "formula": "e' = normalize(e - (e.mu) mu), mu in golden_vectors.npz",
                            "note": "the C/M0.5 winner used mean-projection for CLEAN separation; it is "
                                    "a deterministic post-pool transform, excluded from the C/M1a port gate"},
        "n_probe": len(PROBE_TEXTS),
        "token_ids": {PROBE_TEXTS[i]: token_ids[i] for i in range(len(PROBE_TEXTS))},
        "parity_gates": {
            "1_token_parity": "C-engine token-ids (incl. EOS %d) == gguf_golden token_ids, EXACT" % EOS_ID,
            "2_engine_vs_engine": "C-engine raw (Q8_0, AVX2) vs gguf_golden.vectors_raw: cosine>=1-1e-4 "
                                  "AND report max per-element abs diff (FP-associativity)",
            "3_f32_sanity": "C-engine raw vs golden_vectors.vectors_raw (F32): gap <= %.2e (MEASURED "
                            "quant floor, not guessed)" % f32_tol},
        "measured_quant_floor": {"cos_min": float(min(cos)), "cos_mean": float(sum(cos) / len(cos)),
                                 "gap_max": qfloor_max, "gap_mean": float(sum(gap) / len(gap)),
                                 "f32_tolerance": f32_tol},
    }
    GOLDEN_META.write_text(json.dumps(meta, indent=2), encoding="utf-8")
    print("wrote %s" % GOLDEN_META.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
