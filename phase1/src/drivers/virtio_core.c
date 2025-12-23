/**
 * JARVIS AI-OS Phase 1 - VirtIO Core Implementation
 * Week 23: VirtIO Block Driver Implementation
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#include "virtio_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Helper: Calculate virtqueue memory requirements */
static size_t virtq_size(uint16_t queue_size) {
    size_t desc_size = sizeof(virtq_desc_t) * queue_size;
    size_t avail_size = sizeof(uint16_t) * (3 + queue_size);  /* flags, idx, ring[], used_event */
    size_t used_size = sizeof(uint16_t) * 3 + sizeof(virtq_used_elem_t) * queue_size;  /* flags, idx, ring[], avail_event */

    /* Align to page boundaries (4KB) */
    size_t total = desc_size + avail_size + used_size;
    return (total + 4095) & ~4095UL;
}

int virtio_device_init(virtio_device_t* dev, void* base_addr) {
    if (!dev || !base_addr) {
        fprintf(stderr, "[VirtIO] Invalid arguments\n");
        return -1;
    }

    memset(dev, 0, sizeof(virtio_device_t));
    dev->base_addr = base_addr;

    /* Verify magic value */
    uint32_t magic = virtio_mmio_read32(dev, VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != 0x74726976) {  /* "virt" */
        fprintf(stderr, "[VirtIO] Invalid magic value: 0x%08x (expected 0x74726976)\n", magic);
        return -1;
    }

    /* Check version */
    uint32_t version = virtio_mmio_read32(dev, VIRTIO_MMIO_VERSION);
    if (version != 0x2) {
        fprintf(stderr, "[VirtIO] Unsupported version: %u (expected 2)\n", version);
        return -1;
    }

    /* Read device info */
    dev->device_id = virtio_mmio_read32(dev, VIRTIO_MMIO_DEVICE_ID);
    dev->vendor_id = virtio_mmio_read32(dev, VIRTIO_MMIO_VENDOR_ID);

    if (dev->device_id == 0) {
        fprintf(stderr, "[VirtIO] No device present\n");
        return -1;
    }

    printf("[VirtIO] Device found: ID=%u, Vendor=%u\n", dev->device_id, dev->vendor_id);

    /* Reset device */
    virtio_device_reset(dev);

    /* Acknowledge device */
    dev->status = VIRTIO_STATUS_ACKNOWLEDGE;
    virtio_mmio_write32(dev, VIRTIO_MMIO_STATUS, dev->status);

    /* Set driver status */
    dev->status |= VIRTIO_STATUS_DRIVER;
    virtio_mmio_write32(dev, VIRTIO_MMIO_STATUS, dev->status);

    /* Read device features (64-bit across two 32-bit registers) */
    virtio_mmio_write32(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t features_low = virtio_mmio_read32(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    virtio_mmio_write32(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    uint32_t features_high = virtio_mmio_read32(dev, VIRTIO_MMIO_DEVICE_FEATURES);
    dev->device_features = ((uint64_t)features_high << 32) | features_low;

    printf("[VirtIO] Device features: 0x%016lx\n", dev->device_features);

    return 0;
}

int virtio_device_reset(virtio_device_t* dev) {
    if (!dev) {
        return -1;
    }

    /* Write 0 to status register */
    virtio_mmio_write32(dev, VIRTIO_MMIO_STATUS, 0);
    dev->status = 0;

    return 0;
}

int virtio_negotiate_features(virtio_device_t* dev, uint64_t driver_features) {
    if (!dev) {
        return -1;
    }

    /* Negotiate: driver_features AND device_features */
    dev->driver_features = driver_features & dev->device_features;

    /* Write driver features (64-bit across two 32-bit registers) */
    virtio_mmio_write32(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    virtio_mmio_write32(dev, VIRTIO_MMIO_DRIVER_FEATURES, (uint32_t)(dev->driver_features & 0xFFFFFFFF));
    virtio_mmio_write32(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    virtio_mmio_write32(dev, VIRTIO_MMIO_DRIVER_FEATURES, (uint32_t)(dev->driver_features >> 32));

    /* Mark features OK */
    dev->status |= VIRTIO_STATUS_FEATURES_OK;
    virtio_mmio_write32(dev, VIRTIO_MMIO_STATUS, dev->status);

    /* Verify device accepted features */
    uint32_t status = virtio_mmio_read32(dev, VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        fprintf(stderr, "[VirtIO] Device rejected features\n");
        return -1;
    }

    printf("[VirtIO] Features negotiated: 0x%016lx\n", dev->driver_features);
    return 0;
}

int virtio_device_ready(virtio_device_t* dev) {
    if (!dev) {
        return -1;
    }

    /* Set DRIVER_OK */
    dev->status |= VIRTIO_STATUS_DRIVER_OK;
    virtio_mmio_write32(dev, VIRTIO_MMIO_STATUS, dev->status);

    printf("[VirtIO] Device ready\n");
    return 0;
}

int virtio_setup_queue(virtio_device_t* dev, uint16_t queue_index, uint16_t queue_size) {
    if (!dev || queue_size == 0 || queue_size > VIRTIO_MAX_QUEUE_SIZE) {
        fprintf(stderr, "[VirtIO] Invalid queue parameters\n");
        return -1;
    }

    /* Check if queue_size is power of 2 */
    if ((queue_size & (queue_size - 1)) != 0) {
        fprintf(stderr, "[VirtIO] Queue size must be power of 2\n");
        return -1;
    }

    /* Select queue */
    virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_SEL, queue_index);

    /* Check max queue size */
    uint32_t max_size = virtio_mmio_read32(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_size > max_size) {
        fprintf(stderr, "[VirtIO] Requested queue size %u exceeds max %u\n", queue_size, max_size);
        return -1;
    }

    /* Allocate virtqueue structure */
    virtqueue_t* vq = (virtqueue_t*)calloc(1, sizeof(virtqueue_t));
    if (!vq) {
        fprintf(stderr, "[VirtIO] Failed to allocate virtqueue\n");
        return -1;
    }

    vq->queue_size = queue_size;
    vq->last_used_idx = 0;
    vq->last_avail_idx = 0;
    vq->free_head = 0;
    vq->num_free = queue_size;

    /* Allocate descriptor table */
    size_t desc_size = sizeof(virtq_desc_t) * queue_size;
    vq->desc = (virtq_desc_t*)aligned_alloc(16, desc_size);
    if (!vq->desc) {
        fprintf(stderr, "[VirtIO] Failed to allocate descriptor table\n");
        free(vq);
        return -1;
    }
    memset(vq->desc, 0, desc_size);

    /* Initialize descriptor free list */
    for (uint16_t i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0;

    /* Allocate available ring */
    size_t avail_size = sizeof(uint16_t) * (3 + queue_size);
    vq->avail = (virtq_avail_t*)aligned_alloc(2, avail_size);
    if (!vq->avail) {
        fprintf(stderr, "[VirtIO] Failed to allocate available ring\n");
        free(vq->desc);
        free(vq);
        return -1;
    }
    memset(vq->avail, 0, avail_size);

    /* Allocate used ring */
    size_t used_size = sizeof(uint16_t) * 3 + sizeof(virtq_used_elem_t) * queue_size;
    vq->used = (virtq_used_t*)aligned_alloc(4, used_size);
    if (!vq->used) {
        fprintf(stderr, "[VirtIO] Failed to allocate used ring\n");
        free(vq->avail);
        free(vq->desc);
        free(vq);
        return -1;
    }
    memset(vq->used, 0, used_size);

    /* Write queue size */
    virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_NUM, queue_size);

    /* Write queue addresses (physical addresses, for now use virtual) */
    uint64_t desc_addr = (uint64_t)(uintptr_t)vq->desc;
    uint64_t avail_addr = (uint64_t)(uintptr_t)vq->avail;
    uint64_t used_addr = (uint64_t)(uintptr_t)vq->used;

    virtio_mmio_write64(dev, VIRTIO_MMIO_QUEUE_DESC_LOW, VIRTIO_MMIO_QUEUE_DESC_HIGH, desc_addr);
    virtio_mmio_write64(dev, VIRTIO_MMIO_QUEUE_AVAIL_LOW, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, avail_addr);
    virtio_mmio_write64(dev, VIRTIO_MMIO_QUEUE_USED_LOW, VIRTIO_MMIO_QUEUE_USED_HIGH, used_addr);

    /* Mark queue as ready */
    virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_READY, 1);

    dev->vq = vq;
    printf("[VirtIO] Queue %u initialized (size=%u)\n", queue_index, queue_size);

    return 0;
}

int virtio_alloc_desc_chain(virtqueue_t* vq, uint16_t count, uint16_t* indices) {
    if (!vq || !indices || count == 0) {
        return -1;
    }

    if (vq->num_free < count) {
        fprintf(stderr, "[VirtIO] Not enough free descriptors (%u needed, %u available)\n",
                count, vq->num_free);
        return -1;
    }

    uint16_t current = vq->free_head;
    for (uint16_t i = 0; i < count; i++) {
        indices[i] = current;
        vq->desc_in_use[current] = true;
        current = vq->desc[current].next;
        vq->num_free--;
    }

    vq->free_head = current;
    return 0;
}

void virtio_free_desc_chain(virtqueue_t* vq, uint16_t head) {
    if (!vq) {
        return;
    }

    uint16_t current = head;
    while (vq->desc_in_use[current]) {
        uint16_t next = vq->desc[current].next;
        vq->desc_in_use[current] = false;

        /* Add to free list */
        vq->desc[current].next = vq->free_head;
        vq->free_head = current;
        vq->num_free++;

        /* Stop if no NEXT flag */
        if (!(vq->desc[current].flags & VIRTQ_DESC_F_NEXT)) {
            break;
        }

        current = next;
    }
}

int virtio_add_to_avail(virtio_device_t* dev, uint16_t head) {
    if (!dev || !dev->vq) {
        return -1;
    }

    virtqueue_t* vq = dev->vq;

    /* Add to available ring */
    uint16_t avail_idx = vq->avail->idx % vq->queue_size;
    vq->avail->ring[avail_idx] = head;

    /* Memory barrier (ensure ring update visible before idx update) */
    __sync_synchronize();

    /* Increment available index */
    vq->avail->idx++;
    vq->last_avail_idx = vq->avail->idx;

    /* Memory barrier */
    __sync_synchronize();

    /* Kick device (notify) */
    virtio_mmio_write32(dev, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    return 0;
}

int virtio_poll_used(virtio_device_t* dev, uint16_t* head, uint32_t* len) {
    if (!dev || !dev->vq || !head || !len) {
        return -1;
    }

    virtqueue_t* vq = dev->vq;

    /* Check if new used descriptors available */
    __sync_synchronize();  /* Memory barrier */

    if (vq->last_used_idx == vq->used->idx) {
        /* No new used descriptors */
        return 0;
    }

    /* Get used element */
    uint16_t used_idx = vq->last_used_idx % vq->queue_size;
    virtq_used_elem_t* elem = &vq->used->ring[used_idx];

    *head = (uint16_t)elem->id;
    *len = elem->len;

    /* Increment last_used_idx */
    vq->last_used_idx++;

    return 1;
}

void virtio_device_cleanup(virtio_device_t* dev) {
    if (!dev) {
        return;
    }

    if (dev->vq) {
        if (dev->vq->desc) free(dev->vq->desc);
        if (dev->vq->avail) free(dev->vq->avail);
        if (dev->vq->used) free(dev->vq->used);
        free(dev->vq);
        dev->vq = NULL;
    }

    /* Reset device */
    virtio_device_reset(dev);

    printf("[VirtIO] Device cleanup complete\n");
}
