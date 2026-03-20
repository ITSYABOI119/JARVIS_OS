/**
 * uart_16550.c - 16550A UART Serial Driver (x86-64)
 *
 * Standard 16550A UART driver using x86 I/O port instructions.
 * COM1-COM4 supported. Default: 115200 baud, 8N1, FIFO enabled.
 *
 * JARVIS AI-OS - Phase 3
 */

#include "uart_16550.h"

/* ========================================================================
 * I/O Port Access
 * ======================================================================== */

#ifndef JARVIS_TEST_MOCK
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
#else
/* Mock I/O for testing -- defined in test file */
extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
#endif

/* ========================================================================
 * Initialization
 * ======================================================================== */

int uart_16550_init(uint16_t port, uint32_t baud)
{
    uint16_t divisor;

    if (baud == 0)
        return -1;

    /* Calculate baud rate divisor: 115200 / baud */
    divisor = (uint16_t)(UART_CLOCK_HZ / baud);
    if (divisor == 0)
        divisor = 1;

    /* Disable all interrupts */
    outb(port + UART_IER, 0x00);

    /* Enable DLAB to set baud rate divisor */
    outb(port + UART_LCR, LCR_DLAB);

    /* Set divisor low byte and high byte */
    outb(port + UART_DLL, (uint8_t)(divisor & 0xFF));
    outb(port + UART_DLH, (uint8_t)((divisor >> 8) & 0xFF));

    /* Clear DLAB, set 8 data bits, 1 stop bit, no parity (8N1) */
    outb(port + UART_LCR, LCR_WORD_8BIT | LCR_STOP_1 | LCR_PARITY_NONE);

    /* Enable FIFO, clear TX/RX FIFOs, 14-byte trigger level */
    outb(port + UART_FCR, FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX | FCR_TRIGGER_14);

    /* Enable DTR, RTS, and OUT2 (required for IRQ on PC) */
    outb(port + UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

    /* Read LSR to clear any pending error/status bits */
    (void)inb(port + UART_LSR);

    /* Loopback self-test: enable loopback, send 0xAE, verify echo */
    outb(port + UART_MCR, MCR_LOOPBACK | MCR_DTR | MCR_RTS | MCR_OUT1 | MCR_OUT2);
    outb(port + UART_THR, 0xAE);

    if (inb(port + UART_RBR) != 0xAE)
        return -1;

    /* Loopback passed -- restore normal MCR (DTR + RTS + OUT2) */
    outb(port + UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

    return 0;
}

/* ========================================================================
 * Single Character I/O
 * ======================================================================== */

void uart_16550_putc(uint16_t port, char c)
{
    /* Spin until THR is empty (LSR bit 5) */
    while ((inb(port + UART_LSR) & LSR_THR_EMPTY) == 0)
        ;

    outb(port + UART_THR, (uint8_t)c);
}

char uart_16550_getc(uint16_t port)
{
    /* Spin until data is ready (LSR bit 0) */
    while ((inb(port + UART_LSR) & LSR_DATA_READY) == 0)
        ;

    return (char)inb(port + UART_RBR);
}

/* ========================================================================
 * Status Checks
 * ======================================================================== */

int uart_16550_read_ready(uint16_t port)
{
    return (inb(port + UART_LSR) & LSR_DATA_READY) != 0;
}

int uart_16550_write_ready(uint16_t port)
{
    return (inb(port + UART_LSR) & LSR_THR_EMPTY) != 0;
}

/* ========================================================================
 * Bulk I/O
 * ======================================================================== */

void uart_16550_puts(uint16_t port, const char *s)
{
    while (*s) {
        uart_16550_putc(port, *s);
        s++;
    }
}

int uart_16550_write(uint16_t port, const void *buf, int len)
{
    const uint8_t *p = (const uint8_t *)buf;
    int i;

    for (i = 0; i < len; i++)
        uart_16550_putc(port, (char)p[i]);

    return len;
}

int uart_16550_read(uint16_t port, void *buf, int len, int timeout_ms)
{
    uint8_t *p = (uint8_t *)buf;
    int count = 0;

    /*
     * Simple timeout via loop counter.
     * Each iteration of the idle loop is ~1us on modern x86,
     * so 1000 iterations per ms is a reasonable approximation.
     * For timeout_ms == 0, do a single non-blocking poll.
     */
    int timeout_loops = (timeout_ms > 0) ? timeout_ms * 1000 : 1;
    int idle_count = 0;

    while (count < len && idle_count < timeout_loops) {
        if (inb(port + UART_LSR) & LSR_DATA_READY) {
            p[count++] = inb(port + UART_RBR);
            idle_count = 0;  /* Reset timeout after each byte received */
        } else {
            idle_count++;
        }
    }

    return count;
}
