/*
 * JARVIS AI-OS - BCM2835 SPI0 Driver
 *
 * Week 47: SPI master driver for Raspberry Pi 4 on seL4.
 * Uses SPI0 controller at paddr 0xFE204000.
 *
 * GPIO pins: CE1=GPIO7, CE0=GPIO8, MISO=GPIO9, MOSI=GPIO10, SCLK=GPIO11
 * (all ALT0). The GPIO driver must be initialized before spi_init().
 *
 * IMPORTANT: SPI0 at 0xFE204000 is in the 0xFE000000 device untyped.
 * It must be mapped AFTER UART (0xFE201000) and BEFORE EMMC (0xFE340000)
 * due to the forward-only watermark constraint.
 * Init order: UART -> SPI -> EMMC -> I2C -> USB.
 */

#ifndef BCM_SPI_H
#define BCM_SPI_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * SPI0 Controller Base (ARM physical address)
 * ================================================================ */
#define SPI0_BASE_PADDR     0xFE204000u

/* ================================================================
 * SPI Register Offsets (within 4KB page)
 * ================================================================ */
#define SPI_CS              0x00    /* Control and Status */
#define SPI_FIFO            0x04    /* TX/RX FIFO */
#define SPI_CLK             0x08    /* Clock Divider */
#define SPI_DLEN            0x0C    /* Data Length (DMA mode) */

/* ================================================================
 * CS Register Bits
 * ================================================================ */
#define SPI_CS_LEN_LONG     (1u << 25)
#define SPI_CS_DMA_LEN      (1u << 24)
#define SPI_CS_CSPOL2       (1u << 23)
#define SPI_CS_CSPOL1       (1u << 22)
#define SPI_CS_CSPOL0       (1u << 21)
#define SPI_CS_RXF          (1u << 20)  /* RX FIFO full */
#define SPI_CS_RXR          (1u << 19)  /* RX FIFO needs reading */
#define SPI_CS_TXD          (1u << 18)  /* TX FIFO can accept data */
#define SPI_CS_RXD          (1u << 17)  /* RX FIFO contains data */
#define SPI_CS_DONE         (1u << 16)  /* Transfer done */
#define SPI_CS_TA           (1u << 7)   /* Transfer Active */
#define SPI_CS_CSPOL        (1u << 6)   /* CS polarity */
#define SPI_CS_CLEAR_RX     (1u << 5)   /* Clear RX FIFO (self-clearing) */
#define SPI_CS_CLEAR_TX     (1u << 4)   /* Clear TX FIFO (self-clearing) */
#define SPI_CS_CPOL         (1u << 3)   /* Clock polarity */
#define SPI_CS_CPHA         (1u << 2)   /* Clock phase */
#define SPI_CS_CS_MASK      0x03u       /* Chip select bits 1:0 */

/* ================================================================
 * Chip Select Values
 * ================================================================ */
#define SPI_CS0             0
#define SPI_CS1             1

/* ================================================================
 * Error Codes
 * ================================================================ */
#define SPI_OK              0
#define SPI_ERR_NOT_INIT    -1
#define SPI_ERR_TIMEOUT     -2
#define SPI_ERR_PARAM       -3

/* Transfer timeout (microseconds) */
#define SPI_TIMEOUT_US      100000  /* 100ms */

/* ================================================================
 * Public API
 * ================================================================ */

/* Initialize SPI0 controller.
 * Maps MMIO page via uart_device_map_page(), configures GPIO 7-11 as ALT0,
 * sets default clock divider to 256 (~488 kHz), clears FIFOs.
 * Requires: uart_init() and gpio_init() already called.
 * Returns 0 on success, negative on error. */
int spi_init(void);

/* Set SPI clock divider.
 * SPI clock = 125 MHz / divider.
 * divider must be even and >= 2. 0 means divider of 65536.
 * Examples: 256 -> ~488 kHz, 64 -> ~1.95 MHz, 16 -> ~7.8 MHz.
 * Returns 0 on success, negative error code on failure. */
int spi_set_clock(uint32_t divider);

/* Full-duplex polled SPI transfer.
 * tx_buf: data to transmit (NULL to send zeros)
 * rx_buf: buffer to receive data (NULL to discard received data)
 * len: number of bytes to transfer
 * At least one of tx_buf/rx_buf must be non-NULL and len > 0.
 * Returns 0 on success, negative error code on failure. */
int spi_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len);

/* Select chip select line (SPI_CS0 or SPI_CS1).
 * Returns 0 on success, SPI_ERR_PARAM for invalid cs value. */
int spi_select(uint32_t cs);

/* Check if SPI driver is initialized. */
bool spi_is_initialized(void);

/* Get SPI status string for shell commands.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL). */
int spi_get_status(char *output, uint32_t output_size);

#endif /* BCM_SPI_H */
