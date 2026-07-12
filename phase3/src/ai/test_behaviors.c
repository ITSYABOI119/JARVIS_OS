/*
 * JARVIS AI-OS — Behavior Registry Unit Tests (Phase 6 6-3/M0)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_behaviors.c phase3/src/ai/behaviors.c \
 *       phase3/src/ai/monitors.c phase3/src/ai/shield_action.c \
 *       phase3/src/ai/action_allowlist.c phase3/src/ai/shield_learn.c \
 *       phase3/src/ai/decision_cache.c -lm -o /tmp/t_behaviors
 *
 * Pure host test of the 6-3 behavior registry: the compile-time B1–B5 table
 * (trigger -> allowlisted L0/L1 INFORM action — the K-b select-never-synthesize
 * boundary extended to behaviors), the INFORM-PURE pin (O4: no PROPOSE_LOG /
 * REQUEST / REQUIRE in v1), and the INTEGER-ONLY digest builder (golden
 * byte-exact, keyword-clean BOTH ways through the REAL shield_assess with
 * teeth, cap-clamp/canary, guards).
 * Design: phase6/docs/PHASE_6_GOAL_6-3_PROACTIVE.md §9 (M0).
 */

#include "behaviors.h"
#include "action_allowlist.h"
#include "shield_action.h"
#include "action_audit.h"   /* ACT_TRIGGER_MAX — the digest must fit a JACT snapshot */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", msg, __LINE__); tests_failed++; return; } \
} while (0)

#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while (0)

/* KEEP IN SYNC with shield_action.c g_keyword_blocklist[] (the ONE canonical
 * immutable list). Mirrored so the digest scan is an independent substring
 * check, not a tautology through shield_assess (which is the (ii) leg). */
static const char *const g_blocklist_mirror[] = {
    "delete", "remove", "kill", "destroy", "format",
    "rm -rf", "drop table", "shutdown", "halt",
};
#define N_BLOCK 9

/* Case-insensitive substring check (mirrors shield_action.c's semantics). */
static int has_substr_ci(const char *hay, const char *needle)
{
    size_t hl = strlen(hay), nl = strlen(needle);
    if (nl == 0 || nl > hl) return 0;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t j = 0;
        while (j < nl) {
            char a = hay[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (a != b) break;
            j++;
        }
        if (j == nl) return 1;
    }
    return 0;
}

/* The exact v1 registry contract: {behavior id -> trigger, action_id}. */
static const struct { uint16_t id; monitor_event_type_t trig; uint16_t act; } g_expect[] = {
    { BEHAVIOR_ANOMALY_CONSULT,  MON_EV_ERROR_RATE,       ACTION_WAKE_CONSULT   },
    { BEHAVIOR_SELFHEAL_CONSULT, MON_EV_SELF_HEAL_RATE,   ACTION_WAKE_CONSULT   },
    { BEHAVIOR_STORE_ROLL,       MON_EV_STORE_WRAP,       ACTION_NOTIFY_ANOMALY },
    { BEHAVIOR_STATUS_DIGEST,    MON_EV_UPTIME_MILESTONE, ACTION_STATUS_DIGEST  },
    { BEHAVIOR_DEGRADED_ALERT,   MON_EV_DEGRADED,         ACTION_NOTIFY_ANOMALY },
};
#define N_EXPECT 5

/* A. Registry truth table: count, exact per-id {trigger, action_id}, bounds. */
static void test_registry_truth_table(void)
{
    ASSERT(behavior_count() == 5, "behavior_count() == 5 (O1: B1-B5, B6 deferred)");
    for (int i = 0; i < N_EXPECT; i++) {
        const behavior_def_t *b = behavior_lookup(g_expect[i].id);
        ASSERT(b != NULL, "behavior id resolves");
        ASSERT(b->id == g_expect[i].id, "id round-trips");
        ASSERT(b->trigger == g_expect[i].trig, "EXACT trigger mapping");
        ASSERT(b->action_id == g_expect[i].act, "EXACT action_id mapping");
        ASSERT(b->name != NULL && b->name[0] != '\0', "name non-empty (logs/console only)");
        ASSERT(b->surface != NULL && b->surface[0] != '\0', "surface prose non-empty");
        ASSERT(b->id >= 1 && b->id <= 16,
               "id in 1..16 (the u16 behaviors_mask bit = id-1 must fit)");
    }
    ASSERT(behavior_lookup(0) == NULL, "id 0 -> NULL (0 = none)");
    ASSERT(behavior_lookup(6) == NULL, "id 6 -> NULL (no 6th behavior in v1)");
    ASSERT(behavior_lookup(9999) == NULL, "id 9999 -> NULL");
    PASS("A registry truth table (count 5, exact trigger/action per id, 0/6/9999 -> NULL)");
}

/* B. behavior_for_event: every trigger maps to ITS behavior; unmapped -> NULL. */
static void test_for_event(void)
{
    for (int i = 0; i < N_EXPECT; i++) {
        const behavior_def_t *b = behavior_for_event(g_expect[i].trig);
        ASSERT(b != NULL && b->id == g_expect[i].id, "trigger -> its behavior");
    }
    ASSERT(behavior_for_event(MON_EV_NONE) == NULL, "NONE -> NULL");
    ASSERT(behavior_for_event(MON_EV_HEARTBEAT_AGE) == NULL,
           "HEARTBEAT_AGE (unwired since 6-1) -> NULL");
    ASSERT(behavior_for_event(MON_EV__COUNT) == NULL, "__COUNT -> NULL");
    ASSERT(behavior_for_event((monitor_event_type_t)99) == NULL, "out-of-range 99 -> NULL");
    PASS("B behavior_for_event (5 mapped; NONE/HEARTBEAT_AGE/__COUNT/99 -> NULL)");
}

/* C. INFORM-PURE pin (O4): every behavior's action is allowlisted AND L0/L1 —
 * never TRUST_REQUEST/TRUST_REQUIRE, so nothing can PROPOSE_LOG or REFUSE on
 * a clean verdict; v1 ships zero state-changing behaviors. */
static void test_inform_pure(void)
{
    for (uint16_t id = 1; id <= 5; id++) {
        const behavior_def_t *b = behavior_lookup(id);
        ASSERT(b != NULL, "behavior resolves");
        const action_def_t *a = action_lookup(b->action_id);
        ASSERT(a != NULL, "behavior action_id IS allowlisted (refuse-before-SHIELD holds)");
        ASSERT(a->trust == TRUST_AUTO || a->trust == TRUST_NOTIFY,
               "trust is L0/L1 (AUTO or NOTIFY) — NEVER REQUEST/REQUIRE (O4)");
        act_decision_t d = trust_policy(SHIELD_VERDICT_EXECUTE, a->trust, false);
        ASSERT(d == ACT_EXECUTE || d == ACT_NOTIFY,
               "clean verdict executes as an inform (never PROPOSE_LOG/REFUSE)");
        ASSERT(shield_action_id_blocklisted(b->action_id) == 0,
               "no behavior action is on the immutable id blocklist");
    }
    PASS("C INFORM-PURE (every action allowlisted, L0/L1, executes as an inform)");
}

/* D. Digest builder: byte-exact golden + stability + the JACT-cap bound. */
static void test_digest_golden(void)
{
    char b1[512], b2[512];
    int l1 = behavior_build_digest(b1, sizeof b1, 24, 2711100, 0, 0, 2, 1, 552);
    ASSERT(l1 > 0, "digest builds");
    ASSERT(strcmp(b1, "digest up=24h q=2711100 err=0 heal=0 mon=2 wake=1 tok=552") == 0,
           "EXACT golden: \"digest up=24h q=2711100 err=0 heal=0 mon=2 wake=1 tok=552\"");
    ASSERT(l1 == (int)strlen(b1), "returned len == strlen");
    int l2 = behavior_build_digest(b2, sizeof b2, 24, 2711100, 0, 0, 2, 1, 552);
    ASSERT(l2 == l1 && memcmp(b1, b2, (size_t)l1 + 1) == 0,
           "byte-identical across calls (same integers in, same bytes out)");

    /* All-max args: the worst-case digest still fits a JACT trigger snapshot. */
    int lmax = behavior_build_digest(b1, sizeof b1, UINT32_MAX, UINT64_MAX, UINT64_MAX,
                                     UINT32_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX);
    ASSERT(lmax > 0 && lmax == (int)strlen(b1), "all-max args build");
    ASSERT(lmax < ACT_TRIGGER_MAX, "worst-case digest < ACT_TRIGGER_MAX (456)");
    PASS("D digest golden (byte-exact, stable, worst-case < the JACT trigger cap)");
}

/* E. Digest keyword-clean BOTH ways + the teeth proof. */
static void test_digest_keyword_clean(void)
{
    char buf[512];
    int l = behavior_build_digest(buf, sizeof buf, 24, 2711100, 0, 0, 2, 1, 552);
    ASSERT(l > 0, "digest builds");
    /* (i) independent blocklist SUBSTRING scan (golden + all-max shapes). */
    for (int k = 0; k < N_BLOCK; k++)
        ASSERT(!has_substr_ci(buf, g_blocklist_mirror[k]),
               "digest contains NO canonical blocklist keyword (substring, ci)");
    char mx[512];
    int lmax = behavior_build_digest(mx, sizeof mx, UINT32_MAX, UINT64_MAX, UINT64_MAX,
                                     UINT32_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX);
    ASSERT(lmax > 0, "all-max digest builds");
    for (int k = 0; k < N_BLOCK; k++)
        ASSERT(!has_substr_ci(mx, g_blocklist_mirror[k]),
               "all-max digest keyword-clean too (digits can't form a keyword)");

    /* (ii) the REAL gate: shield_assess(ACTION_STATUS_DIGEST) un-BLOCKED at
     * base risk 0 (class NOTIFY reused), and TRUST_AUTO executes. */
    action_ctx_t ctx = { buf, (uint16_t)l, 0 };
    shield_action_result_t r = shield_assess(ACTION_STATUS_DIGEST, &ctx, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_EXECUTE, "digest passes the REAL shield_assess");
    ASSERT(r.risk_x100 == SHIELD_BASE_RISK_NOTIFY, "digest risk == base 0 (class NOTIFY)");
    const action_def_t *d = action_lookup(ACTION_STATUS_DIGEST);
    ASSERT(d != NULL && trust_policy(r.verdict, d->trust, false) == ACT_EXECUTE,
           "EXECUTE @ TRUST_AUTO -> ACT_EXECUTE (L0 inform)");

    /* TEETH: an injected blocklist keyword in a status-digest trigger DOES
     * block — proving the (ii) leg can actually fail (M0 pin list). */
    const char *poison = "digest up=24h then shutdown the box";
    action_ctx_t bad = { poison, (uint16_t)strlen(poison), 0 };
    r = shield_assess(ACTION_STATUS_DIGEST, &bad, NULL, 0);
    ASSERT(r.verdict == SHIELD_VERDICT_BLOCKED,
           "a poisoned digest-shaped trigger IS BLOCKED (the gate has teeth)");
    PASS("E digest keyword-clean both ways (scan + real shield_assess) + teeth");
}

/* F. Digest cap-clamp + 0xAA canary + NULL/cap guards. */
static void test_digest_bounds(void)
{
    char cb[64];
    memset(cb, (char)0xAA, sizeof cb);
    int l = behavior_build_digest(cb, 12, 24, 2711100, 0, 0, 2, 1, 552);
    ASSERT(l == 11 && cb[11] == '\0', "cap 12 -> 11 chars + NUL");
    ASSERT(strncmp(cb, "digest up=2", 11) == 0, "clamped content is the prefix");
    for (int i = 12; i < (int)sizeof cb; i++)
        ASSERT(cb[i] == (char)0xAA, "canary beyond cap untouched");

    ASSERT(behavior_build_digest(NULL, 64, 1, 1, 1, 1, 1, 1, 1) == -1, "NULL buf -> -1");
    char b[8];
    ASSERT(behavior_build_digest(b, 0, 1, 1, 1, 1, 1, 1, 1) == -1, "cap 0 -> -1");
    l = behavior_build_digest(b, 1, 1, 1, 1, 1, 1, 1, 1);
    ASSERT(l == 0 && b[0] == '\0', "cap 1 -> empty string (NUL only), len 0");
    PASS("F digest cap-clamp + canary + NULL/cap guards");
}

/* G. The 6-2 seam holds: the two consult behaviors' triggers ARE wake-templated
 * (B1/B2 ride the live lane); the notify/digest triggers are NOT (they never
 * stage a wake — benign events deliberately don't consult). */
static void test_wake_seam(void)
{
    /* Deliberately NOT including wake.h: the seam contract here is only that
     * the registry's consult rows use ACTION_WAKE_CONSULT and the others
     * don't — the template-side truth table lives in test_wake.c A. */
    for (uint16_t id = 1; id <= 5; id++) {
        const behavior_def_t *b = behavior_lookup(id);
        ASSERT(b != NULL, "behavior resolves");
        int is_consult = (b->action_id == ACTION_WAKE_CONSULT);
        int is_degradation = (b->trigger == MON_EV_ERROR_RATE ||
                              b->trigger == MON_EV_SELF_HEAL_RATE);
        ASSERT(is_consult == is_degradation,
               "consult behaviors == the two degradation triggers (B1/B2), no others");
    }
    PASS("G consult/notify split matches the 6-2 lane (only B1/B2 consult)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: Behavior Registry Tests (Phase 6 6-3/M0) ===\n\n");
    test_registry_truth_table();
    test_for_event();
    test_inform_pure();
    test_digest_golden();
    test_digest_keyword_clean();
    test_digest_bounds();
    test_wake_seam();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
