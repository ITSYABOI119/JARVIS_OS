/*
 * JARVIS AI-OS Phase 3 — Thread Pool Tests
 *
 * Validates that jarvis_parallel_for executes every index exactly once.
 */

#define JARVIS_PTHREAD 1

#include "threadpool.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

static _Atomic int g_hits;
static _Atomic int *g_seen;

static void hit_fn(int idx, void *ctx) {
    (void)ctx;
    atomic_fetch_add_explicit(&g_hits, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_seen[idx], 1, memory_order_relaxed);
}

int main(void) {
    printf("=== JARVIS threadpool tests ===\n\n");

    const int N = 100000;
    g_seen = (_Atomic int *)malloc((size_t)N * sizeof(*g_seen));
    if (!g_seen) {
        printf("FAIL: alloc\n");
        return 1;
    }
    for (int i = 0; i < N; i++)
        atomic_init(&g_seen[i], 0);

    atomic_init(&g_hits, 0);
    jarvis_parallel_for(0, N, hit_fn, NULL, 0);

    int hits = atomic_load_explicit(&g_hits, memory_order_relaxed);
    if (hits != N) {
        printf("FAIL: hits=%d expected=%d\n", hits, N);
        free(g_seen);
        return 1;
    }

    for (int i = 0; i < N; i++) {
        int c = atomic_load_explicit(&g_seen[i], memory_order_relaxed);
        if (c != 1) {
            printf("FAIL: idx=%d count=%d expected=1\n", i, c);
            free(g_seen);
            return 1;
        }
    }

    jarvis_threadpool_shutdown();
    free(g_seen);

    printf("PASS\n");
    return 0;
}

