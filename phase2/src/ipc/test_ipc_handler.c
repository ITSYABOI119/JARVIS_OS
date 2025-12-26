/*
 * JARVIS AI-OS - IPC Handler Unit Tests
 * Phase 2 Week 31
 *
 * Tests the IPC handler polling mode implementation.
 *
 * Tests:
 * 1. Handler initialization
 * 2. NULL parameter rejection
 * 3. Poll once when queue empty
 * 4. Poll once with cache lookup
 * 5. Poll once with cache stats
 * 6. Unknown message type handling
 * 7. Statistics tracking
 * 8. Handler stop
 * 9. Init polling mode
 * 10. Spawn thread deprecated behavior
 *
 * Compile: gcc -O2 -I../../../phase1/src/cache -I../../../phase1/src/ipc \
 *          ipc_handler.c dual_ring_buffer.c \
 *          ../../../phase1/src/ipc/ring_buffer.c \
 *          ../../../phase1/src/cache/decision_cache.c \
 *          ../../../phase1/src/cache/cache_patterns.c \
 *          test_ipc_handler.c -o test_ipc_handler
 */

#include "ipc_handler.h"
#include "dual_ring_buffer.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/* Test framework macros */
#define TEST_PASS() do { printf("  [PASS]\n"); return true; } while(0)
#define TEST_FAIL(msg) do { printf("  [FAIL] %s\n", msg); return false; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) TEST_FAIL(msg); } while(0)

/* Global test counters */
static int tests_passed = 0;
static int tests_failed = 0;

/* Run a test and track results */
#define RUN_TEST(test_func) do { \
    printf("\n========== %s ==========\n", #test_func); \
    if (test_func()) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
    } \
} while(0)

/*
 * Test 1: Handler initialization with valid parameters
 */
static bool test_handler_init(void)
{
    printf("Testing: Handler initialization\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    /* Initialize dependencies */
    if (!cache_init(&cache)) {
        TEST_FAIL("Failed to initialize cache");
    }
    if (!dual_ring_init(&drb)) {
        TEST_FAIL("Failed to initialize dual ring buffer");
    }

    /* Initialize handler */
    bool result = ipc_handler_init(&handler, &cache, &drb);
    ASSERT(result, "ipc_handler_init returned false");

    /* Verify state */
    ASSERT(handler.cache == &cache, "cache pointer not set");
    ASSERT(handler.drb == &drb, "drb pointer not set");
    ASSERT(handler.running == true, "running flag not set");
    ASSERT(handler.queries_processed == 0, "queries_processed not zero");
    ASSERT(handler.stats_requests == 0, "stats_requests not zero");
    ASSERT(handler.errors == 0, "errors not zero");

    printf("  Handler initialized correctly\n");
    TEST_PASS();
}

/*
 * Test 2: NULL parameter rejection
 */
static bool test_handler_init_null(void)
{
    printf("Testing: NULL parameter rejection\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    dual_ring_init(&drb);

    /* NULL state should fail */
    bool result1 = ipc_handler_init(NULL, &cache, &drb);
    ASSERT(!result1, "Should reject NULL state");

    /* NULL cache should fail */
    bool result2 = ipc_handler_init(&handler, NULL, &drb);
    ASSERT(!result2, "Should reject NULL cache");

    /* NULL drb should fail */
    bool result3 = ipc_handler_init(&handler, &cache, NULL);
    ASSERT(!result3, "Should reject NULL drb");

    printf("  All NULL parameters rejected\n");
    TEST_PASS();
}

/*
 * Test 3: Poll once when queue empty returns false
 */
static bool test_poll_once_empty(void)
{
    printf("Testing: Poll once with empty queue\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    dual_ring_init(&drb);
    ipc_handler_init(&handler, &cache, &drb);
    ipc_handler_init_polling(&handler);

    /* Poll with empty queue should return false */
    bool result = ipc_handler_poll_once(&handler);
    ASSERT(!result, "Should return false for empty queue");

    /* Stats should be unchanged */
    ASSERT(handler.queries_processed == 0, "queries should be 0");
    ASSERT(handler.errors == 0, "errors should be 0");

    printf("  Empty queue handled correctly\n");
    TEST_PASS();
}

/*
 * Test 4: Poll once with cache lookup message
 */
static bool test_poll_once_cache_lookup(void)
{
    printf("Testing: Poll once with cache lookup\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    cache_load_extended_patterns(&cache);  /* Load patterns for lookup */
    dual_ring_init(&drb);
    ipc_handler_init(&handler, &cache, &drb);
    ipc_handler_init_polling(&handler);

    /* Send a query to the queue (simulating Python side) */
    uint32_t msg_id = dual_ring_send_query(&drb, "list files");
    ASSERT(msg_id != 0, "Failed to send query");

    /* Poll should process the message */
    bool result = ipc_handler_poll_once(&handler);
    ASSERT(result, "Should return true when message processed");

    /* Check that query was processed */
    ASSERT(handler.queries_processed == 1, "queries_processed should be 1");

    /* Check that response was sent */
    cache_response_t response;
    uint32_t resp_msg_id;
    bool got_response = dual_ring_recv_response_any(&drb, &resp_msg_id, &response);
    ASSERT(got_response, "Should have received response");
    ASSERT(resp_msg_id == msg_id, "Response msg_id should match");

    printf("  Cache lookup processed correctly\n");
    TEST_PASS();
}

/*
 * Test 5: Poll once with cache stats request
 */
static bool test_poll_once_cache_stats(void)
{
    printf("Testing: Poll once with cache stats request\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    dual_ring_init(&drb);
    ipc_handler_init(&handler, &cache, &drb);
    ipc_handler_init_polling(&handler);

    /* Send a stats request (simulate by writing directly to query ring) */
    ring_message_t msg;
    msg.type = MSG_CACHE_STATS;
    msg.id = 42;
    msg.payload_size = 0;
    msg.timestamp = 0;
    ring_buffer_write(&drb.query_ring, &msg);

    /* Poll should process the message */
    bool result = ipc_handler_poll_once(&handler);
    ASSERT(result, "Should return true when message processed");

    /* Check stats request was processed */
    ASSERT(handler.stats_requests == 1, "stats_requests should be 1");

    printf("  Cache stats request processed correctly\n");
    TEST_PASS();
}

/*
 * Test 6: Unknown message type increments error counter
 */
static bool test_poll_unknown_type(void)
{
    printf("Testing: Unknown message type handling\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    dual_ring_init(&drb);
    ipc_handler_init(&handler, &cache, &drb);
    ipc_handler_init_polling(&handler);

    /* Send an unknown message type */
    ring_message_t msg;
    msg.type = 0x99;  /* Unknown type */
    msg.id = 1;
    msg.payload_size = 0;
    msg.timestamp = 0;
    ring_buffer_write(&drb.query_ring, &msg);

    /* Poll should process but record error */
    bool result = ipc_handler_poll_once(&handler);
    ASSERT(result, "Should still return true (message was processed)");
    ASSERT(handler.errors == 1, "errors should be 1");

    printf("  Unknown message type handled correctly\n");
    TEST_PASS();
}

/*
 * Test 7: Statistics tracking
 */
static bool test_handler_statistics(void)
{
    printf("Testing: Statistics tracking\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    cache_load_extended_patterns(&cache);
    dual_ring_init(&drb);
    ipc_handler_init(&handler, &cache, &drb);
    ipc_handler_init_polling(&handler);

    /* Send multiple messages */
    dual_ring_send_query(&drb, "help");
    dual_ring_send_query(&drb, "status");
    dual_ring_send_query(&drb, "cache stats");

    /* Process all messages */
    while (ipc_handler_poll_once(&handler)) {
        /* Continue polling */
    }

    /* Verify statistics */
    uint32_t queries, stats_reqs, errors;
    ipc_handler_get_stats(&handler, &queries, &stats_reqs, &errors);

    ASSERT(queries == 3, "Should have 3 queries processed");
    ASSERT(errors == 0, "Should have 0 errors");

    printf("  Statistics tracked correctly: queries=%u, errors=%u\n", queries, errors);
    TEST_PASS();
}

/*
 * Test 8: Handler stop clears running flag
 */
static bool test_handler_stop(void)
{
    printf("Testing: Handler stop\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    dual_ring_init(&drb);
    ipc_handler_init(&handler, &cache, &drb);

    /* Verify running is true */
    ASSERT(handler.running == true, "running should be true after init");

    /* Stop handler */
    ipc_handler_stop(&handler);

    /* Verify running is false */
    ASSERT(handler.running == false, "running should be false after stop");

    /* Poll should return false when not running */
    bool result = ipc_handler_poll_once(&handler);
    ASSERT(!result, "Poll should return false when stopped");

    printf("  Handler stopped correctly\n");
    TEST_PASS();
}

/*
 * Test 9: Init polling mode
 */
static bool test_init_polling_mode(void)
{
    printf("Testing: Init polling mode\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    dual_ring_init(&drb);
    ipc_handler_init(&handler, &cache, &drb);

    /* Init polling should succeed */
    bool result = ipc_handler_init_polling(&handler);
    ASSERT(result, "ipc_handler_init_polling should succeed");

    /* NULL should fail */
    bool result_null = ipc_handler_init_polling(NULL);
    ASSERT(!result_null, "Should reject NULL");

    printf("  Polling mode initialized correctly\n");
    TEST_PASS();
}

/*
 * Test 10: Spawn thread deprecated behavior
 */
static bool test_spawn_thread_deprecated(void)
{
    printf("Testing: Spawn thread deprecated\n");

    decision_cache_t cache;
    dual_ring_buffer_t drb;
    ipc_handler_state_t handler;

    cache_init(&cache);
    dual_ring_init(&drb);
    ipc_handler_init(&handler, &cache, &drb);

    /* Spawn thread should still work (calls init_polling internally) */
    bool result = ipc_handler_spawn_thread(&handler);
    ASSERT(result, "spawn_thread should succeed (deprecated but functional)");

    printf("  Deprecated spawn_thread still works\n");
    TEST_PASS();
}

/*
 * Main test runner
 */
int main(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   JARVIS AI-OS IPC Handler Unit Tests                ║\n");
    printf("║   Phase 2 Week 31                                    ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");

    /* Run all tests */
    RUN_TEST(test_handler_init);
    RUN_TEST(test_handler_init_null);
    RUN_TEST(test_poll_once_empty);
    RUN_TEST(test_poll_once_cache_lookup);
    RUN_TEST(test_poll_once_cache_stats);
    RUN_TEST(test_poll_unknown_type);
    RUN_TEST(test_handler_statistics);
    RUN_TEST(test_handler_stop);
    RUN_TEST(test_init_polling_mode);
    RUN_TEST(test_spawn_thread_deprecated);

    /* Print summary */
    printf("\n");
    printf("══════════════════════════════════════════════════════════\n");
    printf("  TEST SUMMARY\n");
    printf("══════════════════════════════════════════════════════════\n");
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Rate:   %.1f%%\n", 100.0 * tests_passed / (tests_passed + tests_failed));
    printf("══════════════════════════════════════════════════════════\n\n");

    return tests_failed == 0 ? 0 : 1;
}
