/*
 * test_route.c — host unit + accuracy test for the control-IN QUERY ROUTER (6-6/B M0).
 *
 * Proves route_classify (1) routes the DEV corpus + hand-written cases to the
 * right handler, (2) honors the CONSERVATIVE default (ambiguous / load-generator
 * counters -> ROUTE_INFER, never a wrong fact), (3) is crash/UB-free with the
 * result-struct invariant intact for degenerate bytes, (4) renders SYSTEM-FACTS
 * ONLY from the host-whitelisted `sysfacts_t` (a structural assertion pins the
 * whitelist), and (5) scores >=95% on the KEYWORD-BLIND HELDOUT split — the
 * done-when number (HARD-FAIL below 95%).
 *
 * ANTI-CIRCULARITY: route.c's keyword tables were tuned against ROUTING_DEV +
 * general knowledge ONLY. HELDOUT is the honest number, measured once; NEVER
 * edit HELDOUT (or reverse-engineer its phrasings into route.c) to pass.
 *
 * HOST + CI ONLY. Nothing links into the box; route.c is self-contained (no
 * shield / decision_cache dep).
 *
 * Build (matches the CI step):
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *     phase3/src/ai/test_route.c phase3/src/ai/route.c -o /tmp/test_route && /tmp/test_route
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "route.h"           /* included BEFORE routing_suite.h so the ROUTE_ / SF_ */
#include "routing_suite.h"   /* enum constants resolve for the suite initialisers   */

/* ---- the §4d whitelist enforcement: pin sysfacts_t to EXACTLY the allowed
 * fields, so a future field add is a conscious, reviewed edit (never an
 * honor-system read of arbitrary box state). Values verified on gcc x86-64. ---- */
_Static_assert(sizeof(sysfacts_t) == 52,                     "sysfacts_t whitelist size pinned");
_Static_assert(offsetof(sysfacts_t, uptime_ms)  == 0,        "uptime_ms offset pinned");
_Static_assert(offsetof(sysfacts_t, boot_id)    == 4,        "boot_id offset pinned");
_Static_assert(offsetof(sysfacts_t, num_nodes)  == 8,        "num_nodes offset pinned");
_Static_assert(offsetof(sysfacts_t, model_name) == 9,        "model_name offset pinned");
_Static_assert(offsetof(sysfacts_t, healthy)    == 49,       "healthy offset pinned");
_Static_assert(sizeof(((sysfacts_t *)0)->model_name) == 40,  "model_name width pinned");

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                        \
    do {                                                                       \
        if (cond) { g_pass++; }                                                \
        else { g_fail++; printf("  FAIL: %s\n", (msg)); }                      \
    } while (0)

/* The route_result_t invariant: cls in {0,1,2}; field valid iff cls==SYSFACTS;
 * matched_pattern >= 0 iff a rule fired (not INFER); INFER carries SF_NONE/-1. */
static int result_invariant_ok(const route_result_t *r)
{
    if (r->cls != ROUTE_INFER && r->cls != ROUTE_SYSFACTS && r->cls != ROUTE_DECLINE)
        return 0;
    if (r->cls == ROUTE_INFER)
        return r->field == SF_NONE && r->matched_pattern == -1;
    if (r->cls == ROUTE_SYSFACTS)
        return r->field > SF_NONE && r->field < SF__COUNT && r->matched_pattern >= 0;
    /* ROUTE_DECLINE */
    return r->field == SF_NONE && r->matched_pattern >= 0;
}

/* ---- T1: every DEV case routes correctly (the tuning set -> 100%) ---- */
static void t1_dev_cases(void)
{
    printf("T1: DEV corpus (%zu) routes to the labeled handler\n", ROUTING_DEV_N);
    for (size_t i = 0; i < ROUTING_DEV_N; i++) {
        const routing_case_t *c = &ROUTING_DEV[i];
        route_result_t r;
        route_classify(c->query, strlen(c->query), &r);
        int ok = ((int)r.cls == c->expect_cls) &&
                 (c->expect_cls != ROUTE_SYSFACTS || (int)r.field == c->expect_field) &&
                 result_invariant_ok(&r);
        if (!ok) {
            printf("  FAIL: \"%s\" exp(cls=%d fld=%d) got(cls=%d fld=%d pat=%d)\n",
                   c->query, c->expect_cls, c->expect_field,
                   (int)r.cls, (int)r.field, (int)r.matched_pattern);
            g_fail++;
        } else {
            g_pass++;
        }
    }
}

/* ---- T2: >=10 INFER fallbacks stay INFER ---- */
static void t2_infer_fallbacks(void)
{
    static const char *const INFER_Q[] = {
        "what is a page fault?",
        "explain paging in one line",
        "what is virtual memory?",             /* has "memory" but no usage word */
        "what does the seL4 capability system provide",
        "what is a mutex?",
        "how does an mmu work?",
        "tell me a joke",
        "what's the capital of France?",
        "what is a system call?",
        "explain deadlock in simple terms",
        "what does DMA stand for?",
        "what is a b-tree?",
        "explain the difference between a process and a thread", /* "thread" but no count ctx */
        "what is memory-mapped io?",           /* has "memory" but no usage word */
    };
    const int N = (int)(sizeof INFER_Q / sizeof INFER_Q[0]);
    printf("T2: INFER fallbacks (%d) -> ROUTE_INFER\n", N);
    for (int i = 0; i < N; i++) {
        route_result_t r;
        route_class_t v = route_classify(INFER_Q[i], strlen(INFER_Q[i]), &r);
        CHECK(v == ROUTE_INFER && r.field == SF_NONE && r.matched_pattern == -1, INFER_Q[i]);
    }
}

/* ---- T3: DECLINE metrics + the CONSERVATIVE boundary ---- */
static void t3_decline_and_boundary(void)
{
    static const char *const DECLINE_Q[] = {
        "what's your cpu usage?",
        "how busy are you right now?",
        "what's the load average?",
        "how much memory are you using?",   /* memory + usage word -> DECLINE */
        "how much ram is in use?",
        "how much disk space is left?",
        "how much storage do you have free?",
        "how hot are you running?",
        "what's your temperature?",
        "what time is it?",
    };
    const int N = (int)(sizeof DECLINE_Q / sizeof DECLINE_Q[0]);
    printf("T3: DECLINE metrics (%d) -> ROUTE_DECLINE + conservative boundary\n", N);
    for (int i = 0; i < N; i++) {
        route_result_t r;
        route_class_t v = route_classify(DECLINE_Q[i], strlen(DECLINE_Q[i]), &r);
        CHECK(v == ROUTE_DECLINE && r.field == SF_NONE && r.matched_pattern >= 0, DECLINE_Q[i]);
    }

    /* The load-generator counters are the PRNG workload's, NOT the human's ->
     * ambiguous -> ROUTE_INFER (fail toward the model, never a wrong fact). */
    static const char *const AMBIG_Q[] = {
        "how many queries have you answered?",
        "how many questions have you handled so far?",
        "how many queries did you process this hour?",
    };
    const int M = (int)(sizeof AMBIG_Q / sizeof AMBIG_Q[0]);
    for (int i = 0; i < M; i++) {
        route_result_t r;
        route_class_t v = route_classify(AMBIG_Q[i], strlen(AMBIG_Q[i]), &r);
        CHECK(v == ROUTE_INFER, AMBIG_Q[i]);
    }
    /* "worker ids" asks for identifiers, not the count -> INFER (no-id guard). */
    {
        route_result_t r;
        CHECK(route_classify("can you show your worker ids?", 28, &r) == ROUTE_INFER,
              "worker ids -> INFER (identifiers, not count)");
    }
}

/* ---- T4: robustness — degenerate input never crashes / never OOBs ---- */
static void t4_robustness(void)
{
    printf("T4: robustness (NULL / len0 / all-0xFF / embedded NUL / control bytes / out==NULL)\n");
    route_result_t r;

    CHECK(route_classify(NULL, 0, &r) == ROUTE_INFER && result_invariant_ok(&r), "NULL query -> INFER");
    CHECK(route_classify(NULL, 10, &r) == ROUTE_INFER, "NULL query, nonzero len -> INFER");
    CHECK(route_classify("x", 0, &r) == ROUTE_INFER, "len==0 -> INFER");
    /* out == NULL must still return a verdict (no deref). */
    CHECK(route_classify("what model are you?", 19, NULL) == ROUTE_SYSFACTS,
          "out==NULL still returns the verdict");

    /* 200 B all-0xFF: every byte non-alnum -> normalizes to empty -> INFER, no OOB. */
    char ff[200];
    memset(ff, (int)0xFF, sizeof ff);
    CHECK(route_classify(ff, sizeof ff, &r) == ROUTE_INFER && result_invariant_ok(&r),
          "200 B 0xFF -> INFER, no OOB");

    /* Embedded NUL splits words but does not terminate the scan (length-carried):
     * "what model\0are you" normalizes to "what model are you" -> SYSFACTS/MODEL. */
    static const char embnul[] = { 'w','h','a','t',' ','m','o','d','e','l','\0',
                                   'a','r','e',' ','y','o','u' };
    CHECK(route_classify(embnul, sizeof embnul, &r) == ROUTE_SYSFACTS && r.field == SF_MODEL,
          "embedded NUL between words still classifies (length-carried, not strlen'd)");

    /* Control-byte run -> collapses away -> INFER, no OOB. */
    static const char ctrl[] = { 0x01, 0x02, 0x03, 0x1F, 0x7F, 0x00, 0x09 };
    CHECK(route_classify(ctrl, sizeof ctrl, &r) == ROUTE_INFER, "control bytes -> INFER");

    /* An arbitrary-length non-terminated buffer keeps the invariant. */
    char buf[257];
    for (int i = 0; i < (int)sizeof buf; i++) buf[i] = (char)(i & 0x7F);
    CHECK(result_invariant_ok((route_classify(buf, sizeof buf, &r), &r)),
          "arbitrary 257 B -> invariant intact");
}

/* ---- T5: sysfacts_answer renders ONLY from the whitelisted facts ---- */
static void t5_sysfacts_answer(void)
{
    printf("T5: sysfacts_answer renders from the whitelist; clamps; SF_NONE -> 0\n");
    sysfacts_t f;
    memset(&f, 0, sizeof f);
    f.uptime_ms = 5000;     /* 5 seconds */
    f.boot_id   = 27;
    f.num_nodes = 6;
    memcpy(f.model_name, "Gemma 4 E2B", 12);   /* NUL-terminated within the array */
    f.healthy   = 1;

    char out[64];
    int n;

    n = sysfacts_answer(SF_UPTIME, &f, out, sizeof out);
    CHECK(n == (int)strlen(out) && strcmp(out, "up 5 seconds") == 0, "SF_UPTIME render");

    n = sysfacts_answer(SF_BOOT_ID, &f, out, sizeof out);
    CHECK(strcmp(out, "boot 27") == 0, "SF_BOOT_ID render");

    n = sysfacts_answer(SF_MODEL, &f, out, sizeof out);
    CHECK(strcmp(out, "Gemma 4 E2B") == 0, "SF_MODEL render");

    n = sysfacts_answer(SF_NODES, &f, out, sizeof out);
    CHECK(strcmp(out, "6 workers") == 0, "SF_NODES render");

    n = sysfacts_answer(SF_HEALTH, &f, out, sizeof out);
    CHECK(strcmp(out, "healthy, no errors") == 0, "SF_HEALTH healthy render");
    f.healthy = 0;
    n = sysfacts_answer(SF_HEALTH, &f, out, sizeof out);
    CHECK(strcmp(out, "errors have occurred") == 0, "SF_HEALTH unhealthy render");

    /* SF_NONE / out-of-range -> 0, empty out. */
    out[0] = 'x';
    CHECK(sysfacts_answer(SF_NONE, &f, out, sizeof out) == 0 && out[0] == '\0', "SF_NONE -> 0");
    out[0] = 'x';
    CHECK(sysfacts_answer((sysfact_field_t)999, &f, out, sizeof out) == 0 && out[0] == '\0',
          "out-of-range field -> 0");

    /* NULL guards. */
    CHECK(sysfacts_answer(SF_UPTIME, &f, NULL, 10) == 0, "NULL out -> 0");
    CHECK(sysfacts_answer(SF_UPTIME, NULL, out, sizeof out) == 0, "NULL facts -> 0");
    CHECK(sysfacts_answer(SF_UPTIME, &f, out, 0) == 0, "cap==0 -> 0");

    /* Over-long render clamps to cap, stays NUL-terminated, 0xAA canary intact. */
    {
        sysfacts_t big;
        memset(&big, 0, sizeof big);
        memset(big.model_name, 'M', sizeof big.model_name);  /* 40 chars, NON-NUL-terminated */
        char cbuf[16];
        memset(cbuf, (int)0xAA, sizeof cbuf);
        int cn = sysfacts_answer(SF_MODEL, &big, cbuf, 8);    /* cap 8 */
        CHECK(cn < 8, "over-long render clamped to cap");
        CHECK(cbuf[cn] == '\0', "clamped render NUL-terminated within cap");
        CHECK(cbuf[8] == (char)0xAA, "0xAA canary past cap intact (no OOB write)");
    }
}

/* ---- accuracy harness: run a suite, print accuracy + confusion ---- */
static void run_suite(const char *name, const routing_case_t *suite, size_t n,
                      int *out_correct, int *out_total)
{
    int correct = 0;
    int conf[3][3] = { { 0 } };   /* [expected_cls][predicted_cls] */
    int infer_misroute = 0;
    for (size_t i = 0; i < n; i++) {
        route_result_t r;
        route_classify(suite[i].query, strlen(suite[i].query), &r);
        int ok = ((int)r.cls == suite[i].expect_cls) &&
                 (suite[i].expect_cls != ROUTE_SYSFACTS || (int)r.field == suite[i].expect_field);
        if (ok) correct++;
        else
            printf("  MISS [%s] \"%s\" exp(cls=%d fld=%d) got(cls=%d fld=%d)\n",
                   name, suite[i].query, suite[i].expect_cls, suite[i].expect_field,
                   (int)r.cls, (int)r.field);
        conf[suite[i].expect_cls][r.cls]++;
        if (suite[i].expect_cls == ROUTE_INFER && r.cls != ROUTE_INFER) infer_misroute++;
    }
    printf("%s accuracy: %d/%zu = %.2f%%\n", name, correct, n, 100.0 * (double)correct / (double)n);
    static const char *const cn[3] = { "INFER", "SYSFACTS", "DECLINE" };
    printf("  confusion [exp\\pred]   INFER / SYSFACTS / DECLINE\n");
    for (int e = 0; e < 3; e++)
        printf("    %-9s ->        %d / %d / %d\n", cn[e], conf[e][0], conf[e][1], conf[e][2]);
    printf("  INFER misroutes (INFER -> SYSFACTS/DECLINE): %d\n", infer_misroute);
    *out_correct = correct;
    *out_total = (int)n;
}

int main(void)
{
    printf("=== test_route (6-6/B M0 control-IN QUERY ROUTER) ===\n");
    t1_dev_cases();
    t2_infer_fallbacks();
    t3_decline_and_boundary();
    t4_robustness();
    t5_sysfacts_answer();

    printf("\n--- ACCURACY (the done-when) ---\n");
    int dc = 0, dt = 0, hc = 0, ht = 0;
    run_suite("DEV",     ROUTING_DEV,     ROUTING_DEV_N,     &dc, &dt);
    run_suite("HELDOUT", ROUTING_HELDOUT, ROUTING_HELDOUT_N, &hc, &ht);

    /* HARD gate: HELDOUT (the keyword-blind held-out split) MUST be >= 95%. */
    int heldout_pass = (hc * 100 >= ht * 95);
    printf("\nHELDOUT >= 95%%: %s (%d/%d)\n", heldout_pass ? "PASS" : "FAIL", hc, ht);
    CHECK(heldout_pass, "HELDOUT accuracy must be >= 95% (done-when)");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) {
        printf("TEST ROUTE: FAIL\n");
        return 1;
    }
    printf("TEST ROUTE: PASS\n");
    return 0;
}
