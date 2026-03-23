/*
 * JARVIS AI-OS Phase 3 — x86 Timer Driver
 * Supports PIT (8254), HPET, and TSC timer sources.
 */

#ifndef X86_TIMER_H
#define X86_TIMER_H

#include <stdint.h>

typedef enum { TIMER_PIT, TIMER_HPET, TIMER_TSC } timer_source_t;

/* PIT constants */
#define PIT_CHANNEL0    0x40
#define PIT_CMD         0x43
#define PIT_FREQUENCY   1193182ULL  /* 1.193182 MHz */

/* PIT command bits */
#define PIT_CMD_CHANNEL0  0x00
#define PIT_CMD_LOHIGH    0x30  /* Access mode: lobyte/hibyte */
#define PIT_CMD_RATEGEN   0x04  /* Mode 2: rate generator */

/* HPET register offsets */
#define HPET_CAP_ID     0x000
#define HPET_CONFIG     0x010
#define HPET_COUNTER    0x0F0

/* HPET config bits */
#define HPET_CFG_ENABLE (1 << 0)

/* API */
int            timer_init(timer_source_t source);
uint64_t       timer_read_ticks(void);
uint64_t       timer_ticks_to_us(uint64_t ticks);
void           timer_delay_us(uint64_t us);
uint64_t       timer_get_frequency(void);
timer_source_t timer_get_source(void);

#endif
