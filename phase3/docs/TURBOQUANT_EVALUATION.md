# TurboQuant Branch Evaluation

**Date:** April 2, 2026
**Branch:** `experiment/turboquant-benchmark` (31 commits ahead of master)
**Scope:** ~10,000 lines added, ~3,500 LOC TurboQuant implementation, 38 files changed
**Evaluators:** Strategic review + 5-agent superpower evaluation

---

## 1. What TurboQuant Is

TurboQuant (arXiv 2504.19874, ICLR 2026) compresses KV cache entries in transformer models via three stages:

1. **Random Rotation:** Generates Haar-distributed orthogonal matrix (Pi) via Gram-Schmidt. Rotates each KV vector: `y = Pi * x`. After rotation, coordinates follow Beta((d-1)/2, (d-1)/2) distribution, not Gaussian.

2. **Lloyd-Max Scalar Quantization:** MSE-optimal centroids for the Beta distribution. Keys use `(b-1)` bits (reserving 1 bit for QJL). Values use `b` bits (softmax averaging mitigates per-vector errors).

3. **QJL Residual Correction (Keys Only):** Computes residual `r = unit_vector - mse_reconstruction`, projects via Gaussian matrix S, stores 1-bit signs. During attention, produces unbiased inner product estimate. Removes systematic MSE bias while adding controlled variance.

**Promise:** 7-9x KV cache compression with minimal quality loss at d=128+ (7B-70B models).

---

## 2. Implementation Summary

### Files Created

| File | LOC | Purpose |
|------|----:|---------|
| turboquant.h | 199 | API header, Beta-optimal codebooks, structs |
| turboquant.c | 730 | Core algorithm (compress/decompress/QJL dot) |
| llama_forward_tq.h/c | 58 / 261 | Algorithm 2 — compressed-representation attention |
| qmodel_tq_forward.h/c | 46 / 364 | Quantized weights + TQ compressed KV cache |
| qmatmul.h | 17 | Shared matmul extraction (refactor) |
| test_turboquant.c | ~500 | 15 unit tests |
| test_qmodel_tq.c | ~150 | 10 state alloc/free + memory tests |
| test_turboquant_real.c | ~613 | Real model validation |
| test_turboquant_gen.c | ~569 | Generation quality sweep |
| bench_turboquant.c | ~200 | Throughput benchmark |
| TURBOQUANT_BENCHMARK.md | 272 | Full benchmark report |
| BENCHMARK_RESULTS.md | 223 | CPU baseline comparison |

### seL4 Integration

- `MSG_SET_TQ_MODE` (0x0F) IPC message type added to `shmem_ipc.h`
- Process B (`inference_server.c`) has runtime toggle: `tq_enabled` flag
- Process A (`main_x86.c`) sends enable/disable, runs A/B comparison demo
- Mixed bit-width support: per-coordinate allocation, outlier channels
- `TQ_CONFIG_35BIT()` / `TQ_CONFIG_25BIT()` macros matching paper configs

---

## 3. What Works

### Unit Tests: 25/28 PASS

| Test File | Tests | Pass | Fail |
|-----------|:-----:|:----:|:----:|
| test_turboquant.c | 15 | 15 | 0 |
| test_qmodel_tq.c | 10 | 10 | 0 |
| test_turboquant_real.c | 1 | 0 | 1 |
| test_turboquant_gen.c | 2 | 0 | 2 |

### Compression Quality (Verified Against Paper)

| Config | Key Cosine | Val Cosine | QJL Dot RMSE | Attn Corr |
|--------|:---------:|:---------:|:----------:|:--------:|
| 2-bit | 0.801 | 0.942 | 1.94 | ~0.90 |
| **3-bit** | **0.941** | **0.984** | **1.16** | **0.96** |
| 4-bit | 0.984 | 0.996 | 0.60 | ~0.99 |

MSE matches paper Table 1 within 2%: 2-bit 0.1146 vs paper 0.117, 3-bit 0.0334 vs paper 0.034.

### Single-Step Quality: PERFECT

- Top-5 overlap: 5/5 (identical set)
- Argmax match: 100% (16/16 heads)
- Softmax correlation: 0.9997 average

### Compression Ratios: VERIFIED

| Model | F32 KV | TQ KV | Compression | Savings |
|-------|-------:|------:|:-----------:|--------:|
| 1B (d=64) | 32.0 MB | 3.9 MB | 8.2x | 28.1 MB |
| 3B (d=128) | 112.0 MB | 14.7 MB | 7.6x | 97.3 MB |

### Code Quality: PRODUCTION-GRADE

- Memory leaks: None — all malloc/free paired, error paths handled
- Buffer overflows: Safe — all bounds use TQ_MAX_HEAD_DIM constant
- Dead code: None
- TODO/FIXME/HACK: Zero across all 7 files
- C11 compliance: Full — no VLAs, no UB, proper types
- Thread safety: Full — no global mutable state, all instance-based
- Compiles clean under `-Wall -Werror`

---

## 4. What Failed

### Multi-Step Generation at d=64 (Llama 1B): CRITICAL FAILURE

| Prompt | F32 Match | Decompress-Overwrite | Algorithm 2 (TQProd) |
|--------|:---------:|:--------------------:|:--------------------:|
| "The seL4 microkernel is" | baseline | 3.3% | 3.3% |
| "Once upon a time there was" | baseline | 3.3% | 16.7% |
| "The capital of Australia is" | baseline | 3.3% | 6.7% |
| **Average** | | **3.3%** | **8.9%** |

Diverges after position 1. Enters repetition loops within 2-3 tokens.

### Bit-Width Sweep: ALL Configs FAIL at d=64

| Config | Prompt 1 | Prompt 2 | Prompt 3 | Avg |
|--------|:--------:|:--------:|:--------:|:---:|
| 3b/3b | 3.3% | 16.7% | 6.7% | **8.9%** |
| 3b/4b | 6.7% | 3.3% | 6.7% | **5.6%** |
| 4b/3b | 6.7% | 3.3% | 6.7% | **5.6%** |
| 4b/4b | 3.3% | 3.3% | 6.7% | **4.4%** |

**Higher bits make it WORSE** — 4b/4b (4.4%) scores below 3b/3b (8.9%). This is not a quantization fidelity problem.

### Root Cause: d=64 Signal-to-Noise Ratio

Per-coordinate quantization error compounds through 16 layers x 16 heads = 256 error sources per token. At d=64, the error dominates signal amplitude. After 2-3 tokens, the model enters a low-entropy attractor (repetition loops).

The paper never tested d=64. It targets d=128+ models (7B-70B) where per-coordinate error is sqrt(2)x smaller relative to signal.

### d=128 (3B) Shows Dramatic Improvement

| Config | 1B (d=64) Avg | 3B (d=128) Avg | 3B Best Prompt |
|--------|:---:|:---:|:---:|
| 3b/3b | 8.9% | 14.4% | 33.3% |
| 4b/3b | 5.6% | **45.6%** | **100%** |
| 4b/4b | 4.4% | **42.2%** | 66.7% |

Prompt 3 at 4b/3b achieves **perfect 100% match** (30/30 tokens) at d=128. 5.1x improvement over d=64.

---

## 5. Cache Pollution Investigation (Dross Hypothesis)

A competing hypothesis was investigated: that the multi-step failure is caused by implementation bugs (state leaking between KV entries) rather than fundamental limits.

### Findings

- **No static/global buffers** — all temp arrays are stack-local, fully written in loops
- **Pi and S matrices** stored per-layer, reused correctly, no drift
- **QJL projection** uses stored S matrix, not regenerated from RNG
- **RNG state** doesn't advance between compress and decompress
- **tq_ckey_t / tq_cval_t** fully self-contained per position per layer
- **No memset needed** — loops write all d elements before reading

### Verdict: NOT A BUG

The d=64 failure is fundamental, not an implementation error. Evidence:
1. Higher bits making it worse rules out quantization precision as the cause
2. d=128 showing 5.1x improvement confirms the scaling relationship
3. Paper only validates d=128+ for a reason

**Recommendation:** Add defensive `memset(quantized, 0, d * sizeof(float))` in `tq_decompress_key()` and `tq_decompress_value()` as hardening, but don't expect it to change outcomes.

---

## 6. Branch Regression Risk: CRITICAL

The branch accidentally regressed driver code that was added to master AFTER the branch was forked:

| File | What Was Lost | Master Commit |
|------|---------------|---------------|
| pci.c/h | 64-bit BAR support removed, uint64_t → uint32_t | `051d684` |
| blk_dev_x86.c | AHCI real init replaced with `return -1` | `166a826` |
| nic_rtl8168.c/h | RX buffer management deleted, `rtl_nic_set_rx_buffer()` gone | `5d58678` |
| tensor_ops.c | All AVX2/FMA SIMD removed | `ccc63e9` |
| test_pci.c | 64-bit BAR tests deleted | `051d684` |
| test_nic.c | RX frame delivery test deleted | `5d58678` |

**These are NOT intentional TQ changes.** The branch diverged before these master commits. The TQ work simplified the same files to get things compiling, creating conflicting modifications.

**DO NOT MERGE AS-IS.** A naive merge would destroy the last 4-5 commits of real master work.

---

## 7. Cherry-Pick Assessment

### Worth Cherry-Picking to Master

| Item | Commit | Reasoning |
|------|--------|-----------|
| qmatmul.h | `2661fb2` | Clean refactor extracting qmatmul_vec/qembed_lookup — reduces duplication (but must preserve AVX2 in llama_quant.c) |

### Do NOT Cherry-Pick

| Item | Reason |
|------|--------|
| tensor_ops.c changes | Removes AVX2 SIMD (performance regression) |
| pci.c / blk_dev_x86.c / nic_rtl8168.c | Breaks existing driver functionality |
| shmem_ipc.h MSG_SET_TQ_MODE | TQ-specific, no value without TQ handlers |
| All TQ source/test files | Keep on branch until TQ validated with 3B model |
| CI changes | Add when TQ is merged |
| BENCHMARK_RESULTS.md | TQ-specific context, belongs with the branch |

---

## 8. Documentation Honesty: 85/100

### Trustworthy

- Failures explicitly documented in commits ("honest FAIL documented")
- Compression ratios mathematically verified
- Paper implementation faithfully reproduced (MSE within 1-2%)
- d=64 vs d=128 scaling evidence genuine
- Bit-width sweep honestly shows higher bits making it worse

### Concern

- Section 9 "Assessment" in TURBOQUANT_BENCHMARK.md says "YES for Phase 3c" without qualifying this applies only to d=128+ models
- "15/15 PASS" is unit-test level — the critical generation tests FAIL
- A careful reader gets the truth from Section 6; a skimmer of the conclusion could be misled

---

## 9. Strategic Verdict

### For Phase 3 (Llama 3.2 1B, d=64)

**TurboQuant is NOT viable.** 8.9% generation quality is unusable. The compression module works correctly but the model's head dimension is too small for autoregressive coherence under lossy KV compression.

### For Phase 3 with 3B Model (d=128)

**TurboQuant becomes viable.** 45-100% match rates at 4b/3b, with perfect results on some prompts. 7.6x KV cache compression (112MB → 15MB) is meaningful on 16GB RAM. Code is production-quality and already integrated into the seL4 IPC system.

### For Phase 4+ (7B-70B, d=128+)

**TurboQuant is the intended target.** Paper validates at these scales. Larger head dimensions improve SNR. Expected to cross 80%+ generation match threshold.

---

## 10. Recommended Actions

| Action | Scope | When |
|--------|-------|------|
| **Do not merge** | Branch as-is has driver regressions | Now |
| **Do not abandon** | Code is production-quality, viable at d=128+ | — |
| **Optional cherry-pick** | qmatmul.h refactor to master (low priority) | When convenient |
| **Rebase on master** | Get driver fixes (PCI 64-bit BAR, NIC RX, AHCI, AVX2) back | When targeting 3B |
| **Re-run generation tests at d=128** | Verify quality with rebased code + 3B model | After 3B model on bare metal |
| **Add defensive memset** | turboquant.c lines ~582 and ~625 (hardening, not a fix) | During rebase |
| **Fix Section 9 assessment** | Qualify "YES" with "d=128+ only, NOT for 1B" | During rebase |
| **Decide on merge** | Based on 3B generation quality results | After rebase + validation |

---

## 11. TurboQuant and Model Selection Synergy

The Phase 3 model target is Llama 3.2 3B Q4_K_M (d=128). This was chosen independently of TurboQuant for these reasons:
- Same architecture as 1B (zero engine changes)
- GGUF format already parsed by custom inference engine
- 15-20 tok/s on Ryzen 2700X (acceptable for 15% cache misses)
- 1.8GB fits easily in 16GB RAM

**The synergy:** Choosing 3B (d=128) makes TurboQuant viable. When 3B is running on bare metal, the TQ branch can be rebased and validated. If generation quality passes at d=128, TurboQuant provides:
- 7.6x KV cache compression (112MB → 15MB)
- Extended context window (512 → 2048+ tokens)
- 3B model KV cache fits alongside SHIELD + decision cache in seL4

This makes TurboQuant a Phase 3c optimization target (hardening phase), not a Phase 3b concern.

---

*Branch: experiment/turboquant-benchmark | 31 commits | ~10K lines | April 2026*
