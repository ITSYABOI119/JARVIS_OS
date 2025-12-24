/*
 * JARVIS AI-OS - Dual Ring Buffer Unit Tests
 * Phase 2 Week 28
 *
 * Comprehensive test suite for bidirectional IPC system
 *
 * Tests:
 * 1. Initialization
 * 2. Query send/receive
 * 3. Response send/receive
 * 4. Cache lookup flow (query → response)
 * 5. Cache statistics flow
 * 6. Message validation
 * 7. Overflow handling
 * 8. Message ID tracking
 * 9. Payload parsing (HIT/MISS)
 * 10. Statistics parsing
 * 11. Concurrent access simulation
 * 12. Error handling
 */

#include "dual_ring_buffer.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Test framework */
#define TEST_PASS printf("  ✓ PASS\n")
#define TEST_FAIL(msg) do { printf("  ✗ FAIL: %s\n", msg); return false; } while(0)
#define RUN_TEST(test) do { \
    printf("\n[TEST] %s\n", #test); \
    if (test()) { \
        passed++; \
        printf("  ✓ PASS\n"); \
    } else { \
        failed++; \
        printf("  ✗ FAIL\n"); \
    } \
} while(0)

/* Global test counters */
static int passed = 0;
static int failed = 0;

/* Test 1: Initialization */
bool test_dual_ring_init(void)
{
    dual_ring_buffer_t drb;

    if (!dual_ring_init(&drb)) {
        TEST_FAIL("dual_ring_init failed");
    }

    /* Check magic number */
    if (drb.magic != DUAL_RING_MAGIC) {
        TEST_FAIL("Invalid magic number");
    }

    /* Check version */
    if (drb.version != DUAL_RING_VERSION) {
        TEST_FAIL("Invalid version");
    }

    /* Check validation */
    if (!dual_ring_validate(&drb)) {
        TEST_FAIL("Validation failed after init");
    }

    return true;
}

/* Test 2: Send and receive query */
bool test_send_recv_query(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    const char *test_query = "show cpu";
    char query_out[MAX_QUERY_LEN];
    uint32_t msg_id_out;

    /* Send query */
    uint32_t msg_id = dual_ring_send_query(&drb, test_query);
    if (msg_id == 0) {
        TEST_FAIL("Failed to send query");
    }

    /* Receive query */
    if (!dual_ring_recv_query(&drb, query_out, MAX_QUERY_LEN, &msg_id_out)) {
        TEST_FAIL("Failed to receive query");
    }

    /* Verify query content */
    if (strcmp(query_out, test_query) != 0) {
        TEST_FAIL("Query content mismatch");
    }

    /* Verify message ID */
    if (msg_id != msg_id_out) {
        TEST_FAIL("Message ID mismatch");
    }

    return true;
}

/* Test 3: Send and receive response (HIT) */
bool test_send_recv_response_hit(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    uint32_t msg_id = 42;
    const char *action = "show_cpu_info";
    trust_level_t trust = TRUST_AUTO;

    /* Send cache HIT response */
    if (!dual_ring_send_response(&drb, msg_id, true, action, trust)) {
        TEST_FAIL("Failed to send response");
    }

    /* Receive response */
    cache_response_t response;
    if (!dual_ring_recv_response(&drb, msg_id, &response)) {
        TEST_FAIL("Failed to receive response");
    }

    /* Verify response */
    if (!response.hit) {
        TEST_FAIL("Expected cache HIT");
    }

    if (strcmp(response.action, action) != 0) {
        TEST_FAIL("Action mismatch");
    }

    if (response.trust_level != trust) {
        TEST_FAIL("Trust level mismatch");
    }

    return true;
}

/* Test 4: Send and receive response (MISS) */
bool test_send_recv_response_miss(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    uint32_t msg_id = 43;

    /* Send cache MISS response */
    if (!dual_ring_send_response(&drb, msg_id, false, NULL, TRUST_AUTO)) {
        TEST_FAIL("Failed to send MISS response");
    }

    /* Receive response */
    cache_response_t response;
    if (!dual_ring_recv_response(&drb, msg_id, &response)) {
        TEST_FAIL("Failed to receive response");
    }

    /* Verify MISS */
    if (response.hit) {
        TEST_FAIL("Expected cache MISS");
    }

    return true;
}

/* Test 5: Full cache lookup flow (query → lookup → response) */
bool test_cache_lookup_flow(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    /* Step 1: Python sends query */
    const char *query = "list files";
    uint32_t query_msg_id = dual_ring_send_query(&drb, query);
    if (query_msg_id == 0) {
        TEST_FAIL("Failed to send query");
    }

    /* Step 2: seL4 receives query */
    char recv_query[MAX_QUERY_LEN];
    uint32_t recv_msg_id;
    if (!dual_ring_recv_query(&drb, recv_query, MAX_QUERY_LEN, &recv_msg_id)) {
        TEST_FAIL("Failed to receive query");
    }

    /* Verify query */
    if (strcmp(recv_query, query) != 0 || recv_msg_id != query_msg_id) {
        TEST_FAIL("Query mismatch");
    }

    /* Step 3: seL4 sends response */
    const char *action = "list_directory";
    if (!dual_ring_send_response(&drb, recv_msg_id, true, action, TRUST_AUTO)) {
        TEST_FAIL("Failed to send response");
    }

    /* Step 4: Python receives response */
    cache_response_t response;
    if (!dual_ring_recv_response(&drb, query_msg_id, &response)) {
        TEST_FAIL("Failed to receive response");
    }

    /* Verify response */
    if (!response.hit || strcmp(response.action, action) != 0) {
        TEST_FAIL("Response mismatch");
    }

    return true;
}

/* Test 6: Cache statistics flow */
bool test_cache_stats_flow(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    /* Step 1: Python sends stats request */
    uint32_t stats_msg_id = dual_ring_send_cache_stats_request(&drb);
    if (stats_msg_id == 0) {
        TEST_FAIL("Failed to send stats request");
    }

    /* Step 2: seL4 receives request (simulate by reading query ring) */
    ring_message_t msg;
    if (!ring_buffer_read(&drb.query_ring, &msg)) {
        TEST_FAIL("Failed to read stats request");
    }

    if (msg.type != MSG_CACHE_STATS) {
        TEST_FAIL("Wrong message type for stats request");
    }

    /* Step 3: seL4 sends stats response */
    cache_stats_t stats = {
        .entries_used = 258,
        .total_lookups = 1000,
        .cache_hits = 857,
        .cache_misses = 143
    };

    if (!dual_ring_send_cache_stats(&drb, msg.id, &stats)) {
        TEST_FAIL("Failed to send stats response");
    }

    /* Step 4: Python receives stats */
    cache_stats_response_t stats_response;
    if (!dual_ring_recv_cache_stats(&drb, stats_msg_id, &stats_response)) {
        TEST_FAIL("Failed to receive stats");
    }

    /* Verify stats */
    if (stats_response.entries_used != 258 ||
        stats_response.cache_hits != 857 ||
        stats_response.cache_misses != 143) {
        TEST_FAIL("Stats mismatch");
    }

    /* Verify hit rate calculation */
    double expected_hit_rate = 857.0 / 1000.0;
    if (stats_response.hit_rate < expected_hit_rate - 0.01 ||
        stats_response.hit_rate > expected_hit_rate + 0.01) {
        TEST_FAIL("Hit rate calculation incorrect");
    }

    return true;
}

/* Test 7: Message validation */
bool test_message_validation(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    /* Test NULL validation */
    if (dual_ring_validate(NULL)) {
        TEST_FAIL("NULL validation should fail");
    }

    /* Test corrupted magic number */
    drb.magic = 0xDEADBEEF;
    if (dual_ring_validate(&drb)) {
        TEST_FAIL("Corrupted magic should fail validation");
    }

    /* Restore and verify */
    drb.magic = DUAL_RING_MAGIC;
    if (!dual_ring_validate(&drb)) {
        TEST_FAIL("Valid buffer should pass validation");
    }

    return true;
}

/* Test 8: Overflow handling */
bool test_overflow_handling(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    /* Fill query ring to capacity */
    int sent_count = 0;
    while (sent_count < RING_BUFFER_SIZE) {
        uint32_t msg_id = dual_ring_send_query(&drb, "overflow test");
        if (msg_id == 0) {
            break;  /* Ring full */
        }
        sent_count++;
    }

    /* Try to overflow */
    uint32_t overflow_msg_id = dual_ring_send_query(&drb, "should fail");
    if (overflow_msg_id != 0) {
        TEST_FAIL("Overflow should return 0");
    }

    /* Drain some messages */
    for (int i = 0; i < 10; i++) {
        char query[MAX_QUERY_LEN];
        uint32_t msg_id;
        dual_ring_recv_query(&drb, query, MAX_QUERY_LEN, &msg_id);
    }

    /* Should be able to send again */
    uint32_t retry_msg_id = dual_ring_send_query(&drb, "retry test");
    if (retry_msg_id == 0) {
        TEST_FAIL("Should be able to send after draining");
    }

    return true;
}

/* Test 9: Message ID monotonicity */
bool test_message_id_monotonicity(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    uint32_t prev_id = 0;

    /* Send multiple queries and verify IDs are increasing */
    for (int i = 0; i < 10; i++) {
        uint32_t msg_id = dual_ring_send_query(&drb, "test query");
        if (msg_id == 0) {
            TEST_FAIL("Failed to send query");
        }

        if (msg_id <= prev_id) {
            TEST_FAIL("Message IDs not monotonically increasing");
        }

        prev_id = msg_id;
    }

    return true;
}

/* Test 10: Payload parsing edge cases */
bool test_payload_parsing_edge_cases(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    /* Test long action string (realistic max: 200 chars to fit "HIT|action|trust" in 256 bytes) */
    char long_action[200];
    memset(long_action, 'a', sizeof(long_action) - 1);
    long_action[sizeof(long_action) - 1] = '\0';

    uint32_t msg_id = 100;
    if (!dual_ring_send_response(&drb, msg_id, true, long_action, TRUST_AUTO)) {
        TEST_FAIL("Failed to send response with long action");
    }

    cache_response_t response;
    if (!dual_ring_recv_response(&drb, msg_id, &response)) {
        TEST_FAIL("Failed to receive response");
    }

    if (strlen(response.action) != strlen(long_action)) {
        TEST_FAIL("Long action truncated incorrectly");
    }

    return true;
}

/* Test 11: Concurrent access simulation */
bool test_concurrent_access_simulation(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    /* Simulate interleaved producer/consumer operations */
    for (int i = 0; i < 100; i++) {
        /* Producer (Python): Send query */
        char query[64];
        snprintf(query, sizeof(query), "query %d", i);
        uint32_t msg_id = dual_ring_send_query(&drb, query);
        if (msg_id == 0) {
            continue;  /* Ring full, skip */
        }

        /* Consumer (seL4): Receive and respond */
        char recv_query[MAX_QUERY_LEN];
        uint32_t recv_msg_id;
        if (dual_ring_recv_query(&drb, recv_query, MAX_QUERY_LEN, &recv_msg_id)) {
            /* Send response */
            dual_ring_send_response(&drb, recv_msg_id, true, "test_action", TRUST_AUTO);
        }

        /* Producer (Python): Receive response */
        cache_response_t response;
        dual_ring_recv_response_any(&drb, &msg_id, &response);
    }

    /* Verify no crashes/corruption */
    if (!dual_ring_validate(&drb)) {
        TEST_FAIL("Dual ring buffer corrupted after concurrent simulation");
    }

    return true;
}

/* Test 12: Error handling */
bool test_error_handling(void)
{
    dual_ring_buffer_t drb;
    dual_ring_init(&drb);

    /* Test NULL parameters */
    if (dual_ring_send_query(NULL, "test") != 0) {
        TEST_FAIL("NULL drb should return 0");
    }

    if (dual_ring_send_query(&drb, NULL) != 0) {
        TEST_FAIL("NULL query should return 0");
    }

    char query[MAX_QUERY_LEN];
    uint32_t msg_id;
    if (dual_ring_recv_query(NULL, query, MAX_QUERY_LEN, &msg_id)) {
        TEST_FAIL("NULL drb should fail");
    }

    if (dual_ring_recv_query(&drb, NULL, MAX_QUERY_LEN, &msg_id)) {
        TEST_FAIL("NULL output buffer should fail");
    }

    return true;
}

/* Main test runner */
int main(void)
{
    printf("======================================================================\n");
    printf("Dual Ring Buffer Unit Tests - Phase 2 Week 28\n");
    printf("======================================================================\n");

    RUN_TEST(test_dual_ring_init);
    RUN_TEST(test_send_recv_query);
    RUN_TEST(test_send_recv_response_hit);
    RUN_TEST(test_send_recv_response_miss);
    RUN_TEST(test_cache_lookup_flow);
    RUN_TEST(test_cache_stats_flow);
    RUN_TEST(test_message_validation);
    RUN_TEST(test_overflow_handling);
    RUN_TEST(test_message_id_monotonicity);
    RUN_TEST(test_payload_parsing_edge_cases);
    RUN_TEST(test_concurrent_access_simulation);
    RUN_TEST(test_error_handling);

    printf("\n======================================================================\n");
    printf("Test Results: %d passed, %d failed\n", passed, failed);
    printf("======================================================================\n");

    if (failed == 0) {
        printf("\n✅ ALL TESTS PASSED\n\n");
        return 0;
    } else {
        printf("\n❌ SOME TESTS FAILED\n\n");
        return 1;
    }
}
