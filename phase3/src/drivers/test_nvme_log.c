/*
 * JARVIS AI-OS - NVMe Log Unit Tests
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
 *       -I phase3/src/drivers \
 *       phase3/src/drivers/test_nvme_log.c phase3/src/drivers/nvme_log.c \
 *       -o /tmp/test_nvme_log
 *
 * Does NOT link nvme.c — mock implementations below replace the real
 * nvme_read_sectors / nvme_write_sectors.
 */

#include "nvme_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Mock NVMe storage -- 1024 sectors of 512 bytes
 * ================================================================ */

#define MOCK_SECTORS 1024
static uint8_t mock_disk[MOCK_SECTORS * 512];

/* Fake controller -- only .initialized is checked */
static nvme_controller_t mock_ctrl = {
    .initialized = true,
    .ns_block_size = 512
};

int nvme_read_sectors(nvme_controller_t *ctrl,
                      uint64_t lba, uint32_t count,
                      void *buf, uint64_t buf_phys)
{
    (void)ctrl; (void)buf_phys;
    uint64_t offset = (lba - NVME_LOG_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk))
        return -1;
    memcpy(buf, mock_disk + offset, count * 512);
    return 0;
}

int nvme_write_sectors(nvme_controller_t *ctrl,
                       uint64_t lba, uint32_t count,
                       const void *buf, uint64_t buf_phys)
{
    (void)ctrl; (void)buf_phys;
    uint64_t offset = (lba - NVME_LOG_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk))
        return -1;
    memcpy(mock_disk + offset, buf, count * 512);
    return 0;
}

/* ================================================================
 * Test helpers
 * ================================================================ */

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define PASS(name) do { \
    printf("  PASS: %s\n", name); \
    tests_passed++; \
} while (0)

/* DMA buffer (page-aligned on heap) */
static uint8_t dma_buf[4096] __attribute__((aligned(4096)));

/* Helper: compute header checksum (same algorithm as nvme_log.c) */
static uint32_t test_compute_checksum(const nvme_log_header_t *h)
{
    const uint8_t *raw = (const uint8_t *)h;
    uint32_t cs = 0;
    for (int i = 0; i < 15; i++) {
        uint32_t w;
        memcpy(&w, raw + i * 4, sizeof(w));
        cs ^= w;
    }
    return cs;
}

/* ================================================================
 * Test 1: Fresh init creates valid header
 * ================================================================ */

static void test_fresh_init(void)
{
    memset(mock_disk, 0xFF, sizeof(mock_disk));  /* garbage disk */
    memset(dma_buf, 0, sizeof(dma_buf));

    int rc = nvme_log_init(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "nvme_log_init should return 0");

    /* Read header from mock disk */
    nvme_log_header_t hdr;
    memcpy(&hdr, mock_disk, sizeof(hdr));

    ASSERT(hdr.magic == NVME_LOG_MAGIC, "header magic should be JRLG");
    ASSERT(hdr.version == NVME_LOG_VERSION, "header version should be 1");
    ASSERT(hdr.boot_id == 1, "first boot_id should be 1");
    ASSERT(hdr.cursor == 0, "cursor should be 0");
    ASSERT(hdr.total_entries == 0, "total_entries should be 0");
    ASSERT(hdr.checksum == test_compute_checksum(&hdr), "checksum should be valid");
    ASSERT(nvme_log_boot_id() == 1, "nvme_log_boot_id should return 1");

    PASS("test_fresh_init");
}

/* ================================================================
 * Test 2: Write entries and verify cursor advance
 * ================================================================ */

static void test_write_entry(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    memset(dma_buf, 0, sizeof(dma_buf));

    int rc = nvme_log_init(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "init should succeed");

    /* Write 3 entries */
    rc = nvme_log_write(&mock_ctrl, dma_buf, 0x1000, LOG_BOOT, "boot ok");
    ASSERT(rc == 0, "write entry 0 should succeed");

    rc = nvme_log_write(&mock_ctrl, dma_buf, 0x1000, LOG_SELFTEST, "5/5 PASS");
    ASSERT(rc == 0, "write entry 1 should succeed");

    rc = nvme_log_write(&mock_ctrl, dma_buf, 0x1000, LOG_MODEL_LOAD, "llama-1b loaded");
    ASSERT(rc == 0, "write entry 2 should succeed");

    /* Verify entry 0 payload (sector 1 on disk) */
    nvme_log_entry_t entry;
    memcpy(&entry, mock_disk + 512, sizeof(entry));
    ASSERT(entry.boot_id == 1, "entry 0 boot_id should be 1");
    ASSERT(entry.seq == 0, "entry 0 seq should be 0");
    ASSERT(entry.type == LOG_BOOT, "entry 0 type should be LOG_BOOT");
    ASSERT(entry.length == 7, "entry 0 length should be 7");
    ASSERT(memcmp(entry.payload, "boot ok", 7) == 0, "entry 0 payload mismatch");

    /* Verify entry 2 payload (sector 3 on disk) */
    memcpy(&entry, mock_disk + 3 * 512, sizeof(entry));
    ASSERT(entry.seq == 2, "entry 2 seq should be 2");
    ASSERT(entry.type == LOG_MODEL_LOAD, "entry 2 type should be LOG_MODEL_LOAD");

    PASS("test_write_entry");
}

/* ================================================================
 * Test 3: Boot ID increments on re-init
 * ================================================================ */

static void test_boot_id_increment(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    memset(dma_buf, 0, sizeof(dma_buf));

    /* First boot */
    int rc = nvme_log_init(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "first init should succeed");
    ASSERT(nvme_log_boot_id() == 1, "first boot_id should be 1");

    rc = nvme_log_write(&mock_ctrl, dma_buf, 0x1000, LOG_BOOT, "boot 1");
    ASSERT(rc == 0, "write should succeed");

    /* Flush to ensure header on disk is current */
    rc = nvme_log_flush(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "flush should succeed");

    /* Second boot (re-init reads existing header) */
    rc = nvme_log_init(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "second init should succeed");
    ASSERT(nvme_log_boot_id() == 2, "second boot_id should be 2");

    /* Third boot */
    rc = nvme_log_flush(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "flush should succeed");
    rc = nvme_log_init(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "third init should succeed");
    ASSERT(nvme_log_boot_id() == 3, "third boot_id should be 3");

    PASS("test_boot_id_increment");
}

/* ================================================================
 * Test 4: Corrupted checksum forces fresh header
 * ================================================================ */

static void test_checksum_corruption(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    memset(dma_buf, 0, sizeof(dma_buf));

    /* Create a valid header */
    int rc = nvme_log_init(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "init should succeed");
    rc = nvme_log_flush(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "flush should succeed");

    /* Corrupt checksum on disk */
    nvme_log_header_t hdr;
    memcpy(&hdr, mock_disk, sizeof(hdr));
    ASSERT(hdr.magic == NVME_LOG_MAGIC, "header should be valid before corruption");
    hdr.boot_id = 99;  /* change boot_id without updating checksum */
    memcpy(mock_disk, &hdr, sizeof(hdr));

    /* Re-init should detect corruption and create fresh header */
    rc = nvme_log_init(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "init after corruption should succeed");
    ASSERT(nvme_log_boot_id() == 1, "boot_id should reset to 1 after corruption");

    PASS("test_checksum_corruption");
}

/* ================================================================
 * Test 5: Log full detection
 * ================================================================ */

static void test_log_full(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    memset(dma_buf, 0, sizeof(dma_buf));

    /* Write a header with cursor at MAX_ENTRIES (simulating a full log) */
    nvme_log_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic   = NVME_LOG_MAGIC;
    hdr.version = NVME_LOG_VERSION;
    hdr.cursor  = NVME_LOG_MAX_ENTRIES;
    hdr.total_entries = NVME_LOG_MAX_ENTRIES;
    hdr.boot_id = 5;
    hdr.checksum = test_compute_checksum(&hdr);
    memcpy(mock_disk, &hdr, sizeof(hdr));

    /* Init reads the valid full header, increments boot_id */
    int rc = nvme_log_init(&mock_ctrl, dma_buf, 0x1000);
    ASSERT(rc == 0, "init with full log should succeed");
    ASSERT(nvme_log_boot_id() == 6, "boot_id should be 6");

    /* Write should fail with -2 (log full) */
    rc = nvme_log_write(&mock_ctrl, dma_buf, 0x1000, LOG_ERROR, "overflow");
    ASSERT(rc == -2, "write to full log should return -2");

    PASS("test_log_full");
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== NVMe Log Tests ===\n");

    test_fresh_init();
    test_write_entry();
    test_boot_id_increment();
    test_checksum_corruption();
    test_log_full();

    printf("\nResults: %d PASS, %d FAIL\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
