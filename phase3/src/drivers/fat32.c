/**
 * fat32.c - Minimal read-only FAT32 parser
 *
 * Reads BPB, scans root directory for 8.3 filenames, follows cluster
 * chains via FAT table. No subdirectory traversal, no writes, no
 * dynamic allocation. Designed for bare-metal seL4 on x86-64.
 *
 * JARVIS AI-OS - Phase 3
 */

#include "fat32.h"
#include <string.h>

/* ---- BPB / Boot Sector Offsets ---- */
#define BPB_BYTES_PER_SECTOR    11
#define BPB_SECTORS_PER_CLUSTER 13
#define BPB_RESERVED_SECTORS    14
#define BPB_NUM_FATS            16
#define BPB_FAT32_FAT_SIZE      36
#define BPB_FAT32_ROOT_CLUSTER  44
#define BPB_SIGNATURE_OFFSET    510
#define BPB_SIGNATURE_VALUE     0xAA55

/* ---- Directory Entry Offsets ---- */
#define DIR_ENTRY_SIZE   32
#define DIR_NAME_OFFSET   0
#define DIR_ATTR_OFFSET  11
#define DIR_CLUSTER_HI   20
#define DIR_CLUSTER_LO   26
#define DIR_FILE_SIZE    28

/* ---- FAT Constants ---- */
#define FAT32_EOC_MIN   0x0FFFFFF8
#define FAT32_ENTRY_MASK 0x0FFFFFFF

/* ---- Helpers to read little-endian from byte buffer ---- */
static inline uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ---- Convert cluster number to absolute LBA ---- */
static uint64_t cluster_to_lba(fat32_fs_t *fs, uint32_t cluster)
{
    return fs->data_lba + (uint64_t)(cluster - 2) * fs->sectors_per_cluster;
}

/* ---- Look up the next cluster in the FAT ---- */
static int fat32_next_cluster(fat32_fs_t *fs, uint32_t cluster, uint32_t *next)
{
    uint32_t fat_offset  = cluster * 4;
    uint32_t fat_sector  = fat_offset / fs->bytes_per_sector;
    uint32_t entry_offset = fat_offset % fs->bytes_per_sector;

    uint8_t sector_buf[512];
    int err = fs->read(fs->fat_lba + fat_sector, 1, sector_buf);
    if (err)
        return err;

    uint32_t val = read_le32(sector_buf + entry_offset);
    *next = val & FAT32_ENTRY_MASK;
    return 0;
}

/* ---- Case-insensitive 11-byte 8.3 name compare ---- */
static int name_match(const uint8_t *entry, const char *name_8_3)
{
    for (int i = 0; i < 11; i++) {
        uint8_t a = entry[i];
        uint8_t b = (uint8_t)name_8_3[i];
        /* Uppercase both */
        if (a >= 'a' && a <= 'z') a -= 32;
        if (b >= 'a' && b <= 'z') b -= 32;
        if (a != b)
            return 0;
    }
    return 1;
}

/* ---- Public API ---- */

int fat32_init(fat32_fs_t *fs, fat32_read_fn read, uint64_t part_lba)
{
    uint8_t bpb[512];

    fs->read = read;
    fs->part_lba = part_lba;

    int err = read(part_lba, 1, bpb);
    if (err)
        return err;

    /* Validate boot signature */
    if (read_le16(bpb + BPB_SIGNATURE_OFFSET) != BPB_SIGNATURE_VALUE)
        return -1;

    fs->bytes_per_sector    = read_le16(bpb + BPB_BYTES_PER_SECTOR);
    fs->sectors_per_cluster = bpb[BPB_SECTORS_PER_CLUSTER];
    fs->reserved_sectors    = read_le16(bpb + BPB_RESERVED_SECTORS);
    fs->num_fats            = bpb[BPB_NUM_FATS];
    fs->fat_size_sectors    = read_le32(bpb + BPB_FAT32_FAT_SIZE);
    fs->root_cluster        = read_le32(bpb + BPB_FAT32_ROOT_CLUSTER);

    fs->fat_lba  = part_lba + fs->reserved_sectors;
    fs->data_lba = fs->fat_lba + (uint64_t)fs->num_fats * fs->fat_size_sectors;

    return 0;
}

int fat32_find_file(fat32_fs_t *fs, const char *name_8_3,
                    uint32_t *first_cluster, uint32_t *file_size)
{
    uint32_t cluster = fs->root_cluster;
    uint32_t cluster_bytes = fs->sectors_per_cluster * fs->bytes_per_sector;

    while (cluster >= 2 && cluster < FAT32_EOC_MIN) {
        uint64_t lba = cluster_to_lba(fs, cluster);

        /* Read one cluster at a time (max 32KB typical) */
        uint8_t buf[4096];  /* enough for 8 sectors * 512 */
        uint32_t to_read = cluster_bytes < sizeof(buf) ? cluster_bytes : sizeof(buf);
        uint32_t sectors = to_read / fs->bytes_per_sector;

        int err = fs->read(lba, sectors, buf);
        if (err)
            return err;

        /* Scan directory entries */
        uint32_t entries = to_read / DIR_ENTRY_SIZE;
        for (uint32_t i = 0; i < entries; i++) {
            uint8_t *ent = buf + i * DIR_ENTRY_SIZE;

            if (ent[0] == 0x00)    /* End of directory */
                return -1;
            if (ent[0] == 0xE5)    /* Deleted entry */
                continue;
            if ((ent[DIR_ATTR_OFFSET] & 0x0F) == 0x0F)  /* LFN entry */
                continue;

            if (name_match(ent + DIR_NAME_OFFSET, name_8_3)) {
                uint32_t hi = read_le16(ent + DIR_CLUSTER_HI);
                uint32_t lo = read_le16(ent + DIR_CLUSTER_LO);
                *first_cluster = (hi << 16) | lo;
                *file_size = read_le32(ent + DIR_FILE_SIZE);
                return 0;
            }
        }

        /* Follow cluster chain to next directory cluster */
        uint32_t next;
        err = fat32_next_cluster(fs, cluster, &next);
        if (err)
            return err;
        cluster = next;
    }

    return -1;  /* Not found */
}

int fat32_read_file(fat32_fs_t *fs, uint32_t first_cluster,
                    uint32_t file_size, void *buf)
{
    uint32_t cluster = first_cluster;
    uint32_t cluster_bytes = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint8_t *out = (uint8_t *)buf;
    uint32_t remaining = file_size;

    while (remaining > 0 && cluster >= 2 && cluster < FAT32_EOC_MIN) {
        uint64_t lba = cluster_to_lba(fs, cluster);
        uint32_t to_read = remaining < cluster_bytes ? remaining : cluster_bytes;
        uint32_t sectors = (to_read + fs->bytes_per_sector - 1) / fs->bytes_per_sector;

        int err = fs->read(lba, sectors, out);
        if (err)
            return err;

        out += to_read;
        remaining -= to_read;

        /* Follow cluster chain */
        uint32_t next;
        err = fat32_next_cluster(fs, cluster, &next);
        if (err)
            return err;
        cluster = next;
    }

    return (remaining == 0) ? 0 : -1;
}
