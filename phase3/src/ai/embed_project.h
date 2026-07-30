/*
 * JARVIS AI-OS — Phase C / C/M2b: the mean-projection pipeline
 *
 * Turns PB's RAW 1024-dimension embedding into the 128-dimension unit vector
 * g3_select_semantic compares at the measured 0.55 floor:
 *
 *     embed(1024)  ->  mean-project with mu in 1024  ->  truncate(128)  ->  L2
 *
 * WHY THIS IS ITS OWN MODULE AND NOT INLINE IN main_x86.c
 * ------------------------------------------------------
 * `main_x86.c` is seL4-only and is NEVER host-compiled, so anything living there
 * has no test. This is pure float arithmetic with an order that matters and a
 * precision contract that is easy to get subtly wrong, and it sits between two
 * things that ARE tested (the parity-verified 1024 embedding on one side,
 * g3_select_semantic on the other). Inline, it would be the only stage of the
 * pipeline with nothing checking it.
 *
 * That follows the unbroken precedent in this tree — km2b_miss, km2b_trigger,
 * wake, control_floor, control_console, embed_region: pure logic leaves
 * main_x86.c so it can be host-tested and CI-gated.
 *
 * ORDER IS LOAD-BEARING, NOT STYLISTIC
 * ------------------------------------
 * Projection happens at 1024 and truncation AFTER it, because `mu` is a frozen
 * 1024-dimension artifact estimated in that space (cm2_floor.py's documented
 * order). Projecting in a truncated space with a truncated mu is a DIFFERENT
 * transform and is not what produced the numbers in cm2_floor_results.json.
 *
 * Renormalisation happens AFTER truncation, because truncating a unit vector
 * does not leave it unit — and cosine over non-unit vectors silently degrades
 * into magnitude ranking. g3_vec_is_unit exists to catch exactly that, and it
 * fires 15/15 on vectors truncated without renormalising (measured, efe5edd).
 *
 * THE NORM IS ACCUMULATED IN DOUBLE — a measured choice, not a default
 * --------------------------------------------------------------------
 * `5c159b6` measured that mu's renormalisation is PRECISION-DEPENDENT: a
 * double-accumulated norm of the stored mu is 0.9999999873, which rounds to
 * exactly 1.0f and recovers the stored bits (0/1024 elements move), while a
 * float32 accumulation lands on 0.99999994/0.99999952 and moves all 1024 by one
 * ULP each. cm2_floor.py renormalised via numpy in float32, so neither branch
 * reproduces it bit-exactly on the box; the difference is ~1e-7 on a cosine
 * against a floor whose measured margins are two orders larger, so it is
 * immaterial to the operating point.
 *
 * Double is chosen for the reason that survives that wash: a 1024-term sum of
 * squares in float32 accumulates real rounding error, and using double costs
 * nothing next to the ~190 M cycles the embedding itself took.
 */

#ifndef EMBED_PROJECT_H
#define EMBED_PROJECT_H

#include <stdint.h>

/* Result codes — distinguishable so a caller can log WHY a projection failed
 * rather than reporting a generic miss. A silently-wrong vector is the failure
 * mode this whole arc is built to avoid, so every rejection is nameable. */
#define EMBED_PROJ_OK            0
#define EMBED_PROJ_E_ARGS       -1   /* NULL, or a dim that is not the compiled pair */
#define EMBED_PROJ_E_MU         -2   /* EMBED_MU_XOR mismatch — mu itself is wrong */
#define EMBED_PROJ_E_DEGENERATE -3   /* residual is noise, not a direction (see below) */

/*
 * Minimum surviving residual, as a fraction of the input's own squared norm,
 * for a projection to count as a real direction.
 *
 * WHY RELATIVE AND WHY IT EXISTS AT ALL: an input (near-)parallel to mu projects
 * to almost nothing, and what survives is float rounding noise. A `n2 > 0` test
 * accepts that and renormalises the noise into a perfectly well-formed unit
 * vector, which then scores a confident cosine against something arbitrary.
 * NEITHER downstream guard can catch it — g3_vec_is_unit sees a genuine unit
 * vector, and the 0.55 floor sees a genuine score. This is the one place it is
 * detectable.
 *
 * The threshold sits in a ~10-order-of-magnitude gap measured on real data:
 * projecting mu by itself leaves n2 ~1e-12 against ||raw||^2 == 1, while a real
 * unit embedding leaves ~0.125 in its surviving 128 components (mu is the corpus
 * MEAN direction, so embeddings have a large mu component but are never parallel
 * to it). Anything in 1e-11..1e-6 would separate those equally well; 1e-9 is the
 * middle of the gap.
 */
#define EMBED_PROJ_MIN_RESIDUAL_REL  1e-9

/*
 * Verify the compiled `mu` against the generator's npz-derived XOR.
 *
 * Returns 1 if EMBED_MU matches EMBED_MU_XOR, 0 otherwise.
 *
 * WHY A CALLER SHOULD REFUSE TO ENABLE SEMANTIC RECALL ON 0: a silently wrong mu
 * is a silently wrong FLOOR. The 0.55 threshold is a measurement taken against
 * this exact projection, so a corrupted or regenerated mu does not degrade
 * recall gracefully — it moves the operating point while every number in the
 * docs still claims the old one. Failing closed costs recall; continuing costs
 * correctness with no signal.
 */
int embed_mu_verify(void);

/*
 * The full pipeline. `raw` is `raw_dim` floats as published by PB (unprojected,
 * the parity-verified artifact); `out` receives `out_dim` unit floats.
 *
 * raw_dim MUST equal EMBED_MU_DIM (1024) and out_dim must be <= raw_dim; both
 * are parameters rather than compile-time constants so a re-measure at another
 * dimension needs no edit here — the same reason g3_select_semantic takes dim
 * and floor as parameters.
 *
 * `out` may NOT alias `raw`: projection reads every element of raw after it has
 * begun writing out would be a real hazard, so the two are required distinct and
 * the caller keeps separate buffers.
 *
 * Returns EMBED_PROJ_OK, or one of the negative codes above. On any error `out`
 * is left ZEROED rather than partially written — a half-projected vector is a
 * valid-looking unit-ish vector of nothing, and g3_vec_is_unit could not tell.
 *
 * Verifies mu on every call. That is 1024 XORs against ~190 M cycles already
 * spent producing `raw`, i.e. free, and it means a corrupted mu cannot be used
 * even once.
 */
int embed_project(const float *raw, int raw_dim, float *out, int out_dim);

/*
 * The projection alone, in place, without truncation or renormalisation:
 *     v -= (v . mu_hat) * mu_hat
 * Exposed so the test can check the defining geometric property directly — the
 * result must be ORTHOGONAL to mu — rather than only checking the composed
 * pipeline's output length. A composed check can pass while the projection
 * itself is wrong (e.g. subtracting twice, or normalising mu in the wrong
 * precision) because the final L2 hides magnitude errors.
 *
 * `dim` must equal EMBED_MU_DIM. Returns EMBED_PROJ_OK or a negative code.
 */
int embed_meanproject_inplace(float *v, int dim);

#endif /* EMBED_PROJECT_H */
