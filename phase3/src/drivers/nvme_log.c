/*
 * JARVIS AI-OS - NVMe Write Log
 *
 * Persistent event log on NVMe at a fixed LBA offset past all partitions.
 * Sector 0 (NVME_LOG_BASE_LBA) holds the header; entries follow at +1..+N.
 * Header checksum is XOR of the first 15 uint32_t words (same pattern as
 * warm_reboot.c).
 *
 * Header is flushed to disk after every write to guarantee the cursor
 * is durable. Previously flushed every 16 entries, but that caused
 * cursor=0 on disk when boot stalled before reaching 16 entries.
 */

#include "nvme_log.h"
#include <string.h>

/* ================================================================
 * Module State
 * ================================================================ */

static nvme_log_header_t log_hdr;
static int log_initialized = 0;

/* ================================================================
 * Checksum: XOR of first 15 uint32_t words (60 bytes)
 * The checksum field itself is at word[15] and is NOT included.
 * Uses memcpy to avoid packed-struct alignment warnings (GCC 13).
 * ================================================================ */

static uint32_t compute_checksum(const nvme_log_header_t *h)
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
 * nvme_log_init
 *
 * Read the log header sector from NVMe. If valid (magic + version +
 * checksum match), increment boot_id and continue from cursor.
 * Otherwise create a fresh header with boot_id = 1.
 * ================================================================ */

int nvme_log_init(nvme_controller_t *ctrl, void *dma_buf, uint64_t dma_phys)
{
    if (!ctrl || !ctrl->initialized)
        return -1;

    /* Read header sector */
    int rc = nvme_read_sectors(ctrl, NVME_LOG_BASE_LBA, 1, dma_buf, dma_phys);
    if (rc == 0) {
        memcpy(&log_hdr, dma_buf, sizeof(log_hdr));
        if (log_hdr.magic == NVME_LOG_MAGIC &&
            log_hdr.version == NVME_LOG_VERSION &&
            log_hdr.checksum == compute_checksum(&log_hdr)) {
            /* Valid header -- increment boot_id, continue from cursor */
            log_hdr.boot_id++;
            log_initialized = 1;
            return nvme_log_flush(ctrl, dma_buf, dma_phys);
        }
    }

    /* Invalid or no header -- create fresh */
    memset(&log_hdr, 0, sizeof(log_hdr));
    log_hdr.magic   = NVME_LOG_MAGIC;
    log_hdr.version = NVME_LOG_VERSION;
    log_hdr.cursor  = 0;
    log_hdr.total_entries = 0;
    log_hdr.boot_id = 1;
    log_initialized = 1;
    return nvme_log_flush(ctrl, dma_buf, dma_phys);
}

/* ================================================================
 * nvme_log_write
 *
 * Append one log entry at the current cursor position.
 * Returns 0 on success, -1 if not initialized, -2 if log full.
 * ================================================================ */

int nvme_log_write(nvme_controller_t *ctrl, void *dma_buf, uint64_t dma_phys,
                   uint16_t type, const char *msg)
{
    if (!log_initialized || !ctrl || !ctrl->initialized)
        return -1;
    if (log_hdr.cursor >= NVME_LOG_MAX_ENTRIES)
        return -2;  /* log full */

    /* Build entry in DMA buffer */
    nvme_log_entry_t *entry = (nvme_log_entry_t *)dma_buf;
    memset(entry, 0, sizeof(*entry));
    entry->boot_id = log_hdr.boot_id;
    entry->seq     = log_hdr.total_entries;
    entry->type    = type;
    if (msg) {
        int len = (int)strlen(msg);
        if (len > 496) len = 496;
        entry->length = (uint16_t)len;
        memcpy(entry->payload, msg, (size_t)len);
    }

    /* Write entry sector (cursor + 1 because sector 0 is header) */
    uint64_t entry_lba = NVME_LOG_BASE_LBA + 1 + log_hdr.cursor;
    int rc = nvme_write_sectors(ctrl, entry_lba, 1, dma_buf, dma_phys);
    if (rc != 0)
        return rc;

    log_hdr.cursor++;
    log_hdr.total_entries++;

    /* Always flush header so cursor is durable on disk.
     * Without this, a stall/reboot before 16 entries leaves cursor=0
     * on disk even though entry sectors were written successfully. */
    return nvme_log_flush(ctrl, dma_buf, dma_phys);
}

/* ================================================================
 * nvme_log_flush
 *
 * Write the current header to disk. Call explicitly before reboot
 * or on critical errors to guarantee cursor/boot_id are persisted.
 * ================================================================ */

int nvme_log_flush(nvme_controller_t *ctrl, void *dma_buf, uint64_t dma_phys)
{
    if (!log_initialized)
        return -1;
    log_hdr.checksum = compute_checksum(&log_hdr);
    memset(dma_buf, 0, 512);
    memcpy(dma_buf, &log_hdr, sizeof(log_hdr));
    return nvme_write_sectors(ctrl, NVME_LOG_BASE_LBA, 1, dma_buf, dma_phys);
}

/* ================================================================
 * nvme_log_boot_id
 *
 * Returns current boot_id (0 if not initialized).
 * ================================================================ */

uint32_t nvme_log_boot_id(void)
{
    return log_initialized ? log_hdr.boot_id : 0;
}
