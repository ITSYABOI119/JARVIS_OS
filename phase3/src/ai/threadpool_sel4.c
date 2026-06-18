/* JARVIS AI-OS Phase 4 M3 — seL4-native worker pool (Process B). Compiled only when
 * JARVIS_SEL4_SMP is defined (the jarvis-inference target). Resources are created by
 * Process A at spawn time; this file is pure runtime (no allocation).
 *
 * Per-worker one-shot wake (Notification) + generation publish (release/acquire) +
 * active-counter join → structurally immune to the cross-dispatch over-decrement race
 * fixed in the pthread backend at M3 step 1 (it ports the work-stealing CORE, NOT the
 * has_work join scheme). See phase4/docs/PHASE_4_M3_THREADPOOL_DESIGN.md.
 */
#ifdef JARVIS_SEL4_SMP
#include "threadpool.h"
#include <sel4/sel4.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

/* JARVIS_MAX_WORKERS comes from threadpool.h (shared with inference_server.c). */

typedef struct {
    _Atomic int      next_idx;
    int              end;
    jarvis_parallel_fn fn;
    char             ctx_buf[64];
    _Atomic int      active;                 /* workers still running this dispatch */
    _Atomic unsigned gen;                    /* release/acquire publish handshake */
    seL4_CPtr        wake[JARVIS_MAX_WORKERS];/* [1..n-1]; [0] unused (dispatcher) */
    seL4_CPtr        done;
    int              n_threads;              /* total incl. dispatcher = numNodes */
    volatile int     shutting_down;
    int              initialized;
} sel4_threadpool_t;

static sel4_threadpool_t g_pool;

int jarvis_threads(void) {
    return (g_pool.initialized && g_pool.n_threads > 0) ? g_pool.n_threads : 1;
}

void jarvis_sel4_worker_entry(void *arg0, void *arg1, void *ipc_buf) {
    (void)arg1;
    /* This thread was started by Process A OUTSIDE sel4runtime's per-thread init, so the
     * libsel4 syscall stubs' IPC-buffer TLS pointer (__sel4_ipc_buffer) is unset — the first
     * seL4_Wait would read a null IPC buffer and fault. sel4utils passes the thread's IPC
     * buffer vaddr as the 3rd entry arg; register it before any seL4_Wait/Signal. */
    seL4_SetIPCBuffer((seL4_IPCBuffer *)ipc_buf);
    seL4_CPtr wake = (seL4_CPtr)(uintptr_t)arg0;
    for (;;) {
        seL4_Wait(wake, NULL);                               /* block until dispatched */
        if (g_pool.shutting_down) return;
        /* DO NOT REMOVE: this acquire pairs with the release on g_pool.gen in
         * jarvis_parallel_for and is the barrier that publishes fn/end/ctx_buf/next_idx
         * to this worker (the seL4_Wait return guarantees the new gen is observable). */
        (void)atomic_load_explicit(&g_pool.gen, memory_order_acquire);
        jarvis_parallel_fn fn = g_pool.fn;
        int end = g_pool.end;
        for (;;) {
            int i = atomic_fetch_add_explicit(&g_pool.next_idx, 1, memory_order_relaxed);
            if (i >= end) break;
            fn(i, g_pool.ctx_buf);
        }
        if (atomic_fetch_sub_explicit(&g_pool.active, 1, memory_order_acq_rel) == 1)
            seL4_Signal(g_pool.done);                        /* last worker joins */
    }
}

void jarvis_sel4_pool_init(int n_threads, seL4_CPtr done, const seL4_CPtr *wake, int n_wake) {
    /* Defensive floor: never advertise more threads than wake caps were delivered, so a
     * PA/PB ABI divergence degrades to fewer-threads-but-correct rather than signaling
     * never-initialized (null) wake slots at dispatch. */
    if (n_threads > n_wake + 1) n_threads = n_wake + 1;
    g_pool.n_threads = n_threads;
    g_pool.done = done;
    for (int i = 0; i < n_wake && (i + 1) < JARVIS_MAX_WORKERS; i++)
        g_pool.wake[i + 1] = wake[i];
    atomic_store_explicit(&g_pool.gen, 0u, memory_order_relaxed);
    atomic_store_explicit(&g_pool.next_idx, 0, memory_order_relaxed);
    atomic_store_explicit(&g_pool.active, 0, memory_order_relaxed);
    g_pool.shutting_down = 0;
    g_pool.initialized = 1;
}

void jarvis_parallel_for(int start, int end, jarvis_parallel_fn fn, void *ctx, size_t ctx_size) {
    if (end <= start || !fn) return;
    int n = g_pool.n_threads;
    if (!g_pool.initialized || n <= 1 || (end - start) < 256) {
        for (int i = start; i < end; i++) fn(i, ctx);
        return;
    }
    int n_workers = n - 1;
    if (ctx && ctx_size > 0) {
        if (ctx_size > sizeof(g_pool.ctx_buf)) ctx_size = sizeof(g_pool.ctx_buf);
        memcpy(g_pool.ctx_buf, ctx, ctx_size);
    }
    g_pool.fn = fn;
    g_pool.end = end;
    atomic_store_explicit(&g_pool.next_idx, start, memory_order_relaxed);
    atomic_store_explicit(&g_pool.active, n_workers, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_pool.gen, 1u, memory_order_release);   /* publish */
    for (int i = 1; i <= n_workers; i++) seL4_Signal(g_pool.wake[i]);
    for (;;) {                                                          /* caller = worker 0 */
        int i = atomic_fetch_add_explicit(&g_pool.next_idx, 1, memory_order_relaxed);
        if (i >= end) break;
        fn(i, g_pool.ctx_buf);
    }
    seL4_Wait(g_pool.done, NULL);                                       /* block for join */
}

/* QUIESCENT-ONLY: must not be called concurrently with an in-flight jarvis_parallel_for.
 * seL4 Notifications coalesce, so a shutdown wake racing a pending work-wake would let a
 * worker exit without its active-counter decrement and hang the dispatcher's join. PB does
 * not call this on the runtime IPC path; it exists for test/tool teardown after all
 * dispatches have completed. */
void jarvis_threadpool_shutdown(void) {
    g_pool.shutting_down = 1;
    for (int i = 1; i < g_pool.n_threads; i++)
        if (g_pool.wake[i]) seL4_Signal(g_pool.wake[i]);
}
#endif /* JARVIS_SEL4_SMP */
