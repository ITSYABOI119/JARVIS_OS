/*
 * JARVIS AI-OS Phase 3 — Block Device Abstraction (x86)
 * In mock mode: uses static 1MB buffer as virtual disk.
 * In real mode: calls AHCI driver for NVMe/SATA access.
 */

#include "blk_dev_x86.h"
#include "ahci.h"
#include <string.h>

static blk_dev_info_t g_info = {0};

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
    /* Real: probe AHCI, IDENTIFY first disk */
    /* TODO: ahci_discover_init, ahci_probe_ports, ahci_identify */
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
    return ahci_read_sectors(NULL, lba, count, buf);
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
    return ahci_write_sectors(NULL, lba, count, buf);
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
