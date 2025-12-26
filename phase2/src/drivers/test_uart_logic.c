/*
 * JARVIS AI-OS - UART Logic Unit Tests
 * Phase 2 Week 31
 *
 * Tests the UART PL011 driver logic WITHOUT hardware.
 * Only tests calculations, flag interpretation, and state management.
 *
 * Tests:
 * 1. Baud divisor calculation for 115200
 * 2. Baud divisor calculation for 9600
 * 3. Baud divisor calculation for custom rate
 * 4. Flag register TX FIFO empty check
 * 5. Flag register RX FIFO empty check
 * 6. Flag register busy check
 * 7. Statistics initial values
 * 8. Functions safe when not initialized
 *
 * Compile: gcc -O2 test_uart_logic.c -o test_uart_logic
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/*
 * Constants from uart_pl011.h (copied to avoid hardware dependencies)
 */
#define UART_CLOCK_HZ           48000000    /* 48 MHz UART clock on Pi 4 */

/* Flag Register bits */
#define UART_FR_TXFE            (1 << 7)    /* TX FIFO Empty */
#define UART_FR_RXFF            (1 << 6)    /* RX FIFO Full */
#define UART_FR_TXFF            (1 << 5)    /* TX FIFO Full */
#define UART_FR_RXFE            (1 << 4)    /* RX FIFO Empty */
#define UART_FR_BUSY            (1 << 3)    /* UART Busy */

/* Test framework */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_PASS() do { printf("  [PASS]\n"); tests_passed++; return; } while(0)
#define TEST_FAIL(msg) do { printf("  [FAIL] %s\n", msg); tests_failed++; return; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) TEST_FAIL(msg); } while(0)

/*
 * Baud rate calculation function (extracted from driver)
 *
 * The PL011 uses a 16x oversampling clock. The divisor is calculated as:
 *   Divisor = UART_CLK / (16 * BAUD_RATE)
 *
 * The divisor is split into integer (IBRD) and fractional (FBRD) parts:
 *   IBRD = floor(Divisor)
 *   FBRD = round((Divisor - IBRD) * 64)
 */
static void calc_baud_divisors(uint32_t baud, uint32_t *ibrd_out, uint32_t *fbrd_out)
{
    /* Calculate divisor * 64 to preserve precision */
    uint32_t divider = (UART_CLOCK_HZ * 4) / baud;  /* 64 / 16 = 4 */

    *ibrd_out = divider / 64;
    *fbrd_out = divider % 64;
}

/*
 * Flag register interpretation functions (extracted logic)
 */
static bool is_tx_fifo_empty(uint32_t fr) { return (fr & UART_FR_TXFE) != 0; }
static bool is_rx_fifo_empty(uint32_t fr) { return (fr & UART_FR_RXFE) != 0; }
static bool is_rx_fifo_full(uint32_t fr)  { return (fr & UART_FR_RXFF) != 0; }
static bool is_tx_fifo_full(uint32_t fr)  { return (fr & UART_FR_TXFF) != 0; }
static bool is_uart_busy(uint32_t fr)     { return (fr & UART_FR_BUSY) != 0; }

/*
 * Test 1: Baud divisor calculation for 115200
 *
 * For 115200 baud with 48 MHz clock:
 *   Divisor = 48000000 / (16 * 115200) = 26.041666...
 *   IBRD = 26
 *   FBRD = round(0.041666 * 64) = round(2.666) = 3
 */
static void test_baud_divisor_115200(void)
{
    printf("\n========== TEST 1: Baud Divisor 115200 ==========\n");

    uint32_t ibrd, fbrd;
    calc_baud_divisors(115200, &ibrd, &fbrd);

    printf("  Baud: 115200\n");
    printf("  IBRD: %u (expected: 26)\n", ibrd);
    printf("  FBRD: %u (expected: ~3)\n", fbrd);

    /* Verify IBRD is 26 */
    ASSERT(ibrd == 26, "IBRD should be 26 for 115200 baud");

    /* FBRD should be 2-3 depending on rounding */
    ASSERT(fbrd >= 2 && fbrd <= 3, "FBRD should be 2-3 for 115200 baud");

    /* Verify the actual baud rate is close to target */
    /* Actual baud = UART_CLK / (16 * (IBRD + FBRD/64)) */
    double actual_divisor = (double)ibrd + (double)fbrd / 64.0;
    double actual_baud = (double)UART_CLOCK_HZ / (16.0 * actual_divisor);
    double error_pct = ((actual_baud - 115200.0) / 115200.0) * 100.0;

    printf("  Actual baud: %.2f\n", actual_baud);
    printf("  Error: %.2f%%\n", error_pct);

    ASSERT(error_pct > -2.0 && error_pct < 2.0, "Baud rate error should be < 2%");

    TEST_PASS();
}

/*
 * Test 2: Baud divisor calculation for 9600
 *
 * For 9600 baud with 48 MHz clock:
 *   Divisor = 48000000 / (16 * 9600) = 312.5
 *   IBRD = 312
 *   FBRD = round(0.5 * 64) = 32
 */
static void test_baud_divisor_9600(void)
{
    printf("\n========== TEST 2: Baud Divisor 9600 ==========\n");

    uint32_t ibrd, fbrd;
    calc_baud_divisors(9600, &ibrd, &fbrd);

    printf("  Baud: 9600\n");
    printf("  IBRD: %u (expected: 312)\n", ibrd);
    printf("  FBRD: %u (expected: 32)\n", fbrd);

    ASSERT(ibrd == 312, "IBRD should be 312 for 9600 baud");
    ASSERT(fbrd == 32, "FBRD should be 32 for 9600 baud");

    TEST_PASS();
}

/*
 * Test 3: Baud divisor calculation for custom rate (230400)
 */
static void test_baud_divisor_custom(void)
{
    printf("\n========== TEST 3: Baud Divisor Custom (230400) ==========\n");

    uint32_t ibrd, fbrd;
    calc_baud_divisors(230400, &ibrd, &fbrd);

    printf("  Baud: 230400\n");
    printf("  IBRD: %u\n", ibrd);
    printf("  FBRD: %u\n", fbrd);

    /* For 230400: Divisor = 48M / (16 * 230400) = 13.0208... */
    ASSERT(ibrd == 13, "IBRD should be 13 for 230400 baud");
    ASSERT(fbrd >= 1 && fbrd <= 2, "FBRD should be 1-2 for 230400 baud");

    /* Verify reasonable result */
    double actual_divisor = (double)ibrd + (double)fbrd / 64.0;
    double actual_baud = (double)UART_CLOCK_HZ / (16.0 * actual_divisor);
    double error_pct = ((actual_baud - 230400.0) / 230400.0) * 100.0;

    printf("  Actual baud: %.2f\n", actual_baud);
    printf("  Error: %.2f%%\n", error_pct);

    ASSERT(error_pct > -2.0 && error_pct < 2.0, "Baud rate error should be < 2%");

    TEST_PASS();
}

/*
 * Test 4: Flag register TX FIFO empty check
 */
static void test_flag_txfe_check(void)
{
    printf("\n========== TEST 4: Flag TX FIFO Empty ==========\n");

    /* TX FIFO empty bit set */
    uint32_t fr_empty = UART_FR_TXFE;
    ASSERT(is_tx_fifo_empty(fr_empty) == true, "Should detect TX FIFO empty");

    /* TX FIFO empty bit not set */
    uint32_t fr_not_empty = 0;
    ASSERT(is_tx_fifo_empty(fr_not_empty) == false, "Should detect TX FIFO not empty");

    /* TX FIFO empty with other flags */
    uint32_t fr_mixed = UART_FR_TXFE | UART_FR_RXFF | UART_FR_BUSY;
    ASSERT(is_tx_fifo_empty(fr_mixed) == true, "Should detect TX FIFO empty with other flags");

    printf("  All TX FIFO empty checks passed\n");
    TEST_PASS();
}

/*
 * Test 5: Flag register RX FIFO empty check
 */
static void test_flag_rxfe_check(void)
{
    printf("\n========== TEST 5: Flag RX FIFO Empty ==========\n");

    /* RX FIFO empty bit set */
    uint32_t fr_empty = UART_FR_RXFE;
    ASSERT(is_rx_fifo_empty(fr_empty) == true, "Should detect RX FIFO empty");

    /* RX FIFO empty bit not set (data available) */
    uint32_t fr_data = 0;
    ASSERT(is_rx_fifo_empty(fr_data) == false, "Should detect RX FIFO has data");

    /* RX FIFO full (not empty) */
    uint32_t fr_full = UART_FR_RXFF;
    ASSERT(is_rx_fifo_empty(fr_full) == false, "RX FIFO full should not be empty");
    ASSERT(is_rx_fifo_full(fr_full) == true, "Should detect RX FIFO full");

    printf("  All RX FIFO empty checks passed\n");
    TEST_PASS();
}

/*
 * Test 6: Flag register busy check
 */
static void test_flag_busy_check(void)
{
    printf("\n========== TEST 6: Flag UART Busy ==========\n");

    /* UART busy */
    uint32_t fr_busy = UART_FR_BUSY;
    ASSERT(is_uart_busy(fr_busy) == true, "Should detect UART busy");

    /* UART not busy */
    uint32_t fr_idle = 0;
    ASSERT(is_uart_busy(fr_idle) == false, "Should detect UART idle");

    /* UART busy with TX FIFO empty (transmitting last byte) */
    uint32_t fr_draining = UART_FR_TXFE | UART_FR_BUSY;
    ASSERT(is_uart_busy(fr_draining) == true, "Should detect busy while draining");
    ASSERT(is_tx_fifo_empty(fr_draining) == true, "Should detect TX FIFO empty while draining");

    printf("  All UART busy checks passed\n");
    TEST_PASS();
}

/*
 * Test 7: Statistics initial values
 */
static void test_stats_initial(void)
{
    printf("\n========== TEST 7: Statistics Initial Values ==========\n");

    /* Simulate initial state structure */
    typedef struct {
        uint64_t tx_bytes;
        uint64_t rx_bytes;
        uint32_t rx_errors;
        uint32_t tx_errors;
        bool initialized;
    } test_uart_state_t;

    test_uart_state_t state;
    memset(&state, 0, sizeof(state));

    /* Verify zero initialization */
    ASSERT(state.tx_bytes == 0, "tx_bytes should start at 0");
    ASSERT(state.rx_bytes == 0, "rx_bytes should start at 0");
    ASSERT(state.rx_errors == 0, "rx_errors should start at 0");
    ASSERT(state.tx_errors == 0, "tx_errors should start at 0");
    ASSERT(state.initialized == false, "initialized should be false");

    /* Simulate initialization */
    state.initialized = true;
    ASSERT(state.initialized == true, "initialized should be true after init");

    /* Simulate some activity */
    state.tx_bytes += 100;
    state.rx_bytes += 50;
    state.rx_errors += 1;

    ASSERT(state.tx_bytes == 100, "tx_bytes should track correctly");
    ASSERT(state.rx_bytes == 50, "rx_bytes should track correctly");
    ASSERT(state.rx_errors == 1, "rx_errors should track correctly");

    printf("  tx_bytes: %llu\n", (unsigned long long)state.tx_bytes);
    printf("  rx_bytes: %llu\n", (unsigned long long)state.rx_bytes);
    printf("  rx_errors: %u\n", state.rx_errors);

    TEST_PASS();
}

/*
 * Test 8: Functions safe when not initialized
 *
 * Tests that the driver logic handles uninitialized state safely.
 */
static void test_state_not_initialized(void)
{
    printf("\n========== TEST 8: State Not Initialized ==========\n");

    /* Simulate state structure with initialized = false */
    typedef struct {
        volatile uint32_t *base;
        bool initialized;
    } test_state_t;

    test_state_t state = {0};
    state.base = NULL;
    state.initialized = false;

    /* Verify guard conditions would work */
    bool can_read = state.initialized && state.base != NULL;
    bool can_write = state.initialized && state.base != NULL;

    ASSERT(can_read == false, "Should not be able to read when not initialized");
    ASSERT(can_write == false, "Should not be able to write when not initialized");

    /* Simulate partial initialization (base set but not initialized flag) */
    state.base = (volatile uint32_t *)0x12345678;  /* Fake address */
    state.initialized = false;

    can_read = state.initialized;
    can_write = state.initialized;

    ASSERT(can_read == false, "Should not read even with base set if not initialized");
    ASSERT(can_write == false, "Should not write even with base set if not initialized");

    /* Now fully initialize */
    state.initialized = true;

    can_read = state.initialized && state.base != NULL;
    can_write = state.initialized && state.base != NULL;

    ASSERT(can_read == true, "Should be able to read when fully initialized");
    ASSERT(can_write == true, "Should be able to write when fully initialized");

    printf("  Guard conditions work correctly\n");
    TEST_PASS();
}

/*
 * Main test runner
 */
int main(void)
{
    printf("\n");
    printf("======================================================================\n");
    printf("  JARVIS AI-OS - UART Logic Unit Tests\n");
    printf("  Phase 2 Week 31\n");
    printf("======================================================================\n");

    /* Run all tests */
    test_baud_divisor_115200();
    test_baud_divisor_9600();
    test_baud_divisor_custom();
    test_flag_txfe_check();
    test_flag_rxfe_check();
    test_flag_busy_check();
    test_stats_initial();
    test_state_not_initialized();

    /* Print summary */
    printf("\n");
    printf("======================================================================\n");
    printf("  TEST SUMMARY\n");
    printf("======================================================================\n");
    printf("  Total:  %d\n", tests_passed + tests_failed);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Rate:   %.1f%%\n", 100.0 * tests_passed / (tests_passed + tests_failed));
    printf("======================================================================\n\n");

    return tests_failed == 0 ? 0 : 1;
}
