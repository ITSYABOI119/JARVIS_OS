/**
 * pci.c - PCI Bus Enumeration Driver (x86-64)
 *
 * Implements PCI configuration space access via I/O ports 0xCF8/0xCFC,
 * bus enumeration, device discovery, BAR parsing, and bus master enable.
 *
 * Scans bus 0 only (sufficient for single-segment desktop/server boards).
 * Multifunction devices detected via header type bit 7.
 *
 * JARVIS AI-OS - Phase 3
 */

#include "pci.h"

/* ========================================================================
 * I/O Port Access
 *
 * Three backends:
 *   1. JARVIS_TEST_MOCK: extern functions (test harness provides)
 *   2. JARVIS_SEL4: function pointers set via pci_set_ioport_ops()
 *      (rootserver uses seL4_X86_IOPort_In32/Out32 with IOPort cap)
 *   3. Default: inline asm (bare metal without OS, or Linux userspace with ioperm)
 * ======================================================================== */

#ifdef JARVIS_TEST_MOCK
/* Test mock — defined externally */
extern void outl(uint16_t port, uint32_t val);
extern uint32_t inl(uint16_t port);

#elif defined(JARVIS_SEL4)
/* seL4: I/O port access via function pointers (set by rootserver) */
static void (*g_outl)(uint16_t port, uint32_t val) = 0;
static uint32_t (*g_inl)(uint16_t port) = 0;

static void outl(uint16_t port, uint32_t val) {
    if (g_outl) g_outl(port, val);
}
static uint32_t inl(uint16_t port) {
    return g_inl ? g_inl(port) : 0xFFFFFFFF;
}

void pci_set_ioport_ops(void (*out_fn)(uint16_t, uint32_t),
                         uint32_t (*in_fn)(uint16_t))
{
    g_outl = out_fn;
    g_inl = in_fn;
}

#else
/* Bare metal: direct I/O port instructions */
static inline void outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
#endif

/* ========================================================================
 * PCI Configuration Space Access
 * ======================================================================== */

/**
 * Build the CONFIG_ADDRESS value for a given BDF + register offset.
 *
 * Format:
 *   Bit 31     : Enable (1)
 *   Bits 23-16 : Bus number
 *   Bits 15-11 : Device number
 *   Bits 10-8  : Function number
 *   Bits 7-2   : Register offset (4-byte aligned)
 *   Bits 1-0   : 0
 */
static uint32_t pci_build_address(uint8_t bus, uint8_t device,
                                   uint8_t func, uint8_t offset)
{
    return PCI_ADDR_ENABLE
         | ((uint32_t)bus    << 16)
         | ((uint32_t)(device & 0x1F) << 11)
         | ((uint32_t)(func & 0x07)   << 8)
         | ((uint32_t)(offset & 0xFC));
}

uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t func,
                          uint8_t offset)
{
    uint32_t addr = pci_build_address(bus, device, func, offset);
    outl(PCI_CONFIG_ADDRESS, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write(uint8_t bus, uint8_t device, uint8_t func,
                       uint8_t offset, uint32_t value)
{
    uint32_t addr = pci_build_address(bus, device, func, offset);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, value);
}

/* ========================================================================
 * PCI Bus Scan
 * ======================================================================== */

/**
 * Read a single PCI device's configuration header into a pci_device_t.
 * Assumes the caller already verified vendor_id != 0xFFFF.
 */
static void pci_read_device(uint8_t bus, uint8_t dev, uint8_t func,
                             pci_device_t *out)
{
    uint32_t reg;

    out->bus      = bus;
    out->device   = dev;
    out->function = func;

    /* Offset 0x00: vendor_id (low 16) | device_id (high 16) */
    reg = pci_config_read(bus, dev, func, PCI_VENDOR_ID);
    out->vendor_id = (uint16_t)(reg & 0xFFFF);
    out->device_id = (uint16_t)(reg >> 16);

    /* Offset 0x08: revision (7:0) | prog_if (15:8) | subclass (23:16) | class (31:24) */
    reg = pci_config_read(bus, dev, func, PCI_REVISION_ID);
    out->revision_id = (uint8_t)(reg & 0xFF);
    out->prog_if     = (uint8_t)((reg >> 8) & 0xFF);
    out->subclass    = (uint8_t)((reg >> 16) & 0xFF);
    out->class_code  = (uint8_t)((reg >> 24) & 0xFF);

    /* Offset 0x0C: cache_line (7:0) | latency (15:8) | header_type (23:16) | BIST (31:24) */
    reg = pci_config_read(bus, dev, func, PCI_CACHE_LINE_SIZE);
    out->header_type = (uint8_t)((reg >> 16) & 0xFF);

    /* BARs: offsets 0x10 - 0x24 (6 BARs, only valid for header type 0) */
    for (int i = 0; i < 6; i++) {
        out->bar[i] = pci_config_read(bus, dev, func,
                                       PCI_BAR0 + (uint8_t)(i * 4));
    }

    /* Offset 0x3C: interrupt_line (7:0) | interrupt_pin (15:8) */
    reg = pci_config_read(bus, dev, func, PCI_INTERRUPT_LINE);
    out->interrupt_line = (uint8_t)(reg & 0xFF);
    out->interrupt_pin  = (uint8_t)((reg >> 8) & 0xFF);
}

int pci_scan(pci_device_t *devices, int max_devices)
{
    int count = 0;

    /* Scan buses 0-15 (real hardware has devices behind PCI bridges) */
    for (uint8_t bus = 0; bus < 16 && count < max_devices; bus++) {
    for (uint8_t dev = 0; dev < PCI_MAX_DEVICE && count < max_devices; dev++) {
        /* Read vendor_id of function 0 */
        uint32_t id_reg = pci_config_read(bus, dev, 0, PCI_VENDOR_ID);
        uint16_t vendor = (uint16_t)(id_reg & 0xFFFF);

        if (vendor == 0xFFFF)
            continue;  /* No device present at this slot */

        /* Read header type to check multifunction bit */
        uint32_t hdr_reg = pci_config_read(bus, dev, 0, PCI_CACHE_LINE_SIZE);
        uint8_t header_type = (uint8_t)((hdr_reg >> 16) & 0xFF);
        int multifunction = (header_type & PCI_HEADER_MULTIFUNCTION) != 0;

        /* Determine how many functions to probe */
        int max_func = multifunction ? PCI_MAX_FUNCTION : 1;

        for (uint8_t func = 0; func < max_func; func++) {
            if (count >= max_devices)
                return count;

            /* For func > 0, re-check vendor_id */
            if (func > 0) {
                id_reg = pci_config_read(bus, dev, func, PCI_VENDOR_ID);
                vendor = (uint16_t)(id_reg & 0xFFFF);
                if (vendor == 0xFFFF)
                    continue;
            }

            pci_read_device(bus, dev, func, &devices[count]);
            count++;
        }
    }
    } /* end bus loop */

    return count;
}

/* ========================================================================
 * Device Lookup
 * ======================================================================== */

pci_device_t *pci_find_device(uint8_t class_code, uint8_t subclass,
                               pci_device_t *devices, int count)
{
    for (int i = 0; i < count; i++) {
        if (devices[i].class_code == class_code &&
            devices[i].subclass == subclass) {
            return &devices[i];
        }
    }
    return (void *)0;  /* NULL without requiring stdlib */
}

/* ========================================================================
 * BAR Helpers
 * ======================================================================== */

uint64_t pci_get_bar_address(pci_device_t *dev, int bar_index)
{
    if (bar_index < 0 || bar_index > 5)
        return 0;

    uint32_t bar = dev->bar[bar_index];

    if (bar & PCI_BAR_IO_SPACE) {
        /* I/O space BAR: 32-bit only */
        return (uint64_t)(bar & PCI_BAR_IO_ADDR_MASK);
    }

    /* Memory space BAR */
    uint64_t addr = (uint64_t)(bar & PCI_BAR_MEM_ADDR_MASK);

    /* Check if 64-bit BAR (type bits 2:1 = 10b = 0x04) */
    if ((bar & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_TYPE_64) {
        if (bar_index < 5) {
            uint64_t high = (uint64_t)dev->bar[bar_index + 1];
            addr |= (high << 32);
        }
    }

    return addr;
}

int pci_is_bar_64bit(pci_device_t *dev, int bar_index)
{
    if (bar_index < 0 || bar_index > 5)
        return 0;
    uint32_t bar = dev->bar[bar_index];
    if (bar & PCI_BAR_IO_SPACE)
        return 0;
    return (bar & PCI_BAR_MEM_TYPE_MASK) == PCI_BAR_MEM_TYPE_64;
}

int pci_is_bar_mmio(pci_device_t *dev, int bar_index)
{
    if (bar_index < 0 || bar_index > 5)
        return 0;

    /* Bit 0: 0 = memory space (MMIO), 1 = I/O space */
    return (dev->bar[bar_index] & PCI_BAR_IO_SPACE) == 0;
}

/* ========================================================================
 * Bus Master Enable
 * ======================================================================== */

void pci_enable_bus_master(pci_device_t *dev)
{
    /* Read current command register (32-bit read at offset 0x04) */
    uint32_t cmd = pci_config_read(dev->bus, dev->device, dev->function,
                                    PCI_COMMAND);

    /* Set bus master bit (bit 2) in the low 16 bits */
    cmd |= PCI_CMD_BUS_MASTER;

    /* Write back */
    pci_config_write(dev->bus, dev->device, dev->function,
                      PCI_COMMAND, cmd);
}
