/*
 * JARVIS AI-OS — Phase 6 goal 6-2 M0: the pure wake decision core.
 *
 * Event-triggered DISPATCH of a consult ("wake" is metaphorical — PA busy-polls):
 * a 6-1 monitor crossing selects a FIXED, human-reviewed query template by
 * monitor_event_type_t and, if the anti-loop gate allows, Process A consults the
 * decision cache first, then PB inference on a miss (M1 wiring).
 *
 * THE LOAD-BEARING INJECTION BOUNDARY (goal doc §5): template selection keys on
 * the event TYPE ONLY — never a snapshot, never free text. The template text is
 * FIXED (no interpolation, not even the event's decimals): fixed text = a stable
 * FNV-1a cache key, which is what makes "cache lookup or inference" real (an
 * interpolated number would make the cache permanently miss). Templates must be
 * keyword-clean BOTH ways (blocklist substring scan + un-BLOCKED through the
 * real shield_assess) — mind SUBSTRING traps ("skill" contains "kill",
 * "information" contains "format").
 *
 * THE ANTI-LOOP GATE (goal doc §6): one-slot pending latch (staged + consumed in
 * the same [STATS] iteration; only TEMPLATED types touch it) + per-type cooldown
 * + a global hourly budget. Wrap-safe: cooldown via uint32 subtraction, budget
 * via a bucket-INDEX change reset (jarvis_uptime_ms wraps ~49.7 d). A suppressed
 * wake is a non-event (counted, never audited).
 *
 * Host-pure: NO <sel4/sel4.h>; caller passes now_ms (the monitors.h /
 * km2b_miss.h precedent). Wiring into Process A is M1 (gated JARVIS_WAKE,
 * default 0). Design: phase6/docs/PHASE_6_GOAL_6-2_EVENT_WAKE.md §5/§6/§7/§8.
 */

#ifndef WAKE_H
#define WAKE_H

#include <stdint.h>
#include "monitors.h"   /* monitor_event_type_t — the deliberate 6-1/6-2 seam */

/* ---- template allowlist: the STATIC, human-reviewed query per event type ---- */
typedef struct {
    monitor_event_type_t type;  /* the triggering event type */
    const char *query;          /* FIXED text — the cache key IS this text.
                                 * Keyword-clean BOTH ways (§5). Must fit
                                 * MAX_QUERY_LEN (128) normalized. */
    const char *event_literal;  /* system-facts literal for the JACT trigger:
                                 * "err-rate" / "heal-rate" */
} wake_template_t;

/* Mapped template for an event TYPE, or NULL. TAKES THE TYPE ONLY — the
 * load-bearing injection boundary (never a snapshot/free text). v1 maps ONLY
 * MON_EV_ERROR_RATE + MON_EV_SELF_HEAL_RATE (the degradation events);
 * STORE_WRAP, HEARTBEAT_AGE (unwired in 6-1), UPTIME_MILESTONE, NONE, and
 * out-of-range => NULL. Pure. */
const wake_template_t *wake_template_lookup(monitor_event_type_t type);

/* Copy the fixed template query into buf (NUL-terminated, bounded). Returns
 * len, or -1 if unmapped / buf NULL / cap < 1. Byte-identical across calls
 * regardless of any event values (there are none to pass — by design). */
int wake_build_query(monitor_event_type_t type, char *buf, uint32_t cap);

typedef enum { WAKE_ROUTE_NONE = 0, WAKE_ROUTE_CACHE = 1, WAKE_ROUTE_INFER = 2 } wake_route_t;

/* JACT system-facts trigger for a DISPATCHED wake:
 *   "wake <event-literal> route=<cache|infer|none> ms=<n>"
 * — fixed literals + decimals only (the km2b_build_trigger discipline),
 * keyword-clean, cap-bounded, NUL-terminated. Returns len, or -1 if
 * unmapped / buf NULL / cap < 1. */
int wake_build_trigger(monitor_event_type_t type, wake_route_t route, uint32_t ms,
                       char *buf, uint32_t cap);

/* ---- the anti-loop gate: one-slot latch + per-type cooldown + hourly budget ----
 * #ifndef-guarded so an M2 gate build can shrink them via -D (the probe-shrunk-cooldown
 * mechanism, §8 M2) without touching this header; defaults are the production values. */
#ifndef WAKE_COOLDOWN_MS
#define WAKE_COOLDOWN_MS      600000u  /* 10 min/type — M2-calibrated; flip may tighten (O2) */
#endif
#ifndef WAKE_BUDGET_PER_HOUR
#define WAKE_BUDGET_PER_HOUR  2u       /* global/hour — tightened 4->2 at the 6-2 flip (O2: a
                                        * healthy box crosses ~never, so a tight cap bounds a
                                        * miscalibrated watcher at zero cost; relax only after
                                        * observation) */
#endif

typedef enum {
    WAKE_ALLOW             = 0,
    WAKE_SUPPRESS_COOLDOWN = 1,
    WAKE_SUPPRESS_BUDGET   = 2,
} wake_verdict_t;

typedef struct {
    monitor_event_type_t pending;              /* one-slot latch; MON_EV_NONE = empty     */
    uint32_t last_fire_ms[MON_EV__COUNT];      /* per-type last DISPATCH time             */
    uint8_t  fired[MON_EV__COUNT];             /* cold-start guard: last_fire_ms[t] valid */
    uint32_t budget_bucket;                    /* now_ms / 3,600,000 hour bucket index    */
    uint8_t  budget_bcount;                    /* dispatches in the current bucket        */
    uint32_t dropped;                          /* TEMPLATED events dropped on a full latch */
    uint32_t suppressed;                       /* cooldown+budget suppressions (non-events) */
} wake_gate_t;

/* Initialize (latch empty, all counters 0). NULL-safe (no-op). */
void wake_gate_init(wake_gate_t *g);

/* STAGE (mon_notify EXECUTED branch). Stage guard: only a TEMPLATED type may
 * touch the latch. Returns 1 staged, 0 no-op (unmapped/benign/NULL), -1 dropped
 * (latch occupied; g->dropped++). */
int wake_gate_stage(wake_gate_t *g, monitor_event_type_t type);

/* TAKE + clear the latch (the dispatch site). Returns the staged type or
 * MON_EV_NONE. NULL-safe (MON_EV_NONE). */
monitor_event_type_t wake_gate_take(wake_gate_t *g);

/* CHECK-AND-COMMIT at now_ms. Cooldown FIRST (per-type; cold-start = no
 * cooldown), then the global budget. On WAKE_ALLOW: commits (last_fire_ms[type]
 * = now, fired[type] = 1, budget_bcount++). On suppress: g->suppressed++ and NO
 * last_fire change (a suppressed wake is a non-event). Wrap-safe: cooldown via
 * (uint32_t)(now - last) subtraction; budget via bucket-INDEX change reset —
 * never absolute marks. NULL/out-of-range type: SUPPRESS_COOLDOWN, no state
 * change (defensive — the caller passes a type from wake_gate_take). */
wake_verdict_t wake_gate_try(wake_gate_t *g, monitor_event_type_t type, uint32_t now_ms);

#endif /* WAKE_H */
