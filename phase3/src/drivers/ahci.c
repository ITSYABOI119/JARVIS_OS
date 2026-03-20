/**
 * ahci.c - AHCI Controller Discovery / Probe (x86-64)
 *
 * Discovers AHCI HBA via PCI BAR5, reads capabilities, probes ports for
 * connected SATA/ATAPI devices. Discovery only -- no read/write/DMA.
 *
 * The functions here use the ahci_controller_t discovery struct and are
 * named ahci_discover_*  /  ahci_probe_*  /  ahci_port_*  to avoid
 * colliding with the full-driver prototypes in ahci.h (ahci_init,
 * ahci_port_init, ahci_read, ahci_write, etc.) which will be implemented
 * later when the full AHCI DMA path is built.
 *
 * Reference: https://wiki.osdev.org/AHCI
 *
 * JARVIS AI-OS - Phase 3
 */

#include "ahci.h"
#include <stdio.h>
#include <string.h>

/* ========================================================================
 * MMIO Access
 * ======================================================================== */

#ifndef JARVIS_TEST_MOCK
static inline uint32_t mmio_read32(volatile void *addr)
{
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write32(volatile void *addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}
#else
/* Mock MMIO for testing -- defined in test file */
extern uint32_t mmio_read32(volatile void *addr);
extern void mmio_write32(volatile void *addr, uint32_t val);
#endif

/* ========================================================================
 * Controller Discovery State
 * ======================================================================== */

typedef struct {
    uint64_t mmio_base;       /* HBA MMIO base from PCI BAR5 */
    uint32_t cap;             /* HBA capabilities register */
    uint32_t version;         /* HBA version (VS register) */
    uint32_t ports_impl;      /* Port implemented bitmap (PI register) */
    uint32_t n_ports;         /* Number of ports available */
    uint32_t n_cmd_slots;     /* Number of command slots per port */
    int      supports_64bit;  /* CAP bit 31 */
    /* Per-port status */
    struct {
        int      present;     /* Device detected */
        uint32_t sig;         /* Device signature (ATA, ATAPI, etc.) */
        uint32_t ssts;        /* SATA status */
    } ports[32];
} ahci_controller_t;

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * Read a 32-bit HBA register at the given offset from the MMIO base.
 */
static uint32_t hba_read(ahci_controller_t *ctrl, uint32_t offset)
{
    return mmio_read32((volatile void *)(uintptr_t)(ctrl->mmio_base + offset));
}

/**
 * Write a 32-bit HBA register at the given offset from the MMIO base.
 */
static void hba_write(ahci_controller_t *ctrl, uint32_t offset, uint32_t val)
{
    mmio_write32((volatile void *)(uintptr_t)(ctrl->mmio_base + offset), val);
}

/**
 * Return the device type string for a given port signature.
 */
static const char *ahci_sig_name(uint32_t sig)
{
    switch (sig) {
    case SATA_SIG_ATA:   return "SATA";
    case SATA_SIG_ATAPI: return "ATAPI";
    case SATA_SIG_SEMB:  return "SEMB";
    case SATA_SIG_PM:    return "Port Multiplier";
    default:             return "Unknown";
    }
}

/* ========================================================================
 * Public Discovery API
 * ======================================================================== */

/**
 * ahci_discover_init - Initialize AHCI controller discovery from MMIO base
 * @ctrl:      Controller discovery state to fill in
 * @mmio_base: Physical MMIO base address (PCI BAR5)
 *
 * Reads HBA capabilities, version, and port implemented registers.
 * Sets GHC.AE (AHCI Enable).
 * Returns 0 on success, -1 on error.
 */
int ahci_discover_init(ahci_controller_t *ctrl, uint64_t mmio_base)
{
    uint32_t cap, ghc;

    if (!ctrl)
        return -1;

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->mmio_base = mmio_base;

    /* Read Host Capabilities (offset 0x00) */
    cap = hba_read(ctrl, HBA_REG_CAP);
    ctrl->cap = cap;

    /* Number of ports: bits 4:0, 0-based (value + 1) */
    ctrl->n_ports = (cap & CAP_NP_MASK) + 1;

    /* Number of command slots: bits 12:8, 0-based (value + 1) */
    ctrl->n_cmd_slots = ((cap & CAP_NCS_MASK) >> CAP_NCS_SHIFT) + 1;

    /* 64-bit addressing support: bit 31 */
    ctrl->supports_64bit = (cap & CAP_S64A) ? 1 : 0;

    /* Read AHCI Version (offset 0x10) */
    ctrl->version = hba_read(ctrl, HBA_REG_VS);

    /* Read Ports Implemented (offset 0x0C) */
    ctrl->ports_impl = hba_read(ctrl, HBA_REG_PI);

    /* Enable AHCI mode: set GHC.AE (bit 31) in Global Host Control */
    ghc = hba_read(ctrl, HBA_REG_GHC);
    ghc |= GHC_AE;
    hba_write(ctrl, HBA_REG_GHC, ghc);

    return 0;
}

/**
 * ahci_probe_ports - Probe all implemented ports for connected devices
 * @ctrl: Controller state (must be initialized via ahci_discover_init)
 *
 * For each bit set in ports_impl, reads the port SSTS and SIG registers
 * to determine if a device is present and what type it is.
 */
void ahci_probe_ports(ahci_controller_t *ctrl)
{
    uint32_t pi, ssts, sig;
    int port;

    if (!ctrl)
        return;

    pi = ctrl->ports_impl;

    for (port = 0; port < 32; port++) {
        if (!(pi & (1U << port)))
            continue;

        /* Port register base = MMIO base + 0x100 + port * 0x80 */
        uint32_t port_base = HBA_PORT_BASE + ((uint32_t)port * HBA_PORT_SIZE);

        /* Read SStatus (port base + 0x28): DET in bits 3:0 */
        ssts = hba_read(ctrl, port_base + PORT_SSTS);
        ctrl->ports[port].ssts = ssts;

        uint32_t det = ssts & PORT_SSTS_DET_MASK;

        if (det == PORT_SSTS_DET_ACTIVE) {
            /* Device present and communication established */
            ctrl->ports[port].present = 1;

            /* Read Signature (port base + 0x24) */
            sig = hba_read(ctrl, port_base + PORT_SIG);
            ctrl->ports[port].sig = sig;
        } else {
            ctrl->ports[port].present = 0;
            ctrl->ports[port].sig = 0;
        }
    }
}

/**
 * ahci_port_check_active - Check if a port has an active device
 * @ctrl: Controller state
 * @port: Port number (0-31)
 *
 * A port is active if:
 *   - SSTS DET field (bits 3:0) == 3 (device present, communication ok)
 *   - SSTS IPM field (bits 11:8) == 1 (interface in active state)
 *
 * Returns 1 if active, 0 otherwise.
 */
int ahci_port_check_active(ahci_controller_t *ctrl, int port)
{
    uint32_t ssts;

    if (!ctrl || port < 0 || port >= 32)
        return 0;

    ssts = ctrl->ports[port].ssts;

    uint32_t det = ssts & PORT_SSTS_DET_MASK;
    uint32_t ipm = ssts & PORT_SSTS_IPM_MASK;

    return (det == PORT_SSTS_DET_ACTIVE) && (ipm == PORT_SSTS_IPM_ACTIVE);
}

/**
 * ahci_print_info - Print AHCI controller and port information
 * @ctrl: Controller state (must have been probed)
 */
void ahci_print_info(ahci_controller_t *ctrl)
{
    int port;
    uint32_t ver_major, ver_minor;

    if (!ctrl)
        return;

    ver_major = (ctrl->version >> 16) & 0xFFFF;
    ver_minor = ctrl->version & 0xFFFF;

    printf("AHCI Controller Info:\n");
    printf("  Version:       %u.%u.%u\n",
           ver_major, (ver_minor >> 8) & 0xFF, ver_minor & 0xFF);
    printf("  Ports:         %u (implemented: 0x%08X)\n",
           ctrl->n_ports, ctrl->ports_impl);
    printf("  Command Slots: %u\n", ctrl->n_cmd_slots);
    printf("  64-bit DMA:    %s\n", ctrl->supports_64bit ? "Yes" : "No");
    printf("  MMIO Base:     0x%lX\n", (unsigned long)ctrl->mmio_base);

    for (port = 0; port < 32; port++) {
        if (!(ctrl->ports_impl & (1U << port)))
            continue;

        if (ctrl->ports[port].present) {
            printf("  Port %d: %s (SIG=0x%08X, SSTS=0x%08X) [ACTIVE]\n",
                   port,
                   ahci_sig_name(ctrl->ports[port].sig),
                   ctrl->ports[port].sig,
                   ctrl->ports[port].ssts);
        } else {
            printf("  Port %d: No device (SSTS=0x%08X)\n",
                   port, ctrl->ports[port].ssts);
        }
    }
}
