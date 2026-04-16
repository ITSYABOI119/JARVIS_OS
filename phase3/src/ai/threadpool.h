/*
 * JARVIS AI-OS Phase 3 — Minimal Thread Pool (native Linux)
 *
 * Purpose: parallelize heavy, row-parallel compute (e.g. matmul rows) with pthreads.
 * This module is only compiled when JARVIS_PTHREAD is defined.
 *
 * Pure C11.
 */

#ifndef JARVIS_THREADPOOL_H
#define JARVIS_THREADPOOL_H

#include <stddef.h>

#ifdef JARVIS_PTHREAD

typedef void (*jarvis_parallel_fn)(int idx, void *ctx);

/* Return configured thread count.
 * Default: env JARVIS_THREADS if set, else number of online CPUs.
 * Returns >= 1.
 */
int jarvis_threads(void);

/* Run fn(idx, ctx) for idx in [start, end) in parallel.
 * ctx is memcpy'd into the pool (max 64 bytes) so callers can pass stack structs safely.
 * If threads <= 1 or range is empty, runs serially.
 */
void jarvis_parallel_for(int start, int end, jarvis_parallel_fn fn, void *ctx, size_t ctx_size);

/* Optional: shutdown worker threads (primarily for tests/tools). */
void jarvis_threadpool_shutdown(void);

#else

/* Build-time stub for non-pthread targets (seL4, etc). */
typedef void (*jarvis_parallel_fn)(int idx, void *ctx);
static inline int jarvis_threads(void) { return 1; }
static inline void jarvis_parallel_for(int start, int end, jarvis_parallel_fn fn, void *ctx, size_t ctx_size) {
    (void)ctx_size;
    for (int i = start; i < end; i++) fn(i, ctx);
}
static inline void jarvis_threadpool_shutdown(void) { (void)0; }

#endif /* JARVIS_PTHREAD */

#endif /* JARVIS_THREADPOOL_H */
