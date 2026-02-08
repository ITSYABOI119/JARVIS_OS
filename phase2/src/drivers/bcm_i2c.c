/*
 * JARVIS AI-OS - BCM2711 BSC I2C Driver Implementation
 *
 * Week 43: I2C master driver for Raspberry Pi 4 on seL4.
 *
 * Uses BSC1 controller at paddr 0xFE804000, mapped via
 * uart_device_map_page() (same pattern as usb_hid.c).
 *
 * IMPORTANT: This driver must be initialized BEFORE usb_hid
 * because 0xFE804000 < 0xFE980000 and the device untyped
 * watermark is forward-only.
 *
 * GPIO 2 (SDA1) and GPIO 3 (SCL1) must be set to ALT0
 * via gpio_set_mode() before calling i2c_init().
 */

#include "bcm_i2c.h"
#include "bcm_gpio.h"
#include "uart_pl011.h"
#include "bcm2711_timer.h"

#include <sel4/sel4.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Internal State
 * ================================================================ */

static volatile uint32_t *i2c_base = NULL;
static bool i2c_initialized = false;

/* ================================================================
 * Debug Output (via seL4 kernel debug port)
 * ================================================================ */

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

static void debug_hex(seL4_Word val)
{
    char buf[17];
    const char *hex = "0123456789abcdef";
    buf[16] = '\0';
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[val & 0xf];
        val >>= 4;
    }
    debug_puts("0x");
    debug_puts(buf);
}

/* ================================================================
 * Register Access
 * ================================================================ */

static inline uint32_t i2c_reg_read(uint32_t offset)
{
    uint32_t val = *(volatile uint32_t *)((uintptr_t)i2c_base + offset);
    __asm__ volatile("dsb sy" ::: "memory");
    return val;
}

static inline void i2c_reg_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)i2c_base + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ================================================================
 * Delay Helper
 * ================================================================ */

static void i2c_delay_us(uint32_t us)
{
    if (systimer_is_initialized()) {
        uint64_t start = systimer_read();
        while ((systimer_read() - start) < (uint64_t)us) {
            /* spin */
        }
    } else {
        for (volatile uint32_t i = 0; i < us * 2; i++);
    }
}

/* ================================================================
 * Internal: Wait for transfer completion
 * ================================================================ */

static int i2c_wait_done(void)
{
    uint32_t timeout = I2C_TIMEOUT_US;

    while (timeout > 0) {
        uint32_t status = i2c_reg_read(BSC_S);

        if (status & BSC_S_ERR) {
            /* NACK error - clear it */
            i2c_reg_write(BSC_S, BSC_S_ERR);
            return I2C_ERR_NACK;
        }

        if (status & BSC_S_CLKT) {
            /* Clock stretch timeout - clear it */
            i2c_reg_write(BSC_S, BSC_S_CLKT);
            return I2C_ERR_CLKT;
        }

        if (status & BSC_S_DONE) {
            /* Transfer complete - clear DONE */
            i2c_reg_write(BSC_S, BSC_S_DONE);
            return I2C_OK;
        }

        i2c_delay_us(10);
        timeout -= 10;
    }

    return I2C_ERR_TIMEOUT;
}

/* ================================================================
 * Public API
 * ================================================================ */

int i2c_init(void)
{
    if (i2c_initialized) {
        return 0;
    }

    debug_puts("[I2C] Initializing BSC1 controller\n");

    /* Configure GPIO 2 and 3 for I2C1 (ALT0) with pull-ups */
    if (gpio_is_initialized()) {
        gpio_set_mode(2, GPIO_FSEL_ALT0);
        gpio_set_mode(3, GPIO_FSEL_ALT0);
        gpio_set_pull(2, GPIO_PULL_UP);
        gpio_set_pull(3, GPIO_PULL_UP);
        debug_puts("[I2C] GPIO 2/3 set to ALT0 with pull-up\n");
    } else {
        debug_puts("[I2C] WARNING: GPIO not initialized, assuming pins already configured\n");
    }

    /* Map BSC1 MMIO page via shared device mapper */
    volatile uint32_t *mapped = NULL;
    if (!uart_device_map_page(I2C_BSC1_BASE, I2C_BSC1_VADDR, &mapped)) {
        debug_puts("[I2C] ERROR: Failed to map BSC1 at paddr=");
        debug_hex(I2C_BSC1_BASE);
        debug_puts("\n");
        return -1;
    }

    i2c_base = mapped;

    debug_puts("[I2C] BSC1 mapped: paddr=");
    debug_hex(I2C_BSC1_BASE);
    debug_puts(" -> vaddr=");
    debug_hex((seL4_Word)i2c_base);
    debug_puts("\n");

    /* Clear FIFO and disable controller */
    i2c_reg_write(BSC_C, BSC_C_CLEAR_FIFO);
    i2c_delay_us(10);

    /* Clear status flags */
    i2c_reg_write(BSC_S, BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE);

    /* Set clock divider for 100 kHz (standard mode) */
    i2c_reg_write(BSC_DIV, I2C_DIV_100KHZ);

    /* Set clock stretch timeout (reasonable default) */
    i2c_reg_write(BSC_CLKT, 0x40);

    /* Enable I2C controller */
    i2c_reg_write(BSC_C, BSC_C_I2CEN);

    i2c_initialized = true;
    debug_puts("[I2C] BSC1 initialized at 100 kHz\n");
    return 0;
}

int i2c_write(uint8_t addr, const uint8_t *data, uint32_t len)
{
    if (!i2c_initialized) {
        return I2C_ERR_NOT_INIT;
    }
    if (!data || len == 0) {
        return I2C_ERR_NACK;
    }

    /* Clear status flags */
    i2c_reg_write(BSC_S, BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE);

    /* Set slave address */
    i2c_reg_write(BSC_A, addr & 0x7F);

    /* Set data length */
    i2c_reg_write(BSC_DLEN, len);

    /* Clear FIFO, then fill with data */
    i2c_reg_write(BSC_C, BSC_C_CLEAR_FIFO);
    i2c_delay_us(5);

    /* Fill FIFO (up to 16 bytes initially) */
    uint32_t i = 0;
    while (i < len && i < 16) {
        i2c_reg_write(BSC_FIFO, data[i]);
        i++;
    }

    /* Start write transfer: I2CEN + ST (no READ bit = write) */
    i2c_reg_write(BSC_C, BSC_C_I2CEN | BSC_C_ST);

    /* Continue filling FIFO as space becomes available */
    while (i < len) {
        uint32_t status = i2c_reg_read(BSC_S);
        if (status & (BSC_S_ERR | BSC_S_CLKT)) {
            break;
        }
        if (status & BSC_S_TXD) {
            i2c_reg_write(BSC_FIFO, data[i]);
            i++;
        }
    }

    return i2c_wait_done();
}

int i2c_read(uint8_t addr, uint8_t *data, uint32_t len)
{
    if (!i2c_initialized) {
        return I2C_ERR_NOT_INIT;
    }
    if (!data || len == 0) {
        return I2C_ERR_NACK;
    }

    /* Clear status flags */
    i2c_reg_write(BSC_S, BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE);

    /* Set slave address */
    i2c_reg_write(BSC_A, addr & 0x7F);

    /* Set data length */
    i2c_reg_write(BSC_DLEN, len);

    /* Start read transfer: I2CEN + ST + READ */
    i2c_reg_write(BSC_C, BSC_C_I2CEN | BSC_C_ST | BSC_C_READ);

    /* Read data from FIFO as it arrives */
    uint32_t i = 0;
    uint32_t timeout = I2C_TIMEOUT_US;

    while (i < len && timeout > 0) {
        uint32_t status = i2c_reg_read(BSC_S);

        if (status & BSC_S_ERR) {
            i2c_reg_write(BSC_S, BSC_S_ERR);
            return I2C_ERR_NACK;
        }
        if (status & BSC_S_CLKT) {
            i2c_reg_write(BSC_S, BSC_S_CLKT);
            return I2C_ERR_CLKT;
        }

        if (status & BSC_S_RXD) {
            data[i] = (uint8_t)(i2c_reg_read(BSC_FIFO) & 0xFF);
            i++;
            timeout = I2C_TIMEOUT_US; /* Reset timeout on data */
        } else {
            i2c_delay_us(10);
            timeout -= 10;
        }
    }

    if (i < len) {
        return I2C_ERR_TIMEOUT;
    }

    /* Wait for DONE */
    int ret = i2c_wait_done();
    return ret;
}

int i2c_write_read(uint8_t addr, const uint8_t *wdata, uint32_t wlen,
                   uint8_t *rdata, uint32_t rlen)
{
    if (!i2c_initialized) {
        return I2C_ERR_NOT_INIT;
    }

    /* Write phase first */
    int ret = i2c_write(addr, wdata, wlen);
    if (ret != I2C_OK) {
        return ret;
    }

    /* Short delay between write and read */
    i2c_delay_us(50);

    /* Read phase */
    return i2c_read(addr, rdata, rlen);
}

int i2c_scan(char *output, uint32_t output_size)
{
    if (!output || output_size < 128) {
        return 0;
    }

    if (!i2c_initialized) {
        return snprintf(output, output_size, "I2C: not initialized\n");
    }

    int pos = 0;
    int found = 0;

    pos += snprintf(output + pos, output_size - pos,
                    "I2C Bus Scan (BSC1)\n");
    pos += snprintf(output + pos, output_size - pos,
                    "     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    for (uint8_t row = 0; row < 8; row++) {
        if ((uint32_t)pos >= output_size - 64) break;

        pos += snprintf(output + pos, output_size - pos,
                        "%02x: ", row * 16);

        for (uint8_t col = 0; col < 16; col++) {
            uint8_t addr = row * 16 + col;

            /* Skip reserved addresses: 0x00-0x02 and 0x78-0x7F */
            if (addr < 0x03 || addr > 0x77) {
                pos += snprintf(output + pos, output_size - pos, "   ");
                continue;
            }

            /* Try reading 1 byte from this address */
            uint8_t dummy;

            /* Clear status first */
            i2c_reg_write(BSC_S, BSC_S_CLKT | BSC_S_ERR | BSC_S_DONE);
            i2c_reg_write(BSC_A, addr);
            i2c_reg_write(BSC_DLEN, 1);
            i2c_reg_write(BSC_C, BSC_C_I2CEN | BSC_C_ST | BSC_C_READ);

            /* Short timeout for scan */
            uint32_t timeout = 5000; /* 5ms */
            bool got_ack = false;

            while (timeout > 0) {
                uint32_t status = i2c_reg_read(BSC_S);
                if (status & BSC_S_ERR) {
                    i2c_reg_write(BSC_S, BSC_S_ERR | BSC_S_DONE);
                    break;
                }
                if (status & BSC_S_DONE) {
                    /* Read any data in FIFO */
                    if (status & BSC_S_RXD) {
                        dummy = (uint8_t)(i2c_reg_read(BSC_FIFO) & 0xFF);
                        (void)dummy;
                    }
                    i2c_reg_write(BSC_S, BSC_S_DONE);
                    got_ack = true;
                    break;
                }
                if (status & BSC_S_RXD) {
                    dummy = (uint8_t)(i2c_reg_read(BSC_FIFO) & 0xFF);
                    (void)dummy;
                    got_ack = true;
                }
                i2c_delay_us(10);
                timeout -= 10;
            }

            if (got_ack) {
                pos += snprintf(output + pos, output_size - pos,
                                "%02x ", addr);
                found++;
            } else {
                pos += snprintf(output + pos, output_size - pos, "-- ");
            }
        }

        pos += snprintf(output + pos, output_size - pos, "\n");
    }

    pos += snprintf(output + pos, output_size - pos,
                    "%d device(s) found\n", found);

    return pos;
}

bool i2c_is_initialized(void)
{
    return i2c_initialized;
}
