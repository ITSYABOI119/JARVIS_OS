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
#define NVME_REG_SQ0TDBL 0x1000 /* Doorbell base */

/* ---- CAP register fields ---- */
#define NVME_CAP_MQES(cap)   ((uint16_t)((cap) & 0xFFFF))
#define NVME_CAP_TO(cap)     ((uint8_t)(((cap) >> 24) & 0xFF))
#define NVME_CAP_DSTRD(cap)  ((uint8_t)(((cap) >> 32) & 0xF))
#define NVME_CAP_MPSMIN(cap) ((uint8_t)(((cap) >> 48) & 0xF))
#define NVME_CAP_MPSMAX(cap) ((uint8_t)(((cap) >> 52) & 0xF))
#define NVME_CAP_CSS(cap)    ((uint8_t)(((cap) >> 37) & 0xFF))

/* ---- CC register fields ---- */
#define NVME_CC_EN        (1 << 0)
#define NVME_CC_CSS_NVM   (0 << 4)
#define NVME_CC_MPS_4K    (0 << 7)
#define NVME_CC_AMS_RR    (0 << 11)
#define NVME_CC_SHN_NONE  (0 << 14)
#define NVME_CC_IOSQES(n) ((n) << 16)
#define NVME_CC_IOCQES(n) ((n) << 20)

/* ---- CSTS register fields ---- */
#define NVME_CSTS_RDY     (1 << 0)
#define NVME_CSTS_CFS     (1 << 1)
#define NVME_CSTS_SHST_MASK (3 << 2)

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

/* ---- SET_FEATURES Feature IDs ---- */
#define NVME_FEAT_HOST_MEM_BUFFER 0x0D

/* ---- Submission Queue Entry (64 bytes) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t cid;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
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
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;  /* Bit 0 = phase, bits 15:1 = status code */
} nvme_cq_entry_t;

_Static_assert(sizeof(nvme_cq_entry_t) == 16, "NVMe CQ entry must be 16 bytes");

/* ---- Queue configuration ---- */
#define NVME_ADMIN_QUEUE_SIZE  16
#define NVME_IO_QUEUE_SIZE     64

/* ---- Queue Pair ---- */
typedef struct {
    nvme_sq_entry_t *sq;
    nvme_cq_entry_t *cq;
    uint64_t         sq_phys;
    uint64_t         cq_phys;
    uint16_t         sq_tail;
    uint16_t         cq_head;
    uint16_t         size;
    uint16_t         qid;
    uint8_t          cq_phase;
    uint16_t         cid_counter;
} nvme_queue_t;

/* ---- Controller State ---- */
typedef struct {
    volatile uint8_t *bar;
    uint64_t          bar_phys;
    uint32_t          dstrd;
    nvme_queue_t      admin;
    nvme_queue_t      io;
    uint32_t          ns_id;
    uint64_t          ns_lba_count;
    uint32_t          ns_block_size;
    char              model[41];
    char              serial[21];
    bool              initialized;
    int               last_submit_err;  /* Last nvme_submit_and_wait return code */
    int               init_step;        /* Last init step completed (for debug) */
} nvme_controller_t;

/* ---- Public API ---- */

/* identify_buf: 4KB page-aligned buffer for IDENTIFY commands.
 * identify_phys: its physical address (for DMA). */
int nvme_init(nvme_controller_t *ctrl,
              volatile uint8_t *mmio_base, uint64_t bar_phys,
              void *admin_sq_buf, uint64_t admin_sq_phys,
              void *admin_cq_buf, uint64_t admin_cq_phys,
              void *io_sq_buf, uint64_t io_sq_phys,
              void *io_cq_buf, uint64_t io_cq_phys,
              void *identify_buf, uint64_t identify_phys);

int nvme_read_sectors(nvme_controller_t *ctrl,
                      uint64_t lba, uint32_t count,
                      void *buf, uint64_t buf_phys);

int nvme_get_info(nvme_controller_t *ctrl,
                  uint64_t *total_lbas, uint32_t *block_size);

#endif /* NVME_H */
