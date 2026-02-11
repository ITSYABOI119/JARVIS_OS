/*
 * JARVIS AI-OS - BCM2711 DMA Engine Driver Implementation
 *
 * Week 48: DMA controller for memory-to-memory transfers.
 *
 * DMA registers at paddr 0xFE007000, mapped via uart_device_map_page().
 * The DMA controller is in the same 0xFE000000 device untyped as UART,
 * GPIO, etc. Systimer at 0xFE003000 is mapped first; after that the
 * watermark is at 0xFE004000, so DMA at 0xFE007000 needs a small
 * buddy skip which uart_device_map_page() handles automatically.
 *
 * Uses channels 0-6 (normal DMA). Each channel has a 256-byte register
 * block at offset channel * 0x100 from the base. Control blocks (CBs)
 * are allocated from DMA memory (uncacheable, physically contiguous)
 * and must be 32-byte aligned.
 *
 * Reference: BCM2711 ARM Peripherals, Section 4 (DMA Controller)
 */

#include "bcm_dma.h"
#include "uart_pl011.h"
#include "bcm2711_timer.h"
#include "dma_alloc.h"

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

static volatile uint32_t *dma_base = NULL;
static bool dma_engine_inited = false;

/* CB pool: one page = 4096 bytes / 32 bytes per CB = 128 CBs */
static dma_control_block_t *cb_pool_vaddr = NULL;
static uintptr_t cb_pool_paddr = 0;

/* Per-channel state */
typedef struct {
    bool allocated;
    bool active;
    uint32_t transfers;   /* Completed transfer count */
} dma_chan_state_t;

static dma_chan_state_t channels[DMA_ENGINE_MAX_CHANNELS];

/* ================================================================
 * Register Access (with DSB barrier)
 * ================================================================ */

static inline uint32_t dma_reg_read(uint32_t offset)
{
    uint32_t val = *(volatile uint32_t *)((uintptr_t)dma_base + offset);
    __asm__ volatile("dsb sy" ::: "memory");
    return val;
}

static inline void dma_reg_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)dma_base + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* Channel register offset: channel * 0x100 + register */
static inline uint32_t chan_offset(int ch, uint32_t reg)
{
    return (uint32_t)(ch * 0x100) + reg;
}

/* ================================================================
 * Internal Helpers
 * ================================================================ */

/* Get the CB for a given channel (one CB per channel from the pool) */
static dma_control_block_t *chan_cb_vaddr(int ch)
{
    return &cb_pool_vaddr[ch];
}

static uintptr_t chan_cb_paddr(int ch)
{
    return cb_pool_paddr + (uintptr_t)(ch * sizeof(dma_control_block_t));
}

/* Reset a channel: abort any transfer, then reset */
static void chan_reset(int ch)
{
    /* Abort if active */
    uint32_t cs = dma_reg_read(chan_offset(ch, DMA_CS));
    if (cs & DMA_CS_ACTIVE) {
        dma_reg_write(chan_offset(ch, DMA_CS), DMA_CS_ABORT);
        /* Brief spin for abort to take effect */
        for (volatile int i = 0; i < 100; i++) {}
    }

    /* Reset channel */
    dma_reg_write(chan_offset(ch, DMA_CS), DMA_CS_RESET);

    /* Brief spin for reset */
    for (volatile int i = 0; i < 100; i++) {}

    /* Clear END and INT flags */
    dma_reg_write(chan_offset(ch, DMA_CS), DMA_CS_END | DMA_CS_INT);
}

/* ================================================================
 * Public API
 * ================================================================ */

int dma_engine_init(void)
{
    if (dma_engine_inited) return DMA_ENGINE_OK;

    debug_puts("[DMA] Mapping DMA engine at paddr ");
    debug_hex(DMA_ENGINE_BASE_PADDR);
    debug_puts("\n");

    if (!uart_device_map_ready()) {
        debug_puts("[DMA] ERROR: device mapper not ready\n");
        return DMA_ENGINE_ERR_INIT;
    }

    if (!uart_device_map_page(DMA_ENGINE_BASE_PADDR, DMA_ENGINE_VADDR,
                              &dma_base)) {
        debug_puts("[DMA] ERROR: failed to map DMA register page\n");
        return DMA_ENGINE_ERR_INIT;
    }

    debug_puts("[DMA] Mapped OK at vaddr ");
    debug_hex((uint32_t)(uintptr_t)dma_base);
    debug_puts("\n");

    /* Allocate CB pool from DMA memory (one page = 128 CBs, 32B aligned) */
    if (!dma_alloc_is_ready()) {
        debug_puts("[DMA] ERROR: DMA allocator not ready\n");
        return DMA_ENGINE_ERR_ALLOC;
    }

    cb_pool_vaddr = (dma_control_block_t *)dma_alloc(4096, &cb_pool_paddr);
    if (!cb_pool_vaddr) {
        debug_puts("[DMA] ERROR: failed to allocate CB pool\n");
        return DMA_ENGINE_ERR_ALLOC;
    }

    debug_puts("[DMA] CB pool at paddr ");
    debug_hex((uint32_t)cb_pool_paddr);
    debug_puts(" vaddr ");
    debug_hex((uint32_t)(uintptr_t)cb_pool_vaddr);
    debug_puts("\n");

    /* Zero the CB pool */
    memset((void *)cb_pool_vaddr, 0, 4096);

    /* Initialize per-channel state */
    memset(channels, 0, sizeof(channels));

    /* Enable channels 0-6 in global enable register */
    uint32_t enable_mask = (1u << DMA_ENGINE_MAX_CHANNELS) - 1; /* 0x7F */
    dma_reg_write(DMA_ENABLE, dma_reg_read(DMA_ENABLE) | enable_mask);

    debug_puts("[DMA] Global ENABLE = ");
    debug_hex(dma_reg_read(DMA_ENABLE));
    debug_puts("\n");

    /* Reset all channels */
    for (int ch = 0; ch < DMA_ENGINE_MAX_CHANNELS; ch++) {
        chan_reset(ch);
    }

    dma_engine_inited = true;
    debug_puts("[DMA] DMA engine initialized OK (channels 0-6)\n");
    return DMA_ENGINE_OK;
}

int dma_chan_alloc(void)
{
    if (!dma_engine_inited) return -1;

    for (int ch = 0; ch < DMA_ENGINE_MAX_CHANNELS; ch++) {
        if (!channels[ch].allocated) {
            channels[ch].allocated = true;
            channels[ch].active = false;
            return ch;
        }
    }
    return -1; /* All channels busy */
}

int dma_chan_free(int channel)
{
    if (!dma_engine_inited) return DMA_ENGINE_ERR_INIT;
    if (channel < 0 || channel >= DMA_ENGINE_MAX_CHANNELS)
        return DMA_ENGINE_ERR_PARAM;
    if (!channels[channel].allocated)
        return DMA_ENGINE_ERR_PARAM;

    /* Reset the channel hardware */
    chan_reset(channel);

    channels[channel].allocated = false;
    channels[channel].active = false;
    return DMA_ENGINE_OK;
}

int dma_memcpy(int channel, uintptr_t dst_paddr, uintptr_t src_paddr,
               uint32_t len)
{
    if (!dma_engine_inited) return DMA_ENGINE_ERR_INIT;
    if (channel < 0 || channel >= DMA_ENGINE_MAX_CHANNELS)
        return DMA_ENGINE_ERR_PARAM;
    if (!channels[channel].allocated)
        return DMA_ENGINE_ERR_PARAM;
    if (len == 0) return DMA_ENGINE_OK;

    /* Check channel is not currently active */
    uint32_t cs = dma_reg_read(chan_offset(channel, DMA_CS));
    if (cs & DMA_CS_ACTIVE) return DMA_ENGINE_ERR_BUSY;

    /* Fill the control block */
    dma_control_block_t *cb = chan_cb_vaddr(channel);
    cb->ti        = DMA_TI_MEM2MEM;  /* SRC_INC | DEST_INC */
    cb->source_ad = DMA_BUS_ADDR(src_paddr);
    cb->dest_ad   = DMA_BUS_ADDR(dst_paddr);
    cb->txfr_len  = len;
    cb->stride    = 0;
    cb->nextconbk = 0;  /* Single transfer, no chaining */
    cb->reserved[0] = 0;
    cb->reserved[1] = 0;

    /* Memory barrier to ensure CB is visible to DMA engine */
    __asm__ volatile("dsb sy" ::: "memory");

    /* Clear any previous END/INT/ERROR flags */
    dma_reg_write(chan_offset(channel, DMA_CS),
                  DMA_CS_END | DMA_CS_INT | DMA_CS_ERROR);

    /* Point channel at the CB (bus address) */
    dma_reg_write(chan_offset(channel, DMA_CONBLK_AD),
                  DMA_BUS_ADDR(chan_cb_paddr(channel)));

    /* Start the transfer */
    dma_reg_write(chan_offset(channel, DMA_CS),
                  DMA_CS_WAIT_WRITES | DMA_CS_DISDEBUG | DMA_CS_ACTIVE);

    channels[channel].active = true;
    return DMA_ENGINE_OK;
}

int dma_wait(int channel, uint32_t timeout_us)
{
    if (!dma_engine_inited) return DMA_ENGINE_ERR_INIT;
    if (channel < 0 || channel >= DMA_ENGINE_MAX_CHANNELS)
        return DMA_ENGINE_ERR_PARAM;

    if (systimer_is_initialized()) {
        uint64_t start = systimer_read();
        while (1) {
            uint32_t cs = dma_reg_read(chan_offset(channel, DMA_CS));

            if (cs & DMA_CS_ERROR) {
                /* Transfer error */
                debug_puts("[DMA] ERROR on channel ");
                debug_hex((uint32_t)channel);
                debug_puts(" DEBUG=");
                debug_hex(dma_reg_read(chan_offset(channel, DMA_DEBUG)));
                debug_puts("\n");
                channels[channel].active = false;
                /* Clear error and end flags */
                dma_reg_write(chan_offset(channel, DMA_CS),
                              DMA_CS_END | DMA_CS_INT | DMA_CS_ERROR);
                return DMA_ENGINE_ERR_INIT;
            }

            if (cs & DMA_CS_END) {
                /* Transfer complete */
                dma_reg_write(chan_offset(channel, DMA_CS),
                              DMA_CS_END | DMA_CS_INT);
                channels[channel].active = false;
                channels[channel].transfers++;
                return DMA_ENGINE_OK;
            }

            if (systimer_read() - start > timeout_us) {
                return DMA_ENGINE_ERR_TIMEOUT;
            }
        }
    } else {
        /* Fallback: spin counter */
        uint32_t spins = 0;
        uint32_t max_spins = timeout_us * 10; /* rough approximation */
        while (1) {
            uint32_t cs = dma_reg_read(chan_offset(channel, DMA_CS));

            if (cs & DMA_CS_ERROR) {
                channels[channel].active = false;
                dma_reg_write(chan_offset(channel, DMA_CS),
                              DMA_CS_END | DMA_CS_INT | DMA_CS_ERROR);
                return DMA_ENGINE_ERR_INIT;
            }

            if (cs & DMA_CS_END) {
                dma_reg_write(chan_offset(channel, DMA_CS),
                              DMA_CS_END | DMA_CS_INT);
                channels[channel].active = false;
                channels[channel].transfers++;
                return DMA_ENGINE_OK;
            }

            if (++spins > max_spins) {
                return DMA_ENGINE_ERR_TIMEOUT;
            }
        }
    }
}

int dma_cancel(int channel)
{
    if (!dma_engine_inited) return DMA_ENGINE_ERR_INIT;
    if (channel < 0 || channel >= DMA_ENGINE_MAX_CHANNELS)
        return DMA_ENGINE_ERR_PARAM;

    chan_reset(channel);
    channels[channel].active = false;
    return DMA_ENGINE_OK;
}

bool dma_engine_is_initialized(void)
{
    return dma_engine_inited;
}

int dma_engine_get_status(char *output, uint32_t output_size)
{
    if (!output || output_size == 0) return 0;

    if (!dma_engine_inited) {
        return snprintf(output, output_size, "DMA Engine: not initialized\n");
    }

    int n = 0;
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "DMA Engine: BCM2711\n");
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  Channels: 0-6 (normal DMA)\n");
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  Global ENABLE: 0x%08x\n",
                  dma_reg_read(DMA_ENABLE));
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  INT_STATUS: 0x%08x\n",
                  dma_reg_read(DMA_INT_STATUS));
    n += snprintf(output + n, output_size - (uint32_t)n,
                  "  CB pool paddr: 0x%08x\n",
                  (uint32_t)cb_pool_paddr);

    for (int ch = 0; ch < DMA_ENGINE_MAX_CHANNELS; ch++) {
        uint32_t cs = dma_reg_read(chan_offset(ch, DMA_CS));
        const char *state = "free";
        if (channels[ch].allocated && channels[ch].active)
            state = "ACTIVE";
        else if (channels[ch].allocated)
            state = "alloc";

        n += snprintf(output + n, output_size - (uint32_t)n,
                      "  CH%d: %s  CS=0x%08x  xfers=%u\n",
                      ch, state, cs, channels[ch].transfers);
    }

    return n;
}
