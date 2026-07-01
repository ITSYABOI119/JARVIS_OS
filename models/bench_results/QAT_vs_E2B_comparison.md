# Gemma 4 E2B — QAT (UD-Q4_K_XL) vs deployed Q4_K_M — factual comparison

**Data only. No keep-or-swap recommendation (strategist's call).**

Bench host: **Main PC** — AMD Ryzen 5 5600 (6c/12t) + NVIDIA RTX 2070 (8 GB, driver 596.49), 16 GB RAM, WSL gcc 13.3.0.
Date: 2026-07-01. **The JARVIS PC / seL4 box was never touched** (no ssh, no `~/sel4-x86`, no box bench).

Two engines used:
- **Our engine** = `bench_engine`-style native build of the JARVIS quantized path (`qmodel_forward`, `-mavx2 -mfma -DJARVIS_PTHREAD`). This is the deploy-relevant engine.
- **llama.cpp** = local CUDA build `5e9c63546 (8728)` at `~/llama.cpp/build/bin/Release` (reference cross-check).

---

## Headline comparison table

| variant | file | size (GiB / bytes) | loads on **our engine** | **coherent** on our engine | our-engine tok/s (tg128) 1T / 6T / 12T | llama.cpp tok/s (tg128) CPU / GPU | WikiText-2 PPL |
|---|---|---|:---:|:---:|---|---|---|
| **BASELINE** (deployed PTQ) | `gemma-4-E2B-it-Q4_K_M.gguf` | 2.893 / 3,106,736,256 | ✅ yes | ✅ **yes** | 2.93 / 6.24 / 5.77 | 20.01 / **71.88** | not run¹ |
| **QAT** (Unsloth) | `gemma-4-E2B-it-qat-UD-Q4_K_XL.gguf` | 2.440 / 2,620,368,960 | ✅ yes (passes H2 whitelist) | ❌ **NO — incoherent garbage** | 2.25 / 7.09 / 7.27 ² | ❌ **fails to load** ³ | n/a³ |

¹ Perplexity not run — see "Perplexity" below (no comparative signal available; Gemma-4 PPL on WikiText-2 is known-invalid, and the QAT can't be measured in llama.cpp).
² QAT tok/s is **measured but moot**: the model generates incoherent output on our engine (see "Quality"), so its speed does not represent usable inference.
³ llama.cpp build 8728 aborts loading the QAT with `error loading model: missing tensor 'blk.15.attn_k.weight'` (it does not handle this file's shared-KV tensor layout, where layers 15–34 omit their own K/V and share from L13/L14).

---

## Load & coherence gate (the decisive result)

| | our engine — loads | our engine — coherent output | llama.cpp — loads |
|---|:---:|:---:|:---:|
| Baseline Q4_K_M | ✅ | ✅ | ✅ |
| QAT UD-Q4_K_XL | ✅ | ❌ (garbage) | ❌ |

- **"Loads" ≠ "works."** The QAT passes our load-time H2 quant-type whitelist (all its tensors are Q4_0, which is allowed), loads in 0.2 s, and reports the correct config (arch=gemma4, 35 layers, PLE=256, SWA=512, shared_kv=20). But every one of the 10 prompts produces incoherent output: repeated `<turn|>` tokens, CJK characters, and punctuation spam. The baseline, run through the **identical harness**, is coherent and high quality.
- **llama.cpp cannot load the QAT at all** on this build (missing shared-KV tensor), so the llama.cpp speed/quality/PPL scripts can only run the baseline.

### Root-cause evidence (tensor-type diff)

The two files differ in how the Gemma-4 **Per-Layer-Embedding (PLE)** tensors and weights are quantized (from our engine's load-time AUDIT):

| tensor | Baseline Q4_K_M | QAT UD-Q4_K_XL |
|---|---|---|
| attn/ffn weights (wq/wk/wv/wo, gate/up/down) | Q4_K / Q6_K | **Q4_0** |
| `inp_gate` (PLE gate) | **F32** | **Q4_0** (engine audit prints "F32 expected") |
| `per_layer_model_proj` (PLE context proj) | **BF16** | **Q4_0** |
| `token_embd` / `output` | Q4_K | Q4_0 |

`inp_gate` is consumed via `qmatmul_vec` (which dequantizes by recorded type), so this is not a trivial raw-F32 misread; the exact numerical mechanism (precision loss vs. a PLE code-path assumption) was **not** isolated — that would require engine debugging, which is out of scope for this data-gathering task. **Observed fact:** the current JARVIS engine does not produce coherent output from this all-Q4_0 UD-Q4_K_XL variant.

---

## Speed detail

### Our engine (JARVIS `qmodel_forward`, Ryzen 5 5600) — tg128 tok/s, greedy
| threads | Baseline Q4_K_M | QAT UD-Q4_K_XL |
|---|---|---|
| 1T | **2.93** | 2.25 |
| 6T (mirrors deployed `NUM_NODES=6`) | 6.24 | **7.09** |
| 12T (all cores) | 5.77 | **7.27** |

Notes: the smaller QAT is slower at 1T but faster multi-threaded (less memory-bandwidth-bound). Baseline peaks at 6T (6.24) and regresses at 12T (5.77) — bandwidth/SMT contention on the 6-core part. **The QAT numbers describe generating garbage.** (The `bench_engine` table's "threads" column is a hardcoded literal `1`; real thread counts set via `JARVIS_THREADS`.)

### llama.cpp (Ryzen 5 5600 + RTX 2070), build 8728 — pp512 / tg128
| | Baseline Q4_K_M | QAT UD-Q4_K_XL |
|---|---|---|
| CPU (`-ngl 0`) | 1528.73 / **20.01** | fails to load |
| GPU (`-ngl 99`) | 3500.26 / **71.88** | fails to load |

llama.cpp identifies the baseline as `gemma4 E2B Q4_K - Medium` (params reported as 4.65 B by llama.cpp's counting vs 2.71 B by ours — cosmetic).

---

## Quality (10 prompts, greedy temp=0, 100 tok, our engine)

Full transcripts: `models/quality_results/qat_vs_e2b_ALL_RESPONSES.txt`.
- **Baseline:** coherent, well-structured answers across all 10 prompts (factual, code, OS-domain).
- **QAT:** incoherent on all 10 prompts (token/character spam). Not judgeable as language output.

Because llama.cpp won't load the QAT, both models were generated on **our engine** for an apples-to-apples, deploy-relevant comparison (rather than baseline-on-llama.cpp vs QAT-on-our-engine, which would be cross-engine).

---

## Perplexity — not run (documented)

- The QAT **cannot be measured** in llama.cpp (won't load), so there is no QAT PPL to compare against.
- Gemma-4 PPL on WikiText-2 is **known-invalid** from the prior bench-off (`MODEL_BENCH_OFF_2026-04-07.md`: E2B 176.30, E4B 63.20 — tokenizer/architecture mismatch), so a baseline-only number carries no comparative signal.
- A baseline-only WikiText-2 PPL on this llama.cpp build can be produced on request, but it would be a lone, invalid-for-Gemma number.

---

## Files produced (this session)

- `models/bench_results/jarvis_engine_DESKTOPJ.txt` — canonical engine bench (both models, default/12T).
- `models/bench_results/jarvis_engine_threadsweep_mainpc.txt` — explicit 1T/6T/12T sweep, both models.
- `models/bench_results/qat_vs_e2b_llamacpp_cpu.txt` / `_gpu.txt` — llama.cpp speed (baseline runs; QAT load-failure captured).
- `models/quality_results/qat_vs_e2b_ALL_RESPONSES.txt` — 10-prompt responses, both models, our engine.
- `models/bench_results/QAT_vs_E2B_comparison.md` — this file.

Historical 11-model bench-off files (`ALL_RESPONSES.txt`, `perplexity_results.txt`, per-model quality txts, `bench_results_mainpc_gpu.txt`, `FINAL_SCORES.txt`) were **not** modified — new artifacts use QAT-distinct names.

## Possible next diagnostic (strategist's call — NOT done here)

The task's fallback (`google/gemma-4-E2B-it-qat-q4_0-gguf`, official pure-Q4_0) was gated on the QAT *failing to load*; it *loaded* (but produced garbage), so it was not fetched. If QAT is worth pursuing, that official variant is the next thing to try — with the caveat that it is also pure Q4_0 and may hit the same PLE-tensor incompatibility on our engine.
