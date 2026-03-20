/**
 * test_uart_16550.c - 16550A UART Driver Unit Tests
 *
 * Mock I/O port tests for the x86 16550A UART driver.
 * Verifies init sequence, baud divisor, TX/RX, and status checks.
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *       -I phase3/src/drivers test_uart_16550.c uart_16550.c -o test_uart
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "uart_16550.h"

/* ========================================================================
 * Mock I/O Layer
 * ======================================================================== */

static uint8_t mock_regs[8] = {0};
static uint8_t mock_rx_data = 0;
static int mock_rx_ready = 0;

/* Track writes for verification */
static uint8_t mock_thr_last = 0;
static int mock_thr_write_count = 0;
static uint8_t mock_lcr_history[16] = {0};
static int mock_lcr_writes = 0;
static uint8_t mock_dll_last = 0;
static uint8_t mock_dlh_last = 0;
static uint8_t mock_fcr_last = 0;

/* Loopback state */
static int mock_loopback_active = 0;
static uint8_t mock_loopback_byte = 0;

void outb(uint16_t port, uint8_t val) {
    uint16_t offset = port - COM1_BASE;
    if (offset >= 8) return;

    mock_regs[offset] = val;

    if (offset == UART_LCR) {
        if (mock_lcr_writes < 16)
            mock_lcr_history[mock_lcr_writes] = val;
        mock_lcr_writes++;
    }
    if (offset == UART_THR) {
        /* DLL when DLAB=1, THR when DLAB=0 */
        if (mock_regs[UART_LCR] & LCR_DLAB) {
            mock_dll_last = val;
        } else {
            mock_thr_last = val;
            mock_thr_write_count++;
            if (mock_loopback_active) {
                mock_loopback_byte = val;
                mock_rx_ready = 1;
            }
        }
    }
    if (offset == UART_IER && (mock_regs[UART_LCR] & LCR_DLAB)) {
        mock_dlh_last = val;
    }
    if (offset == UART_FCR) {
        mock_fcr_last = val;
    }
    if (offset == UART_MCR) {
        mock_loopback_active = (val & MCR_LOOPBACK) ? 1 : 0;
    }
}

uint8_t inb(uint16_t port) {
    uint16_t offset = port - COM1_BASE;
    if (offset >= 8) return 0;

    /* LSR: always report TX empty; conditionally report RX ready */
    if (offset == UART_LSR) {
        uint8_t lsr = LSR_THR_EMPTY | LSR_XMIT_EMPTY;
        if (mock_rx_ready) lsr |= LSR_DATA_READY;
        return lsr;
    }

    /* RBR: return loopback byte or mock RX data */
    if (offset == UART_RBR) {
        if (mock_loopback_active && mock_rx_ready) {
            mock_rx_ready = 0;
            return mock_loopback_byte;
        }
        if (mock_rx_ready) {
            mock_rx_ready = 0;
            return mock_rx_data;
        }
        return 0;
    }

    return mock_regs[offset];
}

static void mock_reset(void) {
    memset(mock_regs, 0, sizeof(mock_regs));
    memset(mock_lcr_history, 0, sizeof(mock_lcr_history));
    mock_rx_data = 0;
    mock_rx_ready = 0;
    mock_thr_last = 0;
    mock_thr_write_count = 0;
    mock_lcr_writes = 0;
    mock_dll_last = 0;
    mock_dlh_last = 0;
    mock_fcr_last = 0;
    mock_loopback_active = 0;
    mock_loopback_byte = 0;
}

/* ========================================================================
 * Test Framework
 * ======================================================================== */

static int tests_run = 0, tests_passed = 0;
#define TEST(name) do { tests_run++; printf("Test %d: %s ... ", tests_run, name); } while(0)
#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); return; } while(0)

/* ========================================================================
 * Tests
 * ======================================================================== */

static void test_init(void) {
    TEST("Init sequence (DLAB, 8N1, FIFO, MCR)");
    mock_reset();

    int rc = uart_16550_init(COM1_BASE, 115200);
    if (rc != 0) FAIL("init returned non-zero");

    /* Verify DLAB was set at some point during init */
    int dlab_seen = 0;
    for (int i = 0; i < mock_lcr_writes && i < 16; i++) {
        if (mock_lcr_history[i] & LCR_DLAB) dlab_seen = 1;
    }
    if (!dlab_seen) FAIL("DLAB never set during init");

    /* Final LCR should be 8N1 (0x03), no DLAB */
    int final_lcr_idx = mock_lcr_writes - 1;
    if (final_lcr_idx < 0) FAIL("no LCR writes");
    /* Find the 8N1 write (the one after DLAB is cleared, before loopback) */
    int found_8n1 = 0;
    for (int i = 0; i < mock_lcr_writes && i < 16; i++) {
        if (mock_lcr_history[i] == (LCR_WORD_8BIT | LCR_STOP_1 | LCR_PARITY_NONE))
            found_8n1 = 1;
    }
    if (!found_8n1) FAIL("8N1 (LCR=0x03) not written");

    /* FIFO should be enabled with 14-byte trigger */
    if (mock_fcr_last != (FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX | FCR_TRIGGER_14))
        FAIL("FCR not set correctly");

    /* MCR should end at DTR + RTS + OUT2 (0x0B) */
    if (mock_regs[UART_MCR] != (MCR_DTR | MCR_RTS | MCR_OUT2))
        FAIL("MCR not restored after loopback");

    PASS();
}

static void test_baud_divisor(void) {
    TEST("Baud divisor (115200=1, 9600=12)");
    mock_reset();

    /* 115200 baud: divisor should be 1 */
    uart_16550_init(COM1_BASE, 115200);
    if (mock_dll_last != 1) FAIL("115200 DLL != 1");
    if (mock_dlh_last != 0) FAIL("115200 DLH != 0");

    mock_reset();

    /* 9600 baud: divisor should be 12 */
    uart_16550_init(COM1_BASE, 9600);
    if (mock_dll_last != 12) FAIL("9600 DLL != 12");
    if (mock_dlh_last != 0) FAIL("9600 DLH != 0");

    PASS();
}

static void test_putc(void) {
    TEST("Putc writes to THR");
    mock_reset();

    uart_16550_putc(COM1_BASE, 'A');
    if (mock_thr_last != 'A') FAIL("THR != 'A'");

    uart_16550_putc(COM1_BASE, 'Z');
    if (mock_thr_last != 'Z') FAIL("THR != 'Z'");

    PASS();
}

static void test_getc(void) {
    TEST("Getc reads from RBR");
    mock_reset();

    mock_rx_ready = 1;
    mock_rx_data = 'X';
    char c = uart_16550_getc(COM1_BASE);
    if (c != 'X') FAIL("getc != 'X'");
    if (mock_rx_ready != 0) FAIL("rx_ready not cleared");

    PASS();
}

static void test_puts(void) {
    TEST("Puts writes string via THR");
    mock_reset();
    mock_thr_write_count = 0;

    uart_16550_puts(COM1_BASE, "Hi");
    if (mock_thr_write_count != 2) FAIL("expected 2 THR writes");
    /* Last char written should be 'i' */
    if (mock_thr_last != 'i') FAIL("last THR != 'i'");

    PASS();
}

static void test_read_ready(void) {
    TEST("Read ready (0 when empty, 1 when data)");
    mock_reset();

    mock_rx_ready = 0;
    if (uart_16550_read_ready(COM1_BASE) != 0) FAIL("should be 0 when no data");

    mock_rx_ready = 1;
    if (uart_16550_read_ready(COM1_BASE) == 0) FAIL("should be non-zero when data ready");

    PASS();
}

static void test_write_ready(void) {
    TEST("Write ready (always 1 in mock)");
    mock_reset();

    /* Mock LSR always has THR_EMPTY set */
    if (uart_16550_write_ready(COM1_BASE) == 0) FAIL("should be non-zero");

    PASS();
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void) {
    printf("=== JARVIS AI-OS: 16550A UART Driver Tests ===\n\n");

    test_init();
    test_baud_divisor();
    test_putc();
    test_getc();
    test_puts();
    test_read_ready();
    test_write_ready();

    printf("\n=== Results: %d/%d PASSED ===\n", tests_passed, tests_run);

    if (tests_passed == tests_run)
        printf("ALL TESTS PASSED\n");
    else
        printf("SOME TESTS FAILED\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
