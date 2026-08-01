/*
 * JARVIS AI-OS — hybrid routing veto tests (Phase C / C/M4)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_route_veto.c phase3/src/ai/route_veto.c \
 *       -o /tmp/test_route_veto -lm
 *
 * WHAT THESE TESTS CAN AND CANNOT DO
 * ----------------------------------
 * They pin the DECISION RULE — scope, direction, tie-breaking, guards — against
 * the frozen centroids. They say nothing about whether the veto is a good idea;
 * that is cm4_routing_results.json's job, and route_veto.h carries its four
 * caveats. A green run here means "the rule implemented is the rule specified",
 * not "the routing defect is fixed".
 *
 * The dots are recomputed locally (dot128 below) rather than imported, so an
 * assertion about a similarity is an INDEPENDENT statement rather than a restated
 * one. The loop order matches route_veto.c's deliberately: with the same order and
 * the same double accumulation the two agree bit-for-bit, which is what lets T3
 * reason about exact ties at all.
 */

#include "route_veto.h"
#include "route_centroids.h"
#include "embed_store.h"     /* EMBED_STORE_DIM — for the cross-header pin below */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * THE CROSS-HEADER PIN. It lives in the test, not in route_veto.c, following
 * test_g3_retrieval.c:18 — a test file is where two otherwise-independent headers
 * legitimately meet, and route_veto.c keeping embed_store.h out of its include set
 * is deliberate (route_veto.h documents why).
 *
 * The centroids and the stored vectors are downstream of the SAME Matryoshka
 * truncation. If one is ever re-truncated without the other, every cosine would be
 * computed across two different spaces — confident, well-formed, wrong. This makes
 * that a BUILD failure rather than a silent misroute.
 */
_Static_assert(ROUTE_CENTROIDS_DIM == EMBED_STORE_DIM,
               "centroid dim and stored-vector dim are the same truncated space");
_Static_assert(ROUTE_VETO_DIM == EMBED_STORE_DIM,
               "the veto's declared vector length is that same space");

#define DIM ROUTE_CENTROIDS_DIM

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

/* ---------------------------------------------------------------- helpers */

static uint32_t bits_of(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

static float from_bits(uint32_t u)
{
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

/* One ULP away, by flipping the low mantissa bit. The smallest change that is
 * still a different float — if the XOR catches this it catches anything larger. */
static float ulp_flip(float f)
{
    return from_bits(bits_of(f) ^ 1u);
}

/* Same order and same accumulation precision as route_veto.c, so the values here
 * are the values it compares. */
static double dot128(const float *a, const float *b)
{
    double s = 0.0;
    for (int i = 0; i < DIM; i++)
        s += (double)a[i] * (double)b[i];
    return s;
}

static double norm128(const float *v)
{
    return sqrt(dot128(v, v));
}

static void scale128(float *v, double k)
{
    for (int i = 0; i < DIM; i++)
        v[i] = (float)((double)v[i] * k);
}

static void unit128(float *v)
{
    scale128(v, 1.0 / norm128(v));
}

/* The XOR route_veto_centroids_ok() computes, but over a CALLER-SUPPLIED copy
 * so a mutation can be applied without touching the frozen header. */
static uint32_t xor_of_rows(const float rows[][DIM], int n)
{
    uint32_t x = 0;
    for (int r = 0; r < n; r++)
        for (int i = 0; i < DIM; i++)
            x ^= bits_of(rows[r][i]);
    return x;
}

/* The deterministic seed for T3's construction. Every value is k/8 for k in -3..3
 * — exact in binary32, so the vector is bit-reproducible on any toolchain and a
 * divergence is the arithmetic, never the input (the test_embed_project.c
 * precedent). It is not orthogonal to anything yet; T3 makes it so. */
static void make_seed(float *v)
{
    for (int i = 0; i < DIM; i++)
        v[i] = (float)(((i % 7) - 3) / 8.0);
}

/* Classical Gram-Schmidt against the two centroids the veto reads, run TWICE.
 * Once leaves a residual proportional to the seed's overlap; the second pass
 * removes it, which is the standard fix for classical GS's known instability.
 * Everything is accumulated in double; the float32 store at the end is what
 * ultimately bounds orthogonality (see T3's comment). */
static void orthogonalize_to_both(float *v)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int r = 0; r <= 1; r++) {                 /* rows 0 (INFER), 1 (SYSFACTS) */
            const float *c = ROUTE_CENTROIDS[r];
            /* Project against THIS row after the previous removal, using the
             * row's own norm — the rows are not exactly unit and not exactly
             * orthogonal to each other, so a shared normaliser would be wrong. */
            const double d = dot128(v, c) / dot128(c, c);
            for (int i = 0; i < DIM; i++)
                v[i] = (float)((double)v[i] - d * (double)c[i]);
        }
    }
    unit128(v);
}

/* ================================================================
 * T1: header integrity — the compiled centroids are the frozen ones
 * ================================================================ */
static void test_header_integrity(void)
{
    /* The shipped gate. 0 == OK; the polarity is the opposite of
     * embed_mu_verify() and is pinned here so an inverted copy-paste (which would
     * fail OPEN) cannot pass. */
    ASSERT(route_veto_centroids_ok() == 1,
           "route_veto_centroids_ok() reports the compiled centroids intact (1 == INTACT)");

    /* Every row is a unit vector. The generator L2-normalises in float64 and casts
     * once at storage, so the residual is ~1e-8; 1e-3 would catch a row that was
     * never normalised, truncated without renormalising, or scaled. */
    for (int r = 0; r < ROUTE_CENTROIDS_N; r++) {
        const double n = norm128(ROUTE_CENTROIDS[r]);
        ASSERT(fabs(n - 1.0) < 1e-3, "each centroid row is unit-norm within 1e-3");
    }

    /* TEETH: prove the XOR actually discriminates, without mutating the frozen
     * header. Copy it, verify the copy reproduces the constant (the CONTROL —
     * without this, a broken xor_of_rows would make the mutation "fail" for the
     * wrong reason), then move ONE float by ONE ULP and recompute. */
    static float copy[ROUTE_CENTROIDS_N][DIM];
    memcpy(copy, ROUTE_CENTROIDS, sizeof(copy));

    ASSERT(xor_of_rows(copy, ROUTE_CENTROIDS_N) == ROUTE_CENTROIDS_XOR,
           "CONTROL: an untouched copy reproduces ROUTE_CENTROIDS_XOR");

    const float orig = copy[1][64];
    copy[1][64] = ulp_flip(orig);
    ASSERT(bits_of(copy[1][64]) != bits_of(orig), "the ULP flip changed the bits");
    ASSERT(xor_of_rows(copy, ROUTE_CENTROIDS_N) != ROUTE_CENTROIDS_XOR,
           "TEETH: a 1-ULP change to one element breaks the XOR");

    copy[1][64] = orig;
    ASSERT(xor_of_rows(copy, ROUTE_CENTROIDS_N) == ROUTE_CENTROIDS_XOR,
           "restoring the element restores the XOR");

    PASS("T1 header integrity (verify == 0, rows unit, XOR has teeth)");
}

/* ================================================================
 * T2: the decision truth table — direction, and SCOPE
 * ================================================================ */
static void test_truth_table(void)
{
    const float *c_infer = ROUTE_CENTROIDS[ROUTE_INFER];
    const float *c_sys   = ROUTE_CENTROIDS[ROUTE_SYSFACTS];

    /* The two extreme queries: a vector that IS a centroid. Separation is large
     * and deterministic — the two centroids sit at cos -0.028, so these are not
     * near-tie cases and no rounding argument applies. */
    printf("    [T2] sims for v=c_INFER   : infer=%.9f sys=%.9f\n",
           dot128(c_infer, c_infer), dot128(c_infer, c_sys));
    printf("    [T2] sims for v=c_SYSFACTS: infer=%.9f sys=%.9f\n",
           dot128(c_sys, c_infer), dot128(c_sys, c_sys));

    /* DIRECTION. A query sitting on the INFER centroid, captured as SYSFACTS by
     * the keyword router, is exactly the defect this exists for -> veto. */
    ASSERT(route_veto_should(c_infer, ROUTE_SYSFACTS) == 1,
           "v == INFER centroid, kw SYSFACTS -> VETO");

    /* A query sitting on the SYSFACTS centroid is a genuine status question; the
     * keyword capture stands. A swapped-index implementation inverts both. */
    ASSERT(route_veto_should(c_sys, ROUTE_SYSFACTS) == 0,
           "v == SYSFACTS centroid, kw SYSFACTS -> NO veto");

    /* SCOPE: DECLINE is never vetoed, for EITHER vector.
     *
     * Both directions matter. The measured Arm C allowed DECLINE captures to be
     * vetoed, so the natural mutation back to it is "also accept ROUTE_DECLINE and
     * compare against row kw_cls". Under that mutant v=c_INFER/kw=DECLINE fires
     * (c_infer.c_infer 1.000 > c_infer.c_decline 0.069) while v=c_SYSFACTS/kw=
     * DECLINE does not (-0.028 < 0.678) — so ONLY the first case catches it, and
     * testing one vector would have left the mutant alive.
     *
     * The reason for the scope is in route_veto.h: vetoing a genuine decline sends
     * a no-source metric question to the model to be FABRICATED. */
    ASSERT(route_veto_should(c_infer, ROUTE_DECLINE) == 0,
           "SCOPE: v == INFER centroid, kw DECLINE -> NO veto (DECLINE is out of scope)");
    ASSERT(route_veto_should(c_sys, ROUTE_DECLINE) == 0,
           "SCOPE: v == SYSFACTS centroid, kw DECLINE -> NO veto");

    /* NEVER CREATES A CAPTURE. kw INFER has nothing to veto; whatever the vector
     * looks like, the answer is 0. This is the structural safety property the
     * measurement asserted at runtime rather than trusting its own prose. */
    ASSERT(route_veto_should(c_infer, ROUTE_INFER) == 0,
           "NEVER-CREATES: v == INFER centroid, kw INFER -> 0");
    ASSERT(route_veto_should(c_sys, ROUTE_INFER) == 0,
           "NEVER-CREATES: v == SYSFACTS centroid, kw INFER -> 0");

    PASS("T2 truth table (direction, DECLINE scope, never-creates-a-capture)");
}

/* ================================================================
 * T3: the strict `>` pin, and the geometry around it
 * ================================================================ */
static void test_strict_greater_and_geometry(void)
{
    static float w[DIM], v[DIM];

    /* ---- T3.1 a direction with no affinity to either class -------------- */
    make_seed(w);
    orthogonalize_to_both(w);

    const double s_i = dot128(w, ROUTE_CENTROIDS[ROUTE_INFER]);
    const double s_s = dot128(w, ROUTE_CENTROIDS[ROUTE_SYSFACTS]);
    printf("    [T3.1] orthogonal seed: infer=%.3e sys=%.3e |v|=%.9f verdict=%d\n",
           s_i, s_s, norm128(w), route_veto_should(w, ROUTE_SYSFACTS));

    ASSERT(fabs(norm128(w) - 1.0) < 1e-6, "the orthogonalised seed is unit");
    ASSERT(fabs(s_i) < 1e-5 && fabs(s_s) < 1e-5,
           "the orthogonalised seed has ~zero similarity to BOTH centroids");

    /*
     * DELIBERATELY NOT ASSERTED: which way this vector's verdict falls.
     *
     * Both residuals exist only because the orthogonalisation is done in double and
     * then STORED as float32; they are rounding, not geometry. Whichever is the
     * larger is a property of this particular seed and this particular rounding, so
     * an assertion on the verdict would pin an artifact and would break on any
     * toolchain that rounds the store differently.
     *
     * And it would buy nothing: `>` and `>=` agree on any two DISTINCT values, so
     * no near-zero pair can discriminate them however it falls. T3.4 is where the
     * operator is actually pinned.
     */

    /* ---- T3.2 orthogonal, nudged toward SYSFACTS -> keyword stands ------ */
    /* eps is dyadic (1/8) so the construction is exact; the resulting margin is
     * ~0.128, seven orders above the noise floor, so this IS geometry. */
    memcpy(v, w, sizeof(v));
    for (int i = 0; i < DIM; i++)
        v[i] = (float)((double)v[i] + 0.125 * (double)ROUTE_CENTROIDS[ROUTE_SYSFACTS][i]);
    unit128(v);
    printf("    [T3.2] +0.125*c_SYSFACTS: infer=%.9f sys=%.9f\n",
           dot128(v, ROUTE_CENTROIDS[ROUTE_INFER]), dot128(v, ROUTE_CENTROIDS[ROUTE_SYSFACTS]));
    ASSERT(route_veto_should(v, ROUTE_SYSFACTS) == 0,
           "leaning toward SYSFACTS -> NO veto");

    /* ---- T3.3 orthogonal, nudged toward INFER -> veto ------------------- */
    /* The mirror of T3.2. Without it, T3.2 could pass on a build that never vetoes
     * at all, and the construction would prove nothing. */
    memcpy(v, w, sizeof(v));
    for (int i = 0; i < DIM; i++)
        v[i] = (float)((double)v[i] + 0.125 * (double)ROUTE_CENTROIDS[ROUTE_INFER][i]);
    unit128(v);
    printf("    [T3.3] +0.125*c_INFER   : infer=%.9f sys=%.9f\n",
           dot128(v, ROUTE_CENTROIDS[ROUTE_INFER]), dot128(v, ROUTE_CENTROIDS[ROUTE_SYSFACTS]));
    ASSERT(route_veto_should(v, ROUTE_SYSFACTS) == 1,
           "leaning toward INFER -> VETO");

    /* ---- T3.4 THE EXACT TIE — this is the `>` vs `>=` pin --------------- */
    /*
     * `>` and `>=` differ on exactly one input class: those where the two
     * similarities are BIT-EQUAL. Everything else compares identically under both,
     * which is why T3.1's near-zero pair cannot discriminate them and why the pin
     * has to be an engineered exact tie.
     *
     * The zero vector is that construction: every product is an exact 0.0 and
     * every accumulation adds an exact 0.0, so both sums are exactly +0.0 on any
     * IEEE-754 implementation. `0.0 > 0.0` is false -> the keyword capture stands.
     * A `>=` build vetoes here.
     *
     * WHY NOT A NON-DEGENERATE TIE: a single-component vector would give an exact
     * tie iff the two centroid rows share bits at that index, because a
     * float32*float32 product is exact in double. MEASURED on the frozen header:
     * there is no such index (0 of 128). So with these centroids the zero vector
     * is the only exact tie available, and the pin rests on it by necessity rather
     * than by preference.
     */
    memset(v, 0, sizeof(v));
    ASSERT(dot128(v, ROUTE_CENTROIDS[ROUTE_INFER]) == dot128(v, ROUTE_CENTROIDS[ROUTE_SYSFACTS]),
           "the zero vector really does produce an EXACT tie");
    ASSERT(route_veto_should(v, ROUTE_SYSFACTS) == 0,
           "STRICT >: an exact tie leaves the keyword decision standing");

    PASS("T3 strict > pin + orthogonal-direction geometry");
}

/* ================================================================
 * T4: guards — NULL, out-of-range class, degenerate vectors
 * ================================================================ */
static void test_guards(void)
{
    static float v[DIM];

    /* NULL. Every class, so a future reordering of the internal checks cannot
     * leave one path dereferencing it. */
    ASSERT(route_veto_should(NULL, ROUTE_SYSFACTS) == 0, "NULL vector, kw SYSFACTS -> 0");
    ASSERT(route_veto_should(NULL, ROUTE_INFER) == 0,    "NULL vector, kw INFER -> 0");
    ASSERT(route_veto_should(NULL, ROUTE_DECLINE) == 0,  "NULL vector, kw DECLINE -> 0");

    /* An out-of-range class value. The vector is the INFER centroid, i.e. the one
     * input that WOULD fire if the class were accepted — so this fails if an
     * out-of-range value is ever treated as vetoable, rather than passing
     * vacuously on a vector that would not have fired anyway. */
    const float *c_infer = ROUTE_CENTROIDS[ROUTE_INFER];
    ASSERT(route_veto_should(c_infer, (route_class_t)3) == 0,
           "class 3 (one past DECLINE) -> 0, fail closed");
    ASSERT(route_veto_should(c_infer, (route_class_t)99) == 0,
           "class 99 -> 0, fail closed");

    /* Zero vector: must not crash, must not veto (also the T3.4 tie). */
    memset(v, 0, sizeof(v));
    ASSERT(route_veto_should(v, ROUTE_SYSFACTS) == 0, "zero vector -> 0, no crash");

    /* NON-UNIT, both directions. Positive scaling multiplies both similarities by
     * the same factor, so the verdict must be identical to the unscaled vector's —
     * the scale-invariance route_veto.h claims. This is what makes a
     * slightly-off-unit vector harmless. */
    memcpy(v, c_infer, sizeof(v));
    scale128(v, 1e6);
    ASSERT(route_veto_should(v, ROUTE_SYSFACTS) == 1, "1e6 x INFER centroid -> same verdict (VETO)");
    memcpy(v, c_infer, sizeof(v));
    scale128(v, 1e-6);
    ASSERT(route_veto_should(v, ROUTE_SYSFACTS) == 1, "1e-6 x INFER centroid -> same verdict (VETO)");

    memcpy(v, ROUTE_CENTROIDS[ROUTE_SYSFACTS], sizeof(v));
    scale128(v, 1e6);
    ASSERT(route_veto_should(v, ROUTE_SYSFACTS) == 0, "1e6 x SYSFACTS centroid -> same verdict (no veto)");

    /* NaN: every comparison against NaN is false, so `s_infer > s_sys` is false and
     * the veto declines. Worth pinning as a PROPERTY rather than an accident — a
     * garbage vector must fail toward the keyword decision, not away from it. */
    for (int i = 0; i < DIM; i++)
        v[i] = (float)NAN;
    ASSERT(route_veto_should(v, ROUTE_SYSFACTS) == 0, "NaN vector -> 0, fails closed");

    PASS("T4 guards (NULL, out-of-range class, zero / non-unit / NaN vectors)");
}

/* ================================================================
 * T5: mutation teeth — what was actually proven, and by which test
 * ================================================================ */
static void test_mutation_teeth(void)
{
    /*
     * This group is the RECORD of a proof that was run, not the proof itself —
     * a mutant has to be compiled to be killed, and this binary compiles one
     * version of route_veto.c. The mutants below were applied to a COPY in /tmp
     * (never the repo), each built and run, with an unmutated CONTROL before and
     * after. Each is listed with the assertion that caught it, because "the suite
     * failed" is not evidence that the intended test did the catching.
     *
     *   MUTANT 1  `s_infer > s_sys`  ->  `s_infer >= s_sys`
     *     killed by T3.4  "STRICT >: an exact tie leaves the keyword decision standing"
     *     (and, redundantly, by T4's zero-vector guard). No other assertion in the
     *     suite can catch it: every non-tie input compares identically under both
     *     operators, which is the whole reason T3.4 exists.
     *
     *   MUTANT 2  the two centroid row indices swapped in the dot loop
     *     killed by T2  "v == INFER centroid, kw SYSFACTS -> VETO"
     *     The rule inverts wholesale: every conceptual question keeps its wrong
     *     capture and every genuine status question loses its right one.
     *
     *   MUTANT 3  the DECLINE-scope guard widened back to the measured Arm C
     *             (`kw_cls != ROUTE_SYSFACTS && kw_cls != ROUTE_DECLINE`, comparing
     *             against row kw_cls)
     *     killed by T2  "SCOPE: v == INFER centroid, kw DECLINE -> NO veto (DECLINE is out of scope)"
     *     Only that case catches it — the mirrored v == SYSFACTS/kw DECLINE case
     *     agrees with the mutant, so a single-vector scope test would have let it
     *     live. That is why T2 checks DECLINE with BOTH vectors.
     *
     * Every mutant is a plausible edit: `>=` is the more common operator, the row
     * indices are adjacent constants, and MUTANT 3 is the configuration the
     * measurement actually ran. None would be caught by review of the diff alone.
     */

    /* One live assertion so the group is not pure prose: the properties the
     * mutants attack are all reachable from here, and if any of these three
     * calls ever stopped being distinguishable the mutants above would go
     * unkilled without any test failing. */
    const float *c_infer = ROUTE_CENTROIDS[ROUTE_INFER];
    const float *c_sys   = ROUTE_CENTROIDS[ROUTE_SYSFACTS];
    static float zero[DIM];
    memset(zero, 0, sizeof(zero));

    const int a = route_veto_should(c_infer, ROUTE_SYSFACTS);  /* 1 — direction     */
    const int b = route_veto_should(c_sys,   ROUTE_SYSFACTS);  /* 0 — direction     */
    const int c = route_veto_should(c_infer, ROUTE_DECLINE);   /* 0 — scope         */
    const int d = route_veto_should(zero,    ROUTE_SYSFACTS);  /* 0 — strict tie    */

    ASSERT(a == 1 && b == 0 && c == 0 && d == 0,
           "the three mutant-discriminating inputs still give distinct, specified verdicts");

    PASS("T5 mutation teeth (3 mutants killed; see the comment for which test caught each)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: hybrid routing veto (Phase C / C/M4) ===\n\n");
    test_header_integrity();
    test_truth_table();
    test_strict_greater_and_geometry();
    test_guards();
    test_mutation_teeth();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
