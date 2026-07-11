/* Phase 6 6-2/M0 — the pure wake decision core. See wake.h for the injection
 * boundary + anti-loop semantics; design: PHASE_6_GOAL_6-2_EVENT_WAKE.md. */

#include "wake.h"
#include <stddef.h>

/* ---- the STATIC, human-reviewed template allowlist (§5) ----
 * FIXED text — no interpolation (the cache key IS this text). v1 maps ONLY the
 * two degradation events; benign liveness events deliberately do NOT wake.
 * Every entry is host-pinned keyword-clean BOTH ways (test_wake.c B) — mind
 * SUBSTRING traps when editing ("skill" contains "kill", "information"
 * contains "format"). Editing a template changes its cache key and orphans its
 * episodic/cache lineage: a changed template is a NEW consult by design. */
static const wake_template_t g_wake_templates[] = {
    { MON_EV_ERROR_RATE,
      "The system error rate just spiked. Summarize the most likely causes and the first safe steps to review.",
      "err-rate" },
    { MON_EV_SELF_HEAL_RATE,
      "The self-repair lane has been busy restarting the inference process. List the first safe checks to review.",
      "heal-rate" },
};
#define WAKE_TEMPLATE_N (sizeof(g_wake_templates) / sizeof(g_wake_templates[0]))

const wake_template_t *wake_template_lookup(monitor_event_type_t type)
{
    for (size_t i = 0; i < WAKE_TEMPLATE_N; i++)
        if (g_wake_templates[i].type == type)
            return &g_wake_templates[i];
    return NULL;
}

/* Bounded append of a NUL-terminated literal (the km2b_trigger style). */
static int wb_ts(char *buf, uint32_t cap, int p, const char *s)
{
    while (*s && p < (int)cap - 1) buf[p++] = *s++;
    return p;
}

/* Bounded append of a u32 in decimal. */
static int wb_tu(char *buf, uint32_t cap, int p, uint32_t v)
{
    char d[12]; int n = 0;
    if (!v) d[n++] = '0'; else while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
    while (n && p < (int)cap - 1) buf[p++] = d[--n];
    return p;
}

int wake_build_query(monitor_event_type_t type, char *buf, uint32_t cap)
{
    const wake_template_t *t = wake_template_lookup(type);
    if (!t || !buf || cap < 1)
        return -1;
    int p = wb_ts(buf, cap, 0, t->query);
    buf[p] = '\0';
    return p;
}

int wake_build_trigger(monitor_event_type_t type, wake_route_t route, uint32_t ms,
                       char *buf, uint32_t cap)
{
    const wake_template_t *t = wake_template_lookup(type);
    if (!t || !buf || cap < 1)
        return -1;
    int p = 0;
    p = wb_ts(buf, cap, p, "wake ");
    p = wb_ts(buf, cap, p, t->event_literal);
    p = wb_ts(buf, cap, p, " route=");
    p = wb_ts(buf, cap, p, route == WAKE_ROUTE_CACHE ? "cache"
                         : route == WAKE_ROUTE_INFER ? "infer" : "none");
    p = wb_ts(buf, cap, p, " ms=");
    p = wb_tu(buf, cap, p, ms);
    buf[p] = '\0';
    return p;
}

void wake_gate_init(wake_gate_t *g)
{
    if (!g) return;
    g->pending = MON_EV_NONE;
    for (int i = 0; i < MON_EV__COUNT; i++) {
        g->last_fire_ms[i] = 0;
        g->fired[i] = 0;
    }
    g->budget_bucket = 0;
    g->budget_bcount = 0;
    g->dropped = 0;
    g->suppressed = 0;
}

int wake_gate_stage(wake_gate_t *g, monitor_event_type_t type)
{
    if (!g) return 0;
    /* STAGE GUARD (§7.3): only a TEMPLATED type may touch the latch — a
     * benign/unmapped event can neither wake nor shadow a real one. */
    if (wake_template_lookup(type) == NULL)
        return 0;
    if (g->pending != MON_EV_NONE) {
        g->dropped++;   /* templated drops only — a benign no-op is not a drop */
        return -1;
    }
    g->pending = type;
    return 1;
}

monitor_event_type_t wake_gate_take(wake_gate_t *g)
{
    if (!g) return MON_EV_NONE;
    monitor_event_type_t t = g->pending;
    g->pending = MON_EV_NONE;
    return t;
}

wake_verdict_t wake_gate_try(wake_gate_t *g, monitor_event_type_t type, uint32_t now_ms)
{
    /* Defensive: the caller passes a type from wake_gate_take (templated by
     * construction). NULL/out-of-range never ALLOWs and changes no state. */
    if (!g || type <= MON_EV_NONE || type >= MON_EV__COUNT)
        return WAKE_SUPPRESS_COOLDOWN;

    /* Cooldown FIRST (per-type). Wrap-safe uint32 subtraction — never absolute
     * marks (jarvis_uptime_ms wraps ~49.7 d). Cold start (never fired) = no
     * cooldown. */
    if (g->fired[type] &&
        (uint32_t)(now_ms - g->last_fire_ms[type]) < WAKE_COOLDOWN_MS) {
        g->suppressed++;
        return WAKE_SUPPRESS_COOLDOWN;
    }

    /* Global hourly budget: a bucket-INDEX change resets the count (wrap-safe
     * — the index simply changes at the wrap; never a monotonic threshold). */
    uint32_t bucket = now_ms / 3600000u;
    if (bucket != g->budget_bucket) {
        g->budget_bucket = bucket;
        g->budget_bcount = 0;
    }
    if (g->budget_bcount >= WAKE_BUDGET_PER_HOUR) {
        g->suppressed++;
        return WAKE_SUPPRESS_BUDGET;
    }

    /* Commit: a suppressed wake changed nothing; an ALLOWED one is recorded. */
    g->last_fire_ms[type] = now_ms;
    g->fired[type] = 1;
    g->budget_bcount++;
    return WAKE_ALLOW;
}
