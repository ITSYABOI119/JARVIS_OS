/*
 * JARVIS AI-OS - BCM2711 GPIO Driver
 *
 * Week 43: General-purpose GPIO driver for Raspberry Pi 4.
 * Reuses the GPIO MMIO page already mapped by uart_pl011.c at vaddr 0x5c1000.
 *
 * BCM2711 GPIO register page at paddr 0xFE200000 covers:
 * - GPFSEL0-5 (function select, 3 bits per pin, 10 pins per register)
 * - GPSET0-1, GPCLR0-1 (set/clear output)
 * - GPLEV0-1 (read pin level)
 * - GPIO_PUP_PDN_CNTRL_REG0-3 (BCM2711 new-style pull-up/down)
 *
 * Pi 4 activity LED is on GPIO 42.
 */

#ifndef BCM_GPIO_H
#define BCM_GPIO_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * GPIO Function Select Modes (3-bit field per pin)
 * Some may already be defined in uart_pl011.h — guard against redefinition
 * ================================================================ */
#ifndef GPIO_FSEL_INPUT
#define GPIO_FSEL_INPUT     0
#endif
#ifndef GPIO_FSEL_OUTPUT
#define GPIO_FSEL_OUTPUT    1
#endif
#ifndef GPIO_FSEL_ALT0
#define GPIO_FSEL_ALT0      4
#endif
#ifndef GPIO_FSEL_ALT1
#define GPIO_FSEL_ALT1      5
#endif
#ifndef GPIO_FSEL_ALT2
#define GPIO_FSEL_ALT2      6
#endif
#ifndef GPIO_FSEL_ALT3
#define GPIO_FSEL_ALT3      7
#endif
#ifndef GPIO_FSEL_ALT4
#define GPIO_FSEL_ALT4      3
#endif
#ifndef GPIO_FSEL_ALT5
#define GPIO_FSEL_ALT5      2
#endif

/* ================================================================
 * GPIO Pull-Up/Down Modes (BCM2711 new-style, 2-bit field)
 * ================================================================ */
#ifndef GPIO_PULL_NONE
#define GPIO_PULL_NONE      0
#endif
#ifndef GPIO_PULL_UP
#define GPIO_PULL_UP        1
#endif
#ifndef GPIO_PULL_DOWN
#define GPIO_PULL_DOWN      2
#endif

/* ================================================================
 * GPIO Register Offsets (within 4KB page at 0xFE200000)
 * ================================================================ */
#define GPIO_GPFSEL0        0x00    /* GPIO 0-9 function select */
#ifndef GPIO_GPFSEL1
#define GPIO_GPFSEL1        0x04    /* GPIO 10-19 function select */
#endif
#define GPIO_GPFSEL2        0x08    /* GPIO 20-29 function select */
#define GPIO_GPFSEL3        0x0C    /* GPIO 30-39 function select */
#define GPIO_GPFSEL4        0x10    /* GPIO 40-49 function select */
#define GPIO_GPFSEL5        0x14    /* GPIO 50-57 function select */
#define GPIO_GPSET0         0x1C    /* GPIO 0-31 set output high */
#define GPIO_GPSET1         0x20    /* GPIO 32-57 set output high */
#define GPIO_GPCLR0         0x28    /* GPIO 0-31 clear output low */
#define GPIO_GPCLR1         0x2C    /* GPIO 32-57 clear output low */
#define GPIO_GPLEV0_OFF     0x34    /* GPIO 0-31 pin level */
#define GPIO_GPLEV1_OFF     0x38    /* GPIO 32-57 pin level */
#define GPIO_PUP_PDN_REG0   0xE4   /* GPIO 0-15 pull-up/down */
#define GPIO_PUP_PDN_REG1   0xE8   /* GPIO 16-31 pull-up/down */
#define GPIO_PUP_PDN_REG2   0xEC   /* GPIO 32-47 pull-up/down */
#define GPIO_PUP_PDN_REG3   0xF0   /* GPIO 48-57 pull-up/down */

/* Total GPIO pins on BCM2711 */
#define GPIO_PIN_COUNT      58

/* Activity LED pin */
#define GPIO_LED_PIN        42

/* ================================================================
 * Public API
 * ================================================================ */

/* Initialize GPIO driver by getting the already-mapped GPIO vaddr
 * from uart_pl011.c. Does NOT map any new device pages.
 * Requires: uart_init() already called (maps GPIO page).
 * Returns true on success. */
int gpio_init(void);

/* Set function select mode for a pin (0-57).
 * mode: GPIO_FSEL_INPUT, GPIO_FSEL_OUTPUT, GPIO_FSEL_ALT0-5 */
void gpio_set_mode(uint32_t pin, uint32_t mode);

/* Write a pin high (value=1) or low (value=0).
 * Pin must be set to GPIO_FSEL_OUTPUT first. */
void gpio_write(uint32_t pin, uint32_t value);

/* Read current level of a pin (returns 0 or 1). */
uint32_t gpio_read(uint32_t pin);

/* Set pull-up/down for a pin.
 * pull: GPIO_PULL_NONE, GPIO_PULL_UP, GPIO_PULL_DOWN */
void gpio_set_pull(uint32_t pin, uint32_t pull);

/* Toggle a pin (read current level, write opposite). */
void gpio_toggle(uint32_t pin);

/* Activity LED control (GPIO 42) */
void gpio_led_on(void);
void gpio_led_off(void);
void gpio_led_toggle(void);

/* Get GPIO status string for shell commands.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL). */
int gpio_get_status(char *output, uint32_t output_size);

/* Check if GPIO driver is initialized. */
bool gpio_is_initialized(void);

#endif /* BCM_GPIO_H */
