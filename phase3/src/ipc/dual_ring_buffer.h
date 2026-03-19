/*
 * JARVIS AI-OS - Dual Ring Buffer (Bidirectional IPC)
 * Phase 2 Week 28
 *
 * Bidirectional communication between Python shell and seL4 kernel
 * using separate query and response ring buffers.
 *
 * Architecture:
 *   Python → seL4:  Query Ring (Python writes, seL4 reads)
 *   seL4 → Python:  Response Ring (seL4 writes, Python reads)
 *
 * Memory Layout:
 *   Offset 0:      Query Ring (~283KB)
 *   Offset 283648: Response Ring (~283KB)
 *   Total: ~567KB shared memory
 */

#ifndef DUAL_RING_BUFFER_H
#define DUAL_RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "ring_buffer.h"
#include "decision_cache.h"

/* Dual ring buffer magic number for validation */
#define DUAL_RING_MAGIC 0x4A415256  /* "JARV" in hex */
#define DUAL_RING_VERSION 1

/* Memory offsets */
#define QUERY_RING_OFFSET 0
#define RESPONSE_RING_OFFSET (sizeof(ring_buffer_t))

/* Calculate total shared memory size */
#define DUAL_RING_SHM_SIZE (2 * sizeof(ring_buffer_t) + sizeof(uint32_t) * 2)

/**
 * Dual ring buffer structure
 *
 * Contains two independent SPSC ring buffers:
 * - query_ring: Python writes queries, seL4 reads
 * - response_ring: seL4 writes responses, Python reads
 */
typedef struct {
    uint32_t magic;                 /* Magic number for validation */
    uint32_t version;               /* Protocol version */
    ring_buffer_t query_ring;       /* Python → seL4 (cache lookups) */
    ring_buffer_t response_ring;    /* seL4 → Python (cache results) */
} dual_ring_buffer_t;

/* Cache response structure (parsed from MSG_CACHE_RESPONSE payload) */
typedef struct {
    bool hit;                       /* true if cache hit, false if miss */
    char action[MAX_ACTION_LEN];    /* Action to execute (if hit) */
    trust_level_t trust_level;      /* Trust level (if hit) */
} cache_response_t;

/* Cache statistics structure (parsed from MSG_CACHE_STATS response) */
typedef struct {
    uint32_t capacity;              /* Max cache entries */
    uint32_t entries_used;          /* Current patterns loaded */
    uint64_t total_lookups;         /* All-time lookups */
    uint64_t cache_hits;            /* All-time hits */
    uint64_t cache_misses;          /* All-time misses */
    uint64_t evictions;             /* LRU evictions */
    double hit_rate;                /* Calculated: hits / total */
} cache_stats_response_t;

/* Public API */

/**
 * Initialize dual ring buffer
 * @param drb Pointer to dual ring buffer structure
 * @return true on success, false on failure
 */
bool dual_ring_init(dual_ring_buffer_t *drb);

/**
 * Validate dual ring buffer (check magic number)
 * @param drb Pointer to dual ring buffer
 * @return true if valid, false otherwise
 */
bool dual_ring_validate(const dual_ring_buffer_t *drb);

/* Query ring functions (Python → seL4) */

/**
 * Send cache lookup query (Python side)
 * @param drb Pointer to dual ring buffer
 * @param query Normalized query string
 * @return Message ID (for matching response), or 0 on failure
 */
uint32_t dual_ring_send_query(dual_ring_buffer_t *drb, const char *query);

/**
 * Receive cache lookup query (seL4 side)
 * @param drb Pointer to dual ring buffer
 * @param query_out Output buffer for query string
 * @param query_max_len Maximum length of query buffer
 * @param msg_id_out Output for message ID
 * @return true if query received, false if queue empty
 */
bool dual_ring_recv_query(dual_ring_buffer_t *drb,
                          char *query_out,
                          size_t query_max_len,
                          uint32_t *msg_id_out);

/* Response ring functions (seL4 → Python) */

/**
 * Send cache response (seL4 side)
 * @param drb Pointer to dual ring buffer
 * @param msg_id Message ID (from original query)
 * @param hit true if cache hit, false if miss
 * @param action Action string (if hit), ignored if miss
 * @param trust_level Trust level (if hit), ignored if miss
 * @return true on success, false if queue full
 */
bool dual_ring_send_response(dual_ring_buffer_t *drb,
                             uint32_t msg_id,
                             bool hit,
                             const char *action,
                             trust_level_t trust_level);

/**
 * Receive cache response (Python side)
 * @param drb Pointer to dual ring buffer
 * @param msg_id Expected message ID (for matching)
 * @param response_out Output buffer for parsed response
 * @return true if response received, false if queue empty
 */
bool dual_ring_recv_response(dual_ring_buffer_t *drb,
                             uint32_t msg_id,
                             cache_response_t *response_out);

/**
 * Receive any cache response (Python side, no ID matching)
 * Useful for asynchronous response handling
 * @param drb Pointer to dual ring buffer
 * @param msg_id_out Output for message ID
 * @param response_out Output buffer for parsed response
 * @return true if response received, false if queue empty
 */
bool dual_ring_recv_response_any(dual_ring_buffer_t *drb,
                                 uint32_t *msg_id_out,
                                 cache_response_t *response_out);

/* Cache statistics request/response (Python ↔ seL4) */

/**
 * Send cache statistics request (Python side)
 * @param drb Pointer to dual ring buffer
 * @return Message ID (for matching response), or 0 on failure
 */
uint32_t dual_ring_send_cache_stats_request(dual_ring_buffer_t *drb);

/**
 * Send cache statistics response (seL4 side)
 * @param drb Pointer to dual ring buffer
 * @param msg_id Message ID (from request)
 * @param stats Cache statistics to send
 * @return true on success, false if queue full
 */
bool dual_ring_send_cache_stats(dual_ring_buffer_t *drb,
                                uint32_t msg_id,
                                const cache_stats_t *stats);

/**
 * Receive cache statistics response (Python side)
 * @param drb Pointer to dual ring buffer
 * @param msg_id Expected message ID
 * @param stats_out Output buffer for parsed statistics
 * @return true if response received, false if queue empty
 */
bool dual_ring_recv_cache_stats(dual_ring_buffer_t *drb,
                                uint32_t msg_id,
                                cache_stats_response_t *stats_out);

/* Utility functions */

/**
 * Get current message ID counter (for debugging)
 * @param drb Pointer to dual ring buffer
 * @return Current message ID
 */
uint32_t dual_ring_get_message_id(const dual_ring_buffer_t *drb);

/**
 * Print dual ring buffer statistics (for debugging)
 * @param drb Pointer to dual ring buffer
 */
void dual_ring_print_stats(const dual_ring_buffer_t *drb);

/**
 * Reset all statistics counters
 * @param drb Pointer to dual ring buffer
 */
void dual_ring_reset_stats(dual_ring_buffer_t *drb);

#endif /* DUAL_RING_BUFFER_H */
