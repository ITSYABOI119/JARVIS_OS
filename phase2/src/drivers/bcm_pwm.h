/*
 * JARVIS AI-OS - BCM2835 PWM Driver
 *
 * Week 47: Hardware PWM driver for Raspberry Pi 4 on seL4.
 *
 * PWM controller at paddr 0xFE20C000, mapped via uart_device_map_page()
 * (same 0xFE000000 peripheral range as UART/GPIO/EMMC).
 *
 * IMPORTANT: PWM at 0xFE20C000 is AFTER UART (0xFE201000) but BEFORE
 * EMMC (0xFE340000) in the address space. Must be mapped in the correct
 * init order due to forward-only watermark constraint.
 *
 * GPIO pins: PWM0 = GPIO 12 (ALT0), PWM1 = GPIO 13 (ALT0).
 * The GPIO driver must be initialized before pwm_init().
 *
 * PWM clock is configured via VideoCore mailbox (clock ID 6).
 * Default: 1 MHz clock, range=1000 -> 1 kHz PWM frequency.
 *
 * Reference: BCM2835 ARM Peripherals, Section 9 (PWM)
 */

#ifndef BCM_PWM_H
#define BCM_PWM_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * PWM Base Address
 * ================================================================ */
#define PWM_BASE_PADDR      0xFE20C000u

/* ================================================================
 * Register Offsets
 * ================================================================ */
#define PWM_CTL             0x00    /* Control */
#define PWM_STA             0x04    /* Status */
#define PWM_DMAC            0x08    /* DMA Configuration */
#define PWM_RNG1            0x10    /* Channel 1 Range (period) */
#define PWM_DAT1            0x14    /* Channel 1 Data (duty) */
#define PWM_FIF1            0x18    /* Channel 1 FIFO input */
#define PWM_RNG2            0x20    /* Channel 2 Range (period) */
#define PWM_DAT2            0x24    /* Channel 2 Data (duty) */

/* ================================================================
 * CTL Register Bits
 * ================================================================ */
#define PWM_CTL_MSEN2       (1u << 15)  /* Channel 2: M/S mode */
#define PWM_CTL_USEF2       (1u << 13)  /* Channel 2: use FIFO */
#define PWM_CTL_POLA2       (1u << 12)  /* Channel 2: invert */
#define PWM_CTL_SBIT2       (1u << 11)  /* Channel 2: silence bit */
#define PWM_CTL_RPTL2       (1u << 10)  /* Channel 2: repeat last FIFO */
#define PWM_CTL_MODE2       (1u << 9)   /* Channel 2: 0=PWM, 1=serializer */
#define PWM_CTL_PWEN2       (1u << 8)   /* Channel 2: enable */
#define PWM_CTL_MSEN1       (1u << 7)   /* Channel 1: M/S mode */
#define PWM_CTL_USEF1       (1u << 5)   /* Channel 1: use FIFO */
#define PWM_CTL_POLA1       (1u << 4)   /* Channel 1: invert */
#define PWM_CTL_SBIT1       (1u << 3)   /* Channel 1: silence bit */
#define PWM_CTL_RPTL1       (1u << 2)   /* Channel 1: repeat last FIFO */
#define PWM_CTL_MODE1       (1u << 1)   /* Channel 1: 0=PWM, 1=serializer */
#define PWM_CTL_PWEN1       (1u << 0)   /* Channel 1: enable */

/* ================================================================
 * STA Register Bits
 * ================================================================ */
#define PWM_STA_STA2        (1u << 10)  /* Channel 2 state */
#define PWM_STA_STA1        (1u << 9)   /* Channel 1 state */
#define PWM_STA_BERR        (1u << 8)   /* Bus error */
#define PWM_STA_GAPO2       (1u << 5)   /* Channel 2 gap occurred */
#define PWM_STA_GAPO1       (1u << 4)   /* Channel 1 gap occurred */
#define PWM_STA_RERR1       (1u << 3)   /* FIFO read error */
#define PWM_STA_WERR1       (1u << 2)   /* FIFO write error */
#define PWM_STA_EMPT1       (1u << 1)   /* FIFO empty */
#define PWM_STA_FULL1       (1u << 0)   /* FIFO full */

/* ================================================================
 * Constants
 * ================================================================ */
#define PWM_NUM_CHANNELS    2
#define PWM_CLOCK_ID        6           /* VideoCore clock ID for PWM */
#define PWM_DEFAULT_CLOCK_HZ  1000000   /* 1 MHz default */

/* ================================================================
 * Error Codes
 * ================================================================ */
#define PWM_OK              0
#define PWM_ERR_NOT_INIT    -1
#define PWM_ERR_PARAM       -2
#define PWM_ERR_CLOCK       -3

/* ================================================================
 * Public API
 * ================================================================ */

/*
 * Initialize PWM controller.
 * Maps PWM MMIO page at 0xFE20C000 via uart_device_map_page().
 * Configures GPIO 12/13 as ALT0 (PWM0/PWM1).
 * Sets PWM clock to 1 MHz via VideoCore mailbox (best-effort).
 * Sets both channels to M/S mode with range=1000 (1 kHz).
 * Channels are NOT enabled until pwm_enable() is called.
 * Requires: gpio_init() and thermal_init() already called.
 * Returns 0 on success, -1 on failure.
 */
int pwm_init(void);

/*
 * Set duty cycle for a PWM channel.
 * channel: 0 or 1.
 * duty_percent: 0 to 100.
 * Returns PWM_OK on success.
 */
int pwm_set_duty(uint32_t channel, uint32_t duty_percent);

/*
 * Set PWM frequency for a channel.
 * channel: 0 or 1.
 * freq_hz: desired frequency in Hz (range = clock_rate / freq_hz).
 * Maintains current duty cycle percentage.
 * Returns PWM_OK on success.
 */
int pwm_set_freq(uint32_t channel, uint32_t freq_hz);

/*
 * Enable or disable a PWM channel.
 * channel: 0 or 1.
 * enable: true to enable, false to disable.
 * Returns PWM_OK on success.
 */
int pwm_enable(uint32_t channel, bool enable);

/*
 * Check if PWM driver is initialized.
 */
bool pwm_is_initialized(void);

/*
 * Get PWM status string for shell commands.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL).
 */
int pwm_get_status(char *output, uint32_t output_size);

#endif /* BCM_PWM_H */
