/*
 * JARVIS AI-OS - BCM2711 System Timer
 *
 * Simple 1 MHz free-running counter for timing measurements.
 * Does NOT require seL4 timer capabilities (just MMIO access).
 */

#ifndef BCM2711_TIMER_H
#define BCM2711_TIMER_H

#include <stdint.h>
#include <stdbool.h>

/* BCM2711 System Timer base (ARM physical address) */
#define BCM2711_SYSTIMER_BASE  0xFE003000u

/* Initialize timer (map device frame). Returns true on success. */
bool systimer_init(void);

/* Check if timer has been initialized */
bool systimer_is_initialized(void);

/* Get current 64-bit counter value (1 MHz, so 1 tick = 1 µs) */
uint64_t systimer_read(void);

/* Convenience: get microseconds elapsed since a previous reading */
static inline uint64_t systimer_elapsed_us(uint64_t start)
{
    return systimer_read() - start;
}

#endif
