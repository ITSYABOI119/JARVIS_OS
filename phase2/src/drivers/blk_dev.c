/*
 * JARVIS AI-OS - Block Device Interface Implementation
 *
 * Wraps the EMMC driver as the primary block device.
 */

#include "blk_dev.h"
#include "emmc_sdhci.h"
#include <stddef.h>  /* NULL */

/* EMMC block device operations */
static int emmc_blk_read(blk_dev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    (void)dev;  /* Single device, no need for priv data */

    if (!buf) {
        return BLK_ERR_PARAM;
    }

    if (!emmc_is_mapped()) {
        return BLK_ERR_NODEV;
    }

    if (count == 1) {
        if (!emmc_read_block((uint32_t)lba, (uint8_t *)buf)) {
            return BLK_ERR_IO;
        }
    } else {
        if (!emmc_read_blocks((uint32_t)lba, count, (uint8_t *)buf)) {
            return BLK_ERR_IO;
        }
    }

    return BLK_OK;
}

static int emmc_blk_write(blk_dev_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    (void)dev;

    if (!buf) {
        return BLK_ERR_PARAM;
    }

    if (!emmc_is_mapped()) {
        return BLK_ERR_NODEV;
    }

    if (count == 1) {
        if (!emmc_write_block((uint32_t)lba, (const uint8_t *)buf)) {
            return BLK_ERR_IO;
        }
    } else {
        if (!emmc_write_blocks((uint32_t)lba, count, (const uint8_t *)buf)) {
            return BLK_ERR_IO;
        }
    }

    return BLK_OK;
}

/* Primary EMMC block device descriptor */
static blk_dev_t emmc_blk_dev = {
    .name = "emmc0",
    .sector_size = 512,
    .sector_count = 0,  /* Set during init */
    .priv = NULL,
    .read = emmc_blk_read,
    .write = emmc_blk_write
};

blk_dev_t *blk_get_primary(void)
{
    if (!emmc_is_mapped()) {
        return NULL;
    }

    /* Update sector count from EMMC capacity */
    uint64_t cap = emmc_get_capacity_bytes();
    emmc_blk_dev.sector_count = cap / 512;

    return &emmc_blk_dev;
}
