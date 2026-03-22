/**
 * ahci.c - AHCI Controller Discovery / Probe + Disk I/O (x86-64)
 *
 * Part 1: Discovers AHCI HBA via PCI BAR5, reads capabilities, probes ports
 * for connected SATA/ATAPI devices.
 *
 * Part 2: Full disk I/O -- command setup, IDENTIFY DEVICE, READ DMA EXT,
 * WRITE DMA EXT using LBA48 addressing and PRDT scatter/gather.
 *
 * The discovery functions use ahci_controller_t and are named
 * ahci_discover_* / ahci_probe_* / ahci_port_* to avoid colliding with the
 * full-driver prototypes in ahci.h (ahci_init, ahci_identify, ahci_read,
 * ahci_write, etc.).
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

/* ========================================================================
 * Part 2: AHCI Disk I/O
 *
 * Command setup, IDENTIFY DEVICE, READ/WRITE DMA EXT (LBA48).
 * Uses the hba_port_t / hba_cmd_header_t / hba_cmd_table_t structs
 * defined in ahci.h.
 * ======================================================================== */

/* Maximum PRDT entries we support in our static command table.
 * 16 entries * 4 MB each = 64 MB max per command, more than enough. */
#define AHCI_IO_MAX_PRDT    16

/* Command timeout: 5 seconds expressed as loop iterations.
 * On real hardware each iteration is ~1us (MMIO read latency),
 * so 5,000,000 iterations ~= 5 seconds.
 * In mock mode, use a small timeout so timeout tests run fast. */
#ifdef JARVIS_TEST_MOCK
#define AHCI_CMD_TIMEOUT    100
#else
#define AHCI_CMD_TIMEOUT    5000000
#endif

/* Command infrastructure for slot 0 (one outstanding command).
 * In a full OS these would be DMA-allocated per port; for bare-metal
 * bootstrap and mock testing a static buffer is fine.
 * In mock mode, drop 'static' so tests can inspect the buffers. */
#ifdef JARVIS_TEST_MOCK
#define AHCI_STORAGE  /* non-static: visible to test file */
#else
#define AHCI_STORAGE static
#endif

/* Command list: 32 command headers (1 KB, 1K-aligned) */
AHCI_STORAGE hba_cmd_header_t ahci_cmd_list[AHCI_CMD_SLOTS]
    __attribute__((aligned(1024)));

/* Received FIS area (256 bytes, 256-byte aligned) */
AHCI_STORAGE hba_fis_t ahci_recv_fis
    __attribute__((aligned(256)));

/* Command table with space for AHCI_IO_MAX_PRDT PRDT entries.
 * Must be 128-byte aligned. Using a raw byte array sized to hold the
 * fixed 128-byte header plus the PRDT entries. */
AHCI_STORAGE uint8_t ahci_cmd_table_buf[128 + AHCI_IO_MAX_PRDT * sizeof(hba_prd_entry_t)]
    __attribute__((aligned(128)));

/**
 * ahci_setup_command - Prepare a command slot for execution
 * @port:    Port registers (used to set CLB/FB on first call)
 * @slot:    Command slot number (0-31)
 * @fis:     Pointer to the H2D FIS to copy into the command table CFIS area
 * @buf:     DMA buffer address (physical) for the data transfer, or NULL
 * @buf_len: Total transfer length in bytes (0 if no data)
 * @write:   Non-zero if this is a host-to-device (write) transfer
 *
 * Builds the command header (CFL, PRDTL, flags) and the command table
 * (CFIS + PRDT entries).  Each PRDT entry handles up to 4 MB.
 *
 * Returns 0 on success, -1 on invalid parameters.
 */
int ahci_setup_command(hba_port_t *port, int slot,
                       const fis_reg_h2d_t *fis,
                       uintptr_t buf, uint32_t buf_len,
                       int write)
{
    hba_cmd_header_t *hdr;
    hba_cmd_table_t *tbl;
    uint32_t prdt_count;
    uint32_t remaining;
    uintptr_t cur_addr;
    uint32_t i;

    if (!port || !fis || slot < 0 || slot >= AHCI_CMD_SLOTS)
        return -1;

    /* Calculate PRDT entries needed */
    if (buf_len == 0) {
        prdt_count = 0;
    } else {
        prdt_count = (buf_len + PRD_MAX_BYTES - 1) / PRD_MAX_BYTES;
        if (prdt_count > AHCI_IO_MAX_PRDT)
            return -1;  /* Transfer too large for our static table */
    }

    /* Point port at our static command list and FIS area */
    port->clb = (uint32_t)(uintptr_t)ahci_cmd_list;
    port->clbu = 0;
    port->fb = (uint32_t)(uintptr_t)&ahci_recv_fis;
    port->fbu = 0;

    /* Set up command header */
    hdr = &ahci_cmd_list[slot];
    memset(hdr, 0, sizeof(*hdr));

    /* CFL = FIS length in DWORDs (H2D FIS = 20 bytes = 5 DWORDs) */
    hdr->flags = (sizeof(fis_reg_h2d_t) / 4) & CMD_HDR_CFL_MASK;
    if (write)
        hdr->flags |= CMD_HDR_WRITE;
    hdr->prdtl = (uint16_t)prdt_count;
    hdr->prdbc = 0;

    /* Point command header at our static command table */
    hdr->ctba = (uint32_t)(uintptr_t)ahci_cmd_table_buf;
    hdr->ctbau = 0;

    /* Set up command table */
    tbl = (hba_cmd_table_t *)ahci_cmd_table_buf;
    memset(ahci_cmd_table_buf, 0, sizeof(ahci_cmd_table_buf));

    /* Copy the FIS into the CFIS area */
    memcpy(tbl->cfis, fis, sizeof(fis_reg_h2d_t));

    /* Build PRDT entries */
    remaining = buf_len;
    cur_addr = buf;
    for (i = 0; i < prdt_count; i++) {
        uint32_t chunk = remaining;
        if (chunk > PRD_MAX_BYTES)
            chunk = PRD_MAX_BYTES;

        tbl->prdt[i].dba = (uint32_t)cur_addr;
        tbl->prdt[i].dbau = 0;  /* 32-bit addresses for now */
        tbl->prdt[i].reserved = 0;
        /* DBC is byte_count - 1; set IOC on last entry */
        tbl->prdt[i].dbc = (chunk - 1) & PRD_DBC_MASK;
        if (i == prdt_count - 1)
            tbl->prdt[i].dbc |= PRD_DBC_IOC;

        cur_addr += chunk;
        remaining -= chunk;
    }

    return 0;
}

/**
 * ahci_issue_command - Issue a prepared command and wait for completion
 * @port: Port registers
 * @slot: Command slot number (0-31)
 *
 * Writes the CI (Command Issue) register to start the command, then polls
 * until the CI bit clears (command complete) or timeout.
 *
 * Returns:  0 on success
 *          -1 on timeout (CI bit never cleared)
 *          -2 on device error (TFD.STS.ERR set)
 */
int ahci_issue_command(hba_port_t *port, int slot)
{
    uint32_t ci_bit;
    int timeout;

    if (!port || slot < 0 || slot >= AHCI_CMD_SLOTS)
        return -1;

    ci_bit = 1U << slot;

    /* Clear any pending interrupt status */
    port->is = (uint32_t)-1;

    /* Issue the command */
    port->ci = ci_bit;

    /* Poll for completion */
    for (timeout = AHCI_CMD_TIMEOUT; timeout > 0; timeout--) {
        uint32_t ci = port->ci;
        if (!(ci & ci_bit))
            break;  /* Command completed */

        /* Check for errors while waiting */
        uint32_t tfd = port->tfd;
        if (tfd & PORT_TFD_STS_ERR)
            return -2;
    }

    if (timeout <= 0)
        return -1;  /* Timed out */

    /* Final error check */
    uint32_t tfd = port->tfd;
    if (tfd & PORT_TFD_STS_ERR)
        return -2;

    return 0;
}

/**
 * ahci_identify - Send ATA IDENTIFY DEVICE and parse the response
 * @port: Port registers
 * @buf:  512-byte output buffer for raw identify data
 *
 * Issues IDENTIFY DEVICE (0xEC), copies the 512 bytes of identify data
 * into buf.  Returns 0 on success, negative on error.
 */
int ahci_identify(hba_port_t *port, void *buf)
{
    fis_reg_h2d_t fis;
    int rc;

    if (!port || !buf)
        return -1;

    /* Build H2D FIS for IDENTIFY DEVICE */
    memset(&fis, 0, sizeof(fis));
    fis.fis_type = FIS_TYPE_REG_H2D;
    fis.flags = FIS_H2D_CMD;       /* This is a command FIS */
    fis.command = ATA_CMD_IDENTIFY;
    fis.device = 0;
    fis.count_lo = 0;
    fis.count_hi = 0;

    /* Set up command: 512-byte PIO-in transfer, slot 0 */
    rc = ahci_setup_command(port, 0, &fis, (uintptr_t)buf,
                            AHCI_SECTOR_SIZE, 0);
    if (rc != 0)
        return rc;

    /* Issue and wait */
    rc = ahci_issue_command(port, 0);
    if (rc != 0)
        return rc;

    return 0;
}

/**
 * ahci_identify_parse_model - Extract model string from identify data
 * @identify_buf: 512-byte identify data buffer
 * @out:          Output buffer (at least 41 bytes)
 *
 * ATA model string is in words 27-46 (40 chars), byte-swapped in pairs.
 */
void ahci_identify_parse_model(const uint16_t *identify_buf, char *out)
{
    int i;
    const uint16_t *words = identify_buf + 27;

    for (i = 0; i < 20; i++) {
        out[i * 2]     = (char)(words[i] >> 8);
        out[i * 2 + 1] = (char)(words[i] & 0xFF);
    }
    out[40] = '\0';

    /* Trim trailing spaces */
    for (i = 39; i >= 0 && out[i] == ' '; i--)
        out[i] = '\0';
}

/**
 * ahci_identify_parse_serial - Extract serial number from identify data
 * @identify_buf: 512-byte identify data buffer
 * @out:          Output buffer (at least 21 bytes)
 *
 * ATA serial is in words 10-19 (20 chars), byte-swapped in pairs.
 */
void ahci_identify_parse_serial(const uint16_t *identify_buf, char *out)
{
    int i;
    const uint16_t *words = identify_buf + 10;

    for (i = 0; i < 10; i++) {
        out[i * 2]     = (char)(words[i] >> 8);
        out[i * 2 + 1] = (char)(words[i] & 0xFF);
    }
    out[20] = '\0';

    /* Trim trailing spaces */
    for (i = 19; i >= 0 && out[i] == ' '; i--)
        out[i] = '\0';
}

/**
 * ahci_identify_parse_capacity - Extract LBA48 sector count from identify data
 * @identify_buf: 512-byte identify data buffer
 *
 * LBA48 capacity is in words 100-103 (uint64_t, little-endian).
 * Returns total number of addressable sectors.
 */
uint64_t ahci_identify_parse_capacity(const uint16_t *identify_buf)
{
    uint64_t cap;

    cap  = (uint64_t)identify_buf[100];
    cap |= (uint64_t)identify_buf[101] << 16;
    cap |= (uint64_t)identify_buf[102] << 32;
    cap |= (uint64_t)identify_buf[103] << 48;

    return cap;
}

/**
 * ahci_identify_parse_sector_size - Check logical sector size from identify data
 * @identify_buf: 512-byte identify data buffer
 *
 * Word 106 bit 12: if set, device has logical sector size > 512 bytes.
 * Returns the logical sector size in bytes (512 or value from words 117-118).
 */
uint32_t ahci_identify_parse_sector_size(const uint16_t *identify_buf)
{
    uint16_t w106 = identify_buf[106];

    /* Bit 14 must be 1 and bit 15 must be 0 for word 106 to be valid */
    if ((w106 & (1U << 14)) && !(w106 & (1U << 15))) {
        /* Bit 12: logical sector size > 512 bytes */
        if (w106 & (1U << 12)) {
            /* Words 117-118 contain logical sector size in words */
            uint32_t sz = (uint32_t)identify_buf[117]
                        | ((uint32_t)identify_buf[118] << 16);
            return sz * 2;  /* Convert words to bytes */
        }
    }

    return 512;  /* Default: standard 512-byte sectors */
}

/**
 * ahci_build_rw_fis - Build an H2D FIS for READ/WRITE DMA EXT
 * @fis:     Output FIS to fill
 * @command: ATA_CMD_READ_DMA_EXT (0x25) or ATA_CMD_WRITE_DMA_EXT (0x35)
 * @lba:     48-bit Logical Block Address
 * @count:   Sector count (1-65536; 0 means 65536 on hardware)
 */
void ahci_build_rw_fis(fis_reg_h2d_t *fis, uint8_t command,
                        uint64_t lba, uint16_t count)
{
    memset(fis, 0, sizeof(*fis));

    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags = FIS_H2D_CMD;
    fis->command = command;
    fis->device = 0x40;  /* LBA mode */

    /* LBA48 addressing: split across 6 bytes */
    fis->lba0 = (uint8_t)(lba & 0xFF);
    fis->lba1 = (uint8_t)((lba >> 8) & 0xFF);
    fis->lba2 = (uint8_t)((lba >> 16) & 0xFF);
    fis->lba3 = (uint8_t)((lba >> 24) & 0xFF);
    fis->lba4 = (uint8_t)((lba >> 32) & 0xFF);
    fis->lba5 = (uint8_t)((lba >> 40) & 0xFF);

    /* Sector count (0 means 65536 on ATA hardware) */
    fis->count_lo = (uint8_t)(count & 0xFF);
    fis->count_hi = (uint8_t)((count >> 8) & 0xFF);
}

/**
 * ahci_read_sectors - Read sectors from disk via READ DMA EXT (0x25)
 * @port:  Port registers
 * @lba:   Starting 48-bit LBA
 * @count: Number of sectors to read (1-65536)
 * @buf:   Output buffer (must be at least count * 512 bytes)
 *
 * Returns 0 on success, negative on error.
 */
int ahci_read_sectors(hba_port_t *port, uint64_t lba,
                      uint32_t count, void *buf)
{
    fis_reg_h2d_t fis;
    uint32_t byte_len;
    int rc;

    if (!port || !buf)
        return -1;
    if (count == 0 || count > 65536)
        return -1;

    byte_len = count * AHCI_SECTOR_SIZE;

    ahci_build_rw_fis(&fis, ATA_CMD_READ_DMA_EXT, lba, (uint16_t)count);

    rc = ahci_setup_command(port, 0, &fis, (uintptr_t)buf, byte_len, 0);
    if (rc != 0)
        return rc;

    return ahci_issue_command(port, 0);
}

/**
 * ahci_write_sectors - Write sectors to disk via WRITE DMA EXT (0x35)
 * @port:  Port registers
 * @lba:   Starting 48-bit LBA
 * @count: Number of sectors to write (1-65536)
 * @buf:   Input buffer (must be at least count * 512 bytes)
 *
 * Returns 0 on success, negative on error.
 */
int ahci_write_sectors(hba_port_t *port, uint64_t lba,
                       uint32_t count, const void *buf)
{
    fis_reg_h2d_t fis;
    uint32_t byte_len;
    int rc;

    if (!port || !buf)
        return -1;
    if (count == 0 || count > 65536)
        return -1;

    byte_len = count * AHCI_SECTOR_SIZE;

    ahci_build_rw_fis(&fis, ATA_CMD_WRITE_DMA_EXT, lba, (uint16_t)count);

    rc = ahci_setup_command(port, 0, &fis, (uintptr_t)buf, byte_len, 1);
    if (rc != 0)
        return rc;

    return ahci_issue_command(port, 0);
}
