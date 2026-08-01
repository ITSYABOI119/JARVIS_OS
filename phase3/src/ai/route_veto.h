/*
 * JARVIS AI-OS — Phase C / C/M4: the HYBRID ROUTING VETO (decision core)
 *
 * route_classify (route.c) is a coarse KEYWORD classifier, and it has a measured,
 * live defect: a CONCEPTUAL question that merely contains a status word is
 * captured as SYSFACTS and answered from box state. "what causes page faults?"
 * comes back as a health report. The same class killed the DECLINE side at the
 * 6-6 flip; route.h's own conservative rule ("any ambiguity -> ROUTE_INFER, never
 * toward a wrong fact") is what such a capture violates.
 *
 * This module is the fix's decision core: given the query's 128-dimension
 * mean-projected unit vector and the keyword verdict, decide whether an embedder
 * should VETO a SYSFACTS capture and let the query fall through to the model.
 *
 *   IT IS NOT WIRED TO ANYTHING. Nothing calls it; route.c is untouched. Wiring
 *   is a later, separately-gated slice.
 *
 * ── WHAT WAS MEASURED (phase3/scripts/embed/cm4_routing_results.json) ─────────
 *
 *   Arm A  keyword only (deployed)   FP 32/32 conceptual   FN  0/37   HELDOUT 70/73 = 95.89%
 *   Arm B  pure nearest-centroid     FP  7/32              FN  6/37   HELDOUT 66/73 = 90.41%   CLOSED
 *   Arm C  hybrid veto (THIS)        FP  6/32              FN  1/37   HELDOUT 70/73 = 95.89%   WORTH BUILDING
 *
 * So: 32 wrong -> 7 wrong, held-out accuracy UNCHANGED. That is the whole case
 * for this module, and the four caveats below are part of it, not footnotes.
 *
 *   1. THE HELD-OUT NUMBER IS "COSTS NOTHING", NOT "CONFIRMS THE VETO".
 *      Arm C's HELDOUT equals baseline because the veto fired on ZERO held-out
 *      cases. Zero firings is evidence of NO HARM there; it is not held-out
 *      validation of the veto, because the veto was never exercised. Never write
 *      "95.89% held-out with the veto" as if the veto earned it.
 *
 *   2. THE CORPUS IS PARTLY SELF-AUTHORED. 55 of the 69 two-sided questions were
 *      written by the session that scored them (the goal doc's §9 was docs-only
 *      and enumerates ~14). The results file carries a selection-bias audit
 *      splitting the gain by source; read it before quoting 32 -> 6.
 *
 *   3. SIX FALSE POSITIVES SURVIVE. The veto REDUCES the defect; it does not
 *      remove it. "what causes page faults?" is fixed; "how many cpus does a
 *      raspberry pi have" is still captured.
 *
 *   4. THE ONE FALSE NEGATIVE IS NOT FREE. Vetoing a genuine status question
 *      ("what gguf is in use") hands a box-state question to a model that cannot
 *      know the answer — which is how it gets fabricated. An FN is a different
 *      harm from an FP, not a smaller one, and it is exactly what killed the
 *      keyword-anchor attempt (goal doc §9). 1 in 37 is the measured price.
 *
 * ── THE RULE IS PARAMETER-FREE, AND THAT IS LOAD-BEARING ─────────────────────
 *
 *   fire  <=>  kw_cls == ROUTE_SYSFACTS  &&  sim(v, INFER) > sim(v, SYSFACTS)
 *
 * No margin, no threshold, no confidence gate. Any knob would make this a
 * DIFFERENT configuration than the one Arm C's verdict covers, and a knob tuned
 * until the numbers look right is tuning against the very set the verdict is read
 * from. If a margin is ever wanted it needs its own measurement, not a #define.
 *
 * STRICT `>`: at an exact tie the KEYWORD decision stands. The veto is the
 * challenger, so it must strictly win to overturn; ties go to the incumbent.
 *
 * ── SYSFACTS ONLY. DECLINE CAPTURES ARE NEVER VETOED. ────────────────────────
 *
 * The measured Arm C allowed the veto to overturn SYSFACTS *or* DECLINE. This
 * module deliberately narrows that to SYSFACTS.
 *
 * WHY THE NARROWING COSTS NOTHING MEASURED (re-derived from the results file,
 * not assumed): all 32 of Arm A's false positives were SYSFACTS captures — zero
 * DECLINE; all 37 genuine cases were routed SYSFACTS; and the veto fired on zero
 * HELDOUT cases. There is therefore no case anywhere in the measurement where a
 * DECLINE capture was vetoed, so on that data SYSFACTS-only and SYSFACTS/DECLINE
 * are the same function and Arm C's numbers carry over unchanged.
 *
 * WHY IT IS WORTH NARROWING ANYWAY: vetoing a genuine DECLINE sends a no-source
 * metric question ("what is your cpu usage?") to the model to be FABRICATED —
 * precisely the failure route_decline_text() exists to prevent, and a risk class
 * the measurement never priced because it never occurred. A supporting
 * observation, measured on the frozen header: the SYSFACTS and DECLINE centroids
 * sit at cos 0.678 of each other, while INFER and SYSFACTS sit at -0.028 — so a
 * DECLINE-vs-INFER comparison would run in a far less separated region than the
 * one Arm C actually exercised. That is a reason for caution, not the argument;
 * the argument is that fabrication is the worse failure and it was never priced.
 *
 * ── STRUCTURAL SAFETY: THE VETO NEVER CREATES A CAPTURE ──────────────────────
 *
 * ROUTE_INFER in -> 0 out, always. The veto can only move a query from a
 * box-state answer TO the model; it can never move one INTO a box-state answer.
 * INFER is route.h's conservative default, so the worst outcome is caveat 4's
 * false negative — never a new fabricated fact. The measurement asserted this
 * property at runtime rather than trusting its own prose
 * (arm_c_veto_only_claim.captures_created_from_infer == 0); test_route_veto.c
 * pins it here.
 *
 * ── HONESTY CEILING ──────────────────────────────────────────────────────────
 *
 * This is a nearest-of-two-centroids comparison in a 128-dimension space. It is
 * not comprehension, not intent detection, and not a general fix for keyword
 * routing. The honest claim is: "on a 69-question two-sided corpus, allowing an
 * embedder to veto a SYSFACTS capture cut conceptual-question captures from 32 to
 * 6 at a cost of 1 of 37 genuine status questions, with held-out accuracy
 * unchanged." Never "the router now understands the question."
 *
 * Host-pure: <stdint.h> only, no seL4 / device / allocator dep, no malloc, no
 * I/O — the route.c / wake.c / km2b_miss.c precedent.
 */

#ifndef ROUTE_VETO_H
#define ROUTE_VETO_H

#include "route.h"   /* route_class_t */

/*
 * Required length of the query vector, in floats.
 *
 * Deliberately NOT `#include "route_centroids.h"` here. That header carries 3x128
 * floats of `static const` data, so including it from this header would emit a
 * copy of the centroid array in every translation unit that wants the veto API.
 * Restating the constant keeps the data in exactly ONE object file (route_veto.o),
 * and route_veto.c pins ROUTE_VETO_DIM == ROUTE_CENTROIDS_DIM with a
 * _Static_assert so the restatement cannot drift — the project rule is that a
 * restated constant must break the build if it diverges, not that constants are
 * never restated (embed_region.h's EMBED_DIM vs embed_store.h's EMBED_STORE_DIM
 * is the same shape).
 */
#define ROUTE_VETO_DIM  128

/*
 * Should the embedder veto this keyword capture?
 *
 * `v128` is ROUTE_VETO_DIM floats: the query embedded and put through the C/M2
 * pipeline (embed 1024 -> mean-project with the frozen mu -> truncate 128 -> L2),
 * i.e. the SAME space the centroids live in and the same one g3_select_semantic
 * compares in. Passing a raw or unprojected vector compares across two different
 * spaces and yields confident nonsense; there is no way for this function to
 * detect that, so it is the caller's contract.
 *
 * Returns 1 iff kw_cls == ROUTE_SYSFACTS AND v128 is STRICTLY closer to the INFER
 * centroid than to the SYSFACTS centroid. Returns 0 for everything else:
 * ROUTE_INFER (nothing to veto), ROUTE_DECLINE (out of scope, see the header), an
 * out-of-range class value, a NULL pointer, or a tie.
 *
 * A returned 1 means "the keyword capture is probably wrong; route to the model
 * instead". It does NOT mean the query is definitely conceptual — 1 of 37 genuine
 * status questions was vetoed in the measurement.
 *
 * Pure and reentrant: reads only the compiled centroids and v128[0..DIM-1],
 * mutates nothing, allocates nothing.
 *
 * SCALE-INVARIANCE (why a slightly-off-unit vector cannot flip the verdict): both
 * sides of the comparison are dotted against the SAME v, so multiplying v by any
 * positive scalar scales both equally and the verdict is unchanged. The decision
 * depends on v's DIRECTION alone. That does NOT extend to the centroids, whose
 * stored float32 norms differ from each other by ~9e-9 — see route_veto.c.
 */
int route_veto_should(const float *v128, route_class_t kw_cls);

/*
 * Verify the compiled ROUTE_CENTROIDS against the generator's npz-derived
 * ROUTE_CENTROIDS_XOR.
 *
 * Returns 0 on match, non-zero on mismatch.
 *
 * NOTE THE POLARITY. This is 0-is-good, the opposite of embed_project.h's
 * embed_mu_verify() (1-is-good). The two are not interchangeable and a
 * copy-pasted `if (!verify())` from one to the other inverts the gate — which
 * fails OPEN, the direction that matters. The polarity here is deliberate (0 ==
 * OK is this file's locked API) and is pinned by test_route_veto.c.
 *
 * THE CALLER MUST REFUSE TO VETO ON A NON-ZERO RETURN. A corrupted centroid is
 * not a degraded router, it is a silently WRONG one: every number above was
 * measured against these exact vectors, so different bits mean a different
 * decision boundary while the docs still claim the old one. Falling back to the
 * keyword verdict costs the 32 -> 6 improvement; continuing costs correctness
 * with no signal.
 *
 * WHY THIS IS SEPARATE FROM route_veto_should RATHER THAN CHECKED PER CALL (the
 * embed_project.c pattern is per-call, so the deviation is deliberate): a
 * per-call verify would have to fail by returning 0 — "no veto" — which is
 * INDISTINGUISHABLE from the common, healthy answer. Corruption would present as
 * "the veto quietly stopped helping". Gated once at init, corruption presents as
 * a refused lane the operator can see, which is the C/M2b embed_mu_verify()
 * init-gate shape.
 *
 * WHAT IT CANNOT CATCH: XOR is commutative, so a PERMUTATION of the three rows
 * verifies clean. That is not a gap here — route_centroids.h pins row order to
 * route_class_t order with _Static_asserts, which is a compile-time check and
 * strictly stronger than a runtime one.
 */
int route_veto_centroids_verify(void);

#endif /* ROUTE_VETO_H */
