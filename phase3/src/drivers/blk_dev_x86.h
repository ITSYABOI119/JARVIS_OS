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

#endif
