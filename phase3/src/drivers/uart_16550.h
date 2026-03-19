/**
 * uart_16550.h - 16550A UART Driver Header (x86)
 *
 * Skeleton header for Phase 3 x86 UART driver.
 * Register definitions, bit fields, and function prototypes only.
 *
 * Reference: https://wiki.osdev.org/Serial_Ports
 *
 * JARVIS AI-OS - Phase 3 Pre-Work
 */

#ifndef JARVIS_UART_16550_H
#define JARVIS_UART_16550_H

#include <stdint.h>

/* ========================================================================
 * COM Port Base Addresses
 * ======================================================================== */

#define COM1_BASE   0x3F8
#define COM2_BASE   0x2F8
#define COM3_BASE   0x3E8
#define COM4_BASE   0x2E8

/* ========================================================================
 * Register Offsets (relative to port base)
 * ======================================================================== */

#define UART_RBR    0x00    /* Receive Buffer Register        (R,   DLAB=0) */
#define UART_THR    0x00    /* Transmit Holding Register      (W,   DLAB=0) */
#define UART_DLL    0x00    /* Divisor Latch Low Byte         (R/W, DLAB=1) */
#define UART_IER    0x01    /* Interrupt Enable Register      (R/W, DLAB=0) */
#define UART_DLH    0x01    /* Divisor Latch High Byte        (R/W, DLAB=1) */
#define UART_IIR    0x02    /* Interrupt Identification Reg   (R)           */
#define UART_FCR    0x02    /* FIFO Control Register          (W)           */
#define UART_LCR    0x03    /* Line Control Register          (R/W)         */
#define UART_MCR    0x04    /* Modem Control Register         (R/W)         */
#define UART_LSR    0x05    /* Line Status Register           (R)           */
#define UART_MSR    0x06    /* Modem Status Register          (R)           */
#define UART_SR     0x07    /* Scratch Register               (R/W)         */

/* ========================================================================
 * Line Control Register (LCR) - Offset +3
 * ======================================================================== */

/* Word length (bits 1:0) */
#define LCR_WORD_5BIT       0x00    /* 5 data bits */
#define LCR_WORD_6BIT       0x01    /* 6 data bits */
#define LCR_WORD_7BIT       0x02    /* 7 data bits */
#define LCR_WORD_8BIT       0x03    /* 8 data bits */
#define LCR_WORD_MASK       0x03

/* Stop bits (bit 2) */
#define LCR_STOP_1          0x00    /* 1 stop bit */
#define LCR_STOP_2          0x04    /* 1.5 (5-bit) or 2 (6/7/8-bit) stop bits */

/* Parity (bits 5:3) */
#define LCR_PARITY_NONE     0x00    /* No parity */
#define LCR_PARITY_ODD      0x08    /* Odd parity */
#define LCR_PARITY_EVEN     0x18    /* Even parity */
#define LCR_PARITY_MARK     0x28    /* Mark parity (always 1) */
#define LCR_PARITY_SPACE    0x38    /* Space parity (always 0) */
#define LCR_PARITY_MASK     0x38

/* Break control (bit 6) */
#define LCR_BREAK_ENABLE    0x40    /* Set break condition */

/* DLAB (bit 7) */
#define LCR_DLAB            0x80    /* Divisor Latch Access Bit */

/* ========================================================================
 * Line Status Register (LSR) - Offset +5  (Read-Only)
 * ======================================================================== */

#define LSR_DATA_READY      0x01    /* Bit 0: Data ready in RBR */
#define LSR_OVERRUN_ERR     0x02    /* Bit 1: Overrun error */
#define LSR_PARITY_ERR      0x04    /* Bit 2: Parity error */
#define LSR_FRAMING_ERR     0x08    /* Bit 3: Framing error */
#define LSR_BREAK_IND       0x10    /* Bit 4: Break indicator */
#define LSR_THR_EMPTY       0x20    /* Bit 5: THR is empty (ready to send) */
#define LSR_XMIT_EMPTY      0x40    /* Bit 6: THR + shift register both empty */
#define LSR_FIFO_ERR        0x80    /* Bit 7: Error in received FIFO */

/* ========================================================================
 * Modem Control Register (MCR) - Offset +4
 * ======================================================================== */

#define MCR_DTR             0x01    /* Bit 0: Data Terminal Ready */
#define MCR_RTS             0x02    /* Bit 1: Request To Send */
#define MCR_OUT1            0x04    /* Bit 2: Auxiliary output 1 */
#define MCR_OUT2            0x08    /* Bit 3: Auxiliary output 2 (IRQ enable) */
#define MCR_LOOPBACK        0x10    /* Bit 4: Loopback mode */

/* ========================================================================
 * Interrupt Enable Register (IER) - Offset +1
 * ======================================================================== */

#define IER_RX_DATA         0x01    /* Bit 0: Received data available */
#define IER_THR_EMPTY       0x02    /* Bit 1: THR empty */
#define IER_RX_LINE_STATUS  0x04    /* Bit 2: Receiver line status change */
#define IER_MODEM_STATUS    0x08    /* Bit 3: Modem status change */

/* ========================================================================
 * FIFO Control Register (FCR) - Offset +2  (Write-Only)
 * ======================================================================== */

#define FCR_ENABLE          0x01    /* Bit 0: Enable FIFOs */
#define FCR_CLEAR_RX        0x02    /* Bit 1: Clear receive FIFO */
#define FCR_CLEAR_TX        0x04    /* Bit 2: Clear transmit FIFO */
#define FCR_DMA_MODE        0x08    /* Bit 3: DMA mode select */

/* Interrupt trigger level (bits 7:6) */
#define FCR_TRIGGER_1       0x00    /* 1-byte trigger level */
#define FCR_TRIGGER_4       0x40    /* 4-byte trigger level */
#define FCR_TRIGGER_8       0x80    /* 8-byte trigger level */
#define FCR_TRIGGER_14      0xC0    /* 14-byte trigger level */
#define FCR_TRIGGER_MASK    0xC0

/* ========================================================================
 * Interrupt Identification Register (IIR) - Offset +2  (Read-Only)
 * ======================================================================== */

#define IIR_NO_INT_PENDING  0x01    /* Bit 0: 1 = no interrupt pending */

/* Interrupt ID (bits 3:1) - valid when bit 0 is 0 */
#define IIR_MODEM_STATUS    0x00    /* Priority 4 (lowest): modem status change */
#define IIR_THR_EMPTY       0x02    /* Priority 3: THR empty */
#define IIR_RX_DATA         0x04    /* Priority 2: received data available */
#define IIR_RX_LINE_STATUS  0x06    /* Priority 1 (highest): receiver line status */
#define IIR_CHAR_TIMEOUT    0x0C    /* Character timeout (FIFO mode) */
#define IIR_ID_MASK         0x0E

/* FIFO status (bits 7:6) */
#define IIR_FIFO_NONE       0x00    /* No FIFO */
#define IIR_FIFO_UNUSABLE   0x40    /* FIFO enabled but unusable (16450) */
#define IIR_FIFO_ENABLED    0xC0    /* FIFO enabled (16550A) */
#define IIR_FIFO_MASK       0xC0

/* ========================================================================
 * Modem Status Register (MSR) - Offset +6  (Read-Only)
 * ======================================================================== */

#define MSR_DCTS            0x01    /* Bit 0: Delta Clear To Send */
#define MSR_DDSR            0x02    /* Bit 1: Delta Data Set Ready */
#define MSR_TERI            0x04    /* Bit 2: Trailing Edge Ring Indicator */
#define MSR_DDCD            0x08    /* Bit 3: Delta Data Carrier Detect */
#define MSR_CTS             0x10    /* Bit 4: Clear To Send */
#define MSR_DSR             0x20    /* Bit 5: Data Set Ready */
#define MSR_RI              0x40    /* Bit 6: Ring Indicator */
#define MSR_DCD             0x80    /* Bit 7: Data Carrier Detect */

/* ========================================================================
 * Baud Rate Divisors
 *
 * UART base clock = 1.8432 MHz
 * Divisor = 115200 / desired_baud_rate
 * Written to DLL (low byte) and DLH (high byte) with DLAB=1
 * ======================================================================== */

#define UART_CLOCK_HZ       115200  /* Effective base rate for divisor calc */

#define BAUD_DIVISOR_115200 1       /* 115200 / 115200 */
#define BAUD_DIVISOR_57600  2       /* 115200 / 57600  */
#define BAUD_DIVISOR_38400  3       /* 115200 / 38400  */
#define BAUD_DIVISOR_19200  6       /* 115200 / 19200  */
#define BAUD_DIVISOR_9600   12      /* 115200 / 9600   */
#define BAUD_DIVISOR_4800   24      /* 115200 / 4800   */
#define BAUD_DIVISOR_2400   48      /* 115200 / 2400   */
#define BAUD_DIVISOR_1200   96      /* 115200 / 1200   */

/* ========================================================================
 * Function Prototypes
 * ======================================================================== */

/**
 * uart_16550_init - Initialize a 16550A UART port
 * @port:  I/O base address (e.g., COM1_BASE)
 * @baud:  Desired baud rate (e.g., 115200)
 *
 * Configures: 8N1, FIFO enabled (14-byte trigger), interrupts off.
 * Returns 0 on success, -1 on failure (e.g., loopback test fails).
 */
int  uart_16550_init(uint16_t port, uint32_t baud);

/**
 * uart_16550_putc - Transmit a single character (blocking)
 * @port:  I/O base address
 * @c:     Character to send
 *
 * Blocks until THR is empty, then writes character.
 */
void uart_16550_putc(uint16_t port, char c);

/**
 * uart_16550_getc - Receive a single character (blocking)
 * @port:  I/O base address
 *
 * Blocks until data is ready in RBR, then returns the character.
 */
char uart_16550_getc(uint16_t port);

/**
 * uart_16550_read_ready - Check if data is available to read
 * @port:  I/O base address
 *
 * Returns non-zero if LSR Data Ready bit is set.
 */
int  uart_16550_read_ready(uint16_t port);

/**
 * uart_16550_write_ready - Check if transmitter is ready
 * @port:  I/O base address
 *
 * Returns non-zero if LSR THR Empty bit is set.
 */
int  uart_16550_write_ready(uint16_t port);

/**
 * uart_16550_puts - Transmit a null-terminated string (blocking)
 * @port:  I/O base address
 * @s:     String to send
 */
void uart_16550_puts(uint16_t port, const char *s);

/**
 * uart_16550_write - Transmit a buffer of bytes (blocking)
 * @port:  I/O base address
 * @buf:   Data buffer
 * @len:   Number of bytes to send
 *
 * Returns number of bytes written.
 */
int  uart_16550_write(uint16_t port, const void *buf, int len);

/**
 * uart_16550_read - Receive bytes into a buffer with timeout
 * @port:       I/O base address
 * @buf:        Destination buffer
 * @len:        Maximum bytes to read
 * @timeout_ms: Timeout in milliseconds (0 = non-blocking poll)
 *
 * Returns number of bytes actually read.
 */
int  uart_16550_read(uint16_t port, void *buf, int len, int timeout_ms);

#endif /* JARVIS_UART_16550_H */
