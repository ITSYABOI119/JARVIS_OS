/* JARVIS AI-OS — HOST unit test for the DEPLOYED seL4-native work-stealing pool
 * (phase3/src/ai/threadpool_sel4.c, the M3 pool that runs the Gemma 4 E2B forward at
 * 5.46 tok/s @ NUM_NODES=6 on the box). The seL4 Notification primitives are stubbed
 * with POSIX semaphores (test_sel4_stubs/sel4/sel4.h) so the gen-publish + active-counter
 * join + per-worker wake logic runs on a normal host under ThreadSanitizer. The production
 * threadpool_sel4.c is compiled UNCHANGED — this test only links against it.
 *
 * Build (see .github/workflows/ci.yml):
 *   gcc -Wall -Werror -O1 -std=c11 -fsanitize=thread -D_POSIX_C_SOURCE=200809L \
 *     -DJARVIS_SEL4_SMP -I phase3/src/ai -I phase3/src/ai/test_sel4_stubs \
 *     phase3/src/ai/test_threadpool_sel4.c phase3/src/ai/threadpool_sel4.c \
 *     -lpthread -o /tmp/test_threadpool_sel4_tsan && /tmp/test_threadpool_sel4_tsan
 *
 * Coverage: T1 exactly-once visit, T2 serial-equivalent reduction, T3 cross-dispatch
 * state (the race the pthread backend once bit — must stay green under TSan), T4 serial
 * fallback (<256), T5 ctx-copy correctness; swept over n_threads {1,2,4,6,8}.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "threadpool.h"

#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXN 1024

/* ---- shared test state (each index touched by exactly one worker per dispatch) ---- */
static atomic_int  g_visited[MAXN];
static atomic_long g_sum;
static int         g_out[MAXN];

static void reset_visited(int n) {
    for (int i = 0; i < n; i++)
        atomic_store_explicit(&g_visited[i], 0, memory_order_relaxed);
}

/* T1/T3: count every visited index. */
static void fn_visit(int idx, void *ctx) {
    (void)ctx;
    atomic_fetch_add_explicit(&g_visited[idx], 1, memory_order_relaxed);
}

/* T2/T3: reduce idx into a ctx-carried shared atomic accumulator (the only way to do a
 * "per-thread partial" with this API is a shared atomic — workers share one ctx copy). */
typedef struct { atomic_long *acc; } sum_ctx_t;
static void fn_sum(int idx, void *ctx) {
    sum_ctx_t *c = (sum_ctx_t *)ctx;
    atomic_fetch_add_explicit(c->acc, (long)idx, memory_order_relaxed);
}

/* T5: prove the 64-byte ctx memcpy reaches every worker intact. */
typedef struct { int base; int mul; int *out; } map_ctx_t;
static void fn_map(int idx, void *ctx) {
    map_ctx_t *c = (map_ctx_t *)ctx;
    c->out[idx] = c->base + idx * c->mul;   /* each idx written exactly once */
}

/* ---- a worker pthread runs the PRODUCTION entry point (arg = its wake cptr) ---- */
static void *worker_thread_main(void *arg) {
    jarvis_sel4_worker_entry(arg, NULL, NULL);   /* arg0 = wake cptr; ipc_buf unused on host */
    return NULL;
}

/* ---- per-n_threads fixture: N-1 wake semaphores + 1 done semaphore + worker pthreads ---- */
typedef struct {
    int       n_workers;                       /* = n_threads - 1 */
    sem_t     done_sem;
    sem_t     wake_sem[JARVIS_MAX_WORKERS];     /* [0 .. n_workers-1] */
    seL4_CPtr wake_cptr[JARVIS_MAX_WORKERS];
    pthread_t worker[JARVIS_MAX_WORKERS];
} pool_fixture_t;

static void pool_up(pool_fixture_t *p, int n_threads) {
    memset(p, 0, sizeof(*p));
    p->n_workers = n_threads - 1;
    sem_init(&p->done_sem, 0, 0);
    seL4_CPtr done_cptr = (seL4_CPtr)(uintptr_t)&p->done_sem;
    for (int k = 0; k < p->n_workers; k++) {
        sem_init(&p->wake_sem[k], 0, 0);
        p->wake_cptr[k] = (seL4_CPtr)(uintptr_t)&p->wake_sem[k];
    }
    /* pool_init maps wake[k] -> g_pool.wake[k+1]; the dispatcher signals g_pool.wake[1..n] */
    jarvis_sel4_pool_init(n_threads, done_cptr, p->wake_cptr, p->n_workers);
    for (int k = 0; k < p->n_workers; k++)
        pthread_create(&p->worker[k], NULL, worker_thread_main,
                       (void *)(uintptr_t)p->wake_cptr[k]);
}

static void pool_down(pool_fixture_t *p) {
    jarvis_threadpool_shutdown();               /* quiescent: all dispatches have completed */
    for (int k = 0; k < p->n_workers; k++) pthread_join(p->worker[k], NULL);
    sem_destroy(&p->done_sem);
    for (int k = 0; k < p->n_workers; k++) sem_destroy(&p->wake_sem[k]);
}

/* ---- test cases (each returns 0 on PASS, 1 on FAIL) ---- */

static int check_visited_once(int n, const char *tag, int nthreads) {
    for (int i = 0; i < n; i++) {
        int v = atomic_load_explicit(&g_visited[i], memory_order_relaxed);
        if (v != 1) {
            printf("  FAIL %s (nthreads=%d): visited[%d]=%d, want 1\n", tag, nthreads, i, v);
            return 1;
        }
    }
    return 0;
}

static int t1_exactly_once(int nthreads) {
    const int N = 300;                          /* >= 256 -> threaded when nthreads>1 */
    reset_visited(N);
    jarvis_parallel_for(0, N, fn_visit, NULL, 0);
    return check_visited_once(N, "T1", nthreads);
}

static int t2_reduction(int nthreads) {
    const int N = 300;
    atomic_store_explicit(&g_sum, 0, memory_order_relaxed);
    sum_ctx_t c = { &g_sum };
    jarvis_parallel_for(0, N, fn_sum, &c, sizeof c);
    long got = atomic_load_explicit(&g_sum, memory_order_relaxed);
    long want = (long)N * (N - 1) / 2;          /* 44850 */
    if (got != want) {
        printf("  FAIL T2 (nthreads=%d): sum=%ld, want %ld\n", nthreads, got, want);
        return 1;
    }
    return 0;
}

static int t3_cross_dispatch(int nthreads) {
    static const int sizes[3] = { 300, 512, 1000 };
    for (int it = 0; it < 500; it++) {
        int N = sizes[it % 3];                  /* all >= 256 -> threaded reuse stress */
        reset_visited(N);
        jarvis_parallel_for(0, N, fn_visit, NULL, 0);
        if (check_visited_once(N, "T3-visit", nthreads)) {
            printf("  FAIL T3 (nthreads=%d): iter=%d N=%d\n", nthreads, it, N);
            return 1;
        }
        atomic_store_explicit(&g_sum, 0, memory_order_relaxed);
        sum_ctx_t c = { &g_sum };
        jarvis_parallel_for(0, N, fn_sum, &c, sizeof c);
        long got = atomic_load_explicit(&g_sum, memory_order_relaxed);
        long want = (long)N * (N - 1) / 2;
        if (got != want) {
            printf("  FAIL T3 (nthreads=%d): iter=%d N=%d sum=%ld want %ld\n",
                   nthreads, it, N, got, want);
            return 1;
        }
    }
    return 0;
}

static int t4_serial_fallback(int nthreads) {
    const int N = 100;                          /* < 256 -> serial path regardless of nthreads */
    reset_visited(N);
    jarvis_parallel_for(0, N, fn_visit, NULL, 0);
    if (check_visited_once(N, "T4", nthreads)) return 1;
    atomic_store_explicit(&g_sum, 0, memory_order_relaxed);
    sum_ctx_t c = { &g_sum };
    jarvis_parallel_for(0, N, fn_sum, &c, sizeof c);
    long got = atomic_load_explicit(&g_sum, memory_order_relaxed);
    if (got != 4950) {                          /* 100*99/2 */
        printf("  FAIL T4 (nthreads=%d): sum=%ld, want 4950\n", nthreads, got);
        return 1;
    }
    return 0;
}

static int t5_ctx_copy(int nthreads) {
    const int N = 300;
    const int base = 7, mul = 3;
    for (int i = 0; i < N; i++) g_out[i] = -1;
    map_ctx_t c = { base, mul, g_out };
    jarvis_parallel_for(0, N, fn_map, &c, sizeof c);
    for (int i = 0; i < N; i++) {
        if (g_out[i] != base + i * mul) {
            printf("  FAIL T5 (nthreads=%d): out[%d]=%d, want %d\n",
                   nthreads, i, g_out[i], base + i * mul);
            return 1;
        }
    }
    return 0;
}

int main(void) {
    const int thread_counts[] = { 1, 2, 4, 6, 8 };
    const int n_counts = (int)(sizeof(thread_counts) / sizeof(thread_counts[0]));
    int total = 0, passed = 0;

    for (int ti = 0; ti < n_counts; ti++) {
        int nt = thread_counts[ti];
        pool_fixture_t p;
        pool_up(&p, nt);

        if (jarvis_threads() != nt) {
            printf("  FAIL setup (nthreads=%d): jarvis_threads()=%d\n", nt, jarvis_threads());
        }

        struct { const char *name; int (*run)(int); } cases[] = {
            { "T1 exactly-once",     t1_exactly_once },
            { "T2 reduction",        t2_reduction },
            { "T3 cross-dispatch",   t3_cross_dispatch },
            { "T4 serial-fallback",  t4_serial_fallback },
            { "T5 ctx-copy",         t5_ctx_copy },
        };
        int n_cases = (int)(sizeof(cases) / sizeof(cases[0]));
        for (int ci = 0; ci < n_cases; ci++) {
            total++;
            int fail = cases[ci].run(nt);
            if (!fail) {
                passed++;
                printf("  PASS %-20s (nthreads=%d)\n", cases[ci].name, nt);
            } else {
                printf("  >>>> FAILED %-15s (nthreads=%d)\n", cases[ci].name, nt);
            }
        }

        pool_down(&p);
    }

    printf("[test_threadpool_sel4] %d/%d PASS\n", passed, total);
    return (passed == total) ? 0 : 1;
}
