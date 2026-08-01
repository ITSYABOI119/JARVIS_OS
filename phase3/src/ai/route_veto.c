/*
 * JARVIS AI-OS — Phase C / C/M4: the hybrid routing veto
 *
 * See route_veto.h for what this is, what was measured, and the four caveats that
 * travel with the numbers. This file is the arithmetic and the reasons for it.
 *
 * Host-pure: no seL4, no device, no allocation, no I/O. The only inputs are the
 * caller's vector and the frozen centroids.
 */

#include "route_veto.h"
#include "route_centroids.h"   /* ROUTE_CENTROIDS / _DIM / _N / _XOR — the FROZEN artifact */
#include <stdint.h>
#include <string.h>

/*
 * The header restates the dimension so consumers of route_veto.h do not drag
 * 3x128 floats of static const data into their translation unit. This is the pin
 * that makes the restatement safe: a Matryoshka re-truncation (the Phase C plan
 * contemplates one) must break THIS BUILD rather than leave route_veto_should
 * reading 128 floats out of a caller's shorter buffer.
 */
_Static_assert(ROUTE_VETO_DIM == ROUTE_CENTROIDS_DIM,
               "route_veto.h's ROUTE_VETO_DIM must equal the frozen centroid dimension");

/* Both centroid rows the veto reads must exist. If a fourth route_class_t is ever
 * appended, route_centroids.h breaks first (it pins N == ROUTE_DECLINE + 1); this
 * is the narrower guard that the two rows THIS file indexes are in range. */
_Static_assert((int)ROUTE_INFER < ROUTE_CENTROIDS_N && (int)ROUTE_SYSFACTS < ROUTE_CENTROIDS_N,
               "the veto indexes the INFER and SYSFACTS centroid rows");

/* Bit pattern of a float, via memcpy. A pointer cast would be a strict-aliasing
 * violation and -O2 is entitled to miscompile it (the embed_project.c precedent). */
static uint32_t bits_of(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

int route_veto_centroids_verify(void)
{
    uint32_t x = 0;

    for (int r = 0; r < ROUTE_CENTROIDS_N; r++)
        for (int i = 0; i < ROUTE_CENTROIDS_DIM; i++)
            x ^= bits_of(ROUTE_CENTROIDS[r][i]);

    /* 0 == OK. Opposite polarity to embed_mu_verify() — see route_veto.h. */
    return x == ROUTE_CENTROIDS_XOR ? 0 : 1;
}

int route_veto_should(const float *v128, route_class_t kw_cls)
{
    if (!v128)
        return 0;

    /*
     * SCOPE GATE. One equality test covers three separate contracts:
     *   ROUTE_INFER   — nothing to veto; this is what makes "the veto never
     *                   creates a capture" true by construction rather than by
     *                   convention (route_veto.h, structural safety).
     *   ROUTE_DECLINE — deliberately out of scope; vetoing a genuine decline
     *                   sends a no-source metric question to the model to be
     *                   fabricated, a risk class the measurement never priced.
     *   anything else — an out-of-range enum value is not a class we have a
     *                   centroid for, and guessing would be the silently-wrong
     *                   behaviour this whole arc exists to avoid.
     *
     * Written as `!=` rather than a switch precisely so a future fourth class
     * defaults to NOT vetoing. Fail closed.
     */
    if (kw_cls != ROUTE_SYSFACTS)
        return 0;

    /*
     * cos == dot, because both sides are unit vectors by construction: the
     * centroids are L2-normalised by the generator, and v128 comes out of
     * embed_project's final renormalisation. No division, no sqrt.
     *
     * ACCUMULATED IN DOUBLE, from float32 inputs — the embed_project.c precedent.
     * A 128-term sum of products in float32 accumulates real rounding error, and
     * this comparison is decided by the DIFFERENCE of two such sums, so error in
     * either one lands directly on the verdict. Double costs nothing next to the
     * ~190 M cycles the embedding itself took (C/M1b-2, box-measured).
     *
     * Both dots share one pass over v128 so the query vector is read once.
     */
    double s_infer = 0.0;
    double s_sys   = 0.0;

    for (int i = 0; i < ROUTE_CENTROIDS_DIM; i++) {
        const double q = (double)v128[i];
        s_infer += q * (double)ROUTE_CENTROIDS[ROUTE_INFER][i];
        s_sys   += q * (double)ROUTE_CENTROIDS[ROUTE_SYSFACTS][i];
    }

    /*
     * STRICT `>`, and it is a decision, not a typo waiting to be tidied.
     *
     * The keyword verdict is the incumbent and the veto is the challenger, so a
     * tie must leave the capture standing. `>=` would let a query with no
     * preference between the two centroids overturn the keyword router, which is
     * the opposite of route.h's conservative rule.
     *
     * `>` and `>=` differ ONLY on exact equality, so the only test that can
     * discriminate them is one that engineers an exact tie — see test_route_veto.c
     * T3, and note there is no index at which the two centroid rows share bits
     * (measured on the frozen header), so the zero vector is the construction for
     * which the tie is exact rather than merely small.
     */
    return s_infer > s_sys ? 1 : 0;
}

/*
 * ── THREE HONEST NUMERICAL LIMITS, RECORDED SO NOBODY READS THEM AS BUGS ──────
 *
 * (a) THE MEASUREMENT USED float64 CENTROIDS; THIS USES THE STORED float32.
 *     gen_route_centroids.py computes in float64 and casts once, at storage
 *     (route_centroids.h's PROVENANCE block), and cm4_routing_measure.py's
 *     `sims(E) = E @ C.T` ran against its own in-memory float64 centroids.
 *     Reading the frozen float32 artifact instead perturbs each element by ~1e-8.
 *     This file uses the STORED values deliberately: they are the committed,
 *     CI-verified artifact, and the measurement's transient in-memory precision is
 *     not reproducible on the box anyway.
 *
 * (b) THE STORED CENTROIDS ARE NOT EXACTLY UNIT, AND THE TWO ROWS DIFFER.
 *     ||c_INFER||^2 = 0.999999986404, ||c_SYSFACTS||^2 = 0.999999994983 — a
 *     relative gap of ~8.6e-9. Unlike the query vector (whose positive scaling
 *     cancels, see route_veto.h), this asymmetry does NOT cancel: it is a
 *     systematic ~4e-9 bias in favour of SYSFACTS, i.e. against firing. They are
 *     NOT renormalised here because the measurement did not renormalise either,
 *     and matching the measured transform matters more than the ninth decimal.
 *
 *     CONSEQUENCE, stated plainly: a verdict whose margin |s_infer - s_sys| falls
 *     below ~1e-8 is not meaningfully determined by this rule OR by the
 *     measurement behind it. Real separations are ~1e-1 (the two centroids sit at
 *     cos -0.028, near-orthogonal), so that is a statement about a region no
 *     observed query occupies, not a caveat on the result.
 *
 * (c) BOX-VS-HOST PARITY IS AN OPEN MEASUREMENT, NOT AN ASSUMPTION. The box embeds
 *     with Q8_0 weights where the measurement used F32; C/M2b measured that as a
 *     systematically negative ~0.003 shift on cosines. What decides a veto is not
 *     either cosine but the SIGN OF THEIR DIFFERENCE, and a shared bias partially
 *     cancels in a difference — "partially" being the honest word, since it is not
 *     proven to cancel. cm4_veto_driver.c exists so that becomes an off-box
 *     measurement rather than an argument.
 */
