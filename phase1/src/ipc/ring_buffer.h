/*
 * JARVIS AI-OS - Lock-Free Ring Buffer (IPC)
 * Phase 1 Week 4
 *
 * Single-Producer Single-Consumer (SPSC) lock-free ring buffer
 * For communication between kernel (Ring 0) and AI agent (Ring 3)
 *
 * Target: <100μs IPC latency
 * Phase 0 validation: 54μs median latency achieved
 */

#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Use custom atomics for seL4 compatibility (no stdatomic.h with -nostdinc) */
#include "sel4_atomics.h"
#define alignas(x) __attribute__((aligned(x)))

/* Configuration */
#define RING_BUFFER_SIZE 1024       /* Must be power of 2 */
#define RING_BUFFER_MASK (RING_BUFFER_SIZE - 1)
#define CACHE_LINE_SIZE 64          /* x86-64 cache line size */
#define MAX_MESSAGE_SIZE 256        /* Maximum message payload */

/* Message types for IPC */
typedef enum {
    MSG_COMMAND = 0,        /* Command from kernel to AI */
    MSG_RESPONSE = 1,       /* Response from AI to kernel */
    MSG_QUERY = 2,          /* Cache query */
    MSG_EVENT = 3,          /* System event notification */
    MSG_CONTROL = 4         /* Control message (shutdown, etc) */
} message_type_t;

/* Message structure */
typedef struct {
    message_type_t type;
    uint32_t id;                    /* Message ID for tracking */
    uint32_t payload_size;          /* Size of payload data */
    char payload[MAX_MESSAGE_SIZE]; /* Message data */
    uint64_t timestamp;             /* For latency measurement */
} ring_message_t;

/*
 * Lock-free ring buffer structure
 *
 * Cache-line alignment prevents false sharing between producer and consumer
 * Atomic operations ensure lock-free thread safety
 */
typedef struct {
    /* Producer cache line (written by producer, read by consumer) */
    alignas(CACHE_LINE_SIZE) atomic_uint_fast64_t write_index;

    /* Consumer cache line (written by consumer, read by producer) */
    alignas(CACHE_LINE_SIZE) atomic_uint_fast64_t read_index;

    /* Ring buffer data */
    alignas(CACHE_LINE_SIZE) ring_message_t buffer[RING_BUFFER_SIZE];

    /* Statistics (non-critical, can share cache line) */
    atomic_uint_fast64_t total_writes;
    atomic_uint_fast64_t total_reads;
    atomic_uint_fast64_t total_drops;     /* Messages dropped when full */
} ring_buffer_t;

/* Public API */

/**
 * Initialize the ring buffer
 * @param rb Pointer to ring buffer structure
 */
void ring_buffer_init(ring_buffer_t *rb);

/**
 * Write a message to the ring buffer (producer side)
 * Non-blocking: returns false if buffer is full
 *
 * @param rb Pointer to ring buffer
 * @param msg Message to write
 * @return true if written successfully, false if buffer full
 */
bool ring_buffer_write(ring_buffer_t *rb, const ring_message_t *msg);

/**
 * Read a message from the ring buffer (consumer side)
 * Non-blocking: returns false if buffer is empty
 *
 * @param rb Pointer to ring buffer
 * @param msg Output buffer for message
 * @return true if read successfully, false if buffer empty
 */
bool ring_buffer_read(ring_buffer_t *rb, ring_message_t *msg);

/**
 * Check if ring buffer is empty
 * @param rb Pointer to ring buffer
 * @return true if empty, false otherwise
 */
bool ring_buffer_is_empty(const ring_buffer_t *rb);

/**
 * Check if ring buffer is full
 * @param rb Pointer to ring buffer
 * @return true if full, false otherwise
 */
bool ring_buffer_is_full(const ring_buffer_t *rb);

/**
 * Get number of messages currently in buffer
 * @param rb Pointer to ring buffer
 * @return Number of messages available to read
 */
size_t ring_buffer_count(const ring_buffer_t *rb);

/**
 * Get available space in buffer
 * @param rb Pointer to ring buffer
 * @return Number of messages that can be written
 */
size_t ring_buffer_space(const ring_buffer_t *rb);

/**
 * Get buffer statistics
 * @param rb Pointer to ring buffer
 * @param total_writes Output: total messages written
 * @param total_reads Output: total messages read
 * @param total_drops Output: total messages dropped
 */
void ring_buffer_stats(const ring_buffer_t *rb,
                       uint64_t *total_writes,
                       uint64_t *total_reads,
                       uint64_t *total_drops);

/**
 * Print buffer statistics (for debugging)
 * @param rb Pointer to ring buffer
 */
void ring_buffer_print_stats(const ring_buffer_t *rb);

/**
 * Reset buffer statistics
 * @param rb Pointer to ring buffer
 */
void ring_buffer_reset_stats(ring_buffer_t *rb);

/* Helper functions for creating messages */

/**
 * Create a command message
 * @param msg Output buffer
 * @param id Message ID
 * @param command Command string
 * @return true on success, false if command too long
 */
bool ring_message_create_command(ring_message_t *msg, uint32_t id, const char *command);

/**
 * Create a response message
 * @param msg Output buffer
 * @param id Message ID (should match request)
 * @param response Response string
 * @return true on success, false if response too long
 */
bool ring_message_create_response(ring_message_t *msg, uint32_t id, const char *response);

/**
 * Create a query message
 * @param msg Output buffer
 * @param id Message ID
 * @param query Query string
 * @return true on success, false if query too long
 */
bool ring_message_create_query(ring_message_t *msg, uint32_t id, const char *query);

/**
 * Get current timestamp (for latency measurement)
 * @return Timestamp in nanoseconds
 */
uint64_t ring_get_timestamp_ns(void);

/**
 * Calculate message latency
 * @param msg Message with timestamp
 * @return Latency in nanoseconds
 */
uint64_t ring_message_latency_ns(const ring_message_t *msg);

#endif /* RING_BUFFER_H */
