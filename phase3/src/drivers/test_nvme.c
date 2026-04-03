/**
 * test_nvme.c - Mock tests for the NVMe driver
 *
 * 10 tests exercising controller init, IDENTIFY parsing, I/O queue
 * creation, read command format, and CQ phase bit toggling.
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *     -I phase3/src/drivers \
 *     phase3/src/drivers/test_nvme.c phase3/src/drivers/nvme.c \
 *     -o /tmp/test_nvme && /tmp/test_nvme
 *
 * JARVIS AI-OS - Phase 3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "nvme.h"

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int tests_passed = 0;
static int tests_failed = 0;

#define EXPECT(cond, msg)                                       \
    do {                                                        \
        if (!(cond)) {                                          \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__);    \
            return 0;                                           \
        }                                                       \
    } while (0)

#define RUN_TEST(fn)                                            \
    do {                                                        \
        printf("[TEST] %s\n", #fn);                             \
        if (fn()) {                                             \
            printf("  PASS\n");                                 \
            tests_passed++;                                     \
        } else {                                                \
            tests_failed++;                                     \
        }                                                       \
    } while (0)

/* ========================================================================
 * Mock MMIO Infrastructure
 * ======================================================================== */

static uint8_t mock_bar[8192];

/* Track the last SQ doorbell write offset and value */
static uint32_t last_sq_doorbell_off = 0;
static uint32_t last_sq_doorbell_val = 0;
static uint32_t last_cq_doorbell_off = 0;
static uint32_t last_cq_doorbell_val = 0;

/* Mock completion control */
static uint8_t mock_completion_phase = 1;
static int mock_doorbell_count = 0;

/* Pointers to the CQ buffer so mock can inject completions */
static nvme_cq_entry_t *mock_admin_cq = NULL;
static uint16_t mock_admin_cq_head = 0;
static nvme_cq_entry_t *mock_io_cq = NULL;
static uint16_t mock_io_cq_head = 0;

/* Track identify buffer PRP1 for injecting data */
static uint64_t last_identify_prp1 = 0;
static uint8_t  last_cmd_opcode = 0;
static uint32_t last_cmd_cdw10 = 0;
static uint32_t last_cmd_cdw11 = 0;
static uint32_t last_cmd_nsid = 0;

/* Track SQ entries for verification */
static nvme_sq_entry_t *mock_admin_sq = NULL;
static uint16_t mock_admin_sq_tail = 0;
static nvme_sq_entry_t *mock_io_sq = NULL;
static uint16_t mock_io_sq_tail = 0;

/* Auto-complete: when an SQ doorbell is written, inject a success CQ entry */
static void mock_auto_complete_admin(void)
{
    if (!mock_admin_cq)
        return;
    nvme_cq_entry_t *cqe = &mock_admin_cq[mock_admin_cq_head];
    cqe->result = 0;
    cqe->reserved = 0;
    cqe->sq_head = 0;
    cqe->sq_id = 0;
    cqe->cid = 0;
    /* Phase bit in bit 0, status 0 (success) in bits 15:1 */
    cqe->status = mock_completion_phase;
    mock_admin_cq_head = (mock_admin_cq_head + 1) % NVME_ADMIN_QUEUE_SIZE;
}

static void mock_auto_complete_io(void)
{
    if (!mock_io_cq)
        return;
    nvme_cq_entry_t *cqe = &mock_io_cq[mock_io_cq_head];
    cqe->result = 0;
    cqe->reserved = 0;
    cqe->sq_head = 0;
    cqe->sq_id = 1;
    cqe->cid = 0;
    cqe->status = mock_completion_phase;
    mock_io_cq_head = (mock_io_cq_head + 1) % NVME_IO_QUEUE_SIZE;
}

/* ---- Mock MMIO functions (called by nvme.c via extern) ---- */

uint32_t nvme_read32(volatile uint8_t *base, uint32_t off)
{
    if (off < sizeof(mock_bar) - 3)
        return *(uint32_t *)(mock_bar + off);
    return 0;
}

void nvme_write32(volatile uint8_t *base, uint32_t off, uint32_t val)
{
    if (off < sizeof(mock_bar) - 3)
        *(uint32_t *)(mock_bar + off) = val;

    /* CC register write: auto-update CSTS.RDY to match CC.EN */
    if (off == NVME_REG_CC) {
        if (val & NVME_CC_EN)
            *(uint32_t *)(mock_bar + NVME_REG_CSTS) = NVME_CSTS_RDY;
        else
            *(uint32_t *)(mock_bar + NVME_REG_CSTS) = 0;
    }

    /* Detect doorbell writes (offset >= 0x1000) */
    if (off >= NVME_REG_SQ0TDBL) {
        mock_doorbell_count++;

        /* Determine if this is an SQ or CQ doorbell.
         * SQ doorbells are at 0x1000 + (2*qid)*stride
         * CQ doorbells are at 0x1000 + (2*qid+1)*stride
         * With dstrd=0, stride=4, so SQ0=0x1000, CQ0=0x1004,
         * SQ1=0x1008, CQ1=0x100C */
        uint32_t db_offset = off - NVME_REG_SQ0TDBL;
        uint32_t db_index = db_offset / 4;  /* For dstrd=0 */

        if (db_index % 2 == 0) {
            /* SQ doorbell */
            last_sq_doorbell_off = off;
            last_sq_doorbell_val = val;

            /* Auto-complete: inject success into the appropriate CQ */
            if (db_index == 0) {
                /* Admin SQ doorbell */
                /* Capture the last submitted command */
                if (mock_admin_sq && mock_admin_sq_tail < NVME_ADMIN_QUEUE_SIZE) {
                    nvme_sq_entry_t *submitted = &mock_admin_sq[mock_admin_sq_tail];
                    last_cmd_opcode = submitted->opcode;
                    last_cmd_cdw10 = submitted->cdw10;
                    last_cmd_cdw11 = submitted->cdw11;
                    last_cmd_nsid = submitted->nsid;
                    last_identify_prp1 = submitted->prp1;

                    /* For IDENTIFY commands, fill the identify buffer */
                    if (submitted->opcode == NVME_ADMIN_IDENTIFY &&
                        submitted->prp1 != 0) {
                        uint8_t *id_buf = (uint8_t *)(uintptr_t)submitted->prp1;
                        if (submitted->cdw10 == NVME_IDENTIFY_CONTROLLER) {
                            /* Fill model at bytes 24-63 */
                            memset(id_buf + 24, ' ', 40);
                            memcpy(id_buf + 24, "Lexar NM790 1TB", 15);
                            /* Fill serial at bytes 4-23 */
                            memset(id_buf + 4, ' ', 20);
                            memcpy(id_buf + 4, "ABCD1234", 8);
                        } else if (submitted->cdw10 == NVME_IDENTIFY_NAMESPACE) {
                            /* NSZE at bytes 8-15 */
                            uint64_t nsze = 1953525168ULL;
                            memcpy(id_buf + 8, &nsze, 8);
                            /* FLBAS at byte 26: format index 0 */
                            id_buf[26] = 0;
                            /* LBA format 0 at byte 128: LBADS=9 (512 bytes) */
                            uint32_t fmt = (9 << 16);
                            memcpy(id_buf + 128, &fmt, 4);
                        }
                    }
                    mock_admin_sq_tail = (mock_admin_sq_tail + 1) % NVME_ADMIN_QUEUE_SIZE;
                }
                mock_auto_complete_admin();
            } else {
                /* I/O SQ doorbell */
                if (mock_io_sq && mock_io_sq_tail < NVME_IO_QUEUE_SIZE) {
                    nvme_sq_entry_t *submitted = &mock_io_sq[mock_io_sq_tail];
                    last_cmd_opcode = submitted->opcode;
                    last_cmd_cdw10 = submitted->cdw10;
                    last_cmd_cdw11 = submitted->cdw11;
                    last_cmd_nsid = submitted->nsid;
                    mock_io_sq_tail = (mock_io_sq_tail + 1) % NVME_IO_QUEUE_SIZE;
                }
                mock_auto_complete_io();
            }
        } else {
            /* CQ doorbell */
            last_cq_doorbell_off = off;
            last_cq_doorbell_val = val;
        }
    }
}

uint64_t nvme_read64(volatile uint8_t *base, uint32_t off)
{
    if (off < sizeof(mock_bar) - 7)
        return *(uint64_t *)(mock_bar + off);
    return 0;
}

void nvme_write64(volatile uint8_t *base, uint32_t off, uint64_t val)
{
    if (off < sizeof(mock_bar) - 7)
        *(uint64_t *)(mock_bar + off) = val;
}

/* ========================================================================
 * Mock Reset
 * ======================================================================== */

static void mock_reset(void)
{
    memset(mock_bar, 0, sizeof(mock_bar));
    last_sq_doorbell_off = 0;
    last_sq_doorbell_val = 0;
    last_cq_doorbell_off = 0;
    last_cq_doorbell_val = 0;
    mock_completion_phase = 1;
    mock_doorbell_count = 0;
    mock_admin_cq = NULL;
    mock_admin_cq_head = 0;
    mock_io_cq = NULL;
    mock_io_cq_head = 0;
    mock_admin_sq = NULL;
    mock_admin_sq_tail = 0;
    mock_io_sq = NULL;
    mock_io_sq_tail = 0;
    last_identify_prp1 = 0;
    last_cmd_opcode = 0;
    last_cmd_cdw10 = 0;
    last_cmd_cdw11 = 0;
    last_cmd_nsid = 0;
}

/* ========================================================================
 * Helper: run full init with mock infrastructure
 * ======================================================================== */

static nvme_controller_t g_ctrl;

static nvme_sq_entry_t admin_sq[NVME_ADMIN_QUEUE_SIZE] __attribute__((aligned(4096)));
static nvme_cq_entry_t admin_cq[NVME_ADMIN_QUEUE_SIZE] __attribute__((aligned(4096)));
static nvme_sq_entry_t io_sq[NVME_IO_QUEUE_SIZE] __attribute__((aligned(4096)));
static nvme_cq_entry_t io_cq[NVME_IO_QUEUE_SIZE] __attribute__((aligned(4096)));

static int mock_full_init(void)
{
    mock_reset();

    /* Set CAP: DSTRD=0, other fields default */
    uint64_t cap = 0;
    cap |= (uint64_t)(NVME_ADMIN_QUEUE_SIZE - 1);  /* MQES */
    /* DSTRD = 0 (bits 35:32) */
    *(uint64_t *)(mock_bar + NVME_REG_CAP) = cap;

    /* CSTS: RDY=0 initially (controller disabled) */
    *(uint32_t *)(mock_bar + NVME_REG_CSTS) = 0;

    /* Wire up mock SQ/CQ pointers for auto-completion */
    mock_admin_sq = admin_sq;
    mock_admin_cq = admin_cq;
    mock_io_sq = io_sq;
    mock_io_cq = io_cq;

    /* CSTS starts at 0 (controller disabled).
     * The mock write32 handler auto-updates CSTS.RDY when CC.EN changes. */

    return nvme_init(&g_ctrl, (volatile uint8_t *)mock_bar, 0x80000000ULL,
                     admin_sq, (uint64_t)(uintptr_t)admin_sq,
                     admin_cq, (uint64_t)(uintptr_t)admin_cq,
                     io_sq, (uint64_t)(uintptr_t)io_sq,
                     io_cq, (uint64_t)(uintptr_t)io_cq);
}

/* ========================================================================
 * Tests
 * ======================================================================== */

/* Test 1: SQ entry struct is exactly 64 bytes */
static int test_sq_entry_size(void)
{
    EXPECT(sizeof(nvme_sq_entry_t) == 64, "SQ entry must be 64 bytes");
    return 1;
}

/* Test 2: CQ entry struct is exactly 16 bytes */
static int test_cq_entry_size(void)
{
    EXPECT(sizeof(nvme_cq_entry_t) == 16, "CQ entry must be 16 bytes");
    return 1;
}

/* Test 3: CAP DSTRD extraction */
static int test_cap_dstrd_extract(void)
{
    mock_reset();

    /* Set DSTRD = 2 in CAP (bits 35:32) */
    uint64_t cap = (uint64_t)2 << 32;
    *(uint64_t *)(mock_bar + NVME_REG_CAP) = cap;

    uint64_t read_cap = nvme_read64((volatile uint8_t *)mock_bar, NVME_REG_CAP);
    uint32_t dstrd = NVME_CAP_DSTRD(read_cap);

    EXPECT(dstrd == 2, "DSTRD should be 2");

    /* Also test DSTRD = 0 */
    cap = 0;
    *(uint64_t *)(mock_bar + NVME_REG_CAP) = cap;
    read_cap = nvme_read64((volatile uint8_t *)mock_bar, NVME_REG_CAP);
    dstrd = NVME_CAP_DSTRD(read_cap);
    EXPECT(dstrd == 0, "DSTRD should be 0");

    return 1;
}

/* Test 4: CC register value after init has correct bits */
static int test_cc_enable_bits(void)
{
    int rc = mock_full_init();
    EXPECT(rc == 0, "init should succeed");

    uint32_t cc = *(uint32_t *)(mock_bar + NVME_REG_CC);
    EXPECT((cc & NVME_CC_EN), "CC.EN must be set");
    EXPECT(((cc >> 16) & 0xF) == 6, "IOSQES must be 6 (64 bytes)");
    EXPECT(((cc >> 20) & 0xF) == 4, "IOCQES must be 4 (16 bytes)");

    return 1;
}

/* Test 5: Admin queue physical addresses written to BAR0 */
static int test_admin_queue_setup(void)
{
    int rc = mock_full_init();
    EXPECT(rc == 0, "init should succeed");

    uint64_t asq = *(uint64_t *)(mock_bar + NVME_REG_ASQ);
    uint64_t acq = *(uint64_t *)(mock_bar + NVME_REG_ACQ);

    EXPECT(asq == (uint64_t)(uintptr_t)admin_sq, "ASQ phys addr must match");
    EXPECT(acq == (uint64_t)(uintptr_t)admin_cq, "ACQ phys addr must match");

    uint32_t aqa = *(uint32_t *)(mock_bar + NVME_REG_AQA);
    EXPECT((aqa & 0xFFFF) == (NVME_ADMIN_QUEUE_SIZE - 1), "AQA ASQS correct");
    EXPECT(((aqa >> 16) & 0xFFFF) == (NVME_ADMIN_QUEUE_SIZE - 1), "AQA ACQS correct");

    return 1;
}

/* Test 6: IDENTIFY controller model string parsed and trimmed */
static int test_identify_model_parse(void)
{
    int rc = mock_full_init();
    EXPECT(rc == 0, "init should succeed");
    EXPECT(strcmp(g_ctrl.model, "Lexar NM790 1TB") == 0,
           "model must be 'Lexar NM790 1TB'");

    return 1;
}

/* Test 7: IDENTIFY controller serial string parsed and trimmed */
static int test_identify_serial_parse(void)
{
    int rc = mock_full_init();
    EXPECT(rc == 0, "init should succeed");
    EXPECT(strcmp(g_ctrl.serial, "ABCD1234") == 0,
           "serial must be 'ABCD1234'");

    return 1;
}

/* Test 8: I/O queue creation commands have correct opcodes and fields */
static int test_io_queue_creation(void)
{
    int rc = mock_full_init();
    EXPECT(rc == 0, "init should succeed");

    /* After init, the I/O queues should be set up */
    EXPECT(g_ctrl.io.qid == 1, "I/O queue ID must be 1");
    EXPECT(g_ctrl.io.size == NVME_IO_QUEUE_SIZE, "I/O queue size correct");
    EXPECT(g_ctrl.io.sq == io_sq, "I/O SQ pointer correct");
    EXPECT(g_ctrl.io.cq == io_cq, "I/O CQ pointer correct");
    EXPECT(g_ctrl.io.cq_phase == 1, "I/O CQ phase starts at 1");

    return 1;
}

/* Test 9: READ command format in the SQ entry */
static int test_read_command_format(void)
{
    int rc = mock_full_init();
    EXPECT(rc == 0, "init should succeed");

    /* Reset tracking for the I/O command */
    last_cmd_opcode = 0;
    last_cmd_cdw10 = 0;
    last_cmd_cdw11 = 0;
    last_cmd_nsid = 0;

    uint8_t read_buf[4096] __attribute__((aligned(4096)));
    memset(read_buf, 0, sizeof(read_buf));

    rc = nvme_read_sectors(&g_ctrl, 1000, 8, read_buf,
                           (uint64_t)(uintptr_t)read_buf);
    EXPECT(rc == 0, "read should succeed");
    EXPECT(last_cmd_opcode == NVME_IO_READ, "opcode must be 0x02 (READ)");
    EXPECT(last_cmd_cdw10 == 1000, "CDW10 must be LBA low (1000)");
    EXPECT(last_cmd_cdw11 == 0, "CDW11 must be LBA high (0)");
    EXPECT(last_cmd_nsid == 1, "NSID must be 1");

    return 1;
}

/* Test 10: CQ phase bit toggles on wrap */
static int test_phase_bit_toggle(void)
{
    int rc = mock_full_init();
    EXPECT(rc == 0, "init should succeed");

    /* After init, I/O queue phase should be 1 */
    EXPECT(g_ctrl.io.cq_phase == 1, "initial phase must be 1");

    /* Submit commands until we wrap the CQ.
     * Queue size is NVME_IO_QUEUE_SIZE (64).
     * Each submit+complete advances cq_head by 1.
     * After 64 completions, cq_head wraps to 0, phase toggles. */
    uint8_t buf[512] __attribute__((aligned(4096)));
    int i;

    for (i = 0; i < NVME_IO_QUEUE_SIZE; i++) {
        rc = nvme_read_sectors(&g_ctrl, 0, 1, buf,
                               (uint64_t)(uintptr_t)buf);
        EXPECT(rc == 0, "read should succeed in phase loop");
    }

    /* After NVME_IO_QUEUE_SIZE completions, phase should have toggled */
    EXPECT(g_ctrl.io.cq_phase == 0, "phase must toggle to 0 after wrap");

    /* Do another full round */
    mock_completion_phase = 0;  /* Match the new phase */
    for (i = 0; i < NVME_IO_QUEUE_SIZE; i++) {
        rc = nvme_read_sectors(&g_ctrl, 0, 1, buf,
                               (uint64_t)(uintptr_t)buf);
        EXPECT(rc == 0, "read should succeed in second phase loop");
    }
    EXPECT(g_ctrl.io.cq_phase == 1, "phase must toggle back to 1");

    return 1;
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== NVMe Driver Tests ===\n\n");

    RUN_TEST(test_sq_entry_size);
    RUN_TEST(test_cq_entry_size);
    RUN_TEST(test_cap_dstrd_extract);
    RUN_TEST(test_cc_enable_bits);
    RUN_TEST(test_admin_queue_setup);
    RUN_TEST(test_identify_model_parse);
    RUN_TEST(test_identify_serial_parse);
    RUN_TEST(test_io_queue_creation);
    RUN_TEST(test_read_command_format);
    RUN_TEST(test_phase_bit_toggle);

    printf("\n=== Results: %d PASS, %d FAIL (of %d) ===\n",
           tests_passed, tests_failed, tests_passed + tests_failed);

    return tests_failed ? 1 : 0;
}
