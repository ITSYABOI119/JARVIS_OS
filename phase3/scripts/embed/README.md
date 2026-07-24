# Phase C / C/M0 — off-box embedder scoring harness

Off-box (Main-PC + RTX 2070) Python measurement that GATES the Phase-C embedding arc: it scores
off-the-shelf embedders on OUR OWN labeled data (NOT MTEB — our domain is narrow/technical) for the
semantic-RECALL use case, and decides which model (if any) and whether a fine-tune is needed. **No box,
no seL4, no deploy — pure measurement.**

## Files
- `cm0_bench.py` — the harness. Parses `../../src/ai/routing_suite.h` live for the intent set, loads
  `cm0_recall_set.json`, scores each model with its OFFICIAL sentence-transformers prompts, and prints
  a comparison table. NOT a CI step (needs a GPU + gated model downloads).
- `cm0_recall_set.json` — the hand-authored recall corpus: `distinct_positives` (topically distinct →
  the fair gate), `adversarial_positives` (near-synonym disambiguation stress), `negatives` (unrelated,
  for false-recall). A stored turn = a prior control-IN query; a `later` = a NEW query about the same
  topic but lexically different (so 6-5's exact-key recall can't match it — only meaning can).
- `cm0_results.json` — the raw per-model / per-strategy metrics from the last run (committed as
  evidence; deterministic given the models).

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

## Result (2026-07-24)
GATE = **DO NOT PROCEED to C/M1 on off-the-shelf** — no model clears ~≥90% recall@1 with clean
separation on our domain; the strong-reuse candidate (EmbeddingGemma-300M) underperforms, and recall —
not routing — is the hard lane. Recommended next step: a 2070 contrastive fine-tune of a Gemma-arch
embedder. Full write-up: `phase6/docs/PHASE_6_GOAL_C_EMBEDDER.md` (C/M0 FINDINGS).
