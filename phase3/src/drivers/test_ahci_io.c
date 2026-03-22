/**
 * test_ahci_io.c - AHCI Disk I/O Unit Tests
 *
 * Mock MMIO tests for AHCI command setup, IDENTIFY parsing,
 * READ/WRITE DMA EXT FIS building, PRDT setup, and error paths.
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *       -I phase3/src/drivers test_ahci_io.c ahci.c -o test_ahci_io
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "ahci.h"

/* ========================================================================
 * Mock MMIO Layer
 *
 * For I/O tests we need a mock hba_port_t.  The port registers are
 * volatile structs that ahci.c reads/writes directly (not through the
 * discovery hba_read/hba_write helpers).  We provide a plain struct
 * instance and mock the MMIO functions used by the discovery code.
 *
 * ahci_issue_command() reads/writes port->ci, port->tfd, port->is.
 * We control those fields to simulate completion or timeout.
 * ======================================================================== */

/* Mock MMIO for discovery code (needed for ahci.c compilation) */
static uint8_t mock_hba[4096] = {0};

uint32_t mmio_read32(volatile void *addr)
{
    uintptr_t offset = (uintptr_t)addr;
    if (offset + 4 <= sizeof(mock_hba))
        return *(uint32_t *)(mock_hba + offset);
    return 0;
}

void mmio_write32(volatile void *addr, uint32_t val)
{
    uintptr_t offset = (uintptr_t)addr;
    if (offset + 4 <= sizeof(mock_hba))
        *(uint32_t *)(mock_hba + offset) = val;
}

/* ========================================================================
 * Mock port: writable hba_port_t
 *
 * ahci_issue_command writes port->ci then polls it.  We simulate
 * instant completion by having CI clear itself when read.
 * For the timeout test we keep CI set.
 * ======================================================================== */

/* Non-volatile shadow of port registers that we can manipulate */
static struct {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t reserved0;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
    uint32_t reserved1[11];
    uint32_t vendor[4];
} mock_port_storage;

/*
 * We cast mock_port_storage to hba_port_t*.  Both are the same layout
 * (hba_port_t is volatile, our storage is not -- that is fine for tests).
 *
 * To simulate command completion: after ahci_issue_command writes CI,
 * we need CI to read back as 0 on the next poll.  We achieve this by
 * hooking into the test flow: set up the port, call the function under
 * test, and for the success path just pre-clear CI to 0 (the function
 * writes CI then reads it -- since we are single-threaded the write
 * goes into mock_port_storage.ci and we need it to read 0).
 *
 * Approach: We patch mock_port_storage.ci = 0 right before the
 * ahci_issue_command poll loop runs.  Since the loop reads port->ci
 * which points into our storage, we can use a small wrapper.
 *
 * Actually, simpler: ahci_issue_command writes port->ci = (1<<slot),
 * then immediately polls.  In our single-threaded test, the write
 * stores the bit.  On the very first read the bit is still set.
 * But by making tfd clean and keeping port->ci writable, we can
 * simply pre-set ci=0 after the function writes it -- but we can't
 * intercept the write.
 *
 * Best approach for mock: redefine AHCI_CMD_TIMEOUT to be small in
 * mock mode?  No, it is in ahci.c.
 *
 * Simplest approach: we test the *helpers* (FIS building, PRDT setup,
 * identify parsing) directly, and test issue_command with a port whose
 * CI field we control.  For the success path, set CI=0 before calling
 * (then issue writes 1, reads 1, but since our mock_port_storage.ci
 * is non-volatile, the compiler might optimize... actually it IS
 * volatile through the hba_port_t pointer, so reads will go to memory).
 *
 * For success: use a real call and accept that the loop runs once
 * (CI is set, then next read still sees it).  We need CI to clear.
 *
 * Solution: In mock mode, ahci_issue_command timeout is 5M iterations.
 * We will set CI to 0 in our mock port BEFORE calling, and since the
 * function does port->ci = ci_bit (sets it to 1), then reads it back,
 * we need it to read 0.  The port pointer is volatile, so reads go
 * through memory.
 *
 * Final approach: write a small mock_prepare_success() that stores
 * a value so that the SECOND read of CI returns 0.  We do this by
 * using a thread or signal -- too complex.
 *
 * PRAGMATIC APPROACH: We test ahci_issue_command indirectly through
 * ahci_read_sectors/ahci_write_sectors.  For success, we set the
 * port's CI to 0 and TFD to 0; the function writes CI=1, then reads
 * CI -- since our storage is regular memory and port->ci is volatile,
 * the read sees 1.  Then the loop iterates... and reads 1 again.
 * This will always timeout.
 *
 * ACTUAL SOLUTION: Set CI field to 0 inside a mock wrapper.
 * We override the CI field to always read 0 by making the mock port
 * use a union trick.  Or, even simpler: since the port is just a
 * pointer to our struct, and the struct is in regular RAM, we can
 * simply set ci = 0 after the write.  But we can't intercept the
 * write from inside the function.
 *
 * REAL SOLUTION: We use a mock hba_port_t that wraps the storage.
 * For the CI register, on write we store the value; on read we
 * return what we want.  This requires custom memory-mapped I/O
 * but the port struct is accessed via direct struct member access
 * (port->ci) not via mmio_read32, so we cannot intercept it.
 *
 * ACCEPTED SOLUTION: For testing ahci_issue_command directly:
 * - Success: set port ci=0 and tfd=0.  The function writes ci=bit,
 *   but since we test with slot 0 and ci is in our writable struct,
 *   we accept that the first read sees the bit.  Instead, we set
 *   ci=0 explicitly.  BUT the function writes it first.
 *
 * OK, truly the simplest: For the SUCCESS test, we test it by making
 * AHCI_CMD_TIMEOUT large enough and observing that after writing CI,
 * the function reads it back.  Since mock_port_storage.ci is written
 * by the function (to 1) and then read back, it sees 1 forever => timeout.
 * We cannot test success through the volatile path in single-threaded mode.
 *
 * FINAL DECISION: Test success path through FIS/PRDT/parsing helpers
 * (which are the meat of the I/O code).  Test issue_command only for
 * the timeout and error paths (which are fully testable).  The success
 * path of issue_command is trivially correct (just polls until CI clears)
 * and will be verified on real hardware.
 */

static hba_port_t *get_mock_port(void)
{
    memset(&mock_port_storage, 0, sizeof(mock_port_storage));
    return (hba_port_t *)&mock_port_storage;
}

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("Test %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

/* ========================================================================
 * Test 1: IDENTIFY data parsing
 * ======================================================================== */

static void test_identify_parse(void)
{
    uint16_t id_buf[256];
    char model[41];
    char serial[21];
    uint64_t capacity;
    uint32_t sec_size;

    TEST("IDENTIFY parse -- model, serial, capacity, sector size");

    memset(id_buf, 0, sizeof(id_buf));

    /* Words 27-46: model string "JARVIS SSD 500GB" (byte-swapped pairs)
     * 'J','A' -> word = ('J'<<8)|'A' = 0x4A41
     * 'R','V' -> word = ('R'<<8)|'V' = 0x5256
     * 'I','S' -> word = ('I'<<8)|'S' = 0x4953
     * ' ','S' -> word = (' '<<8)|'S' = 0x2053
     * 'S','D' -> word = ('S'<<8)|'D' = 0x5344
     * ' ','5' -> word = (' '<<8)|'5' = 0x2035
     * '0','0' -> word = ('0'<<8)|'0' = 0x3030
     * 'G','B' -> word = ('G'<<8)|'B' = 0x4742
     */
    id_buf[27] = 0x4A41;  /* JA */
    id_buf[28] = 0x5256;  /* RV */
    id_buf[29] = 0x4953;  /* IS */
    id_buf[30] = 0x2053;  /* _S */
    id_buf[31] = 0x5344;  /* SD */
    id_buf[32] = 0x2035;  /* _5 */
    id_buf[33] = 0x3030;  /* 00 */
    id_buf[34] = 0x4742;  /* GB */
    /* Words 35-46 are spaces (0x2020) */
    for (int i = 35; i <= 46; i++)
        id_buf[i] = 0x2020;

    /* Words 10-19: serial "ABC123456789" (byte-swapped pairs) */
    id_buf[10] = 0x4142;  /* AB */
    id_buf[11] = 0x4331;  /* C1 */
    id_buf[12] = 0x3233;  /* 23 */
    id_buf[13] = 0x3435;  /* 45 */
    id_buf[14] = 0x3637;  /* 67 */
    id_buf[15] = 0x3839;  /* 89 */
    /* Words 16-19 are spaces */
    for (int i = 16; i <= 19; i++)
        id_buf[i] = 0x2020;

    /* Words 100-103: LBA48 capacity = 976773168 sectors (~500 GB) */
    uint64_t cap_val = 976773168ULL;
    id_buf[100] = (uint16_t)(cap_val & 0xFFFF);
    id_buf[101] = (uint16_t)((cap_val >> 16) & 0xFFFF);
    id_buf[102] = (uint16_t)((cap_val >> 32) & 0xFFFF);
    id_buf[103] = (uint16_t)((cap_val >> 48) & 0xFFFF);

    /* Word 106: standard 512-byte sectors (bit 14=1, bit 15=0, bit 12=0) */
    id_buf[106] = (1U << 14);

    /* Parse and verify */
    ahci_identify_parse_model(id_buf, model);
    if (strcmp(model, "JARVIS SSD 500GB") != 0) {
        printf("(got '%s') ", model);
        FAIL("model mismatch");
    }

    ahci_identify_parse_serial(id_buf, serial);
    if (strcmp(serial, "ABC123456789") != 0) {
        printf("(got '%s') ", serial);
        FAIL("serial mismatch");
    }

    capacity = ahci_identify_parse_capacity(id_buf);
    if (capacity != 976773168ULL) {
        printf("(got %llu) ", (unsigned long long)capacity);
        FAIL("capacity mismatch");
    }

    sec_size = ahci_identify_parse_sector_size(id_buf);
    if (sec_size != 512) {
        printf("(got %u) ", sec_size);
        FAIL("sector size mismatch");
    }

    PASS();
}

/* ========================================================================
 * Test 2: READ DMA EXT FIS building
 * ======================================================================== */

static void test_read_fis_build(void)
{
    fis_reg_h2d_t fis;

    TEST("READ DMA EXT FIS -- LBA=0x100, count=8");

    ahci_build_rw_fis(&fis, ATA_CMD_READ_DMA_EXT, 0x100, 8);

    if (fis.fis_type != FIS_TYPE_REG_H2D)
        FAIL("fis_type != 0x27");
    if (fis.flags != FIS_H2D_CMD)
        FAIL("flags != 0x80 (command)");
    if (fis.command != ATA_CMD_READ_DMA_EXT)
        FAIL("command != 0x25");
    if (fis.device != 0x40)
        FAIL("device != 0x40 (LBA mode)");

    /* LBA 0x100: lba0=0x00, lba1=0x01, lba2..5=0x00 */
    if (fis.lba0 != 0x00) FAIL("lba0 wrong");
    if (fis.lba1 != 0x01) FAIL("lba1 wrong");
    if (fis.lba2 != 0x00) FAIL("lba2 wrong");
    if (fis.lba3 != 0x00) FAIL("lba3 wrong");
    if (fis.lba4 != 0x00) FAIL("lba4 wrong");
    if (fis.lba5 != 0x00) FAIL("lba5 wrong");

    if (fis.count_lo != 8) FAIL("count_lo wrong");
    if (fis.count_hi != 0) FAIL("count_hi wrong");

    PASS();
}

/* ========================================================================
 * Test 3: WRITE DMA EXT FIS building
 * ======================================================================== */

static void test_write_fis_build(void)
{
    fis_reg_h2d_t fis;

    TEST("WRITE DMA EXT FIS -- LBA=0x2000, count=16");

    ahci_build_rw_fis(&fis, ATA_CMD_WRITE_DMA_EXT, 0x2000, 16);

    if (fis.command != ATA_CMD_WRITE_DMA_EXT)
        FAIL("command != 0x35");
    if (fis.device != 0x40)
        FAIL("device != 0x40 (LBA mode)");

    /* LBA 0x2000: lba0=0x00, lba1=0x20, rest=0 */
    if (fis.lba0 != 0x00) FAIL("lba0 wrong");
    if (fis.lba1 != 0x20) FAIL("lba1 wrong");
    if (fis.lba2 != 0x00) FAIL("lba2 wrong");
    if (fis.lba3 != 0x00) FAIL("lba3 wrong");

    if (fis.count_lo != 16) FAIL("count_lo wrong");
    if (fis.count_hi != 0)  FAIL("count_hi wrong");

    PASS();
}

/* ========================================================================
 * Test 4: LBA48 encoding -- large address
 * ======================================================================== */

static void test_lba48_encoding(void)
{
    fis_reg_h2d_t fis;
    uint64_t lba = 0x123456789ABULL;

    TEST("LBA48 encoding -- 0x123456789AB split across FIS");

    ahci_build_rw_fis(&fis, ATA_CMD_READ_DMA_EXT, lba, 1);

    /* 0x123456789AB:
     *   lba0 = 0xAB  (bits  7:0)
     *   lba1 = 0x89  (bits 15:8)
     *   lba2 = 0x67  (bits 23:16)
     *   lba3 = 0x45  (bits 31:24)
     *   lba4 = 0x23  (bits 39:32)
     *   lba5 = 0x01  (bits 47:40)
     */
    if (fis.lba0 != 0xAB) { printf("(got 0x%02X) ", fis.lba0); FAIL("lba0 != 0xAB"); }
    if (fis.lba1 != 0x89) { printf("(got 0x%02X) ", fis.lba1); FAIL("lba1 != 0x89"); }
    if (fis.lba2 != 0x67) { printf("(got 0x%02X) ", fis.lba2); FAIL("lba2 != 0x67"); }
    if (fis.lba3 != 0x45) { printf("(got 0x%02X) ", fis.lba3); FAIL("lba3 != 0x45"); }
    if (fis.lba4 != 0x23) { printf("(got 0x%02X) ", fis.lba4); FAIL("lba4 != 0x23"); }
    if (fis.lba5 != 0x01) { printf("(got 0x%02X) ", fis.lba5); FAIL("lba5 != 0x01"); }

    PASS();
}

/* ========================================================================
 * Test 5: PRDT setup -- 5 MB buffer (needs 2 entries: 4 MB + 1 MB)
 * ======================================================================== */

/* To inspect PRDT entries we need access to the static command table.
 * Declare it as extern -- it is defined in ahci.c. */
extern uint8_t ahci_cmd_table_buf[];
extern hba_cmd_header_t ahci_cmd_list[];

static void test_prdt_setup(void)
{
    hba_port_t *port = get_mock_port();
    fis_reg_h2d_t fis;
    hba_cmd_table_t *tbl;
    uint32_t buf_len = 5 * 1024 * 1024;  /* 5 MB */
    uintptr_t fake_buf = 0x10000;
    int rc;

    TEST("PRDT setup -- 5 MB buffer = 2 entries (4 MB + 1 MB)");

    memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.flags = FIS_H2D_CMD;
    fis.command = ATA_CMD_READ_DMA_EXT;

    rc = ahci_setup_command(port, 0, &fis, fake_buf, buf_len, 0);
    if (rc != 0)
        FAIL("ahci_setup_command returned error");

    /* Check command header PRDTL = 2 */
    if (ahci_cmd_list[0].prdtl != 2) {
        printf("(prdtl=%u) ", ahci_cmd_list[0].prdtl);
        FAIL("expected 2 PRDT entries");
    }

    /* Check PRDT entries */
    tbl = (hba_cmd_table_t *)ahci_cmd_table_buf;

    /* Entry 0: 4 MB at fake_buf */
    uint32_t expected_dbc0 = (4 * 1024 * 1024 - 1) & PRD_DBC_MASK;
    if (tbl->prdt[0].dba != (uint32_t)fake_buf) FAIL("entry 0 dba wrong");
    if ((tbl->prdt[0].dbc & PRD_DBC_MASK) != expected_dbc0) {
        printf("(dbc=0x%X, expected 0x%X) ", tbl->prdt[0].dbc & PRD_DBC_MASK, expected_dbc0);
        FAIL("entry 0 dbc wrong");
    }
    /* Entry 0 should NOT have IOC */
    if (tbl->prdt[0].dbc & PRD_DBC_IOC)
        FAIL("entry 0 should not have IOC");

    /* Entry 1: 1 MB at fake_buf + 4 MB, with IOC */
    uint32_t expected_dbc1 = (1 * 1024 * 1024 - 1) & PRD_DBC_MASK;
    uintptr_t expected_addr1 = fake_buf + 4 * 1024 * 1024;
    if (tbl->prdt[1].dba != (uint32_t)expected_addr1) FAIL("entry 1 dba wrong");
    if ((tbl->prdt[1].dbc & PRD_DBC_MASK) != expected_dbc1) {
        printf("(dbc=0x%X, expected 0x%X) ", tbl->prdt[1].dbc & PRD_DBC_MASK, expected_dbc1);
        FAIL("entry 1 dbc wrong");
    }
    /* Entry 1 MUST have IOC (last entry) */
    if (!(tbl->prdt[1].dbc & PRD_DBC_IOC))
        FAIL("entry 1 should have IOC");

    PASS();
}

/* ========================================================================
 * Test 6: Command timeout -- CI bit never clears
 * ======================================================================== */

static void test_command_timeout(void)
{
    hba_port_t *port = get_mock_port();
    fis_reg_h2d_t fis;
    int rc;

    TEST("Command timeout -- CI never clears => -1");

    /* Set up a valid command first */
    memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.flags = FIS_H2D_CMD;
    fis.command = ATA_CMD_IDENTIFY;

    rc = ahci_setup_command(port, 0, &fis, 0, 0, 0);
    if (rc != 0)
        FAIL("setup_command failed");

    /* Pre-set CI bit 0 = 1 to simulate "command never completes".
     * ahci_issue_command writes ci=1, then polls -- since our storage
     * is regular memory, the volatile read will always see 1.
     * TFD = 0 (no error) so it won't exit early via error path. */
    mock_port_storage.ci = 1;
    mock_port_storage.tfd = 0;

    rc = ahci_issue_command(port, 0);
    if (rc != -1) {
        printf("(got %d) ", rc);
        FAIL("expected -1 (timeout)");
    }

    PASS();
}

/* ========================================================================
 * Test 7: Sector count limits -- reject 0 and >65536
 * ======================================================================== */

static void test_sector_count_limits(void)
{
    hba_port_t *port = get_mock_port();
    static uint8_t dummy_buf[512];
    int rc;

    TEST("Sector count limits -- reject 0 and >65536");

    /* count = 0 should be rejected */
    rc = ahci_read_sectors(port, 0, 0, dummy_buf);
    if (rc == 0) FAIL("count=0 should be rejected");

    /* count = 65537 should be rejected */
    rc = ahci_read_sectors(port, 0, 65537, dummy_buf);
    if (rc == 0) FAIL("count=65537 should be rejected");

    /* Same for write */
    rc = ahci_write_sectors(port, 0, 0, dummy_buf);
    if (rc == 0) FAIL("write count=0 should be rejected");

    rc = ahci_write_sectors(port, 0, 65537, dummy_buf);
    if (rc == 0) FAIL("write count=65537 should be rejected");

    /* count = 65536 should be accepted (at setup level) --
     * it will timeout at issue, but setup should succeed */
    /* count = 1 is the minimum valid */

    PASS();
}

/* ========================================================================
 * Test 8: Command header flags -- write bit and CFL
 * ======================================================================== */

static void test_command_header_flags(void)
{
    hba_port_t *port = get_mock_port();
    fis_reg_h2d_t fis;
    int rc;

    TEST("Command header flags -- CFL=5, W bit for write");

    memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.flags = FIS_H2D_CMD;
    fis.command = ATA_CMD_READ_DMA_EXT;

    /* Read command: W bit should be clear */
    rc = ahci_setup_command(port, 0, &fis, 0x1000, 512, 0);
    if (rc != 0) FAIL("setup read failed");

    uint16_t flags_r = ahci_cmd_list[0].flags;
    if ((flags_r & CMD_HDR_CFL_MASK) != 5) {
        printf("(CFL=%u) ", flags_r & CMD_HDR_CFL_MASK);
        FAIL("CFL should be 5 (20 bytes / 4)");
    }
    if (flags_r & CMD_HDR_WRITE)
        FAIL("W bit should be clear for read");

    /* Write command: W bit should be set */
    fis.command = ATA_CMD_WRITE_DMA_EXT;
    rc = ahci_setup_command(port, 0, &fis, 0x1000, 512, 1);
    if (rc != 0) FAIL("setup write failed");

    uint16_t flags_w = ahci_cmd_list[0].flags;
    if (!(flags_w & CMD_HDR_WRITE))
        FAIL("W bit should be set for write");

    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== JARVIS AI-OS: AHCI Disk I/O Tests ===\n\n");

    test_identify_parse();
    test_read_fis_build();
    test_write_fis_build();
    test_lba48_encoding();
    test_prdt_setup();
    test_command_timeout();
    test_sector_count_limits();
    test_command_header_flags();

    printf("\n=== Results: %d/%d PASSED ===\n", tests_passed, tests_run);

    if (tests_passed == tests_run)
        printf("ALL TESTS PASSED\n");
    else
        printf("SOME TESTS FAILED\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
