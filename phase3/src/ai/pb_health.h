/*
 * pb_health.h — Phase 6 A9/2: the PB crash-loop window, host-pure.
 *
 * WHY THIS EXISTS. The self-heal funnel counts restarts in a SLIDING WINDOW, not over a lifetime:
 * five restarts back-to-back mean PB is crash-looping and PA must stop dispatching to it, while
 * five restarts spread across a week of healthy service mean nothing at all. The difference between
 * those two readings is one counter that has to be incremented in a completely different part of
 * the file from where it is tested — and getting it wrong is not a crash, it is a box that quietly
 * declares itself permanently degraded and serves cache-only from then on.
 *
 * That is not hypothetical either. K/M2b-2 STEP-2 defined KM2B_HEALTHY_RESET and zeroed the counter
 * on every restart but NEVER INCREMENTED IT, so the window never reset and the bound silently
 * became a LIFETIME cap: any five self-heals, however far apart, latched the degraded state. It
 * shipped, and it was found on the box rather than in CI, because main_x86.c is seL4-only and is
 * NEVER host-compiled — there was no test that could have caught it. This module is the test
 * surface that defect never had. test_pb_health.c T2 pins BOTH directions of the window-vs-lifetime
 * distinction, and its mutation teeth reproduce the original defect exactly.
 *
 * BEHAVIOUR-NEUTRAL EXTRACTION of main_x86.c's crash-loop counting. Every rule here is today's
 * rule, including the ones that look accidental. Follows km2b_miss / km2b_trigger / wake /
 * control_floor / pb_progress / ctrl_epi_index / embed_region. Host-tested by test_pb_health.c.
 *
 * *** WHAT THIS MODULE DELIBERATELY DOES NOT OWN: g_pb_dead. ***
 * The degraded flag is NOT a field here, and that is a design decision rather than an oversight.
 * The crash-loop bound is only ONE of its causes: probe legs latch it directly to exercise
 * degraded paths that have nothing to do with crash-loop counting, and it is READ at ~40 sites
 * including the PB_DISPATCH_OK() macro that gates every PB lane. Owning it here would create a
 * second source of truth for a flag the caller already has to latch (this API reports the trip
 * through a return value), and would force those unrelated probe sites to reach into a health
 * module for something that is not about health counting. The caller latches; this module counts.
 *
 * pb_health_may_restart() is a pure predicate for the same reason PB_DISPATCH_OK() is a macro: the
 * `restart_in_progress || pb_dead` conjunction is spelled at THREE call sites, and a guard that
 * must hold identically everywhere is worth stating once. Its justification is the centralization,
 * not the arithmetic — the arithmetic is trivial and that is the point.
 */

#ifndef PB_HEALTH_H
#define PB_HEALTH_H

#include <stdint.h>

/*
 * The window state. Both fields are public because the caller PRINTS them: the deployed [RESTART]
 * line reports `window=` after a restart. They are plain counters with no invariant this module
 * hides, so an accessor would buy nothing.
 *
 *   window  — restarts in the CURRENT window. Reset to 0 when the window closes (enough healthy
 *             responses) and never otherwise decremented.
 *   healthy — consecutive healthy PB responses since the last restart. Zeroed by every restart,
 *             and only ticks while a window is open.
 */
typedef struct {
    uint32_t window;
    uint32_t healthy;
} pb_health_t;

/* Zero both counters. A NULL handle is ignored. */
void pb_health_init(pb_health_t *h);

/*
 * A restart just EXECUTED. Bumps the window, clears the healthy count, and evaluates the bound.
 *
 * Returns 1 iff THIS call tripped the bound — the caller then latches g_pb_dead and logs [FATAL].
 * Returns 0 otherwise (including for a NULL handle, the fail-safe answer: a call that examined
 * nothing must not tell the caller to declare the box degraded).
 *
 * *** ORDER MATTERS AND IS PART OF THE CONTRACT: bump FIRST, then test. *** The restart that trips
 * the bound is still COUNTED as executed, so the deployed [RESTART] line reports `window=5` and the
 * [FATAL] line follows it. A caller that tested before bumping would report `window=4` on the
 * fatal restart and trip one restart late. To preserve that observable ordering the caller should
 * CAPTURE this return value where the bump happens and act on it AFTER printing its [RESTART] line
 * — the deployed code has the print sitting between the increment and the bound check.
 *
 * *** WHY THE BOUND IS EVALUATED ONLY HERE, ON THE EXECUTED PATH. *** The deployed funnel tests the
 * bound UNCONDITIONALLY, including on the branch where SHIELD REFUSED the restart and the window
 * was therefore not bumped. That test can never fire on the refused path, and the proof is short:
 * the window is incremented in exactly one place, and the bound check runs in the same call
 * immediately after, latching the degraded flag the moment window >= bound; the funnel early-returns
 * whenever that flag is set. So on entry the window is always strictly below the bound, and
 * re-testing it without a bump is dead code. Confining the test to this function is therefore
 * behaviour-neutral FOR A NON-ZERO BOUND — see the degenerate note below for the one case where it
 * would not be, and which the deployed compile constant cannot reach.
 *
 * bound == 0 DEGENERATES rather than being refused: `window >= 0` is trivially true for an unsigned
 * value, so every restart trips immediately. This follows pb_wait_decide's documented `cap == 0 ->
 * always TIMEOUT` precedent — one comparison, no special case, so the degenerate leg is teeth on
 * that comparison rather than on a branch written to handle it. It is also the ONE value at which
 * the refused-path reasoning above breaks (an unbumped window is still >= 0), which is worth
 * knowing and cannot happen: the deployed bound is the compile constant KM2B_CRASHLOOP_BOUND = 5.
 */
int pb_health_on_restart(pb_health_t *h, uint32_t bound);

/*
 * A coherent PB response arrived — evidence PB is working.
 *
 * Returns 1 iff the window just RESET (the caller logs the reset line), 0 otherwise.
 *
 * *** THE `window > 0` GUARD IS A SHORT-CIRCUIT, AND IT IS OBSERVABLE. *** The deployed site is a
 * single `&&` chain — `g_restart_window > 0 && ++g_healthy_since_restart >= KM2B_HEALTHY_RESET` —
 * so on a box that has never restarted, the increment DOES NOT EXECUTE AT ALL. It is tempting to
 * call the guard redundant on the grounds that the counter is zeroed at every restart anyway, and
 * that is true of the STATE but false of the OUTPUT: without the guard the count would climb
 * freely on a healthy box, reach reset_at, and print a "window reset (N healthy inferences)" line
 * every reset_at responses forever, with no restart having ever happened. Reproduced here exactly,
 * and pinned by T5 — which asserts the counter is still ZERO afterwards, so it tests that the
 * increment never ran rather than merely that the return value was 0.
 *
 * WHAT THE CALLER PRINTS. The deployed reset line reports the healthy count, read after the
 * increment and before the zeroing. That value is always exactly reset_at, so a caller reproducing
 * the line prints reset_at: the count is zeroed on every restart AND on every reset, and rises by
 * exactly one per call, so it fires the first time it reaches reset_at and can never overshoot.
 * Pinned by T1. This holds for a FIXED reset_at, which is the deployed case (the compile constant
 * KM2B_HEALTHY_RESET = 25); a caller that varied reset_at between calls could observe a count above
 * it, and nothing here prevents that because nothing here needs to.
 *
 * reset_at == 0 DEGENERATES the same way bound == 0 does: `++healthy >= 0` is trivially true, so
 * the first healthy response closes the window. The `window > 0` guard still applies, so with no
 * window open it remains a no-op. The deployed value is never 0.
 */
int pb_health_on_healthy(pb_health_t *h, uint32_t reset_at);

/*
 * The restart funnel's early-return, as a pure predicate over the two blocking flags: a restart may
 * proceed only when one is not already in progress and the box has not been declared degraded.
 *
 * Takes both flags as arguments rather than reading state, because neither belongs to this module —
 * g_restart_in_progress is the funnel's own re-entrancy latch and g_pb_dead is the degraded flag
 * discussed in the header comment. int rather than bool to match the deployed globals.
 */
int pb_health_may_restart(int restart_in_progress, int pb_dead);

#endif /* PB_HEALTH_H */
