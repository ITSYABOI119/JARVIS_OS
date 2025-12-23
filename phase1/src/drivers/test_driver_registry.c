/**
 * JARVIS AI-OS Phase 1 - Driver Registry Test
 * Week 23: VirtIO Block Driver Implementation
 *
 * Unit tests for driver registry functionality.
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#include "driver_registry.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* Mock driver private data */
typedef struct {
    int init_count;
    int start_count;
    int stop_count;
    int read_count;
    int write_count;
} mock_driver_data_t;

/* Mock driver operations */
static int mock_driver_init(void* private_data) {
    mock_driver_data_t* data = (mock_driver_data_t*)private_data;
    data->init_count++;
    printf("[MockDriver] init called\n");
    return 0;
}

static int mock_driver_start(void* private_data) {
    mock_driver_data_t* data = (mock_driver_data_t*)private_data;
    data->start_count++;
    printf("[MockDriver] start called\n");
    return 0;
}

static int mock_driver_stop(void* private_data) {
    mock_driver_data_t* data = (mock_driver_data_t*)private_data;
    data->stop_count++;
    printf("[MockDriver] stop called\n");
    return 0;
}

static int mock_driver_read(void* private_data, uint64_t lba, uint32_t count, void* buffer) {
    mock_driver_data_t* data = (mock_driver_data_t*)private_data;
    data->read_count++;
    printf("[MockDriver] read called: LBA=%lu, count=%u\n", lba, count);

    /* Fill buffer with mock data */
    memset(buffer, 0xAA, count * 512);
    return count * 512;
}

static int mock_driver_write(void* private_data, uint64_t lba, uint32_t count, const void* buffer) {
    mock_driver_data_t* data = (mock_driver_data_t*)private_data;
    data->write_count++;
    printf("[MockDriver] write called: LBA=%lu, count=%u\n", lba, count);
    return count * 512;
}

static void mock_driver_cleanup(void* private_data) {
    printf("[MockDriver] cleanup called\n");
}

/* Test 1: Registry initialization */
static void test_registry_init(void) {
    printf("\n=== Test 1: Registry Initialization ===\n");

    int result = driver_registry_init();
    assert(result == 0 && "Registry init should succeed");

    /* Double init should fail */
    result = driver_registry_init();
    assert(result == -1 && "Double init should fail");

    printf("✓ Test 1 PASSED\n");
}

/* Test 2: Driver registration */
static void test_driver_registration(void) {
    printf("\n=== Test 2: Driver Registration ===\n");

    mock_driver_data_t data = {0};
    driver_ops_t ops = {
        .init = mock_driver_init,
        .start = mock_driver_start,
        .stop = mock_driver_stop,
        .suspend = NULL,
        .resume = NULL,
        .read = mock_driver_read,
        .write = mock_driver_write,
        .cleanup = mock_driver_cleanup
    };

    int result = driver_register("mock-driver", &ops, &data);
    assert(result == 0 && "Driver registration should succeed");
    assert(data.init_count == 1 && "Driver init should be called");

    /* Duplicate registration should fail */
    result = driver_register("mock-driver", &ops, &data);
    assert(result == -1 && "Duplicate registration should fail");

    printf("✓ Test 2 PASSED\n");
}

/* Test 3: Driver state transitions */
static void test_driver_state_transitions(void) {
    printf("\n=== Test 3: Driver State Transitions ===\n");

    mock_driver_data_t data2 = {0};
    driver_ops_t ops = {
        .init = mock_driver_init,
        .start = mock_driver_start,
        .stop = mock_driver_stop,
        .suspend = NULL,
        .resume = NULL,
        .read = mock_driver_read,
        .write = mock_driver_write,
        .cleanup = mock_driver_cleanup
    };

    int result = driver_register("test-driver", &ops, &data2);
    assert(result == 0 && "Driver registration should succeed");

    /* Start driver: LOADED -> ACTIVE */
    result = driver_start("test-driver");
    assert(result == 0 && "Driver start should succeed");
    assert(data2.start_count == 1 && "Driver start should be called");

    /* Get driver info */
    driver_info_t info;
    result = driver_get_info("test-driver", &info);
    assert(result == 0 && "Get info should succeed");
    assert(info.state == DRIVER_ACTIVE && "Driver should be ACTIVE");

    /* Stop driver: ACTIVE -> LOADED */
    result = driver_stop("test-driver");
    assert(result == 0 && "Driver stop should succeed");
    assert(data2.stop_count == 1 && "Driver stop should be called");

    result = driver_get_info("test-driver", &info);
    assert(result == 0 && "Get info should succeed");
    assert(info.state == DRIVER_LOADED && "Driver should be LOADED");

    printf("✓ Test 3 PASSED\n");
}

/* Test 4: Driver I/O operations */
static void test_driver_io(void) {
    printf("\n=== Test 4: Driver I/O Operations ===\n");

    /* Start driver first */
    int result = driver_start("test-driver");
    assert(result == 0 && "Driver start should succeed");

    /* Test read */
    uint8_t read_buffer[512];
    result = driver_read("test-driver", 0, 1, read_buffer);
    assert(result == 512 && "Read should return 512 bytes");
    assert(read_buffer[0] == 0xAA && "Read buffer should contain mock data");

    /* Test write */
    uint8_t write_buffer[512];
    memset(write_buffer, 0xBB, sizeof(write_buffer));
    result = driver_write("test-driver", 10, 1, write_buffer);
    assert(result == 512 && "Write should return 512 bytes");

    /* Get statistics */
    driver_stats_t stats;
    result = driver_get_stats("test-driver", &stats);
    assert(result == 0 && "Get stats should succeed");
    assert(stats.total_requests == 2 && "Should have 2 total requests");
    assert(stats.successful_requests == 2 && "Should have 2 successful requests");
    assert(stats.bytes_read == 512 && "Should have read 512 bytes");
    assert(stats.bytes_written == 512 && "Should have written 512 bytes");

    printf("✓ Test 4 PASSED\n");
}

/* Test 5: Driver listing */
static void test_driver_listing(void) {
    printf("\n=== Test 5: Driver Listing ===\n");

    char names[32][32];
    uint32_t count;

    int result = driver_list(names, 32, &count);
    assert(result == 0 && "Driver list should succeed");
    assert(count == 2 && "Should have 2 registered drivers");

    printf("Registered drivers:\n");
    for (uint32_t i = 0; i < count; i++) {
        printf("  - %s\n", names[i]);
    }

    printf("✓ Test 5 PASSED\n");
}

/* Test 6: Driver unregistration */
static void test_driver_unregistration(void) {
    printf("\n=== Test 6: Driver Unregistration ===\n");

    /* Stop driver first */
    int result = driver_stop("test-driver");
    assert(result == 0 && "Driver stop should succeed");

    /* Unregister */
    result = driver_unregister("test-driver");
    assert(result == 0 && "Driver unregister should succeed");

    /* Should not find driver */
    driver_info_t info;
    result = driver_get_info("test-driver", &info);
    assert(result == -1 && "Should not find unregistered driver");

    printf("✓ Test 6 PASSED\n");
}

/* Main test runner */
int main(void) {
    printf("======================================================================\n");
    printf("Week 23: Driver Registry Test Suite\n");
    printf("======================================================================\n");

    test_registry_init();
    test_driver_registration();
    test_driver_state_transitions();
    test_driver_io();
    test_driver_listing();
    test_driver_unregistration();

    /* Cleanup */
    driver_registry_cleanup();

    printf("\n======================================================================\n");
    printf("Test Summary\n");
    printf("======================================================================\n");
    printf("✅ ALL TESTS PASSED (6/6)\n");
    printf("======================================================================\n");

    return 0;
}
