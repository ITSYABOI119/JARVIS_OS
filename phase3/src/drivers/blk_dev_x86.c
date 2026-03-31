/*
 * JARVIS AI-OS Phase 3 — Block Device Abstraction (x86)
 * In mock mode: uses static 1MB buffer as virtual disk.
 * In real mode: calls AHCI driver for NVMe/SATA access.
 */

#include "blk_dev_x86.h"
#include "ahci.h"
#include "pci.h"
#include <string.h>

static blk_dev_info_t g_info = {0};

#ifndef JARVIS_TEST_MOCK
static hba_port_t *g_active_port = NULL;

/*
 * ahci_controller_t and discovery functions are defined in ahci.c
 * but not exported via ahci.h.  Declare them here for the real path.
 */
typedef struct {
    uint64_t mmio_base;
    uint32_t cap;
    uint32_t version;
    uint32_t ports_impl;
    uint32_t n_ports;
    uint32_t n_cmd_slots;
    int      supports_64bit;
    struct {
        int      present;
        uint32_t sig;
        uint32_t ssts;
    } ports[32];
} ahci_controller_t;

extern int  ahci_discover_init(ahci_controller_t *ctrl, uint64_t mmio_base);
extern void ahci_probe_ports(ahci_controller_t *ctrl);
extern int  ahci_port_check_active(ahci_controller_t *ctrl, int port);
#endif

#ifdef JARVIS_TEST_MOCK
/* Mock disk: 2048 sectors of 512 bytes = 1MB */
#define MOCK_DISK_SECTORS 2048
#define MOCK_SECTOR_SIZE  512
static uint8_t mock_disk[MOCK_DISK_SECTORS * MOCK_SECTOR_SIZE];
#endif

int blk_dev_init(void)
{
#ifdef JARVIS_TEST_MOCK
    memset(mock_disk, 0, sizeof(mock_disk));
    g_info.total_sectors = MOCK_DISK_SECTORS;
    g_info.sector_size = MOCK_SECTOR_SIZE;
    strncpy(g_info.model, "JARVIS Mock Disk", sizeof(g_info.model) - 1);
    g_info.model[sizeof(g_info.model) - 1] = '\0';
    strncpy(g_info.serial, "MOCK123456", sizeof(g_info.serial) - 1);
    g_info.serial[sizeof(g_info.serial) - 1] = '\0';
    g_info.initialized = true;
    return 0;
#else
    /* Real: PCI scan → find AHCI controller → IDENTIFY first disk */
    pci_device_t pci_devs[32];
    int n = pci_scan(pci_devs, 32);
    if (n <= 0) return -1;

    /* Find AHCI controller (class 0x01 Storage, subclass 0x06 SATA) */
    pci_device_t *ahci_pci = pci_find_device(PCI_CLASS_STORAGE,
                                              PCI_SUBCLASS_SATA,
                                              pci_devs, n);
    if (!ahci_pci) return -1;

    /* Get ABAR (BAR5 = AHCI MMIO base) and enable bus mastering */
    uint64_t abar = pci_get_bar_address(ahci_pci, 5);
    if (abar == 0) return -1;
    pci_enable_bus_master(ahci_pci);

    /* Initialize AHCI controller discovery */
    ahci_controller_t ctrl;
    if (ahci_discover_init(&ctrl, abar) != 0) return -1;

    /* Probe ports and find the first active one */
    ahci_probe_ports(&ctrl);
    int port_idx = -1;
    for (int i = 0; i < (int)ctrl.n_ports; i++) {
        if (ahci_port_check_active(&ctrl, i)) {
            port_idx = i;
            break;
        }
    }
    if (port_idx < 0) return -1;

    /* Compute port MMIO address: ABAR + 0x100 + port_idx * 0x80 */
    g_active_port = (hba_port_t *)((uint8_t *)(uintptr_t)abar
                                   + 0x100
                                   + (unsigned)port_idx * 0x80);

    /* Send IDENTIFY DEVICE and parse results */
    uint16_t identify_buf[256];
    if (ahci_identify(g_active_port, identify_buf) != 0) {
        g_active_port = NULL;
        return -1;
    }

    ahci_identify_parse_model(identify_buf, g_info.model);
    ahci_identify_parse_serial(identify_buf, g_info.serial);
    g_info.total_sectors = ahci_identify_parse_capacity(identify_buf);
    g_info.sector_size   = ahci_identify_parse_sector_size(identify_buf);
    g_info.initialized   = true;
    return 0;
#endif
}

int blk_dev_read(uint64_t lba, uint32_t count, void *buf)
{
    if (!g_info.initialized) return -1;
    if (count == 0 || !buf) return -3;
    if (lba + count > g_info.total_sectors) return -2;

#ifdef JARVIS_TEST_MOCK
    memcpy(buf, mock_disk + lba * g_info.sector_size, (size_t)count * g_info.sector_size);
    return 0;
#else
    return ahci_read_sectors(g_active_port, lba, count, buf);
#endif
}

int blk_dev_write(uint64_t lba, uint32_t count, const void *buf)
{
    if (!g_info.initialized) return -1;
    if (count == 0 || !buf) return -3;
    if (lba + count > g_info.total_sectors) return -2;

#ifdef JARVIS_TEST_MOCK
    memcpy(mock_disk + lba * g_info.sector_size, buf, (size_t)count * g_info.sector_size);
    return 0;
#else
    return ahci_write_sectors(g_active_port, lba, count, buf);
#endif
}

int blk_dev_get_info(blk_dev_info_t *info)
{
    if (!info) return -1;
    if (!g_info.initialized) return -1;
    *info = g_info;
    return 0;
}

bool blk_dev_is_ready(void)
{
    return g_info.initialized;
}
