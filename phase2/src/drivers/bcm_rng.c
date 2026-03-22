/*
 * JARVIS AI-OS - BCM2711 Hardware RNG Driver Implementation
 *
 * Week 47: iproc-rng200 hardware random number generator.
 *
 * RNG registers at paddr 0xFE104000, mapped via uart_device_map_page().
 * The RNG is in the same 0xFE000000 device untyped as UART, GPIO, etc.
 * PM watchdog at 0xFE100000 is mapped first; after that the watermark is
 * at 0xFE101000, so RNG at 0xFE104000 needs a small buddy skip which
 * uart_device_map_page() handles automatically.
 *
 * Init sequence (from Linux iproc-rng200 driver):
 *   1. Soft reset RNG core
 *   2. Soft reset RBG core
 *   3. Enable RBG (raw hardware entropy)
 *   4. Wait for FIFO to start filling
 *
 * Reference: Linux drivers/char/hw_random/iproc-rng200.c
 */

#include "bcm_rng.h"
#include "uart_pl011.h"
#include "bcm2711_timer.h"

#include <sel4/sel4.h>
#include <stdio.h>
#include <string.h>

/* ================================================================
 * Debug Output (pre-UART: uses seL4_DebugPutChar)
 * ================================================================ */

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

static void debug_hex(uint32_t val)
{
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        seL4_DebugPutChar(hex[(val >> (i * 4)) & 0xF]);
    }
}

/* ================================================================
 * Driver State
 * ================================================================ */

static volatile uint32_t *rng_base = NULL;
static bool rng_initialized = false;
static uint64_t total_bytes_read = 0;

/* ================================================================
 * Register Access (with DSB barrier)
 * ================================================================ */

static inline uint32_t rng_reg_read(uint32_t offset)
{
    uint32_t val = *(volatile uint32_t *)((uintptr_t)rng_base + offset);
    __asm__ volatile("dsb sy" ::: "memory");
    return val;
}

static inline void rng_reg_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)rng_base + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ================================================================
 * Internal Helpers
 * ================================================================ */

/* Wait for FIFO to have at least one word, with timeout.
 * Returns true if data available, false on timeout. */
static bool rng_wait_fifo(void)
{
    if (systimer_is_initialized()) {
        uint64_t start = systimer_read();
        while ((rng_reg_read(RNG_FIFO_COUNT) & RNG_FIFO_COUNT_MASK) == 0) {
            if (systimer_read() - start > RNG_TIMEOUT_US) {
                return false;
            }
        }
    } else {
        /* Fallback: spin counter */
        uint32_t spins = 0;
        while ((rng_reg_read(RNG_FIFO_COUNT) & RNG_FIFO_COUNT_MASK) == 0) {
            if (++spins > 10000000u) {
                return false;
            }
        }
    }
    return true;
}

/* ================================================================
 * Public API
 * ================================================================ */

int rng_init(void)
{
    if (rng_initialized) return 0;

    debug_puts("[RNG] Mapping iproc-rng200 at paddr ");
    debug_hex(RNG_BASE_PADDR);
    debug_puts("\n");

    if (!uart_device_map_ready()) {
        debug_puts("[RNG] ERROR: device mapper not ready\n");
        return -1;
    }

    if (!uart_device_map_page(RNG_BASE_PADDR, 0, &rng_base)) {
        debug_puts("[RNG] ERROR: failed to map RNG page\n");
        return -1;
    }

    debug_puts("[RNG] Mapped OK at vaddr ");
    debug_hex((uint32_t)(uintptr_t)rng_base);
    debug_puts("\n");

    /* Step 1: Soft reset RNG core */
    rng_reg_write(RNG_SOFT_RESET, 1);
    rng_reg_write(RNG_SOFT_RESET, 0);

    /* Step 2: Soft reset RBG core */
    rng_reg_write(RBG_SOFT_RESET, 1);
    rng_reg_write(RBG_SOFT_RESET, 0);

    /* Step 3: Enable RBG (raw hardware entropy) */
    rng_reg_write(RNG_CTRL, RNG_CTRL_RBG_EN);

    /* Step 4: Disable interrupts (we poll) */
    rng_reg_write(RNG_INT_ENABLE, 0);

    debug_puts("[RNG] RBG enabled, waiting for FIFO...\n");

    /* Wait briefly for FIFO to start filling */
    if (!rng_wait_fifo()) {
        debug_puts("[RNG] WARNING: FIFO empty after timeout (may fill later)\n");
    } else {
        uint32_t count = rng_reg_read(RNG_FIFO_COUNT) & RNG_FIFO_COUNT_MASK;
        debug_puts("[RNG] FIFO ready, words=");
        debug_hex(count);
        debug_puts("\n");
    }

    /* SEC-018: Discard first 32 words for warmup (initial entropy may be low) */
    for (int i = 0; i < 32; i++) {
        if (!rng_wait_fifo()) break;  /* Stop if FIFO stalls */
        (void)rng_reg_read(RNG_FIFO_DATA);  /* Read and discard */
    }

    total_bytes_read = 0;
    rng_initialized = true;

    debug_puts("[RNG] iproc-rng200 initialized OK\n");
    return 0;
}

int rng_read(void *buf, uint32_t len)
{
    if (!rng_initialized || !rng_base) return RNG_ERR_NOT_INIT;
    if (!buf || len == 0) return RNG_OK;

    uint8_t *out = (uint8_t *)buf;
    uint32_t remaining = len;

    while (remaining > 0) {
        if (!rng_wait_fifo()) {
            return RNG_ERR_TIMEOUT;
        }

        uint32_t word = rng_reg_read(RNG_FIFO_DATA);
        uint32_t chunk = remaining < 4 ? remaining : 4;
        memcpy(out, &word, chunk);
        out += chunk;
        remaining -= chunk;
    }

    total_bytes_read += len;
    return RNG_OK;
}

uint32_t rng_read_word(void)
{
    if (!rng_initialized || !rng_base) return 0;

    if (!rng_wait_fifo()) {
        debug_puts("[RNG] WARNING: read_word timeout\n");
        return 0;
    }

    total_bytes_read += 4;
    return rng_reg_read(RNG_FIFO_DATA);
}

uint32_t rng_fifo_count(void)
{
    if (!rng_initialized || !rng_base) return 0;
    return rng_reg_read(RNG_FIFO_COUNT) & RNG_FIFO_COUNT_MASK;
}

uint64_t rng_total_bits(void)
{
    if (!rng_initialized || !rng_base) return 0;
    return (uint64_t)rng_reg_read(RNG_TOTAL_BIT_CNT);
}

bool rng_is_initialized(void)
{
    return rng_initialized;
}

int rng_get_status(char *output, uint32_t output_size)
{
    if (!output || output_size == 0) return 0;

    if (!rng_initialized) {
        return snprintf(output, output_size, "RNG: not initialized\n");
    }

    int n = 0;
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "RNG: iproc-rng200\n");
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  FIFO words: %u\n", rng_fifo_count());
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  Total bits: %llu\n", (unsigned long long)rng_total_bits());
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  Bytes read: %llu\n", (unsigned long long)total_bytes_read);
    uint32_t sample = rng_read_word();
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  Sample: 0x%08x\n", sample);
    return n;
}
