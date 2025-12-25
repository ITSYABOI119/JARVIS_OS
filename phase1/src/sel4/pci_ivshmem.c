/*
 * JARVIS AI-OS - ivshmem PCI Device Driver Implementation
 * Phase 2 Week 30
 *
 * Detects and maps QEMU ivshmem device for Python↔seL4 shared memory IPC.
 *
 * Two approaches are implemented:
 * 1. Full PCI enumeration (requires I/O port access)
 * 2. Fixed physical address mapping (simpler, works with seL4 tutorials)
 *
 * The fixed address approach is recommended for the seL4 tutorials framework
 * as it may not expose PCI configuration space access.
 */

#include "pci_ivshmem.h"
#include <stdio.h>
#include <string.h>

/*
 * NOTE: seL4 tutorials framework limitations
 *
 * The seL4 tutorials don't provide direct I/O port access for PCI configuration.
 * We implement both approaches:
 * 1. PCI detection (may not work in tutorials framework)
 * 2. Fixed address mapping (reliable fallback)
 *
 * For production use, proper seL4 PCI libraries would be needed.
 */

/* Fallback: Known physical address for ivshmem in QEMU */
/* This can be configured via QEMU -device ivshmem-plain options */
#define IVSHMEM_FALLBACK_PHYS_ADDR  0xFEBF0000  /* Default QEMU location */
#define IVSHMEM_FALLBACK_SIZE       567296       /* 567KB */

/* Static flag to track if we're using fallback mode */
static bool using_fallback_mode = false;

/**
 * Initialize ivshmem device structure
 */
void ivshmem_init(ivshmem_device_t *dev)
{
    if (!dev) return;
    memset(dev, 0, sizeof(ivshmem_device_t));
}

/*
 * PCI Configuration Space Access (x86 I/O ports)
 *
 * NOTE: These functions require I/O port access which may not be available
 * in the seL4 tutorials framework. They are included for completeness.
 */

#ifdef HAVE_PCI_IO_ACCESS

/* Build PCI configuration address */
static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    return (1u << 31) |                     /* Enable bit */
           ((uint32_t)bus << 16) |
           ((uint32_t)(slot & 0x1F) << 11) |
           ((uint32_t)(func & 0x07) << 8) |
           (offset & 0xFC);
}

/* Read 32-bit value from PCI configuration space */
static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    /* NOTE: This requires seL4 I/O port capabilities */
    /* In tutorials framework, this likely won't work */
    uint32_t address = pci_make_address(bus, slot, func, offset);

    /* Write address to CONFIG_ADDRESS (port 0xCF8) */
    /* outl(PCI_CONFIG_ADDRESS, address); */

    /* Read data from CONFIG_DATA (port 0xCFC) */
    /* return inl(PCI_CONFIG_DATA); */

    return 0xFFFFFFFF;  /* Return invalid if not implemented */
}

/* Write 32-bit value to PCI configuration space */
static void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    /* NOTE: This requires seL4 I/O port capabilities */
    uint32_t address = pci_make_address(bus, slot, func, offset);

    /* Write address to CONFIG_ADDRESS (port 0xCF8) */
    /* outl(PCI_CONFIG_ADDRESS, address); */

    /* Write data to CONFIG_DATA (port 0xCFC) */
    /* outl(PCI_CONFIG_DATA, value); */
}

/**
 * Scan PCI bus for ivshmem device (full PCI enumeration)
 */
bool ivshmem_detect(ivshmem_device_t *dev)
{
    if (!dev) return false;

    ivshmem_init(dev);

    printf("[ivshmem] Scanning PCI bus for device %04x:%04x...\n",
           IVSHMEM_VENDOR_ID, IVSHMEM_DEVICE_ID);

    /* Scan PCI bus 0 (QEMU places devices here) */
    for (int slot = 0; slot < PCI_MAX_SLOT; slot++) {
        uint32_t vendor_device = pci_config_read32(0, slot, 0, PCI_CONFIG_VENDOR_ID);

        /* Check for invalid device */
        if (vendor_device == 0xFFFFFFFF) continue;

        uint16_t vendor = vendor_device & 0xFFFF;
        uint16_t device = (vendor_device >> 16) & 0xFFFF;

        /* Check for ivshmem device */
        if (vendor == IVSHMEM_VENDOR_ID && device == IVSHMEM_DEVICE_ID) {
            printf("[ivshmem] Found device at bus 0, slot %d\n", slot);

            dev->detected = true;
            dev->bus = 0;
            dev->slot = slot;
            dev->func = 0;
            dev->vendor_id = vendor;
            dev->device_id = device;

            /* Read revision */
            uint32_t rev_class = pci_config_read32(0, slot, 0, PCI_CONFIG_REVISION_ID);
            dev->revision = rev_class & 0xFF;

            /* Read BAR0 (control registers) */
            dev->bar0_phys = pci_config_read32(0, slot, 0, PCI_CONFIG_BAR0) & PCI_BAR_MEMORY_MASK;

            /* Read BAR2 (shared memory) */
            dev->bar2_phys = pci_config_read32(0, slot, 0, PCI_CONFIG_BAR2) & PCI_BAR_MEMORY_MASK;

            /* Determine BAR2 size by writing all 1s and reading back */
            pci_config_write32(0, slot, 0, PCI_CONFIG_BAR2, 0xFFFFFFFF);
            uint32_t bar2_size_mask = pci_config_read32(0, slot, 0, PCI_CONFIG_BAR2);
            pci_config_write32(0, slot, 0, PCI_CONFIG_BAR2, dev->bar2_phys); /* Restore */

            dev->bar2_size = ~(bar2_size_mask & PCI_BAR_MEMORY_MASK) + 1;

            /* Enable memory access */
            uint32_t command = pci_config_read32(0, slot, 0, PCI_CONFIG_COMMAND);
            command |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
            pci_config_write32(0, slot, 0, PCI_CONFIG_COMMAND, command);

            return true;
        }
    }

    printf("[ivshmem] Device not found via PCI scan\n");
    return false;
}

#else /* !HAVE_PCI_IO_ACCESS */

/**
 * Fallback: Use fixed physical address instead of PCI enumeration
 */
bool ivshmem_detect(ivshmem_device_t *dev)
{
    if (!dev) return false;

    ivshmem_init(dev);

    printf("[ivshmem] PCI I/O access not available\n");
    printf("[ivshmem] Using fixed address fallback: 0x%08x\n", IVSHMEM_FALLBACK_PHYS_ADDR);

    /* Set up device with fallback address */
    dev->detected = true;
    dev->vendor_id = IVSHMEM_VENDOR_ID;
    dev->device_id = IVSHMEM_DEVICE_ID;
    dev->bar2_phys = IVSHMEM_FALLBACK_PHYS_ADDR;
    dev->bar2_size = IVSHMEM_FALLBACK_SIZE;

    using_fallback_mode = true;

    return true;
}

#endif /* HAVE_PCI_IO_ACCESS */

/**
 * Map ivshmem BAR2 to virtual address space
 *
 * NOTE: In seL4 tutorials framework, we need to use platform-specific
 * memory mapping. This implementation provides a framework but may need
 * adaptation for specific seL4 configurations.
 */
bool ivshmem_map_bar2(ivshmem_device_t *dev)
{
    if (!dev || !dev->detected) {
        printf("[ivshmem] Cannot map BAR2: device not detected\n");
        return false;
    }

    if (dev->mapped) {
        printf("[ivshmem] BAR2 already mapped\n");
        return true;
    }

    printf("[ivshmem] Mapping BAR2: phys=0x%08x size=%u bytes\n",
           dev->bar2_phys, dev->bar2_size);

    /*
     * NOTE: seL4 memory mapping
     *
     * In a full seL4 implementation, this would use:
     * 1. sel4platsupport's ps_io_map() for device memory
     * 2. Or vka_alloc_frame() + sel4utils_map_page_pd() for manual mapping
     *
     * For seL4 tutorials, the memory might already be mapped by the bootloader
     * or we need to use CAmkES connectors.
     *
     * Simplified approach: Cast physical address to pointer (works in some configs)
     * This is NOT correct for all seL4 configurations!
     */

#if defined(IVSHMEM_DIRECT_MAPPING)
    /*
     * Direct mapping: Physical address = Virtual address
     * This works in some seL4 configurations with identity mapping.
     */
    dev->bar2_vaddr = (void *)(uintptr_t)dev->bar2_phys;
    dev->mapped = true;
    printf("[ivshmem] Using direct mapping (phys == virt)\n");

#elif defined(IVSHMEM_USE_PS_IO_MAP)
    /*
     * Use sel4platsupport ps_io_map()
     * Requires io_ops structure to be available.
     */
    extern ps_io_ops_t io_ops;  /* Must be initialized elsewhere */

    dev->bar2_vaddr = ps_io_map(&io_ops.io_mapper,
                                dev->bar2_phys,
                                dev->bar2_size,
                                false,  /* not cached */
                                PS_MEM_NORMAL);

    if (dev->bar2_vaddr == NULL) {
        printf("[ivshmem] ps_io_map failed!\n");
        return false;
    }
    dev->mapped = true;

#else
    /*
     * Default: Direct mapping as fallback
     * This may or may not work depending on seL4 configuration.
     */
    dev->bar2_vaddr = (void *)(uintptr_t)dev->bar2_phys;
    dev->mapped = true;
    printf("[ivshmem] WARNING: Using direct mapping (may not work)\n");
#endif

    printf("[ivshmem] BAR2 mapped: vaddr=%p\n", dev->bar2_vaddr);
    return true;
}

/**
 * Map shared memory at a fixed physical address
 */
bool ivshmem_map_fixed_address(ivshmem_device_t *dev,
                               uint32_t phys_addr,
                               uint32_t size)
{
    if (!dev) return false;

    ivshmem_init(dev);

    dev->detected = true;
    dev->vendor_id = IVSHMEM_VENDOR_ID;
    dev->device_id = IVSHMEM_DEVICE_ID;
    dev->bar2_phys = phys_addr;
    dev->bar2_size = size;

    using_fallback_mode = true;

    return ivshmem_map_bar2(dev);
}

/**
 * Get pointer to shared memory region
 */
void *ivshmem_get_shared_memory(const ivshmem_device_t *dev)
{
    if (!dev || !dev->mapped) return NULL;
    return dev->bar2_vaddr;
}

/**
 * Get shared memory size
 */
uint32_t ivshmem_get_size(const ivshmem_device_t *dev)
{
    if (!dev || !dev->detected) return 0;
    return dev->bar2_size;
}

/**
 * Read ivshmem control register
 */
uint32_t ivshmem_read_reg(const ivshmem_device_t *dev, uint32_t offset)
{
    if (!dev || !dev->bar0_vaddr) return 0;

    volatile uint32_t *reg = (volatile uint32_t *)((uint8_t *)dev->bar0_vaddr + offset);
    return *reg;
}

/**
 * Write ivshmem control register
 */
void ivshmem_write_reg(ivshmem_device_t *dev, uint32_t offset, uint32_t value)
{
    if (!dev || !dev->bar0_vaddr) return;

    volatile uint32_t *reg = (volatile uint32_t *)((uint8_t *)dev->bar0_vaddr + offset);
    *reg = value;
}

/**
 * Get inter-VM position (guest ID)
 */
int32_t ivshmem_get_position(const ivshmem_device_t *dev)
{
    if (!dev || !dev->bar0_vaddr) return -1;
    return (int32_t)ivshmem_read_reg(dev, IVSHMEM_REG_IVPOSITION);
}

/**
 * Check if device is ready
 */
bool ivshmem_is_ready(const ivshmem_device_t *dev)
{
    return dev && dev->detected && dev->mapped && dev->bar2_vaddr;
}

/**
 * Print ivshmem device information
 */
void ivshmem_print_info(const ivshmem_device_t *dev)
{
    printf("\n========== IVSHMEM DEVICE INFO ==========\n");

    if (!dev) {
        printf("Device structure: NULL\n");
        printf("==========================================\n\n");
        return;
    }

    printf("Detected:    %s\n", dev->detected ? "Yes" : "No");
    printf("Mapped:      %s\n", dev->mapped ? "Yes" : "No");
    printf("Fallback:    %s\n", using_fallback_mode ? "Yes" : "No");

    if (dev->detected) {
        printf("\nPCI Location:\n");
        printf("  Bus:       %d\n", dev->bus);
        printf("  Slot:      %d\n", dev->slot);
        printf("  Function:  %d\n", dev->func);

        printf("\nDevice IDs:\n");
        printf("  Vendor:    0x%04x (%s)\n",
               dev->vendor_id,
               dev->vendor_id == IVSHMEM_VENDOR_ID ? "Red Hat" : "Unknown");
        printf("  Device:    0x%04x (%s)\n",
               dev->device_id,
               dev->device_id == IVSHMEM_DEVICE_ID ? "ivshmem" : "Unknown");
        printf("  Revision:  %d\n", dev->revision);

        printf("\nBAR Addresses:\n");
        printf("  BAR0 phys: 0x%08x (control registers)\n", dev->bar0_phys);
        printf("  BAR2 phys: 0x%08x (shared memory)\n", dev->bar2_phys);
        printf("  BAR2 size: %u bytes (%u KB)\n",
               dev->bar2_size, dev->bar2_size / 1024);

        if (dev->mapped) {
            printf("\nMapped Addresses:\n");
            printf("  BAR0 vaddr: %p\n", dev->bar0_vaddr);
            printf("  BAR2 vaddr: %p\n", dev->bar2_vaddr);
        }
    }

    printf("==========================================\n\n");
}
