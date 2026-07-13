/* Phase 6 6-3/M0 — the compile-time behavior registry + the integer-only
 * status-digest builder. See behaviors.h for the K-b boundary + the forbidden
 * digest inputs; design: PHASE_6_GOAL_6-3_PROACTIVE.md §4/§5/§6. */

#include "behaviors.h"
#include "action_allowlist.h"
#include <stddef.h>

/* ---- THE registry (§5, human-reviewed — the select-never-synthesize table).
 * v1 = B1–B5 (O1: B6 TX-stall deferred). Trust is NOT duplicated here: it
 * lives on the action's allowlist entry (one source — test C resolves it via
 * action_lookup). `surface` is console-row prose (honesty-gated at M3); the
 * shield keyword scan binds trigger SNAPSHOTS, never these strings. */
static const behavior_def_t g_behaviors[] = {
    { BEHAVIOR_ANOMALY_CONSULT,  "anomaly-consult",
      MON_EV_ERROR_RATE,       ACTION_WAKE_CONSULT,
      "consults on an error-rate spike (event-triggered, informs only)" },
    { BEHAVIOR_SELFHEAL_CONSULT, "self-heal-consult",
      MON_EV_SELF_HEAL_RATE,   ACTION_WAKE_CONSULT,
      "consults after repeated self-heal restarts" },
    { BEHAVIOR_STORE_ROLL,       "store-roll",
      MON_EV_STORE_WRAP,       ACTION_NOTIFY_ANOMALY,
      "notifies once when a circular store starts rolling (oldest records overwritten)" },
    { BEHAVIOR_STATUS_DIGEST,    "status-digest",
      MON_EV_UPTIME_MILESTONE, ACTION_STATUS_DIGEST,
      "posts a boot-relative status digest at the uptime marks (1h/24h/7d)" },
    { BEHAVIOR_DEGRADED_ALERT,   "degraded-alert",
      MON_EV_DEGRADED,         ACTION_NOTIFY_ANOMALY,
      "notifies once when inference is down and the box serves cache-only" },
};
#define BEHAVIOR_N (sizeof(g_behaviors) / sizeof(g_behaviors[0]))

const behavior_def_t *behavior_lookup(uint16_t id)
{
    if (id == 0)
        return NULL;   /* 0 = none, never a behavior id */
    for (size_t i = 0; i < BEHAVIOR_N; i++)
        if (g_behaviors[i].id == id)
            return &g_behaviors[i];
    return NULL;
}

const behavior_def_t *behavior_for_event(monitor_event_type_t t)
{
    if (t <= MON_EV_NONE || t >= MON_EV__COUNT)
        return NULL;
    for (size_t i = 0; i < BEHAVIOR_N; i++)
        if (g_behaviors[i].trigger == t)
            return &g_behaviors[i];
    return NULL;
}

uint32_t behavior_count(void)
{
    return (uint32_t)BEHAVIOR_N;
}

/* Bounded append of a NUL-terminated literal (the wake.c wb_ts style). */
static int bb_ts(char *buf, uint32_t cap, int p, const char *s)
{
    while (*s && p < (int)cap - 1) buf[p++] = *s++;
    return p;
}

/* Bounded append of a u64 in decimal (u64 so every integer arg fits). */
static int bb_tu64(char *buf, uint32_t cap, int p, uint64_t v)
{
    char d[24]; int n = 0;
    if (!v) d[n++] = '0'; else while (v) { d[n++] = (char)('0' + v % 10); v /= 10; }
    while (n && p < (int)cap - 1) buf[p++] = d[--n];
    return p;
}

/* ---- the O2 GLOBAL inform cap (6-3/M2) — semantics in behaviors.h. The
 * wake-gate budget pattern verbatim: bucket-INDEX change resets (wrap-safe);
 * ALLOW commits, suppress counts and changes nothing else. ---- */
void behavior_budget_init(behavior_budget_t *b)
{
    if (!b) return;
    b->bucket     = 0;
    b->bcount     = 0;
    b->suppressed = 0;
}

behavior_verdict_t behavior_budget_try(behavior_budget_t *b, uint32_t now_ms)
{
    if (!b) return BEHAVIOR_SUPPRESS_BUDGET;   /* defensive */
    uint32_t bucket = now_ms / 3600000u;
    if (bucket != b->bucket) {
        b->bucket = bucket;
        b->bcount = 0;
    }
    if (b->bcount >= BEHAVIOR_BUDGET_PER_HOUR) {
        b->suppressed++;
        return BEHAVIOR_SUPPRESS_BUDGET;
    }
    b->bcount++;
    return BEHAVIOR_ALLOW;
}

int behavior_build_digest(char *buf, uint32_t cap,
                          uint32_t up_hours, uint64_t q_total, uint64_t q_errors,
                          uint32_t restart_count, uint16_t monitors_fired,
                          uint16_t wakes_fired, uint16_t tok_x100)
{
    if (!buf || cap < 1)
        return -1;
    int p = 0;
    p = bb_ts(buf, cap, p, "digest up=");
    p = bb_tu64(buf, cap, p, up_hours);
    p = bb_ts(buf, cap, p, "h q=");
    p = bb_tu64(buf, cap, p, q_total);
    p = bb_ts(buf, cap, p, " err=");
    p = bb_tu64(buf, cap, p, q_errors);
    p = bb_ts(buf, cap, p, " heal=");
    p = bb_tu64(buf, cap, p, restart_count);
    p = bb_ts(buf, cap, p, " mon=");
    p = bb_tu64(buf, cap, p, monitors_fired);
    p = bb_ts(buf, cap, p, " wake=");
    p = bb_tu64(buf, cap, p, wakes_fired);
    p = bb_ts(buf, cap, p, " tok=");
    p = bb_tu64(buf, cap, p, tok_x100);
    buf[p] = '\0';
    return p;
}
