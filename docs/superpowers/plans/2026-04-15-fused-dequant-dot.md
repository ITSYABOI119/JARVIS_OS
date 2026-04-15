# Fused Dequant-Dot Kernel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Increase JARVIS engine inference speed from ~1.0 tok/s to 4-5 tok/s for Llama 1B on Ryzen 2700X (single-threaded) by fusing dequantization and dot product into a single AVX2 SIMD pass.

**Architecture:** New `qdot.c/h` module with type-specific AVX2 kernels (Q4_K, Q6_K, Q8_0, Q5_K, Q4_0, BF16, F16) that read quantized data directly into SIMD registers and FMA with the F32 activation vector — no intermediate `float tmp[256]` buffer. Scalar fallback for CI (no AVX2). Wired into existing `qmatmul_vec()` in `llama_quant.c`. Secondary: SIMD attention (AVX2 QK dot + V accumulation). Tertiary: RoPE cos/sin precompute table.

**Tech Stack:** C11, AVX2+FMA intrinsics (`immintrin.h`), existing `dequant.h` block structs, `gguf_parser.h` types.

**Design spec:** `docs/superpowers/specs/2026-04-13-perf-fused-dequant-dot-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `phase3/src/ai/qdot.h` | Create | Public API: `qdot_row()` dispatch, per-type AVX2 function declarations |
| `phase3/src/ai/qdot.c` | Create | AVX2 fused kernels + scalar fallback |
| `phase3/src/ai/test_qdot.c` | Create | Correctness tests: fused vs reference `dequant_row()+dot` |
| `phase3/src/ai/llama_quant.c` | Modify | Replace `qmatmul_vec` inner loop with `qdot_row()`, SIMD attention |
| `phase3/src/ai/llama_model.h` | Modify | Add RoPE table fields to `llama_state_t` |
| `phase3/src/ai/llama_load.c` | Modify | Allocate/free RoPE tables in `llama_alloc_state`/`llama_free_state` |
| `.github/workflows/ci.yml` | Modify | Add `test_qdot` CI step |

---

### Task 1: Create qdot.h header with API and scalar fallback

**Files:**
- Create: `phase3/src/ai/qdot.h`
- Create: `phase3/src/ai/qdot.c` (scalar fallback only)

- [ ] **Step 1: Write qdot.h**

```c
/*
 * JARVIS AI-OS Phase 3 — Fused Quantized Dot Product
 *
 * Type-specific AVX2 kernels: dequant + dot in one SIMD pass.
 * No intermediate F32 buffer. Scalar fallback for CI (no AVX2).
 *
 * Pure C11. AVX2 path guarded by #ifdef __AVX2__.
 */

#ifndef QDOT_H
#define QDOT_H

#include "dequant.h"  /* ggml_type_t, block structs, f16_to_f32 */

/**
 * Fused quantized dot product: dot(dequant(W_row), x) without intermediate buffer.
 *
 * @param row_data  Pointer to one quantized row (raw bytes from GGUF .rodata)
 * @param x         F32 activation vector [K]
 * @param K         Row length (number of elements, must be multiple of block_size)
 * @param type      Quantization type (GGML_TYPE_Q4_K, etc.)
 * @return          Dot product as F32
 */
float qdot_row(const void *row_data, const float *x, int K, ggml_type_t type);

#endif /* QDOT_H */
```

- [ ] **Step 2: Write qdot.c with scalar fallback only**

```c
/*
 * JARVIS AI-OS Phase 3 — Fused Quantized Dot Product
 *
 * AVX2 fused kernels with scalar fallback.
 * Each kernel reads quantized data directly into SIMD registers,
 * multiplies by scale, and FMA with F32 activation — no tmp buffer.
 */

#include "qdot.h"
#include <string.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

/* ---- Scalar fallback: dequant + dot (uses existing dequant_row) ---- */

static float qdot_row_scalar(const void *row_data, const float *x, int K, ggml_type_t type)
{
    const int bs = dequant_type_block_size(type);
    const size_t bb = dequant_type_block_bytes(type);
    if (bs <= 0 || bb == 0) return 0.0f;

    const int n_blocks = K / bs;
    const uint8_t *ptr = (const uint8_t *)row_data;
    float tmp[256];
    float dot = 0.0f;

    for (int b = 0; b < n_blocks; b++) {
        dequant_row(ptr, tmp, bs, type);
        const float *xb = x + b * bs;
        for (int j = 0; j < bs; j++)
            dot += tmp[j] * xb[j];
        ptr += bb;
    }
    return dot;
}

/* ---- Dispatch ---- */

float qdot_row(const void *row_data, const float *x, int K, ggml_type_t type)
{
    /* AVX2 kernels added in subsequent tasks */
    return qdot_row_scalar(row_data, x, K, type);
}
```

- [ ] **Step 3: Verify compiles**

Run: `wsl -e bash -lc "cd /mnt/c/Users/jluca/Documents/JARVIS_OS && gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai -c phase3/src/ai/qdot.c -o /dev/null && echo OK"`
Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add phase3/src/ai/qdot.h phase3/src/ai/qdot.c
git commit -m "feat: qdot.h/c scaffold — API + scalar fallback for fused dequant-dot"
```

---

### Task 2: Create test_qdot.c with Q4_K correctness test

**Files:**
- Create: `phase3/src/ai/test_qdot.c`

- [ ] **Step 1: Write test_qdot.c**

```c
/*
 * JARVIS AI-OS Phase 3 — Fused Dequant-Dot Correctness Tests
 *
 * Compares qdot_row() against reference: dequant_row() + scalar dot.
 * Tolerance: |fused - ref| < 1e-4 * |ref| + 1e-7 (FP reordering).
 */

#include "qdot.h"
#include "dequant.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_pass = 0, tests_fail = 0;
#define TEST(name) printf("Test %d: %s ... ", tests_pass + tests_fail + 1, name)
#define PASS() do { printf("PASS\n"); tests_pass++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_fail++; } while(0)

/* Simple PRNG for reproducible test data */
static uint32_t test_rng = 42;
static uint32_t xor32(void) { test_rng ^= test_rng<<13; test_rng ^= test_rng>>17; test_rng ^= test_rng<<5; return test_rng; }
static float randf(void) { return (float)(xor32() % 10000) / 10000.0f - 0.5f; }

/* Reference: dequant_row + scalar dot */
static float ref_dot(const void *row, const float *x, int K, ggml_type_t type)
{
    int bs = dequant_type_block_size(type);
    size_t bb = dequant_type_block_bytes(type);
    int nb = K / bs;
    const uint8_t *p = (const uint8_t *)row;
    float tmp[256], dot = 0.0f;
    for (int b = 0; b < nb; b++) {
        dequant_row(p, tmp, bs, type);
        for (int j = 0; j < bs; j++) dot += tmp[j] * x[b * bs + j];
        p += bb;
    }
    return dot;
}

static int check_close(float a, float b, const char *label)
{
    float tol = 1e-4f * fabsf(b) + 1e-7f;
    if (fabsf(a - b) <= tol) return 1;
    printf("  %s: fused=%.6f ref=%.6f delta=%.6e tol=%.6e\n", label, a, b, a - b, tol);
    return 0;
}

/* ---- Q4_K test ---- */
static void test_q4k(void)
{
    TEST("Q4_K fused vs reference (K=2048, 8 blocks)");
    int K = 2048;
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_Q4_K);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));

    /* Fill with pseudo-random data */
    test_rng = 12345;
    for (size_t i = 0; i < row_bytes; i++) row[i] = (uint8_t)(xor32() & 0xFF);
    for (int i = 0; i < K; i++) x[i] = randf();

    float fused = qdot_row(row, x, K, GGML_TYPE_Q4_K);
    float ref = ref_dot(row, x, K, GGML_TYPE_Q4_K);

    if (check_close(fused, ref, "Q4_K")) PASS(); else FAIL("Q4_K mismatch");
    free(row); free(x);
}

/* ---- Q6_K test ---- */
static void test_q6k(void)
{
    TEST("Q6_K fused vs reference (K=2048)");
    int K = 2048;
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_Q6_K);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));

    test_rng = 54321;
    for (size_t i = 0; i < row_bytes; i++) row[i] = (uint8_t)(xor32() & 0xFF);
    for (int i = 0; i < K; i++) x[i] = randf();

    float fused = qdot_row(row, x, K, GGML_TYPE_Q6_K);
    float ref = ref_dot(row, x, K, GGML_TYPE_Q6_K);

    if (check_close(fused, ref, "Q6_K")) PASS(); else FAIL("Q6_K mismatch");
    free(row); free(x);
}

/* ---- Q8_0 test ---- */
static void test_q8_0(void)
{
    TEST("Q8_0 fused vs reference (K=2048)");
    int K = 2048;
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_Q8_0);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));

    test_rng = 99999;
    for (size_t i = 0; i < row_bytes; i++) row[i] = (uint8_t)(xor32() & 0xFF);
    for (int i = 0; i < K; i++) x[i] = randf();

    float fused = qdot_row(row, x, K, GGML_TYPE_Q8_0);
    float ref = ref_dot(row, x, K, GGML_TYPE_Q8_0);

    if (check_close(fused, ref, "Q8_0")) PASS(); else FAIL("Q8_0 mismatch");
    free(row); free(x);
}

/* ---- Q5_K test ---- */
static void test_q5k(void)
{
    TEST("Q5_K fused vs reference (K=2048)");
    int K = 2048;
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_Q5_K);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));

    test_rng = 77777;
    for (size_t i = 0; i < row_bytes; i++) row[i] = (uint8_t)(xor32() & 0xFF);
    for (int i = 0; i < K; i++) x[i] = randf();

    float fused = qdot_row(row, x, K, GGML_TYPE_Q5_K);
    float ref = ref_dot(row, x, K, GGML_TYPE_Q5_K);

    if (check_close(fused, ref, "Q5_K")) PASS(); else FAIL("Q5_K mismatch");
    free(row); free(x);
}

/* ---- Q4_0 test ---- */
static void test_q4_0(void)
{
    TEST("Q4_0 fused vs reference (K=2048)");
    int K = 2048;
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_Q4_0);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));

    test_rng = 55555;
    for (size_t i = 0; i < row_bytes; i++) row[i] = (uint8_t)(xor32() & 0xFF);
    for (int i = 0; i < K; i++) x[i] = randf();

    float fused = qdot_row(row, x, K, GGML_TYPE_Q4_0);
    float ref = ref_dot(row, x, K, GGML_TYPE_Q4_0);

    if (check_close(fused, ref, "Q4_0")) PASS(); else FAIL("Q4_0 mismatch");
    free(row); free(x);
}

/* ---- BF16 test ---- */
static void test_bf16(void)
{
    TEST("BF16 fused vs reference (K=2048)");
    int K = 2048;
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_BF16);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));

    /* Generate valid BF16 values (upper 16 bits of F32) */
    test_rng = 33333;
    for (int i = 0; i < K; i++) {
        float v = randf();
        uint32_t bits;
        memcpy(&bits, &v, 4);
        uint16_t bf = (uint16_t)(bits >> 16);
        memcpy(row + i * 2, &bf, 2);
    }
    for (int i = 0; i < K; i++) x[i] = randf();

    float fused = qdot_row(row, x, K, GGML_TYPE_BF16);
    float ref = ref_dot(row, x, K, GGML_TYPE_BF16);

    if (check_close(fused, ref, "BF16")) PASS(); else FAIL("BF16 mismatch");
    free(row); free(x);
}

/* ---- F16 test ---- */
static void test_f16(void)
{
    TEST("F16 fused vs reference (K=256)");
    int K = 256;
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_F16);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));

    /* Use small values to avoid F16 overflow */
    test_rng = 11111;
    for (int i = 0; i < K; i++) {
        float v = randf() * 0.1f;
        /* Convert to F16 via round-trip */
        uint16_t h;
        uint32_t bits;
        memcpy(&bits, &v, 4);
        uint32_t sign = (bits >> 16) & 0x8000;
        int exp = ((bits >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = (bits >> 13) & 0x3FF;
        if (exp <= 0) { h = (uint16_t)sign; }
        else if (exp >= 31) { h = (uint16_t)(sign | 0x7C00); }
        else { h = (uint16_t)(sign | (exp << 10) | mant); }
        memcpy(row + i * 2, &h, 2);
    }
    for (int i = 0; i < K; i++) x[i] = randf();

    float fused = qdot_row(row, x, K, GGML_TYPE_F16);
    float ref = ref_dot(row, x, K, GGML_TYPE_F16);

    if (check_close(fused, ref, "F16")) PASS(); else FAIL("F16 mismatch");
    free(row); free(x);
}

/* ---- Large K test (K=14336 = Llama 8B hidden_dim) ---- */
static void test_large_k(void)
{
    TEST("Q4_K fused vs reference (K=14336, large row)");
    int K = 14336;  /* 14336 / 256 = 56 blocks */
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_Q4_K);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));

    test_rng = 88888;
    for (size_t i = 0; i < row_bytes; i++) row[i] = (uint8_t)(xor32() & 0xFF);
    for (int i = 0; i < K; i++) x[i] = randf();

    float fused = qdot_row(row, x, K, GGML_TYPE_Q4_K);
    float ref = ref_dot(row, x, K, GGML_TYPE_Q4_K);

    if (check_close(fused, ref, "Q4_K large")) PASS(); else FAIL("Q4_K large mismatch");
    free(row); free(x);
}

/* ---- Zero block test ---- */
static void test_zero_block(void)
{
    TEST("Q4_K all-zero block returns ~0");
    int K = 256;
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_Q4_K);
    uint8_t *row = (uint8_t *)calloc(1, row_bytes);  /* all zero */
    float x[256];
    for (int i = 0; i < K; i++) x[i] = 1.0f;

    float result = qdot_row(row, x, K, GGML_TYPE_Q4_K);
    /* Zero scales → all dequantized values are 0 or -min → dot ≈ 0 or negative */
    /* Just verify no crash and finite result */
    if (isfinite(result)) PASS(); else FAIL("not finite");
    free(row);
}

int main(void)
{
    printf("=== JARVIS Fused Dequant-Dot (qdot) Test Suite ===\n\n");

    test_q4k();
    test_q6k();
    test_q8_0();
    test_q5k();
    test_q4_0();
    test_bf16();
    test_f16();
    test_large_k();
    test_zero_block();

    printf("\n=== Results: %d/%d PASS, %d FAIL ===\n",
           tests_pass, tests_pass + tests_fail, tests_fail);
    return tests_fail > 0 ? 1 : 0;
}
```

- [ ] **Step 2: Compile and run — all tests should PASS (scalar fallback matches reference exactly)**

Run:
```bash
wsl -e bash -lc "cd /mnt/c/Users/jluca/Documents/JARVIS_OS && \
  gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
  phase3/src/ai/test_qdot.c phase3/src/ai/qdot.c phase3/src/ai/dequant.c \
  -lm -o /tmp/test_qdot && /tmp/test_qdot"
```
Expected: `9/9 PASS, 0 FAIL` (scalar path = reference path, so exact match)

- [ ] **Step 3: Commit**

```bash
git add phase3/src/ai/test_qdot.c
git commit -m "test: qdot correctness suite — 9 tests covering all quant types + edge cases"
```

---

### Task 3: Implement AVX2 fused Q4_K kernel

**Files:**
- Modify: `phase3/src/ai/qdot.c`

This is the highest-impact kernel — Q4_K is used for wq, wk, wo, w_gate, w_up in all Q4_K_M models (the majority of all weight bytes).

- [ ] **Step 1: Add the AVX2 Q4_K kernel to qdot.c**

Add before the dispatch function. The kernel processes one full row: iterates over blocks, unpacks nibbles with AVX2, scales, and FMA with x. The Q4_K block layout and scale unpacking logic MUST match `dequant_q4_k()` in `dequant.c` exactly — refer to lines 106-138 of `dequant.c` for the authoritative nibble/scale mapping.

Key AVX2 operations per 8 values:
- `_mm_loadl_epi64`: load 8 bytes from qs
- `_mm256_cvtepu8_epi32`: zero-extend to 8 x int32
- `_mm256_and_si256(raw32, mask_0f)`: lower nibble
- `_mm256_srli_epi32(raw32, 4)`: upper nibble
- `_mm256_cvtepi32_ps`: int32 -> F32
- `_mm256_fmsub_ps(vd, fval, vm)`: d*nibble - min
- `_mm256_fmadd_ps(val, x_vec, acc)`: accumulate dot

- [ ] **Step 2: Wire Q4_K into dispatch**

In `qdot_row()`, add the AVX2 switch case:
```c
#ifdef __AVX2__
    switch (type) {
    case GGML_TYPE_Q4_K: return qdot_q4_k_avx2(row_data, x, K);
    default: break;
    }
#endif
    return qdot_row_scalar(row_data, x, K, type);
```

- [ ] **Step 3: Compile with AVX2 and run tests**

Run:
```bash
wsl -e bash -lc "cd /mnt/c/Users/jluca/Documents/JARVIS_OS && \
  gcc -Wall -Werror -O2 -mavx2 -mfma -std=c11 -I phase3/src/ai \
  phase3/src/ai/test_qdot.c phase3/src/ai/qdot.c phase3/src/ai/dequant.c \
  -lm -o /tmp/test_qdot && /tmp/test_qdot"
```
Expected: `9/9 PASS` — Q4_K test now exercises AVX2 path, tolerance allows FP reordering.

- [ ] **Step 4: Also test WITHOUT AVX2 (CI path)**

Run:
```bash
wsl -e bash -lc "cd /mnt/c/Users/jluca/Documents/JARVIS_OS && \
  gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
  phase3/src/ai/test_qdot.c phase3/src/ai/qdot.c phase3/src/ai/dequant.c \
  -lm -o /tmp/test_qdot && /tmp/test_qdot"
```
Expected: `9/9 PASS` (scalar fallback, exact match)

- [ ] **Step 5: Commit**

```bash
git add phase3/src/ai/qdot.c
git commit -m "feat: fused Q4_K qdot — AVX2 dequant-dot kernel (P0)"
```

---

### Task 4: Implement AVX2 fused Q6_K kernel

**Files:**
- Modify: `phase3/src/ai/qdot.c`

Q6_K is used for wv, w_down, token_embed, output_weight — the second most common type. More complex interleaving than Q4_K: ql provides lower 4 bits, qh provides upper 2 bits, values are 6-bit signed (subtract 32). Refer to `dequant_q6_k()` in `dequant.c` lines 203-228 for the authoritative layout.

- [ ] **Step 1: Add AVX2 Q6_K kernel to qdot.c**

The Q6_K block (210 bytes) has `ql[128]`, `qh[64]`, `scales[16]`, `d` (F16). Two halves of 128 values each. Within each half, 32 iterations produce 4 values each using ql nibbles and qh 2-bit pairs. AVX2 processes 8 values at a time.

- [ ] **Step 2: Wire Q6_K into dispatch switch**

- [ ] **Step 3: Compile with AVX2 and run tests**

Same command as Task 3 Step 3. Expected: `9/9 PASS`.

- [ ] **Step 4: Commit**

```bash
git add phase3/src/ai/qdot.c
git commit -m "feat: fused Q6_K qdot — AVX2 dequant-dot kernel (P0)"
```

---

### Task 5: Implement AVX2 fused Q8_0, Q4_0, Q5_K kernels

**Files:**
- Modify: `phase3/src/ai/qdot.c`

Three simpler kernels in one task:

**Q8_0** (34B/32val): Simplest — sign-extend int8 to int32, cvt to F32, scale, FMA. 4 FMA ops per block.

**Q4_0** (18B/32val): Like Q4_K but simpler — no mins, values are `(nibble - 8) * scale`.

**Q5_K** (176B/256val): Like Q4_K but with 5th bit from qh[]. Same scale unpacking, plus extract qh bit with `_mm256_and_si256` + shift, `_mm256_or_si256` to combine.

- [ ] **Step 1: Add Q8_0 kernel**
- [ ] **Step 2: Add Q4_0 kernel**
- [ ] **Step 3: Add Q5_K kernel**
- [ ] **Step 4: Wire all three into dispatch switch**
- [ ] **Step 5: Compile with AVX2, run tests**

Expected: `9/9 PASS`.

- [ ] **Step 6: Commit**

```bash
git add phase3/src/ai/qdot.c
git commit -m "feat: fused Q8_0 + Q4_0 + Q5_K qdot — AVX2 kernels (P1)"
```

---

### Task 6: Implement AVX2 fused BF16 and F16 kernels

**Files:**
- Modify: `phase3/src/ai/qdot.c`

**BF16**: Left-shift 16 bits to reconstruct F32. `_mm256_cvtepu16_epi32` + `_mm256_slli_epi32(raw32, 16)` + cast to `__m256`.

**F16**: Use `_mm256_cvtph_ps` (F16C, available on Zen+). Fallback: use scalar `f16_to_f32` if `__F16C__` not defined.

- [ ] **Step 1: Add BF16 kernel**
- [ ] **Step 2: Add F16 kernel**
- [ ] **Step 3: Wire both into dispatch**
- [ ] **Step 4: Compile with AVX2, run tests**

Expected: `9/9 PASS`.

- [ ] **Step 5: Commit**

```bash
git add phase3/src/ai/qdot.c
git commit -m "feat: fused BF16/F16 qdot — AVX2 kernels (P2)"
```

---

### Task 7: Wire qdot_row into qmatmul_vec — THE BIG SWITCH

**Files:**
- Modify: `phase3/src/ai/llama_quant.c` (lines 94-166)

This is where the performance jump happens. Replace the inner loop of `qmatmul_vec()` with a single `qdot_row()` call per row. The existing `dequant.c` functions are NOT removed — they're still used by `qembed_lookup()` and tests.

- [ ] **Step 1: Add `#include "qdot.h"` at top of llama_quant.c**

After the existing `#include "dequant.h"` (line 28).

- [ ] **Step 2: Replace the quantized path in qmatmul_vec**

Replace lines 112-165 (the `/* Quantized path */` block) with:

```c
    /* Quantized path: fused dequant-dot (one call per row) */
    const size_t row_bytes = dequant_row_bytes(K, wtype);
    if (row_bytes == 0) {
        memset(out, 0, (size_t)M * sizeof(float));
        return;
    }
    const uint8_t *wdata = (const uint8_t *)W->data;

    for (int i = 0; i < M; i++)
        out[i] = qdot_row(wdata + (size_t)i * row_bytes, x, K, wtype);
```

- [ ] **Step 3: Compile and run existing tests (test_llama_quant)**

Run:
```bash
wsl -e bash -lc "cd /mnt/c/Users/jluca/Documents/JARVIS_OS && \
  gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
  phase3/src/ai/test_llama_quant.c phase3/src/ai/llama_quant.c \
  phase3/src/ai/llama_load.c phase3/src/ai/llama_forward.c \
  phase3/src/ai/tensor_ops.c phase3/src/ai/dequant.c phase3/src/ai/qdot.c \
  phase3/src/ai/sampling.c phase3/src/ai/gguf_parser.c \
  phase3/src/ai/tokenizer.c phase3/src/ai/ssm.c \
  -lm -o /tmp/test_llama_quant && /tmp/test_llama_quant"
```
Expected: `10/10 PASS` — wired path uses scalar fallback, produces same results.

- [ ] **Step 4: Commit**

```bash
git add phase3/src/ai/llama_quant.c
git commit -m "feat: wire qdot into qmatmul_vec — replace dequant+dot loop"
```

---

### Task 8: SIMD attention — AVX2 QK dot + V accumulation

**Files:**
- Modify: `phase3/src/ai/llama_quant.c`

Replace scalar attention QK dot product (line ~1469-1471) and V accumulation (line ~1483-1485) with AVX2 FMA.

- [ ] **Step 1: Add simd_dot_f32 helper function**

Add near the top of llama_quant.c (after the includes, before `qmatmul_vec`):

```c
/* Reusable F32 dot product with AVX2 FMA (4-accumulator unroll for n>=32) */
static inline float simd_dot_f32(const float *a, const float *b, int n)
{
#ifdef __AVX2__
    __m256 sum0 = _mm256_setzero_ps();
    __m256 sum1 = _mm256_setzero_ps();
    __m256 sum2 = _mm256_setzero_ps();
    __m256 sum3 = _mm256_setzero_ps();
    int i = 0;
    for (; i + 31 < n; i += 32) {
        sum0 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i),    _mm256_loadu_ps(b+i),    sum0);
        sum1 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i+8),  _mm256_loadu_ps(b+i+8),  sum1);
        sum2 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i+16), _mm256_loadu_ps(b+i+16), sum2);
        sum3 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i+24), _mm256_loadu_ps(b+i+24), sum3);
    }
    sum0 = _mm256_add_ps(_mm256_add_ps(sum0, sum1), _mm256_add_ps(sum2, sum3));
    for (; i + 7 < n; i += 8)
        sum0 = _mm256_fmadd_ps(_mm256_loadu_ps(a+i), _mm256_loadu_ps(b+i), sum0);
    __m128 hi = _mm256_extractf128_ps(sum0, 1);
    __m128 lo = _mm256_castps256_ps128(sum0);
    __m128 s = _mm_add_ps(lo, hi);
    s = _mm_hadd_ps(s, s);
    s = _mm_hadd_ps(s, s);
    float dot = _mm_cvtss_f32(s);
    for (; i < n; i++) dot += a[i] * b[i];
    return dot;
#else
    float dot = 0.0f;
    for (int i = 0; i < n; i++) dot += a[i] * b[i];
    return dot;
#endif
}
```

- [ ] **Step 2: Replace QK dot product**

Find `score += q_head[d] * k_t[d]` loop (line ~1469-1471) and replace:

```c
                float score = simd_dot_f32(q_head, k_t, l_head_dim);
```

- [ ] **Step 3: Replace V accumulation**

Find `out_h[d] += w * v_t[d]` loop (line ~1483-1485) and replace:

```c
#ifdef __AVX2__
                __m256 vw = _mm256_set1_ps(w);
                int d = 0;
                for (; d + 7 < l_head_dim; d += 8) {
                    __m256 vo = _mm256_loadu_ps(out_h + d);
                    __m256 vv = _mm256_loadu_ps(v_t + d);
                    _mm256_storeu_ps(out_h + d, _mm256_fmadd_ps(vw, vv, vo));
                }
                for (; d < l_head_dim; d++)
                    out_h[d] += w * v_t[d];
#else
                for (int d = 0; d < l_head_dim; d++)
                    out_h[d] += w * v_t[d];
#endif
```

- [ ] **Step 4: Compile and run test_llama_quant**

Same compile command as Task 7 Step 3. Expected: `10/10 PASS`.

- [ ] **Step 5: Commit**

```bash
git add phase3/src/ai/llama_quant.c
git commit -m "feat: SIMD attention QK dot + V accumulation — AVX2 FMA"
```

---

### Task 9: RoPE cos/sin precompute table

**Files:**
- Modify: `phase3/src/ai/llama_model.h`
- Modify: `phase3/src/ai/llama_load.c`
- Modify: `phase3/src/ai/llama_quant.c`

- [ ] **Step 1: Add RoPE table fields to llama_state_t in llama_model.h**

After `fwd_scratch_size`:
```c
    float *rope_cos;       /* [max_seq_len * rope_half_dim] — precomputed cos */
    float *rope_sin;       /* [max_seq_len * rope_half_dim] — precomputed sin */
    float *rope_cos_swa;   /* Gemma 4 SWA theta (NULL if no SWA) */
    float *rope_sin_swa;   /* Gemma 4 SWA theta (NULL if no SWA) */
    int    rope_half_dim;  /* max(head_dim, head_dim_swa) / 2 */
    int    rope_filled;    /* positions filled so far (lazy fill up to pos) */
    int    rope_filled_swa;
```

- [ ] **Step 2: Allocate RoPE tables in llama_alloc_state (llama_load.c)**

After the `fwd_scratch` allocation block:
```c
    /* RoPE precompute tables — lazy filled during forward pass */
    {
        int max_hd = config->head_dim;
        if (config->head_dim_swa > max_hd) max_hd = config->head_dim_swa;
        state->rope_half_dim = max_hd / 2;
        state->rope_filled = 0;
        state->rope_filled_swa = 0;
        size_t rope_size = (size_t)max_seq * (size_t)state->rope_half_dim;
        state->rope_cos = (float *)malloc(rope_size * sizeof(float));
        state->rope_sin = (float *)malloc(rope_size * sizeof(float));
        if (!state->rope_cos || !state->rope_sin) {
            llama_free_state(state);
            return -1;
        }
        if (config->rope_theta_swa > 0.0f) {
            state->rope_cos_swa = (float *)malloc(rope_size * sizeof(float));
            state->rope_sin_swa = (float *)malloc(rope_size * sizeof(float));
        } else {
            state->rope_cos_swa = NULL;
            state->rope_sin_swa = NULL;
        }
    }
```

- [ ] **Step 3: Free in llama_free_state**

Add before `memset`:
```c
    free(state->rope_cos);
    free(state->rope_sin);
    free(state->rope_cos_swa);
    free(state->rope_sin_swa);
```

- [ ] **Step 4: Lazy fill in qmodel_forward (llama_quant.c)**

At the top of the layer loop (after `int pos = state->pos`), add lazy fill for the current position:
```c
    /* Lazy-fill RoPE table for this position */
    if (state->rope_cos && pos >= state->rope_filled) {
        int half = state->rope_half_dim;
        for (int p = state->rope_filled; p <= pos; p++) {
            for (int i = 0; i < half; i++) {
                float freq = 1.0f / powf(c->rope_theta, (float)(2*i) / (float)(half*2));
                state->rope_cos[p * half + i] = cosf((float)p * freq);
                state->rope_sin[p * half + i] = sinf((float)p * freq);
            }
        }
        state->rope_filled = pos + 1;
    }
```

Then replace the `powf`/`cosf`/`sinf` calls in the RoPE loops with table lookups.

- [ ] **Step 5: Compile and run test_llama_quant**

Expected: `10/10 PASS`.

- [ ] **Step 6: Commit**

```bash
git add phase3/src/ai/llama_model.h phase3/src/ai/llama_load.c phase3/src/ai/llama_quant.c
git commit -m "feat: RoPE cos/sin precompute table — eliminates 61K trig calls/token"
```

---

### Task 10: Add test_qdot to CI workflow + update CLAUDE.md

**Files:**
- Modify: `.github/workflows/ci.yml`
- Modify: `CLAUDE.md`

- [ ] **Step 1: Add CI step**

Add after the "Phase 3: Q4_K/Q6_K Dequantization" step in `.github/workflows/ci.yml`:

```yaml
    - name: "Phase 3: Fused Dequant-Dot (C)"
      run: |
        gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
          phase3/src/ai/test_qdot.c phase3/src/ai/qdot.c \
          phase3/src/ai/dequant.c -lm -o /tmp/test_qdot
        /tmp/test_qdot
```

Note: NO `-mavx2` — CI tests the scalar fallback. AVX2 is tested on JARVIS PC.

- [ ] **Step 2: Also add qdot.c to the test_llama_quant CI step**

Find the "Phase 3: Quantized Model (C)" step and add `phase3/src/ai/qdot.c \` to the gcc line (after `dequant.c`).

- [ ] **Step 3: Update CLAUDE.md**

Update codebase metrics, add qdot.c/h to Quick Reference, update perf numbers with actual benchmark results.

- [ ] **Step 4: Push and verify CI green**

```bash
git push
gh run list --limit 1
gh run watch <id> --exit-status
```

- [ ] **Step 5: Commit**

```bash
git add .github/workflows/ci.yml CLAUDE.md
git commit -m "ci: add test_qdot to workflow + update CLAUDE.md with perf results"
```

---

## Self-Review Checklist

- [x] **Spec coverage:** All 3 parts covered (fused kernels Tasks 1-7, SIMD attention Task 8, RoPE Task 9). All 7 quant types have kernels. CI integration in Task 10. Commit strategy matches spec Section 8.
- [x] **Placeholder scan:** All test code is complete. All AVX2 pseudocode in spec is referenced. No TBDs.
- [x] **Type consistency:** `qdot_row()` signature consistent across all tasks. `simd_dot_f32()` named consistently. `llama_state_t` field names consistent between header and load/free.
- [x] **Spec Section 7 testing:** test_qdot covers all types + edge cases (zero, large K). Tolerance matches spec (1e-4 * |ref| + 1e-7).
- [x] **Spec Section 9 non-goals respected:** No threading, no Q8 activation quant, no GPU, no prefill optimization, no changes to dequant.c.
