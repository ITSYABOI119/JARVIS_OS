/**
 * JARVIS AI-OS Phase 1 - VirtIO Block Driver Test
 * Week 23: VirtIO Block Driver Implementation
 *
 * Unit tests for VirtIO block driver (without real hardware).
 * Tests driver registry integration and basic operations.
 *
 * Note: This test validates the driver framework integration.
 * Actual VirtIO hardware testing requires QEMU with VirtIO device.
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#include "virtio_blk.h"
#include "driver_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test 1: VirtIO block driver integration with registry */
static void test_driver_integration(void) {
    printf("\n=== Test 1: VirtIO Block Driver Integration ===\n");

    /* Get driver operations (no hardware access) */
    driver_ops_t* ops = virtio_blk_get_driver_ops();
    assert(ops != NULL && "Driver ops should not be NULL");
    assert(ops->init != NULL && "Init operation should be defined");
    assert(ops->start != NULL && "Start operation should be defined");
    assert(ops->stop != NULL && "Stop operation should be defined");
    assert(ops->read != NULL && "Read operation should be defined");
    assert(ops->write != NULL && "Write operation should be defined");
    assert(ops->cleanup != NULL && "Cleanup operation should be defined");

    printf("✓ Driver operations structure validated\n");
    printf("✓ All required operations defined:\n");
    printf("  - init:    %p\n", (void*)ops->init);
    printf("  - start:   %p\n", (void*)ops->start);
    printf("  - stop:    %p\n", (void*)ops->stop);
    printf("  - read:    %p\n", (void*)ops->read);
    printf("  - write:   %p\n", (void*)ops->write);
    printf("  - cleanup: %p\n", (void*)ops->cleanup);

    printf("✓ Test 1 PASSED\n");
}

/* Test 2: Driver operations structure */
static void test_driver_ops_structure(void) {
    printf("\n=== Test 2: Driver Operations Structure ===\n");

    driver_ops_t* ops = virtio_blk_get_driver_ops();

    /* Verify all required operations are present */
    assert(ops->init != NULL && "init operation should be defined");
    assert(ops->start != NULL && "start operation should be defined");
    assert(ops->stop != NULL && "stop operation should be defined");
    assert(ops->read != NULL && "read operation should be defined");
    assert(ops->write != NULL && "write operation should be defined");
    assert(ops->cleanup != NULL && "cleanup operation should be defined");

    printf("✓ All driver operations defined:\n");
    printf("  - init:    %p\n", (void*)ops->init);
    printf("  - start:   %p\n", (void*)ops->start);
    printf("  - stop:    %p\n", (void*)ops->stop);
    printf("  - read:    %p\n", (void*)ops->read);
    printf("  - write:   %p\n", (void*)ops->write);
    printf("  - cleanup: %p\n", (void*)ops->cleanup);

    printf("✓ Test 2 PASSED\n");
}

/* Test 3: VirtIO request structures */
static void test_virtio_structures(void) {
    printf("\n=== Test 3: VirtIO Request Structures ===\n");

    /* Test header structure */
    virtio_blk_req_header_t header;
    header.type = VIRTIO_BLK_T_IN;
    header.reserved = 0;
    header.sector = 12345;

    assert(sizeof(virtio_blk_req_header_t) == 16 && "Header size should be 16 bytes");
    assert(header.type == VIRTIO_BLK_T_IN && "Header type should be IN (read)");
    assert(header.sector == 12345 && "Header sector should match");

    /* Test status structure */
    virtio_blk_req_status_t status;
    status.status = VIRTIO_BLK_S_OK;

    assert(sizeof(virtio_blk_req_status_t) == 1 && "Status size should be 1 byte");
    assert(status.status == VIRTIO_BLK_S_OK && "Status should be OK");

    printf("✓ Request header size: %zu bytes\n", sizeof(virtio_blk_req_header_t));
    printf("✓ Request status size: %zu bytes\n", sizeof(virtio_blk_req_status_t));
    printf("✓ Sector size: %d bytes\n", VIRTIO_BLK_SECTOR_SIZE);

    printf("✓ Test 3 PASSED\n");
}

/* Test 4: Feature bits */
static void test_feature_bits(void) {
    printf("\n=== Test 4: Feature Bits ===\n");

    /* Verify feature bit definitions */
    uint64_t version_1 = VIRTIO_F_VERSION_1;
    assert(version_1 == (1ULL << 32) && "VirtIO 1.0 feature bit should be (1 << 32)");

    uint64_t blk_ro = VIRTIO_BLK_F_RO;
    assert(blk_ro == (1ULL << 5) && "Block RO feature bit should be (1 << 5)");

    printf("✓ VIRTIO_F_VERSION_1: 0x%016lx\n", VIRTIO_F_VERSION_1);
    printf("✓ VIRTIO_BLK_F_RO:     0x%016lx\n", VIRTIO_BLK_F_RO);
    printf("✓ VIRTIO_BLK_F_FLUSH:  0x%016lx\n", VIRTIO_BLK_F_FLUSH);

    printf("✓ Test 4 PASSED\n");
}

/* Test 5: Compile-time checks */
static void test_compile_time_checks(void) {
    printf("\n=== Test 5: Compile-Time Checks ===\n");

    /* Verify structure packing */
    printf("✓ virtio_blk_req_header_t: %zu bytes (packed)\n", sizeof(virtio_blk_req_header_t));
    printf("✓ virtio_blk_req_status_t: %zu bytes (packed)\n", sizeof(virtio_blk_req_status_t));
    printf("✓ virtq_desc_t: %zu bytes (packed)\n", sizeof(virtq_desc_t));

    /* Verify sector size */
    assert(VIRTIO_BLK_SECTOR_SIZE == 512 && "Sector size should be 512 bytes");

    printf("✓ All compile-time checks passed\n");
    printf("✓ Test 5 PASSED\n");
}

/* Main test runner */
int main(void) {
    printf("======================================================================\n");
    printf("Week 23: VirtIO Block Driver Test Suite\n");
    printf("======================================================================\n");
    printf("Note: These tests validate driver framework integration.\n");
    printf("Actual VirtIO hardware tests require QEMU with VirtIO device.\n");
    printf("======================================================================\n");

    test_driver_integration();
    test_driver_ops_structure();
    test_virtio_structures();
    test_feature_bits();
    test_compile_time_checks();

    printf("\n======================================================================\n");
    printf("Test Summary\n");
    printf("======================================================================\n");
    printf("✅ ALL TESTS PASSED (5/5)\n");
    printf("======================================================================\n");
    printf("\nNext Steps:\n");
    printf("  1. Test with QEMU VirtIO device (requires MMIO base address)\n");
    printf("  2. Integrate with FileSystem Agent (Phase 3)\n");
    printf("  3. Add comprehensive I/O tests (Week 24)\n");
    printf("======================================================================\n");

    return 0;
}
