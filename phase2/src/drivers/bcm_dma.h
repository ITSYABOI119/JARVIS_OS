/*
 * JARVIS AI-OS - BCM2711 DMA Engine Driver
 *
 * Week 48: DMA controller for memory-to-memory transfers.
 * DMA registers at paddr 0xFE007000, mapped via uart_device_map_page().
 *
 * Uses channels 0-6 only (normal DMA, supports 2D stride and wide
 * addresses). Channels 7-14 are lite DMA (skipped). Channel 15 is
 * at a separate address and also skipped.
 *
 * Must be initialized AFTER systimer (0xFE003000) and BEFORE mailbox
 * (0xFE00B000) in the boot sequence due to seL4 forward-only device
 * untyped watermark.
 *
 * Reference: BCM2711 ARM Peripherals, Section 4 (DMA Controller)
 */

#ifndef BCM_DMA_H
#define BCM_DMA_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * DMA Engine Physical Address
 * ================================================================ */
#define DMA_ENGINE_BASE_PADDR   0xFE007000u

/* ================================================================
 * DMA Control Block (must be 32-byte aligned)
 * ================================================================ */
typedef struct __attribute__((packed)) {
    uint32_t ti;          /* Transfer Information */
    uint32_t source_ad;   /* Source Address (bus address) */
    uint32_t dest_ad;     /* Destination Address (bus address) */
    uint32_t txfr_len;    /* Transfer Length */
    uint32_t stride;      /* 2D Stride (0 for linear) */
    uint32_t nextconbk;   /* Next CB Address (0 = end of chain) */
    uint32_t reserved[2]; /* Padding to 32 bytes */
} dma_control_block_t;

/* ================================================================
 * Per-Channel Register Offsets (add channel * 0x100 to base)
 * ================================================================ */
#define DMA_CS              0x00    /* Control and Status */
#define DMA_CONBLK_AD       0x04    /* Control Block Address */
#define DMA_TI              0x08    /* Transfer Information (read-only copy) */
#define DMA_SOURCE_AD       0x0C    /* Source Address (read-only copy) */
#define DMA_DEST_AD         0x10    /* Destination Address (read-only copy) */
#define DMA_TXFR_LEN        0x14    /* Transfer Length (read-only copy) */
#define DMA_STRIDE          0x18    /* 2D Stride (read-only copy) */
#define DMA_NEXTCONBK       0x1C    /* Next CB Address (read-only copy) */
#define DMA_DEBUG           0x20    /* Debug register */

/* Global registers (offset from base) */
#define DMA_INT_STATUS      0xFE0   /* Interrupt status (bit per channel) */
#define DMA_ENABLE          0xFF0   /* Global enable (bit per channel) */

/* ================================================================
 * CS Register Bits
 * ================================================================ */
#define DMA_CS_RESET        (1u << 31)  /* Write 1 to reset channel */
#define DMA_CS_ABORT        (1u << 30)  /* Write 1 to abort transfer */
#define DMA_CS_DISDEBUG     (1u << 29)  /* Disable debug pause */
#define DMA_CS_WAIT_WRITES  (1u << 28)  /* Wait for outstanding writes */
#define DMA_CS_INT          (1u << 8)   /* Interrupt status (W1C) */
#define DMA_CS_DREQ_STOPS   (1u << 5)   /* DREQ stops DMA */
#define DMA_CS_PAUSED       (1u << 4)   /* Paused state */
#define DMA_CS_DREQ         (1u << 3)   /* DREQ state */
#define DMA_CS_ERROR        (1u << 2)   /* Transfer error */
#define DMA_CS_END          (1u << 1)   /* Transfer complete (W1C) */
#define DMA_CS_ACTIVE       (1u << 0)   /* Set to start, cleared when done */

/* ================================================================
 * TI (Transfer Information) Bits
 * ================================================================ */
#define DMA_TI_NO_WIDE_BURSTS  (1u << 26)
#define DMA_TI_SRC_INC         (1u << 8)   /* Source address increment */
#define DMA_TI_DEST_INC        (1u << 4)   /* Dest address increment */
#define DMA_TI_TDMODE          (1u << 1)   /* 2D mode */
#define DMA_TI_INTEN           (1u << 0)   /* Interrupt enable */

/* Memory-to-memory: SRC_INC | DEST_INC */
#define DMA_TI_MEM2MEM        (DMA_TI_SRC_INC | DMA_TI_DEST_INC)

/* ================================================================
 * Bus Address Conversion
 * ================================================================ */
#define DMA_BUS_ADDR(paddr)    ((uint32_t)((paddr) | 0xC0000000u))

/* ================================================================
 * Constants
 * ================================================================ */
#define DMA_ENGINE_MAX_CHANNELS  7       /* Use channels 0-6 only */
#define DMA_ENGINE_TIMEOUT_US    1000000 /* 1 second default */
#define DMA_ENGINE_VADDR         0x612000u /* Next free after PM watchdog */

/* ================================================================
 * Error Codes
 * ================================================================ */
#define DMA_ENGINE_OK            0
#define DMA_ENGINE_ERR_INIT     (-1)
#define DMA_ENGINE_ERR_PARAM    (-2)
#define DMA_ENGINE_ERR_BUSY     (-3)
#define DMA_ENGINE_ERR_TIMEOUT  (-4)
#define DMA_ENGINE_ERR_ALLOC    (-5)

/* ================================================================
 * Public API
 * ================================================================ */

/*
 * Initialize DMA engine driver.
 * Maps DMA controller register page at 0xFE007000.
 * Allocates a CB pool page from dma_alloc().
 * Must be called AFTER systimer_init() and BEFORE mailbox init.
 * Returns 0 on success, negative error code on failure.
 */
int dma_engine_init(void);

/*
 * Allocate a DMA channel (0-6).
 * Returns channel number on success, -1 if all channels are busy.
 */
int dma_chan_alloc(void);

/*
 * Free a previously allocated DMA channel.
 * Resets the channel hardware and marks it available.
 * Returns 0 on success, negative error code on failure.
 */
int dma_chan_free(int channel);

/*
 * Start a memory-to-memory DMA copy.
 * dst_paddr and src_paddr are ARM physical addresses (not bus addresses).
 * The driver converts to bus addresses internally.
 * len is the transfer size in bytes.
 * Returns 0 on success, negative error code on failure.
 */
int dma_memcpy(int channel, uintptr_t dst_paddr, uintptr_t src_paddr,
               uint32_t len);

/*
 * Wait for a DMA transfer to complete (polling).
 * timeout_us is the maximum wait time in microseconds.
 * Returns 0 on success, DMA_ENGINE_ERR_TIMEOUT on timeout,
 * or DMA_ENGINE_ERR_INIT if an error occurred during transfer.
 */
int dma_wait(int channel, uint32_t timeout_us);

/*
 * Cancel an in-progress DMA transfer.
 * Aborts and resets the channel.
 * Returns 0 on success, negative error code on failure.
 */
int dma_cancel(int channel);

/*
 * Check if DMA engine driver is initialized.
 */
bool dma_engine_is_initialized(void);

/*
 * Get DMA engine status string for shell commands.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL).
 */
int dma_engine_get_status(char *output, uint32_t output_size);

#endif /* BCM_DMA_H */
