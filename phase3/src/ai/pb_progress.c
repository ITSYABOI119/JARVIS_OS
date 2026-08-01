/* Phase 6 N2 — PB liveness tick, host-pure decision half. See pb_progress.h.
 *
 * Deliberately one expression: the whole value of extracting this is that the extend/timeout
 * rule is stated ONCE, in a place that is host-compiled and host-tested. Two conditions, both
 * load-bearing — the counter moved (INEQUALITY, never ordering: it wraps), and the extension
 * budget is not spent (which also gives cap == 0 its always-TIMEOUT degenerate for free, since
 * no unsigned value is < 0). Host-tested by test_pb_progress.c. */

#include "pb_progress.h"

pb_wait_verdict_t pb_wait_decide(uint32_t progress_start, uint32_t progress_now,
                                 uint32_t windows_used, uint32_t window_cap)
{
    if (progress_now != progress_start && windows_used < window_cap) {
        return PB_WAIT_EXTEND;
    }
    return PB_WAIT_TIMEOUT;
}
