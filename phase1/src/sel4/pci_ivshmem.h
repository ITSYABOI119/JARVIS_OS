/*
 * JARVIS AI-OS - ivshmem PCI Device Driver
 * Phase 2 Week 30
 *
 * Detects and maps QEMU ivshmem (Inter-VM Shared Memory) PCI device.
 * This enables shared memory IPC between Python (host) and seL4 (QEMU guest).
 *
 * ivshmem PCI Device:
 *   Vendor ID: 0x1AF4 (Red Hat)
 *   Device ID: 0x1110
 *   BAR0: Control registers (256 bytes)
 *   BAR2: Shared memory region (configurable size)
 *
 * Reference: https://www.qemu.org/docs/master/specs/ivshmem-spec.html
 */

#ifndef PCI_IVSHMEM_H
#define PCI_IVSHMEM_H

#include <stdint.h>
#include <stdbool.h>

/* ivshmem PCI device identifiers */
#define IVSHMEM_VENDOR_ID       0x1AF4
#define IVSHMEM_DEVICE_ID       0x1110
#define IVSHMEM_REVISION        1

/* PCI configuration space offsets */
#define PCI_CONFIG_VENDOR_ID    0x00
#define PCI_CONFIG_DEVICE_ID    0x02
#define PCI_CONFIG_COMMAND      0x04
#define PCI_CONFIG_STATUS       0x06
#define PCI_CONFIG_REVISION_ID  0x08
#define PCI_CONFIG_CLASS_CODE   0x09
#define PCI_CONFIG_CACHE_LINE   0x0C
#define PCI_CONFIG_LATENCY      0x0D
#define PCI_CONFIG_HEADER_TYPE  0x0E
#define PCI_CONFIG_BAR0         0x10
#define PCI_CONFIG_BAR1         0x14
#define PCI_CONFIG_BAR2         0x18
#define PCI_CONFIG_BAR3         0x1C
#define PCI_CONFIG_BAR4         0x20
#define PCI_CONFIG_BAR5         0x24

/* PCI command register bits */
#define PCI_COMMAND_IO          0x0001
#define PCI_COMMAND_MEMORY      0x0002
#define PCI_COMMAND_MASTER      0x0004

/* PCI BAR masks */
#define PCI_BAR_IO_MASK         0x00000001
#define PCI_BAR_MEMORY_MASK     0xFFFFFFF0
#define PCI_BAR_TYPE_MASK       0x00000006

/* ivshmem BAR0 register offsets (control registers) */
#define IVSHMEM_REG_MASK        0x00    /* Interrupt mask */
#define IVSHMEM_REG_STATUS      0x04    /* Interrupt status */
#define IVSHMEM_REG_IVPOSITION  0x08    /* Inter-VM position (guest ID) */
#define IVSHMEM_REG_DOORBELL    0x0C    /* Doorbell register */

/* PCI I/O ports (x86 specific) */
#define PCI_CONFIG_ADDRESS      0x0CF8
#define PCI_CONFIG_DATA         0x0CFC

/* Maximum PCI buses/slots/functions to scan */
#define PCI_MAX_BUS             1       /* Only scan bus 0 for QEMU */
#define PCI_MAX_SLOT            32
#define PCI_MAX_FUNC            1       /* Only function 0 */

/* Expected shared memory size for JARVIS IPC */
#define IVSHMEM_EXPECTED_SIZE   567296  /* 567KB for dual_ring_buffer_t */

/**
 * ivshmem device state structure
 */
typedef struct {
    bool detected;              /* Device found flag */
    bool mapped;                /* BAR2 mapped to virtual address */

    /* PCI location */
    uint8_t bus;                /* PCI bus number */
    uint8_t slot;               /* PCI slot (device) number */
    uint8_t func;               /* PCI function number */

    /* Device info */
    uint16_t vendor_id;         /* Should be 0x1AF4 */
    uint16_t device_id;         /* Should be 0x1110 */
    uint8_t revision;           /* Revision ID */

    /* BAR addresses */
    uint32_t bar0_phys;         /* BAR0 physical address (control regs) */
    uint32_t bar2_phys;         /* BAR2 physical address (shared memory) */
    uint32_t bar2_size;         /* BAR2 size in bytes */

    /* Mapped virtual addresses */
    void *bar0_vaddr;           /* Mapped BAR0 (control registers) */
    void *bar2_vaddr;           /* Mapped BAR2 (shared memory) */
} ivshmem_device_t;

/**
 * Initialize ivshmem device structure
 * @param dev Pointer to device structure
 */
void ivshmem_init(ivshmem_device_t *dev);

/**
 * Scan PCI bus for ivshmem device
 * @param dev Output device structure
 * @return true if found, false otherwise
 */
bool ivshmem_detect(ivshmem_device_t *dev);

/**
 * Map ivshmem BAR2 (shared memory) to virtual address space
 *
 * NOTE: This function requires seL4 memory mapping capabilities.
 * In the seL4 tutorials framework, this may need simplification
 * (e.g., using a known physical address instead of PCI detection).
 *
 * @param dev Device structure (must be detected first)
 * @return true on success, false on failure
 */
bool ivshmem_map_bar2(ivshmem_device_t *dev);

/**
 * Get pointer to shared memory region
 * @param dev Device structure (must be mapped first)
 * @return Pointer to shared memory, or NULL if not mapped
 */
void *ivshmem_get_shared_memory(const ivshmem_device_t *dev);

/**
 * Get shared memory size
 * @param dev Device structure
 * @return Size in bytes, or 0 if not detected
 */
uint32_t ivshmem_get_size(const ivshmem_device_t *dev);

/**
 * Read ivshmem control register (from BAR0)
 * @param dev Device structure
 * @param offset Register offset (IVSHMEM_REG_*)
 * @return Register value, or 0 if not mapped
 */
uint32_t ivshmem_read_reg(const ivshmem_device_t *dev, uint32_t offset);

/**
 * Write ivshmem control register (to BAR0)
 * @param dev Device structure
 * @param offset Register offset (IVSHMEM_REG_*)
 * @param value Value to write
 */
void ivshmem_write_reg(ivshmem_device_t *dev, uint32_t offset, uint32_t value);

/**
 * Get inter-VM position (guest ID)
 * @param dev Device structure
 * @return Guest ID, or -1 if not available
 */
int32_t ivshmem_get_position(const ivshmem_device_t *dev);

/**
 * Print ivshmem device information (for debugging)
 * @param dev Device structure
 */
void ivshmem_print_info(const ivshmem_device_t *dev);

/**
 * Check if ivshmem device is ready for use
 * @param dev Device structure
 * @return true if detected and mapped, false otherwise
 */
bool ivshmem_is_ready(const ivshmem_device_t *dev);

/*
 * Alternative: Fixed Physical Address Mapping
 *
 * If PCI detection proves too complex in seL4 tutorials framework,
 * use this simpler approach with a known physical address.
 */

/**
 * Map shared memory at a fixed physical address (alternative to PCI detection)
 *
 * This bypasses PCI enumeration by using a known physical address.
 * Configure QEMU to place ivshmem at a specific address:
 *   -device ivshmem-plain,memdev=shm0,addr=0x8
 *
 * @param dev Device structure
 * @param phys_addr Known physical address
 * @param size Memory size
 * @return true on success, false on failure
 */
bool ivshmem_map_fixed_address(ivshmem_device_t *dev,
                               uint32_t phys_addr,
                               uint32_t size);

#endif /* PCI_IVSHMEM_H */
