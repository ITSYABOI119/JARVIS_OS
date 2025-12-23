/**
 * JARVIS AI-OS Phase 1 - VirtIO Block Device Driver
 * Week 23: VirtIO Block Driver Implementation
 *
 * This module implements a VirtIO block device driver for disk I/O.
 * Uses virtio_core for VirtIO protocol operations.
 *
 * Features:
 * - Block read/write operations (512-byte sectors)
 * - Polling-based (no interrupts yet)
 * - Error handling with retry logic
 * - Integration with driver registry
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#ifndef VIRTIO_BLK_H
#define VIRTIO_BLK_H

#include "virtio_core.h"
#include "driver_registry.h"
#include <stdint.h>
#include <stdbool.h>

/* VirtIO block device feature bits */
#define VIRTIO_BLK_F_SIZE_MAX       (1ULL << 1)   /* Max segment size */
#define VIRTIO_BLK_F_SEG_MAX        (1ULL << 2)   /* Max segments */
#define VIRTIO_BLK_F_GEOMETRY       (1ULL << 4)   /* Disk geometry */
#define VIRTIO_BLK_F_RO             (1ULL << 5)   /* Read-only */
#define VIRTIO_BLK_F_BLK_SIZE       (1ULL << 6)   /* Block size */
#define VIRTIO_BLK_F_FLUSH          (1ULL << 9)   /* Flush command */
#define VIRTIO_BLK_F_TOPOLOGY       (1ULL << 10)  /* Topology info */
#define VIRTIO_BLK_F_CONFIG_WCE     (1ULL << 11)  /* Write cache enable */

/* VirtIO block request types */
#define VIRTIO_BLK_T_IN             0  /* Read */
#define VIRTIO_BLK_T_OUT            1  /* Write */
#define VIRTIO_BLK_T_FLUSH          4  /* Flush */

/* VirtIO block request status */
#define VIRTIO_BLK_S_OK             0  /* Success */
#define VIRTIO_BLK_S_IOERR          1  /* I/O error */
#define VIRTIO_BLK_S_UNSUPP         2  /* Unsupported operation */

/* Block size */
#define VIRTIO_BLK_SECTOR_SIZE      512

/**
 * VirtIO block request header.
 * Descriptor 0 in chain (read-only for device).
 */
typedef struct {
    uint32_t type;      /* Request type (IN, OUT, FLUSH) */
    uint32_t reserved;  /* Reserved (must be 0) */
    uint64_t sector;    /* Sector number (512-byte sectors) */
} __attribute__((packed)) virtio_blk_req_header_t;

/**
 * VirtIO block request status.
 * Last descriptor in chain (write-only for device).
 */
typedef struct {
    uint8_t status;     /* Status (OK, IOERR, UNSUPP) */
} __attribute__((packed)) virtio_blk_req_status_t;

/**
 * VirtIO block device configuration (MMIO config space).
 * Read from device config registers.
 */
typedef struct {
    uint64_t capacity;      /* Device capacity in 512-byte sectors */
    uint32_t size_max;      /* Max size of any single segment */
    uint32_t seg_max;       /* Max number of segments */
    uint16_t cylinders;     /* Geometry: cylinders */
    uint8_t heads;          /* Geometry: heads */
    uint8_t sectors;        /* Geometry: sectors */
    uint32_t blk_size;      /* Block size (logical block size) */
    uint8_t physical_block_exp;  /* Physical block exponent */
    uint8_t alignment_offset;    /* Alignment offset */
    uint16_t min_io_size;        /* Min I/O size */
    uint32_t opt_io_size;        /* Optimal I/O size */
    uint8_t writeback;           /* Write cache mode */
    uint8_t unused;
    uint16_t num_queues;         /* Number of queues */
    uint32_t max_discard_sectors;
    uint32_t max_discard_seg;
    uint32_t discard_sector_alignment;
    uint32_t max_write_zeroes_sectors;
    uint32_t max_write_zeroes_seg;
    uint8_t write_zeroes_may_unmap;
    uint8_t unused1[3];
} __attribute__((packed)) virtio_blk_config_t;

/**
 * VirtIO block device driver private data.
 */
typedef struct {
    virtio_device_t vdev;       /* VirtIO device */
    virtio_blk_config_t config; /* Device configuration */

    /* Request tracking */
    virtio_blk_req_header_t* req_headers;  /* Request headers pool */
    virtio_blk_req_status_t* req_status;   /* Request status pool */
    uint8_t* data_buffers;                  /* Data buffers pool */

    uint32_t max_requests;      /* Max in-flight requests */
    uint32_t active_requests;   /* Current active requests */

    /* Error handling */
    uint32_t retry_count;       /* Number of retries per operation */
    uint32_t timeout_ms;        /* Timeout in milliseconds */
} virtio_blk_device_t;

/**
 * Initialize VirtIO block device driver.
 * Probes device, negotiates features, sets up queue.
 *
 * Args:
 *   blk_dev: VirtIO block device structure
 *   mmio_base: MMIO base address (e.g., 0x10001000 for QEMU)
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int virtio_blk_init(virtio_blk_device_t* blk_dev, void* mmio_base);

/**
 * Read blocks from device.
 *
 * Args:
 *   blk_dev: VirtIO block device
 *   sector: Starting sector (512-byte sectors)
 *   count: Number of sectors to read
 *   buffer: Output buffer (must be at least count * 512 bytes)
 *
 * Returns:
 *   Number of bytes read on success, -1 on failure
 */
int virtio_blk_read(virtio_blk_device_t* blk_dev, uint64_t sector, uint32_t count, void* buffer);

/**
 * Write blocks to device.
 *
 * Args:
 *   blk_dev: VirtIO block device
 *   sector: Starting sector (512-byte sectors)
 *   count: Number of sectors to write
 *   buffer: Input buffer (must be at least count * 512 bytes)
 *
 * Returns:
 *   Number of bytes written on success, -1 on failure
 */
int virtio_blk_write(virtio_blk_device_t* blk_dev, uint64_t sector, uint32_t count, const void* buffer);

/**
 * Flush write cache.
 *
 * Args:
 *   blk_dev: VirtIO block device
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int virtio_blk_flush(virtio_blk_device_t* blk_dev);

/**
 * Get device capacity.
 *
 * Args:
 *   blk_dev: VirtIO block device
 *
 * Returns:
 *   Capacity in 512-byte sectors
 */
uint64_t virtio_blk_get_capacity(virtio_blk_device_t* blk_dev);

/**
 * Cleanup and shutdown VirtIO block device.
 *
 * Args:
 *   blk_dev: VirtIO block device
 */
void virtio_blk_cleanup(virtio_blk_device_t* blk_dev);

/**
 * Driver operations for driver registry integration.
 * These functions wrap virtio_blk_* functions for driver_ops_t interface.
 */
int virtio_blk_driver_init(void* private_data);
int virtio_blk_driver_start(void* private_data);
int virtio_blk_driver_stop(void* private_data);
int virtio_blk_driver_read(void* private_data, uint64_t lba, uint32_t count, void* buffer);
int virtio_blk_driver_write(void* private_data, uint64_t lba, uint32_t count, const void* buffer);
void virtio_blk_driver_cleanup(void* private_data);

/**
 * Create driver_ops_t structure for VirtIO block driver.
 * Returns a static structure that can be passed to driver_register().
 */
driver_ops_t* virtio_blk_get_driver_ops(void);

#endif /* VIRTIO_BLK_H */
