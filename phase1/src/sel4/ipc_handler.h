/*
 * JARVIS AI-OS - IPC Handler Thread (Phase 2 Week 28)
 *
 * Dedicated thread for handling Python↔seL4 IPC communication
 * Runs continuously in background, processing cache lookups and statistics requests
 *
 * Threading: Uses seL4 threading primitives (TCB, endpoints)
 * Priority: Normal (same as main thread)
 * Stack: 8KB (sufficient for cache operations)
 */

#ifndef IPC_HANDLER_H
#define IPC_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "dual_ring_buffer.h"
#include "decision_cache.h"

/* IPC handler configuration */
#define IPC_HANDLER_STACK_SIZE (8 * 1024)   /* 8KB stack */
#define IPC_HANDLER_PRIORITY   100          /* Normal priority */

/* IPC handler thread state */
typedef struct {
    decision_cache_t *cache;        /* Pointer to decision cache */
    dual_ring_buffer_t *drb;        /* Pointer to dual ring buffer */
    bool running;                   /* Thread running flag */
    uint32_t queries_processed;     /* Statistics: queries handled */
    uint32_t stats_requests;        /* Statistics: stats requests handled */
    uint32_t errors;                /* Statistics: error count */
} ipc_handler_state_t;

/**
 * Initialize IPC handler state
 * @param state Pointer to handler state
 * @param cache Pointer to decision cache
 * @param drb Pointer to dual ring buffer
 * @return true on success, false on failure
 */
bool ipc_handler_init(ipc_handler_state_t *state,
                     decision_cache_t *cache,
                     dual_ring_buffer_t *drb);

/**
 * IPC handler thread entry point
 *
 * This function runs in a dedicated seL4 thread:
 * - Continuously monitors query ring for incoming messages
 * - Handles MSG_CACHE_LOOKUP: Performs cache lookup, sends response
 * - Handles MSG_CACHE_STATS: Sends cache statistics
 * - Sub-millisecond response time (<1ms target)
 *
 * @param arg Pointer to ipc_handler_state_t
 */
void ipc_handler_thread_entry(void *arg);

/**
 * Spawn IPC handler thread (seL4-specific)
 *
 * Creates and configures a new seL4 thread for IPC handling:
 * 1. Allocate Thread Control Block (TCB)
 * 2. Allocate stack (8KB)
 * 3. Configure thread (priority, stack pointer, entry point)
 * 4. Resume thread
 *
 * @param state Pointer to handler state (passed to thread)
 * @return true on success, false on failure
 *
 * NOTE: This function requires seL4 VKA (Virtual Kernel Allocator) context
 *       and should be called from the root task after VKA initialization.
 */
bool ipc_handler_spawn_thread(ipc_handler_state_t *state);

/**
 * Stop IPC handler thread
 * Sets running flag to false, causing thread to exit gracefully
 * @param state Pointer to handler state
 */
void ipc_handler_stop(ipc_handler_state_t *state);

/**
 * Get IPC handler statistics
 * @param state Pointer to handler state
 * @param queries_out Output: queries processed
 * @param stats_requests_out Output: stats requests handled
 * @param errors_out Output: error count
 */
void ipc_handler_get_stats(const ipc_handler_state_t *state,
                          uint32_t *queries_out,
                          uint32_t *stats_requests_out,
                          uint32_t *errors_out);

/**
 * Print IPC handler statistics (for debugging)
 * @param state Pointer to handler state
 */
void ipc_handler_print_stats(const ipc_handler_state_t *state);

#endif /* IPC_HANDLER_H */
