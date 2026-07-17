/*
 * JARVIS AI-OS — Phase 6 K/M0: the static action allowlist (K-b)
 *
 * THE structural boundary of the it-acts spine: every action JARVIS can ever
 * take is a compile-time, human-reviewed entry here. The LLM (or the cache, or
 * — later — inbound text) SELECTS an id; it can never synthesize, parameterize,
 * or mutate an action. Unknown ids are refused before SHIELD is even consulted.
 * This is also the prompt-injection boundary once control-IN exists (goal 6-5).
 *
 * Trust levels REUSE decision_cache.h's trust_level_t (0..3 AUTO/NOTIFY/
 * REQUEST/REQUIRE — K-c); per-action trust is a compile-time constant, never
 * LLM-assigned. `name` is for logs/console ONLY — never parsed.
 *
 * HOST-PURE at M0: nothing links into main_x86.c until K/M1 (gated).
 * Design: phase6/docs/PHASE_6_GOAL_K_IT_ACTS.md §3/§4.
 */

#ifndef ACTION_ALLOWLIST_H
#define ACTION_ALLOWLIST_H

#include <stdint.h>
#include "decision_cache.h"   /* trust_level_t — REUSED, not re-minted (K-c) */

/* Stable action ids — NEVER renumber (they are the audit/telemetry currency). */
enum {
    ACTION_RESTART_PB     = 1,   /* self-heal: respawn Process B (K/M2) */
    ACTION_NOTIFY_ANOMALY = 2,   /* notify-only L0 (the de-risk fallback action) */
    ACTION_WAKE_CONSULT   = 3,   /* 6-2: event-triggered cache/LLM consult (inform-only) */
    ACTION_STATUS_DIGEST  = 4,   /* 6-3 B4: boot-relative status digest (inform-only L0) */
    ACTION_CONTROL_IN_QUERY = 5, /* 6-5/M3-2a: AUDIT TAG for a control-IN query-handling event
                                  * (answered / query-SHIELD-refused). NOT an LLM-selectable action
                                  * (deliberately NOT in the allowlist table — K-b holds); it exists
                                  * only so the JACT read-back distinguishes control-IN records
                                  * (action=5) from self-heal/monitor/wake (1..4). */
};

/* Reserved id for the K/M1 induced-BLOCK probe: on the immutable action-id
 * blocklist (shield_action.c) and NOT in the allowlist — never a real action. */
#define ACTION_ID_BLOCK_PROBE  0xFFFE

/* Base-risk classes (shield_action.c maps class -> base risk). PROBE_HIGH is
 * RESERVED — no deployed action uses it; it exists so the K/M1 probe can
 * demonstrate learned-risk threshold crossing (base 75 + 10 >= 80). */
enum {
    ACTION_CLASS_SELF_HEAL  = 0,
    ACTION_CLASS_NOTIFY     = 1,
    ACTION_CLASS_PROBE_HIGH = 2,
    ACTION_CLASS_CONSULT    = 3,   /* 6-2: a bounded cache/LLM consult (compute-burning, inform-only) */
};

typedef struct {
    uint16_t      id;            /* stable action id */
    const char   *name;          /* logs/console only — never parsed */
    trust_level_t trust;         /* compile-time constant (K-c) */
    uint16_t      action_class;  /* base-risk class for shield_assess */
} action_def_t;

/* NULL = not allowlisted -> refuse before SHIELD. Pure. */
const action_def_t *action_lookup(uint16_t id);

/* Number of DEPLOYED allowlist entries (3 since 6-2/M0: restart_pb +
 * notify_anomaly + wake-consult). */
uint32_t action_allowlist_count(void);

#endif /* ACTION_ALLOWLIST_H */
