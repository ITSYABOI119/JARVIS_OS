/*
 * JARVIS AI-OS Phase 3 — Block Device Abstraction (x86)
 * Thin wrapper over AHCI providing unified block I/O interface.
 * Same API as Phase 2 blk_dev.h for cross-platform compatibility.
 */

#ifndef BLK_DEV_X86_H
#define BLK_DEV_X86_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint64_t total_sectors;
    uint32_t sector_size;
    char     model[41];
    char     serial[21];
    bool     initialized;
} blk_dev_info_t;

int  blk_dev_init(void);
int  blk_dev_read(uint64_t lba, uint32_t count, void *buf);
int  blk_dev_write(uint64_t lba, uint32_t count, const void *buf);
int  blk_dev_get_info(blk_dev_info_t *info);
bool blk_dev_is_ready(void);

/* Initialize block device with pre-mapped NVMe BAR0 and DMA buffers.
 * For seL4 bare metal where the rootserver maps device frames. */
int blk_dev_init_nvme(volatile uint8_t *bar0_mapped, uint64_t bar0_phys,
                       void *admin_sq, uint64_t admin_sq_phys,
                       void *admin_cq, uint64_t admin_cq_phys,
                       void *io_sq, uint64_t io_sq_phys,
                       void *io_cq, uint64_t io_cq_phys,
                       void *identify_buf, uint64_t identify_phys);

#endif
