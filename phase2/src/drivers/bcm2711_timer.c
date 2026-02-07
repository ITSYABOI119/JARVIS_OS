/*
 * JARVIS AI-OS - BCM2711 System Timer
 *
 * Simple 1 MHz free-running counter for timing measurements.
 * The System Timer is a simple peripheral that does NOT require
 * seL4 timer capabilities - just standard MMIO device frame mapping.
 */

#include "bcm2711_timer.h"
#include "uart_pl011.h"

/* System Timer register offsets */
#define SYSTIMER_CS   0x00   /* Control/Status */
#define SYSTIMER_CLO  0x04   /* Counter Low 32 bits */
#define SYSTIMER_CHI  0x08   /* Counter High 32 bits */
/* 0x0C, 0x10, 0x14, 0x18 are compare registers (not used) */

static volatile uint32_t *systimer_base = NULL;

static inline uint32_t systimer_read_reg(uint32_t offset)
{
    return systimer_base[offset / 4];
}

bool systimer_init(void)
{
    if (systimer_base) {
        return true;  /* Already initialized */
    }

    if (!uart_device_map_ready()) {
        return false;
    }

    if (!uart_device_map_page(BCM2711_SYSTIMER_BASE, 0, &systimer_base)) {
        return false;
    }

    return true;
}

bool systimer_is_initialized(void)
{
    return systimer_base != NULL;
}

uint64_t systimer_read(void)
{
    if (!systimer_base) {
        return 0;
    }

    /*
     * Read high, low, high again to detect rollover.
     * If high changed, re-read low to get consistent 64-bit value.
     */
    uint32_t hi1 = systimer_read_reg(SYSTIMER_CHI);
    uint32_t lo  = systimer_read_reg(SYSTIMER_CLO);
    uint32_t hi2 = systimer_read_reg(SYSTIMER_CHI);

    if (hi1 != hi2) {
        lo = systimer_read_reg(SYSTIMER_CLO);
    }

    return ((uint64_t)hi2 << 32) | lo;
}
