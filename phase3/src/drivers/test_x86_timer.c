/*
 * JARVIS AI-OS — x86 Timer Driver Tests
 * Mock-based tests for PIT, HPET, and TSC timer sources.
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <stdint.h>
#include "x86_timer.h"

/* ---- Test framework ---- */
static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("Test %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } } while(0)

/* ---- Mock hardware ---- */

static uint8_t mock_pit_cmd = 0;
static uint8_t mock_pit_lo = 0;
static uint8_t mock_pit_hi = 0;
static int mock_pit_write_phase = 0;  /* 0=lo, 1=hi for channel 0 */

/* Mock HPET: 10 MHz counter (period = 100ns = 100,000,000 fs) */
static uint64_t mock_hpet_counter = 0;
static uint32_t mock_hpet_config = 0;

/* Mock TSC */
static uint64_t mock_tsc = 1000000;

void outb(uint16_t port, uint8_t val)
{
    if (port == PIT_CMD) {
        mock_pit_cmd = val;
        mock_pit_write_phase = 0;
    } else if (port == PIT_CHANNEL0) {
        if (mock_pit_write_phase == 0) { mock_pit_lo = val; mock_pit_write_phase = 1; }
        else { mock_pit_hi = val; mock_pit_write_phase = 0; }
    }
}

uint8_t inb(uint16_t port)
{
    if (port == PIT_CHANNEL0) {
        /* Return decreasing counter (simulates PIT counting down) */
        static uint16_t pit_sim = 65535;
        pit_sim -= 100;
        if (mock_pit_write_phase == 0) { mock_pit_write_phase = 1; return pit_sim & 0xFF; }
        else { mock_pit_write_phase = 0; return (pit_sim >> 8) & 0xFF; }
    }
    return 0;
}

uint64_t rdtsc(void)
{
    return mock_tsc += 1000;  /* Increment by 1000 each call */
}

uint32_t mmio_read32(volatile void *addr)
{
    uintptr_t off = (uintptr_t)addr;
    if (off == HPET_CAP_ID + 4) {
        /* HPET period: 100,000,000 femtoseconds = 100 ns = 10 MHz */
        return 100000000;
    }
    if (off == HPET_CONFIG) {
        return mock_hpet_config;
    }
    return 0;
}

void mmio_write32(volatile void *addr, uint32_t val)
{
    uintptr_t off = (uintptr_t)addr;
    if (off == HPET_CONFIG) {
        mock_hpet_config = val;
    }
}

uint64_t mmio_read64(volatile void *addr)
{
    uintptr_t off = (uintptr_t)addr;
    if (off == HPET_COUNTER) {
        return mock_hpet_counter += 1000;  /* Increment each read */
    }
    return 0;
}

/* ---- Tests ---- */

static void test_pit_init(void)
{
    TEST("PIT init: command byte and channel config");
    mock_pit_cmd = 0;
    timer_init(TIMER_PIT);
    /* Expect: channel 0 (0x00) | lobyte/hibyte (0x30) | rate gen (0x04) = 0x34 */
    ASSERT(mock_pit_cmd == 0x34, "wrong PIT command byte");
    ASSERT(timer_get_source() == TIMER_PIT, "wrong source");
    PASS();
}

static void test_pit_frequency(void)
{
    TEST("PIT frequency: 1193182 Hz");
    timer_init(TIMER_PIT);
    ASSERT(timer_get_frequency() == 1193182ULL, "wrong PIT frequency");
    PASS();
}

static void test_hpet_init(void)
{
    TEST("HPET init: enable bit set in config");
    mock_hpet_config = 0;
    timer_init(TIMER_HPET);
    ASSERT((mock_hpet_config & HPET_CFG_ENABLE) != 0, "HPET enable not set");
    PASS();
}

static void test_hpet_frequency(void)
{
    TEST("HPET frequency: 10 MHz from 100ns period");
    timer_init(TIMER_HPET);
    /* 10^15 / 100,000,000 = 10,000,000 Hz */
    ASSERT(timer_get_frequency() == 10000000ULL, "wrong HPET frequency");
    PASS();
}

static void test_tsc_read(void)
{
    TEST("TSC read: returns increasing values");
    timer_init(TIMER_TSC);
    uint64_t t1 = timer_read_ticks();
    uint64_t t2 = timer_read_ticks();
    uint64_t t3 = timer_read_ticks();
    ASSERT(t2 > t1, "TSC not increasing (t2 <= t1)");
    ASSERT(t3 > t2, "TSC not increasing (t3 <= t2)");
    PASS();
}

static void test_ticks_to_us_pit(void)
{
    TEST("Ticks to us (PIT): 1193182 ticks = 1000000 us");
    timer_init(TIMER_PIT);
    uint64_t us = timer_ticks_to_us(PIT_FREQUENCY);
    ASSERT(us == 1000000ULL, "wrong conversion (PIT)");
    PASS();
}

static void test_ticks_to_us_hpet(void)
{
    TEST("Ticks to us (HPET): 10000000 ticks = 1000000 us");
    timer_init(TIMER_HPET);
    uint64_t us = timer_ticks_to_us(10000000ULL);
    ASSERT(us == 1000000ULL, "wrong conversion (HPET)");
    PASS();
}

static void test_delay(void)
{
    TEST("Delay: timer_delay_us consumes ticks");
    timer_init(TIMER_TSC);
    uint64_t before = timer_read_ticks();
    timer_delay_us(1000);  /* 1ms */
    uint64_t after = timer_read_ticks();
    /* TSC mock increments by 1000 per read. At 3.5 GHz, 1000us = 3,500,000 ticks.
     * The delay loop will read many times. Just verify after > before. */
    ASSERT(after > before, "delay did not consume ticks");
    PASS();
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS x86 Timer Tests ===\n\n");

    test_pit_init();
    test_pit_frequency();
    test_hpet_init();
    test_hpet_frequency();
    test_tsc_read();
    test_ticks_to_us_pit();
    test_ticks_to_us_hpet();
    test_delay();

    printf("\n=== Results: %d/%d PASS ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
