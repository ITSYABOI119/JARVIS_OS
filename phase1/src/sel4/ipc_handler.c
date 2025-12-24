/*
 * JARVIS AI-OS - IPC Handler Thread Implementation
 * Phase 2 Week 28
 *
 * Dedicated thread for processing Python↔seL4 IPC communication
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
 * Spawn IPC handler thread
 *
 * NOTE: This is a PLACEHOLDER implementation.
 * Proper seL4 threading requires:
 * - VKA (Virtual Kernel Allocator) context
 * - TCB (Thread Control Block) allocation
 * - Stack allocation and configuration
 * - seL4_TCB_Configure() and seL4_TCB_Resume()
 *
 * For Week 28, we'll use a simpler approach:
 * - Call ipc_handler_thread_entry() directly from main loop (polling mode)
 * - OR: Use pthread for development/testing (if available)
 * - Full seL4 threading implementation in Week 29
 */
bool ipc_handler_spawn_thread(ipc_handler_state_t *state)
{
    if (!state) {
        return false;
    }

    printf("[IPC Handler] Spawning IPC handler thread...\n");

    /*
     * TODO: Implement proper seL4 threading here
     *
     * Example seL4 threading code (requires VKA context):
     *
     * // Allocate TCB
     * seL4_CPtr tcb = vka_alloc_tcb(&vka);
     *
     * // Allocate stack
     * void *stack = malloc(IPC_HANDLER_STACK_SIZE);
     *
     * // Configure TCB
     * seL4_UserContext regs = {0};
     * regs.pc = (seL4_Word)ipc_handler_thread_entry;
     * regs.sp = (seL4_Word)(stack + IPC_HANDLER_STACK_SIZE);
     * // ... other register setup
     *
     * seL4_TCB_Configure(tcb, ...);
     * seL4_TCB_WriteRegisters(tcb, 0, 0, 2, &regs);
     * seL4_TCB_Resume(tcb);
     */

    printf("[IPC Handler] WARNING: Full seL4 threading not yet implemented\n");
    printf("[IPC Handler] Using polling mode instead (call ipc_handler_thread_entry manually)\n");

    return true;  /* Return true to continue (caller will use polling mode) */
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
