/*
 * JARVIS AI-OS - PL011 UART Driver Implementation
 * Phase 2 Week 32 (safe mapping)
 *
 * BCM2711 PL011 UART driver for seL4 on Raspberry Pi 4.
 *
 * IMPLEMENTATION:
 * - Use seL4 platsupport serial setup to map UART safely.
 * - Avoid direct MMIO in user space; access via __plat_putchar/__plat_getchar.
 */

#include "uart_pl011.h"
#include <stdio.h>
#include <string.h>

#include <sel4/sel4.h>
#include <sel4platsupport/platsupport.h>

extern int __plat_getchar(void) __attribute__((weak));

/* Global UART state */
static uart_state_t uart_state = {0};
static int pending_char = -1;
static bool uart_rx_enabled = false;

static int uart_try_getchar(void)
{
    if (!uart_rx_enabled || !__plat_getchar) {
        return EOF;
    }

    int ch = __plat_getchar();
    if (ch == EOF) {
        return EOF;
    }
    return ch & 0xFF;
}

bool uart_init(void)
{
    return uart_init_baud(UART_DEFAULT_BAUD);
}

bool uart_init_baud(uint32_t baud_rate)
{
    if (uart_state.initialized) {
        return true;
    }

    if (platsupport_serial_setup_bootinfo_failsafe() != 0) {
        return false;
    }

    uart_state.base = NULL;
    uart_state.baud_rate = baud_rate;
    uart_state.tx_bytes = 0;
    uart_state.rx_bytes = 0;
    uart_state.rx_errors = 0;
    uart_state.tx_errors = 0;
    uart_state.initialized = true;
    pending_char = -1;
    uart_rx_enabled = false; /* RX disabled until we have a safe UART RX path */

    return true;
}

void uart_shutdown(void)
{
    if (!uart_state.initialized) {
        return;
    }

    uart_flush_tx();
    uart_state.initialized = false;
    pending_char = -1;
}

void uart_putc(char c)
{
    if (!uart_state.initialized) {
        return;
    }

    seL4_DebugPutChar((int)c);
    uart_state.tx_bytes++;
}

char uart_getc(void)
{
    if (!uart_state.initialized) {
        return '\0';
    }
    if (!uart_rx_enabled) {
        return '\0';
    }

    if (pending_char != -1) {
        int ch = pending_char;
        pending_char = -1;
        uart_state.rx_bytes++;
        return (char)ch;
    }

    int ch;
    do {
        ch = uart_try_getchar();
    } while (ch == EOF);

    uart_state.rx_bytes++;
    return (char)ch;
}

bool uart_rx_ready(void)
{
    if (!uart_state.initialized) {
        return false;
    }
    if (!uart_rx_enabled) {
        return false;
    }

    if (pending_char != -1) {
        return true;
    }

    int ch = uart_try_getchar();
    if (ch == EOF) {
        return false;
    }

    pending_char = ch;
    return true;
}

bool uart_tx_ready(void)
{
    return uart_state.initialized;
}

void uart_puts(const char *s)
{
    if (!s) {
        return;
    }

    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

size_t uart_write(const uint8_t *data, size_t len)
{
    if (!data || !uart_state.initialized) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        uart_putc((char)data[i]);
    }
    return len;
}

static void uart_delay_us(uint32_t us)
{
    volatile uint32_t count = us * 100;
    while (count--) {
        __asm__ volatile("nop");
    }
}

int uart_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!buf || !max_len || !uart_state.initialized) {
        return -1;
    }

    size_t received = 0;
    uint32_t elapsed_us = 0;
    uint32_t timeout_us = timeout_ms * 1000;

    while (received < max_len) {
        if (uart_rx_ready()) {
            buf[received++] = (uint8_t)uart_getc();
            elapsed_us = 0;
        } else if (timeout_ms > 0) {
            uart_delay_us(100);
            elapsed_us += 100;
            if (elapsed_us >= timeout_us) {
                break;
            }
        } else {
            break;
        }
    }

    return (int)received;
}

int uart_read_line(char *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!buf || max_len < 2 || !uart_state.initialized) {
        return -1;
    }

    size_t pos = 0;
    uint32_t elapsed_us = 0;
    uint32_t timeout_us = timeout_ms * 1000;

    while (pos < max_len - 1) {
        if (uart_rx_ready()) {
            char c = uart_getc();
            elapsed_us = 0;

            if (c == '\r' || c == '\n') {
                break;
            }

            if (c == '\b' || c == 0x7F) {
                if (pos > 0) {
                    pos--;
                    uart_puts("\b \b");
                }
                continue;
            }

            if (c >= 0x20 && c < 0x7F) {
                buf[pos++] = c;
                uart_putc(c);
            }
        } else {
            uart_delay_us(100);
            elapsed_us += 100;
            if (timeout_ms > 0 && elapsed_us >= timeout_us) {
                buf[pos] = '\0';
                return -1;
            }
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

void uart_flush_tx(void)
{
    /* No-op: __plat_putchar is synchronous */
}

void uart_flush_rx(void)
{
    if (!uart_state.initialized) {
        return;
    }

    pending_char = -1;
    while (uart_try_getchar() != EOF) {
        /* drain */
    }
}

void uart_get_stats(uint64_t *tx_bytes, uint64_t *rx_bytes, uint32_t *rx_errors)
{
    if (tx_bytes) *tx_bytes = uart_state.tx_bytes;
    if (rx_bytes) *rx_bytes = uart_state.rx_bytes;
    if (rx_errors) *rx_errors = uart_state.rx_errors;
}

void uart_print_status(void)
{
    printf("\n========== UART STATUS ==========\n");
    printf("Initialized: %s\n", uart_state.initialized ? "Yes" : "No");
    printf("Baud Rate:   %u\n", uart_state.baud_rate);
    printf("TX Bytes:    %llu\n", (unsigned long long)uart_state.tx_bytes);
    printf("RX Bytes:    %llu\n", (unsigned long long)uart_state.rx_bytes);
    printf("RX Errors:   %u\n", uart_state.rx_errors);
    printf("=================================\n\n");
}
