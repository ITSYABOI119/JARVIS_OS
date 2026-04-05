# NVMe Driver for seL4 Bare-Metal Model Loading

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Read the 771MB GGUF model file from the NVMe boot drive (Lexar NM790, PCI `1d97:1602`) at runtime, eliminating the need to embed it in the binary (which makes the image 772MB and boot 30+ seconds).

**Architecture:** Minimal read-only NVMe driver using polled submission/completion queues. PCI BAR0 mapped as device frame. DMA buffers from seL4 untypeds (physically contiguous). FAT32 parser locates `model.gguf` on a dedicated data partition. Rootserver reads model into frames, maps frames into Process B, Process B calls `gguf_open_memory()` — same API as embedded path.

**Tech Stack:** seL4 x86-64, NVMe 1.x polled I/O, PCI BAR0 MMIO, FAT32 (read-only), C11.

---

## NVMe Architecture Overview

```
┌──────────────────────────────────────────────────────┐
│  NVMe Controller (Lexar NM790, PCI 01:00.0)         │
│                                                       │
│  BAR0 (MMIO Registers)                               │
│  ┌────────┬────────┬────────┬────────┬──────────┐   │
│  │ CAP    │ VS     │ CC     │ CSTS   │ AQA/ASQ/ │   │
│  │ 0x00   │ 0x08   │ 0x14   │ 0x1C   │ ACQ 0x24 │   │
│  └────────┴────────┴────────┴────────┴──────────┘   │
│  ┌──────────────────────────────────────────────┐    │
│  │ Doorbells: 0x1000 + N * stride               │    │
│  │   SQ0 Tail, CQ0 Head, SQ1 Tail, CQ1 Head   │    │
│  └──────────────────────────────────────────────┘    │
│                                                       │
│                    DMA                                │
│                     ↕                                 │
│  Host Memory (seL4 untypeds, physically contiguous)  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐            │
│  │ Admin SQ │ │ Admin CQ │ │ Identify │            │
│  │ 64B × 16 │ │ 16B × 16 │ │ 4KB buf  │            │
│  └──────────┘ └──────────┘ └──────────┘            │
│  ┌──────────┐ ┌──────────┐ ┌──────────────────┐    │
│  │ I/O SQ   │ │ I/O CQ   │ │ Data buffer      │    │
│  │ 64B × 64 │ │ 16B × 64 │ │ (model pages)    │    │
│  └──────────┘ └──────────┘ └──────────────────┘    │
└──────────────────────────────────────────────────────┘
```

**Key differences from AHCI:**
- No port registers — queues live in host memory, controller DMA-reads commands
- Doorbell writes to BAR0 trigger command processing
- Commands are 64-byte entries (not FIS-based)
- Completion entries are 16-byte with phase bit for new-entry detection
- PRP (Physical Region Pages) replace PRDT for scatter/gather

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `phase3/src/drivers/nvme.h` | Create | NVMe register offsets, command structs, queue structs, public API |
| `phase3/src/drivers/nvme.c` | Create | NVMe driver: init, identify, queue setup, read sectors |
| `phase3/src/drivers/test_nvme.c` | Create | Mock tests: init, identify, queue ops, read |
| `phase3/src/drivers/fat32.h` | Create | FAT32 reader: BPB parsing, cluster chain, file lookup |
| `phase3/src/drivers/fat32.c` | Create | FAT32 implementation (~200 LOC) |
| `phase3/src/drivers/test_fat32.c` | Create | FAT32 mock tests with synthetic disk image |
| `phase3/src/drivers/blk_dev_x86.c` | Modify | Add NVMe backend (alongside AHCI) |
| `phase3/src/sel4/main_x86.c` | Modify | NVMe init + model loading at boot |
| `.github/workflows/ci.yml` | Modify | Add NVMe + FAT32 test steps |
| `CLAUDE.md` | Modify | Update milestones and quick references |

---

## Task 1: NVMe Header — Register Definitions and Data Structures

**Files:**
- Create: `phase3/src/drivers/nvme.h`

This task defines all NVMe constants, register offsets, and data structures. No implementation code.

- [ ] **Step 1: Write nvme.h**

```c
#ifndef NVME_H
#define NVME_H

#include <stdint.h>
#include <stdbool.h>

/* ---- NVMe BAR0 Register Offsets ---- */
#define NVME_REG_CAP     0x00   /* Controller Capabilities (64-bit) */
#define NVME_REG_VS      0x08   /* Version */
#define NVME_REG_INTMS   0x0C   /* Interrupt Mask Set */
#define NVME_REG_INTMC   0x10   /* Interrupt Mask Clear */
#define NVME_REG_CC      0x14   /* Controller Configuration */
#define NVME_REG_CSTS    0x1C   /* Controller Status */
#define NVME_REG_AQA     0x24   /* Admin Queue Attributes */
#define NVME_REG_ASQ     0x28   /* Admin Submission Queue Base (64-bit) */
#define NVME_REG_ACQ     0x30   /* Admin Completion Queue Base (64-bit) */

/* Doorbell base: 0x1000 + (2*qid) * (4 << DSTRD) for SQ tail
 *                0x1000 + (2*qid+1) * (4 << DSTRD) for CQ head */
#define NVME_REG_SQ0TDBL 0x1000

/* ---- CAP register fields ---- */
#define NVME_CAP_MQES(cap)   ((uint16_t)((cap) & 0xFFFF))         /* Max Queue Entries Supported (0-based) */
#define NVME_CAP_TO(cap)     ((uint8_t)(((cap) >> 24) & 0xFF))    /* Timeout (500ms units) */
#define NVME_CAP_DSTRD(cap)  ((uint8_t)(((cap) >> 32) & 0xF))     /* Doorbell Stride */
#define NVME_CAP_MPSMIN(cap) ((uint8_t)(((cap) >> 48) & 0xF))     /* Min Memory Page Size (2^(12+MPSMIN)) */
#define NVME_CAP_MPSMAX(cap) ((uint8_t)(((cap) >> 52) & 0xF))     /* Max Memory Page Size */
#define NVME_CAP_CSS(cap)    ((uint8_t)(((cap) >> 37) & 0xFF))    /* Command Sets Supported */

/* ---- CC register fields ---- */
#define NVME_CC_EN        (1 << 0)   /* Enable */
#define NVME_CC_CSS_NVM   (0 << 4)   /* NVM Command Set */
#define NVME_CC_MPS_4K    (0 << 7)   /* Memory Page Size = 4KB (2^(12+0)) */
#define NVME_CC_AMS_RR    (0 << 11)  /* Round Robin arbitration */
#define NVME_CC_SHN_NONE  (0 << 14)  /* No shutdown */
#define NVME_CC_IOSQES(n) ((n) << 16) /* I/O SQ Entry Size (2^n) */
#define NVME_CC_IOCQES(n) ((n) << 20) /* I/O CQ Entry Size (2^n) */

/* ---- CSTS register fields ---- */
#define NVME_CSTS_RDY     (1 << 0)   /* Ready */
#define NVME_CSTS_CFS     (1 << 1)   /* Controller Fatal Status */
#define NVME_CSTS_SHST_MASK (3 << 2) /* Shutdown Status */

/* ---- Admin Command Opcodes ---- */
#define NVME_ADMIN_IDENTIFY      0x06
#define NVME_ADMIN_CREATE_IO_CQ  0x05
#define NVME_ADMIN_CREATE_IO_SQ  0x01
#define NVME_ADMIN_SET_FEATURES  0x09

/* ---- I/O Command Opcodes ---- */
#define NVME_IO_READ   0x02
#define NVME_IO_WRITE  0x01

/* ---- Identify CNS values ---- */
#define NVME_IDENTIFY_CONTROLLER  0x01
#define NVME_IDENTIFY_NAMESPACE   0x00

/* ---- Submission Queue Entry (64 bytes) ---- */
typedef struct __attribute__((packed)) {
    /* DWORD 0: Command header */
    uint8_t  opcode;
    uint8_t  flags;          /* Fused(1:0), reserved(5:2), PSDT(7:6) */
    uint16_t cid;            /* Command Identifier */

    /* DWORD 1 */
    uint32_t nsid;           /* Namespace Identifier */

    /* DWORD 2-3 */
    uint64_t reserved;

    /* DWORD 4-5 */
    uint64_t mptr;           /* Metadata Pointer */

    /* DWORD 6-9: Data Pointer (2 PRPs or 1 SGL) */
    uint64_t prp1;
    uint64_t prp2;

    /* DWORD 10-15: Command-specific */
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_sq_entry_t;

_Static_assert(sizeof(nvme_sq_entry_t) == 64, "NVMe SQ entry must be 64 bytes");

/* ---- Completion Queue Entry (16 bytes) ---- */
typedef struct __attribute__((packed)) {
    uint32_t result;         /* Command-specific result */
    uint32_t reserved;
    uint16_t sq_head;        /* SQ Head Pointer */
    uint16_t sq_id;          /* SQ Identifier */
    uint16_t cid;            /* Command Identifier */
    uint16_t status;         /* Phase(0), Status(15:1) */
} nvme_cq_entry_t;

_Static_assert(sizeof(nvme_cq_entry_t) == 16, "NVMe CQ entry must be 16 bytes");

/* ---- Queue Pair ---- */
#define NVME_ADMIN_QUEUE_SIZE  16   /* Entries in admin queue */
#define NVME_IO_QUEUE_SIZE     64   /* Entries in I/O queue */

typedef struct {
    nvme_sq_entry_t *sq;     /* Submission queue (host memory, physically contiguous) */
    nvme_cq_entry_t *cq;     /* Completion queue (host memory, physically contiguous) */
    uint64_t         sq_phys; /* Physical address of SQ */
    uint64_t         cq_phys; /* Physical address of CQ */
    uint16_t         sq_tail; /* Next SQ entry to write */
    uint16_t         cq_head; /* Next CQ entry to read */
    uint16_t         size;    /* Queue depth */
    uint16_t         qid;     /* Queue identifier (0 = admin) */
    uint8_t          cq_phase; /* Expected phase bit (toggles on wrap) */
    uint16_t         cid_counter; /* Next command ID */
} nvme_queue_t;

/* ---- Controller State ---- */
typedef struct {
    volatile uint8_t *bar;    /* BAR0 MMIO base (mapped into vspace) */
    uint64_t          bar_phys; /* BAR0 physical address */
    uint32_t          dstrd;  /* Doorbell stride (4 << CAP.DSTRD) */
    nvme_queue_t      admin;  /* Admin queue pair */
    nvme_queue_t      io;     /* I/O queue pair */
    uint32_t          ns_id;  /* Active namespace (usually 1) */
    uint64_t          ns_lba_count; /* Total LBAs in namespace */
    uint32_t          ns_block_size; /* Bytes per LBA (usually 512) */
    char              model[41];    /* Model string from IDENTIFY */
    char              serial[21];   /* Serial from IDENTIFY */
    bool              initialized;
} nvme_controller_t;

/* ---- Public API ---- */

/* Initialize NVMe controller: PCI discovery, BAR mapping, queue setup, IDENTIFY.
 * mmio_base: virtual address of mapped BAR0. bar_phys: physical address of BAR0.
 * sq/cq buffers: caller provides physically contiguous, page-aligned memory. */
int nvme_init(nvme_controller_t *ctrl,
              volatile uint8_t *mmio_base, uint64_t bar_phys,
              void *admin_sq_buf, uint64_t admin_sq_phys,
              void *admin_cq_buf, uint64_t admin_cq_phys,
              void *io_sq_buf, uint64_t io_sq_phys,
              void *io_cq_buf, uint64_t io_cq_phys);

/* Read sectors from namespace. buf must be physically contiguous.
 * buf_phys: physical address of buf (for DMA). */
int nvme_read_sectors(nvme_controller_t *ctrl,
                      uint64_t lba, uint32_t count,
                      void *buf, uint64_t buf_phys);

/* Get namespace info after init */
int nvme_get_info(nvme_controller_t *ctrl,
                  uint64_t *total_lbas, uint32_t *block_size);

#endif /* NVME_H */
```

- [ ] **Step 2: Commit**

```bash
git add phase3/src/drivers/nvme.h
git commit -m "feat: NVMe header — register definitions, command/completion structs, queue API"
```

---

## Task 2: NVMe Driver Core — Controller Init and IDENTIFY

**Files:**
- Create: `phase3/src/drivers/nvme.c`
- Create: `phase3/src/drivers/test_nvme.c`

Implements controller reset, admin queue setup, IDENTIFY controller, IDENTIFY namespace, I/O queue creation.

- [ ] **Step 1: Write nvme.c with MMIO helpers and controller init**

```c
#include "nvme.h"
#include <string.h>

/* ---- MMIO Access ---- */
#ifndef JARVIS_TEST_MOCK
static inline uint32_t nvme_read32(volatile uint8_t *base, uint32_t off) {
    return *(volatile uint32_t *)(base + off);
}
static inline void nvme_write32(volatile uint8_t *base, uint32_t off, uint32_t val) {
    *(volatile uint32_t *)(base + off) = val;
}
static inline uint64_t nvme_read64(volatile uint8_t *base, uint32_t off) {
    uint32_t lo = *(volatile uint32_t *)(base + off);
    uint32_t hi = *(volatile uint32_t *)(base + off + 4);
    return ((uint64_t)hi << 32) | lo;
}
static inline void nvme_write64(volatile uint8_t *base, uint32_t off, uint64_t val) {
    *(volatile uint32_t *)(base + off) = (uint32_t)val;
    *(volatile uint32_t *)(base + off + 4) = (uint32_t)(val >> 32);
}
#else
extern uint32_t nvme_read32(volatile uint8_t *base, uint32_t off);
extern void     nvme_write32(volatile uint8_t *base, uint32_t off, uint32_t val);
extern uint64_t nvme_read64(volatile uint8_t *base, uint32_t off);
extern void     nvme_write64(volatile uint8_t *base, uint32_t off, uint64_t val);
#endif

/* ---- Doorbell helpers ---- */
static void nvme_ring_sq_doorbell(nvme_controller_t *ctrl, nvme_queue_t *q) {
    uint32_t off = NVME_REG_SQ0TDBL + (2 * q->qid) * ctrl->dstrd;
    nvme_write32(ctrl->bar, off, q->sq_tail);
}

static void nvme_ring_cq_doorbell(nvme_controller_t *ctrl, nvme_queue_t *q) {
    uint32_t off = NVME_REG_SQ0TDBL + (2 * q->qid + 1) * ctrl->dstrd;
    nvme_write32(ctrl->bar, off, q->cq_head);
}

/* ---- Submit command and poll for completion ---- */
static int nvme_submit_and_wait(nvme_controller_t *ctrl, nvme_queue_t *q,
                                 nvme_sq_entry_t *cmd)
{
    /* Assign command ID */
    cmd->cid = q->cid_counter++;

    /* Copy command to SQ tail slot */
    memcpy(&q->sq[q->sq_tail], cmd, sizeof(nvme_sq_entry_t));
    q->sq_tail = (q->sq_tail + 1) % q->size;

    /* Ring SQ doorbell */
    nvme_ring_sq_doorbell(ctrl, q);

    /* Poll CQ for completion */
#ifdef JARVIS_TEST_MOCK
    int timeout = 100;
#else
    int timeout = 5000000; /* ~5 seconds at ~1us/iter */
#endif
    while (timeout-- > 0) {
        nvme_cq_entry_t *cqe = &q->cq[q->cq_head];
        /* Check phase bit — new entry has phase matching expected */
        if ((cqe->status & 1) == q->cq_phase) {
            /* Got completion. Check status (bits 15:1). */
            uint16_t sc = (cqe->status >> 1) & 0x7FFF;
            /* Advance CQ head */
            q->cq_head = (q->cq_head + 1) % q->size;
            if (q->cq_head == 0) q->cq_phase ^= 1; /* Toggle phase on wrap */
            /* Ring CQ doorbell */
            nvme_ring_cq_doorbell(ctrl, q);
            return (sc == 0) ? 0 : -(int)sc;
        }
    }
    return -0xFFFF; /* Timeout */
}

/* ---- Controller Reset + Init ---- */
int nvme_init(nvme_controller_t *ctrl,
              volatile uint8_t *mmio_base, uint64_t bar_phys,
              void *admin_sq_buf, uint64_t admin_sq_phys,
              void *admin_cq_buf, uint64_t admin_cq_phys,
              void *io_sq_buf, uint64_t io_sq_phys,
              void *io_cq_buf, uint64_t io_cq_phys)
{
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->bar = mmio_base;
    ctrl->bar_phys = bar_phys;

    /* Read capabilities */
    uint64_t cap = nvme_read64(ctrl->bar, NVME_REG_CAP);
    ctrl->dstrd = 4 << NVME_CAP_DSTRD(cap);
    uint32_t timeout_500ms = NVME_CAP_TO(cap); /* Worst-case timeout in 500ms units */
    (void)timeout_500ms;

    /* 1. Disable controller (CC.EN = 0) */
    nvme_write32(ctrl->bar, NVME_REG_CC, 0);

    /* 2. Wait for CSTS.RDY = 0 */
#ifdef JARVIS_TEST_MOCK
    int wait = 100;
#else
    int wait = 5000000;
#endif
    while ((nvme_read32(ctrl->bar, NVME_REG_CSTS) & NVME_CSTS_RDY) && wait-- > 0)
        ;
    if (wait <= 0) return -1; /* Timeout waiting for disable */

    /* 3. Setup admin queues */
    ctrl->admin.sq = (nvme_sq_entry_t *)admin_sq_buf;
    ctrl->admin.cq = (nvme_cq_entry_t *)admin_cq_buf;
    ctrl->admin.sq_phys = admin_sq_phys;
    ctrl->admin.cq_phys = admin_cq_phys;
    ctrl->admin.size = NVME_ADMIN_QUEUE_SIZE;
    ctrl->admin.qid = 0;
    ctrl->admin.sq_tail = 0;
    ctrl->admin.cq_head = 0;
    ctrl->admin.cq_phase = 1;
    ctrl->admin.cid_counter = 0;
    memset(admin_sq_buf, 0, NVME_ADMIN_QUEUE_SIZE * sizeof(nvme_sq_entry_t));
    memset(admin_cq_buf, 0, NVME_ADMIN_QUEUE_SIZE * sizeof(nvme_cq_entry_t));

    /* AQA: admin queue size (0-based) */
    nvme_write32(ctrl->bar, NVME_REG_AQA,
        ((NVME_ADMIN_QUEUE_SIZE - 1) << 16) | (NVME_ADMIN_QUEUE_SIZE - 1));

    /* ASQ/ACQ base addresses */
    nvme_write64(ctrl->bar, NVME_REG_ASQ, admin_sq_phys);
    nvme_write64(ctrl->bar, NVME_REG_ACQ, admin_cq_phys);

    /* 4. Enable controller: NVM command set, 4K pages, SQ entry=64B, CQ entry=16B */
    uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
                  NVME_CC_AMS_RR | NVME_CC_IOSQES(6) | NVME_CC_IOCQES(4);
    nvme_write32(ctrl->bar, NVME_REG_CC, cc);

    /* 5. Wait for CSTS.RDY = 1 */
#ifdef JARVIS_TEST_MOCK
    wait = 100;
#else
    wait = 5000000;
#endif
    while (!(nvme_read32(ctrl->bar, NVME_REG_CSTS) & NVME_CSTS_RDY) && wait-- > 0)
        ;
    if (wait <= 0) return -2; /* Timeout waiting for ready */
    if (nvme_read32(ctrl->bar, NVME_REG_CSTS) & NVME_CSTS_CFS) return -3; /* Fatal */

    /* 6. IDENTIFY Controller */
    uint8_t identify_buf[4096] __attribute__((aligned(4096)));
    memset(identify_buf, 0, sizeof(identify_buf));

    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 0;
    cmd.prp1 = (uint64_t)(uintptr_t)identify_buf; /* Physical addr (identity-mapped or caller provides) */
    cmd.cdw10 = NVME_IDENTIFY_CONTROLLER;

    int err = nvme_submit_and_wait(ctrl, &ctrl->admin, &cmd);
    if (err) return -4;

    /* Parse controller identify: model (bytes 24-63), serial (bytes 4-23) */
    memcpy(ctrl->serial, identify_buf + 4, 20);
    ctrl->serial[20] = '\0';
    memcpy(ctrl->model, identify_buf + 24, 40);
    ctrl->model[40] = '\0';
    /* Trim trailing spaces */
    for (int i = 19; i >= 0 && ctrl->serial[i] == ' '; i--) ctrl->serial[i] = '\0';
    for (int i = 39; i >= 0 && ctrl->model[i] == ' '; i--) ctrl->model[i] = '\0';

    /* 7. Create I/O Completion Queue (ID=1) */
    ctrl->io.cq = (nvme_cq_entry_t *)io_cq_buf;
    ctrl->io.cq_phys = io_cq_phys;
    ctrl->io.size = NVME_IO_QUEUE_SIZE;
    ctrl->io.qid = 1;
    ctrl->io.cq_head = 0;
    ctrl->io.cq_phase = 1;
    memset(io_cq_buf, 0, NVME_IO_QUEUE_SIZE * sizeof(nvme_cq_entry_t));

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CREATE_IO_CQ;
    cmd.prp1 = io_cq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1; /* Size | QID=1 */
    cmd.cdw11 = 1; /* Physically contiguous, interrupts disabled */

    err = nvme_submit_and_wait(ctrl, &ctrl->admin, &cmd);
    if (err) return -5;

    /* 8. Create I/O Submission Queue (ID=1, linked to CQ 1) */
    ctrl->io.sq = (nvme_sq_entry_t *)io_sq_buf;
    ctrl->io.sq_phys = io_sq_phys;
    ctrl->io.sq_tail = 0;
    ctrl->io.cid_counter = 0;
    memset(io_sq_buf, 0, NVME_IO_QUEUE_SIZE * sizeof(nvme_sq_entry_t));

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_CREATE_IO_SQ;
    cmd.prp1 = io_sq_phys;
    cmd.cdw10 = ((NVME_IO_QUEUE_SIZE - 1) << 16) | 1; /* Size | QID=1 */
    cmd.cdw11 = (1 << 16) | 1; /* CQID=1 | Physically contiguous */

    err = nvme_submit_and_wait(ctrl, &ctrl->admin, &cmd);
    if (err) return -6;

    /* 9. IDENTIFY Namespace 1 */
    memset(identify_buf, 0, sizeof(identify_buf));
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 1;
    cmd.prp1 = (uint64_t)(uintptr_t)identify_buf;
    cmd.cdw10 = NVME_IDENTIFY_NAMESPACE;

    err = nvme_submit_and_wait(ctrl, &ctrl->admin, &cmd);
    if (err) return -7;

    /* Parse namespace size and block size */
    uint64_t nsze;
    memcpy(&nsze, identify_buf + 8, 8); /* Bytes 8-15: Namespace Size (LBAs) */
    ctrl->ns_id = 1;
    ctrl->ns_lba_count = nsze;

    uint8_t flbas = identify_buf[26]; /* Formatted LBA Size byte */
    uint8_t lba_format_idx = flbas & 0x0F;
    /* LBA format table starts at byte 128, each entry 4 bytes */
    uint32_t lbaf;
    memcpy(&lbaf, identify_buf + 128 + lba_format_idx * 4, 4);
    uint8_t lba_ds = (lbaf >> 16) & 0xFF; /* Data size as power of 2 */
    ctrl->ns_block_size = (1U << lba_ds);

    ctrl->initialized = true;
    return 0;
}

/* ---- Read Sectors ---- */
int nvme_read_sectors(nvme_controller_t *ctrl,
                      uint64_t lba, uint32_t count,
                      void *buf, uint64_t buf_phys)
{
    if (!ctrl->initialized) return -1;
    if (count == 0 || count > 256) return -2; /* Max 256 sectors per command (128KB at 512B) */

    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = NVME_IO_READ;
    cmd.nsid = ctrl->ns_id;
    cmd.prp1 = buf_phys;

    /* PRP2: needed if transfer crosses a page boundary.
     * For simplicity, if count * block_size > 4096, use PRP2.
     * If > 8192, need a PRP list (not supported yet — limit reads to 8 sectors). */
    uint32_t transfer_bytes = count * ctrl->ns_block_size;
    if (transfer_bytes > 4096) {
        cmd.prp2 = buf_phys + 4096; /* Second page */
    }

    /* CDW10-11: Starting LBA */
    cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFF);
    cmd.cdw11 = (uint32_t)(lba >> 32);

    /* CDW12: Number of Logical Blocks (0-based) */
    cmd.cdw12 = (count - 1) & 0xFFFF;

    return nvme_submit_and_wait(ctrl, &ctrl->io, &cmd);
}

int nvme_get_info(nvme_controller_t *ctrl,
                  uint64_t *total_lbas, uint32_t *block_size)
{
    if (!ctrl->initialized) return -1;
    if (total_lbas) *total_lbas = ctrl->ns_lba_count;
    if (block_size) *block_size = ctrl->ns_block_size;
    return 0;
}
```

- [ ] **Step 2: Write test_nvme.c with mock MMIO**

Write 10 tests covering: controller version read, reset sequence, admin queue setup, identify parse (model/serial strings), I/O queue creation, read command formatting, timeout handling, namespace info, error status propagation, re-init.

The mock should use a `uint8_t mock_bar[8192]` for MMIO registers. The mock `nvme_read32/write32` read/write from `mock_bar`. The mock `nvme_submit_and_wait` can be tested by pre-populating completion queue entries with expected phase bits and status.

Pattern: same as test_ahci.c — define `JARVIS_TEST_MOCK`, provide extern MMIO functions, pre-populate mock registers.

```c
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "nvme.h"

/* Mock MMIO */
static uint8_t mock_bar[8192];

uint32_t nvme_read32(volatile uint8_t *base, uint32_t off) {
    (void)base;
    uint32_t val;
    memcpy(&val, mock_bar + off, 4);
    return val;
}
void nvme_write32(volatile uint8_t *base, uint32_t off, uint32_t val) {
    (void)base;
    memcpy(mock_bar + off, &val, 4);
}
uint64_t nvme_read64(volatile uint8_t *base, uint32_t off) {
    (void)base;
    uint32_t lo, hi;
    memcpy(&lo, mock_bar + off, 4);
    memcpy(&hi, mock_bar + off + 4, 4);
    return ((uint64_t)hi << 32) | lo;
}
void nvme_write64(volatile uint8_t *base, uint32_t off, uint64_t val) {
    (void)base;
    memcpy(mock_bar + off, &(uint32_t){(uint32_t)val}, 4);
    memcpy(mock_bar + off + 4, &(uint32_t){(uint32_t)(val >> 32)}, 4);
}

/* Test helpers */
static int pass = 0, fail = 0;
#define EXPECT(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fail++; } \
    else { printf("PASS: %s\n", msg); pass++; } \
} while(0)

/* Set mock CSTS.RDY for init flow */
static void mock_setup_ready_controller(void) {
    memset(mock_bar, 0, sizeof(mock_bar));
    /* CAP: MQES=63, TO=1, DSTRD=0, MPSMIN=0, CSS=1 (NVM) */
    uint64_t cap = 63 | ((uint64_t)1 << 24) | ((uint64_t)1 << 37);
    memcpy(mock_bar + NVME_REG_CAP, &cap, 8);
    /* VS: 1.0 */
    uint32_t vs = 0x00010000;
    memcpy(mock_bar + NVME_REG_VS, &vs, 4);
    /* CSTS: RDY will be set after CC.EN write — mock handles this */
}

/* Implement tests: test_cap_read, test_version_read, test_dstrd_extract,
 * test_reset_disables, test_identify_model_parse, test_sq_entry_size,
 * test_cq_entry_size, test_read_cmd_format, test_phase_bit_toggle,
 * test_error_status_propagation */

int main(void) {
    printf("=== NVMe Driver Tests ===\n");
    /* ... 10 tests ... */
    printf("\nResults: %d PASS, %d FAIL (of %d)\n", pass, fail, pass + fail);
    return fail > 0 ? 1 : 0;
}
```

Fill in 10 concrete test functions. Each one validates a specific aspect of the NVMe init flow or command formatting.

- [ ] **Step 3: Verify tests compile and pass**

```bash
gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
  -I phase3/src/drivers \
  phase3/src/drivers/test_nvme.c phase3/src/drivers/nvme.c \
  -o /tmp/test_nvme && /tmp/test_nvme
```

- [ ] **Step 4: Add CI step**

```yaml
    - name: "Phase 3: NVMe Driver (C)"
      run: |
        gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
          -I phase3/src/drivers \
          phase3/src/drivers/test_nvme.c phase3/src/drivers/nvme.c \
          -o /tmp/test_nvme
        /tmp/test_nvme
```

- [ ] **Step 5: Commit**

```bash
git add phase3/src/drivers/nvme.c phase3/src/drivers/test_nvme.c .github/workflows/ci.yml
git commit -m "feat: NVMe driver core — controller init, IDENTIFY, I/O queue creation, READ"
```

---

## Task 3: FAT32 Read-Only Parser

**Files:**
- Create: `phase3/src/drivers/fat32.h`
- Create: `phase3/src/drivers/fat32.c`
- Create: `phase3/src/drivers/test_fat32.c`

Minimal FAT32 parser that can locate and read a file by name from a FAT32 partition. Read-only, no subdirectory traversal (model file in root directory).

- [ ] **Step 1: Write fat32.h**

```c
#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

/* Callback: read sectors from block device.
 * lba = absolute sector on disk. count = number of sectors. buf = output. */
typedef int (*fat32_read_fn)(uint64_t lba, uint32_t count, void *buf);

typedef struct {
    fat32_read_fn read;           /* Sector read callback */
    uint64_t      part_lba;       /* Partition start LBA */
    uint32_t      sectors_per_cluster;
    uint32_t      bytes_per_sector;
    uint32_t      reserved_sectors;
    uint32_t      fat_size_sectors; /* Sectors per FAT */
    uint8_t       num_fats;
    uint32_t      root_cluster;   /* First cluster of root directory */
    uint64_t      data_lba;       /* LBA of cluster 2 (first data cluster) */
    uint64_t      fat_lba;        /* LBA of first FAT */
} fat32_fs_t;

/* Initialize FAT32 from partition at given LBA offset. Reads BPB. */
int fat32_init(fat32_fs_t *fs, fat32_read_fn read, uint64_t part_lba);

/* Find file in root directory. Returns first cluster and file size. */
int fat32_find_file(fat32_fs_t *fs, const char *name_8_3,
                    uint32_t *first_cluster, uint32_t *file_size);

/* Read entire file by following cluster chain.
 * buf must be large enough for file_size bytes. */
int fat32_read_file(fat32_fs_t *fs, uint32_t first_cluster,
                    uint32_t file_size, void *buf);

#endif
```

- [ ] **Step 2: Write fat32.c**

~200 LOC implementing BPB parsing, FAT table reading, cluster chain following, root directory scanning. The 8.3 filename comparison is case-insensitive.

Key implementation details:
- `fat32_init`: read sector 0 of partition, parse BPB fields, compute data_lba
- `fat32_find_file`: read root directory clusters, scan 32-byte directory entries for matching 8.3 name
- `fat32_read_file`: follow cluster chain via FAT table, read each cluster

- [ ] **Step 3: Write test_fat32.c**

Build a synthetic FAT32 disk image in memory (mock_disk array). Create a minimal valid BPB, FAT table, root directory with one file entry ("MODEL   GUF" in 8.3 format), and file data. Test:
1. BPB parsing
2. File found in root directory
3. File not found returns error
4. Cluster chain following
5. File data read correctly
6. Multi-cluster file

~8 tests.

- [ ] **Step 4: Verify and add CI**

```bash
gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
  -I phase3/src/drivers \
  phase3/src/drivers/test_fat32.c phase3/src/drivers/fat32.c \
  -o /tmp/test_fat32 && /tmp/test_fat32
```

- [ ] **Step 5: Commit**

```bash
git add phase3/src/drivers/fat32.h phase3/src/drivers/fat32.c \
        phase3/src/drivers/test_fat32.c .github/workflows/ci.yml
git commit -m "feat: FAT32 read-only parser — BPB, root directory scan, cluster chain read"
```

---

## Task 4: Wire NVMe into Block Device + seL4 Integration

**Files:**
- Modify: `phase3/src/drivers/blk_dev_x86.c`
- Modify: `phase3/src/sel4/main_x86.c`
- Modify: `phase3/scripts/build_jarvis_x86.sh`

- [ ] **Step 1: Add NVMe backend to blk_dev_x86.c**

In the real (non-mock) `blk_dev_init()`, after trying AHCI, try NVMe:

```c
#ifndef JARVIS_TEST_MOCK
int blk_dev_init(void) {
    pci_device_t devs[32];
    int n = pci_scan(devs, 32);

    /* Try AHCI first (existing code) */
    pci_device_t *ahci = pci_find_device(PCI_CLASS_STORAGE, PCI_SUBCLASS_SATA, devs, n);
    if (ahci) {
        /* ... existing AHCI init ... */
    }

    /* Try NVMe if no AHCI device found */
    pci_device_t *nvme_pci = pci_find_device(PCI_CLASS_STORAGE, PCI_SUBCLASS_NVM, devs, n);
    if (nvme_pci) {
        pci_enable_bus_master(nvme_pci);
        uint64_t bar0 = pci_get_bar_address(nvme_pci, 0);
        /* Map BAR0 as device frame, init controller, etc. */
        /* Route blk_dev_read to nvme_read_sectors */
    }

    return -1;
}
#endif
```

- [ ] **Step 2: Add NVMe BAR0 mapping in main_x86.c**

Similar to VGA mapping, map NVMe BAR0 as a device frame:

```c
/* NVMe BAR0 is typically 16KB-64KB. Map enough pages. */
uint64_t nvme_bar0_phys = pci_get_bar_address(&nvme_pci_dev, 0);
/* Use vka_alloc_frame_at for each 4KB page of BAR0 */
```

- [ ] **Step 3: Add model loading from NVMe**

In `main_continued()`, after NVMe init:

```c
/* 1. Init NVMe */
/* 2. Init FAT32 on data partition */
/* 3. Find model.gguf */
/* 4. Read model into allocated frames */
/* 5. Map frames into Process B */
/* 6. Pass address to Process B via argv */
```

- [ ] **Step 4: Update build script to sync new files**

Add nvme.c/h and fat32.c/h to `build_jarvis_x86.sh` copy list.

- [ ] **Step 5: Commit**

```bash
git add phase3/src/drivers/blk_dev_x86.c phase3/src/sel4/main_x86.c \
        phase3/scripts/build_jarvis_x86.sh
git commit -m "feat: wire NVMe + FAT32 into blk_dev and rootserver for model loading"
```

---

## Task 5: Create FAT32 Data Partition on NVMe

**Files:**
- Create: `phase3/scripts/setup_nvme_partition.sh`

Script to create a small FAT32 partition on the NVMe for JARVIS data files. Run from Ubuntu on the JARVIS PC.

- [ ] **Step 1: Write partition setup script**

```bash
#!/bin/bash
# Create a FAT32 data partition on NVMe for JARVIS model files.
# Resizes the existing ext4 partition to make room.
# WARNING: Backup data first!
# Usage: sudo bash setup_nvme_partition.sh
```

The NVMe currently has Ubuntu on partition 3 (198GB ext4). Options:
- Shrink partition 3 by 2GB, create partition 4 as FAT32
- Or use an existing unused partition

Copy model.gguf to the new FAT32 partition.

- [ ] **Step 2: Commit**

```bash
git add phase3/scripts/setup_nvme_partition.sh
git commit -m "feat: NVMe FAT32 data partition setup script"
```

---

## Task 6: Test in QEMU with NVMe Emulation

**Files:**
- Modify: `phase3/scripts/qemu_test.sh`

QEMU can emulate NVMe drives with FAT32.

- [ ] **Step 1: Create a FAT32 disk image with model**

```bash
# Create 1GB disk image with FAT32
dd if=/dev/zero of=/tmp/jarvis_data.img bs=1M count=1024
mkfs.fat -F 32 /tmp/jarvis_data.img
mkdir -p /tmp/mnt_jarvis && sudo mount /tmp/jarvis_data.img /tmp/mnt_jarvis
sudo cp ~/Desktop/JARVIS_OS/phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf /tmp/mnt_jarvis/MODEL.GUF
sudo umount /tmp/mnt_jarvis
```

Note: FAT32 8.3 name for the file. Use short name format.

- [ ] **Step 2: Add NVMe emulation to QEMU script**

```bash
qemu-system-x86_64 ... \
    -drive file=/tmp/jarvis_data.img,if=none,id=nvme0,format=raw \
    -device nvme,serial=JARVIS_DATA,drive=nvme0
```

- [ ] **Step 3: Verify model loading from emulated NVMe**

Boot, check:
```
[JARVIS] NVMe: Lexar NM790 (emulated)
[JARVIS] FAT32: MODEL.GUF found, 771MB
[JARVIS] Reading model from NVMe...
[Process B] Model loaded: 16 layers
```

- [ ] **Step 4: Commit**

```bash
git add phase3/scripts/qemu_test.sh
git commit -m "test: QEMU NVMe emulation for model loading"
```

---

## Task 7: Test on Bare Metal + Documentation

**Files:**
- Modify: `CLAUDE.md`
- Modify: `phase3/docs/BARE_METAL_BOOT_GUIDE.md`

- [ ] **Step 1: Setup FAT32 partition on real NVMe**

```bash
sudo bash phase3/scripts/setup_nvme_partition.sh
```

- [ ] **Step 2: Boot and verify**

Model should load from NVMe on boot. Verify inference works.

- [ ] **Step 3: Update CLAUDE.md**

Add milestones:
```
| NVMe driver (Lexar NM790, PCI 1d97:1602) | DONE |
| FAT32 partition reader (model.gguf from NVMe) | DONE |
| Runtime model loading (no embedded binary) | DONE |
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md phase3/docs/BARE_METAL_BOOT_GUIDE.md
git commit -m "docs: NVMe model loading milestone — runtime GGUF from disk"
```

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| NVMe controller requires specific quirks | Medium | High | Start with QEMU NVMe (clean spec impl), test real hardware incrementally |
| DRAM-less controller needs HMB (Host Memory Buffer) | Medium | Medium | Lexar NM790 may work without HMB for basic I/O; SET_FEATURES can configure HMB if needed |
| BAR0 mapping fails (64-bit, large size) | Low | High | 64-bit BAR support already in pci.c; VGA mapping pattern proven |
| DMA physical addresses wrong in seL4 | Medium | High | Use vka_frame_paddr() or identity-mapped untypeds; verify with single-sector read first |
| FAT32 cluster chain corrupted | Low | Medium | Validate with known-good image; checksums |
| NVMe BAR0 too large for single untyped | Low | Medium | Map multiple 4KB pages; NVMe BAR0 typically 16-64KB |
| 771MB read takes too long | Low | Low | NVMe reads at 1-3 GB/s; ~0.5s even single-threaded polled |

## Effort Estimate

| Task | Hours | Notes |
|------|-------|-------|
| Task 1: NVMe header | 1-2 | Struct definitions |
| Task 2: NVMe driver + tests | 6-8 | Core complexity |
| Task 3: FAT32 parser + tests | 3-4 | Well-understood format |
| Task 4: seL4 integration | 4-6 | BAR mapping, DMA buffers |
| Task 5: Partition setup | 1 | Script |
| Task 6: QEMU testing | 2-3 | Debug cycle |
| Task 7: Bare metal + docs | 2-3 | Debug cycle |
| **Total** | **19-27** | ~3-4 sessions |

## Key Constraint: PRP Lists for Large Reads

The initial implementation limits reads to 8KB (2 pages) per command because it only uses PRP1 and PRP2. For reading the 771MB model, this means ~96K NVMe commands (each reading 8KB).

**Phase 2 optimization:** Add PRP list support. A PRP list is a page of 512 PRP entries (each 8 bytes), allowing 2MB per read command. This reduces to ~386 commands for the full model. But for Task 2, the simple 2-page approach works and is simpler to debug.
