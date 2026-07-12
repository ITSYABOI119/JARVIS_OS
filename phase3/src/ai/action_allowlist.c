/*
 * JARVIS AI-OS — Phase 6 K/M0: the static action allowlist (K-b)
 *
 * FOUR deployed entries (2 at K/M0 + wake-consult at 6-2/M0 + status-digest at
 * 6-3/M0). Additions are a human-reviewed PR to this table (compile-time),
 * never runtime registration.
 */

#include "action_allowlist.h"
#include <stddef.h>

static const action_def_t g_allowlist[] = {
    { ACTION_RESTART_PB,     "restart_pb",     TRUST_NOTIFY, ACTION_CLASS_SELF_HEAL },
    { ACTION_NOTIFY_ANOMALY, "notify_anomaly", TRUST_AUTO,   ACTION_CLASS_NOTIFY    },
    /* 6-2: the event-triggered consult — TRUST_NOTIFY (monotonic with the
     * restart: a consult burns 10-120 s of compute, it must not sit BELOW the
     * self-heal in ceremony; functionally identical to AUTO pre-control-IN). */
    { ACTION_WAKE_CONSULT,   "wake-consult",   TRUST_NOTIFY, ACTION_CLASS_CONSULT   },
    /* 6-3 B4: the boot-relative status digest — TRUST_AUTO (L0: an inform's
     * "execute" = its serial line + JACT record), class NOTIFY REUSED (base
     * risk 0 — a digest is a notify-shaped inform, not a compute-burning
     * consult; no new class). */
    { ACTION_STATUS_DIGEST,  "status-digest",  TRUST_AUTO,   ACTION_CLASS_NOTIFY    },
};

#define ALLOWLIST_N (sizeof(g_allowlist) / sizeof(g_allowlist[0]))

const action_def_t *action_lookup(uint16_t id)
{
    if (id == 0)
        return NULL;   /* 0 is never a valid action id */
    for (size_t i = 0; i < ALLOWLIST_N; i++)
        if (g_allowlist[i].id == id)
            return &g_allowlist[i];
    return NULL;
}

uint32_t action_allowlist_count(void)
{
    return (uint32_t)ALLOWLIST_N;
}
