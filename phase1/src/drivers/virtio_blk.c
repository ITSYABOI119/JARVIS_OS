/**
 * JARVIS AI-OS Phase 1 - VirtIO Block Device Driver Implementation
 * Week 23: VirtIO Block Driver Implementation
 * Week 24: Driver Polish - Enhanced error handling, logging, validation
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#include "virtio_blk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

/* Debug logging (set to 1 to enable) */
#ifndef VIRTIO_DEBUG
#define VIRTIO_DEBUG 0
#endif

#define DEBUG_LOG(fmt, ...) \
    do { if (VIRTIO_DEBUG) fprintf(stderr, "[VirtIO-Blk DEBUG] " fmt "\n", ##__VA_ARGS__); } while (0)

#define ERROR_LOG(fmt, ...) \
    fprintf(stderr, "[VirtIO-Blk ERROR] " fmt "\n", ##__VA_ARGS__)

#define INFO_LOG(fmt, ...) \
    fprintf(stdout, "[VirtIO-Blk INFO] " fmt "\n", ##__VA_ARGS__)

/* Default configuration */
#define DEFAULT_QUEUE_SIZE      128
#define DEFAULT_MAX_REQUESTS    16
#define DEFAULT_RETRY_COUNT     3
#define DEFAULT_TIMEOUT_MS      5000

/* Helper: Wait for request completion with timeout */
static int wait_for_completion(virtio_blk_device_t* blk_dev, uint16_t head_desc, uint32_t timeout_ms) {
    uint64_t start_time = time(NULL) * 1000;  /* Convert to ms */
    uint64_t end_time = start_time + timeout_ms;

    while (time(NULL) * 1000 < end_time) {
        uint16_t used_head;
        uint32_t used_len;

        int result = virtio_poll_used(&blk_dev->vdev, &used_head, &used_len);
        if (result == 1 && used_head == head_desc) {
            return 0;  /* Success */
        }

        /* Polling delay (1ms) */
        usleep(1000);
    }

    fprintf(stderr, "[VirtIO-Block] Timeout waiting for completion\n");
    return -1;
}

int virtio_blk_init(virtio_blk_device_t* blk_dev, void* mmio_base) {
    /* Input validation */
    if (!blk_dev) {
        ERROR_LOG("NULL device pointer");
        return -EINVAL;
    }
    if (!mmio_base) {
        ERROR_LOG("NULL MMIO base address");
        return -EINVAL;
    }

    DEBUG_LOG("Initializing VirtIO block device at MMIO base %p", mmio_base);
    memset(blk_dev, 0, sizeof(virtio_blk_device_t));

    /* Initialize VirtIO device */
    if (virtio_device_init(&blk_dev->vdev, mmio_base) != 0) {
        ERROR_LOG("VirtIO device initialization failed");
        return -EIO;
    }
    DEBUG_LOG("VirtIO device initialized successfully");

    /* Check device type */
    if (blk_dev->vdev.device_id != 2) {
        ERROR_LOG("Wrong device type: %u (expected 2 for block device)",
                blk_dev->vdev.device_id);
        return -ENODEV;
    }
    DEBUG_LOG("Confirmed block device (device_id=2)");

    /* Negotiate features */
    uint64_t driver_features = VIRTIO_F_VERSION_1;  /* VirtIO 1.0+ */
    if (virtio_negotiate_features(&blk_dev->vdev, driver_features) != 0) {
        ERROR_LOG("Feature negotiation failed");
        return -EPROTO;
    }
    DEBUG_LOG("Features negotiated: 0x%016lx", blk_dev->vdev.driver_features);

    /* Setup virtqueue */
    if (virtio_setup_queue(&blk_dev->vdev, 0, DEFAULT_QUEUE_SIZE) != 0) {
        ERROR_LOG("Virtqueue setup failed (size=%u)", DEFAULT_QUEUE_SIZE);
        return -ENOMEM;
    }
    DEBUG_LOG("Virtqueue setup complete (size=%u)", DEFAULT_QUEUE_SIZE);

    /* Mark device as ready */
    if (virtio_device_ready(&blk_dev->vdev) != 0) {
        ERROR_LOG("Failed to mark device as ready");
        return -EIO;
    }
    DEBUG_LOG("Device marked as ready (DRIVER_OK set)");

    /* Read device configuration (capacity, block size, etc.) */
    /* For Phase 1 PoC, we'll use default values and skip config space reads */
    blk_dev->config.capacity = 1024 * 1024;  /* 512MB (1M sectors * 512 bytes) */
    blk_dev->config.blk_size = VIRTIO_BLK_SECTOR_SIZE;

    /* Allocate request buffers */
    blk_dev->max_requests = DEFAULT_MAX_REQUESTS;
    blk_dev->req_headers = (virtio_blk_req_header_t*)calloc(blk_dev->max_requests, sizeof(virtio_blk_req_header_t));
    blk_dev->req_status = (virtio_blk_req_status_t*)calloc(blk_dev->max_requests, sizeof(virtio_blk_req_status_t));
    blk_dev->data_buffers = (uint8_t*)aligned_alloc(512, blk_dev->max_requests * VIRTIO_BLK_SECTOR_SIZE);

    if (!blk_dev->req_headers || !blk_dev->req_status || !blk_dev->data_buffers) {
        fprintf(stderr, "[VirtIO-Block] Failed to allocate request buffers\n");
        virtio_blk_cleanup(blk_dev);
        return -1;
    }

    /* Set defaults */
    blk_dev->retry_count = DEFAULT_RETRY_COUNT;
    blk_dev->timeout_ms = DEFAULT_TIMEOUT_MS;
    blk_dev->active_requests = 0;

    printf("[VirtIO-Block] Initialized: capacity=%luMB\n",
           blk_dev->config.capacity * VIRTIO_BLK_SECTOR_SIZE / (1024 * 1024));

    return 0;
}

int virtio_blk_read(virtio_blk_device_t* blk_dev, uint64_t sector, uint32_t count, void* buffer) {
    /* Input validation */
    if (!blk_dev) {
        ERROR_LOG("read: NULL device pointer");
        return -EINVAL;
    }
    if (!buffer) {
        ERROR_LOG("read: NULL buffer pointer");
        return -EINVAL;
    }
    if (count == 0) {
        DEBUG_LOG("read: zero count, nothing to do");
        return 0;  /* Success, no bytes read */
    }
    if (count > 256) {  /* Reasonable limit for Phase 1 */
        ERROR_LOG("read: count too large (%u sectors, max 256)", count);
        return -EINVAL;
    }

    /* Validate sector bounds */
    if (sector >= blk_dev->config.capacity) {
        ERROR_LOG("read: sector %lu beyond capacity %lu", sector, blk_dev->config.capacity);
        return -EINVAL;
    }
    if (sector + count > blk_dev->config.capacity) {
        ERROR_LOG("read: sector range [%lu, %lu) exceeds capacity %lu",
                sector, sector + count, blk_dev->config.capacity);
        return -EINVAL;
    }

    DEBUG_LOG("read: sector=%lu, count=%u", sector, count);

    /* For Phase 1, we'll do single-sector reads (simplification) */
    uint32_t bytes_read = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t current_sector = sector + i;
        void* current_buffer = (uint8_t*)buffer + (i * VIRTIO_BLK_SECTOR_SIZE);

        int result = -1;
        for (uint32_t retry = 0; retry < blk_dev->retry_count; retry++) {
            /* Allocate descriptor chain (3 descriptors: header, data, status) */
            uint16_t desc_indices[3];
            if (virtio_alloc_desc_chain(blk_dev->vdev.vq, 3, desc_indices) != 0) {
                fprintf(stderr, "[VirtIO-Block] Failed to allocate descriptors\n");
                continue;
            }

            /* Setup request header */
            virtio_blk_req_header_t* header = &blk_dev->req_headers[0];
            header->type = VIRTIO_BLK_T_IN;  /* Read */
            header->reserved = 0;
            header->sector = current_sector;

            /* Setup status buffer */
            virtio_blk_req_status_t* status = &blk_dev->req_status[0];
            status->status = 0xFF;  /* Initialize to non-OK value */

            /* Setup descriptor 0: header (device reads) */
            blk_dev->vdev.vq->desc[desc_indices[0]].addr = (uint64_t)(uintptr_t)header;
            blk_dev->vdev.vq->desc[desc_indices[0]].len = sizeof(virtio_blk_req_header_t);
            blk_dev->vdev.vq->desc[desc_indices[0]].flags = VIRTQ_DESC_F_NEXT;
            blk_dev->vdev.vq->desc[desc_indices[0]].next = desc_indices[1];

            /* Setup descriptor 1: data (device writes) */
            blk_dev->vdev.vq->desc[desc_indices[1]].addr = (uint64_t)(uintptr_t)current_buffer;
            blk_dev->vdev.vq->desc[desc_indices[1]].len = VIRTIO_BLK_SECTOR_SIZE;
            blk_dev->vdev.vq->desc[desc_indices[1]].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
            blk_dev->vdev.vq->desc[desc_indices[1]].next = desc_indices[2];

            /* Setup descriptor 2: status (device writes) */
            blk_dev->vdev.vq->desc[desc_indices[2]].addr = (uint64_t)(uintptr_t)status;
            blk_dev->vdev.vq->desc[desc_indices[2]].len = sizeof(virtio_blk_req_status_t);
            blk_dev->vdev.vq->desc[desc_indices[2]].flags = VIRTQ_DESC_F_WRITE;
            blk_dev->vdev.vq->desc[desc_indices[2]].next = 0;

            /* Add to available ring */
            if (virtio_add_to_avail(&blk_dev->vdev, desc_indices[0]) != 0) {
                fprintf(stderr, "[VirtIO-Block] Failed to add to available ring\n");
                virtio_free_desc_chain(blk_dev->vdev.vq, desc_indices[0]);
                continue;
            }

            /* Wait for completion */
            if (wait_for_completion(blk_dev, desc_indices[0], blk_dev->timeout_ms) == 0) {
                /* Check status */
                if (status->status == VIRTIO_BLK_S_OK) {
                    bytes_read += VIRTIO_BLK_SECTOR_SIZE;
                    virtio_free_desc_chain(blk_dev->vdev.vq, desc_indices[0]);
                    result = 0;
                    break;  /* Success, stop retrying */
                } else {
                    fprintf(stderr, "[VirtIO-Block] Read failed: status=%u\n", status->status);
                    virtio_free_desc_chain(blk_dev->vdev.vq, desc_indices[0]);
                }
            } else {
                /* Timeout */
                virtio_free_desc_chain(blk_dev->vdev.vq, desc_indices[0]);
            }
        }

        if (result != 0) {
            fprintf(stderr, "[VirtIO-Block] Read failed after %u retries\n", blk_dev->retry_count);
            return -1;
        }
    }

    return bytes_read;
}

int virtio_blk_write(virtio_blk_device_t* blk_dev, uint64_t sector, uint32_t count, const void* buffer) {
    if (!blk_dev || !buffer || count == 0) {
        return -1;
    }

    /* Validate sector bounds */
    if (sector + count > blk_dev->config.capacity) {
        fprintf(stderr, "[VirtIO-Block] Write beyond capacity: sector=%lu, count=%u, capacity=%lu\n",
                sector, count, blk_dev->config.capacity);
        return -1;
    }

    /* For Phase 1, we'll do single-sector writes (simplification) */
    uint32_t bytes_written = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t current_sector = sector + i;
        const void* current_buffer = (const uint8_t*)buffer + (i * VIRTIO_BLK_SECTOR_SIZE);

        int result = -1;
        for (uint32_t retry = 0; retry < blk_dev->retry_count; retry++) {
            /* Allocate descriptor chain (3 descriptors: header, data, status) */
            uint16_t desc_indices[3];
            if (virtio_alloc_desc_chain(blk_dev->vdev.vq, 3, desc_indices) != 0) {
                fprintf(stderr, "[VirtIO-Block] Failed to allocate descriptors\n");
                continue;
            }

            /* Setup request header */
            virtio_blk_req_header_t* header = &blk_dev->req_headers[0];
            header->type = VIRTIO_BLK_T_OUT;  /* Write */
            header->reserved = 0;
            header->sector = current_sector;

            /* Copy data to aligned buffer (required for DMA) */
            memcpy(blk_dev->data_buffers, current_buffer, VIRTIO_BLK_SECTOR_SIZE);

            /* Setup status buffer */
            virtio_blk_req_status_t* status = &blk_dev->req_status[0];
            status->status = 0xFF;  /* Initialize to non-OK value */

            /* Setup descriptor 0: header (device reads) */
            blk_dev->vdev.vq->desc[desc_indices[0]].addr = (uint64_t)(uintptr_t)header;
            blk_dev->vdev.vq->desc[desc_indices[0]].len = sizeof(virtio_blk_req_header_t);
            blk_dev->vdev.vq->desc[desc_indices[0]].flags = VIRTQ_DESC_F_NEXT;
            blk_dev->vdev.vq->desc[desc_indices[0]].next = desc_indices[1];

            /* Setup descriptor 1: data (device reads) */
            blk_dev->vdev.vq->desc[desc_indices[1]].addr = (uint64_t)(uintptr_t)blk_dev->data_buffers;
            blk_dev->vdev.vq->desc[desc_indices[1]].len = VIRTIO_BLK_SECTOR_SIZE;
            blk_dev->vdev.vq->desc[desc_indices[1]].flags = VIRTQ_DESC_F_NEXT;
            blk_dev->vdev.vq->desc[desc_indices[1]].next = desc_indices[2];

            /* Setup descriptor 2: status (device writes) */
            blk_dev->vdev.vq->desc[desc_indices[2]].addr = (uint64_t)(uintptr_t)status;
            blk_dev->vdev.vq->desc[desc_indices[2]].len = sizeof(virtio_blk_req_status_t);
            blk_dev->vdev.vq->desc[desc_indices[2]].flags = VIRTQ_DESC_F_WRITE;
            blk_dev->vdev.vq->desc[desc_indices[2]].next = 0;

            /* Add to available ring */
            if (virtio_add_to_avail(&blk_dev->vdev, desc_indices[0]) != 0) {
                fprintf(stderr, "[VirtIO-Block] Failed to add to available ring\n");
                virtio_free_desc_chain(blk_dev->vdev.vq, desc_indices[0]);
                continue;
            }

            /* Wait for completion */
            if (wait_for_completion(blk_dev, desc_indices[0], blk_dev->timeout_ms) == 0) {
                /* Check status */
                if (status->status == VIRTIO_BLK_S_OK) {
                    bytes_written += VIRTIO_BLK_SECTOR_SIZE;
                    virtio_free_desc_chain(blk_dev->vdev.vq, desc_indices[0]);
                    result = 0;
                    break;  /* Success, stop retrying */
                } else {
                    fprintf(stderr, "[VirtIO-Block] Write failed: status=%u\n", status->status);
                    virtio_free_desc_chain(blk_dev->vdev.vq, desc_indices[0]);
                }
            } else {
                /* Timeout */
                virtio_free_desc_chain(blk_dev->vdev.vq, desc_indices[0]);
            }
        }

        if (result != 0) {
            fprintf(stderr, "[VirtIO-Block] Write failed after %u retries\n", blk_dev->retry_count);
            return -1;
        }
    }

    return bytes_written;
}

int virtio_blk_flush(virtio_blk_device_t* blk_dev) {
    if (!blk_dev) {
        return -1;
    }

    /* Flush is optional - for Phase 1, we'll skip it */
    printf("[VirtIO-Block] Flush (no-op for Phase 1)\n");
    return 0;
}

uint64_t virtio_blk_get_capacity(virtio_blk_device_t* blk_dev) {
    if (!blk_dev) {
        return 0;
    }

    return blk_dev->config.capacity;
}

void virtio_blk_cleanup(virtio_blk_device_t* blk_dev) {
    if (!blk_dev) {
        return;
    }

    if (blk_dev->req_headers) free(blk_dev->req_headers);
    if (blk_dev->req_status) free(blk_dev->req_status);
    if (blk_dev->data_buffers) free(blk_dev->data_buffers);

    virtio_device_cleanup(&blk_dev->vdev);

    printf("[VirtIO-Block] Cleanup complete\n");
}

/* Driver operations for driver registry integration */

int virtio_blk_driver_init(void* private_data) {
    /* Initialization happens in virtio_blk_init, called externally */
    return 0;
}

int virtio_blk_driver_start(void* private_data) {
    /* Device is ready after init */
    printf("[VirtIO-Block Driver] Started\n");
    return 0;
}

int virtio_blk_driver_stop(void* private_data) {
    /* No special stop logic for Phase 1 */
    printf("[VirtIO-Block Driver] Stopped\n");
    return 0;
}

int virtio_blk_driver_read(void* private_data, uint64_t lba, uint32_t count, void* buffer) {
    virtio_blk_device_t* blk_dev = (virtio_blk_device_t*)private_data;
    return virtio_blk_read(blk_dev, lba, count, buffer);
}

int virtio_blk_driver_write(void* private_data, uint64_t lba, uint32_t count, const void* buffer) {
    virtio_blk_device_t* blk_dev = (virtio_blk_device_t*)private_data;
    return virtio_blk_write(blk_dev, lba, count, buffer);
}

void virtio_blk_driver_cleanup(void* private_data) {
    virtio_blk_device_t* blk_dev = (virtio_blk_device_t*)private_data;
    virtio_blk_cleanup(blk_dev);
}

driver_ops_t* virtio_blk_get_driver_ops(void) {
    static driver_ops_t ops = {
        .init = virtio_blk_driver_init,
        .start = virtio_blk_driver_start,
        .stop = virtio_blk_driver_stop,
        .suspend = NULL,  /* Not implemented in Phase 1 */
        .resume = NULL,   /* Not implemented in Phase 1 */
        .read = virtio_blk_driver_read,
        .write = virtio_blk_driver_write,
        .cleanup = virtio_blk_driver_cleanup
    };

    return &ops;
}
