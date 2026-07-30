/*
 * JARVIS AI-OS — mean-projection pipeline tests (Phase C / C/M2b)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_embed_project.c phase3/src/ai/embed_project.c \
 *       phase3/src/ai/g3_retrieval.c -o /tmp/test_embed_project -lm
 *
 * g3_retrieval.c is linked deliberately: P3/P5 assert the pipeline's output
 * through the REAL g3_vec_is_unit that g3_select_semantic gates on, rather than
 * through a reimplemented norm check. If the two ever disagreed about what
 * "unit" means, every semantic recall would silently select nothing, and a
 * private norm check in this file would not notice.
 *
 * P2 IS THE ANCHOR. The rest are property tests, which are necessary but cannot
 * catch a systematically wrong transform that is self-consistent — a projection
 * that subtracts the wrong multiple of mu still produces a unit vector, still
 * looks orthogonal-ish, and still passes every property below. So P2 compares
 * against 128 values computed INDEPENDENTLY IN PYTHON with the same documented
 * algorithm, over an input chosen to be bit-exactly reproducible in both
 * languages (v[i] = ((i%7)-3)/8, every value a small dyadic rational).
 */

#include "embed_project.h"
#include "embed_mu.h"
#include "g3_retrieval.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RAW_DIM  EMBED_MU_DIM      /* 1024 */
#define OUT_DIM  G3_SEM_DIM_DEFAULT /* 128  */

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while (0)

/* The synthetic input. Every value is k/8 for k in -3..3 — exact in binary32, so
 * C and Python start from bit-identical bytes and any divergence is the transform. */
static void make_input(float *v)
{
    for (int i = 0; i < RAW_DIM; i++)
        v[i] = (float)(((i % 7) - 3) / 8.0);
}

/* Computed by phase3/scripts/embed/gen_embed_mu.py's sibling one-shot, in Python,
 * with the identical double-accumulated algorithm:
 *     nmu  = sqrt(sum(mu[i]^2))
 *     dot  = sum(v[i] * mu[i]/nmu)          over ALL 1024
 *     p[i] = v[i] - dot * mu[i]/nmu         for i < 128
 *     out  = p / ||p||
 * Python reported dot = 0.2637792079220572, n2 = 8.078873538945386. */
static const float GOLDEN_OUT[OUT_DIM] = {
    -0x1.096eccp-3f, -0x1.516d4p-4f, -0x1.5c47dcp-5f, 0x1.c6025p-8f,
    0x1.3abc8ap-5f, 0x1.66a0a4p-4f, 0x1.0b2718p-3f, -0x1.0dfb26p-3f,
    -0x1.4b0d1p-4f, -0x1.81417p-5f, 0x1.763b6p-9f, 0x1.7758fap-5f,
    0x1.5ca33p-4f, 0x1.1109cap-3f, -0x1.ff2c04p-4f, -0x1.963c06p-4f,
    -0x1.66a92ap-5f, -0x1.07f93p-9f, 0x1.2b84eap-5f, 0x1.63c99ep-4f,
    0x1.1101dcp-3f, -0x1.0ffae8p-3f, -0x1.62256p-4f, -0x1.bbbcfcp-5f,
    0x1.64a44cp-9f, 0x1.581a16p-5f, 0x1.7638b6p-4f, 0x1.03664ep-3f,
    -0x1.10200ep-3f, -0x1.623e7p-4f, -0x1.7ccfeep-5f, -0x1.8bff5p-9f,
    0x1.8c8b02p-5f, 0x1.6f3806p-4f, 0x1.1333dap-3f, -0x1.09f702p-3f,
    -0x1.6b7f08p-4f, -0x1.4bef3cp-5f, 0x1.204528p-10f, 0x1.448ec6p-5f,
    0x1.6c1848p-4f, 0x1.0d7234p-3f, -0x1.137ba8p-3f, -0x1.691506p-4f,
    -0x1.71df32p-5f, 0x1.b86594p-9f, 0x1.507bbp-5f, 0x1.6d9c26p-4f,
    0x1.08b382p-3f, -0x1.0f15ep-3f, -0x1.581874p-4f, -0x1.5ef06p-5f,
    0x1.1af594p-9f, 0x1.6e281p-5f, 0x1.705026p-4f, 0x1.179dbep-3f,
    -0x1.13592ep-3f, -0x1.6715aep-4f, -0x1.7d0768p-5f, 0x1.6be198p-9f,
    0x1.945de4p-5f, 0x1.43b98ap-4f, 0x1.1bc59cp-3f, -0x1.0e633cp-3f,
    -0x1.6c9634p-4f, -0x1.812cd2p-5f, -0x1.71014ap-10f, 0x1.7e9384p-5f,
    0x1.81fdb2p-4f, 0x1.123e22p-3f, -0x1.0cba22p-3f, -0x1.5fc3fep-4f,
    -0x1.4d25f2p-5f, -0x1.b5408ap-9f, 0x1.6dceap-5f, 0x1.5a3578p-4f,
    0x1.0b983cp-3f, -0x1.046696p-3f, -0x1.714ed6p-4f, -0x1.900b28p-5f,
    0x1.156d7cp-9f, 0x1.396278p-5f, 0x1.668346p-4f, 0x1.0c86fp-3f,
    -0x1.0b6902p-3f, -0x1.7df954p-4f, -0x1.6f242ap-5f, 0x1.176f32p-9f,
    0x1.60792ap-5f, 0x1.620de2p-4f, 0x1.133a2ap-3f, -0x1.1a1e92p-3f,
    -0x1.568a2ep-4f, -0x1.4446d2p-5f, -0x1.4aa49ap-11f, 0x1.8063e4p-5f,
    0x1.6e7e48p-4f, 0x1.0e1944p-3f, -0x1.079518p-3f, -0x1.626b7ep-4f,
    -0x1.61daf4p-5f, 0x1.304306p-11f, 0x1.7c0742p-5f, 0x1.6e550ap-4f,
    0x1.158e78p-3f, -0x1.1df854p-3f, -0x1.3fbe7p-4f, -0x1.5c8e64p-5f,
    0x1.552aaep-11f, 0x1.88aef2p-5f, 0x1.60be34p-4f, 0x1.09276ap-3f,
    -0x1.060268p-3f, -0x1.73fa76p-4f, -0x1.53f432p-5f, -0x1.f7e20ep-15f,
    0x1.7879ap-5f, 0x1.72b622p-4f, 0x1.0ec1eap-3f, -0x1.0a9d24p-3f,
    -0x1.6bd2bep-4f, -0x1.6cdbf6p-5f, 0x1.0dc92cp-9f, 0x1.76eep-5f,
    0x1.70c2d2p-4f, 0x1.0d844cp-3f, -0x1.04f706p-3f, -0x1.62c0fcp-4f,
};

/* ================================================================
 * P1: mu is intact
 * ================================================================ */
static void test_mu_verify(void)
{
    ASSERT(embed_mu_verify() == 1, "embed_mu_verify accepts the compiled mu");
    ASSERT(EMBED_MU_DIM == 1024, "mu is 1024-dim");
    ASSERT(OUT_DIM == 128, "the store/selector dim is 128");
    PASS("P1 mu verifies against EMBED_MU_XOR");
}

/* ================================================================
 * P2: THE ANCHOR — cross-language agreement with the Python reference
 * ================================================================ */
static void test_python_agreement(void)
{
    static float v[RAW_DIM], out[OUT_DIM];
    make_input(v);
    ASSERT(embed_project(v, RAW_DIM, out, OUT_DIM) == EMBED_PROJ_OK, "projection succeeds");

    double worst = 0.0;
    int worst_i = -1, exact = 0;
    for (int i = 0; i < OUT_DIM; i++) {
        double d = fabs((double)out[i] - (double)GOLDEN_OUT[i]);
        if (memcmp(&out[i], &GOLDEN_OUT[i], sizeof(float)) == 0) exact++;
        if (d > worst) { worst = d; worst_i = i; }
    }
    printf("    %d/%d bit-identical to Python; worst abs diff %.3e at [%d]\n",
           exact, OUT_DIM, worst, worst_i);

    /* BIT-IDENTITY, not a tolerance — and the reason is measured rather than
     * aspirational. Both sides accumulate in double over the same order and round
     * once at the end, and this comparison was OBSERVED at 128/128 identical with
     * worst diff exactly 0.
     *
     * A loose bound here is not merely weaker, it is not weak in a safe direction:
     * a mutation replacing the projection's double accumulator with float produced
     * a worst diff of 1.5e-8 and SURVIVED a 1e-6 bound, while a legitimate
     * one-ULP toolchain difference at these magnitudes is ~7e-9 — the two are
     * within 2x of each other, so no absolute tolerance separates "wrong
     * precision" from "different compiler". Demanding exactness is the only
     * discriminating choice available, and it is the C/M1a GATE 2' shape: when
     * both sides run the same precision, the expected answer is exact.
     *
     * If a future toolchain does differ in the last bit this fails LOUDLY with the
     * measured diff printed above, which is a judgement call to make then with a
     * number in hand — not something to pre-tolerate now. */
    ASSERT(worst == 0.0, "C agrees with the Python reference BIT-EXACTLY");
    ASSERT(exact == OUT_DIM, "all 128 values are bit-identical");
    PASS("P2 ANCHOR: bit-exact cross-language agreement with the Python pipeline");
}

/* ================================================================
 * P3: output is unit-length BY THE REAL PREDICATE g3_select_semantic gates on
 * ================================================================ */
static void test_output_is_unit(void)
{
    static float v[RAW_DIM], out[OUT_DIM];
    make_input(v);
    ASSERT(embed_project(v, RAW_DIM, out, OUT_DIM) == EMBED_PROJ_OK, "projection succeeds");
    ASSERT(g3_vec_is_unit(out, OUT_DIM) == 1, "the REAL g3_vec_is_unit accepts the output");

    double n2 = 0.0;
    for (int i = 0; i < OUT_DIM; i++) n2 += (double)out[i] * (double)out[i];
    printf("    ||out||^2 = %.17g\n", n2);
    ASSERT(fabs(n2 - 1.0) < 1e-5, "||out||^2 is 1 to within 1e-5");
    PASS("P3 output passes the real unit-vector predicate");
}

/* ================================================================
 * P4: the projection's DEFINING property — the result is orthogonal to mu.
 *
 * Checked at 1024 via embed_meanproject_inplace, because that is where the
 * property actually holds; the truncated output is NOT orthogonal to mu and
 * asserting otherwise would be wrong.
 * ================================================================ */
static void test_orthogonality(void)
{
    static float v[RAW_DIM];
    make_input(v);

    double vlen = 0.0;
    for (int i = 0; i < RAW_DIM; i++) vlen += (double)v[i] * (double)v[i];
    vlen = sqrt(vlen);

    ASSERT(embed_meanproject_inplace(v, RAW_DIM) == EMBED_PROJ_OK, "in-place projection succeeds");

    double nmu = 0.0;
    for (int i = 0; i < RAW_DIM; i++) nmu += (double)EMBED_MU[i] * (double)EMBED_MU[i];
    nmu = sqrt(nmu);
    double dot = 0.0;
    for (int i = 0; i < RAW_DIM; i++) dot += (double)v[i] * ((double)EMBED_MU[i] / nmu);

    /* Relative to the input's length, so the bound means something regardless of
     * how large the input was. */
    double rel = fabs(dot) / vlen;
    printf("    |proj . mu_hat| = %.3e  (relative to ||v|| : %.3e)\n", fabs(dot), rel);
    ASSERT(rel < 1e-6, "the projected vector is orthogonal to mu");
    PASS("P4 projection is orthogonal to mu (its defining property)");
}

/* ================================================================
 * P5: renormalisation is DOING WORK — a positive control for step 3.
 *
 * Truncating a unit vector does not leave it unit. Build exactly the
 * truncated-but-not-renormalised vector and show the real predicate REJECTS it:
 * this is the pipeline error where cosine silently degrades into magnitude
 * ranking, and efe5edd measured g3_vec_is_unit firing 15/15 on it.
 * ================================================================ */
static void test_renormalisation_is_load_bearing(void)
{
    static float v[RAW_DIM], trunc[OUT_DIM], full[OUT_DIM];
    make_input(v);

    /* First normalise the INPUT to unit at 1024, so this mirrors a real embedding. */
    double n = 0.0;
    for (int i = 0; i < RAW_DIM; i++) n += (double)v[i] * (double)v[i];
    n = sqrt(n);
    for (int i = 0; i < RAW_DIM; i++) v[i] = (float)((double)v[i] / n);

    static float vcopy[RAW_DIM];
    memcpy(vcopy, v, sizeof(vcopy));
    ASSERT(embed_project(vcopy, RAW_DIM, full, OUT_DIM) == EMBED_PROJ_OK, "full pipeline succeeds");
    ASSERT(g3_vec_is_unit(full, OUT_DIM) == 1, "the full pipeline's output IS unit");

    /* Now the same thing WITHOUT step 3: project at 1024, truncate, stop. */
    memcpy(vcopy, v, sizeof(vcopy));
    ASSERT(embed_meanproject_inplace(vcopy, RAW_DIM) == EMBED_PROJ_OK, "in-place projection succeeds");
    memcpy(trunc, vcopy, sizeof(trunc));
    double tn2 = 0.0;
    for (int i = 0; i < OUT_DIM; i++) tn2 += (double)trunc[i] * (double)trunc[i];
    printf("    truncated-not-renormalised ||v||^2 = %.6g\n", tn2);
    ASSERT(g3_vec_is_unit(trunc, OUT_DIM) == 0,
           "TEETH: the real predicate REJECTS truncated-without-renormalising");
    PASS("P5 renormalisation is load-bearing (predicate rejects it when skipped)");
}

/* ================================================================
 * P6: THE DOT MUST RUN OVER ALL 1024, not just the surviving 128.
 *
 * This is the specific ordering decision in the design — mu was estimated in
 * 1024-space, so the projection coefficient must be computed there. Restricting
 * the dot to the first 128 dims is a DIFFERENT transform, and it is the plausible
 * mistake a later refactor would make while "simplifying". Show the two differ
 * materially, so the choice is pinned by a number rather than a comment.
 * ================================================================ */
static void test_dot_spans_full_dim(void)
{
    static float v[RAW_DIM], good[OUT_DIM], bad[OUT_DIM];
    make_input(v);
    ASSERT(embed_project(v, RAW_DIM, good, OUT_DIM) == EMBED_PROJ_OK, "full-dim dot succeeds");

    /* The WRONG variant, computed inline: dot over the first OUT_DIM only. */
    double nmu = 0.0;
    for (int i = 0; i < RAW_DIM; i++) nmu += (double)EMBED_MU[i] * (double)EMBED_MU[i];
    nmu = sqrt(nmu);
    double dot_short = 0.0;
    for (int i = 0; i < OUT_DIM; i++) dot_short += (double)v[i] * ((double)EMBED_MU[i] / nmu);
    double n2 = 0.0;
    for (int i = 0; i < OUT_DIM; i++) {
        double p = (double)v[i] - dot_short * ((double)EMBED_MU[i] / nmu);
        bad[i] = (float)p; n2 += p * p;
    }
    double inv = 1.0 / sqrt(n2);
    for (int i = 0; i < OUT_DIM; i++) bad[i] = (float)((double)bad[i] * inv);

    /* Cosine between the two — how much the mistake would actually move a score. */
    double cos = 0.0;
    for (int i = 0; i < OUT_DIM; i++) cos += (double)good[i] * (double)bad[i];
    printf("    cosine(full-dim dot, 128-only dot) = %.9f\n", cos);
    ASSERT(memcmp(good, bad, sizeof(good)) != 0,
           "TEETH: restricting the dot to 128 dims produces a DIFFERENT vector");
    PASS("P6 the projection coefficient spans all 1024 dims (order pinned)");
}

/* ================================================================
 * P7: error paths, including the degenerate case
 * ================================================================ */
static void test_error_paths(void)
{
    static float v[RAW_DIM], out[OUT_DIM];
    make_input(v);

    ASSERT(embed_project(NULL, RAW_DIM, out, OUT_DIM) == EMBED_PROJ_E_ARGS, "NULL raw refused");
    ASSERT(embed_project(v, RAW_DIM, NULL, OUT_DIM) == EMBED_PROJ_E_ARGS, "NULL out refused");
    ASSERT(embed_project(v, 512, out, OUT_DIM) == EMBED_PROJ_E_ARGS, "wrong raw_dim refused");
    ASSERT(embed_project(v, RAW_DIM, out, 0) == EMBED_PROJ_E_ARGS, "out_dim 0 refused");
    ASSERT(embed_project(v, RAW_DIM, out, RAW_DIM + 1) == EMBED_PROJ_E_ARGS, "out_dim > raw_dim refused");
    ASSERT(embed_project(v, RAW_DIM, v, OUT_DIM) == EMBED_PROJ_E_ARGS, "aliasing refused");
    ASSERT(embed_meanproject_inplace(NULL, RAW_DIM) == EMBED_PROJ_E_ARGS, "in-place NULL refused");
    ASSERT(embed_meanproject_inplace(v, 128) == EMBED_PROJ_E_ARGS, "in-place wrong dim refused");

    /* THE DEGENERATE CASE, and it is the reason EMBED_PROJ_MIN_RESIDUAL_REL is
     * relative rather than `n2 > 0`.
     *
     * mu projected by itself leaves only float rounding noise. Under `n2 > 0` that
     * noise renormalises to a textbook unit vector and is returned as OK — and
     * NEITHER downstream guard can see it: g3_vec_is_unit gets a genuine unit
     * vector, the 0.55 floor gets a genuine score. So this must be caught here.
     *
     * NOTE the earlier version of this test asserted `n2 > 1e-12` on the OUTPUT,
     * which is vacuous: the output is renormalised, so its norm is always 1. The
     * check has to be on the returned CODE. */
    static float mu_copy[RAW_DIM];
    memcpy(mu_copy, EMBED_MU, sizeof(mu_copy));
    for (int i = 0; i < OUT_DIM; i++) out[i] = 7.0f;
    int rc = embed_project(mu_copy, RAW_DIM, out, OUT_DIM);
    printf("    projecting mu by itself: rc=%d (expect %d = DEGENERATE)\n",
           rc, EMBED_PROJ_E_DEGENERATE);
    ASSERT(rc == EMBED_PROJ_E_DEGENERATE,
           "TEETH: a near-mu-parallel input is REFUSED, not renormalised from noise");

    /* And on that error path `out` is ZEROED, not left holding the 7.0f fill or a
     * partial write — a half-written vector renormalises into a unit-looking
     * vector of nothing. */
    int all_zero = 1;
    for (int i = 0; i < OUT_DIM; i++) if (out[i] != 0.0f) all_zero = 0;
    ASSERT(all_zero, "the degenerate path leaves out[] zeroed");
    ASSERT(g3_vec_is_unit(out, OUT_DIM) == 0, "and a zeroed vector is not unit");

    /* An all-zero input has no direction at all. */
    static float zeros[RAW_DIM];
    memset(zeros, 0, sizeof(zeros));
    ASSERT(embed_project(zeros, RAW_DIM, out, OUT_DIM) == EMBED_PROJ_E_DEGENERATE,
           "an all-zero input is DEGENERATE");

    /* A real embedding must be nowhere near that threshold — the guard has to
     * separate noise from signal, not merely reject something. */
    static float unit_v[RAW_DIM];
    make_input(unit_v);
    double n = 0.0;
    for (int i = 0; i < RAW_DIM; i++) n += (double)unit_v[i] * (double)unit_v[i];
    n = sqrt(n);
    for (int i = 0; i < RAW_DIM; i++) unit_v[i] = (float)((double)unit_v[i] / n);
    ASSERT(embed_project(unit_v, RAW_DIM, out, OUT_DIM) == EMBED_PROJ_OK,
           "a realistic unit input is ACCEPTED (the guard separates, not just rejects)");
    PASS("P7 error paths (NULL / dims / aliasing / degenerate, both sides)");
}

/* ================================================================
 * P8: determinism — identical input, bit-identical output
 * ================================================================ */
static void test_determinism(void)
{
    static float v[RAW_DIM], a[OUT_DIM], b[OUT_DIM];
    make_input(v);
    ASSERT(embed_project(v, RAW_DIM, a, OUT_DIM) == EMBED_PROJ_OK, "first run");
    ASSERT(embed_project(v, RAW_DIM, b, OUT_DIM) == EMBED_PROJ_OK, "second run");
    ASSERT(memcmp(a, b, sizeof(a)) == 0, "two runs are BIT-identical");
    PASS("P8 determinism (bit-identical across runs)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: mean-projection pipeline (Phase C / C/M2b) ===\n\n");
    test_mu_verify();
    test_python_agreement();
    test_output_is_unit();
    test_orthogonality();
    test_renormalisation_is_load_bearing();
    test_dot_spans_full_dim();
    test_error_paths();
    test_determinism();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
