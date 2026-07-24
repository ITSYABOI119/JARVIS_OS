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
- **C/M0 (OFF-BOX, Python — gates the whole arc): DONE 2026-07-24 — GATE = DO NOT PROCEED to C/M1 on
  an off-the-shelf model (see the C/M0 FINDINGS section below).** Prototyped EmbeddingGemma-300M (the
  strong-reuse candidate) AND bge-small-en-v1.5 (the 33M baseline) — plus, when both fell short, the
  bigger off-the-shelf probes bge-large-en-v1.5 and e5-large-v2 (§5's "a bigger embedder" contingency)
  — on OUR labeled data; scored recall (the C/M2 gate) + intent clustering (routing-relevant). The
  golden-vector save (§4) is DEFERRED — it is C/M1 setup for the CHOSEN model, and the gate reopened
  the model choice before any box work. Harness + data + raw results: `phase3/scripts/embed/`.
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

## C/M0 FINDINGS (2026-07-24, off-box, RTX 2070; harness `phase3/scripts/embed/cm0_bench.py`)

**GATE DECISION: DO NOT PROCEED to C/M1 on an off-the-shelf model.** No off-the-shelf embedder clears
the recall bar (~≥90% top-1 WITH clean separation) on our narrow technical domain. This is the
"prove-it-or-don't" outcome the milestone exists to produce — the gate stopped the arc BEFORE any box
engine work.

**Results** — recall = the C/M2 GATE (a hand-authored, topically-DISTINCT query→prior-query set,
symmetric, N=16); adv = a near-synonym disambiguation stress (N=6); intent = nearest-centroid on the
73-item routing HELDOUT; INFER-FP = the 6-6 conceptual-metric-noun cases clustering with INFER:

| model | dim | recall@1 | recall@3 | adv@1 | clean sep? | intent(routing) | INFER-FP |
|---|---|---|---|---|---|---|---|
| bge-small-en-v1.5 | 384 | 56% (9/16) | 69% | 33% | no | 89.0% | 6/6 |
| **bge-large-en-v1.5** | 1024 | **81% (13/16)** | **88%** | 50% | no (closest, gap 0.08) | 90.4% | 5/6 |
| e5-large-v2 | 1024 | 50% | 69% | 67% | no | 93.2% | 6/6 |
| **EmbeddingGemma-300M** (reuse candidate) | 768 | 50% (8/16) | 56% | 33% | no | 83.6% | 4/6 |

**Three decision-relevant findings:**
1. **Size helps, a lot** — bge-small→bge-large (33M→335M) lifts recall@1 56%→81%. The ceiling is
   partly model scale, not only domain.
2. **The strong-reuse candidate LOSES.** EmbeddingGemma-300M — the near-zero-box-engine Gemma-arch path
   §2a favored — is among the WEAKEST here (50% recall@1, worst top-3, worst intent, worst INFER-FP).
   A genuine "MTEB doesn't transfer to a narrow domain" result (§2f, now demonstrated, not asserted).
3. **The plan's lane prediction is INVERTED.** §2c/§4 guessed ROUTING would be the likely off-the-shelf
   MISS and recall the safe win. The data shows the OPPOSITE: intent clustering is decent off-the-shelf
   (83–93%), while **RECALL is the hard lane** — and NO model achieves clean separation (see below).
   Corollary: an off-the-shelf embedder does NOT beat the deployed keyword router for routing either
   (83–93% centroid vs the 6-6 router's measured 95.89%), so routing is not a free win at C/M4 either.

**The blocker isn't just accuracy — it's SEPARATION (anisotropy).** A sanity check (validated the
pipeline: obvious paraphrases score 0.90–0.92, correctly above unrelated) also showed every model is
strongly anisotropic — unrelated everyday sentences sit at cosine ~0.5–0.7 (EmbeddingGemma's
unrelated-pair floor: min 0.51 / mean 0.62). So no model's weakest true pair clears its strongest
unrelated pair (all OVERLAP; bge-large is closest at 0.432 vs 0.511). For a RECALL lane that must
decide "inject a prior answer or not," poor separation = false-recall risk — the exact P6 contamination
class 6-5 fought to eliminate. A fixed absolute threshold is unreliable here.

**Methodology corrections made before trusting any number** (the numbers moved a lot as these landed —
recorded so the result is auditable, not cherry-picked):
- The recall task is SYMMETRIC query-to-query (the faithful extension of 6-5's key-over-the-QUERY
  match), NOT asymmetric query→document retrieval. Running EmbeddingGemma in the wrong (document-prompt)
  mode alone dropped it to 16.7% — a misuse, not a result. The harness now SWEEPS the sensible symmetric
  prompt strategies per model and reports the best (a mild optimistic bias, noted).
- The first recall corpus packed near-synonyms (page-fault/paging/vm/mmu; mutex/semaphore/spinlock),
  making exact-top-1 ill-defined. Split into a topically-DISTINCT gate set (fair ground truth) + a
  separate adversarial near-synonym stress set.

**RECOMMENDED NEXT STEP (reopens the model choice per §5, BEFORE any box work) — a 2070 CONTRASTIVE
FINE-TUNE of a DECODER-arch (Gemma-arch) embedder.** This is the coherent path because it (a) fixes the
MEASURED miss (off-the-shelf recall + separation), (b) directly attacks anisotropy — contrastive
training with in-batch/hard negatives pushes negatives apart, the separation the lane needs, and (c)
PRESERVES §2a's near-zero-box-engine reuse (a fine-tuned EmbeddingGemma/Gemma-arch embedder still loads
on the box's existing Gemma path). Note the doc scoped the 2070 fine-tune as a ROUTING contingency; the
data reassigns it to RECALL. **Off-the-shelf fallback if the fine-tune is deferred:** bge-large-en-v1.5
(81% top-1 / 88% top-3, best separation) — but at the cost of a NET-NEW BERT engine on the box (§2a's
rejected-first-choice) AND still no clean separation. Not recommended over the fine-tune.

**Honest caveats on these numbers:** the recall set is tiny and hand-authored (N=16 distinct / 6
adversarial) → wide error bars (81% = 13/16, ±~1 item is ±6%); the ~90% bar on a hard hand-authored
paraphrase set is a soft target; the per-model best-of-sweep prompt pick is a mild optimistic bias; and
this measures OFF-BOX sentence-transformers vectors (the C-engine/GGUF parity is a separate C/M1
concern, deferred here since the gate did not pass). The real validation was always meant to be on-box
with the accumulating control-IN store — this is a pre-box SIGNAL that correctly gated the arc.

## 4. Risks / honest limits
- ~~Off-the-shelf may miss ROUTING (weak zero-shot); recall/cache are the safe wins.~~ **INVERTED by
  C/M0 (2026-07-24): off-the-shelf misses RECALL (50–81% top-1, no clean separation), while intent/
  routing clustering is relatively strong (83–93%). The measured miss is in the RECALL lane, so the
  2070 contrastive fine-tune is reassigned there.** Fine-tune stays scoped to the measured miss, not
  pre-committed.
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
