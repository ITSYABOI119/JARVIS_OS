/*
 * JARVIS AI-OS Phase 3 — Fused Quantized Dot Product Tests
 *
 * 9 correctness tests: qdot_row() vs dequant_row() + manual dot reference.
 * Covers all supported quant types + edge cases.
 */

#include "qdot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- Test framework ---- */

static int tests_pass = 0, tests_fail = 0;
#define TEST(name) printf("Test %d: %s ... ", tests_pass + tests_fail + 1, name)
#define PASS() do { printf("PASS\n"); tests_pass++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_fail++; } while(0)

/* ---- Deterministic PRNG (xorshift32) ---- */

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

/* ---- Reference: dequant then dot ---- */

static float ref_dot(const void *row_data, const float *x, int K, ggml_type_t type)
{
    float *tmp = (float *)malloc((size_t)K * sizeof(float));
    if (!tmp) return 0.0f;
    dequant_row(row_data, tmp, K, type);
    float dot = 0.0f;
    for (int i = 0; i < K; i++)
        dot += tmp[i] * x[i];
    free(tmp);
    return dot;
}

/* ---- Check: |fused - ref| < 1e-4 * |ref| + 1e-7 ---- */

static int check_close(float fused, float ref, char *msg, size_t msg_sz)
{
    float diff = fabsf(fused - ref);
    float tol = 1e-4f * fabsf(ref) + 1e-7f;
    if (diff <= tol) return 1;
    snprintf(msg, msg_sz, "fused=%.8g ref=%.8g diff=%.8g tol=%.8g", fused, ref, diff, tol);
    return 0;
}

/* ---- Fill random quantized row data ---- */

static void fill_random_row(uint8_t *row, size_t bytes)
{
    for (size_t i = 0; i < bytes; i++)
        row[i] = xor_u8();
}

/* ---- Fill random F32 activation vector ---- */

static void fill_random_x(float *x, int K)
{
    for (int i = 0; i < K; i++)
        x[i] = xor_f32();
}

/* ---- Generate a valid random F16 (no NaN/Inf) ---- */

static uint16_t xor_valid_f16(void)
{
    uint32_t r = xor_next();
    uint16_t sign = (uint16_t)((r >> 31) & 1);
    uint16_t exp  = (uint16_t)(1 + (r % 28));  /* exponents 1-28, avoids 0 (denorm) and 31 (NaN/Inf) */
    uint16_t mant = (uint16_t)(r & 0x3FF);
    return (uint16_t)((sign << 15) | (exp << 10) | mant);
}

/* ---- Patch F16 scale fields in quantized row to avoid NaN propagation ---- */

static void fix_row_scales(uint8_t *row, int K, ggml_type_t type)
{
    int bs = dequant_type_block_size(type);
    size_t bb = dequant_type_block_bytes(type);
    if (bs <= 0 || bb == 0) return;
    int n_blocks = K / bs;

    for (int b = 0; b < n_blocks; b++) {
        uint8_t *blk = row + b * bb;
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

/* ---- Generic quant type test ---- */

static void test_quant_type(const char *name, ggml_type_t type, int K, uint32_t seed)
{
    TEST(name);
    xor_seed(seed);

    size_t row_bytes = dequant_row_bytes(K, type);
    uint8_t *row = (uint8_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    fill_random_row(row, row_bytes);
    fix_row_scales(row, K, type);
    fill_random_x(x, K);

    float fused = qdot_row(row, x, K, type);
    float ref = ref_dot(row, x, K, type);

    char msg[256];
    if (check_close(fused, ref, msg, sizeof(msg)))
        PASS();
    else
        FAIL(msg);

    free(row);
    free(x);
}

/* ---- Test 6: BF16 (generate valid BF16 values) ---- */

static void test_bf16(void)
{
    TEST("BF16 fused vs reference (K=2048)");
    xor_seed(600);

    int K = 2048;
    size_t row_bytes = (size_t)K * 2;  /* 2 bytes per BF16 */
    uint16_t *row = (uint16_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    /* Generate valid BF16: upper 16 bits of small F32 values */
    for (int i = 0; i < K; i++) {
        float val = xor_f32() * 10.0f;  /* [-10, 10] */
        union { float f; uint32_t u; } un;
        un.f = val;
        row[i] = (uint16_t)(un.u >> 16);  /* BF16 = upper 16 bits */
    }
    fill_random_x(x, K);

    float fused = qdot_row(row, x, K, GGML_TYPE_BF16);
    float ref = ref_dot(row, x, K, GGML_TYPE_BF16);

    char msg[256];
    if (check_close(fused, ref, msg, sizeof(msg)))
        PASS();
    else
        FAIL(msg);

    free(row);
    free(x);
}

/* ---- Test 7: F16 (generate valid F16 via bit packing, small values) ---- */

static void test_f16(void)
{
    TEST("F16 fused vs reference (K=256)");
    xor_seed(700);

    int K = 256;
    size_t row_bytes = (size_t)K * 2;  /* 2 bytes per F16 */
    uint16_t *row = (uint16_t *)malloc(row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    /* Generate valid F16: sign(1) + exp(5) + mant(10), small exponent to avoid overflow */
    for (int i = 0; i < K; i++) {
        uint32_t r = xor_next();
        uint16_t sign = (r >> 31) & 1;
        uint16_t exp = 12 + (r % 7);  /* exponents 12-18 -> small values around 0.01-16 */
        uint16_t mant = (uint16_t)(r & 0x3FF);
        row[i] = (uint16_t)((sign << 15) | (exp << 10) | mant);
    }

    /* Small activations to avoid dot product overflow */
    for (int i = 0; i < K; i++)
        x[i] = xor_f32() * 0.1f;

    float fused = qdot_row(row, x, K, GGML_TYPE_F16);
    float ref = ref_dot(row, x, K, GGML_TYPE_F16);

    char msg[256];
    if (check_close(fused, ref, msg, sizeof(msg)))
        PASS();
    else
        FAIL(msg);

    free(row);
    free(x);
}

/* ---- Test 9: Q4_K all-zero block (crash guard) ---- */

static void test_zero_block(void)
{
    TEST("Q4_K all-zero block (crash guard)");

    int K = 256;  /* one Q4_K block */
    size_t row_bytes = dequant_row_bytes(K, GGML_TYPE_Q4_K);
    uint8_t *row = (uint8_t *)calloc(1, row_bytes);
    float *x = (float *)malloc((size_t)K * sizeof(float));
    if (!row || !x) { FAIL("malloc"); free(row); free(x); return; }

    /* Non-zero activations */
    xor_seed(900);
    fill_random_x(x, K);

    float fused = qdot_row(row, x, K, GGML_TYPE_Q4_K);

    if (isfinite(fused))
        PASS();
    else {
        char msg[128];
        snprintf(msg, sizeof(msg), "result not finite: %g", fused);
        FAIL(msg);
    }

    free(row);
    free(x);
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS qdot correctness tests ===\n\n");

    /* Tests 1-5: standard quant types at K=2048 */
    test_quant_type("Q4_K fused vs reference (K=2048)", GGML_TYPE_Q4_K, 2048, 100);
    test_quant_type("Q6_K fused vs reference (K=2048)", GGML_TYPE_Q6_K, 2048, 200);
    test_quant_type("Q8_0 fused vs reference (K=2048)", GGML_TYPE_Q8_0, 2048, 300);
    test_quant_type("Q5_K fused vs reference (K=2048)", GGML_TYPE_Q5_K, 2048, 400);
    test_quant_type("Q4_0 fused vs reference (K=2048)", GGML_TYPE_Q4_0, 2048, 500);

    /* Test 6: BF16 with valid values */
    test_bf16();

    /* Test 7: F16 with valid values */
    test_f16();

    /* Test 8: Q4_K large K (Llama 8B hidden_dim) */
    test_quant_type("Q4_K large K=14336 (Llama 8B hidden_dim)", GGML_TYPE_Q4_K, 14336, 800);

    /* Test 9: all-zero block crash guard */
    test_zero_block();

    printf("\n=== Results: %d PASS, %d FAIL ===\n", tests_pass, tests_fail);
    return tests_fail > 0 ? 1 : 0;
}
