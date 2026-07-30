/*
 * JARVIS AI-OS — embed_mu.h compiler round-trip tests (Phase C / C/M2b prerequisite)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_embed_mu.c -o /tmp/test_embed_mu -lm
 *
 * WHAT THIS TESTS, AND WHAT IT DELIBERATELY DOES NOT
 * --------------------------------------------------
 * The bit-exactness of embed_mu.h AGAINST THE npz is proven by
 * phase3/scripts/embed/test_embed_mu.py, which reads golden_vectors.npz's raw
 * bytes and the header's literals and compares them as bit patterns. That is the
 * anchor, and it is not repeated here — reading an .npz from C is not reasonable.
 *
 * This file tests the OTHER half of the bit-exactness risk, which only C can
 * reach: that the COMPILER parses those hex float literals back to exactly the
 * intended bits. Hex literals are exact by construction, but "by construction"
 * is an argument, and an argument is not a test.
 *
 * The anchor for that check is EMBED_MU_XOR, which the generator computes from
 * the npz and which the Python test verifies against BOTH the npz and the
 * header's own literals. So this file's chain of custody runs
 * compiled array -> EMBED_MU_XOR -> (CI-verified) -> npz, never
 * header -> something derived from the header.
 *
 * C3 is the control that makes C2 meaningful: a checksum comparison that always
 * passes is indistinguishable from one that works, so a single ULP is injected
 * and the comparison must reject it.
 */

#include "embed_mu.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define PASS(name) do { \
    printf("  PASS: %s\n", name); \
    tests_passed++; \
} while (0)

/* IEEE-754 binary32 bit pattern of a float. memcpy, not a pointer cast — a cast
 * would be a strict-aliasing violation and -O2 is entitled to miscompile it. */
static uint32_t bits_of(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static float float_of(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static uint32_t xor_over(const float *v, int n)
{
    uint32_t x = 0;
    for (int i = 0; i < n; i++)
        x ^= bits_of(v[i]);
    return x;
}

/* ================================================================
 * C1: geometry — length, and that it tracks the transport dimension
 * ================================================================ */
static void test_geometry(void)
{
    ASSERT(sizeof(EMBED_MU) / sizeof(EMBED_MU[0]) == EMBED_MU_DIM, "array length");
    ASSERT(EMBED_MU_DIM == 1024, "mu is a 1024-dimension artifact");
    /* mu is applied BEFORE Matryoshka truncation, so it lives in the transport's
     * space (EMBED_DIM 1024), not the store's (G3_SEM_DIM_DEFAULT 128). The
     * header static-asserts this; assert it at runtime too so the intent is
     * visible in the test output and not only in a compile that already happened. */
    ASSERT(EMBED_MU_DIM == (int)EMBED_DIM, "mu dimension == transport EMBED_DIM");
    ASSERT(sizeof(EMBED_MU) == 4096, "the array is exactly 4096 bytes");
    PASS("C1 geometry (1024 floats, 4096 B, tracks EMBED_DIM not EMBED_STORE_DIM)");
}

/* ================================================================
 * C2: THE COMPILER ROUND-TRIP.
 *
 * Recompute the XOR of the bit patterns the compiler actually produced and
 * compare against the value the generator computed from the npz. If gcc parsed
 * any one of the 1024 hex literals to a neighbouring float, this fails.
 * ================================================================ */
static void test_compiler_roundtrip(void)
{
    uint32_t x = xor_over(EMBED_MU, EMBED_MU_DIM);
    if (x != EMBED_MU_XOR)
        printf("    compiled XOR 0x%08X vs EMBED_MU_XOR 0x%08X\n",
               (unsigned)x, (unsigned)EMBED_MU_XOR);
    ASSERT(x == EMBED_MU_XOR, "compiled bit patterns match the generator's XOR");

    /* An independent tripwire that does not go through the XOR at all: the first
     * element's bit pattern, read out of golden_vectors.npz's mu.npy. Catches
     * gross truncation or a reordered array, which a XOR is insensitive to. */
    ASSERT(bits_of(EMBED_MU[0]) == 0xBCCD7CC2u, "EMBED_MU[0] bit pattern (npz-derived)");
    PASS("C2 compiler parsed all 1024 hex literals to the intended bits");
}

/* ================================================================
 * C3: the ULP control — proves C2 can fail.
 * ================================================================ */
static void test_ulp_control(void)
{
    static float copy[EMBED_MU_DIM];
    memcpy(copy, EMBED_MU, sizeof(copy));
    ASSERT(xor_over(copy, EMBED_MU_DIM) == EMBED_MU_XOR, "the copy starts clean");

    const int idx = 500;
    uint32_t orig = bits_of(copy[idx]);
    copy[idx] = float_of(orig + 1u);            /* exactly one ULP */
    ASSERT(bits_of(copy[idx]) == orig + 1u, "the perturbation is exactly 1 ULP");
    ASSERT(copy[idx] != EMBED_MU[idx], "1 ULP is a real, non-zero change");
    ASSERT(xor_over(copy, EMBED_MU_DIM) != EMBED_MU_XOR,
           "TEETH: the XOR comparison REJECTS a 1-ULP perturbation");

    copy[idx] = EMBED_MU[idx];                  /* restore */
    ASSERT(xor_over(copy, EMBED_MU_DIM) == EMBED_MU_XOR,
           "restoring makes it match again (the checksum, not luck)");
    printf("    idx %d: 0x%08X -> 0x%08X rejected\n", idx, (unsigned)orig, (unsigned)(orig + 1u));
    PASS("C3 ULP control (a single-ULP change is detected)");
}

/* ================================================================
 * C4: mu is a DIRECTION — finite, unit to within 1e-6, and NOT exactly unit.
 *
 * The last part is the load-bearing one: it is why renormalisation at use is a
 * real instruction and not a no-op to be optimised away.
 * ================================================================ */
static void test_is_a_direction(void)
{
    double n2 = 0.0;
    for (int i = 0; i < EMBED_MU_DIM; i++) {
        ASSERT(isfinite(EMBED_MU[i]), "every element is finite (no NaN/Inf)");
        n2 += (double)EMBED_MU[i] * (double)EMBED_MU[i];
    }
    ASSERT(fabs(n2 - 1.0) < 1e-6, "||mu||^2 is within 1e-6 of 1 (it IS a direction)");
    ASSERT(n2 != 1.0, "but NOT exactly 1 — renormalisation is not vacuous");

    /* Not all zero, and not all the same value — a garbled or memset header would
     * otherwise satisfy a norm check by accident. */
    int nonzero = 0, distinct = 0;
    for (int i = 0; i < EMBED_MU_DIM; i++) {
        if (EMBED_MU[i] != 0.0f) nonzero++;
        if (EMBED_MU[i] != EMBED_MU[0]) distinct++;
    }
    ASSERT(nonzero == EMBED_MU_DIM, "no zero elements");
    ASSERT(distinct > EMBED_MU_DIM / 2, "the values are not a repeated constant");
    printf("    ||mu||^2 = %.17g (double accumulation)\n", n2);
    PASS("C4 mu is a finite, non-degenerate, almost-unit direction");
}

/* ================================================================
 * C5: THE PRECISION CONTRACT for C/M2b.
 *
 * cm2_floor.py renormalises before projecting, so the 0.55 floor was measured
 * with mu/||mu||. Whether that division changes any bits depends on how the norm
 * is accumulated, and BOTH branches are exercised here so the contract is a
 * checked property rather than a comment:
 *
 *   double accumulation  -> rounds to exactly 1.0f -> no-op
 *   float  accumulation  -> not 1.0f              -> every element moves 1 ULP
 *
 * numpy accumulated in float32, so the measurement is the SECOND branch. The
 * difference is ~1e-7 on a cosine against margins two orders larger, so it is
 * immaterial to the operating point — it is pinned so that a later session
 * comparing box output against Python does not read 1 ULP as a port bug.
 * ================================================================ */
static void test_precision_contract(void)
{
    double n2d = 0.0;
    for (int i = 0; i < EMBED_MU_DIM; i++)
        n2d += (double)EMBED_MU[i] * (double)EMBED_MU[i];
    float norm_dbl = (float)sqrt(n2d);

    float n2f = 0.0f;
    for (int i = 0; i < EMBED_MU_DIM; i++)
        n2f += EMBED_MU[i] * EMBED_MU[i];
    float norm_f32 = sqrtf(n2f);

    int moved_dbl = 0, moved_f32 = 0;
    for (int i = 0; i < EMBED_MU_DIM; i++) {
        if (bits_of((float)(EMBED_MU[i] / norm_dbl)) != bits_of(EMBED_MU[i])) moved_dbl++;
        if (bits_of((float)(EMBED_MU[i] / norm_f32)) != bits_of(EMBED_MU[i])) moved_f32++;
    }

    ASSERT(norm_dbl == 1.0f, "a double-accumulated norm rounds to exactly 1.0f");
    ASSERT(moved_dbl == 0, "so dividing by it is a no-op");
    ASSERT(norm_f32 != 1.0f, "a float-accumulated norm is NOT 1.0f");
    ASSERT(moved_f32 == EMBED_MU_DIM, "so dividing by it moves every element");
    ASSERT(moved_dbl != moved_f32,
           "TEETH: the two accumulation precisions give DIFFERENT results");
    printf("    double-norm %.9g moves %d/%d ; float-norm %.9g moves %d/%d\n",
           (double)norm_dbl, moved_dbl, EMBED_MU_DIM,
           (double)norm_f32, moved_f32, EMBED_MU_DIM);
    PASS("C5 renormalisation is PRECISION-DEPENDENT (the C/M2b contract)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: embed_mu.h compiler round-trip (Phase C) ===\n\n");
    test_geometry();
    test_compiler_roundtrip();
    test_ulp_control();
    test_is_a_direction();
    test_precision_contract();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
