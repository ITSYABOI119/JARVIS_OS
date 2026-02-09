/*
 * JARVIS AI-OS - Warm Reboot Support
 *
 * Week 46: Warm/cold boot detection via SD card state persistence.
 * Saves boot state to a reserved SD sector before reboot.
 * On next boot, detects warm vs cold and skips re-probing if warm.
 *
 * BCM2711 RAM is reinitialized by GPU firmware on every reboot,
 * so we persist a 512-byte state block to a reserved SD card sector.
 */

#ifndef WARM_REBOOT_H
#define WARM_REBOOT_H

#include <stdint.h>
#include <stdbool.h>

/* Magic and version for state validation */
#define WARM_BOOT_MAGIC     0x4A525753  /* "JRWS" */
#define WARM_BOOT_VERSION   1

/* Reserved SD sector for warm boot state (safely before EMMC write test
 * area at 30374912, and well beyond any normal filesystem data) */
#define WARM_BOOT_LBA       30374000

/* ================================================================
 * Warm boot state -- exactly 512 bytes = 1 SD sector
 * ================================================================ */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /* WARM_BOOT_MAGIC if valid           [0..3]   */
    uint32_t version;         /* WARM_BOOT_VERSION                  [4..7]   */
    uint32_t boot_count;      /* Incremented each warm boot         [8..11]  */
    uint32_t cache_entries;   /* Cache entries at shutdown           [12..15] */
    uint32_t last_uptime_ms;  /* Uptime at shutdown (milliseconds)  [16..19] */
    uint32_t flags;           /* bit 0: warm boot requested         [20..23] */
    uint32_t driver_mask;     /* Bitmask of initialized drivers     [24..27] */
    uint32_t reserved1[8];    /* Future use, zero                   [28..59] */
    uint32_t checksum;        /* XOR checksum of first 60 bytes     [60..63] */
    uint8_t  pad[448];        /* Pad to 512 bytes total             [64..511]*/
} warm_boot_state_t;

_Static_assert(sizeof(warm_boot_state_t) == 512, "warm_boot_state_t must be 512 bytes");

/* ================================================================
 * Public API
 * ================================================================ */

/*
 * Initialize warm reboot subsystem.
 * Reads the warm boot sector from SD and determines boot type.
 * Requires EMMC driver to be initialized first.
 * Returns 0 on success, -1 on failure (EMMC not ready).
 */
int warm_reboot_init(void);

/*
 * Check if this is a warm boot (previous state was saved and valid).
 */
bool warm_reboot_is_warm(void);

/*
 * Get the current boot count (1 for first cold boot, increments on warm boots).
 */
uint32_t warm_reboot_boot_count(void);

/*
 * Save current system state to SD for warm reboot recovery.
 * Sets the "warm boot requested" flag so the next boot is detected as warm.
 * Returns 0 on success, -1 on failure.
 */
int warm_reboot_save_state(uint32_t cache_entries, uint32_t uptime_ms,
                           uint32_t driver_mask);

/*
 * Clear the warm boot state on SD (zero the sector).
 * Use before a clean shutdown to ensure next boot is cold.
 */
void warm_reboot_clear_state(void);

/*
 * Trigger a reboot via the PM watchdog.
 * If warm=true, saves state first so next boot detects warm.
 * If warm=false, clears state for a clean cold boot.
 */
void warm_reboot_trigger(bool warm);

/*
 * Get warm reboot status string for shell commands.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL).
 */
int warm_reboot_get_status(char *output, uint32_t output_size);

#endif /* WARM_REBOOT_H */
