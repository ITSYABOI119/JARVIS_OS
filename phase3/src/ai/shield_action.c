/*
 * JARVIS AI-OS — Phase 6 K/M0: the SHIELD action gate (K-a)
 *
 * Pure logic — no globals, no I/O, no device. See shield_action.h for the
 * N-A blocklist reconciliation and the pinned risk arithmetic.
 */

#include "shield_action.h"

/* ---- N-A: THE canonical immutable keyword blocklist (see header) ----
 * UNION of shield.c's blocklist[] (9) and PA's inline bad[] (6 — a strict
 * subset). Compile-time const; never learned-down; owned HERE from M0 on. */
static const char *const g_keyword_blocklist[] = {
    "delete",
    "remove",
    "kill",
    "destroy",
    "format",
    "rm -rf",
    "drop table",
    "shutdown",
    "halt",
};
#define KEYWORD_N (sizeof(g_keyword_blocklist) / sizeof(g_keyword_blocklist[0]))

/* ---- N-A: the immutable ACTION-ID blocklist — v1 carries ONLY the reserved
 * K/M1 probe id; no DEPLOYED action is blocklisted. ---- */
static const uint16_t g_action_id_blocklist[] = {
    ACTION_ID_BLOCK_PROBE,
};
#define ID_BLOCK_N (sizeof(g_action_id_blocklist) / sizeof(g_action_id_blocklist[0]))

int shield_action_id_blocklisted(uint16_t action_id)
{
    for (unsigned i = 0; i < ID_BLOCK_N; i++)
        if (g_action_id_blocklist[i] == action_id)
            return 1;
    return 0;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* Bounded, case-insensitive substring search: does hay[0..hay_len) contain
 * needle (a NUL-terminated lowercase literal)? Never reads past hay_len and
 * never strlen()s hay — the trigger is untrusted/non-NUL-required. */
static int contains_keyword(const char *hay, uint16_t hay_len, const char *needle)
{
    if (!hay || hay_len == 0)
        return 0;
    for (uint16_t i = 0; i < hay_len; i++) {
        uint16_t j = 0;
        while (needle[j] != '\0' && (uint16_t)(i + j) < hay_len &&
               lower(hay[i + j]) == needle[j])
            j++;
        if (needle[j] == '\0')
            return 1;
    }
    return 0;
}

static uint16_t base_risk(uint16_t action_class)
{
    switch (action_class) {
    case ACTION_CLASS_SELF_HEAL:  return SHIELD_BASE_RISK_SELF_HEAL;
    case ACTION_CLASS_NOTIFY:     return SHIELD_BASE_RISK_NOTIFY;
    case ACTION_CLASS_PROBE_HIGH: return SHIELD_BASE_RISK_PROBE_HIGH;
    case ACTION_CLASS_CONSULT:    return SHIELD_BASE_RISK_CONSULT;   /* 6-2 */
    default:                      return 100;   /* unknown class -> auto-block (conservative) */
    }
}

shield_action_result_t shield_assess_class(uint16_t action_class, const action_ctx_t *ctx,
                                           const shield_learn_slot_t *learn, int learn_cap)
{
    shield_action_result_t r;
    r.risk_x100 = base_risk(action_class);

    /* #5's learned adjustment — monotonic-raise-only (nothing lowers a risk). */
    if (learn && learn_cap > 0 && ctx) {
        float adj = shield_learn_adjustment(learn, learn_cap, ctx->query_key);
        r.risk_x100 = (uint16_t)(r.risk_x100 + (uint16_t)(adj * 100.0f + 0.5f));
    }

    /* Canonical keyword scan over the (untrusted, length-carried) trigger. */
    if (ctx && ctx->trigger && ctx->trigger_len > 0) {
        for (unsigned k = 0; k < KEYWORD_N; k++) {
            if (contains_keyword(ctx->trigger, ctx->trigger_len, g_keyword_blocklist[k])) {
                r.verdict = SHIELD_VERDICT_BLOCKED;
                return r;
            }
        }
    }

    r.verdict = (r.risk_x100 >= SHIELD_BLOCK_THRESHOLD_X100)
                    ? SHIELD_VERDICT_BLOCKED : SHIELD_VERDICT_EXECUTE;
    return r;
}

shield_action_result_t shield_assess(uint16_t action_id, const action_ctx_t *ctx,
                                     const shield_learn_slot_t *learn, int learn_cap)
{
    /* Blocklist FIRST: a blocklisted id refuses even if someone later
     * allowlists it (defense in depth). */
    if (shield_action_id_blocklisted(action_id)) {
        shield_action_result_t r = { 100, SHIELD_VERDICT_BLOCKED };
        return r;
    }

    const action_def_t *def = action_lookup(action_id);
    if (!def) {
        /* Not allowlisted -> refuse before any scoring (K-b). */
        shield_action_result_t r = { 100, SHIELD_VERDICT_BLOCKED };
        return r;
    }

    return shield_assess_class(def->action_class, ctx, learn, learn_cap);
}

act_decision_t trust_policy(shield_verdict_t v, trust_level_t t, bool control_in_available)
{
    if (v == SHIELD_VERDICT_BLOCKED)
        return ACT_REFUSE;

    switch (t) {
    case TRUST_AUTO:    return ACT_EXECUTE;
    case TRUST_NOTIFY:  return ACT_NOTIFY;
    case TRUST_REQUEST: return control_in_available ? ACT_REQUEST_APPROVAL : ACT_PROPOSE_LOG;
    case TRUST_REQUIRE: return ACT_REFUSE;
    default:            return ACT_REFUSE;   /* unknown trust -> never execute */
    }
}
