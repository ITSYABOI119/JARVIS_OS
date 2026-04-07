# Phase 3c Dynamic Scaling — Model Bench-Off (April 7, 2026)

**Author:** Claude (research assistant)
**Purpose:** Survey current CPU-friendly LLMs (April 2026) for JARVIS AI-OS 4-state dynamic scaling tiers (IDLE / ACTIVE / CRITICAL / EMERGENCY) on Ryzen 7 2700X, 32GB RAM, custom C inference engine.
**Status:** Section 1-3 only. Section 3 is the unbiased recommendation; further sections deferred per task instructions.

## Hard Constraints (recap)

| Constraint | Value |
|---|---|
| CPU | Ryzen 7 2700X (Zen+, AVX2, no AVX-512, 8C/16T) |
| RAM ceiling per model | ~8 GB |
| Inference engine | Custom C, vanilla transformer + RoPE + GQA + SwiGLU + RMSNorm |
| Quantization formats supported | F32, F16, Q4_0, Q8_0, **Q4_K, Q6_K** only |
| `head_dim` | **≥ 128** required for ACTIVE / CRITICAL tiers (KV compression dependency); IDLE exempt |
| License | Permissive (Apache 2.0, MIT, Llama, Gemma, similar) — must allow standalone deployment |
| Distribution | Hugging Face GGUF Q4_K_M must exist |

**Tier targets:**
- **IDLE** ≈ 700 MB – 1.5 GB
- **ACTIVE** ≈ 2 – 4 GB (`head_dim` ≥ 128)
- **CRITICAL** ≈ 5 – 8 GB (`head_dim` ≥ 128)
- **EMERGENCY** rules-only (no model)

---

## Section 1 — Model Survey

Wide-net survey across 0.5B to ~9B dense models released April 2025 → April 2026, plus baseline models still in current use. **Q4_K_M sizes verified directly from Hugging Face repo Files tab.** Architecture facts come from each model's HF model card or config.json; benchmark numbers come from the official model cards where available, falling back to apxml.com / llm-stats.com / project blog posts (cited in §4).

| # | Model | Params | Hidden | n_layers | n_heads (Q/KV) | head_dim | Vocab | Arch (RoPE / GQA / SwiGLU / RMSNorm) | License | HF GGUF (Q4_K_M) | Q4_K_M size | MMLU | HumanEval | GSM8K | IFEval | Notes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | **Llama 3.2 1B Instruct** | 1.24B | 2048 | 16 | 32 / 8 | **64** | 128,256 | RoPE / GQA / SwiGLU / RMSNorm — **vanilla** | Llama 3.2 | `bartowski/Llama-3.2-1B-Instruct-GGUF` | **0.81 GB** | 49.3 (5-shot) | n/r | 44.4 (CoT) | 59.5 | ARC-C 59.4. Already proven in JARVIS Phase 3 |
| 2 | **Llama 3.2 3B Instruct** | 3.21B | 3072 | 28 | 24 / 8 | **128** | 128,256 | vanilla | Llama 3.2 | `bartowski/Llama-3.2-3B-Instruct-GGUF` | **2.02 GB** | 63.4 (5-shot) | n/r | 77.7 (CoT) | 77.4 | ARC-C 78.6 |
| 3 | **Llama 3.1 8B Instruct** | 8.03B | 4096 | 32 | 32 / 8 | **128** | 128,256 | vanilla | Llama 3.1 | `bartowski/Meta-Llama-3.1-8B-Instruct-GGUF` | **4.92 GB** | 69.4 (5-shot) | 72.6 (0-shot pass@1) | 84.5 (CoT) | 80.4 | ARC-C 83.4. Strongest "boring" baseline |
| 4 | **Qwen3 4B Instruct (2507)** | 4.0B (3.6B non-emb) | 4096 | 36 | 32 / 8 | **128** (explicit in config.json) | 151,936 | vanilla, RoPE+YaRN | Apache 2.0 | `Qwen/Qwen3-4B-GGUF` | **2.5 GB** | 69.6 (MMLU-Pro) | n/r in card | n/r in card | **83.4** | GPQA Diamond 62.0; native ctx 262K |
| 5 | **Qwen3 8B Instruct** | 8.2B (6.95B non-emb) | 4096 | 36 | 32 / 8 | **128** | 151,936 | vanilla, RoPE+YaRN | Apache 2.0 | `Qwen/Qwen3-8B-GGUF` | **5.03 GB** | (per Qwen blog: outperforms Qwen2.5-14B Base on >50% of benchmarks) | per blog | per blog | per blog | "Thinking mode" optional. Best Qwen3 dense that fits 8GB |
| 6 | **Phi-4-mini Instruct** | 3.84B | 3072 | 32 | 24 / 8 | **128** | 200,064 | **fractional RoPE (75% rotated, 25% position-agnostic)** + GQA + SwiGLU + RMSNorm + tied embeddings | MIT | `bartowski/microsoft_Phi-4-mini-instruct-GGUF` | **2.49 GB** | **67.3** (5-shot) | n/r | **88.6** (CoT) | n/r | MMLU-Pro 52.8, ARC-C 83.7. **Partial RoPE will require engine work** — see §2 |
| 7 | **SmolLM3 3B Instruct** | 3.08B | 2048 | 36 | 16 / 4 | **128** | 128,256 | **NoPE/RoPE 3:1 hybrid** (3 of 4 layers use no positional encoding), GQA, SwiGLU, RMSNorm | Apache 2.0 | `ggml-org/SmolLM3-3B-GGUF` | **1.92 GB** | 44.1 (MMLU-CF) | 30.5 (HumanEval+) | 67.6 (5-shot) | **76.7** | ARC-CF 65.6. **NoPE 3:1 will require engine work** — see §2 |
| 8 | **Gemma 3 4B IT** | 4.3B | 2304 | 26 | 8 / 4 | **256** (explicit, NOT hidden/n_heads) | 262,144 | RoPE + **alternating local-sliding-window + global attention** + GQA + SwiGLU + RMSNorm; SigLip vision encoder (ignored for text) | Gemma | `bartowski/google_gemma-3-4b-it-GGUF` | **2.49 GB** | 59.6 (5-shot) | 36.0 (0-shot) | 38.4 (8-shot) | n/r | ARC-C 56.2. **Sliding-window attention will require engine work** — see §2 |
| 9 | **Gemma 3 1B IT** | 999M | 1152 | 26 | 4 / 1 | 256 (explicit) | 262,144 | RoPE + sliding-window-only (no full attention) + GQA + SwiGLU + RMSNorm | Gemma | `bartowski/google_gemma-3-1b-it-GGUF` | ~0.72 GB | (per Gemma blog: ~38–42 MMLU class) | n/r | n/r | n/r | Text-only. Same sliding-window concern as Gemma 3 4B |
| 10 | **Mistral Ministral 3 8B Instruct (2512)** | 8.4B (text) + 0.4B vision | 4096 | 34 | 32 / 8 | **128** | 131,072 | vanilla RoPE + GQA + SwiGLU + RMSNorm. Text-only variant exists separately | Apache 2.0 | `lmstudio-community/Ministral-3-8B-Instruct-2512-GGUF` (filename not shown in card) | **5.2 GB** | n/r in HF card | n/r | n/r | n/r | Released Dec 2025; vision encoder optional. Slightly over CRITICAL ceiling but within range |
| 11 | **OLMo 2 7B Instruct (1124)** | 7.3B | 4096 | 32 | 32 / 8 | 128 (derived) | 100,352 | RoPE + GQA + SwiGLU + RMSNorm + **no biases** | Apache 2.0 (with Gemma terms note) | `bartowski/OLMo-2-1124-7B-Instruct-GGUF` | **4.47 GB** | 61.3 | n/r in card | **85.1** | 72.3 | Fully open data + recipe. Good Llama 3.1 8B alternative |
| 12 | **OLMo 2 13B Instruct (1124)** | 13.7B | 5120 | 40 | 40 / 8 | 128 | 100,352 | as OLMo 2 7B | Apache 2.0 (Gemma terms note) | `bartowski/OLMo-2-1124-13B-Instruct-GGUF` | ~8.0 GB | 67.5 | n/r | 87.5 | 75.6 | Right at the 8GB ceiling; risky on 32GB during scaling transitions |
| 13 | **OLMo 3 7B Instruct** | ~7.3B | (per blog) | (per blog) | (per blog) | (per blog) | (per blog) | declared "fully open" follow-up to OLMo 2; vanilla transformer per Ai2 blog (one search hit hinted at hybrid SSM, **needs verification before adoption**) | Apache 2.0 (assumed) | not yet on bartowski as of survey | unknown | matches Qwen 3 8B on MATH per Ai2 blog | leads HumanEvalPlus | n/r | n/r | Released Nov 2025. **HF GGUF availability unverified — abstain until confirmed.** |
| 14 | **Mistral 7B Instruct v0.3** | 7.25B | 4096 | 32 | 32 / 8 | 128 | 32,768 | RoPE + GQA + SwiGLU + RMSNorm — **the canonical reference architecture** | Apache 2.0 | `bartowski/Mistral-7B-Instruct-v0.3-GGUF` | **4.37 GB** | ~62 (5-shot) | ~38 | ~53 | ~57 | Older but rock-solid; small vocab is a memory win |
| 15 | **Mistral Nemo 12B** | 12.2B | 5120 | 40 | 32 / 8 | 128 | 131,072 | vanilla | Apache 2.0 | `bartowski/Mistral-Nemo-Instruct-2407-GGUF` | ~7.5 GB | 68.0 | n/r | ~75 | ~63 | Fits 8GB ceiling. Good "stretch" CRITICAL pick |
| 16 | **Qwen3 1.7B Instruct** | 1.7B | 2048 | 28 | 16 / 8 | **128** (explicit) | 151,936 | vanilla | Apache 2.0 | `Qwen/Qwen3-1.7B-GGUF` | ~1.1 GB | per Qwen blog: matches Qwen2.5-3B Base | n/r | n/r | n/r | Strongest IDLE-class candidate **with** head_dim=128 (i.e., usable in higher tiers too) |
| 17 | **Qwen3 0.6B Instruct** | 0.6B | 1024 | 28 | 16 / 8 | 64 (derived) | 151,936 | vanilla | Apache 2.0 | `Qwen/Qwen3-0.6B-GGUF` | ~0.4 GB | per Qwen blog: strong for size | n/r | n/r | n/r | Smallest viable; sub-IDLE bucket |
| 18 | **Gemma 2 2B IT** | 2.6B | 2304 | 26 | 8 / 4 | 256 | 256,000 | RoPE + GQA + SwiGLU + RMSNorm + **logit soft-cap** + sliding-window in even layers | Gemma | `bartowski/gemma-2-2b-it-GGUF` | **1.71 GB** | 51.3 | 17.7 | 23.9 | 51.0 | Older Gemma; sliding-window concern persists |
| 19 | **TinyLlama 1.1B Chat v1.0** | 1.1B | 2048 | 22 | 32 / 4 | 64 | 32,000 | vanilla | Apache 2.0 | `TheBloke/TinyLlama-1.1B-Chat-v1.0-GGUF` | **0.67 GB** | 25.3 | ~10 | ~2 | ~22 | Cheap reference; weak quality |
| 20 | **OLMo 2 1B Instruct (0425)** | 1.5B | 2048 | 16 | 16 / 16 | 128 (derived) | 100,352 | RoPE + (MHA, n_kv=n_q) + SwiGLU + RMSNorm + no biases | Apache 2.0 | `bartowski/OLMo-2-0425-1B-Instruct-GGUF` | **0.95 GB** | 32.2 | n/r | 65.0 | 67.7 | head_dim=128 even at 1B size — only IDLE-class model that doesn't sacrifice the d_head invariant |

> **n/r** = not reported in the official source consulted. Where the model card omitted a benchmark, I did not invent a number.

---

## Section 2 — Eliminated Candidates

| Model / Family | Why eliminated |
|---|---|
| **Llama 4 Scout (109B / 17B active)** | MoE architecture; even active params overshoot 8GB Q4_K_M; engine has no expert routing |
| **Llama 4 Maverick (400B / 17B active)** | Same as Scout, larger total |
| **Llama 3.3 70B** | 70B at Q4_K_M ≈ 40GB, vastly over budget |
| **Mistral Small 3 / 3.1 24B** | 24B at Q4_K_M ≈ 14GB, over budget |
| **Mistral 3 / Mistral 4** | MoE family (Mistral 4 = 119B/6B active); MoE not supported by engine |
| **Qwen3.5 family (0.8B / 2B / 4B / 9B / MoE)** | **Gated DeltaNet hybrid** (Mamba2 derivative + sparse MoE) — 3 of every 4 layers use linear-attention DeltaNet, not full attention. Engine has no Mamba/SSM kernel. **Hard architectural mismatch.** |
| **Gemma 4 E2B / E4B / 26B-MoE / 31B-Dense** | Architecture introduces **Per-Layer Embeddings (PLE)** (a second embedding table feeding residuals into every decoder layer), **Shared KV Cache** (last N layers reuse K/V from earlier layers), **dual RoPE configs**, and **alternating local-sliding-window + global attention**. Each is a deviation from the vanilla transformer the JARVIS engine implements. 31B Dense also too big. 26B is MoE. **All Gemma 4 sizes eliminated on architectural grounds for the current bench-off.** **See §7 — tracked as deferred contender, not permanent elimination.** Note: Gemma 4 was released Apr 2 2026 — five days before this survey — so this isn't a "we'll get there" deferral on the surface, but the engineering scope to add the missing features is bounded and worth tracking (see §7). |
| **Phi-4 14B / Phi-4 reasoning 14B** | 14B at Q4_K_M ≈ 8.5GB, just over the model ceiling; tight RAM headroom for KV cache + activations on 32GB total. Also Phi-4 (full) uses different RoPE config than Phi-4-mini. |
| **DeepSeek V2 Lite (16B/2.4B-active)** | MoE architecture |
| **DeepSeek V3 / R1** | MoE 671B/37B active; far over budget regardless of activation count |
| **DeepSeek R1 Distill Llama 8B / Qwen 7B** | These are vanilla transformers (Llama-arch / Qwen-arch base), so they would technically pass — but they are derivative reasoning fine-tunes whose quality on non-reasoning workloads varies. **Listed here for transparency, not eliminated; could be promoted into §1 on a follow-up pass if the user wants reasoning-tuned options.** |
| **Yi-1.5 9B** | Apache 2.0, vanilla transformer, head_dim=128, 4.4 GB Q4_K_M — actually a viable candidate. **Dropped from §1 because release is now ~18 months old and Qwen3 8B / Llama 3.1 8B / OLMo 2 7B all measurably outperform it.** Mention only. |
| **Phi-3.5 mini 3.8B** | Superseded by Phi-4-mini in early 2025. Not worth listing alongside the newer version. |
| **Phi-4 multimodal** | Same backbone as Phi-4-mini but with vision encoder bolted on; same partial-RoPE concern, larger and not relevant to text-only inference |
| **Gemma 2 9B IT** | Apache 2.0 (sort-of — Gemma license), vanilla-ish but with logit soft-cap + sliding-window; slightly older than Gemma 3 family. **Listed here as a fallback only, displaced by Llama 3.1 8B and Qwen3 8B in current rankings.** |
| **Stable LM 2 1.6B** | Smaller vocab, weak benchmarks, no recent updates |
| **MiniCPM 3 4B / MiniCPM-V** | Vision/edge focus; MiniCPM 3 uses MLA-style attention (DeepSeek MLA), not vanilla GQA — engine mismatch |
| **MiniCPM 4 8B** | MLA attention, eliminated for same reason |
| **InternLM 2.5 7B / 8B** | Vanilla transformer Apache 2.0 — actually viable but no longer competitive vs. Llama 3.1 8B / Qwen3 8B |
| **Granite 3.1 / 3.2 small models (IBM)** | Apache 2.0, vanilla. Listed here for completeness — could be added to §1 in a follow-up if needed |
| **xLAM-2 8B (function-calling fine-tune)** | Llama 3.1 base, vanilla — viable, but agent-tuned not general-purpose. Out of scope |

### Models with Q4_K_M GGUF but architecture caveats (NOT eliminated, but flagged)

These are in §1 with a "see §2" pointer because they require explicit engine work before they will run on the existing JARVIS C inference engine. They are kept in §1 because they are legitimately the best in their size class on quality:

1. **Phi-4-mini** — fractional RoPE: only 75% of `head_dim` is rotated, the last 25% is position-agnostic. The current `qmodel_forward` in `phase3/src/ai/llama_quant.c` rotates the entire `head_dim`. Engine work needed: split the per-head dim into "rotated" and "static" halves and apply RoPE only to the rotated portion. **Estimated impact: 30-50 LOC change in the RoPE block, plus a config bool (`use_fractional_rope`) loaded from GGUF metadata.**
2. **SmolLM3** — NoPE/RoPE 3:1 ratio: 3 of every 4 transformer layers use **no** positional encoding at all; only 1 in 4 uses RoPE. Engine work needed: per-layer flag controlling whether RoPE is applied. **Estimated impact: smaller than Phi-4-mini's, but the config layout in GGUF is non-standard and would need a parser change.**
3. **Gemma 3 4B** — alternating local-sliding-window (1024 tokens) + global attention layers, plus `head_dim=256` (which is fine, ≥128). Engine work needed: per-layer attention mask logic. **Estimated impact: more invasive — sliding window changes the KV cache addressing scheme.**
4. **Mistral Ministral 3 8B** — text backbone is vanilla but the published checkpoint includes a 0.4B vision encoder; need to load only the LLM tensors. **Estimated impact: GGUF tensor filtering, minor.**

If the user wants to keep the engine perfectly vanilla, the eligible-without-engine-work shortlist becomes:
- IDLE: Llama 3.2 1B, Qwen3 0.6B, Qwen3 1.7B, OLMo 2 1B, TinyLlama 1.1B
- ACTIVE: Llama 3.2 3B, Qwen3 4B
- CRITICAL: Llama 3.1 8B, Qwen3 8B, OLMo 2 7B, Mistral 7B v0.3, Mistral Nemo 12B (over 8GB)

---

## Section 3 — Top Picks (no anchoring, honest read of the survey data)

These are ranked purely on what the data says, with the JARVIS engine's vanilla-transformer requirement as a hard filter. I will note when I am picking the *safer* model over the *higher-benchmark* model and why.

### IDLE pick — `Llama 3.2 1B Instruct`

**Why this and not something else:**
- **Already proven on the existing engine.** JARVIS Phase 3 currently runs this exact model (Q4_K_M, 0.81 GB) end-to-end on bare-metal Ryzen 7 2700X via NVMe runtime loading. Picking it for IDLE means **zero engine work and zero validation risk** for the smallest tier — which is exactly what IDLE is for.
- The IDLE tier explicitly does NOT require `head_dim ≥ 128`, so the model's `head_dim = 64` (2048/32) is acceptable here even though it would disqualify the model from ACTIVE/CRITICAL.
- 0.81 GB at Q4_K_M is the smallest viable Llama-arch instruction model with serious tuning behind it.
- MMLU 49.3 / IFEval 59.5 / GSM8K 44.4 puts it well below the larger tiers, which is fine — IDLE's job is *fast*, not *smart*.
- Llama 3.2 license is permissive enough for standalone deployment.
- Honorable mention: **Qwen3 1.7B** is a tempting upgrade — head_dim=128, Apache 2.0, and Qwen3 is reportedly stronger than equivalent-size Llama. But it's untested on this engine and ~30% larger. **Save it for a follow-up tier swap, not for the initial cut.** **OLMo 2 1B** is the only IDLE-class option that holds head_dim=128 — useful if you ever want a single model that can graduate up tiers without swapping.

### ACTIVE pick (`head_dim ≥ 128`) — `Qwen3 4B Instruct (2507)`

**Why this and not Llama 3.2 3B:**
- The honest read of the benchmark numbers: **Qwen3 4B beats Llama 3.2 3B everywhere they overlap.** MMLU-Pro 69.6 (Qwen3 4B) vs. MMLU 63.4 (Llama 3.2 3B) — and MMLU-Pro is the harder benchmark. IFEval 83.4 vs. 77.4. The Qwen team's claim that Qwen3 4B-Base matches Qwen2.5-7B-Base is consistent with what's in the model card.
- Architecture is **vanilla** — explicit `head_dim=128` in config.json, GQA (32 Q / 8 KV), 36 layers, RoPE, RMSNorm, SwiGLU. **No engine modifications required.** Confirmed on the model card and the Qwen3 transformers `configuration_qwen3.py` source.
- Apache 2.0 license — strictly cleaner for standalone deployment than Llama's license.
- 2.5 GB Q4_K_M sits squarely in the 2-4 GB tier window.
- Vocab is 151,936 (vs Llama's 128,256). That's a ~10% larger embedding table — costs ~50 MB extra at Q4_K_M, manageable.
- Risk: Qwen3 has a "thinking mode" toggle. The Instruct-2507 variant defaults to non-thinking (good for serving latency), but verify this is the variant whose GGUF you load — avoid the `Qwen3-4B-Thinking` checkpoint, which always emits reasoning traces.

**Runner-up: `Llama 3.2 3B Instruct`** — pick this instead if you want lineage continuity with the IDLE tier (same Meta tokenizer, same RoPE config, same general code paths). It's measurably weaker on hard benchmarks but the safest possible drop-in.

**Not picked: `Phi-4-mini`** — would beat both on MMLU (67.3 vs. 69.6 vs. 63.4) and especially GSM8K (88.6, the highest in the survey for any model under 5 GB), but the **fractional RoPE** is a hard architectural mismatch with the current engine. If the user wants Phi-4-mini, that's a separate engine-work conversation.

### CRITICAL pick (`head_dim ≥ 128`) — `Llama 3.1 8B Instruct`

**Why this and not Qwen3 8B or OLMo 2 7B or Ministral 3 8B:**
- **Most thoroughly benchmarked, most thoroughly verified.** Official Meta numbers in the model card give MMLU 69.4 / IFEval 80.4 / GSM8K 84.5 / HumanEval 72.6 / ARC-C 83.4. All five numbers are documented at the source — no guesswork.
- **Same family as IDLE pick** (Llama 3.x). Same tokenizer, same RoPE, same RMSNorm, same SwiGLU. If your engine works for Llama 3.2 1B, it works for Llama 3.1 8B with effectively zero risk. Tier transitions IDLE→CRITICAL stay within one architectural family.
- 4.92 GB at Q4_K_M is comfortably under the 8 GB ceiling, leaving headroom for KV cache + activations + an in-flight ACTIVE model during a tier swap.
- Llama 3.1 license is permissive enough for standalone deployment.
- **Honest weakness:** the model is from July 2024 — by April 2026 standards it is no longer the strongest 8B-class model. **Qwen3 8B is almost certainly higher quality** in absolute terms (Qwen team's own report says Qwen3 8B exceeds Qwen2.5-14B Base on most benchmarks), but I do not have verified MMLU/IFEval/GSM8K numbers from the Qwen3 8B model card itself, only the project blog.

**Runner-up: `Qwen3 8B Instruct`** — pick this instead if you accept the (small) risk of running on slightly less verified benchmark numbers and you want the latest-generation tuning. Same architecture as Qwen3 4B (vanilla, head_dim=128, GQA 32/8). Apache 2.0. 5.03 GB Q4_K_M. **If you go this route, Qwen3 4B as ACTIVE + Qwen3 8B as CRITICAL gives you a single tokenizer across the two big tiers, which is genuinely valuable** — your tokenizer code path stops being a tier-switching footgun.

**Other options considered:**
- **OLMo 2 7B Instruct** — Apache 2.0, fully open data + recipe, GSM8K 85.1 (highest in this size class). 4.47 GB. Vanilla architecture (no biases, RoPE, SwiGLU, RMSNorm). The HF card carries a Gemma-terms note because the instruction tuning used some Gemma outputs — read that before adoption. **Strong "ethics-first" pick if Apache + open data matters more than peak benchmarks.**
- **Ministral 3 8B Instruct (2512)** — newest of the four (Dec 2025), vanilla text backbone, 5.2 GB Q4_K_M, Apache 2.0. But the published HF card has no benchmark table — picking it means trusting Mistral's marketing copy, which I won't do without numbers.
- **Mistral 7B Instruct v0.3** — the rock-solid older option. 4.37 GB. Vanilla. 32K vocab is the smallest in the survey, which is a memory win on the embedding table. **Dropped from top pick because the newer 8B-class models are clearly better; kept as a recovery option.**
- **Mistral Nemo 12B** — fits the 8 GB ceiling at ~7.5 GB Q4_K_M, vanilla, Apache 2.0. **Stretch CRITICAL** if you're willing to live closer to the RAM limit.

---

## Architectural compatibility summary (the spec the user actually cares about)

| Model | head_dim | Quants | Vanilla? | Engine work? |
|---|---|---|---|---|
| Llama 3.2 1B | 64 | F16/Q4/Q8/Q4_K/Q6_K all available | Yes | None — already running |
| Llama 3.2 3B | **128** | all available | Yes | None |
| Llama 3.1 8B | **128** | all available | Yes | None |
| Qwen3 1.7B | **128** | all available | Yes | None (new tokenizer config to load) |
| Qwen3 4B | **128** | all available | Yes | None (new tokenizer config) |
| Qwen3 8B | **128** | all available | Yes | None (new tokenizer config) |
| OLMo 2 7B | 128 | all available | Yes (no biases is an *ablation* not an extension) | None expected; verify "no biases" config flag |
| OLMo 2 13B | 128 | all available | Yes | None |
| Mistral 7B v0.3 | **128** | all available | Yes | None |
| Mistral Nemo 12B | **128** | all available | Yes | None |
| Mistral Ministral 3 8B | **128** | all available | Yes (text backbone) | Filter vision-encoder tensors at load time |
| **Phi-4-mini** | 128 | all available | **Partial** — fractional RoPE | ~30-50 LOC RoPE patch |
| **SmolLM3 3B** | 128 | all available | **Partial** — NoPE/RoPE 3:1 hybrid | Per-layer RoPE flag + GGUF metadata parse |
| **Gemma 3 4B** | 256 | all available | **Partial** — sliding-window + global alternation | KV cache + attention mask rework |
| **Gemma 4 (any)** | varies | all available | **No** — PLE, shared KV cache, dual RoPE, sliding window | Substantial engine rewrite |
| **Qwen3.5 (any)** | varies | available | **No** — Gated DeltaNet hybrid (Mamba2-derived linear attention) | Hard mismatch, would require an SSM kernel |

---

## Section 7 — Gemma 4: Deferred Contender, Future Workstream

**Status:** Architecturally incompatible with the current JARVIS C inference engine. **Tracked as a separate workstream** on `feature/gemma4-arch` branch (TBD — branch not yet created). Will be re-evaluated for the ACTIVE / CRITICAL slot after the engine adapter work in §7.5 completes and passes the gates in §7.6.

This section exists because Gemma 4 is the most capable open model in its weight class as of April 2026, and "eliminated on architectural grounds" understates the strategic value of supporting it later. Documenting the scope here keeps the option live without polluting the §1-§3 bench-off.

### 7.1 Why Gemma 4 Matters

- Released **April 2, 2026** by Google DeepMind, under Apache 2.0 — five days before this survey was written. Same research lineage as Gemini 3.
- Four sizes: **E2B**, **E4B**, **26B-MoE (4B active)**, **31B-Dense**. Per the Google launch post and the LMArena leaderboard cited there, the **31B Dense ranks #3** and **26B-MoE ranks #6** among open models on the Arena AI text leaderboard at release; the smaller E2B and E4B variants are positioned as device-class with quality competitive with Gemma 3 12B.
- For JARVIS scaling tiers, only **E2B** and **E4B** are relevant: 26B-MoE needs MoE expert routing (not in the engine), and 31B-Dense at Q4_K_M ≈ 18 GB is far over the 8 GB single-model budget. **E2B fits IDLE/lower-ACTIVE; E4B fits ACTIVE.**
- The reason to track Gemma 4 specifically (and not the dozens of other "interesting" releases) is that several of its architecture features — sliding-window attention, shared KV cache — are also present in **Gemma 3 4B** and likely future Gemma releases, so the engine work compounds across the family. PLE and dual RoPE are more Gemma-4-specific.

### 7.2 Architectural Features (Why It Doesn't Drop In)

Each feature below is what the HF Gemma 4 blog and Google's Gemma 4 launch post explicitly call out as new. I have **not** yet read the Gemma 4 model card config.json or a reference implementation source — the descriptions below are the best summary I have without that primary-source confirmation.

#### 1. Per-Layer Embeddings (PLE)

- **What it is:** Beyond the standard token embedding table, Gemma 4 carries a *second* embedding table whose rows are added into the residual stream of every decoder layer. The HF Gemma 4 blog frames this as "feeding a small residual signal into every layer," intended to reinforce the token identity throughout depth without inflating the main embedding dim.
- **New tensors to load:** at minimum a `per_layer_embd.weight` (or whatever the GGUF tensor key turns out to be — must be verified against the actual GGUF file before coding).
- **Forward-pass change:** an additional `add_into_residual` op at the start of each layer's residual.
- **JARVIS files affected:**
  - **Loading:** `phase3/src/ai/llama_load.c` (F32 path), `phase3/src/ai/llama_quant.c::qmodel_load()` (production zero-copy path), `phase3/src/ai/llama_model.h` (config + qmodel structs need a new field).
  - **Forward:** `phase3/src/ai/llama_forward.c` (F32) and `phase3/src/ai/llama_quant.c::qmodel_forward()` (production). The production forward pass currently lives inside `llama_quant.c` — there is no separate `qmodel_tq_forward.c` file in the tree as of April 7 2026; if a TurboQuant variant is added later, that's where PLE handling would also need to live.
  - **Tensor add op:** the existing `tensor_add()` in `tensor_ops.c` already does element-wise addition; PLE doesn't need a new kernel, only a new call site.

#### 2. Shared KV Cache

- **What it is:** The last N decoder layers do not compute their own K/V projections; they reuse K/V tensors from an earlier specified layer. This is a memory-saving technique distinct from GQA (which shares K/V across heads within a layer); shared-KV-cache shares across layers.
- **State-struct change:** the model needs a `kv_share_map[n_layers]` array — for each layer, either a sentinel meaning "compute my own K/V" or an integer meaning "reuse layer #i's K/V."
- **Forward-pass change:** the per-layer Q·K and softmax(QK)·V steps need to look up K/V from `kv_share_map[layer]` instead of always indexing the current layer's slot.
- **JARVIS files affected:**
  - `phase3/src/ai/llama_model.h` — extend `llama_state_t` (or a new `gemma4_state_t`) with the share map. The KV cache allocation in `llama_alloc_state()` shrinks because shared layers don't need their own slots.
  - `phase3/src/ai/llama_forward.c` and `llama_quant.c` — KV cache addressing in the per-layer attention loop.
- **Risk:** The current production path stores K/V per layer per position with no aliasing. Adding aliasing means any future cache-compaction or KV-quantization work has to honor the share map. **This is the highest-impact change of the four** because it touches the data structure that every layer reads and writes.

#### 3. Dual RoPE Configurations

- **What it is:** Different RoPE theta (frequency base) values for different layer groups — typically a "global theta" for the global-attention layers and a smaller "local theta" for the sliding-window layers, since sliding-window layers only need positional disambiguation within a small window.
- **Forward-pass change:** RoPE precompute table is currently a single `(max_seq_len, head_dim/2)` cos/sin pair. Becomes either two such tables (one per group) or computed on-the-fly from a per-layer theta scalar.
- **JARVIS files affected:**
  - `phase3/src/ai/llama_quant.c` — the existing RoPE block in `qmodel_forward()` consults a single `rope_freqs` array. Add a per-layer theta lookup or a layer-group → freq-table mapping.
  - `phase3/src/ai/llama_forward.c` — same, F32 path.
  - `phase3/src/ai/llama_load.c` — load both theta values from GGUF metadata.

#### 4. Alternating Local-Sliding-Window / Global Attention

- **What it is:** Some layers attend only to the last N tokens (sliding window, e.g. 1024 for the smaller dense models per the HF Gemma 4 blog); others attend to all tokens (global). The layer pattern is fixed and known at load time.
- **Forward-pass change:** Per-layer attention mask logic. The QK·V product for sliding-window layers must mask out positions outside `[pos - window, pos]`. KV cache for sliding-window layers can also be smaller (only `min(pos+1, window)` slots) — this is where the memory savings actually come from.
- **JARVIS files affected:**
  - `phase3/src/ai/llama_model.h` — per-layer `attention_type` field (`ATTN_GLOBAL` / `ATTN_LOCAL_WINDOW`) and the window size.
  - `phase3/src/ai/llama_forward.c` and `llama_quant.c` — attention loop branches on the per-layer type. The KV cache iteration upper/lower bound becomes layer-dependent.
- **Note:** This is the same feature already needed for **Gemma 3 4B** (see §2 — sliding-window concern). The work here would directly unlock Gemma 3 4B as an ACTIVE tier alternative as well, so it's the highest-leverage change of the four.

### 7.3 Engineering Scope Estimate

> **Confidence:** **Low to medium.** These LOC numbers assume a clean implementation against a reference; they exclude unknown-unknowns from the actual GGUF tensor naming, edge cases in the attention mask code, and any debug iteration after the first run produces garbage. **Treat as ±50% bands, not commitments.** I have not read the Gemma 4 paper or reference implementation; the table is calibrated to comparable changes elsewhere in the JARVIS engine (e.g., the size of the existing RoPE block in `llama_quant.c`).

| Feature | Est LOC | Files Changed | New Tests | Notes |
|---|---|---|---|---|
| PLE loading + forward residual add | ~150 | `llama_load.c`, `llama_quant.c`, `llama_model.h`, `gguf_parser.c` (3-4) | 2-3 | Smallest, most isolated. Good first commit on the branch. |
| Shared KV cache | ~250 | `llama_model.h`, `llama_forward.c`, `llama_quant.c`, optionally `tensor_ops.h` (4-5) | 4-5 | Largest blast radius. KV cache is the data structure every layer reads/writes. |
| Dual RoPE | ~80 | `llama_quant.c`, `llama_forward.c`, `llama_load.c` (2-3) | 2 | Localized to RoPE blocks. Easy if you already have per-layer state. |
| Sliding-window attention | ~200 | `llama_model.h`, `llama_quant.c`, `llama_forward.c` (2-3) | 3-4 | **Doubles as the Gemma 3 4B unlock.** Has interesting interaction with shared KV (sliding-window layers may share K/V with each other, not just with global layers). |
| GGUF metadata key handling (`gemma4.*`) | ~50 | `gguf_parser.c` or `llama_load.c` (1) | 1 | Mostly switch-statement plumbing for new key strings. |
| Integration + debugging buffer | ~200 | — | — | First-run-after-implementation always reveals gaps. Reserve this. |
| **Total** | **~900-1000 LOC** | **5-7 files** | **12-15 tests** | **±50% confidence band — treat as a planning estimate, not a commitment.** |

**Estimated wall-clock effort:** 2-3 weeks of focused work, matching the original Phase 3c plan rhythm. **Caveat:** the 2-3 week figure is a planning anchor, not a measurement. Actual time depends heavily on how cleanly the Gemma 4 GGUF reflects the architectural features (clean per-layer config arrays vs. inferred-from-tensor-naming).

### 7.4 Why Defer (Not Skip)

1. **The bench-off has a working candidate set already.** Llama 3.2 1B for IDLE, Qwen3 4B for ACTIVE, Llama 3.1 8B for CRITICAL. None of these blocks Gemma 4 work; the bench-off can run, ship, and be re-run later.
2. **Master branch just completed a security audit.** Two rounds of adversarial audit (March and April 2026) closed 25 findings. Adding ~1000 LOC of new attention/KV cache logic immediately reopens the audit surface — the new code paths would need their own fuzz coverage and bounds review before they go anywhere near the production rootserver.
3. **High-value but high-risk.** Gemma 4 quality is real, but the engine adapter touches the hottest data structure in the inference path (KV cache). A bug there is hard to debug on bare metal. Deserves an isolated branch with thorough testing before any merge consideration.
4. **The investment compounds.** Sliding-window attention alone unlocks **Gemma 3 4B** as an ACTIVE alternative. Shared KV cache is a technique increasingly common in 2026-era models — supporting it once pays off across the family. PLE and dual RoPE are more Gemma-4-specific, so those are pure Gemma-4 bets.
5. **Better to have one strong baseline first.** If the Llama 3.2 1B / Qwen3 4B / Llama 3.1 8B stack runs the 30-day stability test cleanly, that gives a known-good comparator for the Gemma 4 work to be measured against. Doing both in parallel risks confounding.

### 7.5 Workstream Plan

```
Phase 3c Bench-Off (current path, on master):
  1. Compatibility check Qwen3 4B + Llama 3.1 8B            ← next step
  2. Build bench-off harness (load model → run prompts → log tok/s + quality)
  3. Run baseline bench-off across IDLE / ACTIVE / CRITICAL picks
  4. Pick winners for ACTIVE / CRITICAL slots
  5. Wire winners into model_scaling.c state machine
  6. Run 30-day stability test against the chosen tiers

Gemma 4 Workstream (parallel, on feature/gemma4-arch branch — branch not yet created):
  1. Create branch feature/gemma4-arch from current master
  2. Read Gemma 4 paper / model card / HF transformers reference (gemma4 modeling source)
  3. Verify GGUF tensor naming for PLE / shared-KV / dual-RoPE / sliding-window flags
  4. Implement PLE (smallest, most isolated change)        ← first commit
  5. Implement dual RoPE (small, isolated)
  6. Implement sliding window attention                    ← unlocks Gemma 3 4B as side effect
  7. Implement shared KV cache (largest change, depends on layer-type framework from #6)
  8. Add GGUF metadata key handling for gemma4.* keys
  9. Test against Gemma 4 E2B (smallest variant — fastest iteration loop)
  10. Run fuzz harness against new code paths (extend phase3/src/drivers/fuzz_harness.c)
  11. Run bench-off harness against E2B and E4B
  12. Compare to Qwen3 4B baseline (same tier slot)
  13. Decision: merge to master (if quality+stability win) or keep on branch (if not)
```

### 7.6 Re-Evaluation Trigger

Re-run the bench-off including Gemma 4 E2B and E4B **only when all of the following are true** on `feature/gemma4-arch`:

1. All four architectural features (PLE, shared KV, dual RoPE, sliding-window) compile cleanly with `-Wall -Werror -O2 -std=c11`.
2. New unit tests for each feature pass (target: 12-15 new tests per §7.3).
3. Forward pass against Gemma 4 E2B produces **non-degenerate output**: greedy generation on a known prompt produces coherent natural-language text (not garbage tokens or repetition loops). This is the same gate that was used for the original Llama 3.2 1B bring-up on bare metal.
4. Logits sanity check: top-5 logits on a fixed prompt are within tolerance of a reference implementation (e.g., `transformers` Python or `llama.cpp` once it has Gemma 4 support — verify llama.cpp Gemma 4 support landed before relying on it as a reference).
5. Fuzz harness (`phase3/src/drivers/fuzz_harness.c`) extended to cover the new code paths (especially the shared-KV addressing and the sliding-window mask) and clean across 100K+ iterations under ASAN+UBSAN.
6. **No regression** on existing models: the Llama 3.2 1B and Llama 3.1 8B paths still load, run, and produce identical outputs on the existing test suite. This is the non-negotiable gate — Gemma 4 work cannot break the existing winners.

If any of the above is not true, the bench-off does not include Gemma 4 in the comparator set; the workstream stays on its branch and gets another iteration.

---

**Stop point reached for Section 7. The feature/gemma4-arch branch is NOT yet created — that's a future task per the user's instructions.**

---

## Sources (used for §1 specs and §2/§3 reasoning)

### Hugging Face model cards (architecture + benchmarks)
- [meta-llama/Llama-3.2-1B-Instruct](https://huggingface.co/meta-llama/Llama-3.2-1B-Instruct)
- [meta-llama/Llama-3.2-3B-Instruct](https://huggingface.co/meta-llama/Llama-3.2-3B-Instruct)
- [meta-llama/Llama-3.1-8B-Instruct](https://huggingface.co/meta-llama/Llama-3.1-8B-Instruct)
- [Qwen/Qwen3-4B-Instruct-2507](https://huggingface.co/Qwen/Qwen3-4B-Instruct-2507)
- [Qwen/Qwen3-8B](https://huggingface.co/Qwen/Qwen3-8B)
- [microsoft/Phi-4-mini-instruct](https://huggingface.co/microsoft/Phi-4-mini-instruct)
- [HuggingFaceTB/SmolLM3-3B](https://huggingface.co/HuggingFaceTB/SmolLM3-3B)
- [google/gemma-3-4b-it](https://huggingface.co/google/gemma-3-4b-it)
- [allenai/OLMo-2-1124-7B-Instruct](https://huggingface.co/allenai/OLMo-2-1124-7B-Instruct)
- [mistralai/Ministral-3-8B-Instruct-2512](https://huggingface.co/mistralai/Ministral-3-8B-Instruct-2512)

### GGUF availability (Q4_K_M file size verification)
- [bartowski/Llama-3.2-1B-Instruct-GGUF](https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF)
- [bartowski/Llama-3.2-3B-Instruct-GGUF](https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF)
- [bartowski/Meta-Llama-3.1-8B-Instruct-GGUF](https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF)
- [Qwen/Qwen3-4B-GGUF](https://huggingface.co/Qwen/Qwen3-4B-GGUF)
- [Qwen/Qwen3-8B-GGUF](https://huggingface.co/Qwen/Qwen3-8B-GGUF)
- [bartowski/microsoft_Phi-4-mini-instruct-GGUF](https://huggingface.co/bartowski/microsoft_Phi-4-mini-instruct-GGUF)
- [ggml-org/SmolLM3-3B-GGUF](https://huggingface.co/ggml-org/SmolLM3-3B-GGUF)
- [bartowski/google_gemma-3-4b-it-GGUF](https://huggingface.co/bartowski/google_gemma-3-4b-it-GGUF)
- [bartowski/OLMo-2-1124-7B-Instruct-GGUF](https://huggingface.co/bartowski/OLMo-2-1124-7B-Instruct-GGUF)
- [lmstudio-community/Ministral-3-8B-Instruct-2512-GGUF](https://huggingface.co/lmstudio-community/Ministral-3-8B-Instruct-2512-GGUF)

### Architecture deep-dives & technical reports
- [Phi-4-Mini Technical Report (arxiv.org/html/2503.01743)](https://arxiv.org/html/2503.01743v1) — fractional RoPE confirmed
- [Hugging Face Transformers — Qwen3 model docs](https://huggingface.co/docs/transformers/model_doc/qwen3) — explicit head_dim=128
- [Hugging Face Transformers — Gemma 3 model docs](https://huggingface.co/docs/transformers/en/model_doc/gemma3) — alternating sliding-window attention
- [Hugging Face Transformers — SmolLM3 model docs](https://huggingface.co/docs/transformers/main/en/model_doc/smollm3) — 3:1 NoPE ratio
- [Hugging Face Transformers — OLMo2](https://huggingface.co/docs/transformers/en/model_doc/olmo2) — no biases, vanilla otherwise
- [Welcome Gemma 4 (HuggingFace blog)](https://huggingface.co/blog/gemma4) — PLE, shared KV cache, dual RoPE
- [Gemma 4: Byte for byte (Google blog)](https://blog.google/innovation-and-ai/technology/developers-tools/gemma-4/)
- [Qwen3 Technical Report (arxiv.org/html/2505.09388)](https://arxiv.org/html/2505.09388v1)
- [Qwen3.5 architecture analysis (gist)](https://gist.github.com/justinchuby/0213aa253664fb72e9adb0089816de15) — Gated DeltaNet hybrid confirmed

### Specs & RAM requirements
- [apxml.com — Llama 3.2 1B](https://apxml.com/models/llama-3-2-1b)
- [apxml.com — Llama 3.2 3B](https://apxml.com/models/llama-3-2-3b)
- [apxml.com — Phi-4-mini](https://apxml.com/models/phi-4-mini)
- [apxml.com — SmolLM3 3B](https://apxml.com/models/smollm3-3b)
- [apxml.com — Gemma 3 4B](https://apxml.com/models/gemma-3-4b)
- [Sebastian Raschka — The Big LLM Architecture Comparison](https://magazine.sebastianraschka.com/p/the-big-llm-architecture-comparison)

### Release announcements (release dates, license)
- [Meta — Llama 3.2 launch](https://www.llama.com/)
- [Llama 4 launch (interconnects.ai)](https://www.interconnects.ai/p/llama-4)
- [Qwen3 blog (qwenlm.github.io)](https://qwenlm.github.io/blog/qwen3/)
- [Welcome Gemma 3 (Hugging Face blog)](https://huggingface.co/blog/gemma3)
- [Mistral 3 announcement (mistral.ai)](https://mistral.ai/news/mistral-3)
- [Mistral 3 — TechCrunch coverage](https://techcrunch.com/2025/12/02/mistral-closes-in-on-big-ai-rivals-with-mistral-3-open-weight-frontier-and-small-models/)
- [SmolLM3 (HuggingFace blog)](https://huggingface.co/blog/smollm3)
- [OLMo 2 release notes (allenai.org)](https://allenai.org/olmo/release-notes)
- [Olmo 3 announcement (allenai.org)](https://allenai.org/blog/olmo3)
- [Best Small Language Models 2026 (localaimaster.com)](https://localaimaster.com/blog/small-language-models-guide-2026)
- [Small Language Model Leaderboard (awesomeagents.ai)](https://awesomeagents.ai/leaderboards/small-language-model-leaderboard/)

### §7 Gemma 4 deferred-workstream sources
- [Welcome Gemma 4 (Hugging Face blog)](https://huggingface.co/blog/gemma4) — primary source for PLE, shared KV cache, dual RoPE, sliding-window descriptions
- [Gemma 4: Byte for byte, the most capable open models (Google blog)](https://blog.google/innovation-and-ai/technology/developers-tools/gemma-4/) — release date (Apr 2 2026), four-size lineup, Arena leaderboard rankings (#3 / #6)
- [A Visual Guide to Gemma 4 (Maarten Grootendorst)](https://newsletter.maartengrootendorst.com/p/a-visual-guide-to-gemma-4) — architecture diagrams
- [Gemma 4 and what makes an open model succeed (Interconnects)](https://www.interconnects.ai/p/gemma-4-and-what-makes-an-open-model) — strategic context
- [Gemma 4 model overview (ai.google.dev)](https://ai.google.dev/gemma/docs/core) — official model overview
- [Gemma 4 31B and 26B A4B: Architecture and Memory Consumption (kaitchup)](https://kaitchup.substack.com/p/gemma-4-31b-and-26b-a4b-architecture) — sizing/memory analysis
