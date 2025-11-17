/*
 * JARVIS AI-OS - IPC seL4 Integration Implementation
 * Week 4: seL4-specific IPC wrapper
 *
 * This implements IPC using shared memory in seL4.
 * For now, we use a simple static allocation approach.
 */

#include "ipc_sel4.h"
#include "ring_buffer.h"
#include <stdio.h>
#include <string.h>

/*
 * Global ring buffer (shared memory)
 *
 * NOTE: In a full seL4 implementation, this would be mapped
 * into shared memory accessible by both kernel and user space.
 * For Phase 1 PoC, we use a static global as proof-of-concept.
 */
static ring_buffer_t g_ipc_buffer;
static bool g_ipc_initialized = false;
static uint32_t g_message_id_counter = 0;

/*
 * Initialize IPC system
 */
bool ipc_sel4_init(void)
{
    if (g_ipc_initialized) {
        printf("[IPC] Already initialized\n");
        return true;
    }

    printf("[IPC] Initializing shared memory IPC...\n");

    /* Initialize ring buffer */
    ring_buffer_init(&g_ipc_buffer);

    g_ipc_initialized = true;
    g_message_id_counter = 0;

    printf("[IPC] Initialization complete\n");
    printf("[IPC] Buffer size: %d messages\n", RING_BUFFER_SIZE);
    printf("[IPC] Message size: %d bytes max\n", MAX_MESSAGE_SIZE);

    return true;
}

/*
 * Send a message via IPC
 */
bool ipc_sel4_send(message_type_t type, const char *payload, uint32_t payload_size)
{
    if (!g_ipc_initialized) {
        printf("[IPC] ERROR: Not initialized\n");
        return false;
    }

    if (payload_size > MAX_MESSAGE_SIZE) {
        printf("[IPC] ERROR: Payload too large (%u > %d)\n", payload_size, MAX_MESSAGE_SIZE);
        return false;
    }

    /* Create message */
    ring_message_t msg;
    msg.type = type;
    msg.id = g_message_id_counter++;
    msg.payload_size = payload_size;
    memcpy(msg.payload, payload, payload_size);
    msg.timestamp = ring_get_timestamp_ns();

    /* Send via ring buffer */
    if (!ring_buffer_write(&g_ipc_buffer, &msg)) {
        printf("[IPC] WARNING: Buffer full, message dropped (id=%u)\n", msg.id);
        return false;
    }

    return true;
}

/*
 * Receive a message via IPC
 */
bool ipc_sel4_receive(ring_message_t *msg)
{
    if (!g_ipc_initialized) {
        printf("[IPC] ERROR: Not initialized\n");
        return false;
    }

    return ring_buffer_read(&g_ipc_buffer, msg);
}

/*
 * Get IPC statistics
 */
void ipc_sel4_stats(uint64_t *total_sent, uint64_t *total_received, uint64_t *total_drops)
{
    if (!g_ipc_initialized) {
        if (total_sent) *total_sent = 0;
        if (total_received) *total_received = 0;
        if (total_drops) *total_drops = 0;
        return;
    }

    ring_buffer_stats(&g_ipc_buffer, total_sent, total_received, total_drops);
}

/*
 * Print IPC statistics
 */
void ipc_sel4_print_stats(void)
{
    if (!g_ipc_initialized) {
        printf("[IPC] Not initialized\n");
        return;
    }

    printf("\n");
    printf("========== IPC STATISTICS ==========\n");

    uint64_t sent, received, drops;
    ipc_sel4_stats(&sent, &received, &drops);

    printf("Total sent:      %llu\n", (unsigned long long)sent);
    printf("Total received:  %llu\n", (unsigned long long)received);
    printf("Total drops:     %llu\n", (unsigned long long)drops);

    size_t count = ring_buffer_count(&g_ipc_buffer);
    size_t space = ring_buffer_space(&g_ipc_buffer);

    printf("Current count:   %zu / %d\n", count, RING_BUFFER_SIZE);
    printf("Available space: %zu\n", space);

    if (sent > 0) {
        double drop_rate = (double)drops / (double)sent * 100.0;
        printf("Drop rate:       %.2f%%\n", drop_rate);
    }

    printf("====================================\n");
    printf("\n");
}
