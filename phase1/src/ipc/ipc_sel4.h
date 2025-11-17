/*
 * JARVIS AI-OS - IPC seL4 Integration Header
 * Week 4: seL4-specific IPC wrapper
 *
 * This wraps the lock-free ring buffer for use in seL4 environment
 * with shared memory between kernel and user space.
 */

#ifndef IPC_SEL4_H
#define IPC_SEL4_H

#include "ring_buffer.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * Initialize IPC system in seL4
 *
 * This sets up shared memory for the ring buffer.
 * Must be called during system initialization.
 *
 * @return true on success, false on failure
 */
bool ipc_sel4_init(void);

/*
 * Send a message via IPC
 *
 * @param type Message type
 * @param payload Message data (up to MAX_MESSAGE_SIZE)
 * @param payload_size Size of payload in bytes
 * @return true if sent, false if buffer full
 */
bool ipc_sel4_send(message_type_t type, const char *payload, uint32_t payload_size);

/*
 * Receive a message via IPC (non-blocking)
 *
 * @param msg Output buffer for received message
 * @return true if message received, false if buffer empty
 */
bool ipc_sel4_receive(ring_message_t *msg);

/*
 * Get IPC statistics
 *
 * @param total_sent Output: total messages sent
 * @param total_received Output: total messages received
 * @param total_drops Output: total messages dropped
 */
void ipc_sel4_stats(uint64_t *total_sent, uint64_t *total_received, uint64_t *total_drops);

/*
 * Print IPC statistics (for debugging)
 */
void ipc_sel4_print_stats(void);

#endif /* IPC_SEL4_H */
