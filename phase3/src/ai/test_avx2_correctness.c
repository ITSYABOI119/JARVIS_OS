/*
 * JARVIS AI-OS Phase 3 — AVX2 qdot Correctness Test
 *
 * Dedicated AVX2-path correctness harness. This translation unit is ALWAYS
 * compiled with -mavx2 -mfma in CI, so __AVX2__ is defined and qdot_row()
 * dispatches to the type-specific SIMD kernels in qdot.c. This closes a
 * latent gap: the existing test_qdot CI step does NOT pass -mavx2/-mfma, so
 * the AVX2 kernels were compiled out (scalar fallback) and thus UNTESTED in
 * CI. Here we exercise the real AVX2 kernels against a plain scalar
 * dequant_row()+dot ground truth.
 *
 * For every AVX2-supported quant type (Q4_0, Q4_K, Q5_K, Q6_K, Q8_0, BF16,
 * F16) we build DETERMINISTIC valid quantized rows at K=512 AND K=4096 plus
 * a deterministic input vector x, then assert:
 *
 *   actual    = qdot_row(row, x, K, type)          // AVX2 fused path
 *   reference = dequant_row(row, tmp, K, type); scalar dot(tmp, x)  // truth
 *
 * Tolerance is RELATIVE, not bit-exact: AVX2+FMA fuses/reorders the
 * multiply-adds, so rounding differs from the strictly-sequential scalar
 * reference. We reuse test_qdot.c's check:
 *     |actual - reference| <= 1e-4 * |reference| + 1e-7
 * (rel err < 1e-4 with a tiny absolute floor for near-zero results). This
 * passed on all standard types at K up to 14336 in test_qdot.c, so it is a
 * safe, already-validated bar for the AVX2 path.
 *
 * RUNTIME GUARD: if the host lacks AVX2 at runtime we SKIP (return 0) rather
 * than execute AVX2 instructions and SIGILL. (Compile-time __AVX2__ only
 * means the kernels were emitted; this guards actually running them.)
 *
 * Pure C11. No libc rand() — deterministic xorshift32, identical style to
 * test_qdot.c so the quantized bytes/scales are constructed the same way.
 */

#include "qdot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

/* ---- Test framework ---- */

static int tests_pass = 0, tests_fail = 0;
#define TEST(name) printf("Test %d: %s ... ", tests_pass + tests_fail + 1, name)
#define PASS() do { printf("PASS\n"); tests_pass++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_fail++; } while(0)

/* ---- Deterministic PRNG (xorshift32) — no libc rand() ---- */

static uint32_t xor_state;

static void xor_seed(uint32_t s) { xor_state = s ? s : 1; }

static uint32_t xor_next(void)
{
    uint32_t x = xor_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    xor_state = x;
    return x;
}

/* Random float in [-1, 1] */
static float xor_f32(void)
{
    return (float)(int32_t)xor_next() / (float)INT32_MAX;
}

/* Random byte */
static uint8_t xor_u8(void)
{
    return (uint8_t)(xor_next() & 0xFF);
}

/* Valid F16 (no NaN/Inf/denormal): exponents 1-28 */
static uint16_t xor_valid_f16(void)
{
    uint32_t r = xor_next();
    uint16_t sign = (uint16_t)((r >> 31) & 1);
    uint16_t exp  = (uint16_t)(1 + (r % 28));
    uint16_t mant = (uint16_t)(r & 0x3FF);
    return (uint16_t)((sign << 15) | (exp << 10) | mant);
}

/* ---- Reference: scalar dequant_row() then plain sequential dot ---- */

static float ref_dot(const void *row_data, const float *x, int K, ggml_type_t type)
{
    float *tmp = (float *)malloc((size_t)K * sizeof(float));
    if (!tmp) return 0.0f / 0.0f;  /* NaN — forces a visible failure on OOM */
    dequant_row(row_data, tmp, K, type);
    float dot = 0.0f;
    for (int i = 0; i < K; i++)
        dot += tmp[i] * x[i];
    free(tmp);
    return dot;
}

/* ---- Tolerance check (identical to test_qdot.c) ---- */

static int check_close(float actual, float ref, char *msg, size_t msg_sz)
{
    float diff = fabsf(actual - ref);
    float tol = 1e-4f * fabsf(ref) + 1e-7f;
    if (diff <= tol) return 1;
    snprintf(msg, msg_sz, "actual=%.8g ref=%.8g diff=%.8g tol=%.8g", actual, ref, diff, tol);
    return 0;
}

/* ---- Deterministic row fill (raw quantized bytes) ---- */

static void fill_row(uint8_t *row, size_t bytes)
{
    for (size_t i = 0; i < bytes; i++)
        row[i] = xor_u8();
}

static void fill_x(float *x, int K)
{
    for (int i = 0; i < K; i++)
        x[i] = xor_f32();
}

/* ---- Patch F16 scale fields so blockwise types stay finite ----
 * Same field offsets as test_qdot.c::fix_row_scales(). */

static void fix_row_scales(uint8_t *row, int K, ggml_type_t type)
{
    int bs = dequant_type_block_size(type);
    size_t bb = dequant_type_block_bytes(type);
    if (bs <= 0 || bb == 0) return;
    int n_blocks = K / bs;

    for (int b = 0; b < n_blocks; b++) {
        uint8_t *blk = row + (size_t)b * bb;
        uint16_t val;
        switch (type) {
        case GGML_TYPE_Q4_0:
        case GGML_TYPE_Q8_0:
            /* First 2 bytes = d (F16 scale) */
            val = xor_valid_f16();
            memcpy(blk, &val, 2);
            break;
        case GGML_TYPE_Q4_K:
        case GGML_TYPE_Q5_K:
            /* First 2 bytes = d, next 2 = dmin (both F16) */
            val = xor_valid_f16();
            memcpy(blk, &val, 2);
            val = xor_valid_f16();
            memcpy(blk + 2, &val, 2);
            break;
        case GGML_TYPE_Q6_K:
            /* d is at offset 208 (last 2 bytes of 210-byte block) */
            val = xor_valid_f16();
            memcpy(blk + 208, &val, 2);
            break;
        default:
            break;
        }
    }
}

/* ---- Block-based quant type test (Q4_0/Q4_K/Q5_K/Q6_K/Q8_0) ---- */

static void test_quant_type(const char *name, ggml_type_t type, int K, uint32_t seed)
{
    TEST(name);
    xor_seed(seed);

    size_t row_bytes = dequant_row_bytes(K, type);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    fill_row(row, row_bytes);
    fix_row_scales(row, K, type);
    fill_x(x, K);

    float actual = qdot_row(row, x, K, type);   /* AVX2 path */
    float ref = ref_dot(row, x, K, type);       /* scalar truth */

    char msg[256];
    if (check_close(actual, ref, msg, sizeof(msg)))
        PASS();
    else
        FAIL(msg);

    free(row);
    free(x);
}

/* ---- BF16: row is K * uint16 (upper 16 bits of small F32 values) ---- */

static void test_bf16(int K, uint32_t seed)
{
    char name[64];
    snprintf(name, sizeof(name), "BF16 fused vs reference (K=%d)", K);
    TEST(name);
    xor_seed(seed);

    size_t row_bytes = (size_t)K * 2;
    uint16_t *row = (uint16_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    for (int i = 0; i < K; i++) {
        float val = xor_f32() * 10.0f;  /* [-10, 10] */
        union { float f; uint32_t u; } un;
        un.f = val;
        row[i] = (uint16_t)(un.u >> 16);  /* BF16 = upper 16 bits */
    }
    fill_x(x, K);

    float actual = qdot_row(row, x, K, GGML_TYPE_BF16);
    float ref = ref_dot(row, x, K, GGML_TYPE_BF16);

    char msg[256];
    if (check_close(actual, ref, msg, sizeof(msg)))
        PASS();
    else
        FAIL(msg);

    free(row);
    free(x);
}

/* ---- F16: row is K * uint16 (valid small-magnitude half floats) ---- */

static void test_f16(int K, uint32_t seed)
{
    char name[64];
    snprintf(name, sizeof(name), "F16 fused vs reference (K=%d)", K);
    TEST(name);
    xor_seed(seed);

    size_t row_bytes = (size_t)K * 2;
    uint16_t *row = (uint16_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    /* Small-magnitude F16: exponents 12-18 -> ~0.01..16 */
    for (int i = 0; i < K; i++) {
        uint32_t r = xor_next();
        uint16_t sign = (uint16_t)((r >> 31) & 1);
        uint16_t exp = (uint16_t)(12 + (r % 7));
        uint16_t mant = (uint16_t)(r & 0x3FF);
        row[i] = (uint16_t)((sign << 15) | (exp << 10) | mant);
    }
    /* Small activations to avoid dot product overflow */
    for (int i = 0; i < K; i++)
        x[i] = xor_f32() * 0.1f;

    float actual = qdot_row(row, x, K, GGML_TYPE_F16);
    float ref = ref_dot(row, x, K, GGML_TYPE_F16);

    char msg[256];
    if (check_close(actual, ref, msg, sizeof(msg)))
        PASS();
    else
        FAIL(msg);

    free(row);
    free(x);
}

/* ---- Build a row of finite weights for a per-element type (BF16/F16) ----
 * Raw random bytes interpreted as BF16/F16 can be NaN/Inf, and NaN*0 = NaN.
 * For the zero-input edge case we need finite weights so that finite*0 == 0,
 * matching how the main BF16/F16 tests construct valid values. */

static void fill_valid_elem_row(uint16_t *row, int K, ggml_type_t type)
{
    for (int i = 0; i < K; i++) {
        if (type == GGML_TYPE_BF16) {
            float val = xor_f32() * 10.0f;
            union { float f; uint32_t u; } un;
            un.f = val;
            row[i] = (uint16_t)(un.u >> 16);
        } else { /* F16 */
            row[i] = xor_valid_f16();
        }
    }
}

/* ---- Edge case: zero input x => dot must be exactly 0 (every type) ---- */

static void test_zero_input(const char *tname, ggml_type_t type, int K, uint32_t seed)
{
    char name[96];
    snprintf(name, sizeof(name), "zero-input x -> 0 (%s, K=%d)", tname, K);
    TEST(name);
    xor_seed(seed);

    /* Row sizing: block types use dequant_row_bytes; BF16/F16 are K*2 */
    size_t row_bytes;
    if (type == GGML_TYPE_BF16 || type == GGML_TYPE_F16)
        row_bytes = (size_t)K * 2;
    else
        row_bytes = dequant_row_bytes(K, type);

    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)calloc((size_t)K, sizeof(float));  /* all zeros */
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    /* Valid finite weights — result must be exactly 0 because x == 0
     * (finite * 0 == 0; a NaN/Inf weight would propagate, so keep weights
     * finite to test the actual zero-input property). */
    if (type == GGML_TYPE_BF16 || type == GGML_TYPE_F16) {
        fill_valid_elem_row((uint16_t *)row, K, type);
    } else {
        fill_row(row, row_bytes);
        fix_row_scales(row, K, type);
    }

    float actual = qdot_row(row, x, K, type);

    if (actual == 0.0f) {
        PASS();
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "expected 0, got %.8g", actual);
        FAIL(msg);
    }

    free(row);
    free(x);
}

/* ---- Edge case: all-zero / degenerate block must stay finite ----
 * Mirrors test_qdot.c::test_zero_block (a zeroed block has zero scales,
 * exercising the AVX2 kernel's degenerate path). */

static void test_zero_block(const char *tname, ggml_type_t type, int K, uint32_t seed)
{
    char name[96];
    snprintf(name, sizeof(name), "zero/degenerate block finite (%s, K=%d)", tname, K);
    TEST(name);

    size_t row_bytes;
    if (type == GGML_TYPE_BF16 || type == GGML_TYPE_F16)
        row_bytes = (size_t)K * 2;
    else
        row_bytes = dequant_row_bytes(K, type);

    uint8_t *row = (uint8_t *)calloc(1, row_bytes);  /* zeroed weights */
    float *x = (float *)malloc((size_t)K * sizeof(float));
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    xor_seed(seed);
    fill_x(x, K);  /* non-zero activations */

    float actual = qdot_row(row, x, K, type);

    if (isfinite(actual))
        PASS();
    else {
        char msg[128];
        snprintf(msg, sizeof(msg), "result not finite: %g", actual);
        FAIL(msg);
    }

    free(row);
    free(x);
}

/* ---- Main ---- */

int main(void)
{
    /* Runtime guard: if the host has no AVX2, do NOT run AVX2 kernels.
     * (This TU is compiled -mavx2, so the kernels exist; executing them on a
     * non-AVX2 runner would SIGILL. SKIP cleanly so CI passes there.) */
    if (!__builtin_cpu_supports("avx2")) {
        printf("SKIP: no AVX2 on this host\n");
        return 0;
    }

    printf("=== JARVIS AVX2 qdot correctness tests ===\n");
    printf("(this TU compiled -mavx2 -mfma: qdot_row dispatches to AVX2 kernels)\n\n");

    /* Block-based quant types at K=512 and K=4096 */
    test_quant_type("Q4_0 AVX2 vs scalar (K=512)",  GGML_TYPE_Q4_0, 512,  0x1001);
    test_quant_type("Q4_0 AVX2 vs scalar (K=4096)", GGML_TYPE_Q4_0, 4096, 0x1002);
    test_quant_type("Q4_K AVX2 vs scalar (K=512)",  GGML_TYPE_Q4_K, 512,  0x2001);
    test_quant_type("Q4_K AVX2 vs scalar (K=4096)", GGML_TYPE_Q4_K, 4096, 0x2002);
    test_quant_type("Q5_K AVX2 vs scalar (K=512)",  GGML_TYPE_Q5_K, 512,  0x3001);
    test_quant_type("Q5_K AVX2 vs scalar (K=4096)", GGML_TYPE_Q5_K, 4096, 0x3002);
    test_quant_type("Q6_K AVX2 vs scalar (K=512)",  GGML_TYPE_Q6_K, 512,  0x4001);
    test_quant_type("Q6_K AVX2 vs scalar (K=4096)", GGML_TYPE_Q6_K, 4096, 0x4002);
    test_quant_type("Q8_0 AVX2 vs scalar (K=512)",  GGML_TYPE_Q8_0, 512,  0x5001);
    test_quant_type("Q8_0 AVX2 vs scalar (K=4096)", GGML_TYPE_Q8_0, 4096, 0x5002);

    /* Per-element types at K=512 and K=4096 */
    test_bf16(512,  0x6001);
    test_bf16(4096, 0x6002);
    test_f16(512,  0x7001);
    test_f16(4096, 0x7002);

    /* Edge case: zero input vector -> exactly 0 for all 7 types */
    test_zero_input("Q4_0", GGML_TYPE_Q4_0, 512, 0xA001);
    test_zero_input("Q4_K", GGML_TYPE_Q4_K, 512, 0xA002);
    test_zero_input("Q5_K", GGML_TYPE_Q5_K, 512, 0xA003);
    test_zero_input("Q6_K", GGML_TYPE_Q6_K, 512, 0xA004);
    test_zero_input("Q8_0", GGML_TYPE_Q8_0, 512, 0xA005);
    test_zero_input("BF16", GGML_TYPE_BF16, 512, 0xA006);
    test_zero_input("F16",  GGML_TYPE_F16,  512, 0xA007);

    /* Edge case: all-zero / degenerate weight block stays finite */
    test_zero_block("Q4_K", GGML_TYPE_Q4_K, 512, 0xB001);
    test_zero_block("Q5_K", GGML_TYPE_Q5_K, 512, 0xB002);
    test_zero_block("Q6_K", GGML_TYPE_Q6_K, 512, 0xB003);

    printf("\n=== %d PASS, %d FAIL ===\n", tests_pass, tests_fail);
    return tests_fail > 0 ? 1 : 0;
}
