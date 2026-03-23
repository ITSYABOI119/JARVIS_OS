/*
 * JARVIS AI-OS — Block Device Abstraction Tests
 * Uses mock 1MB virtual disk.
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "blk_dev_x86.h"

/* ---- Test framework ---- */
static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("Test %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } } while(0)

/* ---- AHCI mock stubs (needed for linking ahci.c) ---- */
uint32_t mmio_read32(volatile void *addr) { (void)addr; return 0; }
void mmio_write32(volatile void *addr, uint32_t val) { (void)addr; (void)val; }

/* ---- Tests ---- */

static void test_init(void)
{
    TEST("blk_dev_init returns 0, is_ready true");
    int err = blk_dev_init();
    ASSERT(err == 0, "init failed");
    ASSERT(blk_dev_is_ready(), "not ready after init");
    PASS();
}

static void test_info(void)
{
    TEST("blk_dev_get_info: model, serial, sectors, size");
    blk_dev_init();
    blk_dev_info_t info;
    int err = blk_dev_get_info(&info);
    ASSERT(err == 0, "get_info failed");
    ASSERT(info.total_sectors == 2048, "wrong total_sectors");
    ASSERT(info.sector_size == 512, "wrong sector_size");
    ASSERT(strcmp(info.model, "JARVIS Mock Disk") == 0, "wrong model");
    ASSERT(strcmp(info.serial, "MOCK123456") == 0, "wrong serial");
    ASSERT(info.initialized, "not initialized");
    PASS();
}

static void test_read_single(void)
{
    TEST("read single sector: LBA 0");
    blk_dev_init();
    uint8_t buf[512];
    int err = blk_dev_read(0, 1, buf);
    ASSERT(err == 0, "read failed");
    /* Mock disk is zeroed, so buf should be all zeros */
    int ok = 1;
    for (int i = 0; i < 512; i++) if (buf[i] != 0) ok = 0;
    ASSERT(ok, "sector not zeroed");
    PASS();
}

static void test_read_multiple(void)
{
    TEST("read 8 sectors (4096 bytes)");
    blk_dev_init();
    uint8_t buf[4096];
    int err = blk_dev_read(0, 8, buf);
    ASSERT(err == 0, "multi-sector read failed");
    PASS();
}

static void test_write_read_roundtrip(void)
{
    TEST("write-read roundtrip: pattern verified");
    blk_dev_init();
    uint8_t wbuf[512];
    for (int i = 0; i < 512; i++) wbuf[i] = (uint8_t)(i & 0xFF);

    int err = blk_dev_write(10, 1, wbuf);
    ASSERT(err == 0, "write failed");

    uint8_t rbuf[512];
    memset(rbuf, 0xFF, sizeof(rbuf));
    err = blk_dev_read(10, 1, rbuf);
    ASSERT(err == 0, "read failed");
    ASSERT(memcmp(wbuf, rbuf, 512) == 0, "data mismatch");
    PASS();
}

static void test_write_beyond_capacity(void)
{
    TEST("write beyond capacity: returns -2");
    blk_dev_init();
    uint8_t buf[512];
    int err = blk_dev_write(2048, 1, buf);  /* LBA = total_sectors */
    ASSERT(err == -2, "should reject out-of-bounds write");
    PASS();
}

static void test_read_after_init(void)
{
    TEST("read after init: returns 0");
    blk_dev_init();
    uint8_t buf[512];
    int err = blk_dev_read(0, 1, buf);
    ASSERT(err == 0, "read after init should work");
    PASS();
}

static void test_zero_count(void)
{
    TEST("read/write with count=0: returns -3");
    blk_dev_init();
    uint8_t buf[512];
    int err = blk_dev_read(0, 0, buf);
    ASSERT(err == -3, "read count=0 should return -3");
    err = blk_dev_write(0, 0, buf);
    ASSERT(err == -3, "write count=0 should return -3");
    PASS();
}

static void test_sector_size(void)
{
    TEST("sector size reported as 512");
    blk_dev_init();
    blk_dev_info_t info;
    blk_dev_get_info(&info);
    ASSERT(info.sector_size == 512, "wrong sector size");
    PASS();
}

int main(void)
{
    printf("=== JARVIS Block Device Tests ===\n\n");

    test_init();
    test_info();
    test_read_single();
    test_read_multiple();
    test_write_read_roundtrip();
    test_write_beyond_capacity();
    test_read_after_init();
    test_zero_count();
    test_sector_size();

    printf("\n=== Results: %d/%d PASS ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
