/*
 * JARVIS AI-OS - PL011 UART Driver Implementation
 * Phase 2 Week 32 (REVISED)
 *
 * BCM2711 PL011 UART driver for seL4 on Raspberry Pi 4.
 *
 * IMPLEMENTATION: Uses seL4 capability-based MMIO via ps_io_map()
 * - Requires platsupport library linked in CMakeLists.txt
 * - Requires ps_io_ops_t initialized by root task
 *
 * NOTE: This implementation uses proper seL4 capability-based memory mapping.
 */

#include "uart_pl011.h"
#include <stdio.h>
#include <string.h>
#include <platsupport/io.h>

/* seL4 I/O operations - must be initialized by root task before calling uart_init() */
extern ps_io_ops_t io_ops;

/* Global UART state */
static uart_state_t uart_state = {0};

/*
 * Memory-Mapped Register Access
 *
 * These inline functions provide type-safe access to MMIO registers
 * with proper memory barriers for ARM64.
 */

static inline uint32_t uart_read_reg(uint32_t offset)
{
    if (!uart_state.base) return 0;

    volatile uint32_t *reg = (volatile uint32_t *)((uint8_t *)uart_state.base + offset);
    uint32_t val = *reg;

    /* Memory barrier to ensure read completes */
    __asm__ volatile("dmb sy" ::: "memory");

    return val;
}

static inline void uart_write_reg(uint32_t offset, uint32_t val)
{
    if (!uart_state.base) return;

    /* Memory barrier before write */
    __asm__ volatile("dmb sy" ::: "memory");

    volatile uint32_t *reg = (volatile uint32_t *)((uint8_t *)uart_state.base + offset);
    *reg = val;

    /* Memory barrier after write */
    __asm__ volatile("dmb sy" ::: "memory");
}

/*
 * Calculate Baud Rate Divisors
 *
 * The PL011 uses a 16x oversampling clock. The divisor is calculated as:
 *   Divisor = UART_CLK / (16 * BAUD_RATE)
 *
 * The divisor is split into integer (IBRD) and fractional (FBRD) parts:
 *   IBRD = floor(Divisor)
 *   FBRD = round((Divisor - IBRD) * 64)
 */
static void uart_set_baud_rate(uint32_t baud)
{
    /* Calculate divisor * 64 to preserve precision */
    uint32_t divider = (UART_CLOCK_HZ * 4) / baud;  /* 64 / 16 = 4 */

    uint32_t ibrd = divider / 64;
    uint32_t fbrd = divider % 64;

    uart_write_reg(UART_IBRD, ibrd);
    uart_write_reg(UART_FBRD, fbrd);

    uart_state.baud_rate = baud;
}

/*
 * Initialize UART
 */
bool uart_init(void)
{
    return uart_init_baud(UART_DEFAULT_BAUD);
}

bool uart_init_baud(uint32_t baud_rate)
{
    /*
     * NOTE: In seL4, the UART base address must be mapped before calling this.
     *
     * For seL4 tutorials framework, this might be done via:
     * 1. CAmkES dataport connector
     * 2. ps_io_map() from sel4platsupport
     * 3. Manual page table mapping
     *
     * For now, we use the physical address directly (works in some configs).
     * This will need to be updated for proper seL4 integration.
     */

    /* Check if already initialized */
    if (uart_state.initialized) {
        printf("[UART] Already initialized\n");
        return true;
    }

    /*
     * Map UART registers using seL4 capability-based MMIO
     *
     * Uses ps_io_map() from platsupport library to create proper
     * capability-safe mapping of UART peripheral registers.
     */
    void *vaddr = ps_io_map(&io_ops.io_mapper,
                            UART0_BASE,        /* Physical address: 0xFE201000 for Pi 4 */
                            0x200,             /* Size: 512 bytes (PL011 register space) */
                            false,             /* Not cached (device memory) */
                            PS_MEM_NORMAL);    /* Normal device memory */

    if (vaddr == NULL) {
        printf("[UART] ERROR: Failed to map UART registers at 0x%08x\n", UART0_BASE);
        printf("[UART] Capability mapping failed - check ps_io_ops initialization\n");
        return false;
    }

    uart_state.base = (volatile uint32_t *)vaddr;

    printf("[UART] Initializing PL011 at 0x%08x, baud=%u\n",
           UART0_BASE, baud_rate);

    /* Disable UART while configuring */
    uart_write_reg(UART_CR, 0);

    /* Wait for any pending transmission to complete */
    while (uart_read_reg(UART_FR) & UART_FR_BUSY);

    /* Flush FIFOs by disabling them */
    uart_write_reg(UART_LCRH, 0);

    /* Clear all interrupts */
    uart_write_reg(UART_ICR, 0x7FF);

    /* Set baud rate */
    uart_set_baud_rate(baud_rate);

    /*
     * Configure line control:
     * - 8 data bits (WLEN_8)
     * - No parity
     * - 1 stop bit
     * - Enable FIFOs (FEN)
     */
    uart_write_reg(UART_LCRH, UART_LCRH_WLEN_8 | UART_LCRH_FEN);

    /*
     * Enable UART:
     * - UARTEN: Enable UART
     * - TXE: Enable transmitter
     * - RXE: Enable receiver
     */
    uart_write_reg(UART_CR, UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);

    /* Initialize statistics */
    uart_state.tx_bytes = 0;
    uart_state.rx_bytes = 0;
    uart_state.rx_errors = 0;
    uart_state.tx_errors = 0;
    uart_state.initialized = true;

    printf("[UART] Initialized successfully\n");
    return true;
}

void uart_shutdown(void)
{
    if (!uart_state.initialized) return;

    /* Wait for pending transmission */
    uart_flush_tx();

    /* Disable UART */
    uart_write_reg(UART_CR, 0);

    uart_state.initialized = false;
    uart_state.base = NULL;

    printf("[UART] Shutdown complete\n");
}

/*
 * Basic I/O Operations
 */

void uart_putc(char c)
{
    if (!uart_state.initialized) return;

    /* Wait for TX FIFO to have space */
    while (uart_read_reg(UART_FR) & UART_FR_TXFF);

    uart_write_reg(UART_DR, (uint32_t)c);
    uart_state.tx_bytes++;
}

char uart_getc(void)
{
    if (!uart_state.initialized) return '\0';

    /* Wait for RX FIFO to have data */
    while (uart_read_reg(UART_FR) & UART_FR_RXFE);

    uint32_t data = uart_read_reg(UART_DR);

    /* Check for errors in upper bits */
    if (data & 0xF00) {
        uart_state.rx_errors++;
    }

    uart_state.rx_bytes++;
    return (char)(data & 0xFF);
}

bool uart_rx_ready(void)
{
    if (!uart_state.initialized) return false;
    return !(uart_read_reg(UART_FR) & UART_FR_RXFE);
}

bool uart_tx_ready(void)
{
    if (!uart_state.initialized) return false;
    return !(uart_read_reg(UART_FR) & UART_FR_TXFF);
}

/*
 * String and Buffer Operations
 */

void uart_puts(const char *s)
{
    if (!s) return;

    while (*s) {
        /* Convert LF to CRLF for terminal compatibility */
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

size_t uart_write(const uint8_t *data, size_t len)
{
    if (!data || !uart_state.initialized) return 0;

    size_t written = 0;
    while (written < len) {
        /* Wait for TX FIFO space */
        while (uart_read_reg(UART_FR) & UART_FR_TXFF);

        uart_write_reg(UART_DR, data[written]);
        written++;
    }

    uart_state.tx_bytes += written;
    return written;
}

/*
 * Simple delay for timeout handling
 *
 * NOTE: This is a crude busy-wait. In seL4, we should use
 * proper timers via ltimer or seL4_Wait with timeout.
 */
static void uart_delay_us(uint32_t us)
{
    /* Very rough approximation - tune for actual clock speed */
    volatile uint32_t count = us * 100;
    while (count--) {
        __asm__ volatile("nop");
    }
}

int uart_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!buf || !max_len || !uart_state.initialized) return -1;

    size_t received = 0;
    uint32_t elapsed_us = 0;
    uint32_t timeout_us = timeout_ms * 1000;

    while (received < max_len) {
        if (uart_rx_ready()) {
            buf[received++] = (uint8_t)uart_getc();
            elapsed_us = 0;  /* Reset timeout on successful read */
        } else if (timeout_ms > 0) {
            uart_delay_us(100);
            elapsed_us += 100;
            if (elapsed_us >= timeout_us) {
                break;  /* Timeout */
            }
        } else {
            /* Non-blocking mode: return immediately if no data */
            break;
        }
    }

    return (int)received;
}

int uart_read_line(char *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!buf || max_len < 2 || !uart_state.initialized) return -1;

    size_t pos = 0;
    uint32_t elapsed_us = 0;
    uint32_t timeout_us = timeout_ms * 1000;

    while (pos < max_len - 1) {
        if (uart_rx_ready()) {
            char c = uart_getc();
            elapsed_us = 0;  /* Reset timeout on successful read */

            /* End of line */
            if (c == '\r' || c == '\n') {
                break;
            }

            /* Handle backspace */
            if (c == '\b' || c == 0x7F) {
                if (pos > 0) {
                    pos--;
                    /* Echo backspace-space-backspace to erase character */
                    uart_puts("\b \b");
                }
                continue;
            }

            /* Store printable characters */
            if (c >= 0x20 && c < 0x7F) {
                buf[pos++] = c;
                uart_putc(c);  /* Echo */
            }
        } else {
            uart_delay_us(100);
            elapsed_us += 100;
            if (timeout_ms > 0 && elapsed_us >= timeout_us) {
                buf[pos] = '\0';
                return -1;  /* Timeout */
            }
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

/*
 * Buffer Management
 */

void uart_flush_tx(void)
{
    if (!uart_state.initialized) return;

    /* Wait for TX FIFO to empty */
    while (!(uart_read_reg(UART_FR) & UART_FR_TXFE));

    /* Wait for transmitter to finish */
    while (uart_read_reg(UART_FR) & UART_FR_BUSY);
}

void uart_flush_rx(void)
{
    if (!uart_state.initialized) return;

    /* Read and discard all pending data */
    while (uart_rx_ready()) {
        (void)uart_read_reg(UART_DR);
    }
}

/*
 * Statistics and Debugging
 */

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

    if (uart_state.initialized) {
        uint32_t fr = uart_read_reg(UART_FR);

        printf("Baud Rate:   %u\n", uart_state.baud_rate);
        printf("Base Addr:   %p\n", (void *)uart_state.base);
        printf("\nFlag Register (0x%02x):\n", fr);
        printf("  TX FIFO Empty: %s\n", (fr & UART_FR_TXFE) ? "Yes" : "No");
        printf("  RX FIFO Empty: %s\n", (fr & UART_FR_RXFE) ? "Yes" : "No");
        printf("  TX FIFO Full:  %s\n", (fr & UART_FR_TXFF) ? "Yes" : "No");
        printf("  RX FIFO Full:  %s\n", (fr & UART_FR_RXFF) ? "Yes" : "No");
        printf("  UART Busy:     %s\n", (fr & UART_FR_BUSY) ? "Yes" : "No");
        printf("\nStatistics:\n");
        printf("  TX Bytes:   %llu\n", (unsigned long long)uart_state.tx_bytes);
        printf("  RX Bytes:   %llu\n", (unsigned long long)uart_state.rx_bytes);
        printf("  RX Errors:  %u\n", uart_state.rx_errors);
    }

    printf("=================================\n\n");
}
