/*
 * JARVIS AI-OS Phase 3 — x86 Timer Driver
 * Supports PIT (8254), HPET, and TSC with common interface.
 */

#include "x86_timer.h"

/* ---- Hardware access ---- */

#ifndef JARVIS_TEST_MOCK
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v;
}
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static inline uint32_t mmio_read32(volatile void *addr) { return *(volatile uint32_t *)addr; }
static inline void mmio_write32(volatile void *addr, uint32_t val) { *(volatile uint32_t *)addr = val; }
static inline uint64_t mmio_read64(volatile void *addr) { return *(volatile uint64_t *)addr; }
#else
extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
extern uint64_t rdtsc(void);
extern uint32_t mmio_read32(volatile void *addr);
extern void mmio_write32(volatile void *addr, uint32_t val);
extern uint64_t mmio_read64(volatile void *addr);
#endif

/* ---- State ---- */

static timer_source_t g_source = TIMER_PIT;
static uint64_t g_frequency = 0;
static uint8_t *g_hpet_base = (void *)0;  /* HPET MMIO base (set externally or via ACPI) */

/* ---- PIT ---- */

static int pit_init(void)
{
    /* Channel 0, lobyte/hibyte access, rate generator mode */
    outb(PIT_CMD, PIT_CMD_CHANNEL0 | PIT_CMD_LOHIGH | PIT_CMD_RATEGEN);

    /* Set max divisor (0 = 65536) for ~18.2 Hz tick */
    outb(PIT_CHANNEL0, 0x00);  /* Low byte */
    outb(PIT_CHANNEL0, 0x00);  /* High byte */

    g_frequency = PIT_FREQUENCY;
    return 0;
}

static uint64_t pit_read_ticks(void)
{
    /* Latch channel 0 counter */
    outb(PIT_CMD, 0x00);  /* Latch command for channel 0 */
    uint8_t lo = inb(PIT_CHANNEL0);
    uint8_t hi = inb(PIT_CHANNEL0);
    /* PIT counts DOWN, so invert for increasing ticks */
    static uint64_t pit_overflows = 0;
    static uint16_t pit_last = 0;
    uint16_t current = (uint16_t)hi << 8 | lo;
    if (current > pit_last) pit_overflows++;  /* Wrapped */
    pit_last = current;
    return pit_overflows * 65536ULL + (65535 - current);
}

/* ---- HPET ---- */

static int hpet_init(void)
{
    /* Read capabilities — period in femtoseconds is in bits 63:32 of CAP register */
    uint32_t cap_hi = mmio_read32(g_hpet_base + HPET_CAP_ID + 4);
    uint64_t period_fs = (uint64_t)cap_hi;  /* Period in femtoseconds */

    if (period_fs == 0) return -1;

    /* Frequency = 10^15 / period_fs */
    g_frequency = 1000000000000000ULL / period_fs;

    /* Enable main counter */
    uint32_t config = mmio_read32(g_hpet_base + HPET_CONFIG);
    config |= HPET_CFG_ENABLE;
    mmio_write32(g_hpet_base + HPET_CONFIG, config);

    return 0;
}

static uint64_t hpet_read_ticks(void)
{
    return mmio_read64(g_hpet_base + HPET_COUNTER);
}

/* ---- TSC ---- */

static int tsc_init(void)
{
    /* Calibrate TSC frequency using PIT.
     * In mock mode or when PIT isn't available, use a fixed estimate. */
#ifdef JARVIS_TEST_MOCK
    g_frequency = 3500000000ULL;  /* 3.5 GHz mock Ryzen */
#else
    /* Count TSC ticks over ~10ms measured by PIT */
    outb(PIT_CMD, PIT_CMD_CHANNEL0 | PIT_CMD_LOHIGH | PIT_CMD_RATEGEN);
    uint16_t count = 11932;  /* ~10ms at 1.193182 MHz */
    outb(PIT_CHANNEL0, count & 0xFF);
    outb(PIT_CHANNEL0, (count >> 8) & 0xFF);

    uint64_t start = rdtsc();
    /* Wait for PIT to count down */
    outb(PIT_CMD, 0x00);
    while (1) {
        outb(PIT_CMD, 0x00);
        uint8_t lo = inb(PIT_CHANNEL0);
        uint8_t hi = inb(PIT_CHANNEL0);
        uint16_t cur = (uint16_t)hi << 8 | lo;
        if (cur > count / 2) break;  /* Wrapped past halfway */
    }
    uint64_t end = rdtsc();
    uint64_t elapsed = end - start;
    g_frequency = elapsed * 100;  /* 10ms -> 1s extrapolation */
#endif
    return 0;
}

/* ---- Public API ---- */

int timer_init(timer_source_t source)
{
    g_source = source;
    switch (source) {
        case TIMER_PIT:  return pit_init();
        case TIMER_HPET: return hpet_init();
        case TIMER_TSC:  return tsc_init();
        default: return -1;
    }
}

uint64_t timer_read_ticks(void)
{
    switch (g_source) {
        case TIMER_PIT:  return pit_read_ticks();
        case TIMER_HPET: return hpet_read_ticks();
        case TIMER_TSC:  return rdtsc();
        default: return 0;
    }
}

uint64_t timer_ticks_to_us(uint64_t ticks)
{
    if (g_frequency == 0) return 0;
    /* Avoid overflow: ticks * 1000000 could overflow uint64.
     * Split: (ticks / freq) * 1000000 + (ticks % freq) * 1000000 / freq */
    uint64_t secs = ticks / g_frequency;
    uint64_t rem  = ticks % g_frequency;
    return secs * 1000000ULL + rem * 1000000ULL / g_frequency;
}

void timer_delay_us(uint64_t us)
{
    uint64_t start = timer_read_ticks();
    uint64_t target_ticks = us * g_frequency / 1000000ULL;
    while (timer_read_ticks() - start < target_ticks) {
        /* Busy wait */
    }
}

uint64_t timer_get_frequency(void)
{
    return g_frequency;
}

timer_source_t timer_get_source(void)
{
    return g_source;
}
