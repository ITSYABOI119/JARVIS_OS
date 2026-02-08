/*
 * JARVIS AI-OS - BCM2711 PM Watchdog Driver
 *
 * Week 44: Hardware watchdog timer using the BCM2711 Power Management block.
 * PM registers at paddr 0xFE100000, mapped via uart_device_map_page()
 * with explicit vaddr 0x611000 (before UART init, ascending paddr order).
 *
 * The PM watchdog is a 20-bit down-counter clocked at ~62.5 kHz (16 MHz / 256).
 * When it reaches zero, the PM block triggers a full system reset.
 * All PM register writes require the password 0x5A000000 in bits [31:24].
 *
 * Reference: BCM2835 ARM Peripherals datasheet, Section 14 (PM)
 *            (BCM2711 uses the same PM block)
 */

#ifndef BCM_WATCHDOG_H
#define BCM_WATCHDOG_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * PM Register Offsets (within 4KB page at 0xFE100000)
 * ================================================================ */
#define PM_BASE_PADDR       0xFE100000
#define PM_VADDR            0x611000

#define PM_RSTC             0x1C    /* Reset Control */
#define PM_RSTS             0x20    /* Reset Status */
#define PM_WDOG             0x24    /* Watchdog timer */

/* PM password — must be OR'd into bits [31:24] of every write */
#define PM_PASSWORD         0x5A000000

/* PM_RSTC bits */
#define PM_RSTC_WRCFG_CLR  0xFFFFFFCF  /* Mask to clear WRCFG bits */
#define PM_RSTC_WRCFG_FULL 0x00000020  /* Full reset on watchdog timeout */

/* PM_WDOG bits */
#define PM_WDOG_TIME_MASK   0x000FFFFF  /* 20-bit counter value */

/* Watchdog clock: 16 MHz / 256 = 62500 Hz (ticks per second) */
#define PM_WDOG_TICKS_PER_SEC  62500

/* Maximum timeout: 2^20 / 62500 = ~16.7 seconds */
#define PM_WDOG_MAX_SECS    16

/* ================================================================
 * Public API
 * ================================================================ */

/*
 * Initialize watchdog driver.
 * Maps PM register page at 0xFE100000 via uart_device_map_page().
 * MUST be called BEFORE uart_init() (ascending paddr order).
 * Returns 0 on success, -1 on failure.
 */
int watchdog_init(void);

/*
 * Start the watchdog timer with the given timeout in seconds (1-16).
 * After start, watchdog_feed() must be called periodically or the
 * system will reset.
 */
void watchdog_start(uint32_t secs);

/*
 * Feed (pet) the watchdog — reloads the counter.
 * Must be called more frequently than the timeout period.
 */
void watchdog_feed(void);

/*
 * Stop the watchdog timer.
 */
void watchdog_stop(void);

/*
 * Force immediate system reboot via watchdog.
 * Sets a very short timeout (~100ms) to trigger reset.
 */
void watchdog_reboot(void);

/*
 * Check if watchdog timer is currently running.
 */
bool watchdog_is_running(void);

/*
 * Get watchdog status string for shell commands.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL).
 */
int watchdog_get_status(char *output, uint32_t output_size);

#endif /* BCM_WATCHDOG_H */
