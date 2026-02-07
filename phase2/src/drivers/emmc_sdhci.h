/*
 * JARVIS AI-OS - BCM2711 EMMC2 (SDHCI) Driver
 *
 * SD card bring-up, block read/write via PIO.
 * Week 35: probe + identify + read
 * Week 36: write + throughput + test suite
 */

#ifndef EMMC_SDHCI_H
#define EMMC_SDHCI_H

#include <stdint.h>
#include <stdbool.h>

/* BCM2711 EMMC2 / SDHCI base */
#define EMMC2_BASE       0xFE340000u

/* 0 means auto-allocate from the device mapping region */
#define EMMC2_MMIO_VADDR 0u

/* Compile-time flag to enable destructive write tests (default: ON for Week 36 validation) */
#ifndef EMMC_WRITE_TEST_ENABLE
#define EMMC_WRITE_TEST_ENABLE 1
#endif

/* Compile-time flag to enable ADMA2 DMA for reads (default: OFF until validated) */
#ifndef EMMC_USE_ADMA2
#define EMMC_USE_ADMA2 1  /* TESTING: Enable ADMA2 */
#endif

/* Fallback LBA for write tests (only used if capacity query fails).
 * The test suite computes the actual LBA dynamically from card capacity:
 *   test_lba = total_sectors - 1024
 * This works on any card size (16GB, 32GB, 64GB, etc.). */
#ifndef EMMC_WRITE_TEST_LBA
#define EMMC_WRITE_TEST_LBA 30500000u
#endif

void emmc_set_mmio_base(volatile uint32_t *base);
bool emmc_is_mapped(void);
bool emmc_map_device_frame(void);
bool emmc_init(void);

/* Card identification - call after emmc_init() */
void emmc_get_cid(uint32_t cid_out[4]);
uint64_t emmc_get_capacity_bytes(void);  /* Returns 0 if unknown */

/* Read operations */
bool emmc_read_block(uint32_t lba, uint8_t *out_512);
bool emmc_read_blocks(uint32_t lba, uint32_t count, uint8_t *out_buf);

/* Write operations - USE WITH CAUTION!
 * Writing to wrong LBAs can corrupt filesystem.
 * Only use with known-safe LBAs (e.g., beyond partition end). */
bool emmc_write_block(uint32_t lba, const uint8_t *data_512);
bool emmc_write_blocks(uint32_t lba, uint32_t count, const uint8_t *data);

#if EMMC_USE_ADMA2
/* ADMA2 DMA support for high-throughput reads */
bool emmc_adma2_init(void);
bool emmc_adma2_is_enabled(void);
uint8_t *emmc_adma2_get_buffer(void);
bool emmc_read_blocks_adma2(uint32_t lba, uint32_t count, uint8_t *out_buf);
#define EMMC_ADMA2_BUF_SIZE (256 * 512)  /* 128KB DMA buffer */
#endif

#endif
