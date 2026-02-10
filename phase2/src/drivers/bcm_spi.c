/*
 * JARVIS AI-OS - BCM2835 SPI0 Driver Implementation
 *
 * Week 47: SPI master driver for Raspberry Pi 4 on seL4.
 *
 * Uses SPI0 controller at paddr 0xFE204000, mapped via
 * uart_device_map_page() (same pattern as bcm_i2c.c).
 *
 * SPI0 is in the 0xFE000000 device untyped, so it shares the
 * device mapper with UART/EMMC/I2C/USB. Must be mapped AFTER
 * UART (0xFE201000) and BEFORE EMMC (0xFE340000).
 *
 * GPIO pins: CE1=GPIO7, CE0=GPIO8, MISO=GPIO9, MOSI=GPIO10,
 * SCLK=GPIO11 (all ALT0).
 *
 * SPI clock = core_clk / CDIV = 125 MHz / CDIV.
 * Default CDIV=256 gives ~488 kHz.
 */

#include "bcm_spi.h"
#include "bcm_gpio.h"
#include "uart_pl011.h"
#include "bcm2711_timer.h"

#include <sel4/sel4.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Internal State
 * ================================================================ */

static volatile uint32_t *spi_base = NULL;
static bool spi_initialized = false;

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

static inline uint32_t spi_reg_read(uint32_t offset)
{
    uint32_t val = *(volatile uint32_t *)((uintptr_t)spi_base + offset);
    __asm__ volatile("dsb sy" ::: "memory");
    return val;
}

static inline void spi_reg_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)spi_base + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ================================================================
 * Delay Helper
 * ================================================================ */

static void spi_delay_us(uint32_t us)
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
 * Public API
 * ================================================================ */

int spi_init(void)
{
    if (spi_initialized) {
        return 0;
    }

    debug_puts("[SPI] Initializing SPI0 controller\n");

    /* Check device mapper is ready */
    if (!uart_device_map_ready()) {
        debug_puts("[SPI] ERROR: device mapper not ready\n");
        return -1;
    }

    /* Configure GPIO pins for SPI0 (ALT0) */
    if (gpio_is_initialized()) {
        gpio_set_mode(7, GPIO_FSEL_ALT0);   /* CE1 */
        gpio_set_mode(8, GPIO_FSEL_ALT0);   /* CE0 */
        gpio_set_mode(9, GPIO_FSEL_ALT0);   /* MISO */
        gpio_set_mode(10, GPIO_FSEL_ALT0);  /* MOSI */
        gpio_set_mode(11, GPIO_FSEL_ALT0);  /* SCLK */
        debug_puts("[SPI] GPIO 7-11 set to ALT0\n");
    } else {
        debug_puts("[SPI] WARNING: GPIO not initialized, assuming pins already configured\n");
    }

    /* Map SPI0 MMIO page via shared device mapper */
    debug_puts("[SPI] Mapping SPI0 at paddr ");
    debug_hex(SPI0_BASE_PADDR);
    debug_puts("\n");

    volatile uint32_t *mapped = NULL;
    if (!uart_device_map_page(SPI0_BASE_PADDR, 0, &mapped)) {
        debug_puts("[SPI] ERROR: failed to map SPI0\n");
        return -1;
    }

    spi_base = mapped;

    debug_puts("[SPI] SPI0 mapped at vaddr ");
    debug_hex((seL4_Word)(uintptr_t)spi_base);
    debug_puts("\n");

    /* Clear FIFOs */
    spi_reg_write(SPI_CS, SPI_CS_CLEAR_TX | SPI_CS_CLEAR_RX);
    spi_delay_us(10);

    /* Set default clock divider: 125 MHz / 256 = ~488 kHz */
    spi_reg_write(SPI_CLK, 256);

    /* CS register: CS0 selected, CPOL=0, CPHA=0, no interrupts */
    spi_reg_write(SPI_CS, 0);

    spi_initialized = true;
    debug_puts("[SPI] Init OK (488 kHz, CS0)\n");
    return 0;
}

int spi_set_clock(uint32_t divider)
{
    if (!spi_initialized) {
        return SPI_ERR_NOT_INIT;
    }

    /* CDIV=0 is valid (means 65536). Otherwise must be even and >= 2. */
    if (divider != 0 && (divider < 2 || (divider & 1))) {
        return SPI_ERR_PARAM;
    }

    spi_reg_write(SPI_CLK, divider);
    return SPI_OK;
}

int spi_transfer(const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len)
{
    if (!spi_initialized) {
        return SPI_ERR_NOT_INIT;
    }

    if (len == 0 || (!tx_buf && !rx_buf)) {
        return SPI_ERR_PARAM;
    }

    /* Clear FIFOs and set Transfer Active */
    uint32_t cs = spi_reg_read(SPI_CS);
    cs &= ~SPI_CS_DONE;   /* Clear DONE (write 0 is no-op, but be explicit) */
    cs |= SPI_CS_CLEAR_TX | SPI_CS_CLEAR_RX | SPI_CS_TA;
    spi_reg_write(SPI_CS, cs);

    uint32_t tx_count = 0;
    uint32_t rx_count = 0;
    uint32_t timeout = SPI_TIMEOUT_US;
    bool use_timer = systimer_is_initialized();
    uint64_t start = 0;

    if (use_timer) {
        start = systimer_read();
    }

    /* Transfer loop: write TX bytes when FIFO has space,
     * read RX bytes when FIFO has data */
    while ((rx_count < len) && (timeout > 0)) {
        uint32_t status = spi_reg_read(SPI_CS);

        /* Write TX data while FIFO can accept and we have data */
        while (tx_count < len && (status & SPI_CS_TXD)) {
            uint8_t byte = tx_buf ? tx_buf[tx_count] : 0x00;
            spi_reg_write(SPI_FIFO, byte);
            tx_count++;
            status = spi_reg_read(SPI_CS);
        }

        /* Read RX data while FIFO has data */
        while (rx_count < len && (status & SPI_CS_RXD)) {
            uint8_t byte = (uint8_t)(spi_reg_read(SPI_FIFO) & 0xFF);
            if (rx_buf) {
                rx_buf[rx_count] = byte;
            }
            rx_count++;
            status = spi_reg_read(SPI_CS);
        }

        /* Update timeout */
        if (use_timer) {
            uint64_t elapsed = systimer_read() - start;
            if (elapsed >= (uint64_t)SPI_TIMEOUT_US) {
                timeout = 0;
            }
        } else {
            timeout--;
        }
    }

    /* Wait for DONE bit */
    if (timeout > 0) {
        uint32_t done_timeout = 10000;
        while (done_timeout > 0) {
            uint32_t status = spi_reg_read(SPI_CS);
            if (status & SPI_CS_DONE) {
                break;
            }
            spi_delay_us(1);
            done_timeout--;
        }

        /* Drain any remaining RX bytes after DONE */
        while (rx_count < len) {
            uint32_t status = spi_reg_read(SPI_CS);
            if (!(status & SPI_CS_RXD)) {
                break;
            }
            uint8_t byte = (uint8_t)(spi_reg_read(SPI_FIFO) & 0xFF);
            if (rx_buf) {
                rx_buf[rx_count] = byte;
            }
            rx_count++;
        }
    }

    /* Clear TA (deassert CS) */
    cs = spi_reg_read(SPI_CS);
    cs &= ~SPI_CS_TA;
    spi_reg_write(SPI_CS, cs);

    if (timeout == 0) {
        return SPI_ERR_TIMEOUT;
    }

    return SPI_OK;
}

int spi_select(uint32_t cs)
{
    if (!spi_initialized) {
        return SPI_ERR_NOT_INIT;
    }

    if (cs > SPI_CS1) {
        return SPI_ERR_PARAM;
    }

    uint32_t reg = spi_reg_read(SPI_CS);
    reg = (reg & ~SPI_CS_CS_MASK) | (cs & SPI_CS_CS_MASK);
    spi_reg_write(SPI_CS, reg);

    return SPI_OK;
}

bool spi_is_initialized(void)
{
    return spi_initialized;
}

int spi_get_status(char *output, uint32_t output_size)
{
    if (!spi_initialized) {
        return snprintf(output, output_size, "SPI: not initialized\n");
    }

    int n = 0;
    uint32_t cs = spi_reg_read(SPI_CS);
    uint32_t clk = spi_reg_read(SPI_CLK);

    n += snprintf(output + n, output_size - (uint32_t)n,
                  "SPI0: initialized\n");
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  Clock divider: %u (~%u Hz)\n",
                  clk, clk ? 125000000 / clk : 0);
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  Chip select: CS%u\n", cs & SPI_CS_CS_MASK);
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  CPOL=%u CPHA=%u\n",
                  !!(cs & SPI_CS_CPOL), !!(cs & SPI_CS_CPHA));
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  Status: DONE=%u TXD=%u RXD=%u\n",
                  !!(cs & SPI_CS_DONE), !!(cs & SPI_CS_TXD),
                  !!(cs & SPI_CS_RXD));
    return n;
}
