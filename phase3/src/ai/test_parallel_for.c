/*
 * JARVIS AI-OS Phase 3 — jarvis_parallel_for ABI / work-stealing correctness test.
 * Host (pthread) proxy for the M3 seL4 threadpool — same algorithm, different primitives.
 * CI runs this at JARVIS_THREADS=1/2/4/8 to prove the result is worker-count-independent.
 *
 * Design: phase4/docs/PHASE_4_M3_THREADPOOL_DESIGN.md (the seL4 backend ports this exact
 * work-stealing algorithm; only thread-create + sync primitives change).
 */
#define JARVIS_PTHREAD 1
#include "threadpool.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { if(!(cond)){ printf("FAIL: %s\n", (msg)); g_fail=1; } } while(0)

typedef struct { int64_t base; int mult; int *out; int n; } fill_ctx_t;
_Static_assert(sizeof(fill_ctx_t) <= 64, "ctx must fit threadpool ctx_buf[64]");

static void fill_fn(int idx, void *p) {
    fill_ctx_t *c = (fill_ctx_t *)p;
    if (idx < 0 || idx >= c->n) return;          /* bounds guard (stale end) */
    c->out[idx] = (int)(c->base + (int64_t)idx * c->mult);
}

int main(void) {
    int T = jarvis_threads();
    printf("=== parallel_for ABI tests (threads=%d) ===\n", T);

    const int N = 50000;
    int *out = (int *)malloc((size_t)N * sizeof(int));
    CHECK(out != NULL, "alloc");
    if (!out) return 1;

    /* Test 1+2: ctx-copy fill, then reduction == serial closed form (exact int64) */
    for (int i = 0; i < N; i++) out[i] = -1;
    fill_ctx_t c1 = { .base = 1000, .mult = 1, .out = out, .n = N };
    jarvis_parallel_for(0, N, fill_fn, &c1, sizeof(c1));
    int64_t sum = 0, expect = 0;
    for (int i = 0; i < N; i++) { sum += out[i]; expect += 1000 + i; }
    CHECK(sum == expect, "reduction == serial (round 1)");
    CHECK(out[0]==1000 && out[1]==1001 && out[N-1]==1000+(N-1), "ctx fill values");

    /* Test 3: second dispatch, different ctx -> no stale-ctx bleed between rounds */
    for (int i = 0; i < N; i++) out[i] = -1;
    fill_ctx_t c2 = { .base = 0, .mult = 2, .out = out, .n = N };
    jarvis_parallel_for(0, N, fill_fn, &c2, sizeof(c2));
    int64_t sum2 = 0, expect2 = 0;
    for (int i = 0; i < N; i++) { sum2 += out[i]; expect2 += (int64_t)2 * i; }
    CHECK(sum2 == expect2, "reduction == serial (round 2, new ctx)");
    CHECK(out[10]==20 && out[N-1]==2*(N-1), "round-2 ctx values (no bleed)");

    /* Test 4: edge ranges are no-ops / minimal */
    out[3] = 777; out[4] = 888;
    jarvis_parallel_for(5, 5, fill_fn, &c2, sizeof(c2));   /* empty */
    jarvis_parallel_for(3, 3, fill_fn, &c2, sizeof(c2));   /* empty */
    CHECK(out[3]==777 && out[4]==888, "empty range is a no-op");
    out[0] = -1;
    jarvis_parallel_for(0, 1, fill_fn, &c2, sizeof(c2));   /* single index */
    CHECK(out[0]==0, "single-index range");

    /* Test 5: full coverage — every index written exactly once, none missed/doubled */
    for (int i = 0; i < N; i++) out[i] = -1;
    jarvis_parallel_for(0, N, fill_fn, &c1, sizeof(c1));
    int missed = 0;
    for (int i = 0; i < N; i++) if (out[i] != 1000 + i) missed++;
    CHECK(missed == 0, "full coverage, no missed/double indices");

    /* Test 6: 100 back-to-back dispatches, each a distinct ctx — stresses the cross-dispatch
     * join boundary (the locus of the fixed race). Every index must match every round. */
    for (int round = 0; round < 100 && !g_fail; round++) {
        for (int i = 0; i < N; i++) out[i] = -1;
        fill_ctx_t cr = { .base = (int64_t)round * 7, .mult = (round % 3) + 1, .out = out, .n = N };
        jarvis_parallel_for(0, N, fill_fn, &cr, sizeof(cr));
        for (int i = 0; i < N; i++) {
            int want = (int)(cr.base + (int64_t)i * cr.mult);
            if (out[i] != want) { printf("FAIL: round %d idx %d got %d want %d\n", round, i, out[i], want); g_fail = 1; break; }
        }
    }

    jarvis_threadpool_shutdown();
    free(out);
    if (g_fail) { printf("RESULT: FAIL\n"); return 1; }
    printf("PASS (threads=%d)\n", T);
    return 0;
}
