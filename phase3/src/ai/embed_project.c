/*
 * JARVIS AI-OS — Phase C / C/M2b: the mean-projection pipeline
 *
 * Pure float arithmetic over the frozen mu in embed_mu.h. No device, no seL4, no
 * allocation — host-testable in full, which is the point of it being here rather
 * than inline in main_x86.c. See embed_project.h for why the order and the
 * accumulation precision are load-bearing.
 */

#include "embed_project.h"
#include "embed_mu.h"
#include <math.h>
#include <string.h>

/* Bit pattern of a float, via memcpy. A pointer cast would be a strict-aliasing
 * violation and -O2 is entitled to miscompile it. */
static uint32_t bits_of(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

int embed_mu_verify(void)
{
    uint32_t x = 0;
    for (int i = 0; i < EMBED_MU_DIM; i++)
        x ^= bits_of(EMBED_MU[i]);
    return x == EMBED_MU_XOR ? 1 : 0;
}

/* ||mu||, accumulated in DOUBLE — see the header. Computed on demand rather than
 * cached in a static: it is 1024 multiply-adds against the ~190 M cycles already
 * spent on the embedding, and a mutable cache would be one more thing that can be
 * wrong at the moment it matters. */
static double mu_norm(void)
{
    double n2 = 0.0;
    for (int i = 0; i < EMBED_MU_DIM; i++)
        n2 += (double)EMBED_MU[i] * (double)EMBED_MU[i];
    return sqrt(n2);
}

int embed_meanproject_inplace(float *v, int dim)
{
    if (!v || dim != EMBED_MU_DIM)
        return EMBED_PROJ_E_ARGS;
    if (!embed_mu_verify())
        return EMBED_PROJ_E_MU;

    const double nmu = mu_norm();
    if (!(nmu > 0.0))
        return EMBED_PROJ_E_MU;

    /* mu_hat = mu / ||mu||, and the dot product v . mu_hat, in one pass. Both
     * accumulated in double: this is the step whose precision cm2_floor.py's
     * numpy did in float32, and double is the branch that recovers the stored
     * bits (5c159b6).
     *
     * MEASURED, so nobody has to wonder whether dividing mu by its norm matters:
     * it does not, numerically. ||mu|| is 1 - 1.3e-8, so mu_hat differs from mu by
     * less than a float32 ULP, and a mutant that drops the division changes the
     * output by 2.3e-10 — undetectable by any test at this precision. The division
     * stays because it is the transform cm2_floor.py documented and measured, not
     * because it moves the numbers. This is 5c159b6's precision finding seen from
     * the other side, and it is the reason test_embed_project cannot mutation-prove
     * this particular line. */
    double dot = 0.0;
    for (int i = 0; i < dim; i++)
        dot += (double)v[i] * ((double)EMBED_MU[i] / nmu);

    /* v -= dot * mu_hat */
    for (int i = 0; i < dim; i++)
        v[i] = (float)((double)v[i] - dot * ((double)EMBED_MU[i] / nmu));

    return EMBED_PROJ_OK;
}

int embed_project(const float *raw, int raw_dim, float *out, int out_dim)
{
    if (!raw || !out || raw_dim != EMBED_MU_DIM || out_dim <= 0 || out_dim > raw_dim)
        return EMBED_PROJ_E_ARGS;
    if (raw == out)
        return EMBED_PROJ_E_ARGS;   /* aliasing is a real hazard here — see the header */

    /* Zero `out` up front so every error path leaves it zeroed rather than
     * partially written. A half-projected vector renormalises to a perfectly
     * unit-looking vector of nothing, and g3_vec_is_unit could not tell. */
    memset(out, 0, (size_t)out_dim * sizeof(float));

    if (!embed_mu_verify())
        return EMBED_PROJ_E_MU;

    const double nmu = mu_norm();
    if (!(nmu > 0.0))
        return EMBED_PROJ_E_MU;

    /* STEP 1 — project at FULL 1024, because mu was estimated in that space.
     * The dot product runs over all raw_dim elements even though only the first
     * out_dim survive truncation: dropping the tail from the dot would compute a
     * projection against a truncated mu, which is a different transform.
     * ||raw||^2 is accumulated in the same pass for the relative-degeneracy test
     * at step 3. */
    double dot = 0.0, raw_n2 = 0.0;
    for (int i = 0; i < raw_dim; i++) {
        dot += (double)raw[i] * ((double)EMBED_MU[i] / nmu);
        raw_n2 += (double)raw[i] * (double)raw[i];
    }
    if (!(raw_n2 > 0.0)) {
        /* An all-zero input has no direction to project. Caught here rather than
         * falling through to a 0/0 renormalisation. */
        return EMBED_PROJ_E_DEGENERATE;
    }

    /* STEP 2 — truncate: write only the first out_dim projected components. The
     * discarded tail is still correctly accounted for, above. */
    double n2 = 0.0;
    for (int i = 0; i < out_dim; i++) {
        double p = (double)raw[i] - dot * ((double)EMBED_MU[i] / nmu);
        out[i] = (float)p;
        n2 += p * p;
    }

    /* STEP 3 — L2 renormalise AFTER truncation. Truncating a unit vector does not
     * leave it unit, and cosine over non-unit vectors degrades into magnitude
     * ranking.
     *
     * DEGENERACY IS RELATIVE, NOT `n2 > 0`, and the difference is not academic.
     * An input (near-)parallel to mu projects to almost nothing, and the residual
     * that survives is float rounding noise — of order 1e-12 for a unit input.
     * `n2 > 0` accepts that and renormalises the noise into a perfectly
     * well-formed unit vector, which then scores a confident cosine against
     * something arbitrary. g3_vec_is_unit cannot detect it (it IS unit) and the
     * similarity floor cannot either (the score is real). Measured: projecting mu
     * by itself yields n2 ~1e-12 against ||raw||^2 == 1.
     *
     * So the residual must be a real fraction of the input's own magnitude. Real
     * embeddings sit ~10 orders of magnitude clear of this: mu is the corpus MEAN
     * direction, so an embedding has a large mu component but is never parallel to
     * it, and the surviving 128 components carry n2 ~0.125 of a unit input. */
    if (!(n2 > EMBED_PROJ_MIN_RESIDUAL_REL * raw_n2)) {
        memset(out, 0, (size_t)out_dim * sizeof(float));
        return EMBED_PROJ_E_DEGENERATE;
    }
    const double inv = 1.0 / sqrt(n2);
    for (int i = 0; i < out_dim; i++)
        out[i] = (float)((double)out[i] * inv);

    return EMBED_PROJ_OK;
}
