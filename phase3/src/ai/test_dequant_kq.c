/*
 * JARVIS AI-OS -- Q4_K and Q6_K Dequantization Test Suite
 * Validates new K-quant dequantization routines with known block data.
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

/* ---- Helper: float to f16 ---- */

static uint16_t f32_to_f16(float f)
{
    union { float f; uint32_t u; } v = { f };
    uint32_t sign = (v.u >> 16) & 0x8000;
    int32_t  exp  = ((v.u >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = (v.u >> 13) & 0x3FF;

    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        mant = (mant | 0x400) >> (1 - exp);
        return (uint16_t)(sign | mant);
    }
    if (exp >= 31) {
        return (uint16_t)(sign | 0x7C00 | (mant ? 0x200 : 0));
    }
    return (uint16_t)(sign | ((uint32_t)exp << 10) | mant);
}

/* ---- Q4_K Tests ---- */

static void test_q4k_type_info(void)
{
    TEST("Q4_K: type_info block_bytes=144, block_size=256");
    CHECK(dequant_type_block_bytes(GGML_TYPE_Q4_K) == 144
          && dequant_type_block_size(GGML_TYPE_Q4_K) == 256,
          "Q4_K type info wrong");
}

static void test_q4k_type_name(void)
{
    TEST("Q4_K: type_name returns Q4_K");
    CHECK(strcmp(dequant_type_name(GGML_TYPE_Q4_K), "Q4_K") == 0,
          "Q4_K type name wrong");
}

static void test_q4k_all_zeros(void)
{
    TEST("Q4_K: d=1.0, dmin=0.0, scales=1, qs=0 => all zeros");
    q4_k_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = f32_to_f16(1.0f);
    blk.dmin = f32_to_f16(0.0f);
    /* Set scales: all sub-block scales = 1, all mins = 0
     * For i=0..3: scales[i] lower 6 bits = 1 => scales[i] = 0x01
     * For i=0..3: scales[i+4] lower 6 bits = 0 (min=0)
     * scales[8..11] = 0 (high bits of sc[4..7] and mn[4..7])
     * But sc[4..7] = (scales[i+8] & 0xF) | ((scales[i] >> 6) << 4)
     *              = (0 & 0xF) | ((1 >> 6) << 4) = 0 | 0 = 0
     * That would make sc[4..7] = 0, not 1. We need to also set upper nibble.
     * For sc[4..7]: sc[j+4] = (scales[j+8] & 0xF) | ((scales[j] >> 6) << 4)
     *   We need sc[4]=1: (scales[8] & 0xF) | ((scales[0] >> 6) << 4)
     *   = (scales[8] & 0xF) | 0 = scales[8] & 0xF
     *   So scales[8] low nibble = 1 => scales[8] |= 0x01
     *   Similarly scales[9] |= 0x01, scales[10] |= 0x01, scales[11] |= 0x01
     */
    blk.scales[0] = 1; blk.scales[1] = 1; blk.scales[2] = 1; blk.scales[3] = 1;
    blk.scales[4] = 0; blk.scales[5] = 0; blk.scales[6] = 0; blk.scales[7] = 0;
    blk.scales[8] = 1; blk.scales[9] = 1; blk.scales[10] = 1; blk.scales[11] = 1;
    /* qs all zero => all nibbles = 0 => d_sc * 0 - 0 = 0 */

    float out[256];
    int rc = dequant_row(&blk, out, 256, GGML_TYPE_Q4_K);
    int ok = (rc == 0);
    for (int i = 0; i < 256 && ok; i++) {
        if (out[i] != 0.0f) { ok = 0; printf("(idx %d = %f) ", i, out[i]); }
    }
    CHECK(ok, "expected all zeros");
}

static void test_q4k_all_ones(void)
{
    TEST("Q4_K: d=1.0, dmin=0.0, scales=1, qs=0x11 => all 1.0");
    q4_k_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = f32_to_f16(1.0f);
    blk.dmin = f32_to_f16(0.0f);
    /* scales: sc[j]=1, mn[j]=0 for all j */
    blk.scales[0] = 1; blk.scales[1] = 1; blk.scales[2] = 1; blk.scales[3] = 1;
    blk.scales[4] = 0; blk.scales[5] = 0; blk.scales[6] = 0; blk.scales[7] = 0;
    blk.scales[8] = 1; blk.scales[9] = 1; blk.scales[10] = 1; blk.scales[11] = 1;
    /* qs all 0x11: lower nibble = 1, upper nibble = 1
     * For k < 16: q = qs[j*16+k] & 0xF = 1
     * For k >= 16: q = qs[j*16+(k-16)] >> 4 = 1
     * Result: d_sc * 1 - d_min = 1.0 * 1 * 1 - 0 = 1.0 */
    memset(blk.qs, 0x11, 128);

    float out[256];
    int rc = dequant_row(&blk, out, 256, GGML_TYPE_Q4_K);
    int ok = (rc == 0);
    for (int i = 0; i < 256 && ok; i++) {
        if (!CLOSE(out[i], 1.0f)) { ok = 0; printf("(idx %d = %f) ", i, out[i]); }
    }
    CHECK(ok, "expected all 1.0");
}

static void test_q4k_with_dmin(void)
{
    TEST("Q4_K: d=2.0, dmin=0.5, sc=1, mn=2, qs=0x33 => 2*1*3 - 0.5*2 = 5.0");
    q4_k_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = f32_to_f16(2.0f);
    blk.dmin = f32_to_f16(0.5f);
    /* sc[0..3]=1 via scales[0..3] lower 6 bits */
    blk.scales[0] = 1; blk.scales[1] = 1; blk.scales[2] = 1; blk.scales[3] = 1;
    /* mn[0..3]=2 via scales[4..7] lower 6 bits */
    blk.scales[4] = 2; blk.scales[5] = 2; blk.scales[6] = 2; blk.scales[7] = 2;
    /* sc[4..7]=1 via scales[8..11] low nibble */
    blk.scales[8] = 1; blk.scales[9] = 1; blk.scales[10] = 1; blk.scales[11] = 1;
    /* mn[4..7]=2 via scales[8..11] high nibble. mn[4+i] = (scales[i+8] >> 4) | ((scales[i+4] >> 6) << 4)
     * = (scales[i+8] >> 4) | 0. So scales[i+8] high nibble = 2 => scales[i+8] |= 0x20 */
    blk.scales[8] |= 0x20; blk.scales[9] |= 0x20;
    blk.scales[10] |= 0x20; blk.scales[11] |= 0x20;
    /* qs = 0x33 => low nibble=3, high nibble=3 */
    memset(blk.qs, 0x33, 128);

    /* Expected: d * sc * q - dmin * mn = 2.0 * 1 * 3 - 0.5 * 2 = 6.0 - 1.0 = 5.0 */
    float out[256];
    int rc = dequant_row(&blk, out, 256, GGML_TYPE_Q4_K);
    int ok = (rc == 0);
    for (int i = 0; i < 256 && ok; i++) {
        if (!CLOSE(out[i], 5.0f)) { ok = 0; printf("(idx %d = %f) ", i, out[i]); }
    }
    CHECK(ok, "expected all 5.0");
}

static void test_q4k_row_bytes(void)
{
    TEST("Q4_K: row_bytes 256 values = 1 block = 144 bytes");
    CHECK(dequant_row_bytes(256, GGML_TYPE_Q4_K) == 144, "wrong row bytes");
}

static void test_q4k_bad_count(void)
{
    TEST("Q4_K: n_values=100 (not multiple of 256) => error -2");
    float dummy[256];
    q4_k_block_t blk;
    memset(&blk, 0, sizeof(blk));
    int rc = dequant_row(&blk, dummy, 100, GGML_TYPE_Q4_K);
    CHECK(rc == -2, "expected -2");
}

/* ---- Q6_K Tests ---- */

static void test_q6k_type_info(void)
{
    TEST("Q6_K: type_info block_bytes=210, block_size=256");
    CHECK(dequant_type_block_bytes(GGML_TYPE_Q6_K) == 210
          && dequant_type_block_size(GGML_TYPE_Q6_K) == 256,
          "Q6_K type info wrong");
}

static void test_q6k_type_name(void)
{
    TEST("Q6_K: type_name returns Q6_K");
    CHECK(strcmp(dequant_type_name(GGML_TYPE_Q6_K), "Q6_K") == 0,
          "Q6_K type name wrong");
}

static void test_q6k_all_zeros(void)
{
    TEST("Q6_K: d=1.0, scales=1, all q=32 (maps to 0) => all zeros");
    q6_k_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = f32_to_f16(1.0f);
    /* Set all scales to 1 */
    for (int i = 0; i < 16; i++) blk.scales[i] = 1;
    /* For value j to be 0: (ql_val | (qh_val << 4)) - 32 = 0
     * => ql_val | (qh_val << 4) = 32 = 0b100000
     * => qh_val = 2 (bits 5:4), ql_val = 0 (bits 3:0)
     * So ql[j/2] has both nibbles = 0 => ql all 0x00
     * qh[j/4] has all 2-bit fields = 2 => 0b10101010 = 0xAA */
    memset(blk.ql, 0x00, 128);
    memset(blk.qh, 0xAA, 64);

    float out[256];
    int rc = dequant_row(&blk, out, 256, GGML_TYPE_Q6_K);
    int ok = (rc == 0);
    for (int i = 0; i < 256 && ok; i++) {
        if (!CLOSE(out[i], 0.0f)) { ok = 0; printf("(idx %d = %f) ", i, out[i]); }
    }
    CHECK(ok, "expected all zeros");
}

static void test_q6k_known_value(void)
{
    TEST("Q6_K: known single value at j=0");
    q6_k_block_t blk;
    memset(&blk, 0, sizeof(blk));
    blk.d = f32_to_f16(2.0f);
    blk.scales[0] = 3;  /* scale for sub-block 0 (values 0..15) */

    /* Value j=0: ql[0] low nibble = 5, qh[0] low 2 bits = 1
     * q6 = 5 | (1 << 4) = 5 + 16 = 21, signed = 21 - 32 = -11
     * result = 2.0 * 3 * (-11) = -66.0 */
    blk.ql[0] = 0x05;  /* j=0: low nibble=5, j=1: low nibble=0 */
    blk.qh[0] = 0x01;  /* j=0: 2-bit=1, j=1: 2-bit=0, j=2: 0, j=3: 0 */

    float out[256];
    int rc = dequant_row(&blk, out, 256, GGML_TYPE_Q6_K);
    int ok = (rc == 0);
    /* j=0: q = (5 | (1<<4)) - 32 = 21 - 32 = -11, result = 2.0 * 3 * (-11) = -66.0 */
    if (!CLOSE(out[0], -66.0f)) {
        ok = 0;
        printf("(idx 0 = %f, expected -66.0) ", out[0]);
    }
    CHECK(ok, "known value mismatch");
}

static void test_q6k_row_bytes(void)
{
    TEST("Q6_K: row_bytes 512 values = 2 blocks = 420 bytes");
    CHECK(dequant_row_bytes(512, GGML_TYPE_Q6_K) == 420, "wrong row bytes");
}

static void test_q6k_bad_count(void)
{
    TEST("Q6_K: n_values=100 (not multiple of 256) => error -2");
    float dummy[256];
    q6_k_block_t blk;
    memset(&blk, 0, sizeof(blk));
    int rc = dequant_row(&blk, dummy, 100, GGML_TYPE_Q6_K);
    CHECK(rc == -2, "expected -2");
}

/* ---- Main ---- */

int main(void)
{
    printf("=== Q4_K / Q6_K Dequantization Test Suite ===\n\n");

    /* Q4_K (7 tests) */
    test_q4k_type_info();
    test_q4k_type_name();
    test_q4k_all_zeros();
    test_q4k_all_ones();
    test_q4k_with_dmin();
    test_q4k_row_bytes();
    test_q4k_bad_count();

    /* Q6_K (5 tests) */
    test_q6k_type_info();
    test_q6k_type_name();
    test_q6k_all_zeros();
    test_q6k_known_value();
    test_q6k_row_bytes();
    test_q6k_bad_count();

    printf("\n=== Results: %d/%d PASS, %d FAIL ===\n", pass, total, fail);
    return fail ? 1 : 0;
}
