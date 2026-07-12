/*
 * JARVIS AI-OS — SHIELD Action Gate Unit Tests (Phase 6 K/M0)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_shield_action.c phase3/src/ai/shield_action.c \
 *       phase3/src/ai/action_allowlist.c phase3/src/ai/shield_learn.c \
 *       phase3/src/ai/decision_cache.c -lm -o /tmp/test_shield_action
 *
 * Pure host test of the linked-gate LOGIC (K-a): shield_assess (base risk +
 * shield_learn's learned adjustment, the ONE canonical keyword blocklist, the
 * action-id blocklist, the threshold) and the trust_policy truth table.
 * Proves ON THE HOST the K-e arithmetic that K/M1 demonstrates live:
 * base 75 + one learned step (+10) crosses the 80 threshold -> BLOCKED on the
 * second attempt ("blocked faster on 2nd attempt", Phase-5 criterion 2).
 */

#include "shield_action.h"
#include "action_allowlist.h"
#include "shield_learn.h"
#include "decision_cache.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", msg, __LINE__); tests_failed++; return; } \
} while (0)

#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while (0)

static void test_benign_execute(void)
{
    action_ctx_t ctx = { NULL, 0, 0 };
    shield_action_result_t r = shield_assess(ACTION_RESTART_PB, &ctx, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "benign restart_pb -> EXECUTE");
    ASSERT(r.risk_x100 == 10, "SELF_HEAL base risk == 10");

    r = shield_assess(ACTION_NOTIFY_ANOMALY, &ctx, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "benign notify_anomaly -> EXECUTE");
    ASSERT(r.risk_x100 == 0, "NOTIFY base risk == 0");

    r = shield_assess(ACTION_RESTART_PB, NULL, NULL, 0);   /* NULL ctx tolerated */
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "NULL ctx -> still assessable");
    PASS("T1 benign actions EXECUTE at base risk (10 / 0)");
}

static void test_unknown_and_blocklisted(void)
{
    action_ctx_t ctx = { NULL, 0, 0 };
    shield_action_result_t r = shield_assess(9999, &ctx, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_BLOCKED, "unknown id -> BLOCKED");

    ASSERT(shield_action_id_blocklisted(ACTION_ID_BLOCK_PROBE) == 1,
           "the reserved probe id IS on the immutable action-id blocklist");
    ASSERT(shield_action_id_blocklisted(ACTION_RESTART_PB) == 0 &&
           shield_action_id_blocklisted(ACTION_NOTIFY_ANOMALY) == 0,
           "no DEPLOYED action is blocklisted (v1)");
    r = shield_assess(ACTION_ID_BLOCK_PROBE, &ctx, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_BLOCKED,
           "blocklisted id -> BLOCKED (checked BEFORE the allowlist — regardless of trust)");
    PASS("T2 unknown id + immutable action-id blocklist -> BLOCKED");
}

static void test_keyword_trigger(void)
{
    /* One canonical harmful keyword in the trigger text -> BLOCKED. The scan is
     * bounded by trigger_len (never strlen on untrusted) and case-insensitive. */
    const char *bad = "user asked to DELETE the store";     /* 'delete' — in both source lists */
    action_ctx_t ctx = { bad, (uint16_t)strlen(bad), 0 };
    shield_action_result_t r = shield_assess(ACTION_NOTIFY_ANOMALY, &ctx, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_BLOCKED, "harmful keyword in trigger -> BLOCKED");

    const char *bad2 = "operator requested drop table users";  /* shield.c-only keyword */
    action_ctx_t ctx2 = { bad2, (uint16_t)strlen(bad2), 0 };
    r = shield_assess(ACTION_NOTIFY_ANOMALY, &ctx2, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_BLOCKED,
           "shield.c-only keyword ('drop table') is in the canonical union");

    const char *benign = "PB heartbeat age 12000 ms";
    action_ctx_t ctx3 = { benign, (uint16_t)strlen(benign), 0 };
    r = shield_assess(ACTION_RESTART_PB, &ctx3, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "benign trigger -> EXECUTE");

    /* Bounded scan: the keyword sits BEYOND trigger_len -> not scanned, EXECUTE. */
    const char *cut = "all nominal / delete";
    action_ctx_t ctx4 = { cut, 11 /* "all nominal" only */, 0 };
    r = shield_assess(ACTION_RESTART_PB, &ctx4, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "scan is trigger_len-bounded");
    PASS("T3 canonical keyword blocklist on the trigger (union, case-insensitive, bounded)");
}

static void test_learned_risk_crossing(void)
{
    /* K-e / M1 demonstrator, host-proven: PROBE_HIGH base 75; one recorded
     * failure (+0.1 -> +10) crosses SHIELD_BLOCK_THRESHOLD_X100 (80). */
    shield_learn_slot_t learn[SHIELD_LEARN_CAP_KEYS];
    memset(learn, 0, sizeof learn);

    char norm[MAX_QUERY_LEN];
    ASSERT(cache_normalize_query("probe risky thing", norm, sizeof norm), "normalize");
    uint64_t key = cache_hash(norm);

    action_ctx_t ctx = { NULL, 0, key };
    shield_action_result_t r =
        shield_assess_class(ACTION_CLASS_PROBE_HIGH, &ctx, learn, SHIELD_LEARN_CAP_KEYS);
    ASSERT(r.risk_x100 == 75, "0 failures: risk == base 75");
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "75 < 80 -> EXECUTE (1st attempt)");

    shield_learn_record_failure(learn, SHIELD_LEARN_CAP_KEYS, key);
    r = shield_assess_class(ACTION_CLASS_PROBE_HIGH, &ctx, learn, SHIELD_LEARN_CAP_KEYS);
    ASSERT(r.risk_x100 == 85, "1 failure: risk == 75 + 10 == 85");
    ASSERT(r.verdict == SHIELD_VERDICT_BLOCKED, "85 >= 80 -> BLOCKED (2nd attempt) — K-e");

    shield_learn_record_failure(learn, SHIELD_LEARN_CAP_KEYS, key);
    r = shield_assess_class(ACTION_CLASS_PROBE_HIGH, &ctx, learn, SHIELD_LEARN_CAP_KEYS);
    ASSERT(r.risk_x100 == 95, "2 failures: 75 + 20 == 95 (monotonic)");

    for (int i = 0; i < 10; i++)
        shield_learn_record_failure(learn, SHIELD_LEARN_CAP_KEYS, key);
    r = shield_assess_class(ACTION_CLASS_PROBE_HIGH, &ctx, learn, SHIELD_LEARN_CAP_KEYS);
    ASSERT(r.risk_x100 == 125, "learned adj caps at +50 (shield_learn 0.5): 75 + 50 == 125");
    PASS("T4 learned-risk threshold crossing (75 -> 85 BLOCKED on 2nd attempt; cap +50)");
}

static void test_key_parity(void)
{
    /* The key the caller passes must be the decision-cache FNV-1a — no re-impl. */
    char norm[MAX_QUERY_LEN];
    ASSERT(cache_normalize_query("  Restart   THE pb  ", norm, sizeof norm), "normalize");
    ASSERT(strcmp(norm, "restart the pb") == 0, "normalization folds case+whitespace");
    uint64_t k1 = cache_hash(norm);
    ASSERT(cache_normalize_query("restart the pb", norm, sizeof norm), "normalize 2");
    ASSERT(k1 == cache_hash(norm), "FNV-1a key parity across variants");
    PASS("T5 query_key parity == cache_hash(cache_normalize_query(q))");
}

static void test_trust_policy_table(void)
{
    /* BLOCKED -> REFUSE for ALL four trust levels. */
    ASSERT(trust_policy(SHIELD_VERDICT_BLOCKED, TRUST_AUTO,    false) == ACT_REFUSE, "B/AUTO");
    ASSERT(trust_policy(SHIELD_VERDICT_BLOCKED, TRUST_NOTIFY,  false) == ACT_REFUSE, "B/NOTIFY");
    ASSERT(trust_policy(SHIELD_VERDICT_BLOCKED, TRUST_REQUEST, true)  == ACT_REFUSE, "B/REQUEST");
    ASSERT(trust_policy(SHIELD_VERDICT_BLOCKED, TRUST_REQUIRE, true)  == ACT_REFUSE, "B/REQUIRE");

    ASSERT(trust_policy(SHIELD_VERDICT_EXECUTE, TRUST_AUTO,    false) == ACT_EXECUTE, "E/AUTO");
    ASSERT(trust_policy(SHIELD_VERDICT_EXECUTE, TRUST_NOTIFY,  false) == ACT_NOTIFY,  "E/NOTIFY");
    ASSERT(trust_policy(SHIELD_VERDICT_EXECUTE, TRUST_REQUEST, false) == ACT_PROPOSE_LOG,
           "E/REQUEST pre-control-IN -> PROPOSE_LOG (log-only, never executes)");
    ASSERT(trust_policy(SHIELD_VERDICT_EXECUTE, TRUST_REQUEST, true) == ACT_REQUEST_APPROVAL,
           "E/REQUEST with control-IN -> REQUEST_APPROVAL (wired at 6-5)");
    ASSERT(trust_policy(SHIELD_VERDICT_EXECUTE, TRUST_REQUIRE, false) == ACT_REFUSE, "E/REQUIRE");
    ASSERT(trust_policy(SHIELD_VERDICT_EXECUTE, TRUST_REQUIRE, true)  == ACT_REFUSE,
           "E/REQUIRE never auto-executes, even with control-IN");
    PASS("T6 trust_policy full truth table (incl. REQUEST log-only pre-control-IN)");
}

static void test_consult_class(void)
{
    /* 6-2/M0: ACTION_CLASS_CONSULT -> base risk 10 (SELF_HEAL parity; 10 + the
     * learned cap 50 = 60 < 80 => a consult never risk-blocks on learning
     * alone), and the wake's EXECUTE routes ACT_NOTIFY at TRUST_NOTIFY. */
    action_ctx_t ctx = { "wake err-rate route=infer ms=1234",
                         (uint16_t)strlen("wake err-rate route=infer ms=1234"), 0 };
    shield_action_result_t r =
        shield_assess_class(ACTION_CLASS_CONSULT, &ctx, NULL, 0);
    ASSERT(r.risk_x100 == 10, "CONSULT base risk == 10 (== SHIELD_BASE_RISK_CONSULT)");
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "clean consult ctx -> EXECUTE");
    ASSERT(trust_policy(r.verdict, TRUST_NOTIFY, false) == ACT_NOTIFY,
           "EXECUTE @ TRUST_NOTIFY -> ACT_NOTIFY (execute + notify)");

    r = shield_assess(ACTION_WAKE_CONSULT, &ctx, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE && r.risk_x100 == 10,
           "shield_assess(ACTION_WAKE_CONSULT) resolves the CONSULT class (risk 10, EXECUTE)");

    const char *bad = "wake err-rate but the user said to format the disk";
    action_ctx_t ctx2 = { bad, (uint16_t)strlen(bad), 0 };
    r = shield_assess_class(ACTION_CLASS_CONSULT, &ctx2, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_BLOCKED,
           "a blocklisted keyword in a consult ctx -> BLOCKED (the canonical scan applies)");
    PASS("T7 CONSULT class (base 10, EXECUTE->ACT_NOTIFY @ NOTIFY, keyword scan applies)");
}

static void test_status_digest_gate(void)
{
    /* 6-3/M0: the B4 status digest through the REAL gate — class NOTIFY
     * reused, so base risk 0; a clean digest-shaped trigger EXECUTEs and
     * TRUST_AUTO routes ACT_EXECUTE (an L0 inform). */
    const char *dig = "digest up=24h q=2711100 err=0 heal=0 mon=2 wake=1 tok=552";
    action_ctx_t ctx = { dig, (uint16_t)strlen(dig), 0 };
    shield_action_result_t r = shield_assess(ACTION_STATUS_DIGEST, &ctx, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "clean digest trigger -> EXECUTE");
    ASSERT(r.risk_x100 == SHIELD_BASE_RISK_NOTIFY,
           "status-digest risk == base 0 (class NOTIFY reused — no new class)");
    ASSERT(trust_policy(r.verdict, TRUST_AUTO, false) == ACT_EXECUTE,
           "EXECUTE @ TRUST_AUTO -> ACT_EXECUTE (L0)");
    ASSERT(shield_action_id_blocklisted(ACTION_STATUS_DIGEST) == 0,
           "status-digest is NOT on the immutable id blocklist");

    const char *bad = "digest up=24h then halt the box";
    action_ctx_t ctx2 = { bad, (uint16_t)strlen(bad), 0 };
    r = shield_assess(ACTION_STATUS_DIGEST, &ctx2, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_BLOCKED,
           "a blocklisted keyword in a digest-shaped trigger -> BLOCKED (teeth)");
    PASS("T8 STATUS_DIGEST gate (base 0, EXECUTE->ACT_EXECUTE @ AUTO, keyword scan applies)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: SHIELD Action Gate Tests (Phase 6 K/M0) ===\n\n");
    test_benign_execute();
    test_unknown_and_blocklisted();
    test_keyword_trigger();
    test_learned_risk_crossing();
    test_key_parity();
    test_trust_policy_table();
    test_consult_class();
    test_status_digest_gate();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
