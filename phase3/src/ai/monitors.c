/*
 * JARVIS AI-OS — Phase 6 goal 6-1 M0: the pure monitor framework.
 * Host-pure (no seL4 dependency). Contract + rationale in monitors.h.
 * Design: phase6/docs/PHASE_6_GOAL_6-1_MONITORS.md.
 */

#include "monitors.h"
#include <stdio.h>

void monitor_init(monitor_t *m, int64_t threshold, monitor_cmp_t cmp, uint16_t debounce)
{
    if (!m) return;
    m->threshold = threshold;
    m->cmp       = cmp;
    m->debounce  = debounce;
    m->consec    = 0;
    m->armed     = 1;
}

int monitor_sample(monitor_t *m, int64_t value)
{
    if (!m) return 0;
    uint16_t eff = m->debounce ? m->debounce : 1;   /* debounce 0 acts as 1 */
    int trig = (m->cmp == MON_CMP_LE) ? (value <= m->threshold)
                                      : (value >= m->threshold);
    if (!trig) {
        /* The non-triggering side: reset the consecutive run and RE-ARM (hysteresis —
         * only a genuine clear-then-recross can fire again). */
        m->consec = 0;
        m->armed  = 1;
        return 0;
    }
    if (!m->armed) return 0;   /* already fired for this sustained condition */
    if (++m->consec >= eff) {
        m->armed  = 0;         /* fire exactly ONCE per debounced crossing */
        m->consec = 0;
        return 1;
    }
    return 0;
}

uint64_t monitor_delta_step(monitor_delta_t *d, uint64_t cumulative, uint32_t boot_id)
{
    if (!d) return 0;
    if (!d->have_prev || d->boot_id != boot_id) {
        /* First sample, or the counter restarted with a new boot — baseline, report 0
         * (a per-boot counter's post-reboot value is not a delta from the prior boot). */
        d->prev      = cumulative;
        d->boot_id   = boot_id;
        d->have_prev = 1;
        return 0;
    }
    uint64_t delta = cumulative - d->prev;
    d->prev = cumulative;
    return delta;
}

int monitor_build_snapshot(monitor_event_t *ev, monitor_event_type_t type,
                           uint8_t severity, int64_t v1, int64_t v2)
{
    if (!ev || type <= MON_EV_NONE || type >= MON_EV__COUNT) return -1;

    /* Fixed literals + decimal counters ONLY (keyword-clean by construction — never
     * query text, never free text; T7 pins this against the canonical blocklist AND
     * the real shield_assess). */
    int n;
    switch (type) {
    case MON_EV_ERROR_RATE:
        n = snprintf(ev->snap, MON_SNAP_MAX, "mon err-rate d=%lld win=%lld",
                     (long long)v1, (long long)v2);
        break;
    case MON_EV_SELF_HEAL_RATE:
        n = snprintf(ev->snap, MON_SNAP_MAX, "mon heal-rate d=%lld win=%lld",
                     (long long)v1, (long long)v2);
        break;
    case MON_EV_STORE_WRAP:
        n = snprintf(ev->snap, MON_SNAP_MAX, "mon store-wrap id=%lld total=%lld",
                     (long long)v1, (long long)v2);
        break;
    case MON_EV_HEARTBEAT_AGE:
        n = snprintf(ev->snap, MON_SNAP_MAX, "mon hb-age ms=%lld thr=%lld",
                     (long long)v1, (long long)v2);
        break;
    case MON_EV_UPTIME_MILESTONE:
        n = snprintf(ev->snap, MON_SNAP_MAX, "mon uptime ms=%lld mark=%lld",
                     (long long)v1, (long long)v2);
        break;
    case MON_EV_DEGRADED:
        /* 6-3 B5: PB dead, the box latched cache-only. v1 = the miss count that
         * tripped the latch (v2 unused). System facts only — keyword-clean. */
        n = snprintf(ev->snap, MON_SNAP_MAX, "mon degraded cache-only miss=%lld",
                     (long long)v1);
        break;
    default:
        return -1;
    }
    if (n < 0) return -1;
    if (n >= MON_SNAP_MAX) n = MON_SNAP_MAX - 1;   /* snprintf truncated; still NUL'd */

    ev->type     = type;
    ev->severity = severity;
    ev->snap_len = (uint16_t)n;
    return n;
}
