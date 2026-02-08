/*
 * JARVIS AI-OS - BCM2711 Thermal Monitoring Implementation
 *
 * Week 44: GPU temperature reading via VideoCore mailbox property interface.
 *
 * Mailbox MMIO page at paddr 0xFE00B000, mapped to explicit vaddr 0x610000.
 * Mapped BEFORE watchdog and UART (ascending paddr order for seL4).
 *
 * Property tag buffer allocated from DMA pool (GPU needs physical/bus address).
 * GPU bus address = ARM paddr | 0xC0000000 on BCM2711.
 */

#include "bcm_thermal.h"
#include "dma_alloc.h"
#include "bcm2711_timer.h"
#include "uart_pl011.h"

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

static volatile uint32_t *mbox_page = NULL;   /* Base of mapped 4KB page */
static bool thermal_initialized = false;

/* DMA buffer for property tag messages (one-time allocation) */
static volatile uint32_t *tag_buf = NULL;     /* CPU virtual address */
static uintptr_t tag_buf_paddr = 0;           /* ARM physical address */

/* Cached last temperature reading */
static int last_temp_millic = -1;

/* ================================================================
 * Mailbox I/O
 * ================================================================ */

static inline uint32_t mbox_read_reg(uint32_t offset)
{
    /* offset is from page base 0xFE00B000 */
    return *((volatile uint32_t *)((uintptr_t)mbox_page + offset));
}

static inline void mbox_write_reg(uint32_t offset, uint32_t val)
{
    *((volatile uint32_t *)((uintptr_t)mbox_page + offset)) = val;
}

static bool mbox_send(uint32_t bus_addr, uint8_t channel)
{
    /* Wait until mailbox 1 is not full (use MAIL1 status register) */
    uint32_t timeout = 100000;
    while ((mbox_read_reg(MBOX_WR_STATUS_OFF) & MBOX_FULL) && timeout > 0) {
        timeout--;
    }
    if (timeout == 0) {
        debug_puts("[THERMAL] ERROR: mailbox full timeout\n");
        return false;
    }

    /* Write address | channel to mailbox 1 write register */
    mbox_write_reg(MBOX_WRITE_OFF, (bus_addr & ~0xF) | (channel & 0xF));
    return true;
}

static uint32_t mbox_recv(uint8_t channel)
{
    /* Poll until we get a response on our channel */
    uint32_t timeout = 1000000;
    while (timeout > 0) {
        /* Wait for mailbox 0 to have data (use MAIL0 status register) */
        while ((mbox_read_reg(MBOX_RD_STATUS_OFF) & MBOX_EMPTY) && timeout > 0) {
            timeout--;
        }
        if (timeout == 0) break;

        uint32_t data = mbox_read_reg(MBOX_READ_OFF);
        if ((data & 0xF) == channel) {
            return data & ~0xF;  /* Return address without channel bits */
        }
        /* Wrong channel, keep polling */
        timeout--;
    }

    debug_puts("[THERMAL] ERROR: mailbox recv timeout\n");
    return 0;
}

/* ================================================================
 * Public API
 * ================================================================ */

int thermal_init(void)
{
    if (thermal_initialized) return 0;

    debug_puts("[THERMAL] Mapping mailbox at paddr ");
    debug_hex(MBOX_PAGE_PADDR);
    debug_puts(" -> vaddr ");
    debug_hex(MBOX_PAGE_VADDR);
    debug_puts("\n");

    if (!uart_device_map_ready()) {
        debug_puts("[THERMAL] ERROR: device mapper not ready\n");
        return -1;
    }

    if (!uart_device_map_page(MBOX_PAGE_PADDR, MBOX_PAGE_VADDR, &mbox_page)) {
        debug_puts("[THERMAL] ERROR: failed to map mailbox page\n");
        return -1;
    }

    debug_puts("[THERMAL] Mailbox mapped OK\n");

    /* Allocate DMA buffer for property tags (256 bytes, 16-byte aligned) */
    if (!dma_alloc_is_ready()) {
        debug_puts("[THERMAL] ERROR: DMA allocator not ready\n");
        return -1;
    }

    tag_buf = (volatile uint32_t *)dma_alloc(256, &tag_buf_paddr);
    if (!tag_buf) {
        debug_puts("[THERMAL] ERROR: DMA alloc failed for tag buffer\n");
        return -1;
    }

    debug_puts("[THERMAL] Tag buffer at vaddr ");
    debug_hex((uint32_t)(uintptr_t)tag_buf);
    debug_puts(" paddr ");
    debug_hex((uint32_t)tag_buf_paddr);
    debug_puts("\n");

    thermal_initialized = true;
    return 0;
}

int thermal_get_temp(void)
{
    if (!thermal_initialized || !mbox_page || !tag_buf) return -1;

    /* Build property tag buffer for GET_TEMPERATURE:
     * [0] buffer size = 32 bytes
     * [1] request code = 0 (process request)
     * [2] tag ID = 0x00030006 (GET_TEMPERATURE)
     * [3] value buffer size = 8
     * [4] request/response code = 0
     * [5] temperature ID = 0 (SoC)
     * [6] value = 0 (will be filled by GPU)
     * [7] end tag = 0
     */
    tag_buf[0] = 32;                    /* Buffer size */
    tag_buf[1] = MBOX_REQUEST;          /* Request code */
    tag_buf[2] = TAG_GET_TEMPERATURE;   /* Tag ID */
    tag_buf[3] = 8;                     /* Value buffer size */
    tag_buf[4] = 0;                     /* Request/response code */
    tag_buf[5] = 0;                     /* Temperature ID (SoC) */
    tag_buf[6] = 0;                     /* Value (output) */
    tag_buf[7] = TAG_END;               /* End tag */

    /* Memory barrier before sending to GPU */
    __asm__ volatile("dsb sy" ::: "memory");

    /* Send buffer bus address to mailbox channel 8 */
    uint32_t bus_addr = ARM_TO_BUS(tag_buf_paddr);
    if (!mbox_send(bus_addr, MBOX_CHANNEL_PROP)) {
        return -1;
    }

    /* Wait for response */
    uint32_t resp = mbox_recv(MBOX_CHANNEL_PROP);
    (void)resp;

    /* Memory barrier after receiving from GPU */
    __asm__ volatile("dsb sy" ::: "memory");

    /* Check response code */
    if (tag_buf[1] != MBOX_RESPONSE_OK) {
        debug_puts("[THERMAL] ERROR: mailbox response code ");
        debug_hex(tag_buf[1]);
        debug_puts("\n");
        return -1;
    }

    /* tag_buf[6] contains temperature in millidegrees C */
    int temp = (int)tag_buf[6];
    last_temp_millic = temp;
    return temp;
}

int thermal_get_status(char *output, uint32_t output_size)
{
    if (!output || output_size == 0) return 0;

    if (!thermal_initialized) {
        return snprintf(output, output_size, "Temperature: NOT INITIALIZED\n");
    }

    int temp = thermal_get_temp();
    if (temp < 0) {
        return snprintf(output, output_size, "Temperature: READ ERROR\n");
    }

    int degrees = temp / 1000;
    int frac = (temp % 1000) / 100;

    return snprintf(output, output_size,
                    "Temperature: %d.%dC\n",
                    degrees, frac);
}

bool thermal_is_initialized(void)
{
    return thermal_initialized;
}
