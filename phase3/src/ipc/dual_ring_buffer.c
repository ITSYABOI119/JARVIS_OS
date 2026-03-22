/*
 * JARVIS AI-OS - Dual Ring Buffer Implementation
 * Phase 2 Week 28
 *
 * Bidirectional IPC using separate query and response ring buffers
 */

#include "dual_ring_buffer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Global message ID counter (atomic in multi-threaded environment) */
static uint32_t g_message_id_counter = 1;

/* Initialize dual ring buffer */
bool dual_ring_init(dual_ring_buffer_t *drb)
{
    if (!drb) {
        return false;
    }

    /* Set magic number and version */
    drb->magic = DUAL_RING_MAGIC;
    drb->version = DUAL_RING_VERSION;

    /* Initialize query ring */
    ring_buffer_init(&drb->query_ring);

    /* Initialize response ring */
    ring_buffer_init(&drb->response_ring);

    /* Reset message ID counter */
    g_message_id_counter = 1;

    return true;
}

/* Validate dual ring buffer */
bool dual_ring_validate(const dual_ring_buffer_t *drb)
{
    if (!drb) {
        return false;
    }

    /* Check magic number */
    if (drb->magic != DUAL_RING_MAGIC) {
        return false;
    }

    /* Check version */
    if (drb->version != DUAL_RING_VERSION) {
        return false;
    }

    return true;
}

/* Send cache lookup query (Python → seL4) */
uint32_t dual_ring_send_query(dual_ring_buffer_t *drb, const char *query)
{
    if (!drb || !query) {
        return 0;
    }

    /* Validate buffer */
    if (!dual_ring_validate(drb)) {
        return 0;
    }

    /* Get next message ID */
    uint32_t msg_id = g_message_id_counter++;

    /* Create MSG_CACHE_LOOKUP message */
    ring_message_t msg;
    if (!ring_message_create_query(&msg, msg_id, query)) {
        return 0;
    }

    /* Override type to MSG_CACHE_LOOKUP */
    msg.type = MSG_CACHE_LOOKUP;

    /* Write to query ring */
    if (!ring_buffer_write(&drb->query_ring, &msg)) {
        /* Queue full */
        return 0;
    }

    return msg_id;
}

/* Receive cache lookup query (seL4 side) */
bool dual_ring_recv_query(dual_ring_buffer_t *drb,
                          char *query_out,
                          size_t query_max_len,
                          uint32_t *msg_id_out)
{
    if (!drb || !query_out || !msg_id_out) {
        return false;
    }

    /* Validate buffer */
    if (!dual_ring_validate(drb)) {
        return false;
    }

    /* Read from query ring */
    ring_message_t msg;
    if (!ring_buffer_read(&drb->query_ring, &msg)) {
        /* Queue empty */
        return false;
    }

    /* Verify message type */
    if (msg.type != MSG_CACHE_LOOKUP && msg.type != MSG_CACHE_STATS) {
        /* Wrong message type - skip */
        return false;
    }

    /* Extract query string */
    size_t copy_len = (msg.payload_size < query_max_len) ? msg.payload_size : (query_max_len - 1);
    memcpy(query_out, msg.payload, copy_len);
    query_out[copy_len] = '\0';

    /* Return message ID */
    *msg_id_out = msg.id;

    return true;
}

/* Send cache response (seL4 → Python) */
bool dual_ring_send_response(dual_ring_buffer_t *drb,
                             uint32_t msg_id,
                             bool hit,
                             const char *action,
                             trust_level_t trust_level)
{
    if (!drb) {
        return false;
    }

    /* Validate buffer */
    if (!dual_ring_validate(drb)) {
        return false;
    }

    /* Format response payload */
    char payload[MAX_MESSAGE_SIZE];
    if (hit) {
        /* Cache HIT: "HIT|action|trust_level" */
        if (!action) {
            return false;
        }
        snprintf(payload, MAX_MESSAGE_SIZE, "HIT|%s|%d", action, (int)trust_level);
    } else {
        /* Cache MISS: "MISS" */
        strncpy(payload, "MISS", MAX_MESSAGE_SIZE);
    }

    /* Create MSG_CACHE_RESPONSE message */
    ring_message_t msg;
    if (!ring_message_create_response(&msg, msg_id, payload)) {
        return false;
    }

    /* Override type to MSG_CACHE_RESPONSE */
    msg.type = MSG_CACHE_RESPONSE;

    /* Write to response ring */
    if (!ring_buffer_write(&drb->response_ring, &msg)) {
        /* Queue full */
        return false;
    }

    return true;
}

/* Parse cache response payload */
static bool parse_cache_response(const char *payload, cache_response_t *response)
{
    if (!payload || !response) {
        return false;
    }

    /* Check for MISS */
    if (strcmp(payload, "MISS") == 0) {
        response->hit = false;
        response->action[0] = '\0';
        response->trust_level = TRUST_AUTO;
        return true;
    }

    /* Parse HIT response: "HIT|action|trust" */
    if (strncmp(payload, "HIT|", 4) != 0) {
        return false;
    }

    /* Find delimiters */
    const char *action_start = payload + 4;  /* After "HIT|" */
    const char *trust_start = strchr(action_start, '|');

    if (!trust_start) {
        return false;
    }

    /* Extract action */
    size_t action_len = trust_start - action_start;
    if (action_len >= MAX_ACTION_LEN) {
        return false;
    }
    memcpy(response->action, action_start, action_len);
    response->action[action_len] = '\0';

    /* SEC-002: Extract trust level with bounds check */
    int trust_int = atoi(trust_start + 1);
    if (trust_int < 0 || trust_int > 5) {
        return false;  /* Reject invalid trust level */
    }
    response->trust_level = (trust_level_t)trust_int;

    response->hit = true;
    return true;
}

/* Receive cache response (Python side, with ID matching) */
bool dual_ring_recv_response(dual_ring_buffer_t *drb,
                             uint32_t msg_id,
                             cache_response_t *response_out)
{
    if (!drb || !response_out) {
        return false;
    }

    /* Validate buffer */
    if (!dual_ring_validate(drb)) {
        return false;
    }

    /* Read from response ring */
    ring_message_t msg;
    if (!ring_buffer_read(&drb->response_ring, &msg)) {
        /* Queue empty */
        return false;
    }

    /* Verify message type */
    if (msg.type != MSG_CACHE_RESPONSE) {
        /* Wrong message type */
        return false;
    }

    /* SEC-026: Reject mismatched message ID */
    if (msg.id != msg_id) {
        return false;
    }

    /* SEC-023: Ensure null termination before string parsing */
    msg.payload[msg.payload_size < MAX_MESSAGE_SIZE ? msg.payload_size : MAX_MESSAGE_SIZE - 1] = '\0';

    /* Parse response payload */
    if (!parse_cache_response(msg.payload, response_out)) {
        return false;
    }

    return true;
}

/* Receive any cache response (Python side, no ID matching) */
bool dual_ring_recv_response_any(dual_ring_buffer_t *drb,
                                 uint32_t *msg_id_out,
                                 cache_response_t *response_out)
{
    if (!drb || !msg_id_out || !response_out) {
        return false;
    }

    /* Validate buffer */
    if (!dual_ring_validate(drb)) {
        return false;
    }

    /* Read from response ring */
    ring_message_t msg;
    if (!ring_buffer_read(&drb->response_ring, &msg)) {
        /* Queue empty */
        return false;
    }

    /* Verify message type */
    if (msg.type != MSG_CACHE_RESPONSE) {
        /* Wrong message type */
        return false;
    }

    /* Return message ID */
    *msg_id_out = msg.id;

    /* Parse response payload */
    if (!parse_cache_response(msg.payload, response_out)) {
        return false;
    }

    return true;
}

/* Send cache statistics request (Python → seL4) */
uint32_t dual_ring_send_cache_stats_request(dual_ring_buffer_t *drb)
{
    if (!drb) {
        return 0;
    }

    /* Validate buffer */
    if (!dual_ring_validate(drb)) {
        return 0;
    }

    /* Get next message ID */
    uint32_t msg_id = g_message_id_counter++;

    /* Create MSG_CACHE_STATS message (empty payload) */
    ring_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_CACHE_STATS;
    msg.id = msg_id;
    msg.payload_size = 0;
    msg.timestamp = ring_get_timestamp_ns();

    /* Write to query ring */
    if (!ring_buffer_write(&drb->query_ring, &msg)) {
        /* Queue full */
        return 0;
    }

    return msg_id;
}

/* Send cache statistics response (seL4 → Python) */
bool dual_ring_send_cache_stats(dual_ring_buffer_t *drb,
                                uint32_t msg_id,
                                const cache_stats_t *stats)
{
    if (!drb || !stats) {
        return false;
    }

    /* Validate buffer */
    if (!dual_ring_validate(drb)) {
        return false;
    }

    /* Format stats payload: "capacity|used|lookups|hits|misses|evictions" */
    char payload[MAX_MESSAGE_SIZE];
    snprintf(payload, MAX_MESSAGE_SIZE, "%u|%u|%llu|%llu|%llu|0",
             CACHE_SIZE,                          /* capacity */
             stats->entries_used,                 /* entries used */
             (unsigned long long)stats->total_lookups,
             (unsigned long long)stats->cache_hits,
             (unsigned long long)stats->cache_misses);

    /* Create MSG_RESPONSE message */
    ring_message_t msg;
    if (!ring_message_create_response(&msg, msg_id, payload)) {
        return false;
    }

    /* Write to response ring */
    if (!ring_buffer_write(&drb->response_ring, &msg)) {
        /* Queue full */
        return false;
    }

    return true;
}

/* Parse cache statistics response payload */
static bool parse_cache_stats(const char *payload, cache_stats_response_t *stats)
{
    if (!payload || !stats) {
        return false;
    }

    /* Parse: "capacity|used|lookups|hits|misses|evictions" */
    int parsed = sscanf(payload, "%u|%u|%llu|%llu|%llu|%llu",
                       &stats->capacity,
                       &stats->entries_used,
                       (unsigned long long *)&stats->total_lookups,
                       (unsigned long long *)&stats->cache_hits,
                       (unsigned long long *)&stats->cache_misses,
                       (unsigned long long *)&stats->evictions);

    if (parsed != 6) {
        return false;
    }

    /* Calculate hit rate */
    if (stats->total_lookups > 0) {
        stats->hit_rate = (double)stats->cache_hits / (double)stats->total_lookups;
    } else {
        stats->hit_rate = 0.0;
    }

    return true;
}

/* Receive cache statistics response (Python side) */
bool dual_ring_recv_cache_stats(dual_ring_buffer_t *drb,
                                uint32_t msg_id,
                                cache_stats_response_t *stats_out)
{
    if (!drb || !stats_out) {
        return false;
    }

    /* Validate buffer */
    if (!dual_ring_validate(drb)) {
        return false;
    }

    /* Read from response ring */
    ring_message_t msg;
    if (!ring_buffer_read(&drb->response_ring, &msg)) {
        /* Queue empty */
        return false;
    }

    /* Verify message type (MSG_RESPONSE for stats) */
    if (msg.type != MSG_RESPONSE) {
        /* Wrong message type */
        return false;
    }

    /* Check message ID */
    if (msg.id != msg_id) {
        /* ID mismatch */
        return false;
    }

    /* Parse stats payload */
    if (!parse_cache_stats(msg.payload, stats_out)) {
        return false;
    }

    return true;
}

/* Get current message ID */
uint32_t dual_ring_get_message_id(const dual_ring_buffer_t *drb)
{
    (void)drb;  /* Unused - message ID is global */
    return g_message_id_counter;
}

/* Print dual ring buffer statistics */
void dual_ring_print_stats(const dual_ring_buffer_t *drb)
{
    if (!drb) {
        return;
    }

    printf("\n========== DUAL RING BUFFER STATISTICS ==========\n");

    /* Validate */
    if (dual_ring_validate(drb)) {
        printf("Magic:    0x%08X (valid)\n", drb->magic);
        printf("Version:  %u\n", drb->version);
    } else {
        printf("Invalid dual ring buffer!\n");
        return;
    }

    /* Query ring stats */
    printf("\nQuery Ring (Python → seL4):\n");
    ring_buffer_print_stats(&drb->query_ring);

    /* Response ring stats */
    printf("\nResponse Ring (seL4 → Python):\n");
    ring_buffer_print_stats(&drb->response_ring);

    printf("=================================================\n\n");
}

/* Reset statistics */
void dual_ring_reset_stats(dual_ring_buffer_t *drb)
{
    if (!drb) {
        return;
    }

    ring_buffer_reset_stats(&drb->query_ring);
    ring_buffer_reset_stats(&drb->response_ring);

    /* Reset message ID counter */
    g_message_id_counter = 1;
}
