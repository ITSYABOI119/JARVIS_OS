/*
 * JARVIS AI-OS Phase 3 — Block Device Abstraction (x86)
 * In mock mode: uses static 1MB buffer as virtual disk.
 * In real mode: calls AHCI driver for NVMe/SATA access.
 */

#include "blk_dev_x86.h"
#include "ahci.h"
#include "pci.h"
#include "nvme.h"
#include <string.h>

static blk_dev_info_t g_info = {0};

#ifndef JARVIS_TEST_MOCK
static hba_port_t *g_active_port = NULL;
static nvme_controller_t g_nvme_ctrl;
static int g_use_nvme = 0;  /* 1 if NVMe is the active backend */

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

    /* Try AHCI first (class 0x01 Storage, subclass 0x06 SATA) */
    pci_device_t *ahci_pci = pci_find_device(PCI_CLASS_STORAGE,
                                              PCI_SUBCLASS_SATA,
                                              pci_devs, n);
    if (ahci_pci) {
        /* Get ABAR (BAR5 = AHCI MMIO base) and enable bus mastering */
        uint64_t abar = pci_get_bar_address(ahci_pci, 5);
        if (abar != 0) {
            pci_enable_bus_master(ahci_pci);

            /* Initialize AHCI controller discovery */
            ahci_controller_t ctrl;
            if (ahci_discover_init(&ctrl, abar) == 0) {
                /* Probe ports and find the first active one */
                ahci_probe_ports(&ctrl);
                int port_idx = -1;
                for (int i = 0; i < (int)ctrl.n_ports; i++) {
                    if (ahci_port_check_active(&ctrl, i)) {
                        port_idx = i;
                        break;
                    }
                }
                if (port_idx >= 0) {
                    /* Compute port MMIO address: ABAR + 0x100 + port_idx * 0x80 */
                    g_active_port = (hba_port_t *)((uint8_t *)(uintptr_t)abar
                                                   + 0x100
                                                   + (unsigned)port_idx * 0x80);

                    /* Send IDENTIFY DEVICE and parse results */
                    uint16_t identify_buf[256];
                    if (ahci_identify(g_active_port, identify_buf) == 0) {
                        ahci_identify_parse_model(identify_buf, g_info.model);
                        ahci_identify_parse_serial(identify_buf, g_info.serial);
                        g_info.total_sectors = ahci_identify_parse_capacity(identify_buf);
                        g_info.sector_size   = ahci_identify_parse_sector_size(identify_buf);
                        g_info.initialized   = true;
                        return 0;
                    }
                    g_active_port = NULL;
                }
            }
        }
    }

    /* If no AHCI disk, try NVMe (class 0x01 Storage, subclass 0x08 NVM) */
    {
        pci_device_t *nvme_pci = pci_find_device(PCI_CLASS_STORAGE,
                                                  PCI_SUBCLASS_NVM,
                                                  pci_devs, n);
        if (nvme_pci) {
            pci_enable_bus_master(nvme_pci);
            uint64_t bar0 = pci_get_bar_address(nvme_pci, 0);
            if (bar0 != 0) {
                /* NOTE: On seL4, bar0 must be mapped via vka_alloc_frame_at first.
                 * The rootserver (main_x86.c) handles this and passes the mapped
                 * address via blk_dev_init_nvme(). For now, use bar0 directly
                 * (works in non-seL4 environments or when identity-mapped). */

                /* Static DMA buffers (page-aligned for NVMe) */
                static uint8_t admin_sq[NVME_ADMIN_QUEUE_SIZE * 64]
                    __attribute__((aligned(4096)));
                static uint8_t admin_cq[NVME_ADMIN_QUEUE_SIZE * 16]
                    __attribute__((aligned(4096)));
                static uint8_t io_sq[NVME_IO_QUEUE_SIZE * 64]
                    __attribute__((aligned(4096)));
                static uint8_t io_cq[NVME_IO_QUEUE_SIZE * 16]
                    __attribute__((aligned(4096)));

                int err = nvme_init(&g_nvme_ctrl,
                    (volatile uint8_t *)(uintptr_t)bar0, bar0,
                    admin_sq, (uint64_t)(uintptr_t)admin_sq,
                    admin_cq, (uint64_t)(uintptr_t)admin_cq,
                    io_sq, (uint64_t)(uintptr_t)io_sq,
                    io_cq, (uint64_t)(uintptr_t)io_cq);

                if (err == 0) {
                    g_use_nvme = 1;
                    uint64_t lbas = 0;
                    uint32_t bsz = 0;
                    nvme_get_info(&g_nvme_ctrl, &lbas, &bsz);
                    g_info.total_sectors = lbas;
                    g_info.sector_size = bsz;
                    memcpy(g_info.model, g_nvme_ctrl.model,
                           sizeof(g_info.model));
                    memcpy(g_info.serial, g_nvme_ctrl.serial,
                           sizeof(g_info.serial));
                    g_info.initialized = true;
                    return 0;
                }
            }
        }
    }

    return -1;
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
    if (g_use_nvme) {
        /* NVMe read — buf_phys = (uintptr_t)buf for identity-mapped memory */
        return nvme_read_sectors(&g_nvme_ctrl, lba, count, buf,
                                  (uint64_t)(uintptr_t)buf);
    }
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
    if (g_use_nvme) {
        /* NVMe write not implemented yet — read-only for model loading */
        return -1;
    }
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

#ifndef JARVIS_TEST_MOCK
int blk_dev_init_nvme(volatile uint8_t *bar0_mapped, uint64_t bar0_phys,
                       void *admin_sq, uint64_t admin_sq_phys,
                       void *admin_cq, uint64_t admin_cq_phys,
                       void *io_sq, uint64_t io_sq_phys,
                       void *io_cq, uint64_t io_cq_phys)
{
    int err = nvme_init(&g_nvme_ctrl,
        bar0_mapped, bar0_phys,
        admin_sq, admin_sq_phys,
        admin_cq, admin_cq_phys,
        io_sq, io_sq_phys,
        io_cq, io_cq_phys);

    if (err != 0) return err;

    g_use_nvme = 1;
    uint64_t lbas = 0;
    uint32_t bsz = 0;
    nvme_get_info(&g_nvme_ctrl, &lbas, &bsz);
    g_info.total_sectors = lbas;
    g_info.sector_size = bsz;
    memcpy(g_info.model, g_nvme_ctrl.model, sizeof(g_info.model));
    memcpy(g_info.serial, g_nvme_ctrl.serial, sizeof(g_info.serial));
    g_info.initialized = true;
    return 0;
}
#endif
