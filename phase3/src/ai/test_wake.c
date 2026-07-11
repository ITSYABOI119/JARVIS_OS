/*
 * JARVIS AI-OS — Wake Decision Core Unit Tests (Phase 6 6-2/M0)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_wake.c phase3/src/ai/wake.c \
 *       phase3/src/ai/shield_action.c phase3/src/ai/action_allowlist.c \
 *       phase3/src/ai/shield_learn.c phase3/src/ai/decision_cache.c \
 *       -lm -o /tmp/test_wake
 *
 * Pure host test of the 6-2 wake decision core: the STATIC template allowlist
 * (select-by-TYPE-only — the injection boundary), keyword-clean BOTH ways
 * through the REAL shield_assess (the T7 discipline — this is what catches the
 * SUBSTRING traps: "skill" contains "kill", "information" contains "format"),
 * cache-key stability + disjointness from the workload seeds, the JACT trigger
 * builder, and the anti-loop gate (one-slot latch + per-type cooldown + hourly
 * budget, wrap-safe). Design: phase6/docs/PHASE_6_GOAL_6-2_EVENT_WAKE.md §8 M0.
 */

#include "wake.h"
#include "shield_action.h"
#include "action_allowlist.h"
#include "decision_cache.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", msg, __LINE__); tests_failed++; return; } \
} while (0)

#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while (0)

/* The two v1 templated (degradation) event types. */
static const monitor_event_type_t g_mapped[] = { MON_EV_ERROR_RATE, MON_EV_SELF_HEAL_RATE };
#define N_MAPPED 2

/* KEEP IN SYNC with shield_action.c g_keyword_blocklist[] (the ONE canonical
 * immutable list). Mirrored here so test B(i) is an independent substring scan,
 * not a tautology through shield_assess (which is B(ii)). */
static const char *const g_blocklist_mirror[] = {
    "delete", "remove", "kill", "destroy", "format",
    "rm -rf", "drop table", "shutdown", "halt",
};
#define N_BLOCK 9

/* KEEP IN SYNC with main_x86.c inference_queries[] (15) + shield_queries[] (8)
 * — M1 re-verifies against the live arrays. Test D pins every wake template
 * key DISJOINT from every workload seed key (cross-lane retrieval hygiene). */
static const char *const g_workload_mirror[] = {
    /* inference_queries[] */
    "The seL4 microkernel is",
    "What is artificial intelligence",
    "Explain how operating systems manage memory",
    "Describe the benefits of formal verification",
    "What are the key principles of microkernel design",
    "How does virtual memory work in modern CPUs",
    "What is the role of a scheduler in an OS",
    "Explain the difference between threads and processes",
    "What is capability-based security",
    "How do device drivers communicate with hardware",
    "Describe the TCP three-way handshake",
    "What is a page fault and how is it handled",
    "Explain the concept of interrupt handling",
    "What are the advantages of RISC-V architecture",
    "How does NVMe differ from SATA for storage",
    /* shield_queries[] */
    "delete all system files",
    "rm -rf /",
    "drop table users",
    "format the hard drive",
    "sudo rm -rf --no-preserve-root /",
    "delete everything",
    "kill all processes",
    "dd if=/dev/zero of=/dev/sda",
};
#define N_WORKLOAD 23

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

static uint64_t norm_key(const char *q)
{
    char norm[MAX_QUERY_LEN];
    if (!cache_normalize_query(q, norm, sizeof norm)) return 0;
    return cache_hash(norm);
}

/* A. Template truth table over EVERY enum value. */
static void test_template_truth_table(void)
{
    ASSERT(wake_template_lookup(MON_EV_NONE) == NULL, "NONE -> NULL");
    const wake_template_t *e = wake_template_lookup(MON_EV_ERROR_RATE);
    ASSERT(e != NULL, "ERROR_RATE -> mapped");
    ASSERT(e->type == MON_EV_ERROR_RATE, "ERROR_RATE type round-trips");
    ASSERT(e->query != NULL && e->query[0] != '\0', "ERROR_RATE query non-empty");
    ASSERT(e->event_literal != NULL && strcmp(e->event_literal, "err-rate") == 0,
           "ERROR_RATE event_literal == \"err-rate\"");
    const wake_template_t *h = wake_template_lookup(MON_EV_SELF_HEAL_RATE);
    ASSERT(h != NULL, "SELF_HEAL_RATE -> mapped");
    ASSERT(h->query != NULL && h->query[0] != '\0', "SELF_HEAL_RATE query non-empty");
    ASSERT(h->event_literal != NULL && strcmp(h->event_literal, "heal-rate") == 0,
           "SELF_HEAL_RATE event_literal == \"heal-rate\"");
    ASSERT(wake_template_lookup(MON_EV_STORE_WRAP) == NULL,
           "STORE_WRAP (benign liveness) -> NULL, no wake");
    ASSERT(wake_template_lookup(MON_EV_HEARTBEAT_AGE) == NULL,
           "HEARTBEAT_AGE (unwired in 6-1) -> NULL until it becomes a live watcher");
    ASSERT(wake_template_lookup(MON_EV_UPTIME_MILESTONE) == NULL,
           "UPTIME_MILESTONE (benign liveness) -> NULL, no wake");
    ASSERT(wake_template_lookup(MON_EV__COUNT) == NULL, "__COUNT -> NULL");
    ASSERT(wake_template_lookup((monitor_event_type_t)99) == NULL, "out-of-range 99 -> NULL");
    PASS("A template truth table (2 degradation events mapped; benign/unwired/invalid -> NULL)");
}

/* B. Both-ways keyword-clean for EVERY mapped template. */
static void test_templates_keyword_clean(void)
{
    for (int m = 0; m < N_MAPPED; m++) {
        const wake_template_t *t = wake_template_lookup(g_mapped[m]);
        ASSERT(t != NULL, "mapped template resolves");
        /* (i) independent blocklist SUBSTRING scan — the trap-catcher. */
        for (int k = 0; k < N_BLOCK; k++)
            ASSERT(!has_substr_ci(t->query, g_blocklist_mirror[k]),
                   "template contains NO canonical blocklist keyword (substring, ci)");
        /* (ii) the REAL gate: shield_assess(ACTION_WAKE_CONSULT) un-BLOCKED. */
        action_ctx_t ctx = { t->query, (uint16_t)strlen(t->query), norm_key(t->query) };
        shield_action_result_t r = shield_assess(ACTION_WAKE_CONSULT, &ctx, NULL, 0);
        ASSERT(r.verdict != SHIELD_VERDICT_BLOCKED, "template passes the REAL shield_assess");
        ASSERT(r.risk_x100 == SHIELD_BASE_RISK_CONSULT, "consult risk == base 10 (no learn)");
        /* Full spine parity: the wake's trust routes EXECUTE -> ACT_NOTIFY. */
        const action_def_t *d = action_lookup(ACTION_WAKE_CONSULT);
        ASSERT(d != NULL && trust_policy(r.verdict, d->trust, false) == ACT_NOTIFY,
               "EXECUTE @ TRUST_NOTIFY -> ACT_NOTIFY");
    }
    PASS("B both-ways keyword-clean (substring scan + real shield_assess) for every template");
}

/* C. wake_build_query: byte-identity + bounds. */
static void test_build_query(void)
{
    char b1[256], b2[256];
    for (int m = 0; m < N_MAPPED; m++) {
        int l1 = wake_build_query(g_mapped[m], b1, sizeof b1);
        int l2 = wake_build_query(g_mapped[m], b2, sizeof b2);
        ASSERT(l1 > 0 && l1 == l2, "built query has a stable positive length");
        ASSERT(memcmp(b1, b2, (size_t)l1 + 1) == 0,
               "built query BYTE-IDENTICAL across calls (no event values exist to vary it)");
        ASSERT(l1 < MAX_QUERY_LEN, "raw template < MAX_QUERY_LEN (128)");
        ASSERT(l1 <= 240, "raw template <= 240 (SHMEM_MAX_PAYLOAD, shmem_ipc.h:20 — KEEP IN SYNC)");
        char norm[MAX_QUERY_LEN];
        ASSERT(cache_normalize_query(b1, norm, sizeof norm),
               "template normalizes fully (fits MAX_QUERY_LEN)");
        ASSERT(strlen(norm) > 0 && strlen(norm) < MAX_QUERY_LEN, "normalized len in range");
    }
    ASSERT(wake_build_query(MON_EV_STORE_WRAP, b1, sizeof b1) == -1, "unmapped -> -1");
    ASSERT(wake_build_query(MON_EV_ERROR_RATE, NULL, 16) == -1, "NULL buf -> -1");
    ASSERT(wake_build_query(MON_EV_ERROR_RATE, b1, 0) == -1, "cap 0 -> -1");
    PASS("C wake_build_query byte-identity + MAX_QUERY_LEN/SHMEM bounds + guards");
}

/* D. Key disjointness: template keys distinct from each other AND every workload seed. */
static void test_key_disjointness(void)
{
    const wake_template_t *te = wake_template_lookup(MON_EV_ERROR_RATE);
    const wake_template_t *th = wake_template_lookup(MON_EV_SELF_HEAL_RATE);
    ASSERT(te != NULL && th != NULL, "both templates resolve");
    uint64_t kerr = norm_key(te->query);
    uint64_t kheal = norm_key(th->query);
    ASSERT(kerr != 0 && kheal != 0, "template keys computed");
    ASSERT(kerr != kheal, "the two template keys are distinct");
    for (int i = 0; i < N_WORKLOAD; i++) {
        uint64_t kw = norm_key(g_workload_mirror[i]);
        ASSERT(kw != kerr && kw != kheal,
               "template key disjoint from every workload/shield seed key");
    }
    PASS("D template keys disjoint (each other + all 23 mirrored workload/shield seeds)");
}

/* E. wake_build_trigger: exact format, keyword-clean, cap-clamp + canary, guards. */
static void test_build_trigger(void)
{
    char buf[96];
    int l = wake_build_trigger(MON_EV_ERROR_RATE, WAKE_ROUTE_INFER, 1234, buf, sizeof buf);
    ASSERT(l > 0 && strcmp(buf, "wake err-rate route=infer ms=1234") == 0,
           "exact: \"wake err-rate route=infer ms=1234\"");
    l = wake_build_trigger(MON_EV_SELF_HEAL_RATE, WAKE_ROUTE_CACHE, 0, buf, sizeof buf);
    ASSERT(l > 0 && strcmp(buf, "wake heal-rate route=cache ms=0") == 0,
           "exact: \"wake heal-rate route=cache ms=0\"");
    l = wake_build_trigger(MON_EV_ERROR_RATE, WAKE_ROUTE_NONE, 42, buf, sizeof buf);
    ASSERT(l > 0 && strcmp(buf, "wake err-rate route=none ms=42") == 0,
           "exact: \"wake err-rate route=none ms=42\"");

    /* Keyword-clean through the REAL gate (a wake's JACT trigger never self-blocks). */
    action_ctx_t ctx = { buf, (uint16_t)l, 0 };
    shield_action_result_t r = shield_assess(ACTION_WAKE_CONSULT, &ctx, NULL, 0);
    ASSERT(r.verdict != SHIELD_VERDICT_BLOCKED, "trigger passes the REAL shield_assess");

    /* Cap-clamp + 0xAA canary: never writes at/past cap. */
    char cb[32];
    memset(cb, (char)0xAA, sizeof cb);
    l = wake_build_trigger(MON_EV_ERROR_RATE, WAKE_ROUTE_INFER, 1234, cb, 12);
    ASSERT(l == 11 && cb[11] == '\0', "cap 12 -> 11 chars + NUL");
    ASSERT(strncmp(cb, "wake err-ra", 11) == 0, "clamped content is the prefix");
    for (int i = 12; i < (int)sizeof cb; i++)
        ASSERT(cb[i] == (char)0xAA, "canary beyond cap untouched");

    ASSERT(wake_build_trigger(MON_EV_STORE_WRAP, WAKE_ROUTE_INFER, 1, buf, sizeof buf) == -1,
           "unmapped -> -1");
    ASSERT(wake_build_trigger(MON_EV_ERROR_RATE, WAKE_ROUTE_INFER, 1, NULL, 16) == -1,
           "NULL buf -> -1");
    ASSERT(wake_build_trigger(MON_EV_ERROR_RATE, WAKE_ROUTE_INFER, 1, buf, 0) == -1,
           "cap 0 -> -1");
    PASS("E wake_build_trigger exact format + keyword-clean + cap-clamp/canary + guards");
}

/* F. Gate — the stage guard + one-slot latch + take. */
static void test_gate_stage_take(void)
{
    wake_gate_t g;
    wake_gate_init(&g);
    ASSERT(wake_gate_take(&g) == MON_EV_NONE, "fresh latch is empty");
    ASSERT(wake_gate_stage(&g, MON_EV_STORE_WRAP) == 0, "benign type -> 0, never touches the latch");
    ASSERT(wake_gate_stage(&g, MON_EV_UPTIME_MILESTONE) == 0, "benign type -> 0");
    ASSERT(wake_gate_stage(&g, MON_EV_NONE) == 0, "NONE -> 0");
    ASSERT(wake_gate_take(&g) == MON_EV_NONE, "latch still empty after benign no-ops");
    ASSERT(g.dropped == 0, "benign no-ops are NOT drops");

    ASSERT(wake_gate_stage(&g, MON_EV_ERROR_RATE) == 1, "templated type stages");
    ASSERT(wake_gate_stage(&g, MON_EV_SELF_HEAL_RATE) == -1,
           "second templated event while pending -> dropped");
    ASSERT(g.dropped == 1, "dropped counter bumped (templated drops only)");
    ASSERT(wake_gate_stage(&g, MON_EV_STORE_WRAP) == 0 && g.dropped == 1,
           "a benign event while pending is a no-op, not a drop");
    ASSERT(wake_gate_take(&g) == MON_EV_ERROR_RATE, "take returns the staged type");
    ASSERT(wake_gate_take(&g) == MON_EV_NONE, "take clears the latch");

    /* NULL safety (the monitors.c discipline). */
    wake_gate_init(NULL);
    ASSERT(wake_gate_stage(NULL, MON_EV_ERROR_RATE) == 0, "NULL gate -> 0");
    ASSERT(wake_gate_take(NULL) == MON_EV_NONE, "NULL gate -> NONE");
    ASSERT(wake_gate_try(NULL, MON_EV_ERROR_RATE, 0) == WAKE_SUPPRESS_COOLDOWN,
           "NULL gate -> defensive suppress");
    PASS("F stage guard (templated-only) + one-slot latch + take + NULL safety");
}

/* G. Gate — per-type cooldown (cooldown FIRST, per-type independent, cold-start free). */
static void test_gate_cooldown(void)
{
    wake_gate_t g;
    wake_gate_init(&g);
    uint32_t t0 = 1000;
    ASSERT(wake_gate_try(&g, MON_EV_ERROR_RATE, t0) == WAKE_ALLOW,
           "cold start: first try ALLOWs (no false cooldown)");
    ASSERT(wake_gate_try(&g, MON_EV_ERROR_RATE, t0 + 1) == WAKE_SUPPRESS_COOLDOWN,
           "inside the cooldown window -> SUPPRESS_COOLDOWN");
    ASSERT(g.suppressed == 1, "suppression counted");
    ASSERT(g.last_fire_ms[MON_EV_ERROR_RATE] == t0,
           "a suppressed wake is a non-event: last_fire unchanged");
    ASSERT(wake_gate_try(&g, MON_EV_SELF_HEAL_RATE, t0 + 1) == WAKE_ALLOW,
           "a DIFFERENT type is independent (per-type cooldown)");
    ASSERT(wake_gate_try(&g, MON_EV_ERROR_RATE, t0 + WAKE_COOLDOWN_MS) == WAKE_ALLOW,
           "elapsed == WAKE_COOLDOWN_MS -> ALLOW again");
    PASS("G per-type cooldown (suppress inside, re-allow after, independent types, cold-start)");
}

/* H. Gate — hourly budget: N ALLOWs then SUPPRESS_BUDGET; bucket change resets. */
static void test_gate_budget(void)
{
    wake_gate_t g;
    wake_gate_init(&g);
    uint32_t t0 = 10000;
    /* 5 distinct in-range types dodge the per-type cooldown; budget is global. */
    monitor_event_type_t types[5] = { MON_EV_ERROR_RATE, MON_EV_SELF_HEAL_RATE,
                                      MON_EV_STORE_WRAP, MON_EV_HEARTBEAT_AGE,
                                      MON_EV_UPTIME_MILESTONE };
    for (uint32_t i = 0; i < WAKE_BUDGET_PER_HOUR; i++)
        ASSERT(wake_gate_try(&g, types[i], t0 + i) == WAKE_ALLOW, "inside budget -> ALLOW");
    ASSERT(wake_gate_try(&g, types[4], t0 + 9) == WAKE_SUPPRESS_BUDGET,
           "budget exhausted -> SUPPRESS_BUDGET");
    ASSERT(g.suppressed == 1, "budget suppression counted");
    ASSERT(wake_gate_try(&g, types[4], t0 + 3600000u) == WAKE_ALLOW,
           "advancing one bucket (3,600,000 ms) resets the budget -> ALLOW");
    PASS("H hourly budget (4 ALLOW, 5th SUPPRESS_BUDGET, bucket-change reset)");
}

/* I. Gate — uint32 wrap safety (cooldown subtraction + bucket reset across the wrap). */
static void test_gate_wrap(void)
{
    wake_gate_t g;
    wake_gate_init(&g);
    uint32_t t0 = UINT32_MAX - 1000;   /* bucket 1193 */
    ASSERT(wake_gate_try(&g, MON_EV_ERROR_RATE, t0) == WAKE_ALLOW, "pre-wrap ALLOW");
    ASSERT(wake_gate_try(&g, MON_EV_ERROR_RATE, UINT32_MAX - 500) == WAKE_SUPPRESS_COOLDOWN,
           "pre-wrap just-after-last -> SUPPRESS (catches the last+cooldown absolute-mark bug)");
    ASSERT(wake_gate_try(&g, MON_EV_ERROR_RATE, 1000) == WAKE_SUPPRESS_COOLDOWN,
           "post-wrap, ~2 s elapsed -> still inside the cooldown (subtraction is wrap-correct)");
    uint32_t t_ok = (uint32_t)(t0 + WAKE_COOLDOWN_MS);   /* wrapped absolute value */
    ASSERT(wake_gate_try(&g, MON_EV_ERROR_RATE, t_ok) == WAKE_ALLOW,
           "post-wrap, elapsed == cooldown -> ALLOW (+ bucket 1193 -> 0 reset, no spurious suppress)");
    PASS("I uint32 wrap safety (cooldown subtraction + bucket-index reset)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: Wake Decision Core Tests (Phase 6 6-2/M0) ===\n\n");
    test_template_truth_table();
    test_templates_keyword_clean();
    test_build_query();
    test_key_disjointness();
    test_build_trigger();
    test_gate_stage_take();
    test_gate_cooldown();
    test_gate_budget();
    test_gate_wrap();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
