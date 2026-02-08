/*
 * JARVIS AI-OS - BCM2711 PM Watchdog Driver Implementation
 *
 * Week 44: Hardware watchdog timer using BCM2711 Power Management block.
 *
 * PM registers at paddr 0xFE100000, mapped to explicit vaddr 0x611000.
 * Mapped BEFORE uart_init() in the boot sequence (ascending paddr order
 * required by seL4 forward-only device untyped watermark).
 *
 * The PM watchdog is a 20-bit down-counter at ~62.5 kHz.
 * When it reaches zero with WRCFG_FULL set, the system resets.
 */

#include "bcm_watchdog.h"
#include "bcm2711_timer.h"
#include "uart_pl011.h"

#include <sel4/sel4.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Debug Output (pre-UART: uses seL4_DebugPutChar)
 * ================================================================ */

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

static void debug_hex(uint32_t val)
{
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        seL4_DebugPutChar(hex[(val >> (i * 4)) & 0xF]);
    }
}

/* ================================================================
 * Driver State
 * ================================================================ */

static volatile uint32_t *pm_base = NULL;
static bool wdog_initialized = false;
static bool wdog_running = false;
static uint32_t wdog_timeout_secs = 0;
static uint64_t last_feed_us = 0;
static uint32_t wdog_timeout_ticks = 0;

/* Register read/write helpers */
static inline uint32_t pm_read(uint32_t offset)
{
    return pm_base[offset / 4];
}

static inline void pm_write(uint32_t offset, uint32_t val)
{
    pm_base[offset / 4] = PM_PASSWORD | val;
}

/* ================================================================
 * Public API
 * ================================================================ */

int watchdog_init(void)
{
    if (wdog_initialized) return 0;

    debug_puts("[WDOG] Mapping PM at paddr ");
    debug_hex(PM_BASE_PADDR);
    debug_puts(" -> vaddr ");
    debug_hex(PM_VADDR);
    debug_puts("\n");

    if (!uart_device_map_ready()) {
        debug_puts("[WDOG] ERROR: device mapper not ready\n");
        return -1;
    }

    if (!uart_device_map_page(PM_BASE_PADDR, PM_VADDR, &pm_base)) {
        debug_puts("[WDOG] ERROR: failed to map PM page\n");
        return -1;
    }

    debug_puts("[WDOG] PM mapped OK at vaddr ");
    debug_hex((uint32_t)(uintptr_t)pm_base);
    debug_puts("\n");

    wdog_initialized = true;
    wdog_running = false;
    return 0;
}

void watchdog_start(uint32_t secs)
{
    if (!wdog_initialized || !pm_base) return;

    if (secs == 0) secs = 1;
    if (secs > PM_WDOG_MAX_SECS) secs = PM_WDOG_MAX_SECS;

    wdog_timeout_secs = secs;
    wdog_timeout_ticks = secs * PM_WDOG_TICKS_PER_SEC;
    if (wdog_timeout_ticks > PM_WDOG_TIME_MASK)
        wdog_timeout_ticks = PM_WDOG_TIME_MASK;

    /* Load counter */
    pm_write(PM_WDOG, wdog_timeout_ticks);

    /* Enable full reset on watchdog expiry */
    uint32_t rstc = pm_read(PM_RSTC);
    rstc &= PM_RSTC_WRCFG_CLR;
    rstc |= PM_RSTC_WRCFG_FULL;
    pm_write(PM_RSTC, rstc);

    wdog_running = true;
    if (systimer_is_initialized())
        last_feed_us = systimer_read();

    debug_puts("[WDOG] Started, timeout=");
    debug_hex(secs);
    debug_puts("s (");
    debug_hex(wdog_timeout_ticks);
    debug_puts(" ticks)\n");
}

void watchdog_feed(void)
{
    if (!wdog_initialized || !wdog_running || !pm_base) return;

    /* Reload counter to full timeout value */
    pm_write(PM_WDOG, wdog_timeout_ticks);

    if (systimer_is_initialized())
        last_feed_us = systimer_read();
}

void watchdog_stop(void)
{
    if (!wdog_initialized || !pm_base) return;

    /* Clear watchdog config bits (disables watchdog reset) */
    uint32_t rstc = pm_read(PM_RSTC);
    rstc &= PM_RSTC_WRCFG_CLR;
    pm_write(PM_RSTC, rstc);

    /* Zero the counter */
    pm_write(PM_WDOG, 0);

    wdog_running = false;
    debug_puts("[WDOG] Stopped\n");
}

void watchdog_reboot(void)
{
    if (!wdog_initialized || !pm_base) return;

    debug_puts("[WDOG] Triggering reboot...\n");

    /* Set very short timeout (~1.6ms = 100 ticks at 62.5kHz) */
    pm_write(PM_WDOG, 100);

    /* Enable full reset */
    uint32_t rstc = pm_read(PM_RSTC);
    rstc &= PM_RSTC_WRCFG_CLR;
    rstc |= PM_RSTC_WRCFG_FULL;
    pm_write(PM_RSTC, rstc);

    /* Spin until reset triggers */
    while (1) {
        __asm__ volatile("wfe");
    }
}

bool watchdog_is_running(void)
{
    return wdog_initialized && wdog_running;
}

int watchdog_get_status(char *output, uint32_t output_size)
{
    if (!output || output_size == 0) return 0;

    if (!wdog_initialized) {
        return snprintf(output, output_size, "Watchdog: NOT INITIALIZED\n");
    }

    if (!wdog_running) {
        return snprintf(output, output_size, "Watchdog: STOPPED\n");
    }

    /* Read current counter value */
    uint32_t remaining = pm_read(PM_WDOG) & PM_WDOG_TIME_MASK;
    uint32_t remaining_ms = remaining * 1000 / PM_WDOG_TICKS_PER_SEC;

    uint64_t since_feed_ms = 0;
    if (systimer_is_initialized() && last_feed_us > 0) {
        since_feed_ms = (systimer_read() - last_feed_us) / 1000;
    }

    return snprintf(output, output_size,
                    "Watchdog: ACTIVE timeout=%us remaining=%ums last_feed=%lums ago\n",
                    wdog_timeout_secs, remaining_ms,
                    (unsigned long)since_feed_ms);
}
