/*
 * Phase 6 goal 6-1 M0 — host test for the pure monitor framework.
 *
 * Pins the anti-spam crux (debounced fire-ONCE-per-crossing + hysteresis re-arm), the
 * per-boot delta semantics, and the keyword-clean snapshot discipline: every event
 * type's snapshot must contain NO canonical blocklist keyword AND must pass the REAL
 * shield_assess(ACTION_NOTIFY_ANOMALY, ...) un-BLOCKED (the test_km2b_trigger
 * discipline — a monitor NOTIFY must never block itself).
 *
 * Build: gcc -Wall -Werror -O2 -std=c11 -I. test_monitors.c monitors.c \
 *   shield_action.c action_allowlist.c shield_learn.c -o test_monitors && ./test_monitors
 */

#include "monitors.h"
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

static int snapshot_keyword_clean(const char *s)
{
    for (size_t k = 0; k < KW_N; k++)
        if (contains_ci(s, kw[k])) return 0;
    return 1;
}

int main(void)
{
    printf("=== test_monitors (Phase 6 goal 6-1 M0) ===\n");

    /* T1: GE crossing, debounce 1 — fires on the first over-threshold sample. */
    {
        monitor_t m;
        monitor_init(&m, 10, MON_CMP_GE, 1);
        CHECK(monitor_sample(&m, 5)  == 0, "T1a under threshold -> 0");
        CHECK(monitor_sample(&m, 10) == 1, "T1b value == threshold (GE) -> fires");
    }

    /* T2: debounce 3 — two consecutive then a gap resets; three consecutive fire on the 3rd. */
    {
        monitor_t m;
        monitor_init(&m, 10, MON_CMP_GE, 3);
        CHECK(monitor_sample(&m, 20) == 0, "T2a 1st consecutive -> 0");
        CHECK(monitor_sample(&m, 20) == 0, "T2b 2nd consecutive -> 0");
        CHECK(monitor_sample(&m, 3)  == 0, "T2c gap (under) resets consec");
        CHECK(monitor_sample(&m, 20) == 0 && monitor_sample(&m, 20) == 0,
              "T2d post-gap 1st+2nd -> 0 (not cumulative)");
        CHECK(monitor_sample(&m, 20) == 1, "T2e 3rd consecutive -> fires");
    }

    /* T3: fire-once — a sustained over-threshold condition fires exactly once. */
    {
        monitor_t m;
        monitor_init(&m, 10, MON_CMP_GE, 1);
        CHECK(monitor_sample(&m, 50) == 1, "T3a crossing fires");
        CHECK(monitor_sample(&m, 60) == 0 && monitor_sample(&m, 70) == 0,
              "T3b sustained condition does NOT re-fire");
    }

    /* T4: re-arm / hysteresis — return to the non-triggering side, then a new crossing fires again. */
    {
        monitor_t m;
        monitor_init(&m, 10, MON_CMP_GE, 1);
        (void)monitor_sample(&m, 50);                       /* fire #1 */
        CHECK(monitor_sample(&m, 2)  == 0, "T4a back under threshold -> 0 (re-arms)");
        CHECK(monitor_sample(&m, 50) == 1, "T4b next crossing fires again");
    }

    /* T5: MON_CMP_LE (link-down style) — fires when value <= threshold. */
    {
        monitor_t m;
        monitor_init(&m, 0, MON_CMP_LE, 1);
        CHECK(monitor_sample(&m, 7) == 0, "T5a above threshold (LE) -> 0");
        CHECK(monitor_sample(&m, 0) == 1, "T5b value == threshold (LE) -> fires");
        CHECK(monitor_sample(&m, 9) == 0 && monitor_sample(&m, 0) == 1,
              "T5c LE re-arm on the high side, fires again");
    }

    /* T6: delta_step — first call baselines to 0; then cur-prev; a boot_id change resets. */
    {
        monitor_delta_t d = {0};
        CHECK(monitor_delta_step(&d, 100, 7) == 0,  "T6a first call -> 0 (baseline)");
        CHECK(monitor_delta_step(&d, 130, 7) == 30, "T6b 130-100 -> 30");
        CHECK(monitor_delta_step(&d, 130, 7) == 0,  "T6c unchanged counter -> 0");
        CHECK(monitor_delta_step(&d, 20, 8)  == 0,  "T6d boot_id change -> 0 (re-baseline)");
        CHECK(monitor_delta_step(&d, 45, 8)  == 25, "T6e post-reboot 45-20 -> 25");
    }

    /* T7: every event type's snapshot is keyword-clean AND never blocks its own NOTIFY
     * through the REAL shield_assess (ACTION_NOTIFY_ANOMALY, TRUST_AUTO, class NOTIFY). */
    {
        int all_clean = 1, all_allowed = 1, all_len_ok = 1;
        for (int t = MON_EV_ERROR_RATE; t < MON_EV__COUNT; t++) {
            monitor_event_t ev;
            int n = monitor_build_snapshot(&ev, (monitor_event_type_t)t, 1,
                                           1234567, -42);
            if (n <= 0 || ev.snap_len != (uint16_t)n ||
                ev.snap[ev.snap_len] != '\0' || (int)strlen(ev.snap) != n) {
                all_len_ok = 0;
                printf("        (type %d: n=%d snap_len=%u)\n", t, n, ev.snap_len);
                continue;
            }
            if (!snapshot_keyword_clean(ev.snap)) {
                all_clean = 0;
                printf("        (type %d snapshot carries a blocklist keyword: \"%s\")\n", t, ev.snap);
            }
            action_ctx_t ctx = { .trigger = ev.snap, .trigger_len = ev.snap_len,
                                 .query_key = 0 };
            shield_action_result_t sr = shield_assess(ACTION_NOTIFY_ANOMALY, &ctx, NULL, 0);
            if (sr.verdict != SHIELD_VERDICT_EXECUTE) all_allowed = 0;
        }
        CHECK(all_len_ok,  "T7a every type: NUL-bounded + snap_len == strlen");
        CHECK(all_clean,   "T7b every type: NO canonical blocklist keyword");
        CHECK(all_allowed, "T7c every type: shield_assess(NOTIFY_ANOMALY) never BLOCKED");
    }

    /* T7d: distinct types render distinct literals (a shared misformat can't hide). */
    {
        monitor_event_t a, b;
        monitor_build_snapshot(&a, MON_EV_ERROR_RATE,   1, 5, 100);
        monitor_build_snapshot(&b, MON_EV_STORE_WRAP,   1, 5, 100);
        CHECK(strcmp(a.snap, b.snap) != 0, "T7d different types -> different snapshots");
    }

    /* T8: NULL-arg guards + debounce 0 treated as 1 + invalid snapshot type. */
    {
        CHECK(monitor_sample(NULL, 99) == 0, "T8a monitor_sample(NULL) -> 0, no crash");
        monitor_init(NULL, 1, MON_CMP_GE, 1);   /* no-op, no crash */
        CHECK(monitor_delta_step(NULL, 5, 1) == 0, "T8b delta_step(NULL) -> 0, no crash");
        monitor_t z;
        monitor_init(&z, 10, MON_CMP_GE, 0);
        CHECK(monitor_sample(&z, 99) == 1, "T8c debounce 0 acts as 1 (fires immediately)");
        monitor_event_t ev;
        CHECK(monitor_build_snapshot(&ev, MON_EV_NONE, 0, 0, 0) == -1 &&
              monitor_build_snapshot(&ev, MON_EV__COUNT, 0, 0, 0) == -1 &&
              monitor_build_snapshot(NULL, MON_EV_ERROR_RATE, 0, 0, 0) == -1,
              "T8d invalid type / NULL event -> -1");
    }

    printf("=== %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
