/*
 * JARVIS AI-OS - Boot Manager
 *
 * Week 46: Per-stage boot timing, lazy initialization tracking,
 * and boot optimization reporting. Single-threaded rootserver -
 * optimization through ordered init and deferred loading.
 */

#ifndef BOOT_MANAGER_H
#define BOOT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#define BOOT_MAX_STAGES 16

typedef enum {
    BOOT_STAGE_SYSTIMER = 0,
    BOOT_STAGE_DMA,        /* Week 48 - DMA engine at 0xFE007000 */
    BOOT_STAGE_THERMAL,
    BOOT_STAGE_WATCHDOG,
    BOOT_STAGE_FDT,
    BOOT_STAGE_UART,
    BOOT_STAGE_CACHE,
    BOOT_STAGE_EMMC,
    BOOT_STAGE_GENET,
    BOOT_STAGE_NET,
    BOOT_STAGE_GPIO,
    BOOT_STAGE_I2C,
    BOOT_STAGE_USB,
    BOOT_STAGE_SPI,
    BOOT_STAGE_RNG,
    BOOT_STAGE_PWM,
    BOOT_STAGE_COUNT
} boot_stage_t;

typedef struct {
    const char *name;
    uint64_t start_us;
    uint64_t end_us;
    bool completed;
    bool lazy;         /* Deferred until first use */
    bool required;     /* Must succeed for boot */
} boot_stage_info_t;

/* Initialize boot manager (call before any stage tracking) */
void boot_manager_init(void);

/* Mark beginning of a boot stage */
void boot_stage_begin(boot_stage_t stage);

/* Mark end of a boot stage */
void boot_stage_end(boot_stage_t stage);

/* Mark a stage as lazy-initialized (deferred until first use) */
void boot_stage_mark_lazy(boot_stage_t stage);

/* Check if a stage has completed initialization */
bool boot_stage_is_initialized(boot_stage_t stage);

/* Print boot timing report to serial console */
void boot_manager_print_report(void);

/* Get total boot time in microseconds (earliest start to latest end) */
uint64_t boot_manager_total_us(void);

/* Get count of completed boot stages */
int boot_manager_completed_count(void);

/* Format boot status into output buffer (for shell commands).
 * Returns bytes written. */
int boot_manager_get_status(char *output, uint32_t output_size);

#endif
