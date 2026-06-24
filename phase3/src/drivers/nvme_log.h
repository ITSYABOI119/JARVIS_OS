/*
 * JARVIS AI-OS - NVMe Write Log
 *
 * Persistent event log stored on NVMe past all FAT32 partitions.
 * Header at sector NVME_LOG_BASE_LBA, entries at +1..+N.
 * XOR checksum on header for corruption detection (same pattern
 * as warm_reboot.c).
 *
 * Requires nvme_write_sectors() — added alongside nvme_read_sectors().
 */

#ifndef NVME_LOG_H
#define NVME_LOG_H

#include "nvme.h"
#include <stdint.h>

/* Log region starts past all partitions on the NVMe. CIRCULAR / rolling buffer:
 * keeps the latest NVME_LOG_MAX_ENTRIES (cursor = the wrapping write slot;
 * total_entries = the monotonic lifetime count). Never fills / never returns -2.
 * JARVIS PC (Lexar NM790 2TB, 4,000,797,360 sectors total): p4 ends
 * at sector 4000794623. Start at 4000794624 (next sector after the
 * last partition). Layout = header (BASE) + 2700 entry slots (BASE+1..BASE+2700)
 * = 2701 sectors (~1.4 MB), fitting the ~2,736-sector disk tail with ~35 sectors
 * spare — so do NOT raise NVME_LOG_MAX_ENTRIES without re-checking the tail
 * (sectors = /sys/block/nvme0n1/size minus the log base LBA). Recovery dd reads
 * count=2701 (header + all 2700 slots). */
#define NVME_LOG_BASE_LBA    4000794624ULL
#define NVME_LOG_MAX_ENTRIES  2700
#define NVME_LOG_MAGIC        0x4A524C47  /* "JRLG" */
#define NVME_LOG_VERSION      1

/* Header sector (512 bytes, only first 64 used) */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t cursor;        /* circular WRITE slot (0..MAX-1; entry i at LBA BASE+1+i) */
    uint32_t total_entries;
    uint32_t boot_id;
    uint32_t reserved[10];
    uint32_t checksum;      /* XOR of words 0..14 */
} nvme_log_header_t;

_Static_assert(sizeof(nvme_log_header_t) <= 512, "log header must fit one sector");

/* Entry types */
#define LOG_BOOT         0x0001
#define LOG_SELFTEST     0x0002
#define LOG_MODEL_LOAD   0x0003
#define LOG_INFER        0x0004
#define LOG_IPC_STATS    0x0005
#define LOG_ERROR        0x0006
#define LOG_HEARTBEAT    0x0007

/* Log entry (512 bytes = 1 sector) */
typedef struct __attribute__((packed)) {
    uint32_t boot_id;
    uint32_t seq;           /* entry sequence number */
    uint16_t type;
    uint16_t length;        /* payload bytes (max 496) */
    uint32_t reserved;      /* pad to 16-byte entry header */
    uint8_t  payload[496];
} nvme_log_entry_t;

_Static_assert(sizeof(nvme_log_entry_t) == 512, "log entry must be exactly one sector");

/* ---- Public API ----
 * All functions require an initialized nvme_controller_t and a 512-byte
 * DMA buffer (page-aligned) with its physical address. */

int  nvme_log_init(nvme_controller_t *ctrl, void *dma_buf, uint64_t dma_phys);
int  nvme_log_write(nvme_controller_t *ctrl, void *dma_buf, uint64_t dma_phys,
                    uint16_t type, const char *msg);
int  nvme_log_flush(nvme_controller_t *ctrl, void *dma_buf, uint64_t dma_phys);
uint32_t nvme_log_boot_id(void);
uint16_t nvme_log_cursor(void);   /* entries stored (capped at MAX) — rolling-full once total_entries >= MAX */

#endif /* NVME_LOG_H */
