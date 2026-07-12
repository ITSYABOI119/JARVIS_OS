/*
 * JARVIS AI-OS — Action Allowlist Unit Tests (Phase 6 K/M0)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_action_allowlist.c phase3/src/ai/action_allowlist.c \
 *       -o /tmp/test_action_allowlist
 *
 * Pure host test: the static compile-time allowlist (K-b — the LLM selects an
 * id, never synthesizes an action; unknown ids refuse before SHIELD).
 */

#include "action_allowlist.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", msg, __LINE__); tests_failed++; return; } \
} while (0)

#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while (0)

static void test_restart_pb_entry(void)
{
    const action_def_t *d = action_lookup(ACTION_RESTART_PB);
    ASSERT(d != NULL, "ACTION_RESTART_PB is allowlisted");
    ASSERT(d->id == ACTION_RESTART_PB, "id round-trips");
    ASSERT(d->trust == TRUST_NOTIFY, "restart_pb trust == TRUST_NOTIFY (L1 — execute + notify)");
    ASSERT(d->action_class == ACTION_CLASS_SELF_HEAL, "restart_pb class == SELF_HEAL");
    ASSERT(d->name != NULL && strcmp(d->name, "restart_pb") == 0, "name for logs only");
    PASS("T1 ACTION_RESTART_PB entry (TRUST_NOTIFY, SELF_HEAL)");
}

static void test_notify_anomaly_entry(void)
{
    const action_def_t *d = action_lookup(ACTION_NOTIFY_ANOMALY);
    ASSERT(d != NULL, "ACTION_NOTIFY_ANOMALY is allowlisted");
    ASSERT(d->trust == TRUST_AUTO, "notify_anomaly trust == TRUST_AUTO (L0)");
    ASSERT(d->action_class == ACTION_CLASS_NOTIFY, "notify_anomaly class == NOTIFY");
    PASS("T2 ACTION_NOTIFY_ANOMALY entry (TRUST_AUTO, NOTIFY)");
}

static void test_unknown_id_refusal(void)
{
    ASSERT(action_lookup(9999) == NULL, "unknown id 9999 -> NULL (refuse before SHIELD)");
    ASSERT(action_lookup(0) == NULL, "id 0 -> NULL (never a valid action)");
    ASSERT(action_lookup(ACTION_ID_BLOCK_PROBE) == NULL,
           "the reserved M1 probe id is NOT allowlisted");
    PASS("T3 unknown-id refusal (9999, 0, the reserved probe id)");
}

static void test_count_and_names(void)
{
    ASSERT(action_allowlist_count() == 4, "exactly 4 DEPLOYED entries (since 6-3/M0)");
    ASSERT(action_lookup(ACTION_RESTART_PB)->name != NULL &&
           action_lookup(ACTION_NOTIFY_ANOMALY)->name != NULL &&
           action_lookup(ACTION_WAKE_CONSULT)->name != NULL &&
           action_lookup(ACTION_STATUS_DIGEST)->name != NULL,
           "every entry has a non-NULL name");
    PASS("T4 allowlist count == 4, names non-NULL");
}

static void test_wake_consult_entry(void)
{
    /* 6-2/M0: the event-triggered consult — TRUST_NOTIFY (monotonic with the
     * restart's NOTIFY: a consult burns 10-120 s of compute, it must not sit
     * BELOW the self-heal in ceremony), class CONSULT (base risk 10). */
    const action_def_t *d = action_lookup(ACTION_WAKE_CONSULT);
    ASSERT(d != NULL, "ACTION_WAKE_CONSULT is allowlisted");
    ASSERT(d->id == ACTION_WAKE_CONSULT, "id round-trips");
    ASSERT(d->trust == TRUST_NOTIFY, "wake-consult trust == TRUST_NOTIFY (L1)");
    ASSERT(d->action_class == ACTION_CLASS_CONSULT, "wake-consult class == CONSULT");
    ASSERT(d->name != NULL && strcmp(d->name, "wake-consult") == 0, "name for logs only");
    PASS("T5 ACTION_WAKE_CONSULT entry (TRUST_NOTIFY, CONSULT)");
}

static void test_status_digest_entry(void)
{
    /* 6-3/M0: the B4 status digest — TRUST_AUTO (L0, an inform's "execute" =
     * its serial line + JACT record), class NOTIFY reused (base risk 0 — NO
     * new class; the digest is a notify-shaped inform, not a consult). */
    const action_def_t *d = action_lookup(ACTION_STATUS_DIGEST);
    ASSERT(d != NULL, "ACTION_STATUS_DIGEST is allowlisted");
    ASSERT(d->id == ACTION_STATUS_DIGEST && d->id == 4, "id round-trips (== 4, append-only)");
    ASSERT(d->trust == TRUST_AUTO, "status-digest trust == TRUST_AUTO (L0)");
    ASSERT(d->action_class == ACTION_CLASS_NOTIFY,
           "status-digest class == NOTIFY (reused — base risk 0, no new class)");
    ASSERT(d->name != NULL && strcmp(d->name, "status-digest") == 0, "name for logs only");
    PASS("T6 ACTION_STATUS_DIGEST entry (TRUST_AUTO, class NOTIFY reused)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: Action Allowlist Tests (Phase 6 K/M0) ===\n\n");
    test_restart_pb_entry();
    test_notify_anomaly_entry();
    test_unknown_id_refusal();
    test_count_and_names();
    test_wake_consult_entry();
    test_status_digest_entry();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
