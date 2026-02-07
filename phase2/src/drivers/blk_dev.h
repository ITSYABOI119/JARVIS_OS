/*
 * JARVIS AI-OS - Block Device Interface
 *
 * Minimal generic block-device abstraction for filesystem layers.
 * Each block device provides 512-byte sector read/write operations.
 */

#ifndef BLK_DEV_H
#define BLK_DEV_H

#include <stdint.h>
#include <stdbool.h>

/* Error codes */
#define BLK_OK           0
#define BLK_ERR_NODEV   -1  /* Device not found or not initialized */
#define BLK_ERR_IO      -2  /* I/O error during transfer */
#define BLK_ERR_PARAM   -3  /* Invalid parameter (NULL buffer, etc.) */
#define BLK_ERR_RANGE   -4  /* LBA out of range */

/* Block device descriptor */
typedef struct blk_dev {
    const char *name;           /* Device name (e.g., "emmc0") */
    uint32_t sector_size;       /* Bytes per sector (always 512) */
    uint64_t sector_count;      /* Total sectors */
    void *priv;                 /* Driver-private data */

    /* Operations */
    int (*read)(struct blk_dev *dev, uint64_t lba, uint32_t count, void *buf);
    int (*write)(struct blk_dev *dev, uint64_t lba, uint32_t count, const void *buf);
} blk_dev_t;

/* Get the system's primary block device (SD/EMMC).
 * Returns NULL if no device is available. */
blk_dev_t *blk_get_primary(void);

/* Convenience wrappers */
static inline int blk_read_sector(blk_dev_t *dev, uint64_t lba, void *buf)
{
    if (!dev || !dev->read) return BLK_ERR_NODEV;
    return dev->read(dev, lba, 1, buf);
}

static inline int blk_write_sector(blk_dev_t *dev, uint64_t lba, const void *buf)
{
    if (!dev || !dev->write) return BLK_ERR_NODEV;
    return dev->write(dev, lba, 1, buf);
}

static inline int blk_read_sectors(blk_dev_t *dev, uint64_t lba, uint32_t count, void *buf)
{
    if (!dev || !dev->read) return BLK_ERR_NODEV;
    return dev->read(dev, lba, count, buf);
}

static inline int blk_write_sectors(blk_dev_t *dev, uint64_t lba, uint32_t count, const void *buf)
{
    if (!dev || !dev->write) return BLK_ERR_NODEV;
    return dev->write(dev, lba, count, buf);
}

#endif
