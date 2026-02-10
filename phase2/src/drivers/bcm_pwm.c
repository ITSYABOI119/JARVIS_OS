/*
 * JARVIS AI-OS - BCM2835 PWM Driver Implementation
 *
 * Week 47: Hardware PWM driver for Raspberry Pi 4 on seL4.
 *
 * PWM controller at paddr 0xFE20C000 (one 4KB page), mapped via
 * uart_device_map_page() (same 0xFE000000 peripheral untyped as
 * UART, GPIO, EMMC, I2C, USB).
 *
 * Two independent PWM channels:
 *   Channel 0 -> RNG1/DAT1 registers, CTL bits PWEN1/MSEN1
 *   Channel 1 -> RNG2/DAT2 registers, CTL bits PWEN2/MSEN2
 *
 * PWM clock is configured via VideoCore mailbox property interface
 * using thermal_mailbox_tag() from bcm_thermal.c. Clock ID 6 = PWM.
 * Default clock rate: 1 MHz. PWM freq = clock / range.
 *
 * GPIO mapping:
 *   PWM0 -> GPIO 12 (ALT0)
 *   PWM1 -> GPIO 13 (ALT0)
 *
 * Reference: BCM2835 ARM Peripherals, Section 9 (PWM)
 */

#include "bcm_pwm.h"
#include "bcm_gpio.h"
#include "uart_pl011.h"
#include "bcm2711_timer.h"
#include "bcm_thermal.h"

#include <sel4/sel4.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Internal State
 * ================================================================ */

static volatile uint32_t *pwm_base = NULL;
static bool pwm_initialized = false;

static uint32_t pwm_clock_hz = 0;
static uint32_t channel_range[2] = {1000, 1000};
static uint32_t channel_duty_pct[2] = {0, 0};

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

static inline uint32_t pwm_reg_read(uint32_t offset)
{
    uint32_t val = *(volatile uint32_t *)((uintptr_t)pwm_base + offset);
    __asm__ volatile("dsb sy" ::: "memory");
    return val;
}

static inline void pwm_reg_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)pwm_base + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ================================================================
 * Delay Helper
 * ================================================================ */

static void pwm_delay_us(uint32_t us)
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
 * Internal: Clock Configuration via VideoCore Mailbox
 * ================================================================ */

static int pwm_set_clock_rate(uint32_t hz)
{
    if (!thermal_is_initialized()) {
        debug_puts("[PWM] WARNING: Mailbox not ready, clock config skipped\n");
        return -1;
    }

    /* SET_CLOCK_RATE: buf[0]=clock_id, buf[1]=rate_hz, buf[2]=skip_turbo */
    uint32_t buf[3];
    buf[0] = PWM_CLOCK_ID;
    buf[1] = hz;
    buf[2] = 0;  /* skip_turbo = 0 */

    int rc = thermal_mailbox_tag(TAG_SET_CLOCK_RATE, buf, 3, 2);
    if (rc != 0) {
        debug_puts("[PWM] WARNING: Failed to set clock rate\n");
        return -1;
    }

    pwm_clock_hz = buf[1];
    return 0;
}

/* ================================================================
 * Public API
 * ================================================================ */

int pwm_init(void)
{
    if (pwm_initialized) {
        return 0;
    }

    debug_puts("[PWM] Initializing BCM2835 PWM controller\n");

    /* Configure GPIO 12 and 13 for PWM (ALT0) */
    if (gpio_is_initialized()) {
        gpio_set_mode(12, GPIO_FSEL_ALT0);
        gpio_set_mode(13, GPIO_FSEL_ALT0);
        debug_puts("[PWM] GPIO 12/13 set to ALT0 (PWM0/PWM1)\n");
    } else {
        debug_puts("[PWM] WARNING: GPIO not initialized, assuming pins already configured\n");
    }

    /* Map PWM MMIO page via shared device mapper */
    volatile uint32_t *mapped = NULL;
    if (!uart_device_map_page(PWM_BASE_PADDR, 0, &mapped)) {
        debug_puts("[PWM] ERROR: Failed to map PWM at paddr=");
        debug_hex(PWM_BASE_PADDR);
        debug_puts("\n");
        return -1;
    }

    pwm_base = mapped;

    debug_puts("[PWM] Mapped: paddr=");
    debug_hex(PWM_BASE_PADDR);
    debug_puts(" -> vaddr=");
    debug_hex((seL4_Word)pwm_base);
    debug_puts("\n");

    /* Set PWM clock to 1 MHz via VideoCore mailbox (best-effort) */
    if (pwm_set_clock_rate(PWM_DEFAULT_CLOCK_HZ) == 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[PWM] Clock set to %u Hz\n",
                 (unsigned)pwm_clock_hz);
        debug_puts(msg);
    } else {
        /* Clock config failed but PWM page is mapped; set nominal rate */
        pwm_clock_hz = PWM_DEFAULT_CLOCK_HZ;
        debug_puts("[PWM] Using nominal clock rate (mailbox unavailable)\n");
    }

    /* Disable both channels before configuring */
    pwm_reg_write(PWM_CTL, 0);
    pwm_delay_us(10);

    /* Clear status flags */
    pwm_reg_write(PWM_STA, PWM_STA_BERR | PWM_STA_GAPO1 | PWM_STA_GAPO2 |
                            PWM_STA_RERR1 | PWM_STA_WERR1);

    /* Set both channels: range=1000, data=0 (0% duty), M/S mode */
    pwm_reg_write(PWM_RNG1, 1000);
    pwm_reg_write(PWM_DAT1, 0);
    pwm_reg_write(PWM_RNG2, 1000);
    pwm_reg_write(PWM_DAT2, 0);

    channel_range[0] = 1000;
    channel_range[1] = 1000;
    channel_duty_pct[0] = 0;
    channel_duty_pct[1] = 0;

    /* Enable M/S mode for both channels (but don't enable outputs yet) */
    pwm_reg_write(PWM_CTL, PWM_CTL_MSEN1 | PWM_CTL_MSEN2);

    pwm_initialized = true;
    debug_puts("[PWM] Initialized OK (2 channels, M/S mode, 1 kHz)\n");
    return 0;
}

int pwm_set_duty(uint32_t channel, uint32_t duty_percent)
{
    if (!pwm_initialized) {
        return PWM_ERR_NOT_INIT;
    }
    if (channel >= PWM_NUM_CHANNELS || duty_percent > 100) {
        return PWM_ERR_PARAM;
    }

    uint32_t range = channel_range[channel];
    uint32_t data = range * duty_percent / 100;

    channel_duty_pct[channel] = duty_percent;

    if (channel == 0) {
        pwm_reg_write(PWM_DAT1, data);
    } else {
        pwm_reg_write(PWM_DAT2, data);
    }

    return PWM_OK;
}

int pwm_set_freq(uint32_t channel, uint32_t freq_hz)
{
    if (!pwm_initialized) {
        return PWM_ERR_NOT_INIT;
    }
    if (channel >= PWM_NUM_CHANNELS || freq_hz == 0 || freq_hz > pwm_clock_hz) {
        return PWM_ERR_PARAM;
    }

    uint32_t range = pwm_clock_hz / freq_hz;
    if (range == 0) {
        range = 1;
    }

    channel_range[channel] = range;

    /* Update range register */
    if (channel == 0) {
        pwm_reg_write(PWM_RNG1, range);
    } else {
        pwm_reg_write(PWM_RNG2, range);
    }

    /* Recalculate data to maintain current duty percentage */
    uint32_t data = range * channel_duty_pct[channel] / 100;
    if (channel == 0) {
        pwm_reg_write(PWM_DAT1, data);
    } else {
        pwm_reg_write(PWM_DAT2, data);
    }

    return PWM_OK;
}

int pwm_enable(uint32_t channel, bool enable)
{
    if (!pwm_initialized) {
        return PWM_ERR_NOT_INIT;
    }
    if (channel >= PWM_NUM_CHANNELS) {
        return PWM_ERR_PARAM;
    }

    uint32_t ctl = pwm_reg_read(PWM_CTL);

    if (channel == 0) {
        if (enable) {
            ctl |= PWM_CTL_PWEN1;
        } else {
            ctl &= ~PWM_CTL_PWEN1;
        }
    } else {
        if (enable) {
            ctl |= PWM_CTL_PWEN2;
        } else {
            ctl &= ~PWM_CTL_PWEN2;
        }
    }

    pwm_reg_write(PWM_CTL, ctl);
    return PWM_OK;
}

bool pwm_is_initialized(void)
{
    return pwm_initialized;
}

int pwm_get_status(char *output, uint32_t output_size)
{
    if (!output || output_size < 64) {
        return 0;
    }

    if (!pwm_initialized) {
        return snprintf(output, output_size, "PWM: not initialized\n");
    }

    int pos = 0;
    uint32_t ctl = pwm_reg_read(PWM_CTL);

    pos += snprintf(output + pos, (uint32_t)output_size - (uint32_t)pos,
                    "PWM Status (BCM2835)\n");
    pos += snprintf(output + pos, (uint32_t)output_size - (uint32_t)pos,
                    "  Base vaddr: 0x%lx\n", (unsigned long)(uintptr_t)pwm_base);
    pos += snprintf(output + pos, (uint32_t)output_size - (uint32_t)pos,
                    "  Clock: %u Hz\n", (unsigned)pwm_clock_hz);

    /* Channel 0 */
    pos += snprintf(output + pos, (uint32_t)output_size - (uint32_t)pos,
                    "  CH0 (GPIO12): %s, range=%u, duty=%u%%\n",
                    (ctl & PWM_CTL_PWEN1) ? "ON" : "OFF",
                    (unsigned)channel_range[0],
                    (unsigned)channel_duty_pct[0]);

    /* Channel 1 */
    pos += snprintf(output + pos, (uint32_t)output_size - (uint32_t)pos,
                    "  CH1 (GPIO13): %s, range=%u, duty=%u%%\n",
                    (ctl & PWM_CTL_PWEN2) ? "ON" : "OFF",
                    (unsigned)channel_range[1],
                    (unsigned)channel_duty_pct[1]);

    return pos;
}
