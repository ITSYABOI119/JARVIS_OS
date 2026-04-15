/*
 * JARVIS AI-OS Phase 3 — Minimal Thread Pool (native Linux)
 *
 * This is intentionally tiny: a global worker pool + a single parallel-for primitive.
 * Designed for coarse-grained, row-parallel workloads (matmul rows, head loops).
 *
 * Compile with:
 *   -DJARVIS_PTHREAD=1 -pthread
 *
 * Pure C11.
 */

#ifdef JARVIS_PTHREAD

#include "threadpool.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#ifdef __linux__
#include <unistd.h>
#endif

typedef struct {
    pthread_t *threads;
    int n_threads;
    int n_workers;

    pthread_mutex_t mu;
    pthread_cond_t cv_work;
    pthread_cond_t cv_done;

    atomic_int next_idx;
    int start;
    int end;
    jarvis_parallel_fn fn;
    void *ctx;

    int active_workers;
    int has_work;
    int shutting_down;
    int initialized;
} jarvis_threadpool_t;

static jarvis_threadpool_t g_pool;
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static int clamp_threads(int n) {
    if (n < 1) return 1;
    if (n > 256) return 256;
    return n;
}

static int detect_cpus(void) {
#ifdef __linux__
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n > 0 && n < 1024) return (int)n;
#endif
    return 1;
}

static int read_env_threads(void) {
    const char *s = getenv("JARVIS_THREADS");
    if (!s || !*s) return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s) return 0;
    if (v <= 0) return 1;
    if (v > 256) v = 256;
    return (int)v;
}

int jarvis_threads(void) {
    int env = read_env_threads();
    if (env > 0) return clamp_threads(env);
    return clamp_threads(detect_cpus());
}

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_pool.mu);
        while (!g_pool.has_work && !g_pool.shutting_down) {
            pthread_cond_wait(&g_pool.cv_work, &g_pool.mu);
        }
        if (g_pool.shutting_down) {
            pthread_mutex_unlock(&g_pool.mu);
            return NULL;
        }

        jarvis_parallel_fn fn = g_pool.fn;
        void *ctx = g_pool.ctx;
        int end = g_pool.end;
        pthread_mutex_unlock(&g_pool.mu);

        for (;;) {
            int idx = atomic_fetch_add_explicit(&g_pool.next_idx, 1, memory_order_relaxed);
            if (idx >= end) break;
            fn(idx, ctx);
        }

        pthread_mutex_lock(&g_pool.mu);
        g_pool.active_workers--;
        if (g_pool.active_workers == 0) {
            g_pool.has_work = 0;
            pthread_cond_signal(&g_pool.cv_done);
        }
        pthread_mutex_unlock(&g_pool.mu);
    }
}

static void init_pool_once(void) {
    g_pool.threads = NULL;
    g_pool.n_threads = 1;
    g_pool.n_workers = 0;
    pthread_mutex_init(&g_pool.mu, NULL);
    pthread_cond_init(&g_pool.cv_work, NULL);
    pthread_cond_init(&g_pool.cv_done, NULL);
    atomic_init(&g_pool.next_idx, 0);
    g_pool.start = 0;
    g_pool.end = 0;
    g_pool.fn = NULL;
    g_pool.ctx = NULL;
    g_pool.active_workers = 0;
    g_pool.has_work = 0;
    g_pool.shutting_down = 0;
    g_pool.initialized = 0;

    int desired = jarvis_threads();
    if (desired <= 1) {
        g_pool.n_threads = 1;
        g_pool.n_workers = 0;
        g_pool.initialized = 1;
        return;
    }

    g_pool.threads = (pthread_t *)calloc((size_t)(desired - 1), sizeof(pthread_t));
    if (!g_pool.threads) {
        g_pool.n_threads = 1;
        g_pool.n_workers = 0;
        g_pool.initialized = 1;
        return;
    }

    int created = 0;
    for (int i = 0; i < desired - 1; i++) {
        if (pthread_create(&g_pool.threads[i], NULL, worker_main, NULL) != 0)
            break;
        created++;
    }

    g_pool.n_workers = created;
    g_pool.n_threads = created + 1; /* include calling thread */
    g_pool.initialized = 1;
}

static void ensure_pool(void) {
    pthread_once(&g_once, init_pool_once);
}

void jarvis_parallel_for(int start, int end, jarvis_parallel_fn fn, void *ctx) {
    if (end <= start) return;
    if (!fn) return;

    ensure_pool();

    if (g_pool.n_threads <= 1) {
        for (int i = start; i < end; i++) fn(i, ctx);
        return;
    }

    pthread_mutex_lock(&g_pool.mu);
    while (g_pool.has_work && !g_pool.shutting_down) {
        pthread_cond_wait(&g_pool.cv_done, &g_pool.mu);
    }
    if (g_pool.shutting_down) {
        pthread_mutex_unlock(&g_pool.mu);
        for (int i = start; i < end; i++) fn(i, ctx);
        return;
    }

    g_pool.start = start;
    g_pool.end = end;
    g_pool.fn = fn;
    g_pool.ctx = ctx;
    atomic_store_explicit(&g_pool.next_idx, start, memory_order_relaxed);
    g_pool.active_workers = g_pool.n_workers;
    g_pool.has_work = 1;

    pthread_cond_broadcast(&g_pool.cv_work);
    pthread_mutex_unlock(&g_pool.mu);

    /* Use the calling thread as worker 0 */
    for (;;) {
        int idx = atomic_fetch_add_explicit(&g_pool.next_idx, 1, memory_order_relaxed);
        if (idx >= end) break;
        fn(idx, ctx);
    }

    pthread_mutex_lock(&g_pool.mu);
    while (g_pool.has_work) {
        pthread_cond_wait(&g_pool.cv_done, &g_pool.mu);
    }
    pthread_mutex_unlock(&g_pool.mu);
}

void jarvis_threadpool_shutdown(void) {
    ensure_pool();
    if (g_pool.n_threads <= 1) return;

    pthread_mutex_lock(&g_pool.mu);
    g_pool.shutting_down = 1;
    pthread_cond_broadcast(&g_pool.cv_work);
    pthread_mutex_unlock(&g_pool.mu);

    for (int i = 0; i < g_pool.n_workers; i++) {
        if (g_pool.threads) pthread_join(g_pool.threads[i], NULL);
    }

    free(g_pool.threads);
    g_pool.threads = NULL;

    pthread_mutex_destroy(&g_pool.mu);
    pthread_cond_destroy(&g_pool.cv_work);
    pthread_cond_destroy(&g_pool.cv_done);

    g_pool.n_threads = 1;
    g_pool.n_workers = 0;
}

#endif /* JARVIS_PTHREAD */

