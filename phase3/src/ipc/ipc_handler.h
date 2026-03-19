/*
 * JARVIS AI-OS - IPC Handler (Phase 2 Week 28)
 *
 * Handles Python↔seL4 IPC communication for cache lookups and statistics.
 *
 * OPERATION MODE: POLLING (Phase 2)
 * =================================
 * Phase 2 uses polling mode, not threading, because:
 * - seL4 tutorials framework doesn't expose threading infrastructure
 * - Polling is simpler and sufficient for proof-of-concept
 * - For Pi 4 UART IPC, polling is natural (matches serial port model)
 *
 * Usage:
 *   1. Call ipc_handler_init() to initialize state
 *   2. Call ipc_handler_init_polling() to set up polling mode
 *   3. Call ipc_handler_poll_once() in your main loop, OR
 *      Call ipc_handler_thread_entry() to run continuous polling
 *
 * Phase 3 threading (future):
 *   Would use seL4 TCB allocation, stack setup, seL4_TCB_Configure()
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
 * Initialize polling mode (PREFERRED for Phase 2)
 *
 * Prepares the handler for polling mode operation.
 * After this, call ipc_handler_poll_once() in your main loop.
 *
 * @param state Pointer to handler state
 * @return true on success, false on failure
 */
bool ipc_handler_init_polling(ipc_handler_state_t *state);

/**
 * Poll for one message (non-blocking)
 *
 * Checks query ring for one incoming message and processes it.
 * Returns immediately if no message available.
 *
 * @param state Pointer to handler state
 * @return true if message processed, false if queue empty
 */
bool ipc_handler_poll_once(ipc_handler_state_t *state);

/**
 * Legacy: Spawn IPC handler thread
 *
 * DEPRECATED: Use ipc_handler_init_polling() instead.
 * This function now just calls ipc_handler_init_polling().
 * Kept for backward compatibility with existing code.
 *
 * @param state Pointer to handler state
 * @return true (always succeeds, enables polling mode)
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
