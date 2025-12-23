/**
 * JARVIS AI-OS Phase 1 - VirtIO Core
 * Week 23: VirtIO Block Driver Implementation
 *
 * This module implements the VirtIO protocol layer (MMIO transport).
 * Provides virtqueue management and device communication primitives.
 * Reusable for future VirtIO drivers (network, console, etc.).
 *
 * VirtIO Specification: https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
 * Transport: MMIO (Memory-Mapped I/O) - simplest, QEMU default
 *
 * Architecture:
 * - Single virtqueue per device (simplified for Phase 1)
 * - Polling-based operation (no interrupts yet)
 * - Split virtqueue layout (descriptor table + available ring + used ring)
 * - 3-descriptor chains per request (header, data, status)
 *
 * Author: JARVIS AI-OS Team
 * Date: December 3, 2025
 */

#ifndef VIRTIO_CORE_H
#define VIRTIO_CORE_H

#include <stdint.h>
#include <stdbool.h>

/* VirtIO MMIO register offsets (from base address) */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000  /* 0x74726976 ("virt") */
#define VIRTIO_MMIO_VERSION             0x004  /* Version (0x2 for VirtIO 1.0+) */
#define VIRTIO_MMIO_DEVICE_ID           0x008  /* Device ID (2 = block device) */
#define VIRTIO_MMIO_VENDOR_ID           0x00c  /* Vendor ID */
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010  /* Device features (read) */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014  /* Device features select */
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020  /* Driver features (write) */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024  /* Driver features select */
#define VIRTIO_MMIO_QUEUE_SEL           0x030  /* Queue select (0, 1, 2...) */
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034  /* Max queue size */
#define VIRTIO_MMIO_QUEUE_NUM           0x038  /* Queue size (write) */
#define VIRTIO_MMIO_QUEUE_READY         0x044  /* Queue ready flag */
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050  /* Queue notify (kick) */
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060  /* Interrupt status */
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064  /* Interrupt acknowledge */
#define VIRTIO_MMIO_STATUS              0x070  /* Device status */
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080  /* Queue descriptor table (low 32 bits) */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084  /* Queue descriptor table (high 32 bits) */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090  /* Queue available ring (low 32 bits) */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094  /* Queue available ring (high 32 bits) */
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0a0  /* Queue used ring (low 32 bits) */
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0a4  /* Queue used ring (high 32 bits) */
#define VIRTIO_MMIO_CONFIG_GENERATION   0x0fc  /* Configuration atomicity */

/* VirtIO device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE       0x01   /* Guest acknowledges device */
#define VIRTIO_STATUS_DRIVER            0x02   /* Guest has driver */
#define VIRTIO_STATUS_DRIVER_OK         0x04   /* Driver is ready */
#define VIRTIO_STATUS_FEATURES_OK       0x08   /* Feature negotiation done */
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40  /* Device needs reset */
#define VIRTIO_STATUS_FAILED            0x80   /* Fatal error occurred */

/* VirtIO descriptor flags */
#define VIRTQ_DESC_F_NEXT               0x01   /* Descriptor continues (has next) */
#define VIRTQ_DESC_F_WRITE              0x02   /* Buffer is write-only (device writes) */
#define VIRTQ_DESC_F_INDIRECT           0x04   /* Buffer is indirect descriptor table */

/* VirtIO available ring flags */
#define VIRTQ_AVAIL_F_NO_INTERRUPT      0x01   /* Don't send interrupt */

/* VirtIO used ring flags */
#define VIRTQ_USED_F_NO_NOTIFY          0x01   /* Don't notify driver */

/* VirtIO feature bits (generic) */
#define VIRTIO_F_RING_INDIRECT_DESC     (1ULL << 28)  /* Indirect descriptors */
#define VIRTIO_F_RING_EVENT_IDX         (1ULL << 29)  /* Event index */
#define VIRTIO_F_VERSION_1              (1ULL << 32)  /* VirtIO 1.0+ */

/* Maximum virtqueue size (power of 2) */
#define VIRTIO_MAX_QUEUE_SIZE           256

/**
 * VirtIO descriptor structure.
 * Describes a single memory buffer (physical address, length, flags).
 */
typedef struct {
    uint64_t addr;      /* Physical address */
    uint32_t len;       /* Length in bytes */
    uint16_t flags;     /* Descriptor flags (NEXT, WRITE, INDIRECT) */
    uint16_t next;      /* Next descriptor index (if NEXT flag set) */
} __attribute__((packed)) virtq_desc_t;

/**
 * VirtIO available ring structure.
 * Driver writes here to notify device of available descriptors.
 */
typedef struct {
    uint16_t flags;     /* Flags (NO_INTERRUPT) */
    uint16_t idx;       /* Next available descriptor index */
    uint16_t ring[];    /* Array of descriptor indices (size = queue_size) */
    /* Note: used_event follows ring[] at runtime */
} __attribute__((packed)) virtq_avail_t;

/**
 * VirtIO used ring element.
 * Device writes this when descriptor chain is used.
 */
typedef struct {
    uint32_t id;        /* Descriptor chain head index */
    uint32_t len;       /* Total bytes written */
} __attribute__((packed)) virtq_used_elem_t;

/**
 * VirtIO used ring structure.
 * Device writes here when descriptors are used.
 */
typedef struct {
    uint16_t flags;     /* Flags (NO_NOTIFY) */
    uint16_t idx;       /* Next used descriptor index */
    virtq_used_elem_t ring[];  /* Array of used elements (size = queue_size) */
    /* Note: avail_event follows ring[] at runtime */
} __attribute__((packed)) virtq_used_t;

/**
 * VirtIO virtqueue structure.
 * Complete virtqueue with all components.
 */
typedef struct {
    uint16_t queue_size;        /* Queue size (power of 2, max 256) */
    uint16_t last_used_idx;     /* Last used index seen by driver */
    uint16_t last_avail_idx;    /* Last available index written by driver */

    /* Virtqueue components (allocated memory) */
    virtq_desc_t* desc;         /* Descriptor table */
    virtq_avail_t* avail;       /* Available ring */
    virtq_used_t* used;         /* Used ring */

    /* Tracking */
    uint16_t free_head;         /* First free descriptor */
    uint16_t num_free;          /* Number of free descriptors */
    bool desc_in_use[VIRTIO_MAX_QUEUE_SIZE];  /* Descriptor allocation bitmap */
} virtqueue_t;

/**
 * VirtIO device structure.
 * Represents a VirtIO device with MMIO transport.
 */
typedef struct {
    void* base_addr;            /* MMIO base address (virtual) */
    uint32_t device_id;         /* Device ID (2 = block, 1 = network, etc.) */
    uint32_t vendor_id;         /* Vendor ID */
    uint64_t device_features;   /* Device-supported features */
    uint64_t driver_features;   /* Driver-negotiated features */
    uint32_t status;            /* Device status */

    virtqueue_t* vq;            /* Virtqueue (single queue for Phase 1) */
} virtio_device_t;

/**
 * Initialize VirtIO device.
 * Probes MMIO region and sets up device structure.
 *
 * Args:
 *   dev: VirtIO device structure
 *   base_addr: MMIO base address (e.g., 0x10001000 for QEMU)
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int virtio_device_init(virtio_device_t* dev, void* base_addr);

/**
 * Reset VirtIO device.
 * Clears device status and resets all state.
 *
 * Args:
 *   dev: VirtIO device
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int virtio_device_reset(virtio_device_t* dev);

/**
 * Negotiate features with device.
 * Driver selects features from device-supported set.
 *
 * Args:
 *   dev: VirtIO device
 *   driver_features: Driver-desired features
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int virtio_negotiate_features(virtio_device_t* dev, uint64_t driver_features);

/**
 * Finalize device initialization.
 * Marks device as ready (sets DRIVER_OK status).
 *
 * Args:
 *   dev: VirtIO device
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int virtio_device_ready(virtio_device_t* dev);

/**
 * Allocate and initialize virtqueue.
 *
 * Args:
 *   dev: VirtIO device
 *   queue_index: Queue index (0 for single queue)
 *   queue_size: Queue size (power of 2, max 256)
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int virtio_setup_queue(virtio_device_t* dev, uint16_t queue_index, uint16_t queue_size);

/**
 * Allocate a descriptor chain from virtqueue.
 *
 * Args:
 *   vq: Virtqueue
 *   count: Number of descriptors needed
 *   indices: Output array for allocated descriptor indices
 *
 * Returns:
 *   0 on success, -1 on failure (not enough free descriptors)
 */
int virtio_alloc_desc_chain(virtqueue_t* vq, uint16_t count, uint16_t* indices);

/**
 * Free a descriptor chain.
 *
 * Args:
 *   vq: Virtqueue
 *   head: Head descriptor index
 */
void virtio_free_desc_chain(virtqueue_t* vq, uint16_t head);

/**
 * Add descriptor chain to available ring.
 * Notifies device that descriptors are available.
 *
 * Args:
 *   dev: VirtIO device
 *   head: Head descriptor index
 *
 * Returns:
 *   0 on success, -1 on failure
 */
int virtio_add_to_avail(virtio_device_t* dev, uint16_t head);

/**
 * Check for used descriptors (polling).
 * Retrieves completed descriptor chains from used ring.
 *
 * Args:
 *   dev: VirtIO device
 *   head: Output - head descriptor index
 *   len: Output - bytes written by device
 *
 * Returns:
 *   1 if descriptor available, 0 if none, -1 on error
 */
int virtio_poll_used(virtio_device_t* dev, uint16_t* head, uint32_t* len);

/**
 * Cleanup VirtIO device and free resources.
 *
 * Args:
 *   dev: VirtIO device
 */
void virtio_device_cleanup(virtio_device_t* dev);

/**
 * Helper: Read 32-bit MMIO register.
 */
static inline uint32_t virtio_mmio_read32(virtio_device_t* dev, uint32_t offset) {
    volatile uint32_t* addr = (volatile uint32_t*)((uint8_t*)dev->base_addr + offset);
    return *addr;
}

/**
 * Helper: Write 32-bit MMIO register.
 */
static inline void virtio_mmio_write32(virtio_device_t* dev, uint32_t offset, uint32_t value) {
    volatile uint32_t* addr = (volatile uint32_t*)((uint8_t*)dev->base_addr + offset);
    *addr = value;
}

/**
 * Helper: Read 64-bit MMIO register (split into two 32-bit reads).
 */
static inline uint64_t virtio_mmio_read64(virtio_device_t* dev, uint32_t offset_low, uint32_t offset_high) {
    uint32_t low = virtio_mmio_read32(dev, offset_low);
    uint32_t high = virtio_mmio_read32(dev, offset_high);
    return ((uint64_t)high << 32) | low;
}

/**
 * Helper: Write 64-bit MMIO register (split into two 32-bit writes).
 */
static inline void virtio_mmio_write64(virtio_device_t* dev, uint32_t offset_low, uint32_t offset_high, uint64_t value) {
    virtio_mmio_write32(dev, offset_low, (uint32_t)(value & 0xFFFFFFFF));
    virtio_mmio_write32(dev, offset_high, (uint32_t)(value >> 32));
}

#endif /* VIRTIO_CORE_H */
