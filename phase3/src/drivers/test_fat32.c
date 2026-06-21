/**
 * test_fat32.c - Mock tests for the FAT32 read-only parser
 *
 * Builds a synthetic FAT32 filesystem in a 1MB memory buffer and
 * exercises BPB parsing, root directory scan, and cluster-chain read.
 *
 * 10 tests, all using mock block I/O (no hardware needed).
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *     -I phase3/src/drivers \
 *     phase3/src/drivers/test_fat32.c phase3/src/drivers/fat32.c \
 *     -o /tmp/test_fat32 && /tmp/test_fat32
 *
 * JARVIS AI-OS - Phase 3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "fat32.h"

/* ---- Synthetic disk image ---- */
#define MOCK_SECTORS 2048
#define SECTOR_SIZE  512
static uint8_t mock_disk[MOCK_SECTORS * SECTOR_SIZE];

static int pass_count = 0;
static int fail_count = 0;
static int g_fat_sector_reads = 0;   /* counts mock reads of the FAT sector (LBA RESERVED_SECTS = 2) */

/* progress-hook capture for test_read_file_progress */
static uint32_t g_pd = 0, g_pt = 0, g_pc = 0;
static void tprog(uint32_t d, uint32_t t) { g_pd = d; g_pt = t; g_pc++; }

#define TEST(name) static void name(void)
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
        fail_count++; \
        return; \
    } \
} while (0)
#define PASS() do { printf("  PASS: %s\n", __func__); pass_count++; } while (0)

/* ---- Little-endian write helpers ---- */
static void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

/* ---- Mock read callback ---- */
static int mock_read(uint64_t lba, uint32_t count, void *buf)
{
    if (lba + count > MOCK_SECTORS)
        return -1;
    if (lba == 2) g_fat_sector_reads++;  /* FAT #1 lives at LBA RESERVED_SECTS (=2); count cache misses */
    memcpy(buf, mock_disk + lba * SECTOR_SIZE, count * SECTOR_SIZE);
    return 0;
}

/*
 * Disk layout (partition starts at LBA 0):
 *
 *   Sector 0:     BPB (boot sector)
 *   Sector 1:     Reserved sector (padding)
 *   Sector 2:     FAT #1 (1 sector)
 *   Sector 3:     FAT #2 (1 sector, copy)
 *   Sector 4-11:  Cluster 2 = root directory (8 sectors/cluster)
 *   Sector 12-19: Cluster 3 = file data
 *   Sector 20-27: Cluster 4 = second file data cluster
 */
#define PART_LBA         0
#define RESERVED_SECTS   2
#define NUM_FATS         2
#define FAT_SIZE_SECTS   1
#define SECTS_PER_CLUST  8
#define ROOT_CLUSTER     2

/* Data area starts after reserved + FAT tables */
#define DATA_START_SECT  (RESERVED_SECTS + NUM_FATS * FAT_SIZE_SECTS) /* = 4 */

static void build_bpb(void)
{
    uint8_t *bpb = mock_disk;  /* sector 0 */
    memset(bpb, 0, SECTOR_SIZE);

    /* Jump boot code (not checked, but realistic) */
    bpb[0] = 0xEB; bpb[1] = 0x58; bpb[2] = 0x90;

    /* BPB fields */
    write_le16(bpb + 11, SECTOR_SIZE);         /* bytes_per_sector */
    bpb[13] = SECTS_PER_CLUST;                 /* sectors_per_cluster */
    write_le16(bpb + 14, RESERVED_SECTS);      /* reserved_sectors */
    bpb[16] = NUM_FATS;                        /* num_fats */
    write_le32(bpb + 36, FAT_SIZE_SECTS);      /* FAT32 fat_size */
    write_le32(bpb + 44, ROOT_CLUSTER);        /* root_cluster */

    /* Boot signature */
    bpb[510] = 0x55;
    bpb[511] = 0xAA;
}

static void build_fat(void)
{
    /* FAT starts at sector 2 */
    uint8_t *fat = mock_disk + RESERVED_SECTS * SECTOR_SIZE;
    memset(fat, 0, SECTOR_SIZE);

    /* Entry 0: media type marker */
    write_le32(fat + 0, 0x0FFFFFF8);
    /* Entry 1: EOC marker */
    write_le32(fat + 4, 0x0FFFFFFF);
    /* Cluster 2 (root dir): end of chain */
    write_le32(fat + 8, 0x0FFFFFFF);
    /* Cluster 3 (file data): end of chain */
    write_le32(fat + 12, 0x0FFFFFFF);
    /* Cluster 4: end of chain (used in multi-cluster test) */
    write_le32(fat + 16, 0x0FFFFFFF);

    /* Copy to FAT #2 */
    memcpy(mock_disk + (RESERVED_SECTS + FAT_SIZE_SECTS) * SECTOR_SIZE,
           fat, SECTOR_SIZE);
}

static void add_dir_entry(uint8_t *dir, int index,
                          const char *name_8_3, uint8_t attr,
                          uint32_t cluster, uint32_t size)
{
    uint8_t *ent = dir + index * 32;
    memcpy(ent, name_8_3, 11);
    ent[11] = attr;
    write_le16(ent + 20, (uint16_t)(cluster >> 16));  /* cluster high */
    write_le16(ent + 26, (uint16_t)(cluster & 0xFFFF)); /* cluster low */
    write_le32(ent + 28, size);
}

static void build_root_dir(void)
{
    /* Root directory at cluster 2, sector DATA_START_SECT */
    uint8_t *dir = mock_disk + DATA_START_SECT * SECTOR_SIZE;
    memset(dir, 0, SECTS_PER_CLUST * SECTOR_SIZE);

    /* Entry 0: normal file "MODEL   GUF" */
    add_dir_entry(dir, 0, "MODEL   GUF", 0x20, 3, 4096);

    /* Entry 1: end marker (0x00 byte at start) is implicit from memset */
}

static void build_file_data(void)
{
    /* File data at cluster 3, sector DATA_START_SECT + SECTS_PER_CLUST */
    uint8_t *data = mock_disk + (DATA_START_SECT + SECTS_PER_CLUST) * SECTOR_SIZE;
    memset(data, 0xAB, SECTS_PER_CLUST * SECTOR_SIZE);

    /* Write known pattern at start */
    memcpy(data, "GGUF", 4);
    data[4] = 0x03;  /* version */
    data[5] = 0x00;
    data[6] = 0x00;
    data[7] = 0x00;
}

static void build_mock_disk(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    build_bpb();
    build_fat();
    build_root_dir();
    build_file_data();
}

/* ================================================================
 * Tests
 * ================================================================ */

TEST(test_init_parses_bpb)
{
    fat32_fs_t fs;
    int err = fat32_init(&fs, mock_read, PART_LBA);
    ASSERT(err == 0, "fat32_init should succeed");
    ASSERT(fs.sectors_per_cluster == SECTS_PER_CLUST, "sectors_per_cluster");
    ASSERT(fs.bytes_per_sector == SECTOR_SIZE, "bytes_per_sector");
    ASSERT(fs.reserved_sectors == RESERVED_SECTS, "reserved_sectors");
    ASSERT(fs.num_fats == NUM_FATS, "num_fats");
    ASSERT(fs.fat_size_sectors == FAT_SIZE_SECTS, "fat_size_sectors");
    ASSERT(fs.root_cluster == ROOT_CLUSTER, "root_cluster");
    ASSERT(fs.fat_lba == PART_LBA + RESERVED_SECTS, "fat_lba");
    ASSERT(fs.data_lba == PART_LBA + RESERVED_SECTS + NUM_FATS * FAT_SIZE_SECTS,
           "data_lba");
    PASS();
}

TEST(test_init_bad_signature)
{
    /* Corrupt the signature */
    mock_disk[510] = 0x00;
    mock_disk[511] = 0x00;

    fat32_fs_t fs;
    int err = fat32_init(&fs, mock_read, PART_LBA);
    ASSERT(err != 0, "fat32_init should fail on bad signature");

    /* Restore */
    mock_disk[510] = 0x55;
    mock_disk[511] = 0xAA;
    PASS();
}

TEST(test_find_file_found)
{
    fat32_fs_t fs;
    fat32_init(&fs, mock_read, PART_LBA);

    uint32_t cluster, size;
    int err = fat32_find_file(&fs, "MODEL   GUF", &cluster, &size);
    ASSERT(err == 0, "should find MODEL   GUF");
    ASSERT(cluster == 3, "first cluster should be 3");
    ASSERT(size == 4096, "file size should be 4096");
    PASS();
}

TEST(test_find_file_not_found)
{
    fat32_fs_t fs;
    fat32_init(&fs, mock_read, PART_LBA);

    uint32_t cluster, size;
    int err = fat32_find_file(&fs, "NOTHERE TXT", &cluster, &size);
    ASSERT(err == -1, "should not find NOTHERE TXT");
    PASS();
}

TEST(test_find_file_skips_deleted)
{
    /* Insert a deleted entry before the real one */
    uint8_t *dir = mock_disk + DATA_START_SECT * SECTOR_SIZE;

    /* Shift entry 0 to entry 1 */
    memcpy(dir + 32, dir, 32);

    /* Entry 0: deleted */
    memset(dir, 0, 32);
    dir[0] = 0xE5;
    memcpy(dir + 1, "DELETED XX", 10);
    dir[11] = 0x20;

    fat32_fs_t fs;
    fat32_init(&fs, mock_read, PART_LBA);

    uint32_t cluster, size;
    int err = fat32_find_file(&fs, "MODEL   GUF", &cluster, &size);
    ASSERT(err == 0, "should find file after deleted entry");
    ASSERT(cluster == 3, "cluster should be 3");

    /* Restore original layout */
    build_root_dir();
    PASS();
}

TEST(test_find_file_skips_lfn)
{
    /* Insert a LFN entry before the real one */
    uint8_t *dir = mock_disk + DATA_START_SECT * SECTOR_SIZE;

    /* Shift entry 0 to entry 1 */
    memcpy(dir + 32, dir, 32);

    /* Entry 0: LFN (attr = 0x0F) */
    memset(dir, 0x41, 32);  /* LFN ordinal byte */
    dir[11] = 0x0F;         /* LFN attribute */

    fat32_fs_t fs;
    fat32_init(&fs, mock_read, PART_LBA);

    uint32_t cluster, size;
    int err = fat32_find_file(&fs, "MODEL   GUF", &cluster, &size);
    ASSERT(err == 0, "should find file after LFN entry");
    ASSERT(cluster == 3, "cluster should be 3");

    /* Restore original layout */
    build_root_dir();
    PASS();
}

TEST(test_read_file_data)
{
    fat32_fs_t fs;
    fat32_init(&fs, mock_read, PART_LBA);

    uint8_t buf[4096];
    memset(buf, 0, sizeof(buf));
    int err = fat32_read_file(&fs, 3, 4096, buf);
    ASSERT(err == 0, "fat32_read_file should succeed");
    ASSERT(memcmp(buf, "GGUF", 4) == 0, "first 4 bytes should be GGUF");
    ASSERT(buf[4] == 0x03, "version byte should be 0x03");
    /* Bytes 8+ should be 0xAB (fill pattern) */
    ASSERT(buf[8] == 0xAB, "fill pattern at byte 8");
    ASSERT(buf[4095] == 0xAB, "fill pattern at byte 4095");
    PASS();
}

TEST(test_read_file_multi_cluster)
{
    /* Set up cluster 3 -> cluster 4 -> EOC in FAT */
    uint8_t *fat = mock_disk + RESERVED_SECTS * SECTOR_SIZE;
    write_le32(fat + 12, 4);           /* cluster 3 -> cluster 4 */
    write_le32(fat + 16, 0x0FFFFFFF);  /* cluster 4 -> EOC */
    /* Copy to FAT #2 */
    memcpy(mock_disk + (RESERVED_SECTS + FAT_SIZE_SECTS) * SECTOR_SIZE,
           fat, SECTOR_SIZE);

    /* Write pattern in cluster 4 data */
    uint8_t *cluster4_data = mock_disk +
        (DATA_START_SECT + 2 * SECTS_PER_CLUST) * SECTOR_SIZE;
    memset(cluster4_data, 0xCD, SECTS_PER_CLUST * SECTOR_SIZE);

    /* Update root dir: file size = 2 clusters worth = 8192 bytes */
    uint8_t *dir = mock_disk + DATA_START_SECT * SECTOR_SIZE;
    write_le32(dir + 28, 8192);

    fat32_fs_t fs;
    fat32_init(&fs, mock_read, PART_LBA);

    uint8_t buf[8192];
    memset(buf, 0, sizeof(buf));
    int err = fat32_read_file(&fs, 3, 8192, buf);
    ASSERT(err == 0, "multi-cluster read should succeed");

    /* First cluster: starts with GGUF pattern */
    ASSERT(memcmp(buf, "GGUF", 4) == 0, "cluster 3 should have GGUF");
    /* Remaining bytes of cluster 3 are 0xAB */
    ASSERT(buf[8] == 0xAB, "cluster 3 fill at byte 8");

    /* Second cluster: all 0xCD */
    ASSERT(buf[4096] == 0xCD, "cluster 4 start should be 0xCD");
    ASSERT(buf[8191] == 0xCD, "cluster 4 end should be 0xCD");

    /* Restore: cluster 3 -> EOC, file size back to 4096 */
    write_le32(fat + 12, 0x0FFFFFFF);
    memcpy(mock_disk + (RESERVED_SECTS + FAT_SIZE_SECTS) * SECTOR_SIZE,
           fat, SECTOR_SIZE);
    write_le32(dir + 28, 4096);
    PASS();
}

TEST(test_fat_sector_cached)
{
    /* cluster 3 -> cluster 4 -> EOC (same shape as the multi-cluster test) */
    uint8_t *fat = mock_disk + RESERVED_SECTS * SECTOR_SIZE;
    write_le32(fat + 12, 4);           /* cluster 3 -> cluster 4 */
    write_le32(fat + 16, 0x0FFFFFFF);  /* cluster 4 -> EOC */
    memcpy(mock_disk + (RESERVED_SECTS + FAT_SIZE_SECTS) * SECTOR_SIZE,
           fat, SECTOR_SIZE);

    uint8_t *cluster4_data = mock_disk +
        (DATA_START_SECT + 2 * SECTS_PER_CLUST) * SECTOR_SIZE;
    memset(cluster4_data, 0xCD, SECTS_PER_CLUST * SECTOR_SIZE);

    fat32_fs_t fs;
    fat32_init(&fs, mock_read, PART_LBA);

    uint8_t buf[8192];
    memset(buf, 0, sizeof(buf));
    g_fat_sector_reads = 0;
    int err = fat32_read_file(&fs, 3, 8192, buf);
    ASSERT(err == 0, "cached multi-cluster read should succeed");
    /* Cluster 3 and 4 FAT entries share one FAT sector, so the cache serves the
     * second lookup -> exactly 1 FAT-sector read (would be 2 without the cache). */
    ASSERT(g_fat_sector_reads == 1, "FAT sector read once - cached across the 3->4 chain");
    /* Cache must not corrupt the chain: data is still correct. */
    ASSERT(memcmp(buf, "GGUF", 4) == 0, "cluster 3 data intact");
    ASSERT(buf[4096] == 0xCD, "cluster 4 data intact via cached chain");

    /* Restore: cluster 3 -> EOC */
    write_le32(fat + 12, 0x0FFFFFFF);
    memcpy(mock_disk + (RESERVED_SECTS + FAT_SIZE_SECTS) * SECTOR_SIZE,
           fat, SECTOR_SIZE);
    PASS();
}

TEST(test_read_file_progress)
{
    /* cluster 3 -> cluster 4 -> EOC, file = 2 clusters = 8192 bytes */
    uint8_t *fat = mock_disk + RESERVED_SECTS * SECTOR_SIZE;
    write_le32(fat + 12, 4);
    write_le32(fat + 16, 0x0FFFFFFF);
    memcpy(mock_disk + (RESERVED_SECTS + FAT_SIZE_SECTS) * SECTOR_SIZE,
           fat, SECTOR_SIZE);

    uint8_t *cluster4_data = mock_disk +
        (DATA_START_SECT + 2 * SECTS_PER_CLUST) * SECTOR_SIZE;
    memset(cluster4_data, 0xCD, SECTS_PER_CLUST * SECTOR_SIZE);

    fat32_fs_t fs;
    fat32_init(&fs, mock_read, PART_LBA);
    fs.progress = tprog;

    uint8_t buf[8192];
    memset(buf, 0, sizeof(buf));
    g_pd = g_pt = g_pc = 0;
    int err = fat32_read_file(&fs, 3, 8192, buf);
    ASSERT(err == 0, "progress read should succeed");
    ASSERT(g_pc >= 2, "progress called per cluster (>=2)");
    ASSERT(g_pt == 8192, "progress total == file_size");
    ASSERT(g_pd == 8192, "progress reaches 100%: done == file_size");

    /* Restore: cluster 3 -> EOC */
    write_le32(fat + 12, 0x0FFFFFFF);
    memcpy(mock_disk + (RESERVED_SECTS + FAT_SIZE_SECTS) * SECTOR_SIZE,
           fat, SECTOR_SIZE);
    PASS();
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== FAT32 Read-Only Parser Tests ===\n");

    build_mock_disk();

    test_init_parses_bpb();
    test_init_bad_signature();
    test_find_file_found();
    test_find_file_not_found();
    test_find_file_skips_deleted();
    test_find_file_skips_lfn();
    test_read_file_data();
    test_read_file_multi_cluster();
    test_fat_sector_cached();
    test_read_file_progress();

    printf("\nResults: %d PASS, %d FAIL (of %d)\n",
           pass_count, fail_count, pass_count + fail_count);
    return fail_count ? 1 : 0;
}
