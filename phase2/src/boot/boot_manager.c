/*
 * JARVIS AI-OS - Boot Manager
 *
 * Week 46: Per-stage boot timing, lazy initialization tracking,
 * and boot optimization reporting. Single-threaded rootserver -
 * optimization through ordered init and deferred loading.
 */

#include "boot_manager.h"
#include "bcm2711_timer.h"
#include <sel4/sel4.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

static boot_stage_info_t stages[BOOT_STAGE_COUNT];
static bool manager_initialized = false;

/* ------------------------------------------------------------------ */
/* Debug output via seL4_DebugPutChar                                  */
/* ------------------------------------------------------------------ */

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void boot_manager_init(void)
{
    memset(stages, 0, sizeof(stages));

    stages[BOOT_STAGE_SYSTIMER].name = "systimer";
    stages[BOOT_STAGE_THERMAL].name  = "thermal";
    stages[BOOT_STAGE_WATCHDOG].name = "watchdog";
    stages[BOOT_STAGE_FDT].name      = "fdt";
    stages[BOOT_STAGE_UART].name     = "uart";
    stages[BOOT_STAGE_CACHE].name    = "cache";
    stages[BOOT_STAGE_EMMC].name     = "emmc";
    stages[BOOT_STAGE_GENET].name    = "genet";
    stages[BOOT_STAGE_NET].name      = "net";
    stages[BOOT_STAGE_GPIO].name     = "gpio";
    stages[BOOT_STAGE_I2C].name      = "i2c";
    stages[BOOT_STAGE_USB].name      = "usb";
    stages[BOOT_STAGE_SPI].name      = "spi";
    stages[BOOT_STAGE_RNG].name      = "rng";
    stages[BOOT_STAGE_PWM].name      = "pwm";

    /* Core subsystems that must succeed for a functional boot */
    stages[BOOT_STAGE_SYSTIMER].required = true;
    stages[BOOT_STAGE_UART].required     = true;
    stages[BOOT_STAGE_EMMC].required     = true;

    manager_initialized = true;
}

void boot_stage_begin(boot_stage_t stage)
{
    if (stage >= BOOT_STAGE_COUNT)
        return;

    if (systimer_is_initialized())
        stages[stage].start_us = systimer_read();
    else
        stages[stage].start_us = 0;
}

void boot_stage_end(boot_stage_t stage)
{
    if (stage >= BOOT_STAGE_COUNT)
        return;

    if (systimer_is_initialized())
        stages[stage].end_us = systimer_read();
    else
        stages[stage].end_us = 0;

    /* If timer wasn't available at begin time (start_us==0 but end_us!=0),
     * set start_us = end_us so elapsed = 0.  This happens for SYSTIMER
     * stage where begin is called before the timer is mapped. */
    if (stages[stage].start_us == 0 && stages[stage].end_us != 0)
        stages[stage].start_us = stages[stage].end_us;

    stages[stage].completed = true;
}

void boot_stage_mark_lazy(boot_stage_t stage)
{
    if (stage >= BOOT_STAGE_COUNT)
        return;

    stages[stage].lazy = true;
}

bool boot_stage_is_initialized(boot_stage_t stage)
{
    if (stage >= BOOT_STAGE_COUNT)
        return false;

    return stages[stage].completed;
}

void boot_manager_print_report(void)
{
    char buf[128];

    debug_puts("[BOOT] === Boot Timing Report ===\n");

    for (int i = 0; i < BOOT_STAGE_COUNT; i++) {
        if (stages[i].completed) {
            uint64_t elapsed = stages[i].end_us - stages[i].start_us;
            uint32_t ms_whole = (uint32_t)(elapsed / 1000);
            uint32_t ms_frac  = (uint32_t)(elapsed % 1000) / 100;
            snprintf(buf, sizeof(buf), "[BOOT] %-10s: %u.%ums (%u us)\n",
                     stages[i].name,
                     (unsigned)ms_whole, (unsigned)ms_frac,
                     (unsigned)elapsed);
            debug_puts(buf);
        } else if (stages[i].lazy) {
            snprintf(buf, sizeof(buf), "[BOOT] %-10s: DEFERRED\n",
                     stages[i].name);
            debug_puts(buf);
        }
    }

    uint64_t total = boot_manager_total_us();
    uint32_t total_ms_whole = (uint32_t)(total / 1000);
    uint32_t total_ms_frac  = (uint32_t)(total % 1000) / 100;
    snprintf(buf, sizeof(buf), "[BOOT] Total: %u.%ums (%d stages)\n",
             (unsigned)total_ms_whole, (unsigned)total_ms_frac,
             boot_manager_completed_count());
    debug_puts(buf);
}

uint64_t boot_manager_total_us(void)
{
    uint64_t earliest = UINT64_MAX;
    uint64_t latest   = 0;
    bool found = false;

    for (int i = 0; i < BOOT_STAGE_COUNT; i++) {
        if (!stages[i].completed)
            continue;

        if (stages[i].start_us < earliest)
            earliest = stages[i].start_us;
        if (stages[i].end_us > latest)
            latest = stages[i].end_us;

        found = true;
    }

    if (!found)
        return 0;

    return latest - earliest;
}

int boot_manager_completed_count(void)
{
    int count = 0;

    for (int i = 0; i < BOOT_STAGE_COUNT; i++) {
        if (stages[i].completed)
            count++;
    }

    return count;
}

int boot_manager_get_status(char *output, uint32_t output_size)
{
    if (!output || output_size == 0)
        return 0;

    int written = 0;
    int ret;

    ret = snprintf(output + written, output_size - written,
                   "Boot Timing Report\n");
    if (ret > 0)
        written += ret;

    for (int i = 0; i < BOOT_STAGE_COUNT; i++) {
        if ((uint32_t)written >= output_size - 1)
            break;

        if (stages[i].completed) {
            uint64_t elapsed = stages[i].end_us - stages[i].start_us;
            uint32_t ms_whole = (uint32_t)(elapsed / 1000);
            uint32_t ms_frac  = (uint32_t)(elapsed % 1000) / 100;
            ret = snprintf(output + written, output_size - written,
                           "  %-10s: %u.%ums (%u us)%s\n",
                           stages[i].name,
                           (unsigned)ms_whole, (unsigned)ms_frac,
                           (unsigned)elapsed,
                           stages[i].required ? " [required]" : "");
        } else if (stages[i].lazy) {
            ret = snprintf(output + written, output_size - written,
                           "  %-10s: DEFERRED\n",
                           stages[i].name);
        } else {
            ret = snprintf(output + written, output_size - written,
                           "  %-10s: NOT STARTED\n",
                           stages[i].name);
        }

        if (ret > 0)
            written += ret;
    }

    if ((uint32_t)written < output_size - 1) {
        uint64_t total = boot_manager_total_us();
        uint32_t total_ms_whole = (uint32_t)(total / 1000);
        uint32_t total_ms_frac  = (uint32_t)(total % 1000) / 100;
        ret = snprintf(output + written, output_size - written,
                       "Total: %u.%ums (%d/%d stages)\n",
                       (unsigned)total_ms_whole, (unsigned)total_ms_frac,
                       boot_manager_completed_count(), BOOT_STAGE_COUNT);
        if (ret > 0)
            written += ret;
    }

    return written;
}
