/*
 * JARVIS AI-OS - BCM2711 Power Management Implementation
 *
 * Week 46: WFI-based idle power saving and ARM clock frequency
 * scaling via VideoCore mailbox property interface.
 *
 * WFI: On Cortex-A72, the WFI instruction halts execution until
 * an interrupt arrives (timer, UART, etc.), saving power in idle.
 *
 * Clock scaling: Uses thermal_mailbox_tag() for mailbox property calls.
 * Tags GET_CLOCK_RATE (0x00030002) and SET_CLOCK_RATE (0x00038002)
 * for clock ID 3 (ARM). The GPU firmware handles PLLB reconfiguration.
 *
 * Shares the mailbox MMIO page with bcm_thermal.c (already mapped
 * at vaddr 0x610000). Requires thermal_init() to be called first.
 */

#include "bcm_power.h"
#include "bcm_thermal.h"
#include "bcm2711_timer.h"

#include <sel4/sel4.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Debug Output
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

static bool power_initialized = false;
static uint32_t boot_freq_hz = 0;
static uint32_t current_clock_hz = 0;
static uint32_t wfi_count = 0;

/* Profile frequency targets (Hz) */
static const uint32_t profile_hz[POWER_PROFILE_COUNT] = {
    600000000,    /* LOW  - 600 MHz */
    1000000000,   /* MED  - 1000 MHz */
    1500000000,   /* HIGH - 1500 MHz */
    1800000000,   /* MAX  - 1800 MHz */
};

static const char *profile_names[POWER_PROFILE_COUNT] = {
    "LOW (600 MHz)",
    "MED (1000 MHz)",
    "HIGH (1500 MHz)",
    "MAX (1800 MHz)",
};

/* ================================================================
 * Public API
 * ================================================================ */

int power_init(void)
{
    if (power_initialized) return 0;

    if (!thermal_is_initialized()) {
        debug_puts("[POWER] ERROR: thermal/mailbox not initialized\n");
        return -1;
    }

    /* Read current ARM clock rate via thermal_mailbox_tag */
    uint32_t buf[2];
    buf[0] = CLOCK_ID_ARM;  /* clock ID */
    buf[1] = 0;             /* rate (output) */

    if (thermal_mailbox_tag(TAG_GET_CLOCK_RATE, buf, 2, 2) == 0) {
        current_clock_hz = buf[1];
        boot_freq_hz = buf[1];
        char msg[80];
        snprintf(msg, sizeof(msg), "[POWER] ARM clock: %u MHz\n",
                 current_clock_hz / 1000000);
        debug_puts(msg);
    } else {
        debug_puts("[POWER] WARNING: Could not read ARM clock rate\n");
        current_clock_hz = 1500000000;  /* Assume default 1.5 GHz */
        boot_freq_hz = current_clock_hz;
    }

    power_initialized = true;
    wfi_count = 0;
    debug_puts("[POWER] Power management initialized\n");
    return 0;
}

bool power_is_initialized(void)
{
    return power_initialized;
}

void power_wfi(void)
{
    __asm__ volatile("wfi" ::: "memory");
    wfi_count++;
}

uint32_t power_get_clock_hz(void)
{
    if (!power_initialized) return 0;

    uint32_t buf[2];
    buf[0] = CLOCK_ID_ARM;
    buf[1] = 0;

    if (thermal_mailbox_tag(TAG_GET_CLOCK_RATE, buf, 2, 2) == 0) {
        current_clock_hz = buf[1];
    }
    return current_clock_hz;
}

uint32_t power_set_clock_hz(uint32_t hz)
{
    if (!power_initialized) return 0;

    uint32_t buf[3];
    buf[0] = CLOCK_ID_ARM;
    buf[1] = hz;
    buf[2] = 0;  /* skip turbo = 0 */

    if (thermal_mailbox_tag(TAG_SET_CLOCK_RATE, buf, 3, 2) == 0) {
        current_clock_hz = buf[1];
        char msg[80];
        snprintf(msg, sizeof(msg), "[POWER] Clock set to %u MHz\n",
                 current_clock_hz / 1000000);
        debug_puts(msg);
        return current_clock_hz;
    }

    debug_puts("[POWER] ERROR: Failed to set clock\n");
    return 0;
}

int power_set_profile(power_profile_t profile)
{
    if (profile >= POWER_PROFILE_COUNT) return -1;

    uint32_t target = profile_hz[profile];
    uint32_t actual = power_set_clock_hz(target);

    if (actual > 0) return 0;
    return -1;
}

power_profile_t power_get_profile(void)
{
    uint32_t hz = power_get_clock_hz();

    if (hz <= 700000000)  return POWER_PROFILE_LOW;
    if (hz <= 1200000000) return POWER_PROFILE_MED;
    if (hz <= 1600000000) return POWER_PROFILE_HIGH;
    return POWER_PROFILE_MAX;
}

int power_get_status(char *output, uint32_t output_size)
{
    if (!output || output_size == 0) return 0;

    if (!power_initialized) {
        return snprintf(output, output_size, "Power: NOT INITIALIZED\n");
    }

    int n = 0;
    uint32_t hz = power_get_clock_hz();
    power_profile_t prof = power_get_profile();

    n += snprintf(output + n, output_size - n,
                  "Power: %s\n",
                  (prof < POWER_PROFILE_COUNT) ? profile_names[prof] : "unknown");
    n += snprintf(output + n, output_size - n,
                  "  ARM freq: %u MHz\n", hz / 1000000);
    n += snprintf(output + n, output_size - n,
                  "  Boot freq: %u MHz\n", boot_freq_hz / 1000000);
    n += snprintf(output + n, output_size - n,
                  "  WFI count: %u\n", wfi_count);

    return n;
}
