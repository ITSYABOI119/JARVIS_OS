/*
 * JARVIS AI-OS — Phase 6 K/M0: the static action allowlist (K-b)
 *
 * v1: exactly TWO deployed entries. Additions are a human-reviewed PR to this
 * table (compile-time), never runtime registration.
 */

#include "action_allowlist.h"
#include <stddef.h>

static const action_def_t g_allowlist[] = {
    { ACTION_RESTART_PB,     "restart_pb",     TRUST_NOTIFY, ACTION_CLASS_SELF_HEAL },
    { ACTION_NOTIFY_ANOMALY, "notify_anomaly", TRUST_AUTO,   ACTION_CLASS_NOTIFY    },
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
