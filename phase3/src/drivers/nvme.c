/**
 * nvme.c - Minimal read-only NVMe driver for seL4 x86-64 bare metal
 *
 * Controller reset, admin queue setup, IDENTIFY controller + namespace,
 * I/O CQ/SQ creation, polled READ command via PRP1+PRP2 (max 8KB).
 *
 * Follows the same mock/real MMIO switching pattern as ahci.c.
 *
 * JARVIS AI-OS - Phase 3
 */

#include "nvme.h"
#include <string.h>

/* ========================================================================
 * MMIO Access (mock-switchable)
 * ======================================================================== */

#ifndef JARVIS_TEST_MOCK
static inline uint32_t nvme_read32(volatile uint8_t *base, uint32_t off)
{
    return *(volatile uint32_t *)(base + off);
}

static inline void nvme_write32(volatile uint8_t *base, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(base + off) = val;
}

static inline uint64_t nvme_read64(volatile uint8_t *base, uint32_t off)
{
    /* NVMe spec: 64-bit registers must be read as two 32-bit reads.
     * Many controllers don't support atomic 64-bit MMIO transactions. */
    uint32_t lo = *(volatile uint32_t *)(base + off);
    uint32_t hi = *(volatile uint32_t *)(base + off + 4);
    return ((uint64_t)hi << 32) | lo;
}

static inline void nvme_write64(volatile uint8_t *base, uint32_t off, uint64_t val)
{
    /* Write low 32 bits first, then high 32 bits (NVMe spec order) */
    *(volatile uint32_t *)(base + off) = (uint32_t)(val & 0xFFFFFFFF);
    *(volatile uint32_t *)(base + off + 4) = (uint32_t)(val >> 32);
}
#else
/* Mock MMIO for testing -- defined in test file */
extern uint32_t nvme_read32(volatile uint8_t *base, uint32_t off);
extern void     nvme_write32(volatile uint8_t *base, uint32_t off, uint32_t val);
extern uint64_t nvme_read64(volatile uint8_t *base, uint32_t off);
extern void     nvme_write64(volatile uint8_t *base, uint32_t off, uint64_t val);
#endif

/* ========================================================================
 * Timeout Configuration
 * ======================================================================== */

#ifdef JARVIS_TEST_MOCK
#define NVME_TIMEOUT  100
#else
#define NVME_TIMEOUT  5000000
#endif

/* ========================================================================
 * Doorbell Helpers
 * ======================================================================== */

static void nvme_ring_sq_doorbell(nvme_controller_t *ctrl, nvme_queue_t *q)
{
    uint32_t off = NVME_REG_SQ0TDBL + (2 * q->qid) * (4 << ctrl->dstrd);
    nvme_write32(ctrl->bar, off, q->sq_tail);
}

static void nvme_ring_cq_doorbell(nvme_controller_t *ctrl, nvme_queue_t *q)
{
    uint32_t off = NVME_REG_SQ0TDBL + (2 * q->qid + 1) * (4 << ctrl->dstrd);
    nvme_write32(ctrl->bar, off, q->cq_head);
}

/* ========================================================================
 * Command Submission + Polled Completion
 * ======================================================================== */

/**
 * nvme_submit_and_wait - Submit a command and poll for completion
 * @ctrl:  Controller state
 * @q:     Queue pair (admin or I/O)
 * @cmd:   64-byte submission queue entry
 *
 * Returns 0 on success, negative status on NVMe error, -0xFFFF on timeout.
 */
static int nvme_submit_and_wait(nvme_controller_t *ctrl, nvme_queue_t *q,
                                nvme_sq_entry_t *cmd)
{
    int i;
    nvme_cq_entry_t *cqe;
    uint16_t status;

    /* Assign command ID */
    cmd->cid = q->cid_counter++;

    /* Copy command to SQ slot and advance tail */
    memcpy(&q->sq[q->sq_tail], cmd, sizeof(*cmd));
    q->sq_tail = (q->sq_tail + 1) % q->size;

    /* Ring SQ doorbell */
    nvme_ring_sq_doorbell(ctrl, q);

    /* Poll CQ for completion */
    for (i = 0; i < NVME_TIMEOUT; i++) {
        cqe = &q->cq[q->cq_head];
        /* Check phase bit (bit 0 of status word) */
        if ((cqe->status & 1) == q->cq_phase) {
            /* Completion arrived -- extract status (bits 15:1) */
            status = (cqe->status >> 1) & 0x7FFF;

            /* Advance CQ head, toggle phase on wrap */
            q->cq_head = (q->cq_head + 1) % q->size;
            if (q->cq_head == 0)
                q->cq_phase ^= 1;

            /* Ring CQ doorbell */
            nvme_ring_cq_doorbell(ctrl, q);

            return (status == 0) ? 0 : -(int)status;
        }
    }

    return -0xFFFF;  /* Timeout */
}

/* ========================================================================
 * Controller Initialization
 * ======================================================================== */

/**
 * nvme_init - Initialize NVMe controller
 *
 * Returns:
 *   0  success
 *  -1  disable timeout
 *  -2  ready timeout
 *  -3  fatal (CSTS.CFS)
 *  -4  identify controller fail
 *  -5  create I/O CQ fail
 *  -6  create I/O SQ fail
 *  -7  identify namespace fail
 */
int nvme_init(nvme_controller_t *ctrl,
              volatile uint8_t *mmio_base, uint64_t bar_phys,
              void *admin_sq_buf, uint64_t admin_sq_phys,
              void *admin_cq_buf, uint64_t admin_cq_phys,
              void *io_sq_buf, uint64_t io_sq_phys,
              void *io_cq_buf, uint64_t io_cq_phys,
              void *identify_buf, uint64_t identify_phys)
{
    uint64_t cap;
    uint32_t cc, csts, aqa;
    int i, rc;
    nvme_sq_entry_t cmd;

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->bar = mmio_base;
    ctrl->bar_phys = bar_phys;

    ctrl->init_step = 0;

    /* Step 1: Read CAP, extract doorbell stride */
    cap = nvme_read64(ctrl->bar, NVME_REG_CAP);
    ctrl->dstrd = NVME_CAP_DSTRD(cap);
    ctrl->init_step = 1;

    /* Step 2: Disable controller (CC.EN = 0), wait CSTS.RDY = 0 */
    cc = nvme_read32(ctrl->bar, NVME_REG_CC);
    cc &= ~NVME_CC_EN;
    nvme_write32(ctrl->bar, NVME_REG_CC, cc);

    for (i = 0; i < NVME_TIMEOUT; i++) {
        csts = nvme_read32(ctrl->bar, NVME_REG_CSTS);
        if (!(csts & NVME_CSTS_RDY))
            break;
    }
    if (i >= NVME_TIMEOUT)
        return -1;
    ctrl->init_step = 2;

    /* Step 3: Setup admin queues */
    memset(admin_sq_buf, 0, NVME_ADMIN_QUEUE_SIZE * sizeof(nvme_sq_entry_t));
    memset(admin_cq_buf, 0, NVME_ADMIN_QUEUE_SIZE * sizeof(nvme_cq_entry_t));

    ctrl->admin.sq      = (nvme_sq_entry_t *)admin_sq_buf;
    ctrl->admin.cq      = (nvme_cq_entry_t *)admin_cq_buf;
    ctrl->admin.sq_phys = admin_sq_phys;
    ctrl->admin.cq_phys = admin_cq_phys;
    ctrl->admin.sq_tail = 0;
    ctrl->admin.cq_head = 0;
    ctrl->admin.size    = NVME_ADMIN_QUEUE_SIZE;
    ctrl->admin.qid     = 0;
    ctrl->admin.cq_phase = 1;
    ctrl->admin.cid_counter = 0;

    /* AQA: admin SQ size and CQ size (0-based) */
    aqa = ((NVME_ADMIN_QUEUE_SIZE - 1) << 16) | (NVME_ADMIN_QUEUE_SIZE - 1);
    nvme_write32(ctrl->bar, NVME_REG_AQA, aqa);

    /* ASQ and ACQ base addresses */
    nvme_write64(ctrl->bar, NVME_REG_ASQ, admin_sq_phys);
    nvme_write64(ctrl->bar, NVME_REG_ACQ, admin_cq_phys);

    /* Step 4: Enable controller */
    cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K
       | NVME_CC_IOSQES(6) | NVME_CC_IOCQES(4);
    nvme_write32(ctrl->bar, NVME_REG_CC, cc);

    /* Wait for CSTS.RDY = 1 */
    for (i = 0; i < NVME_TIMEOUT; i++) {
        csts = nvme_read32(ctrl->bar, NVME_REG_CSTS);
        if (csts & NVME_CSTS_RDY)
            break;
    }
    if (i >= NVME_TIMEOUT)
        return -2;

    /* Check for fatal error */
    if (csts & NVME_CSTS_CFS)
        return -3;
    ctrl->init_step = 4;

    /* Step 5: IDENTIFY controller (CNS=1) */
    memset(identify_buf, 0, 4096);
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid   = 0;
    cmd.prp1   = identify_phys;
    cmd.prp2   = 0;
    cmd.cdw10  = NVME_IDENTIFY_CONTROLLER;

    rc = nvme_submit_and_wait(ctrl, &ctrl->admin, &cmd);
    ctrl->last_submit_err = rc;
    if (rc != 0)
        return -4;

    /* Parse serial (bytes 4-23) and model (bytes 24-63), trim trailing spaces */
    uint8_t *id = (uint8_t *)identify_buf;
    memcpy(ctrl->serial, &id[4], 20);
    ctrl->serial[20] = '\0';
    for (i = 19; i >= 0 && ctrl->serial[i] == ' '; i--)
        ctrl->serial[i] = '\0';

    memcpy(ctrl->model, &id[24], 40);
    ctrl->model[40] = '\0';
    for (i = 39; i >= 0 && ctrl->model[i] == ' '; i--)
        ctrl->model[i] = '\0';

    /* Step 6: Create I/O Completion Queue (QID=1) */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CREATE_IO_CQ;
    cmd.prp1   = io_cq_phys;
    cmd.cdw10  = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1;  /* size | QID=1 */
    cmd.cdw11  = (1 << 0);  /* Physically contiguous, interrupts disabled */

    rc = nvme_submit_and_wait(ctrl, &ctrl->admin, &cmd);
    if (rc != 0)
        return -5;

    /* Step 7: Create I/O Submission Queue (QID=1, linked to CQ 1) */
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CREATE_IO_SQ;
    cmd.prp1   = io_sq_phys;
    cmd.cdw10  = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1;  /* size | QID=1 */
    cmd.cdw11  = (1 << 16) | (1 << 0);  /* CQID=1 | Physically contiguous */

    rc = nvme_submit_and_wait(ctrl, &ctrl->admin, &cmd);
    if (rc != 0)
        return -6;

    /* Setup I/O queue state */
    memset(io_sq_buf, 0, NVME_IO_QUEUE_SIZE * sizeof(nvme_sq_entry_t));
    memset(io_cq_buf, 0, NVME_IO_QUEUE_SIZE * sizeof(nvme_cq_entry_t));

    ctrl->io.sq      = (nvme_sq_entry_t *)io_sq_buf;
    ctrl->io.cq      = (nvme_cq_entry_t *)io_cq_buf;
    ctrl->io.sq_phys = io_sq_phys;
    ctrl->io.cq_phys = io_cq_phys;
    ctrl->io.sq_tail = 0;
    ctrl->io.cq_head = 0;
    ctrl->io.size    = NVME_IO_QUEUE_SIZE;
    ctrl->io.qid     = 1;
    ctrl->io.cq_phase = 1;
    ctrl->io.cid_counter = 0;

    /* Step 8: IDENTIFY namespace 1 (CNS=0) */
    memset(identify_buf, 0, 4096);
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid   = 1;
    cmd.prp1   = identify_phys;
    cmd.prp2   = 0;
    cmd.cdw10  = NVME_IDENTIFY_NAMESPACE;

    rc = nvme_submit_and_wait(ctrl, &ctrl->admin, &cmd);
    if (rc != 0)
        return -7;

    /* Parse namespace size (bytes 8-15, uint64_t LE) */
    id = (uint8_t *)identify_buf;
    memcpy(&ctrl->ns_lba_count, &id[8], sizeof(uint64_t));

    /* Parse LBA format: FLBAS at byte 26 (bits 3:0 = format index) */
    {
        uint8_t flbas = id[26] & 0x0F;
        /* LBA format table starts at byte 128, each entry is 4 bytes.
         * Bits 23:16 of the entry = LBADS (log2 block size) */
        uint32_t lba_fmt;
        memcpy(&lba_fmt, &id[128 + flbas * 4], sizeof(uint32_t));
        uint8_t lbads = (lba_fmt >> 16) & 0xFF;
        ctrl->ns_block_size = (lbads > 0) ? (1U << lbads) : 512;
    }

    ctrl->ns_id = 1;
    ctrl->initialized = true;

    return 0;
}

/* ========================================================================
 * Read Sectors
 * ======================================================================== */

/**
 * nvme_read_sectors - Read sectors from NVMe namespace
 * @ctrl:     Controller state (must be initialized)
 * @lba:      Starting LBA
 * @count:    Number of sectors to read
 * @buf:      Output buffer
 * @buf_phys: Physical address of output buffer
 *
 * Limited to 2 pages (8KB) per command. Uses PRP1 for the first page
 * and PRP2 for the second page if the transfer crosses a page boundary.
 *
 * Returns 0 on success, negative on error.
 */
int nvme_read_sectors(nvme_controller_t *ctrl,
                      uint64_t lba, uint32_t count,
                      void *buf, uint64_t buf_phys)
{
    nvme_sq_entry_t cmd;
    uint32_t max_sectors;
    uint32_t byte_len;

    if (!ctrl || !ctrl->initialized || !buf)
        return -1;
    if (count == 0)
        return -1;

    /* Limit: max 8KB (2 pages) per command */
    max_sectors = 8192 / ctrl->ns_block_size;
    if (count > max_sectors)
        return -1;

    byte_len = count * ctrl->ns_block_size;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_IO_READ;
    cmd.nsid   = ctrl->ns_id;
    cmd.prp1   = buf_phys;

    /* If transfer > 4096 bytes, set PRP2 for the second page */
    if (byte_len > 4096)
        cmd.prp2 = buf_phys + 4096;

    /* CDW10/11 = starting LBA (64-bit) */
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)(lba >> 32);

    /* CDW12 = number of logical blocks (0-based) */
    cmd.cdw12 = count - 1;

    return nvme_submit_and_wait(ctrl, &ctrl->io, &cmd);
}

/* ========================================================================
 * Info Getter
 * ======================================================================== */

/**
 * nvme_get_info - Get namespace capacity and block size
 * @ctrl:       Controller state (must be initialized)
 * @total_lbas: Output: total number of LBAs
 * @block_size: Output: bytes per LBA
 *
 * Returns 0 on success, -1 if not initialized.
 */
int nvme_get_info(nvme_controller_t *ctrl,
                  uint64_t *total_lbas, uint32_t *block_size)
{
    if (!ctrl || !ctrl->initialized)
        return -1;

    if (total_lbas)
        *total_lbas = ctrl->ns_lba_count;
    if (block_size)
        *block_size = ctrl->ns_block_size;

    return 0;
}
