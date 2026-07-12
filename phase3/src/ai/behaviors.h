/*
 * JARVIS AI-OS — Phase 6 goal 6-3 M0: the compile-time behavior registry.
 *
 * THE butler behavior catalog (PHASE_6_GOAL_6-3_PROACTIVE.md §4/§5): every
 * proactive behavior JARVIS has is one human-reviewed row here —
 * { trigger (an existing monitor event type) -> spine action id (allowlisted,
 * L0/L1 INFORM-only in v1) -> surface prose }. This is the K-b
 * select-never-synthesize boundary EXTENDED to behaviors: the registry is
 * compile-time; the LLM never selects, parameterizes, or synthesizes a
 * behavior — its only role remains filling 6-2's fixed consult templates, and
 * its answers have no actuator. The registry is also the 6-5/6-6 seam:
 * approvals and routing attach to behavior ids.
 *
 * v1 (O1 resolved: B1–B5, B6 TX-stall deferred; O4 resolved: no PROPOSE_LOG
 * lane pre-6-5 — every v1 behavior is an L0/L1 inform).
 *
 * Host-pure: NO <sel4/sel4.h> (the wake.h / monitors.h precedent). Wiring
 * into Process A is M1 (gated JARVIS_PROACTIVE, default 0).
 * Design: phase6/docs/PHASE_6_GOAL_6-3_PROACTIVE.md §9 (M0).
 */

#ifndef BEHAVIORS_H
#define BEHAVIORS_H

#include <stdint.h>
#include "monitors.h"   /* monitor_event_type_t — the deliberate 6-1 seam */

/* Behavior ids: 1..N (0 = none). The proposed-v10 behaviors_mask bit for
 * behavior b is (b - 1) — a u16 mask, so ids must stay <= 16. APPEND-ONLY:
 * ids are the JACT/console/6-5-approval currency — never renumber. */
enum {
    BEHAVIOR_ANOMALY_CONSULT  = 1,  /* B1: MON_EV_ERROR_RATE       -> ACTION_WAKE_CONSULT (the live 6-2 lane) */
    BEHAVIOR_SELFHEAL_CONSULT = 2,  /* B2: MON_EV_SELF_HEAL_RATE   -> ACTION_WAKE_CONSULT                     */
    BEHAVIOR_STORE_ROLL       = 3,  /* B3: MON_EV_STORE_WRAP       -> ACTION_NOTIFY_ANOMALY                   */
    BEHAVIOR_STATUS_DIGEST    = 4,  /* B4: MON_EV_UPTIME_MILESTONE -> ACTION_STATUS_DIGEST                    */
    BEHAVIOR_DEGRADED_ALERT   = 5,  /* B5: MON_EV_DEGRADED         -> ACTION_NOTIFY_ANOMALY                   */
};

typedef struct {
    uint16_t             id;         /* behavior id (stable, append-only)                       */
    const char          *name;       /* console/logs — never parsed, never shield-scanned       */
    monitor_event_type_t trigger;    /* the event that fires this behavior                      */
    uint16_t             action_id;  /* the spine action id (must be allowlisted + L0/L1)       */
    const char          *surface;    /* console-row prose (honesty-gated at M3, NOT keyword-
                                      * scanned — the shield scan binds trigger snapshots only) */
} behavior_def_t;

const behavior_def_t *behavior_lookup(uint16_t id);               /* by behavior id; NULL if none      */
const behavior_def_t *behavior_for_event(monitor_event_type_t t); /* trigger -> behavior; NULL unmapped */
uint32_t behavior_count(void);

/* B4 status digest — INTEGER-ONLY (there is NO char* parameter, so free text
 * can NEVER be smuggled in — the compile-enforced no-free-text property,
 * goal doc §6/F1). Renders fixed literals + decimal counters:
 *   "digest up=<u>h q=<u> err=<u> heal=<u> mon=<u> wake=<u> tok=<u>"
 * — keyword-clean, cap-bounded, NUL-terminated. Returns len, or -1 on NULL
 * buf / cap < 1. FORBIDDEN inputs (NEVER add a string parameter to this
 * signature): model answers (wresp), the response head (g_fb_last_resp /
 * last_text), episodic resp text, cache action strings, GGUF-sourced strings,
 * model_name. */
int behavior_build_digest(char *buf, uint32_t cap,
                          uint32_t up_hours, uint64_t q_total, uint64_t q_errors,
                          uint32_t restart_count, uint16_t monitors_fired,
                          uint16_t wakes_fired, uint16_t tok_x100);

#endif /* BEHAVIORS_H */
