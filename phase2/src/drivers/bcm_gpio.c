/*
 * JARVIS AI-OS - BCM2711 GPIO Driver Implementation
 *
 * Week 43: General-purpose GPIO driver for Raspberry Pi 4 on seL4.
 *
 * CRITICAL: The GPIO page at paddr 0xFE200000 is ALREADY mapped by
 * uart_pl011.c to vaddr 0x5c1000 during uart_init(). This driver
 * reuses that mapping via uart_get_gpio_vaddr() - it does NOT map
 * any new device pages.
 *
 * BCM2711 uses NEW-style pull-up/down registers at offsets 0xE4-0xF0
 * (2 bits per pin, 16 pins per register). This is different from the
 * legacy GPPUD/GPPUDCLK sequence used on BCM2835/BCM2837.
 */

#include "bcm_gpio.h"
#include "uart_pl011.h"

#include <sel4/sel4.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Internal State
 * ================================================================ */

static volatile uint32_t *gpio_base = NULL;
static bool gpio_initialized = false;

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

static inline uint32_t gpio_reg_read(uint32_t offset)
{
    return *(volatile uint32_t *)((uintptr_t)gpio_base + offset);
}

static inline void gpio_reg_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)gpio_base + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ================================================================
 * Public API
 * ================================================================ */

int gpio_init(void)
{
    if (gpio_initialized) {
        return 0;
    }

    /* Get the GPIO vaddr from uart_pl011.c (already mapped at 0x5c1000) */
    uintptr_t vaddr = uart_get_gpio_vaddr();
    if (vaddr == 0) {
        debug_puts("[GPIO] ERROR: GPIO page not mapped (uart_init not called?)\n");
        return -1;
    }

    gpio_base = (volatile uint32_t *)vaddr;

    debug_puts("[GPIO] Init: reusing UART GPIO mapping at vaddr=");
    debug_hex(vaddr);
    debug_puts("\n");

    /* Set activity LED (GPIO 42) to output mode */
    gpio_set_mode(GPIO_LED_PIN, GPIO_FSEL_OUTPUT);

    gpio_initialized = true;
    debug_puts("[GPIO] Initialized OK\n");
    return 0;
}

void gpio_set_mode(uint32_t pin, uint32_t mode)
{
    if (!gpio_base || pin >= GPIO_PIN_COUNT) {
        return;
    }

    /* Each GPFSEL register covers 10 pins, 3 bits each */
    uint32_t reg_index = pin / 10;
    uint32_t bit_pos = (pin % 10) * 3;
    uint32_t offset = GPIO_GPFSEL0 + (reg_index * 4);

    uint32_t val = gpio_reg_read(offset);
    val &= ~(0x7u << bit_pos);       /* Clear 3-bit field */
    val |= (mode & 0x7u) << bit_pos; /* Set new mode */
    gpio_reg_write(offset, val);
}

void gpio_write(uint32_t pin, uint32_t value)
{
    if (!gpio_base || pin >= GPIO_PIN_COUNT) {
        return;
    }

    uint32_t reg_index = pin / 32;
    uint32_t bit = 1u << (pin % 32);

    if (value) {
        /* GPSET: write-only, sets pin high */
        gpio_reg_write(GPIO_GPSET0 + (reg_index * 4), bit);
    } else {
        /* GPCLR: write-only, sets pin low */
        gpio_reg_write(GPIO_GPCLR0 + (reg_index * 4), bit);
    }
}

uint32_t gpio_read(uint32_t pin)
{
    if (!gpio_base || pin >= GPIO_PIN_COUNT) {
        return 0;
    }

    uint32_t reg_index = pin / 32;
    uint32_t bit = 1u << (pin % 32);

    uint32_t val = gpio_reg_read(GPIO_GPLEV0_OFF + (reg_index * 4));
    return (val & bit) ? 1 : 0;
}

void gpio_set_pull(uint32_t pin, uint32_t pull)
{
    if (!gpio_base || pin >= GPIO_PIN_COUNT || pull > 2) {
        return;
    }

    /*
     * BCM2711 new-style pull-up/down:
     * GPIO_PUP_PDN_CNTRL_REG0 (0xE4): GPIO 0-15, 2 bits per pin
     * GPIO_PUP_PDN_CNTRL_REG1 (0xE8): GPIO 16-31
     * GPIO_PUP_PDN_CNTRL_REG2 (0xEC): GPIO 32-47
     * GPIO_PUP_PDN_CNTRL_REG3 (0xF0): GPIO 48-57
     */
    uint32_t reg_index = pin / 16;
    uint32_t bit_pos = (pin % 16) * 2;
    uint32_t offset = GPIO_PUP_PDN_REG0 + (reg_index * 4);

    uint32_t val = gpio_reg_read(offset);
    val &= ~(0x3u << bit_pos);        /* Clear 2-bit field */
    val |= (pull & 0x3u) << bit_pos;  /* Set new pull mode */
    gpio_reg_write(offset, val);
}

void gpio_toggle(uint32_t pin)
{
    if (!gpio_base || pin >= GPIO_PIN_COUNT) {
        return;
    }
    uint32_t level = gpio_read(pin);
    gpio_write(pin, level ? 0 : 1);
}

void gpio_led_on(void)
{
    gpio_write(GPIO_LED_PIN, 1);
}

void gpio_led_off(void)
{
    gpio_write(GPIO_LED_PIN, 0);
}

void gpio_led_toggle(void)
{
    gpio_toggle(GPIO_LED_PIN);
}

int gpio_get_status(char *output, uint32_t output_size)
{
    if (!output || output_size < 64) {
        return 0;
    }

    if (!gpio_initialized) {
        return snprintf(output, output_size, "GPIO: not initialized\n");
    }

    int pos = 0;
    pos += snprintf(output + pos, output_size - pos,
                    "GPIO Status (BCM2711)\n");
    pos += snprintf(output + pos, output_size - pos,
                    "  Base vaddr: 0x%lx\n", (unsigned long)(uintptr_t)gpio_base);
    pos += snprintf(output + pos, output_size - pos,
                    "  LED (GPIO%u): %s\n", GPIO_LED_PIN,
                    gpio_read(GPIO_LED_PIN) ? "ON" : "OFF");

    /* Show UART pins (14=TX, 15=RX) */
    pos += snprintf(output + pos, output_size - pos,
                    "  GPIO14 (TX): level=%u\n", gpio_read(14));
    pos += snprintf(output + pos, output_size - pos,
                    "  GPIO15 (RX): level=%u\n", gpio_read(15));

    /* Show I2C pins (2=SDA, 3=SCL) */
    pos += snprintf(output + pos, output_size - pos,
                    "  GPIO2 (SDA): level=%u\n", gpio_read(2));
    pos += snprintf(output + pos, output_size - pos,
                    "  GPIO3 (SCL): level=%u\n", gpio_read(3));

    return pos;
}

bool gpio_is_initialized(void)
{
    return gpio_initialized;
}
