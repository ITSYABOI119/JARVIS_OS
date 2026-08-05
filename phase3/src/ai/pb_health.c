/* Phase 6 A9/2 — the PB crash-loop window, host-pure. See pb_health.h.
 *
 * Deliberately thin: the value is not the code, it is that the window-vs-lifetime rule now exists
 * somewhere a host test can reach it. The defect this replaces was a MISSING increment, which no
 * amount of care in main_x86.c would have caught, because main_x86.c is never host-compiled.
 *
 * Behaviour-neutral extraction of main_x86.c's crash-loop counting. Host-tested by
 * test_pb_health.c. */

#include "pb_health.h"

void pb_health_init(pb_health_t *h)
{
    if (!h)
        return;
    h->window  = 0;
    h->healthy = 0;
}

int pb_health_on_restart(pb_health_t *h, uint32_t bound)
{
    if (!h)
        return 0;   /* fail-safe: a call that examined nothing must not order a degrade */

    /* Bump BEFORE the test: the restart that trips the bound is still counted, so the caller's
     * [RESTART] line reports the tripping window value and [FATAL] follows it. */
    h->window++;
    h->healthy = 0;

    /* One comparison, which also gives bound == 0 its always-trip degenerate for free (every
     * unsigned value is >= 0) rather than through a branch written to handle it. */
    return h->window >= bound;
}

int pb_health_on_healthy(pb_health_t *h, uint32_t reset_at)
{
    if (!h)
        return 0;

    /* THE SHORT-CIRCUIT, reproduced exactly. No window open means the increment does not run at
     * all — not merely that the result is discarded. Dropping this would leave the count climbing
     * on a healthy box and printing a spurious reset line every reset_at responses forever. */
    if (h->window == 0)
        return 0;

    if (++h->healthy < reset_at)
        return 0;

    h->window  = 0;
    h->healthy = 0;
    return 1;
}

int pb_health_may_restart(int restart_in_progress, int pb_dead)
{
    return !restart_in_progress && !pb_dead;
}
