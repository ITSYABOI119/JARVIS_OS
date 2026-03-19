/*
 * JARVIS AI-OS - IPC Handler Implementation (POLLING MODE)
 * Phase 2 Week 28
 *
 * Processes Python↔seL4 IPC communication in polling mode.
 *
 * ============================================================================
 * ARCHITECTURE NOTE: Polling vs Threading (Phase 2 Decision)
 * ============================================================================
 *
 * This implementation uses POLLING mode, not true threading.
 *
 * Current behavior:
 * - ipc_handler_init_polling() sets up the handler state
 * - ipc_handler_poll_once() processes one message (non-blocking)
 * - Caller is responsible for calling poll in a loop
 * - usleep(100) is used for POSIX testing only
 *
 * Why not threading?
 * - seL4 threading requires VKA context, TCB allocation, capability setup
 * - seL4 tutorials framework doesn't expose threading infrastructure
 * - Polling is simpler and sufficient for Phase 2 proof-of-concept
 *
 * Phase 3 threading approach (when needed):
 * 1. Allocate TCB via vka_alloc_tcb()
 * 2. Allocate stack memory
 * 3. Configure TCB with seL4_TCB_Configure()
 * 4. Set entry point and registers
 * 5. Resume with seL4_TCB_Resume()
 *
 * For Pi 4 (UART IPC):
 * - Polling is actually preferred (interrupt-driven UART has its own timing)
 * - The Python AI agent polls the serial port; seL4 polls the buffer
 *
 * ============================================================================
 */

#include "ipc_handler.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>  /* For usleep (temporary - will use seL4 sleep) */

/* Initialize IPC handler state */
bool ipc_handler_init(ipc_handler_state_t *state,
                     decision_cache_t *cache,
                     dual_ring_buffer_t *drb)
{
    if (!state || !cache || !drb) {
        return false;
    }

    state->cache = cache;
    state->drb = drb;
    state->running = true;
    state->queries_processed = 0;
    state->stats_requests = 0;
    state->errors = 0;

    return true;
}

/**
 * Process MSG_CACHE_LOOKUP message
 *
 * Performs cache lookup and sends response via dual ring buffer
 */
static void handle_cache_lookup(ipc_handler_state_t *state,
                                const char *query,
                                uint32_t msg_id)
{
    /* Perform cache lookup */
    char action[MAX_ACTION_LEN];
    trust_level_t trust;
    bool hit = cache_lookup(state->cache, query, action, MAX_ACTION_LEN, &trust);

    /* Send response */
    if (dual_ring_send_response(state->drb, msg_id, hit, action, trust)) {
        state->queries_processed++;
    } else {
        state->errors++;
        printf("[IPC Handler] ERROR: Failed to send cache response\n");
    }
}

/**
 * Process MSG_CACHE_STATS message
 *
 * Sends full cache statistics via dual ring buffer
 */
static void handle_cache_stats(ipc_handler_state_t *state, uint32_t msg_id)
{
    /* Get cache statistics */
    cache_stats_t stats;
    cache_get_stats(state->cache, &stats);

    /* Send stats response */
    if (dual_ring_send_cache_stats(state->drb, msg_id, &stats)) {
        state->stats_requests++;
    } else {
        state->errors++;
        printf("[IPC Handler] ERROR: Failed to send cache stats\n");
    }
}

/**
 * IPC handler thread main loop
 *
 * Continuously monitors query ring and processes incoming messages:
 * - MSG_CACHE_LOOKUP: Perform cache lookup, send result
 * - MSG_CACHE_STATS: Send cache statistics
 * - Other: Ignore
 */
void ipc_handler_thread_entry(void *arg)
{
    ipc_handler_state_t *state = (ipc_handler_state_t *)arg;

    if (!state) {
        printf("[IPC Handler] ERROR: NULL state pointer\n");
        return;
    }

    printf("[IPC Handler] Thread started\n");
    printf("[IPC Handler] Monitoring query ring for incoming messages...\n");

    /* Main loop - runs until running flag is cleared */
    while (state->running) {
        char query[MAX_QUERY_LEN];
        uint32_t msg_id;
        ring_message_t msg;

        /* Poll query ring for incoming messages (non-blocking) */
        if (ring_buffer_read(&state->drb->query_ring, &msg)) {
            /* Message received! */
            msg_id = msg.id;

            /* Handle based on message type */
            if (msg.type == MSG_CACHE_LOOKUP) {
                /* Extract query string */
                size_t copy_len = (msg.payload_size < MAX_QUERY_LEN) ? msg.payload_size : (MAX_QUERY_LEN - 1);
                memcpy(query, msg.payload, copy_len);
                query[copy_len] = '\0';

                /* Process cache lookup */
                handle_cache_lookup(state, query, msg_id);

            } else if (msg.type == MSG_CACHE_STATS) {
                /* Process stats request */
                handle_cache_stats(state, msg_id);

            } else {
                /* Unknown/unsupported message type */
                printf("[IPC Handler] WARNING: Unsupported message type %d\n", msg.type);
                state->errors++;
            }

        } else {
            /* No messages available - yield CPU briefly to avoid busy-waiting */
            /* In seL4, this would be seL4_Yield() or a short sleep */
            usleep(100);  /* 100μs sleep (temporary - use seL4 primitives in production) */
        }
    }

    printf("[IPC Handler] Thread stopping (running flag cleared)\n");
}

/**
 * Initialize polling mode (PREFERRED for Phase 2)
 *
 * This function prepares the handler for polling mode operation.
 * Caller should then use ipc_handler_poll_once() or ipc_handler_thread_entry()
 * to process messages.
 *
 * Polling mode is used because:
 * - seL4 tutorials framework doesn't provide threading infrastructure
 * - Simpler and more debuggable for Phase 2 proof-of-concept
 * - For Pi 4 with UART, polling is natural (no shared memory threading needed)
 *
 * Returns true to indicate handler is ready for polling.
 */
bool ipc_handler_init_polling(ipc_handler_state_t *state)
{
    if (!state) {
        return false;
    }

    printf("[IPC Handler] Initializing polling mode...\n");
    printf("[IPC Handler] Ready for polling (call ipc_handler_poll_once or ipc_handler_thread_entry)\n");

    return true;
}

/**
 * Legacy alias for ipc_handler_init_polling
 *
 * DEPRECATED: Use ipc_handler_init_polling() instead.
 * Kept for backward compatibility with existing code.
 */
bool ipc_handler_spawn_thread(ipc_handler_state_t *state)
{
    printf("[IPC Handler] NOTE: spawn_thread() is deprecated, using polling mode\n");
    return ipc_handler_init_polling(state);
}

/**
 * Poll for one message (non-blocking)
 *
 * Checks the query ring for one incoming message and processes it.
 * Returns immediately if no message is available.
 *
 * This is the preferred way to integrate IPC handling with main loop.
 *
 * @param state IPC handler state
 * @return true if a message was processed, false if queue empty
 */
bool ipc_handler_poll_once(ipc_handler_state_t *state)
{
    if (!state || !state->running) {
        return false;
    }

    char query[MAX_QUERY_LEN];
    uint32_t msg_id;
    ring_message_t msg;

    /* Poll query ring for incoming message (non-blocking) */
    if (ring_buffer_read(&state->drb->query_ring, &msg)) {
        msg_id = msg.id;

        if (msg.type == MSG_CACHE_LOOKUP) {
            size_t copy_len = (msg.payload_size < MAX_QUERY_LEN) ? msg.payload_size : (MAX_QUERY_LEN - 1);
            memcpy(query, msg.payload, copy_len);
            query[copy_len] = '\0';
            handle_cache_lookup(state, query, msg_id);
        } else if (msg.type == MSG_CACHE_STATS) {
            handle_cache_stats(state, msg_id);
        } else {
            printf("[IPC Handler] WARNING: Unsupported message type %d\n", msg.type);
            state->errors++;
        }
        return true;  /* Message processed */
    }

    return false;  /* No message available */
}

/* Stop IPC handler thread */
void ipc_handler_stop(ipc_handler_state_t *state)
{
    if (!state) {
        return;
    }

    printf("[IPC Handler] Stopping thread...\n");
    state->running = false;
}

/* Get IPC handler statistics */
void ipc_handler_get_stats(const ipc_handler_state_t *state,
                          uint32_t *queries_out,
                          uint32_t *stats_requests_out,
                          uint32_t *errors_out)
{
    if (!state) {
        return;
    }

    if (queries_out) {
        *queries_out = state->queries_processed;
    }
    if (stats_requests_out) {
        *stats_requests_out = state->stats_requests;
    }
    if (errors_out) {
        *errors_out = state->errors;
    }
}

/* Print IPC handler statistics */
void ipc_handler_print_stats(const ipc_handler_state_t *state)
{
    if (!state) {
        return;
    }

    printf("\n========== IPC HANDLER STATISTICS ==========\n");
    printf("Queries processed:    %u\n", state->queries_processed);
    printf("Stats requests:       %u\n", state->stats_requests);
    printf("Errors:               %u\n", state->errors);
    printf("Running:              %s\n", state->running ? "Yes" : "No");
    printf("============================================\n\n");
}
