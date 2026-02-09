/*
 * JARVIS AI-OS - Warm Reboot Support
 *
 * Week 46: Warm/cold boot detection via SD card state persistence.
 * Saves boot state to a reserved SD sector before reboot.
 * On next boot, detects warm vs cold and skips re-probing if warm.
 *
 * BCM2711 RAM is reinitialized by GPU firmware on every reboot,
 * so we persist a 512-byte state block to a reserved SD card sector
 * (LBA 30374000, safely before the EMMC write test area at 30374912).
 */

#include "warm_reboot.h"
#include "emmc_sdhci.h"
#include "bcm_watchdog.h"
#include "bcm2711_timer.h"

#include <sel4/sel4.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Debug Output (uses seL4_DebugPutChar)
 * ================================================================ */

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

/* ================================================================
 * Module State
 * ================================================================ */

static warm_boot_state_t current_state;
static bool is_warm = false;
static bool init_done = false;

/* ================================================================
 * Checksum: XOR of first 15 uint32_t words (60 bytes)
 * The checksum field itself is at word[15] and is NOT included.
 * ================================================================ */

static uint32_t compute_checksum(const warm_boot_state_t *s)
{
    /* Use memcpy to avoid packed-struct pointer alignment warning (GCC 13 -Werror) */
    const uint8_t *raw = (const uint8_t *)s;
    uint32_t xor_val = 0;
    for (int i = 0; i < 15; i++) {
        uint32_t w;
        memcpy(&w, raw + i * 4, sizeof(w));
        xor_val ^= w;
    }
    return xor_val;
}

/* ================================================================
 * warm_reboot_init
 *
 * Read the warm boot sector from SD and determine boot type.
 * Must be called after EMMC is initialized.
 * ================================================================ */

int warm_reboot_init(void)
{
    char msg[80];

    if (!emmc_is_mapped()) {
        debug_puts("[WARM] ERROR: EMMC not mapped, cannot init warm reboot\n");
        return -1;
    }

    /* Read the warm boot sector */
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));

    if (!emmc_read_block(WARM_BOOT_LBA, buf)) {
        debug_puts("[WARM] ERROR: Failed to read warm boot sector\n");
        return -1;
    }

    warm_boot_state_t *state = (warm_boot_state_t *)buf;

    /* Validate magic, version, and checksum */
    if (state->magic == WARM_BOOT_MAGIC &&
        state->version == WARM_BOOT_VERSION &&
        state->checksum == compute_checksum(state)) {

        /* Valid state found -- check if warm boot was requested */
        if (state->flags & 1) {
            /* Warm boot detected */
            memcpy(&current_state, state, sizeof(current_state));
            current_state.boot_count++;
            current_state.flags &= ~1u;  /* Clear warm boot request flag */
            current_state.checksum = compute_checksum(&current_state);

            /* Write updated state back to SD */
            if (!emmc_write_block(WARM_BOOT_LBA, (const uint8_t *)&current_state)) {
                debug_puts("[WARM] WARNING: Failed to write updated warm state\n");
            }

            is_warm = true;
            snprintf(msg, sizeof(msg), "[WARM] Warm boot #%u detected\n",
                     (unsigned)current_state.boot_count);
            debug_puts(msg);
        } else {
            /* Valid state but warm boot not requested -- treat as cold */
            memcpy(&current_state, state, sizeof(current_state));
            current_state.boot_count++;
            current_state.checksum = compute_checksum(&current_state);
            is_warm = false;
            debug_puts("[WARM] Cold boot (warm flag not set)\n");
        }
    } else {
        /* No valid state -- first boot or corrupted */
        memset(&current_state, 0, sizeof(current_state));
        current_state.magic = WARM_BOOT_MAGIC;
        current_state.version = WARM_BOOT_VERSION;
        current_state.boot_count = 1;
        current_state.checksum = compute_checksum(&current_state);
        is_warm = false;
        debug_puts("[WARM] Cold boot (no valid warm state)\n");
    }

    init_done = true;
    return 0;
}

/* ================================================================
 * warm_reboot_is_warm
 * ================================================================ */

bool warm_reboot_is_warm(void)
{
    return is_warm;
}

/* ================================================================
 * warm_reboot_boot_count
 * ================================================================ */

uint32_t warm_reboot_boot_count(void)
{
    return current_state.boot_count;
}

/* ================================================================
 * warm_reboot_save_state
 *
 * Save current system state to SD with the warm-boot-requested flag.
 * ================================================================ */

int warm_reboot_save_state(uint32_t cache_entries, uint32_t uptime_ms,
                           uint32_t driver_mask)
{
    if (!init_done) {
        debug_puts("[WARM] ERROR: Not initialized\n");
        return -1;
    }

    current_state.magic = WARM_BOOT_MAGIC;
    current_state.version = WARM_BOOT_VERSION;
    current_state.cache_entries = cache_entries;
    current_state.last_uptime_ms = uptime_ms;
    current_state.flags |= 1u;   /* Set warm boot requested flag */
    current_state.driver_mask = driver_mask;
    current_state.checksum = compute_checksum(&current_state);

    if (!emmc_write_block(WARM_BOOT_LBA, (const uint8_t *)&current_state)) {
        debug_puts("[WARM] ERROR: Failed to save state to SD\n");
        return -1;
    }

    debug_puts("[WARM] State saved to SD\n");
    return 0;
}

/* ================================================================
 * warm_reboot_clear_state
 *
 * Zero the warm boot sector on SD for a clean cold boot.
 * ================================================================ */

void warm_reboot_clear_state(void)
{
    uint8_t zeros[512];
    memset(zeros, 0, sizeof(zeros));

    if (!emmc_write_block(WARM_BOOT_LBA, zeros)) {
        debug_puts("[WARM] WARNING: Failed to clear warm state on SD\n");
    } else {
        debug_puts("[WARM] State cleared\n");
    }
}

/* ================================================================
 * warm_reboot_trigger
 *
 * Trigger a reboot via the PM watchdog.
 * warm=true:  save state first, so next boot is detected as warm.
 * warm=false: clear state, ensuring a clean cold boot.
 * ================================================================ */

void warm_reboot_trigger(bool warm)
{
    if (warm) {
        /* Save current state with warm flag before rebooting */
        uint32_t uptime_ms = 0;
        if (systimer_is_initialized()) {
            uptime_ms = (uint32_t)(systimer_read() / 1000);
        }
        warm_reboot_save_state(current_state.cache_entries, uptime_ms,
                               current_state.driver_mask);
        debug_puts("[WARM] Triggering warm reboot...\n");
    } else {
        warm_reboot_clear_state();
        debug_puts("[WARM] Triggering cold reboot...\n");
    }

    watchdog_reboot();
    /* Does not return */
}

/* ================================================================
 * warm_reboot_get_status
 *
 * Format warm reboot status into output buffer for shell display.
 * ================================================================ */

int warm_reboot_get_status(char *output, uint32_t output_size)
{
    if (!init_done) {
        return snprintf(output, output_size, "Warm reboot: not initialized\n");
    }

    int n = 0;
    n += snprintf(output + n, output_size - n,
                  "Boot type:  %s\n", is_warm ? "WARM" : "COLD");
    n += snprintf(output + n, output_size - n,
                  "Boot count: %u\n", (unsigned)current_state.boot_count);

    if (is_warm) {
        n += snprintf(output + n, output_size - n,
                      "Last uptime: %u ms\n",
                      (unsigned)current_state.last_uptime_ms);
        n += snprintf(output + n, output_size - n,
                      "Last cache entries: %u\n",
                      (unsigned)current_state.cache_entries);
        n += snprintf(output + n, output_size - n,
                      "Driver mask: 0x%08x\n",
                      (unsigned)current_state.driver_mask);
    }

    n += snprintf(output + n, output_size - n,
                  "State LBA: %u\n", (unsigned)WARM_BOOT_LBA);

    return n;
}
