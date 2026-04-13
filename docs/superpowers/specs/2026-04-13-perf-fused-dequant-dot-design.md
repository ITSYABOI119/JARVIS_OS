# Single-Threaded Performance Optimization — Fused Dequant-Dot + SIMD Attention

**Date:** 2026-04-13
**Status:** Approved
**Target:** Llama 1B Q4_K_M: 1.0 tok/s -> 4-5 tok/s single-threaded (Ryzen 2700X)
**Scope:** Phase 3, Week 35 — all work in `phase3/src/ai/`, no seL4 kernel changes

---

## 1. Problem

The JARVIS engine's quantized inference path (`qmatmul_vec` in `llama_quant.c`) runs
at 1.0 tok/s for Llama 1B on Ryzen 2700X — 40x slower than llama.cpp (8 threads).
Even accounting for the 8x thread difference, single-threaded performance is 5x below
what the hardware can deliver.

**Root cause:** The current matmul inner loop dequantizes one block (256 floats) into a
stack buffer `float tmp[256]`, then performs an AVX2 FMA dot product on that buffer.
This means:

1. **3x memory traffic per block:** Read 144B quantized -> write 1024B F32 tmp -> read 1024B F32 tmp for dot
2. **Scalar dequant:** Nibble unpacking is byte-at-a-time C loops (~500 cycles/block)
3. **Function call overhead:** `dequant_row()` dispatches through a switch per block (8 calls/row for K=2048)
4. **Compute utilization:** ~3.9% of single-core peak (2.47 GFLOP/s of 64 GFLOP/s theoretical)

## 2. Solution

Two optimizations that compose multiplicatively:

### 2a. Fused Dequant-Dot Kernels (primary — 3-4x)

Type-specific AVX2 functions that unpack quantized nibbles directly into SIMD registers,
multiply by scale factors, and FMA with the F32 activation vector — all without an
intermediate buffer. One function call per row instead of N calls per row.

```
Current (per block):
  read 144B quantized -> scalar unpack (500 cyc) -> write 1024B tmp -> read 1024B tmp -> AVX2 dot (50 cyc)
  ~850 cycles/block

Fused (per block):
  read 144B quantized + 1024B x -> AVX2 unpack+scale+FMA in registers -> accumulate
  ~200 cycles/block
```

### 2b. SIMD Attention (secondary — 1.1-1.2x)

The attention QK dot product and V accumulation loops in `qmodel_forward` are pure
scalar. Replacing with AVX2 FMA gives 4-8x on those inner loops. Attention is ~10-15%
of total forward pass time, so overall improvement is modest but free.

### 2c. RoPE Table Precompute (micro — 1.02x)

Pre-compute cos/sin values at each position once, reuse across all layers and heads.
Eliminates ~61K transcendental function calls per token (Llama 1B). Small absolute
savings but removes variable cost that scales with model size.

## 3. Fused Kernel Design

### 3.1. New Files

- `phase3/src/ai/qdot.h` — public API: `qdot_row()` dispatch + per-type function declarations
- `phase3/src/ai/qdot.c` — AVX2 fused kernels with scalar fallback
- `phase3/src/ai/test_qdot.c` — correctness tests against reference dequant+dot path

### 3.2. API

```c
/**
 * Fused quantized dot product: dot(dequant(W_row), x) without intermediate buffer.
 *
 * @param row_data  Pointer to one quantized row (raw bytes from GGUF .rodata)
 * @param x         F32 activation vector [K]
 * @param K         Row length (number of elements)
 * @param type      Quantization type (GGML_TYPE_Q4_K, etc.)
 * @return          Dot product as F32
 */
float qdot_row(const void *row_data, const float *x, int K, ggml_type_t type);
```

### 3.3. Per-Kernel Breakdown

#### qdot_q4_k (P0 — ~55 LOC)

Q4_K: 256 values in 144 bytes (d:F16, dmin:F16, scales:12B, qs:128B).
Most common type — used for wq, wk, wo, w_gate, w_up in all Q4_K_M models.

```
For each block (256 values):
  1. f16_to_f32(d), f16_to_f32(dmin)                     // 2 converts
  2. Unpack scales[12] -> sc[8], mn[8]                    // scalar, 12 bytes
  3. For each of 4 groups (j=0..3):
     Broadcast: vd1=d*sc[2j], vd2=d*sc[2j+1], vm1=dmin*mn[2j], vm2=dmin*mn[2j+1]
     For k=0,8,16,24:
       raw8 = _mm_loadl_epi64(qs + 32*j + k)             // load 8 bytes
       raw32 = _mm256_cvtepu8_epi32(raw8)                 // zero-extend to 8x int32
       // Lower nibble -> 8 values at offset 64*j + k
       lo = _mm256_and_si256(raw32, 0x0F mask)
       flo = _mm256_cvtepi32_ps(lo)
       val = vd1 * flo - vm1                              // _mm256_fmsub_ps
       dot += val * x[64*j + k .. k+7]                    // _mm256_fmadd_ps
       // Upper nibble -> 8 values at offset 64*j + 32 + k
       hi = _mm256_srli_epi32(raw32, 4)
       fhi = _mm256_cvtepi32_ps(hi)
       val = vd2 * fhi - vm2
       dot += val * x[64*j + 32 + k .. k+7]
```

32 FMA + 32 convert operations per block. 4 loads from qs, 32 loads from x.

#### qdot_q6_k (P0 — ~65 LOC)

Q6_K: 256 values in 210 bytes (d:F16, ql:128B, qh:64B, scales:16B).
Used for wv, w_down, token_embed, output_weight.

More complex interleaving: ql provides lower 4 bits, qh provides upper 2 bits.
Each value is a 6-bit signed integer (subtract 32). Two halves of 128 values,
each half processed in 32-iteration loops producing 4 values per iteration.

AVX2 strategy: process 8 values at a time from ql + qh, combine bits with
`_mm256_or_si256`, subtract 32, convert to F32, scale by `d * scales[is]`, FMA with x.

#### qdot_q8_0 (P1 — ~25 LOC)

Q8_0: 32 values in 34 bytes (d:F16, qs:32 x int8). Simplest kernel.

```
For each block (32 values):
  scale = f16_to_f32(d)
  For k=0,8,16,24:
    raw8 = _mm_loadl_epi64(qs + k)           // 8 bytes
    raw32 = _mm256_cvtepi8_epi32(raw8)        // sign-extend to 8x int32
    fval = _mm256_cvtepi32_ps(raw32) * scale
    dot += fval * x[k..k+7]
```

4 FMA operations per block of 32 values.

#### qdot_q5_k (P1 — ~55 LOC)

Q5_K: 256 values in 176 bytes (d:F16, dmin:F16, scales:12B, qh:32B, qs:128B).
Same structure as Q4_K but with a 5th bit from qh[]. Used by Phi-3, Qwen3.5.

AVX2 strategy: same as Q4_K, plus extract high bit from qh using shifts and masks,
`_mm256_or_si256` to combine with lower 4 bits before scaling.

#### qdot_q4_0 (P2 — ~25 LOC)

Q4_0: 32 values in 18 bytes (d:F16, qs:16B). Similar to Q4_K but simpler (no mins).
Values are `(nibble - 8) * scale`. Straightforward nibble split + FMA.

#### qdot_bf16 (P2 — ~20 LOC)

BF16: 2 bytes per value, left-shift 16 to reconstruct F32.

```
For k=0,8,...:
  raw16 = _mm_loadu_si128(bf16 + k)            // 8 x uint16
  raw32 = _mm256_cvtepu16_epi32(raw16)          // zero-extend
  raw32 = _mm256_slli_epi32(raw32, 16)          // shift to F32 position
  // reinterpret as __m256 (cast, no conversion)
  dot += val * x[k..k+7]
```

#### qdot_f16 (P2 — ~20 LOC)

F16: use `_mm256_cvtph_ps` (F16C instruction, available on Zen+) for 8-wide conversion,
then FMA with x. Fallback to scalar `f16_to_f32` if F16C unavailable.

#### Dispatch + Scalar Fallback (~40 LOC)

```c
float qdot_row(const void *row_data, const float *x, int K, ggml_type_t type)
{
#ifdef __AVX2__
    switch (type) {
    case GGML_TYPE_Q4_K: return qdot_q4_k_avx2(row_data, x, K);
    case GGML_TYPE_Q6_K: return qdot_q6_k_avx2(row_data, x, K);
    // ... etc
    }
#endif
    // Scalar fallback: dequant + dot (current behavior)
    return qdot_row_scalar(row_data, x, K, type);
}
```

Scalar fallback reuses existing `dequant_row()` + loop — ensures CI (no AVX2) still works.

### 3.4. Integration into qmatmul_vec

Replace the inner loop in `llama_quant.c:qmatmul_vec()`:

```c
// Current: per-block dequant + dot
for (int i = 0; i < M; i++) {
    const uint8_t *block_ptr = wdata + (size_t)i * row_bytes;
    float dot = 0.0f;
    for (int b = 0; b < n_blocks; b++) {
        dequant_row(block_ptr, tmp, block_size, wtype);
        // AVX2 dot of tmp with x...
        block_ptr += block_bytes;
    }
    out[i] = dot;
}

// New: single call per row
for (int i = 0; i < M; i++) {
    out[i] = qdot_row(wdata + (size_t)i * row_bytes, x, K, wtype);
}
```

The existing `dequant.c` and `dequant_row()` are NOT modified or removed — they are
still used by `qembed_lookup()`, `test_dequant.c`, and anywhere else that needs full
F32 dequantization. `qdot.c` is a new parallel path optimized for dot products only.

## 4. SIMD Attention

### 4.1. Reusable F32 Dot Helper

```c
static inline float simd_dot_f32(const float *a, const float *b, int n)
```

AVX2 FMA with 4-accumulator unroll for n >= 32, single-accumulator for n >= 8,
scalar tail. Used by both QK scoring and V accumulation, and potentially by the
F32 fast path in qmatmul_vec.

### 4.2. QK Dot Product (line ~1467-1472 of llama_quant.c)

Replace:
```c
float score = 0.0f;
for (int d = 0; d < l_head_dim; d++)
    score += q_head[d] * k_t[d];
```

With:
```c
float score = simd_dot_f32(q_head, k_t, l_head_dim);
```

### 4.3. V Accumulation (line ~1481-1485)

Replace scalar `out_h[d] += w * v_t[d]` with AVX2 broadcast of `w` + FMA:

```c
__m256 vw = _mm256_set1_ps(w);
for (int d = 0; d + 7 < l_head_dim; d += 8) {
    __m256 vo = _mm256_loadu_ps(out_h + d);
    __m256 vv = _mm256_loadu_ps(v_t + d);
    _mm256_storeu_ps(out_h + d, _mm256_fmadd_ps(vw, vv, vo));
}
```

## 5. RoPE Table Precompute

### 5.1. Table Structure

Add to `llama_state_t`:
```c
float *rope_cos;   // [max_seq_len * max_half_dim]
float *rope_sin;   // [max_seq_len * max_half_dim]
int    rope_half_dim;  // head_dim / 2 (or max of SWA/global)
```

### 5.2. Lazy Fill

On each `qmodel_forward` call, if `pos >= rope_filled`:
```c
for (int i = 0; i < half_dim; i++) {
    float freq = 1.0f / powf(theta, (float)(2*i) / (float)head_dim);
    rope_cos[pos * half_dim + i] = cosf(pos * freq);
    rope_sin[pos * half_dim + i] = sinf(pos * freq);
}
```

Each position computed once, reused across all heads and layers. For Llama 1B
(32 heads + 8 kv_heads, 16 layers), this replaces 40 * 16 = 640 identical
frequency computations per position with 1.

### 5.3. Dual Theta (Gemma 4)

Gemma 4 uses two theta values (SWA vs global). Allocate two tables if
`rope_theta_swa > 0`. Index by layer type.

## 6. Expected Results

| Stage | Optimization | Factor | Llama 1B | Gemma 4 E2B | Mistral 7B |
|-------|-------------|--------|----------|-------------|------------|
| Baseline | Current | 1.0x | 0.99 | 0.59 | 0.17 |
| Part 1+2 | Fused dequant-dot | 3.0-4.0x | 3.0-4.0 | 1.8-2.4 | 0.5-0.7 |
| Part 3 | + SIMD attention | 1.1-1.2x | 3.3-4.8 | 2.0-2.9 | 0.6-0.8 |
| Part 4 | + RoPE table | 1.02x | 3.4-4.9 | 2.0-3.0 | 0.6-0.8 |
| **Final** | **All** | **3.5-5.0x** | **3.5-5.0** | **2.0-3.0** | **0.6-0.8** |

Gemma 4 E2B gains less because PLE, sandwich norms, and softcapping add non-matmul
overhead. Mistral 7B gains less in absolute tok/s because it's 7x larger, but the
percentage improvement is similar.

## 7. Testing

### 7.1. Unit Tests (test_qdot.c)

For each kernel type:
1. Generate 1000 random blocks with known seeds
2. Compute reference: `dequant_row(block, tmp, K, type); dot = sum(tmp[i] * x[i])`
3. Compute fused: `qdot_row(block, x, K, type)`
4. Assert `|fused - reference| < 1e-4 * |reference| + 1e-7` (tolerance for FP reordering)
5. Edge cases: all-zero blocks, max-value blocks, single-block rows, large rows (K=14336)

### 7.2. Benchmark Protocol

Per-commit on JARVIS PC (Ryzen 2700X):
```bash
/tmp/bench_engine models/Llama-3.2-1B-Instruct-Q4_K_M.gguf --tokens 128
/tmp/bench_engine models/gemma-4-E2B-it-Q4_K_M.gguf --tokens 128
/tmp/bench_engine models/mistral-7b-instruct-v0.2.Q8_0.gguf --tokens 128
```

Record tok/s in commit message. Format: `perf: fused Q4_K qdot -- Llama 1B 0.99->X.XX tok/s (+NNN%)`

### 7.3. Correctness Regression

After wiring fused kernels into qmatmul_vec, verify generation output:
- Same prompt -> same or near-identical top-5 logits (greedy decode should match)
- If logits differ by more than 1e-3: investigate before proceeding
- Run bench_engine with `--debug` to print first 20 generated tokens for visual comparison

### 7.4. CI

Add to `.github/workflows/ci.yml`:
```yaml
- name: Test qdot kernels
  run: |
    gcc -O2 -std=c11 -I phase3/src/ai \
        phase3/src/ai/test_qdot.c phase3/src/ai/qdot.c \
        phase3/src/ai/dequant.c -lm -o /tmp/test_qdot
    /tmp/test_qdot
```

CI compiles WITHOUT `-mavx2` — tests the scalar fallback path. AVX2 path tested
on JARVIS PC only (bare metal bench).

## 8. Commit Strategy

| # | Message | Files Changed | Bench After |
|---|---------|---------------|-------------|
| 1 | `bench: baseline tok/s before perf work` | bench results | Record 0.99 / 0.59 / 0.17 |
| 2 | `feat: fused Q4_K qdot -- AVX2 dequant-dot kernel` | qdot.c/h, test_qdot.c | Unit tests only |
| 3 | `feat: fused Q6_K qdot` | qdot.c, test_qdot.c | Unit tests only |
| 4 | `feat: wire qdot into qmatmul_vec` | llama_quant.c | **Big jump** -- all 3 models |
| 5 | `feat: fused Q8_0 + Q4_0 + Q5_K qdot kernels` | qdot.c, test_qdot.c | + Mistral, Phi-3 |
| 6 | `feat: fused BF16/F16 qdot` | qdot.c | + Gemma 4 PLE |
| 7 | `feat: SIMD attention QK dot + V accumulation` | llama_quant.c | All 3 models |
| 8 | `feat: RoPE cos/sin precompute table` | llama_quant.c, llama_model.h | All 3 models |
| 9 | `docs: update CLAUDE.md + plan with perf results` | CLAUDE.md, plan | -- |
| 10 | `ci: add test_qdot to CI workflow` | ci.yml | CI green |

## 9. Non-Goals

- **Threading:** Deferred to Phase 3d / Phase 4. seL4 TCB thread pool is a separate workstream.
- **Q8 activation quantization:** Potential additional 2x by quantizing the activation vector to Q8_K and using integer VPMADDUBSW dot products. Follow-up if 4-5 tok/s is insufficient.
- **GPU compute:** Deferred to Phase 4. Phase 3 is CPU-only.
- **Prompt processing (prefill):** This spec targets token generation (tg). Prefill optimization (processing multiple tokens in parallel) is a separate concern.
- **Changes to dequant.c:** Existing dequant functions are preserved for embedding lookup and other non-dot-product uses. qdot.c is a new parallel path.

## 10. Risks

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| Q4_K nibble interleaving bug | Medium | High (wrong output) | Unit test against reference dequant before wiring in |
| Q6_K complex ql/qh layout bug | Medium | High | Same — extensive unit tests with known values |
| AVX2 alignment issue | Low | Medium (crash on bare metal) | `_mm_loadl_epi64` and `_mm256_loadu_*` handle unaligned; test on real hardware |
| Gemma 4 PLE regression | Low | High | Bench Gemma 4 E2B after every commit |
| Performance below 3x target | Low | Medium | Even 2.5x (2.5 tok/s) is a significant improvement; Q8 activation quantization as backup |
| seL4 Process B stack overflow | Very Low | High | Fused kernels use LESS stack than current (no tmp[256]) |
