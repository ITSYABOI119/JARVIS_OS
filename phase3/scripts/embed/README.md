# Phase C / C/M0 — off-box embedder scoring harness

Off-box (Main-PC + RTX 2070) Python measurement that GATES the Phase-C embedding arc: it scores
off-the-shelf embedders on OUR OWN labeled data (NOT MTEB — our domain is narrow/technical) for the
semantic-RECALL use case, and decides which model (if any) and whether a fine-tune is needed. **No box,
no seL4, no deploy — pure measurement.**

## Files
- `cm0_bench.py` — the harness (C/M0 + C/M0.5). Parses `../../src/ai/routing_suite.h` live for the
  intent set, loads `cm0_recall_set.json`, scores each base with its OFFICIAL sentence-transformers
  prompts (symmetric query-to-query sweep), runs the mean-projection ablation (`mu` from
  `cm0_generic_corpus.txt` + our data, frozen), prints a base-selection table, and saves golden vectors
  for the winner. NOT a CI step (needs a GPU + gated model downloads).
- `cm0_recall_set.json` — the hand-authored recall corpus (ENLARGED to ~36 distinct at C/M0.5):
  `distinct_positives` (topically distinct → the fair gate), `adversarial_positives` (near-synonym
  disambiguation stress), `negatives` (unrelated, for false-recall). A stored turn = a prior control-IN
  query; a `later` = a NEW query about the same topic but lexically different (so 6-5's exact-key recall
  can't match it — only meaning can).
- `cm0_generic_corpus.txt` — ~110 diverse everyday/science/arts sentences for the frozen mean-direction
  `mu` estimate (the harness also folds in our own data).
- `cm0_results.json` — the raw per-model / per-strategy metrics from the last run (committed evidence).
- `golden_vectors.npz` + `golden_meta.json` — the C/M1 box-parity reference for the WINNER
  (Qwen3-Embedding-0.6B, sym:none + frozen mean-projection): 15 probe texts × 1024-d, L2-normalized,
  with `mu`. C/M1's C engine must reproduce these to 1e-3.

## Reproduce
EmbeddingGemma is HF-license-gated: accept the license at
https://huggingface.co/google/embeddinggemma-300m and `hf auth login` with a read token first. Its
Gemma3 modeling code needs **torch ≥ 2.6**, so run under the interpreter that has it:

```
# miniconda python has torch 2.7.1 (the py -3 env has 2.5.1, which errors on the gemma3 mask):
"$USERPROFILE/miniconda3/python.exe" phase3/scripts/embed/cm0_bench.py
"$USERPROFILE/miniconda3/python.exe" phase3/scripts/embed/cm0_bench.py --verbose   # dump recall misses
"$USERPROFILE/miniconda3/python.exe" phase3/scripts/embed/cm0_bench.py --models BAAI/bge-large-en-v1.5
```

Models (GGUF or HF, hundreds of MB) are NEVER committed; neither is any HF token.

## Result
- **C/M0 (2026-07-24):** on the bases it tested (EmbeddingGemma + bge/e5), no off-the-shelf model
  cleared the recall bar → "fine-tune required." SUPERSEDED by C/M0.5.
- **C/M0.5 (2026-07-24):** WINNER = **Qwen/Qwen3-Embedding-0.6B**, which **clears the recall bar
  off-the-shelf** (97.2% recall@1 / 100% top-3 / CLEAN separation with mean-projection on N=36). The
  C/M0 "fail" was testing the wrong bases (raw EmbeddingGemma + BERT encoders). A contrastively-trained
  DECODER embedder wins on accuracy AND separation, at partial box-reuse cost. **So fine-tuning is NOT
  needed for the recall lane** — go straight to C/M1 (box engine + parity); the 2070 fine-tune is a
  measured-miss contingency. Full write-up: `phase6/docs/PHASE_6_GOAL_C_EMBEDDER.md` (C/M0.5 FINDINGS).

## C/M1a — HOST parity foundation (2026-07-24)
The gate before any box work: prove the deployed C engine reproduces reference embeddings for
Qwen3-Embedding-0.6B. `cm1a_golden.py` builds the **two-golden** foundation (the methodology fix — one
golden conflates a PORT bug with quant error):
- `gguf_golden.npz` — the OFFICIAL `Qwen/Qwen3-Embedding-0.6B-GGUF` at **Q8_0** (the box quant), embedded
  via llama-cpp-python (last-token pool + L2). The **tight engine-vs-engine** target (~1e-4): same quant,
  so a C-vs-GGUF gap is a PORT bug. Its `token_ids` (in `golden_meta.json`, incl. EOS 151643) are the
  **token-parity reference**.
- `golden_vectors.npz` — the sentence-transformers **F32** golden (sym:none, last-pool + L2, NO
  mean-projection) + the frozen `mu`. The **loose end-to-end** target at a **MEASURED** tolerance.
- **Measured quant floor** (GGUF Q8_0 vs ST F32): cosine 0.99915–0.99965 → honest F32 tolerance
  **1.27e-3** (measured, not a guessed 1e-3). The 0.999+ cross-engine cosine also confirms the
  pooling/EOS config. Config: pooling = last-token; add_bos=false, add_eos=true (EOS=151643 appended +
  pooled); pre-tokenizer = qwen2 (GPT-2-style byte-level rank-BPE); prompt = sym:none.

**Remaining C/M1a (the C-engine port — the next phase):** a merge-RANK BPE path for qwen in
`tokenizer.c` (the deployed path is score-priority SentencePiece — wrong for qwen; the merges list is
already loaded in `gguf_vocab.c`), RoPE-NEOX for qwen3, a gated embed-mode forward (last-token pool at
`llama_quant.c:1801` → skip LM head → L2 → mean-project → L2), a host harness compiled with
`-DJARVIS_EMBED=1 -mavx2 -mfma`, and the 3 parity gates in `golden_meta.json` (token-parity GREEN →
engine-vs-engine ≤1e-4 → F32 ≤1.27e-3). All engine edits `#if JARVIS_EMBED`-gated from the start.

## Reproduce (C/M0.5 candidates)
`Alibaba-NLP/gte-large-en-v1.5` needs `trust_remote_code=True` (the harness retries automatically);
`Qwen/Qwen3-Embedding-0.6B` / `mxbai-embed-large-v1` are ungated; EmbeddingGemma is license-gated.
Default `cm0_bench.py` runs the ref (bge-small/large) + candidate (Qwen3 / gte / mxbai / EmbeddingGemma)
set, the mean-projection ablation, and saves golden for the winner.
