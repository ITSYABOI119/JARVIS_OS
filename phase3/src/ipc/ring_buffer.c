/*
 * JARVIS AI-OS - Lock-Free Ring Buffer Implementation
 * Phase 1 Week 4
 *
 * SPSC lock-free ring buffer using atomic operations
 * Based on Phase 0 validation (54μs median latency)
 */

#include "ring_buffer.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

/* Initialize ring buffer */
void ring_buffer_init(ring_buffer_t *rb)
{
    if (!rb) {
        return;
    }

    /* Initialize atomic indices */
    atomic_init(&rb->write_index, 0);
    atomic_init(&rb->read_index, 0);

    /* Initialize statistics */
    atomic_init(&rb->total_writes, 0);
    atomic_init(&rb->total_reads, 0);
    atomic_init(&rb->total_drops, 0);

    /* Zero out buffer */
    memset(rb->buffer, 0, sizeof(rb->buffer));
}

/* Write message to ring buffer (producer) */
bool ring_buffer_write(ring_buffer_t *rb, const ring_message_t *msg)
{
    if (!rb || !msg) {
        return false;
    }

    /* Load indices with acquire semantics */
    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_relaxed);
    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_acquire);

    /* Calculate next write position */
    uint64_t next_write = write_idx + 1;

    /* Check if buffer is full */
    if (next_write - read_idx > RING_BUFFER_SIZE) {
        /* Buffer full - drop message */
        atomic_fetch_add_explicit(&rb->total_drops, 1, memory_order_relaxed);
        return false;
    }

    /* Write message to buffer */
    size_t slot = write_idx & RING_BUFFER_MASK;
    memcpy(&rb->buffer[slot], msg, sizeof(ring_message_t));

    /* Update write index with release semantics (makes write visible to consumer) */
    atomic_store_explicit(&rb->write_index, next_write, memory_order_release);

    /* Update statistics */
    atomic_fetch_add_explicit(&rb->total_writes, 1, memory_order_relaxed);

    return true;
}

/* Read message from ring buffer (consumer) */
bool ring_buffer_read(ring_buffer_t *rb, ring_message_t *msg)
{
    if (!rb || !msg) {
        return false;
    }

    /* Load indices with acquire semantics */
    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_relaxed);
    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);

    /* Check if buffer is empty */
    if (read_idx == write_idx) {
        return false;
    }

    /* Read message from buffer */
    size_t slot = read_idx & RING_BUFFER_MASK;
    memcpy(msg, &rb->buffer[slot], sizeof(ring_message_t));

    /* Update read index with release semantics */
    atomic_store_explicit(&rb->read_index, read_idx + 1, memory_order_release);

    /* Update statistics */
    atomic_fetch_add_explicit(&rb->total_reads, 1, memory_order_relaxed);

    return true;
}

/* Check if buffer is empty */
bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    if (!rb) {
        return true;
    }

    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_acquire);
    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);

    return (read_idx == write_idx);
}

/* Check if buffer is full */
bool ring_buffer_is_full(const ring_buffer_t *rb)
{
    if (!rb) {
        return true;
    }

    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);
    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_acquire);

    return ((write_idx - read_idx) >= RING_BUFFER_SIZE);
}

/* Get number of messages in buffer */
size_t ring_buffer_count(const ring_buffer_t *rb)
{
    if (!rb) {
        return 0;
    }

    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);
    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_acquire);

    return (size_t)(write_idx - read_idx);
}

/* Get available space in buffer */
size_t ring_buffer_space(const ring_buffer_t *rb)
{
    if (!rb) {
        return 0;
    }

    return RING_BUFFER_SIZE - ring_buffer_count(rb);
}

/* Get buffer statistics */
void ring_buffer_stats(const ring_buffer_t *rb,
                       uint64_t *total_writes,
                       uint64_t *total_reads,
                       uint64_t *total_drops)
{
    if (!rb) {
        return;
    }

    if (total_writes) {
        *total_writes = atomic_load_explicit(&rb->total_writes, memory_order_relaxed);
    }
    if (total_reads) {
        *total_reads = atomic_load_explicit(&rb->total_reads, memory_order_relaxed);
    }
    if (total_drops) {
        *total_drops = atomic_load_explicit(&rb->total_drops, memory_order_relaxed);
    }
}

/* Print buffer statistics */
void ring_buffer_print_stats(const ring_buffer_t *rb)
{
    if (!rb) {
        return;
    }

    uint64_t writes, reads, drops;
    ring_buffer_stats(rb, &writes, &reads, &drops);

    printf("\n========== RING BUFFER STATISTICS ==========\n");
    printf("Total writes:      %llu\n", (unsigned long long)writes);
    printf("Total reads:       %llu\n", (unsigned long long)reads);
    printf("Total drops:       %llu\n", (unsigned long long)drops);
    printf("Current count:     %zu / %d\n", ring_buffer_count(rb), RING_BUFFER_SIZE);
    printf("Available space:   %zu\n", ring_buffer_space(rb));

    if (writes > 0) {
        double drop_rate = (drops * 100.0) / writes;
        printf("Drop rate:         %.2f%%\n", drop_rate);
    }
    printf("============================================\n\n");
}

/* Reset buffer statistics */
void ring_buffer_reset_stats(ring_buffer_t *rb)
{
    if (!rb) {
        return;
    }

    atomic_store_explicit(&rb->total_writes, 0, memory_order_relaxed);
    atomic_store_explicit(&rb->total_reads, 0, memory_order_relaxed);
    atomic_store_explicit(&rb->total_drops, 0, memory_order_relaxed);
}

/* Helper: Create command message */
bool ring_message_create_command(ring_message_t *msg, uint32_t id, const char *command)
{
    if (!msg || !command) {
        return false;
    }

    size_t len = strlen(command);
    if (len >= MAX_MESSAGE_SIZE) {
        return false;
    }

    msg->type = MSG_COMMAND;
    msg->id = id;
    msg->payload_size = (uint32_t)len;
    strncpy(msg->payload, command, MAX_MESSAGE_SIZE - 1);
    msg->payload[MAX_MESSAGE_SIZE - 1] = '\0';
    msg->timestamp = ring_get_timestamp_ns();

    return true;
}

/* Helper: Create response message */
bool ring_message_create_response(ring_message_t *msg, uint32_t id, const char *response)
{
    if (!msg || !response) {
        return false;
    }

    size_t len = strlen(response);
    if (len >= MAX_MESSAGE_SIZE) {
        return false;
    }

    msg->type = MSG_RESPONSE;
    msg->id = id;
    msg->payload_size = (uint32_t)len;
    strncpy(msg->payload, response, MAX_MESSAGE_SIZE - 1);
    msg->payload[MAX_MESSAGE_SIZE - 1] = '\0';
    msg->timestamp = ring_get_timestamp_ns();

    return true;
}

/* Helper: Create query message */
bool ring_message_create_query(ring_message_t *msg, uint32_t id, const char *query)
{
    if (!msg || !query) {
        return false;
    }

    size_t len = strlen(query);
    if (len >= MAX_MESSAGE_SIZE) {
        return false;
    }

    msg->type = MSG_QUERY;
    msg->id = id;
    msg->payload_size = (uint32_t)len;
    strncpy(msg->payload, query, MAX_MESSAGE_SIZE - 1);
    msg->payload[MAX_MESSAGE_SIZE - 1] = '\0';
    msg->timestamp = ring_get_timestamp_ns();

    return true;
}

/* Get current timestamp in nanoseconds */
uint64_t ring_get_timestamp_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* Calculate message latency */
uint64_t ring_message_latency_ns(const ring_message_t *msg)
{
    if (!msg) {
        return 0;
    }

    uint64_t now = ring_get_timestamp_ns();
    return now - msg->timestamp;
}
