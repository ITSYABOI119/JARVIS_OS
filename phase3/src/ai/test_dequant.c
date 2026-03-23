/*
 * JARVIS AI-OS — Dequantization Test Suite
 * Verifies F16-to-F32, Q4_0, Q8_0, F32 passthrough, type info, and error handling.
 */

#include <stdio.h>
#include <math.h>
#include <string.h>
#include "dequant.h"

static int pass = 0, fail = 0, total = 0;
#define EPS 1e-3f
#define CLOSE(a, b) (fabsf((a) - (b)) < EPS)

#define TEST(name) do { total++; printf("Test %d: %s ... ", total, name); } while(0)
#define PASS() do { pass++; printf("PASS\n"); } while(0)
#define FAIL_MSG(msg) do { fail++; printf("FAIL: %s\n", msg); } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL_MSG(msg); } while(0)

/* ---- Helper: float to f16 (for building test data) ---- */

static uint16_t f32_to_f16(float f)
{
    union { float f; uint32_t u; } v = { f };
    uint32_t sign = (v.u >> 16) & 0x8000;
    int32_t  exp  = ((v.u >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = (v.u >> 13) & 0x3FF;

    if (exp <= 0) {
        /* Denorm or zero */
        if (exp < -10) return (uint16_t)sign;  /* too small, flush to zero */
        mant = (mant | 0x400) >> (1 - exp);
        return (uint16_t)(sign | mant);
    }
    if (exp >= 31) {
        /* Inf/NaN */
        return (uint16_t)(sign | 0x7C00 | (mant ? 0x200 : 0));
    }
    return (uint16_t)(sign | ((uint32_t)exp << 10) | mant);
}

/* ---- F16-to-F32 tests ---- */

static void test_f16_zero(void)
{
    TEST("f16_to_f32: +0.0");
    float r = f16_to_f32(0x0000);
    CHECK(r == 0.0f, "expected 0.0");
}

static void test_f16_one(void)
{
    TEST("f16_to_f32: 1.0");
    /* F16 1.0 = 0x3C00 (sign=0, exp=15, mant=0) */
    float r = f16_to_f32(0x3C00);
    CHECK(CLOSE(r, 1.0f), "expected 1.0");
}

static void test_f16_two(void)
{
    TEST("f16_to_f32: 2.0");
    /* F16 2.0 = 0x4000 (sign=0, exp=16, mant=0) */
    float r = f16_to_f32(0x4000);
    CHECK(CLOSE(r, 2.0f), "expected 2.0");
}

static void test_f16_neg_two(void)
{
    TEST("f16_to_f32: -2.0");
    /* F16 -2.0 = 0xC000 (sign=1, exp=16, mant=0) */
    float r = f16_to_f32(0xC000);
    CHECK(CLOSE(r, -2.0f), "expected -2.0");
}

static void test_f16_half(void)
{
    TEST("f16_to_f32: 0.5");
    /* F16 0.5 = 0x3800 (sign=0, exp=14, mant=0) */
    float r = f16_to_f32(0x3800);
    CHECK(CLOSE(r, 0.5f), "expected 0.5");
}

static void test_f16_pos_inf(void)
{
    TEST("f16_to_f32: +inf");
    /* F16 +inf = 0x7C00 */
    float r = f16_to_f32(0x7C00);
    CHECK(isinf(r) && r > 0, "expected +inf");
}

static void test_f16_neg_inf(void)
{
    TEST("f16_to_f32: -inf");
    /* F16 -inf = 0xFC00 */
    float r = f16_to_f32(0xFC00);
    CHECK(isinf(r) && r < 0, "expected -inf");
}

/* ---- Q4_0 tests ---- */

static void test_q4_0_zeros(void)
{
    TEST("Q4_0: scale=0 yields all zeros");
    q4_0_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = 0x0000;  /* scale = 0 */
    /* qs all zero: nibble values are 0, dequant = (0 - 8) * 0 = 0 */
    float out[32];
    int rc = dequant_row(&blk, out, 32, GGML_TYPE_Q4_0);
    int ok = (rc == 0);
    for (int i = 0; i < 32 && ok; i++)
        if (out[i] != 0.0f) ok = 0;
    CHECK(ok, "expected all zeros");
}

static void test_q4_0_scale_one(void)
{
    TEST("Q4_0: scale=1.0, nibbles check");
    q4_0_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = 0x3C00;  /* scale = 1.0 */
    /* qs[0] = 0x88 -> low nibble=8, high nibble=8
     * dequant: (8-8)*1=0 and (8-8)*1=0 */
    blk.qs[0] = 0x88;
    /* qs[1] = 0x9A -> low=0xA=10, high=0x9=9
     * dequant: (10-8)*1=2 and (9-8)*1=1 */
    blk.qs[1] = 0x9A;
    float out[32];
    int rc = dequant_row(&blk, out, 32, GGML_TYPE_Q4_0);
    CHECK(rc == 0 && CLOSE(out[0], 0.0f) && CLOSE(out[1], 2.0f)
          && CLOSE(out[16], 0.0f) && CLOSE(out[17], 1.0f),
          "nibble decode wrong");
}

static void test_q4_0_two_blocks(void)
{
    TEST("Q4_0: two blocks (64 values)");
    q4_0_block_t blks[2];
    memset(blks, 0, sizeof(blks));
    blks[0].d = 0x3C00;  /* scale = 1.0 */
    blks[1].d = 0x4000;  /* scale = 2.0 */
    /* blk0 qs[0] = 0x08 -> low=8(->0), high=0(-8) => out[0]=0, out[16]=-8 */
    blks[0].qs[0] = 0x08;
    /* blk1 qs[0] = 0x09 -> low=9(->1), high=0(-8) => out[32]=1*2=2, out[48]=-8*2=-16 */
    blks[1].qs[0] = 0x09;
    float out[64];
    int rc = dequant_row(blks, out, 64, GGML_TYPE_Q4_0);
    CHECK(rc == 0 && CLOSE(out[0], 0.0f) && CLOSE(out[16], -8.0f)
          && CLOSE(out[32], 2.0f) && CLOSE(out[48], -16.0f),
          "two-block decode wrong");
}

/* ---- Q8_0 tests ---- */

static void test_q8_0_zeros(void)
{
    TEST("Q8_0: scale=0 yields all zeros");
    q8_0_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = 0x0000;
    for (int i = 0; i < 32; i++) blk.qs[i] = (int8_t)(i - 16);
    float out[32];
    int rc = dequant_row(&blk, out, 32, GGML_TYPE_Q8_0);
    int ok = (rc == 0);
    for (int i = 0; i < 32 && ok; i++)
        if (out[i] != 0.0f) ok = 0;
    CHECK(ok, "expected all zeros when scale=0");
}

static void test_q8_0_scale_two(void)
{
    TEST("Q8_0: scale=2.0, values [-3..3]");
    q8_0_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = 0x4000;  /* scale = 2.0 */
    blk.qs[0] = -3;
    blk.qs[1] = -1;
    blk.qs[2] = 0;
    blk.qs[3] = 1;
    blk.qs[4] = 3;
    float out[32];
    int rc = dequant_row(&blk, out, 32, GGML_TYPE_Q8_0);
    CHECK(rc == 0 && CLOSE(out[0], -6.0f) && CLOSE(out[1], -2.0f)
          && CLOSE(out[2], 0.0f) && CLOSE(out[3], 2.0f) && CLOSE(out[4], 6.0f),
          "Q8_0 scale*value wrong");
}

/* ---- F32 passthrough test ---- */

static void test_f32_passthrough(void)
{
    TEST("F32: passthrough copies data unchanged");
    float src[] = {1.5f, -2.25f, 0.0f, 3.14159f};
    float dst[4];
    int rc = dequant_row(src, dst, 4, GGML_TYPE_F32);
    CHECK(rc == 0 && CLOSE(dst[0], 1.5f) && CLOSE(dst[1], -2.25f)
          && CLOSE(dst[2], 0.0f) && CLOSE(dst[3], 3.14159f),
          "F32 passthrough mismatch");
}

/* ---- F16 array dequant test ---- */

static void test_f16_array(void)
{
    TEST("F16: array of 4 values via dequant_row");
    uint16_t src[4];
    src[0] = f32_to_f16(1.0f);
    src[1] = f32_to_f16(-0.5f);
    src[2] = f32_to_f16(0.0f);
    src[3] = f32_to_f16(3.0f);
    float dst[4];
    int rc = dequant_row(src, dst, 4, GGML_TYPE_F16);
    CHECK(rc == 0 && CLOSE(dst[0], 1.0f) && CLOSE(dst[1], -0.5f)
          && CLOSE(dst[2], 0.0f) && CLOSE(dst[3], 3.0f),
          "F16 array dequant wrong");
}

/* ---- Type info tests ---- */

static void test_type_info_q4_0(void)
{
    TEST("type_info: Q4_0 block_bytes=18, block_size=32");
    CHECK(dequant_type_block_bytes(GGML_TYPE_Q4_0) == 18
          && dequant_type_block_size(GGML_TYPE_Q4_0) == 32,
          "Q4_0 type info wrong");
}

static void test_type_info_q8_0(void)
{
    TEST("type_info: Q8_0 block_bytes=34, block_size=32");
    CHECK(dequant_type_block_bytes(GGML_TYPE_Q8_0) == 34
          && dequant_type_block_size(GGML_TYPE_Q8_0) == 32,
          "Q8_0 type info wrong");
}

static void test_type_info_f16(void)
{
    TEST("type_info: F16 block_bytes=2, block_size=1");
    CHECK(dequant_type_block_bytes(GGML_TYPE_F16) == 2
          && dequant_type_block_size(GGML_TYPE_F16) == 1,
          "F16 type info wrong");
}

static void test_type_info_f32(void)
{
    TEST("type_info: F32 block_bytes=4, block_size=1");
    CHECK(dequant_type_block_bytes(GGML_TYPE_F32) == 4
          && dequant_type_block_size(GGML_TYPE_F32) == 1,
          "F32 type info wrong");
}

/* ---- Row bytes test ---- */

static void test_row_bytes(void)
{
    TEST("row_bytes: 64 Q4_0 values = 2*18 = 36 bytes");
    CHECK(dequant_row_bytes(64, GGML_TYPE_Q4_0) == 36, "Q4_0 row bytes wrong");
}

/* ---- Type name test ---- */

static void test_type_name(void)
{
    TEST("type_name: Q4_0, Q8_0, F16, F32, unknown");
    int ok = 1;
    if (strcmp(dequant_type_name(GGML_TYPE_Q4_0), "Q4_0") != 0) ok = 0;
    if (strcmp(dequant_type_name(GGML_TYPE_Q8_0), "Q8_0") != 0) ok = 0;
    if (strcmp(dequant_type_name(GGML_TYPE_F16),  "F16")  != 0) ok = 0;
    if (strcmp(dequant_type_name(GGML_TYPE_F32),  "F32")  != 0) ok = 0;
    if (strcmp(dequant_type_name(GGML_TYPE_Q4_1), "UNKNOWN") != 0) ok = 0;
    CHECK(ok, "type name mismatch");
}

/* ---- Error handling tests ---- */

static void test_unsupported_type(void)
{
    TEST("error: unsupported type returns -1");
    float dummy[32];
    int rc = dequant_row(dummy, dummy, 32, GGML_TYPE_Q4_1);
    CHECK(rc == -1, "expected -1 for unsupported type");
}

static void test_bad_count_q4_0(void)
{
    TEST("error: Q4_0 with n_values=31 returns -2");
    float dummy[32];
    q4_0_block_t blk;
    memset(&blk, 0, sizeof(blk));
    int rc = dequant_row(&blk, dummy, 31, GGML_TYPE_Q4_0);
    CHECK(rc == -2, "expected -2 for non-multiple of block_size");
}

static void test_bad_count_zero(void)
{
    TEST("error: n_values=0 returns -2");
    float dummy[1];
    int rc = dequant_row(dummy, dummy, 0, GGML_TYPE_F32);
    CHECK(rc == -2, "expected -2 for n_values=0");
}

/* ---- Main ---- */

int main(void)
{
    printf("=== Dequantization Test Suite ===\n\n");

    /* F16-to-F32 (7 tests) */
    test_f16_zero();
    test_f16_one();
    test_f16_two();
    test_f16_neg_two();
    test_f16_half();
    test_f16_pos_inf();
    test_f16_neg_inf();

    /* Q4_0 (3 tests) */
    test_q4_0_zeros();
    test_q4_0_scale_one();
    test_q4_0_two_blocks();

    /* Q8_0 (2 tests) */
    test_q8_0_zeros();
    test_q8_0_scale_two();

    /* F32 passthrough (1 test) */
    test_f32_passthrough();

    /* F16 array (1 test) */
    test_f16_array();

    /* Type info (4 tests) */
    test_type_info_q4_0();
    test_type_info_q8_0();
    test_type_info_f16();
    test_type_info_f32();

    /* Row bytes (1 test) */
    test_row_bytes();

    /* Type name (1 test) */
    test_type_name();

    /* Error handling (3 tests) */
    test_unsupported_type();
    test_bad_count_q4_0();
    test_bad_count_zero();

    printf("\n=== Results: %d/%d PASS, %d FAIL ===\n", pass, total, fail);
    return fail ? 1 : 0;
}
