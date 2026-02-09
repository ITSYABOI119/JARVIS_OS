/*
 * JARVIS AI-OS - BCM2711 Power Management
 *
 * Week 46: WFI-based idle power saving and ARM clock frequency
 * scaling via VideoCore mailbox. Uses the same mailbox interface
 * as bcm_thermal.c (shared MMIO page at 0xFE00B000).
 *
 * On ARM Cortex-A72, WFI halts the core until an interrupt fires.
 * Frequency scaling uses mailbox tags GET_CLOCK_RATE / SET_CLOCK_RATE
 * for the ARM clock (clock ID 3).
 *
 * Reference: https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 */

#ifndef BCM_POWER_H
#define BCM_POWER_H

#include <stdint.h>
#include <stdbool.h>

/* ARM clock ID for mailbox property tags */
#define CLOCK_ID_ARM        3

/* Power profiles */
typedef enum {
    POWER_PROFILE_LOW  = 0,    /* 600 MHz - idle / sleep */
    POWER_PROFILE_MED  = 1,    /* 1000 MHz - light workload */
    POWER_PROFILE_HIGH = 2,    /* 1500 MHz - default */
    POWER_PROFILE_MAX  = 3,    /* 1800 MHz - full performance (Cortex-A72 max) */
    POWER_PROFILE_COUNT
} power_profile_t;

/* ================================================================
 * Public API
 * ================================================================ */

/*
 * Initialize power management.
 * Requires thermal_init() to have been called first (shares mailbox).
 * Reads current ARM clock rate.
 * Returns 0 on success, -1 on failure.
 */
int power_init(void);

/*
 * Check if power management is initialized.
 */
bool power_is_initialized(void);

/*
 * Execute WFI (Wait For Interrupt) to idle the current core.
 * Returns when any interrupt fires. Safe to call in the main loop
 * when no work is pending.
 */
void power_wfi(void);

/*
 * Get current ARM clock frequency in Hz.
 * Returns 0 on error.
 */
uint32_t power_get_clock_hz(void);

/*
 * Set ARM clock frequency in Hz.
 * Returns actual frequency set (may differ from request), or 0 on error.
 */
uint32_t power_set_clock_hz(uint32_t hz);

/*
 * Apply a named power profile (adjusts clock frequency).
 * Returns 0 on success, -1 on error.
 */
int power_set_profile(power_profile_t profile);

/*
 * Get the current power profile (based on clock frequency).
 */
power_profile_t power_get_profile(void);

/*
 * Get power status string for shell commands.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL).
 */
int power_get_status(char *output, uint32_t output_size);

#endif /* BCM_POWER_H */
