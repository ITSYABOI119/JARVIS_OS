#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>

/* Callback: read sectors from block device.
 * lba = absolute sector on disk. count = number of sectors. buf = output.
 * Returns 0 on success, negative on error. */
typedef int (*fat32_read_fn)(uint64_t lba, uint32_t count, void *buf);

typedef struct {
    fat32_read_fn read;
    uint64_t      part_lba;          /* Partition start LBA on disk */
    uint32_t      sectors_per_cluster;
    uint32_t      bytes_per_sector;
    uint32_t      reserved_sectors;
    uint32_t      fat_size_sectors;
    uint8_t       num_fats;
    uint32_t      root_cluster;
    uint64_t      data_lba;          /* Absolute LBA of cluster 2 */
    uint64_t      fat_lba;           /* Absolute LBA of first FAT */
} fat32_fs_t;

/* Initialize from partition at given LBA. Reads BPB (sector 0 of partition). */
int fat32_init(fat32_fs_t *fs, fat32_read_fn read, uint64_t part_lba);

/* Find file in root directory by 8.3 name (e.g. "MODEL   GUF").
 * Returns 0 if found, -1 if not found. */
int fat32_find_file(fat32_fs_t *fs, const char *name_8_3,
                    uint32_t *first_cluster, uint32_t *file_size);

/* Read entire file by following cluster chain.
 * buf must be large enough for file_size bytes.
 * Returns 0 on success, negative on error. */
int fat32_read_file(fat32_fs_t *fs, uint32_t first_cluster,
                    uint32_t file_size, void *buf);

#endif
