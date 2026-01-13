/*
 * JARVIS AI-OS - PL011 UART Driver for Raspberry Pi 4
 * Phase 2 Week 31-32
 *
 * BCM2711 PL011 UART driver for seL4 on Raspberry Pi 4.
 * Used for Python<->seL4 IPC via USB-serial adapter.
 *
 * Hardware: BCM2711 ARM Peripherals
 * Reference: BCM2711 ARM Peripherals datasheet, Chapter 11 (UART)
 */

#ifndef UART_PL011_H
#define UART_PL011_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * BCM2711 Memory Map (Pi 4)
 *
 * The BCM2711 uses a different peripheral base than Pi 3:
 * - Pi 3 (BCM2837): 0x3F000000
 * - Pi 4 (BCM2711): 0xFE000000
 *
 * UART0 (PL011) is at offset 0x201000 from peripheral base.
 * GPIO is at offset 0x200000 from peripheral base.
 */
#define BCM2711_PERI_BASE       0xFE000000
#define UART0_BASE              (BCM2711_PERI_BASE + 0x201000)
#define GPIO_BASE               (BCM2711_PERI_BASE + 0x200000)

/*
 * BCM2711 GPIO Pull-Up/Down Registers
 *
 * Unlike BCM2835/BCM2837, BCM2711 uses a simpler pull-up/down mechanism:
 * - GPIO_PUP_PDN_CNTRL_REG0-3 at offsets 0xE4, 0xE8, 0xEC, 0xF0
 * - Each register controls 16 GPIOs (2 bits per GPIO)
 * - Values: 00=none, 01=pull-up, 10=pull-down, 11=reserved
 */
#define GPIO_PUP_PDN_CNTRL_REG0 0xE4    /* GPIO 0-15 */
#define GPIO_PUP_PDN_CNTRL_REG1 0xE8    /* GPIO 16-31 */
#define GPIO_PUP_PDN_CNTRL_REG2 0xEC    /* GPIO 32-47 */
#define GPIO_PUP_PDN_CNTRL_REG3 0xF0    /* GPIO 48-57 */
#define GPIO_GPFSEL1            0x04    /* GPIO 10-19 function select */
#define GPIO_GPLEV0             0x34    /* GPIO 0-31 level */

/* Pull-up/down values (2 bits per GPIO) */
#define GPIO_PUD_NONE           0x0
#define GPIO_PUD_UP             0x1
#define GPIO_PUD_DOWN           0x2
#define GPIO_FSEL_ALT0          0x4

/*
 * PL011 Register Offsets
 *
 * The PL011 is an ARM PrimeCell UART with the following registers:
 */
#define UART_DR                 0x00    /* Data Register */
#define UART_RSRECR             0x04    /* Receive Status/Error Clear */
#define UART_FR                 0x18    /* Flag Register */
#define UART_ILPR               0x20    /* IrDA Low-Power Counter (unused) */
#define UART_IBRD               0x24    /* Integer Baud Rate Divisor */
#define UART_FBRD               0x28    /* Fractional Baud Rate Divisor */
#define UART_LCRH               0x2C    /* Line Control Register */
#define UART_CR                 0x30    /* Control Register */
#define UART_IFLS               0x34    /* Interrupt FIFO Level Select */
#define UART_IMSC               0x38    /* Interrupt Mask Set/Clear */
#define UART_RIS                0x3C    /* Raw Interrupt Status */
#define UART_MIS                0x40    /* Masked Interrupt Status */
#define UART_ICR                0x44    /* Interrupt Clear Register */
#define UART_DMACR              0x48    /* DMA Control Register */

/*
 * Flag Register (UART_FR) Bits
 */
#define UART_FR_RI              (1 << 8)    /* Ring Indicator */
#define UART_FR_TXFE            (1 << 7)    /* TX FIFO Empty */
#define UART_FR_RXFF            (1 << 6)    /* RX FIFO Full */
#define UART_FR_TXFF            (1 << 5)    /* TX FIFO Full */
#define UART_FR_RXFE            (1 << 4)    /* RX FIFO Empty */
#define UART_FR_BUSY            (1 << 3)    /* UART Busy */
#define UART_FR_DCD             (1 << 2)    /* Data Carrier Detect */
#define UART_FR_DSR             (1 << 1)    /* Data Set Ready */
#define UART_FR_CTS             (1 << 0)    /* Clear To Send */

/*
 * Line Control Register (UART_LCRH) Bits
 */
#define UART_LCRH_SPS           (1 << 7)    /* Stick Parity Select */
#define UART_LCRH_WLEN_8        (3 << 5)    /* 8-bit word length */
#define UART_LCRH_WLEN_7        (2 << 5)    /* 7-bit word length */
#define UART_LCRH_WLEN_6        (1 << 5)    /* 6-bit word length */
#define UART_LCRH_WLEN_5        (0 << 5)    /* 5-bit word length */
#define UART_LCRH_FEN           (1 << 4)    /* FIFO Enable */
#define UART_LCRH_STP2          (1 << 3)    /* Two Stop Bits */
#define UART_LCRH_EPS           (1 << 2)    /* Even Parity Select */
#define UART_LCRH_PEN           (1 << 1)    /* Parity Enable */
#define UART_LCRH_BRK           (1 << 0)    /* Send Break */

/*
 * Control Register (UART_CR) Bits
 */
#define UART_CR_CTSEN           (1 << 15)   /* CTS Hardware Flow Control */
#define UART_CR_RTSEN           (1 << 14)   /* RTS Hardware Flow Control */
#define UART_CR_RTS             (1 << 11)   /* Request To Send */
#define UART_CR_DTR             (1 << 10)   /* Data Transmit Ready */
#define UART_CR_RXE             (1 << 9)    /* Receive Enable */
#define UART_CR_TXE             (1 << 8)    /* Transmit Enable */
#define UART_CR_LBE             (1 << 7)    /* Loopback Enable */
#define UART_CR_UARTEN          (1 << 0)    /* UART Enable */

/*
 * Interrupt Bits (IMSC, RIS, MIS, ICR)
 */
#define UART_INT_OE             (1 << 10)   /* Overrun Error */
#define UART_INT_BE             (1 << 9)    /* Break Error */
#define UART_INT_PE             (1 << 8)    /* Parity Error */
#define UART_INT_FE             (1 << 7)    /* Framing Error */
#define UART_INT_RT             (1 << 6)    /* Receive Timeout */
#define UART_INT_TX             (1 << 5)    /* Transmit */
#define UART_INT_RX             (1 << 4)    /* Receive */
#define UART_INT_DSRM           (1 << 3)    /* DSR Modem */
#define UART_INT_DCDM           (1 << 2)    /* DCD Modem */
#define UART_INT_CTSM           (1 << 1)    /* CTS Modem */
#define UART_INT_RIM            (1 << 0)    /* RI Modem */

/*
 * Default Configuration
 */
#define UART_DEFAULT_BAUD       115200
#define UART_CLOCK_HZ           48000000    /* 48 MHz UART clock on Pi 4 */

/*
 * IPC Buffer Sizes
 */
#define UART_RX_BUFFER_SIZE     1024
#define UART_TX_BUFFER_SIZE     1024
#define UART_MAX_MESSAGE_SIZE   256

/*
 * UART Driver State
 */
typedef struct {
    volatile uint32_t *base;        /* Memory-mapped register base */
    uint32_t baud_rate;             /* Current baud rate */
    bool initialized;               /* Initialization flag */

    /* Statistics */
    uint64_t tx_bytes;              /* Total bytes transmitted */
    uint64_t rx_bytes;              /* Total bytes received */
    uint32_t rx_errors;             /* Receive errors (overrun, framing, etc.) */
    uint32_t tx_errors;             /* Transmit errors */
} uart_state_t;

/*
 * Function Prototypes
 */

/**
 * Initialize UART with default settings (115200 8N1)
 * @return true on success, false on failure
 */
bool uart_init(void);

/**
 * Map UART device frame for direct MMIO access (Week 33)
 * This enables RX by mapping the PL011 registers into virtual memory.
 * Called automatically by uart_init_baud().
 * @return true on success, false on failure
 */
bool uart_map_device_frame(void);

/**
 * Initialize UART with custom baud rate
 * @param baud_rate Desired baud rate (e.g., 115200, 9600)
 * @return true on success, false on failure
 */
bool uart_init_baud(uint32_t baud_rate);

/**
 * Shutdown UART and release resources
 */
void uart_shutdown(void);

/**
 * Transmit a single character (blocking)
 * @param c Character to send
 */
void uart_putc(char c);

/**
 * Receive a single character (blocking)
 * @return Received character
 */
char uart_getc(void);

/**
 * Check if receive data is available (non-blocking)
 * @return true if data available, false otherwise
 */
bool uart_rx_ready(void);

/**
 * Check if transmit buffer has space (non-blocking)
 * @return true if ready to transmit, false if FIFO full
 */
bool uart_tx_ready(void);

/**
 * Transmit a null-terminated string
 * @param s String to send (will add \r before \n)
 */
void uart_puts(const char *s);

/**
 * Transmit raw bytes (no newline conversion)
 * @param data Pointer to data buffer
 * @param len Number of bytes to send
 * @return Number of bytes sent
 */
size_t uart_write(const uint8_t *data, size_t len);

/**
 * Read GPIO15 level (1=high, 0=low, -1=unavailable)
 */
int uart_gpio15_level(void);

/**
 * Dump UART register state for diagnostics
 * @param tag Label printed before the register dump (may be NULL)
 */
void uart_dump_regs(const char *tag);

/**
 * Get UART receive error status (RSRECR)
 * @return RSRECR value (0 if no errors or MMIO unmapped)
 */
uint32_t uart_rx_error_status(void);

/**
 * Clear UART receive error status (RSRECR)
 */
void uart_clear_rx_errors(void);

/**
 * Drain up to max_bytes from RX FIFO (non-blocking)
 * @param max_bytes Maximum bytes to discard
 */
void uart_rx_drain(int max_bytes);

/**
 * Read bytes with timeout
 * @param buf Buffer to store received data
 * @param max_len Maximum bytes to read
 * @param timeout_ms Timeout in milliseconds (0 = non-blocking)
 * @return Number of bytes read, or -1 on error
 */
int uart_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms);

/**
 * Read a line (until \r or \n) with timeout
 * @param buf Buffer to store line (null-terminated)
 * @param max_len Maximum buffer size including null terminator
 * @param timeout_ms Timeout in milliseconds
 * @return Number of characters read (excluding null), or -1 on timeout
 */
int uart_read_line(char *buf, size_t max_len, uint32_t timeout_ms);

/**
 * Flush transmit buffer (wait for all data to be sent)
 */
void uart_flush_tx(void);

/**
 * Flush receive buffer (discard pending data)
 */
void uart_flush_rx(void);

/**
 * Get UART statistics
 * @param tx_bytes Pointer to store TX byte count (can be NULL)
 * @param rx_bytes Pointer to store RX byte count (can be NULL)
 * @param rx_errors Pointer to store RX error count (can be NULL)
 */
void uart_get_stats(uint64_t *tx_bytes, uint64_t *rx_bytes, uint32_t *rx_errors);

/**
 * Print UART status for debugging
 */
void uart_print_status(void);

/**
 * Check if UART RX is enabled (Week 33)
 * @return true if RX is enabled (device frame mapped), false otherwise
 */
bool uart_is_rx_enabled(void);

#endif /* UART_PL011_H */
