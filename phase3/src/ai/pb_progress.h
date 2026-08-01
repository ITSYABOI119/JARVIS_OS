/*
 * pb_progress.h — Phase 6 N2: the PB liveness tick, host-pure decision half.
 *
 * WHY THIS EXISTS. A slow-but-ALIVE generation is today indistinguishable from a wedged PB.
 * PA polls for a control-IN response with a fixed budget; when that budget is exhausted the
 * lane records a miss (KM2B_LANE_CTRL), and THREE consecutive misses funnel pa_restart_pb().
 * M2 raised the control-IN cap to 250 tokens, which at the deployed 5.46 tok/s is a MEASURED
 * worst case of ~46 s for a single legitimate answer — sitting against a poll budget whose
 * only description in-tree is an UNMEASURED "~60-120 s" comment. So the margin between
 * "answering" and "presumed wedged" is not a number anyone has measured, and the failure it
 * produces is the worst kind: PA restarts a healthy PB in the middle of a correct answer.
 *
 * THE LOCKED DIRECTION — a liveness TICK, not a bigger timeout. Raising the poll budget would
 * blind the K/M2c wedge detector by exactly the margin it adds: a genuinely wedged PB would go
 * unnoticed for the whole new budget, every lane, every time. The tick instead lets PA EXTEND
 * only a wait it can SEE is alive (the counter moved, so tokens are still being produced),
 * while a wedge produces no movement and therefore still times out in exactly today's budget.
 * The detector's sensitivity to a real wedge is unchanged; only demonstrably-live waits are
 * granted more time.
 *
 * SCOPE. The CONTROL-IN lane (KM2B_LANE_CTRL) wait ONLY. The heartbeat, shield and workload
 * lanes are untouched, as is the K/M2c wedge-detection path itself (km2b_miss.c): when this
 * returns PB_WAIT_TIMEOUT the caller takes exactly today's path, miss and all.
 *
 * PRODUCER/CONSUMER CONTRACT (the embed_region.h publish discipline, x86-TSO):
 *   producer (PB): bump the counter once per GENERATED TOKEN, RELEASE-store
 *   consumer (PA): ACQUIRE-load the counter only at window boundaries — once when the window
 *                  opens (that value is progress_start) and once when it is exhausted
 *                  (progress_now)
 * This function is itself memory-order-agnostic: it takes two values the caller has ALREADY
 * loaded, exactly as embed_result_usable() decides usability without performing the loads.
 *
 * Host-pure: no seL4 dependency, no state, no globals — a pure function of its four arguments.
 * main_x86.c is seL4-only and is NEVER host-compiled, so anything left in it cannot be tested
 * at all; this is the one place a subtle bug in the extend/timeout decision could hide, so it
 * is extracted here, following km2b_miss / km2b_trigger / wake / control_floor / embed_region.
 * Host-tested by test_pb_progress.c.
 */

#ifndef PB_PROGRESS_H
#define PB_PROGRESS_H

#include <stdint.h>

typedef enum { PB_WAIT_TIMEOUT = 0, PB_WAIT_EXTEND = 1 } pb_wait_verdict_t;

/*
 * Decide what an exhausted CTRL-lane poll window means.
 *
 * progress_start = the counter ACQUIRE-loaded when the window opened;
 * progress_now   = the counter now; windows_used = extensions already granted.
 *
 * Returns PB_WAIT_EXTEND iff the counter MOVED (progress_now != progress_start) AND the
 * extension budget is not exhausted (windows_used < window_cap). Everything else is
 * PB_WAIT_TIMEOUT — there is deliberately no third verdict and no partial credit.
 *
 * *** THE COMPARISON IS INEQUALITY ONLY, NEVER ORDERING. *** The counter is a uint32 that
 * wraps once per 2^32 generated tokens, and at that wrap the newest value is numerically the
 * SMALLEST — so `progress_now > progress_start` inverts exactly once per wrap and reports a
 * live generation as a wedge. "Did it move" is genuinely an equality question, not an ordering
 * one, and equality has no wrap behaviour to get wrong. This is the same pin embed_region.h
 * puts on its `seq`; this repo compares wrapping counters with == / != and never with < / >.
 *
 * window_cap == 0 degenerates to exactly today's behaviour (always PB_WAIT_TIMEOUT, whatever
 * the progress), which is also the KVM negative-control gate leg. That falls out of the SAME
 * unsigned `windows_used < window_cap` test — no special case — because no unsigned value is
 * less than 0. Deliberate: one budget check, so the degenerate leg is teeth on that check.
 */
pb_wait_verdict_t pb_wait_decide(uint32_t progress_start, uint32_t progress_now,
                                 uint32_t windows_used, uint32_t window_cap);

#endif /* PB_PROGRESS_H */
