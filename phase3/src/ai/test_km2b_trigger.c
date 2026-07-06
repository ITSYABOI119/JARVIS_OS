/*
 * Phase 6 K/M2b-2 — host test for the keyword-clean restart trigger (§5-E).
 *
 * Pins the two properties pa_restart_pb depends on:
 *   1. the BUILT trigger_snapshot never contains a canonical blocklist keyword
 *      (system facts only — fixed literals + digits), so
 *   2. shield_assess(ACTION_RESTART_PB, <built trigger>, NULL, 0) NEVER returns
 *      BLOCKED — the restart can never block itself (§5-E / Decision #6).
 * Plus: cap honoring (reason is caller-controlled), exact format, and the
 * defense still firing when the §5-E contract is violated (poisoned reason).
 *
 * Build: gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *   phase3/src/ai/test_km2b_trigger.c phase3/src/ai/km2b_trigger.c \
 *   phase3/src/ai/shield_action.c phase3/src/ai/action_allowlist.c \
 *   phase3/src/ai/shield_learn.c phase3/src/ai/decision_cache.c -lm
 */

#include "km2b_trigger.h"
#include "shield_action.h"
#include "action_allowlist.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  PASS  %s\n", name); } \
    else      { g_fail++; printf("  FAIL  %s\n", name); } \
} while (0)

/* The canonical keyword blocklist (shield_action.c N-A — the reviewed union). */
static const char *const kw[] = {
    "delete", "remove", "kill", "destroy", "format",
    "rm -rf", "drop table", "shutdown", "halt",
};
#define KW_N (sizeof(kw) / sizeof(kw[0]))

static int contains_ci(const char *hay, const char *needle)
{
    size_t nl = strlen(needle), hl = strlen(hay);
    if (nl == 0 || hl < nl) return 0;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t j = 0;
        while (j < nl && tolower((unsigned char)hay[i + j]) ==
                         tolower((unsigned char)needle[j])) j++;
        if (j == nl) return 1;
    }
    return 0;
}

static int trigger_keyword_clean(const char *trig)
{
    for (size_t k = 0; k < KW_N; k++)
        if (contains_ci(trig, kw[k])) return 0;
    return 1;
}

int main(void)
{
    printf("=== test_km2b_trigger (Phase 6 K/M2b-2 §5-E) ===\n");
    char trig[456];

    /* T1: exact format, "fault" reason (the fault-EP path). */
    int len = km2b_build_trigger(trig, sizeof(trig), "fault", 0, 0, 0, 5, 3);
    CHECK(strcmp(trig, "respawn fault lane=0 miss=0 age=0ms flt=5/3") == 0,
          "T1a fault-reason exact format");
    CHECK(len == (int)strlen(trig), "T1b returned length == strlen");

    /* T2: exact format, "hang" reason (the miss-counter path). */
    len = km2b_build_trigger(trig, sizeof(trig), "hang", 2, 3, 12345, 0, 0);
    CHECK(strcmp(trig, "respawn hang lane=2 miss=3 age=12345ms flt=0/0") == 0,
          "T2 hang-reason exact format");

    /* T3: max counter values never overflow nor approach the JACT cap (456). */
    len = km2b_build_trigger(trig, sizeof(trig), "fault", 65535, 4294967295u,
                             0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull,
                             0xFFFFFFFFFFFFFFFFull);
    CHECK(len > 0 && len < 128 && trig[len] == '\0',
          "T3 max values bounded well under ACT_TRIGGER_MAX");

    /* T4: keyword-clean for both LIVE reasons across value ranges. */
    int clean = 1;
    for (uint32_t m = 0; m < 4 && clean; m++) {
        km2b_build_trigger(trig, sizeof(trig), "fault", (uint16_t)m, m * 7u,
                           m * 1000ull, m, m + 1);
        clean = trigger_keyword_clean(trig);
        if (clean) {
            km2b_build_trigger(trig, sizeof(trig), "hang", (uint16_t)m, m * 7u,
                               m * 1000ull, m, m + 1);
            clean = trigger_keyword_clean(trig);
        }
    }
    CHECK(clean, "T4 built triggers contain NO canonical blocklist keyword");

    /* T5: the restart can never block itself — shield_assess on the built
     * trigger EXECUTEs at base SELF_HEAL risk for both live reasons. */
    len = km2b_build_trigger(trig, sizeof(trig), "fault", 1, 2, 300, 5, 0);
    action_ctx_t ctx = { .trigger = trig, .trigger_len = (uint16_t)len, .query_key = 0 };
    shield_action_result_t r = shield_assess(ACTION_RESTART_PB, &ctx, NULL, 0);
    CHECK(r.verdict == SHIELD_VERDICT_EXECUTE && r.risk_x100 == SHIELD_BASE_RISK_SELF_HEAL,
          "T5a fault trigger never self-blocks (EXECUTE, risk=10)");
    len = km2b_build_trigger(trig, sizeof(trig), "hang", 0, 3, 9000, 0, 0);
    ctx.trigger_len = (uint16_t)len;
    r = shield_assess(ACTION_RESTART_PB, &ctx, NULL, 0);
    CHECK(r.verdict == SHIELD_VERDICT_EXECUTE && r.risk_x100 == SHIELD_BASE_RISK_SELF_HEAL,
          "T5b hang trigger never self-blocks (EXECUTE, risk=10)");

    /* T6: the defense still fires when the §5-E contract is violated — a
     * poisoned reason (query text leaking in) IS caught by the keyword scan. */
    len = km2b_build_trigger(trig, sizeof(trig), "kill all processes", 0, 0, 0, 0, 0);
    ctx.trigger_len = (uint16_t)len;
    r = shield_assess(ACTION_RESTART_PB, &ctx, NULL, 0);
    CHECK(r.verdict == SHIELD_VERDICT_BLOCKED,
          "T6 poisoned reason IS blocked (the gate still guards)");

    /* T7: cap honored — reason is caller-controlled, output must clamp. */
    char small[16];
    memset(small, 0xAA, sizeof(small));
    len = km2b_build_trigger(small, 12, "fault", 65535, 4294967295u, 0, 0, 0);
    CHECK(len == 11 && small[11] == '\0' && (unsigned char)small[12] == 0xAA &&
          (unsigned char)small[15] == 0xAA,
          "T7 cap clamps output (NUL at cap-1, canary intact)");

    /* T8: NULL/degenerate guards. */
    CHECK(km2b_build_trigger(NULL, 456, "fault", 0, 0, 0, 0, 0) == 0 &&
          km2b_build_trigger(small, 0, "fault", 0, 0, 0, 0, 0) == 0,
          "T8 NULL buf / cap 0 return 0");

    /* T9: the funnel's trust path — ACTION_RESTART_PB is TRUST_NOTIFY, so an
     * EXECUTE verdict resolves to ACT_NOTIFY (execute + notify), which
     * pa_restart_pb treats as execute. */
    const action_def_t *ad = action_lookup(ACTION_RESTART_PB);
    CHECK(ad != NULL &&
          trust_policy(SHIELD_VERDICT_EXECUTE, ad->trust, false) == ACT_NOTIFY,
          "T9 restart trust path: EXECUTE @ TRUST_NOTIFY -> ACT_NOTIFY");

    printf("=== %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
