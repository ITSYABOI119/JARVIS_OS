/*
 * JARVIS AI-OS - BCM2711 BSC I2C Driver
 *
 * Week 43: I2C master driver for Raspberry Pi 4 on seL4.
 * Uses BSC1 controller at paddr 0xFE804000.
 *
 * GPIO pins: SDA1 = GPIO 2 (ALT0), SCL1 = GPIO 3 (ALT0).
 * The GPIO driver must set these to ALT0 before i2c_init().
 *
 * IMPORTANT: BSC1 at 0xFE804000 is in the 0xFE000000 device untyped.
 * It must be mapped BEFORE USB (0xFE980000) due to the forward-only
 * watermark constraint. Init order: UART -> EMMC -> I2C -> USB.
 */

#ifndef BCM_I2C_H
#define BCM_I2C_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * BSC1 I2C Controller Base (ARM physical address)
 * ================================================================ */
#define I2C_BSC1_BASE           0xFE804000u
#define I2C_BSC1_VADDR          0       /* Auto-assign via uart_device_map_page */

/* ================================================================
 * BSC Register Offsets (within 4KB page)
 * ================================================================ */
#define BSC_C                   0x00    /* Control */
#define BSC_S                   0x04    /* Status */
#define BSC_DLEN                0x08    /* Data length */
#define BSC_A                   0x0C    /* Slave address */
#define BSC_FIFO                0x10    /* Data FIFO */
#define BSC_DIV                 0x14    /* Clock divider */
#define BSC_DEL                 0x18    /* Data delay */
#define BSC_CLKT                0x1C    /* Clock stretch timeout */

/* ================================================================
 * BSC_C (Control) Bits
 * ================================================================ */
#define BSC_C_I2CEN             (1u << 15)  /* I2C Enable */
#define BSC_C_INTR              (1u << 10)  /* Interrupt on RX */
#define BSC_C_INTT              (1u << 9)   /* Interrupt on TX */
#define BSC_C_INTD              (1u << 8)   /* Interrupt on DONE */
#define BSC_C_ST                (1u << 7)   /* Start transfer */
#define BSC_C_CLEAR_FIFO        (3u << 4)   /* Clear FIFO (write bits 5:4) */
#define BSC_C_READ              (1u << 0)   /* Read transfer */

/* ================================================================
 * BSC_S (Status) Bits
 * ================================================================ */
#define BSC_S_CLKT              (1u << 9)   /* Clock stretch timeout */
#define BSC_S_ERR               (1u << 8)   /* ACK error */
#define BSC_S_RXF               (1u << 7)   /* FIFO full */
#define BSC_S_TXE               (1u << 6)   /* FIFO empty */
#define BSC_S_RXD               (1u << 5)   /* FIFO has data */
#define BSC_S_TXD               (1u << 4)   /* FIFO can accept data */
#define BSC_S_RXR               (1u << 3)   /* FIFO needs reading */
#define BSC_S_TXW               (1u << 2)   /* FIFO needs writing */
#define BSC_S_DONE              (1u << 1)   /* Transfer done */
#define BSC_S_TA                (1u << 0)   /* Transfer active */

/* ================================================================
 * Clock Divider Values
 *
 * I2C clock = core_clk / DIV
 * BCM2711 core_clk defaults to 150 MHz.
 * DIV=1500 -> 100 kHz (standard mode)
 * DIV=375  -> 400 kHz (fast mode)
 * ================================================================ */
#define I2C_DIV_100KHZ          1500
#define I2C_DIV_400KHZ          375

/* Transfer timeout (microseconds) */
#define I2C_TIMEOUT_US          100000  /* 100ms */

/* ================================================================
 * Error Codes
 * ================================================================ */
#define I2C_OK                  0
#define I2C_ERR_NACK            -1
#define I2C_ERR_CLKT            -2
#define I2C_ERR_TIMEOUT         -3
#define I2C_ERR_NOT_INIT        -4

/* ================================================================
 * Public API
 * ================================================================ */

/* Initialize BSC1 I2C controller.
 * Maps MMIO page via uart_device_map_page(), sets clock to 100kHz.
 * Requires: uart_init() and gpio_init() already called.
 * GPIO 2/3 must be set to ALT0 before calling this.
 * Returns 0 on success, negative on error. */
int i2c_init(void);

/* Write data to an I2C slave device.
 * addr: 7-bit slave address (0x00-0x7F)
 * data: pointer to bytes to write
 * len: number of bytes to write
 * Returns 0 on success, negative error code on failure. */
int i2c_write(uint8_t addr, const uint8_t *data, uint32_t len);

/* Read data from an I2C slave device.
 * addr: 7-bit slave address
 * data: buffer to receive bytes
 * len: number of bytes to read
 * Returns 0 on success, negative error code on failure. */
int i2c_read(uint8_t addr, uint8_t *data, uint32_t len);

/* Write then read in a single I2C transaction (repeated start).
 * Common pattern: write register address, then read register value.
 * addr: 7-bit slave address
 * wdata: bytes to write (e.g., register address)
 * wlen: number of bytes to write
 * rdata: buffer to receive bytes
 * rlen: number of bytes to read
 * Returns 0 on success, negative error code on failure. */
int i2c_write_read(uint8_t addr, const uint8_t *wdata, uint32_t wlen,
                   uint8_t *rdata, uint32_t rlen);

/* Scan I2C bus for connected devices.
 * Tries reading 1 byte from each address 0x03-0x77.
 * Writes results to output buffer (formatted text).
 * Returns bytes written (excluding NUL). */
int i2c_scan(char *output, uint32_t output_size);

/* Check if I2C driver is initialized. */
bool i2c_is_initialized(void);

#endif /* BCM_I2C_H */
