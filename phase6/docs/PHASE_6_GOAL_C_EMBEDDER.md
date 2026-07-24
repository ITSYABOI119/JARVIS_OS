# Phase C — Small Embedding Model (semantic matching) — PLAN-FIRST

**Status: PLAN-FIRST. The neural arc that follows the Phase-6 graduation (6-7). Grounded by a 4-lens
research pass 2026-07-24. C/M0 is off-box; box integration (C/M1+) gets its own pre-mortem.**

> Line-number caveat: every `file:line` below was verified once at authoring against HEAD, but line
> numbers drift — RE-GREP the distinctive string before relying on any citation at implementation time.
> (Verified at authoring: the pooling tap `llama_quant.c:1801` is the `memcpy(state->x, state->xb, …)`
> immediately after the final `tensor_rms_norm` and before the output projection; `dot_f32_avx2` exists
> at `llama_quant.c:243`; `g3_select_exact_only` in `g3_retrieval.h`; `CTRL_EPI_BASE_LBA = 21140000`.)

## 1. Goal + honest scope
Add a small embedding model (runs on the box CPU in PB, trained/tuned off-box on the RTX 2070 only IF
a lane misses) for SEMANTIC MATCHING. First payoff: **semantic RECALL** — close 6-5's documented
exact-repeat-only limit ("state a fact, then ask a DIFFERENT question"). Later: semantic routing
(if the 6-6 keyword router's measured misses justify it) + a semantic cache. NOT a claim of
understanding — meaning-based nearest-match over the box's own prior answers, measured on our data.

## 2. Locked technical direction (from the research)
a. DECODER-style embedder (reuses the box's Gemma-arch load+forward+tokenizer+kernels; the pooling tap
   already exists at llama_quant.c:1801, post-final-norm, pre-LM-head). A BERT encoder is REJECTED as
   a first choice — it is a net-new second engine (bidirectional attn + learned/token-type pos-emb +
   LayerNorm + WordPiece). Lead candidate: EmbeddingGemma-300M (Gemma-arch, GGUF, Matryoshka->128).
b. OFF-THE-SHELF FIRST. No training until a lane's measured miss justifies it; routing is the only
   likely miss (weak zero-shot intent) -> a routing-only 2070 contrastive fine-tune is a LATER
   contingency (bf16 + LoRA + in-batch negatives / SetFit, hundreds of pairs).
c. RECALL is the FIRST lane (PA-has-no-compute doesn't bite: control-IN queries already pay full PB
   inference; tiny PB-adjacent corpus; one selector swap; closes a named limit). Routing + cache
   DEFERRED.
d. The embedder runs in PB (PA has no compute); a MSG_EMBED IPC path PA<->PB; a co-resident second
   model in PB (memory + NVMe-boot budget). Cosine == dot over the existing dot_f32_avx2; NO ANN at
   this scale (thousands of vectors max).
e. PARITY IS THE SILENT-FAILURE GUARD: our C engine must reproduce the Python/llama.cpp reference
   vectors to ~1e-3 (pooling type + any prefix + L2-norm replicated exactly). A mismatch degrades
   silently. This gates C/M1.
f. The GATE is OUR data, not MTEB (narrow/technical domain). Prove-it-or-don't, the 6-6 held-out
   discipline.

## 3. Milestones
- **C/M0 (OFF-BOX, Python — gates the whole arc):** prototype EmbeddingGemma-300M (strong-reuse
  candidate) AND bge-small-en-v1.5 (33M cheap baseline it must beat) in Python/llama.cpp; embed our
  own labeled data (routing_suite.h paraphrase clusters + the control-IN seeds + a small hand-authored
  "state a fact / ask a different question" recall set); score the two-part benchmark: (1) nearest-
  centroid HELDOUT accuracy, (2) positive/negative separation margin. SAVE the reference vectors
  (golden, for C/M1 parity). GATE: proceed only if HELDOUT paraphrase clustering is strong (target
  >=~90%) AND positives/negatives are cleanly threshold-separable on held-out data. The score picks
  the model AND decides off-the-shelf-vs-fine-tune. NO box code.
- **C/M1 (BOX, gated new flag e.g. JARVIS_EMBED default-0; pre-mortem first):** the embed-mode forward
  in PB (pool + L2-norm + skip the LM head; the :1801 tap), the co-resident second GGUF, the MSG_EMBED
  IPC, + THE PARITY HARNESS (C-engine vectors == C/M0 golden to 1e-3). OFF byte-identical. Prove the
  C engine reproduces the reference embeddings before wiring any lane.
- **C/M2 (BOX, gated):** the RECALL lane — replace g3_select_exact_only with cosine-topk over per-
  record vectors in the dedicated control-IN store @21,140,000 (add a vector column); the semantic
  "state-a-fact/ask-different" recall works; benchmark on the box; the answer-only-preamble hygiene
  (P6/P7) preserved.
- **C/M3 (BOX):** telemetry (a semantic-recall counter, riding the exhausted-flags pattern) + console
  + the flip (JARVIS_EMBED default-ON) with supervised proof, honest limits.
- **C/M4+ (LATER, measured-miss-gated):** routing (with the 2070 fine-tune if zero-shot misses) +
  semantic cache. Each its own slice.

## 4. Risks / honest limits
- Off-the-shelf may miss ROUTING (weak zero-shot); recall/cache are the safe wins. Fine-tune is scoped
  to the miss, not pre-committed.
- The pooling/prefix parity trap (§2e) is the top silent-failure risk — the harness is mandatory.
- A second resident model competes with Gemma for the single box CPU + PB heap; measure the per-query
  embed latency on-box (C/M1) before claiming the recall path stays cheap.
- MTEB numbers may not transfer to our narrow domain — the gate is our data.
- Honest claim ceiling: meaning-based recall of the box's own prior answers, NOT "understands" /
  "remembers your conversation" as reasoning.

---

*Companion to the Phase-6 goal docs (this arc follows the 6-7 graduation) and the neural-future
roadmap. PLAN-FIRST — authored 2026-07-24 from a 4-lens grounding research pass (folded); no code, flag,
or wire change. C/M0 is off-box Python (Main-PC + the RTX 2070) and gates the whole arc; C/M1+ box
integration gets its own pre-mortem. RE-GREP every `file:line` before relying on it.*
