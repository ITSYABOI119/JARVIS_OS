/*
 * JARVIS AI-OS — Phase 6 K/M0: the SHIELD action gate (K-a — the linked gate)
 *
 * The gate every action passes BEFORE execution. Host-pure at M0 (no globals,
 * no I/O); K/M1 links it into Process A's action path — closing SEC-039 when
 * the live induced-BLOCK is proven on the box.
 *
 * NAMES: shield.h already owns shield_result_t / shield_decision_t (the query
 * path) — this module deliberately uses shield_action_result_t /
 * shield_verdict_t so shield.h is untouched.
 *
 * N-A — THE ONE CANONICAL IMMUTABLE KEYWORD BLOCKLIST (compile-time const in
 * shield_action.c) is the reviewed UNION of the two divergent SHIELD surfaces:
 *   shield.c blocklist[] (9): delete, remove, kill, destroy, format, "rm -rf",
 *                             "drop table", shutdown, halt
 *   main_x86.c PA inline bad[] (6): delete, remove, kill, destroy, format,
 *                             "rm -rf"   — a strict SUBSET of shield.c's list
 *   UNION (deduped) = shield.c's 9 keywords. (PB's SHIELD_CHECK is an
 *   unconditional-ALLOW stub with no list — nothing to merge.)
 * This module OWNS the canonical list from M0 on; PA's inline check is retired
 * at K/M1. Plus an immutable ACTION-ID blocklist — empty of DEPLOYED actions in
 * v1 (no deployed action is blocklisted); it carries only the reserved
 * ACTION_ID_BLOCK_PROBE so the K/M1 induced-BLOCK probe has a poison id.
 *
 * Risk arithmetic (pinned by PHASE_6_GOAL_K_IT_ACTS §4): risk_x100 =
 * base(action_class) + round(100 * shield_learn_adjustment(key)) — the learned
 * half is #5's monotonic-raise-only map (step +0.1, cap +0.5; NOTHING ever
 * lowers a risk). SHIELD_BLOCK_THRESHOLD_X100 = 80; base SELF_HEAL=10 /
 * NOTIFY=0 / PROBE_HIGH=75. So a benign self-heal maxes at 10+50=60 < 80
 * (never risk-blocked — it is service-restoring), while PROBE_HIGH crosses on
 * ONE learned failure (75+10=85 >= 80) — the "blocked faster on the 2nd
 * attempt" demonstrator (K-e).
 *
 * Design: phase6/docs/PHASE_6_GOAL_K_IT_ACTS.md §3/§4.
 */

#ifndef SHIELD_ACTION_H
#define SHIELD_ACTION_H

#include <stdint.h>
#include <stdbool.h>
#include "action_allowlist.h"   /* action_def_t, trust_level_t (reused) */
#include "shield_learn.h"       /* shield_learn_slot_t + adjustment (the live feed) */

#define SHIELD_BLOCK_THRESHOLD_X100  80
#define SHIELD_BASE_RISK_SELF_HEAL   10
#define SHIELD_BASE_RISK_NOTIFY       0
#define SHIELD_BASE_RISK_PROBE_HIGH  75

/* Context for one assessment. trigger MAY be NULL (trigger_len 0); when
 * present it is scanned BOUNDED by trigger_len (never strlen on untrusted
 * bytes), case-insensitively, against the canonical keyword blocklist.
 * query_key = cache_hash(cache_normalize_query(...)) — decision-cache FNV-1a
 * parity (the same key currency as #1/#3/#5/#6). */
typedef struct {
    const char *trigger;
    uint16_t    trigger_len;
    uint64_t    query_key;
} action_ctx_t;

typedef enum {
    SHIELD_VERDICT_EXECUTE = 0,
    SHIELD_VERDICT_BLOCKED = 1,
} shield_verdict_t;

typedef struct {
    uint16_t         risk_x100;
    shield_verdict_t verdict;
} shield_action_result_t;

/* 1 if id is on the immutable action-id blocklist (v1: only the reserved
 * probe id — no deployed action). Pure. */
int shield_action_id_blocklisted(uint16_t action_id);

/* Assess an allowlisted action id. learn MAY be NULL (or learn_cap <= 0) ->
 * no learned adjustment. BLOCKED iff: id blocklisted (checked FIRST — a
 * blocklisted id refuses even if later allowlisted), OR id unknown
 * (action_lookup NULL), OR the trigger contains a canonical keyword, OR
 * risk_x100 >= SHIELD_BLOCK_THRESHOLD_X100. Pure. */
shield_action_result_t shield_assess(uint16_t action_id, const action_ctx_t *ctx,
                                     const shield_learn_slot_t *learn, int learn_cap);

/* Class-level core (shield_assess after its id checks resolve the class).
 * Exposed so the RESERVED PROBE_HIGH class is host-testable without adding a
 * fake deployed allowlist entry; K/M1's probe drives it via an id. Pure. */
shield_action_result_t shield_assess_class(uint16_t action_class, const action_ctx_t *ctx,
                                           const shield_learn_slot_t *learn, int learn_cap);

/* The trust policy (K-c): what the spine DOES with a verdict at a trust level.
 *   BLOCKED        -> ACT_REFUSE (any trust level)
 *   TRUST_AUTO     -> ACT_EXECUTE
 *   TRUST_NOTIFY   -> ACT_NOTIFY (execute + notify)
 *   TRUST_REQUEST  -> control_in_available ? ACT_REQUEST_APPROVAL
 *                                          : ACT_PROPOSE_LOG (log-only — never
 *                       executes; there is no channel to ask on until 6-5)
 *   TRUST_REQUIRE  -> ACT_REFUSE (never auto, even with control-IN)          */
typedef enum {
    ACT_EXECUTE = 0,
    ACT_NOTIFY  = 1,
    ACT_PROPOSE_LOG = 2,
    ACT_REQUEST_APPROVAL = 3,
    ACT_REFUSE  = 4,
} act_decision_t;

act_decision_t trust_policy(shield_verdict_t v, trust_level_t t, bool control_in_available);

#endif /* SHIELD_ACTION_H */
