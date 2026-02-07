/*
 * JARVIS AI-OS - BCM2711 EMMC2 (SDHCI) Driver
 *
 * Minimal SD card bring-up and single-block read via PIO.
 * Week 35: probe + identify + read LBA0.
 */

#include "emmc_sdhci.h"
#include "uart_pl011.h"

#include <sel4/sel4.h>
#include <string.h>

/* Debug switch for write-path instrumentation */
#ifndef EMMC_DEBUG
#define EMMC_DEBUG 0
#endif

/*
 * BCM2711 EMMC2 / SDHCI (Arasan)
 *
 * Base: EMMC2_BASE (0xFE340000)
 * Key registers used:
 *  - ARG1, CMDTM, RESP0-3
 *  - BLKSIZECNT, DATA
 *  - STATUS, CONTROL0/1, INTERRUPT
 */
/* Register offsets (SDHCI) */
#define EMMC_ARG2        0x00
#define EMMC_BLKSIZECNT  0x04
#define EMMC_ARG1        0x08
#define EMMC_CMDTM       0x0C
#define EMMC_RESP0       0x10
#define EMMC_RESP1       0x14
#define EMMC_RESP2       0x18
#define EMMC_RESP3       0x1C
#define EMMC_DATA        0x20
#define EMMC_STATUS      0x24
#define EMMC_CONTROL0    0x28
#define EMMC_CONTROL1    0x2C
#define EMMC_INTERRUPT   0x30
#define EMMC_IRPT_MASK   0x34
#define EMMC_IRPT_EN     0x38
#define EMMC_CONTROL2    0x3C
#define EMMC_CAPABILITIES 0x40
#define EMMC_CAPABILITIES_1 0x44
#define EMMC_SLOTISR_VER 0xFC

/* ADMA2 registers (SDHCI 3.0 spec) */
#define EMMC_ADMA_ERR        0x54  /* ADMA Error Status */
#define EMMC_ADMA_SYS_ADDR   0x58  /* ADMA System Address (32-bit) */

/* CONTROL0 DMA Select bits (bits 4:3) */
#define CTRL0_DMA_SEL_MASK   (0x3u << 3)
#define CTRL0_DMA_SEL_SDMA   (0x0u << 3)
#define CTRL0_DMA_SEL_ADMA2  (0x2u << 3)  /* ADMA2 32-bit addressing */

/* ADMA Error Status bits */
#define ADMA_ERR_STATE_MASK  (0x3u << 0)
#define ADMA_ERR_LEN_MISMATCH (1u << 2)

/* CONTROL1 bits */
#define C1_CLK_INTLEN    (1u << 0)
#define C1_CLK_STABLE    (1u << 1)
#define C1_CLK_EN        (1u << 2)
#define C1_DTOCV_SHIFT   16          /* Data Timeout Counter Value (bits 16-19) */
#define C1_DTOCV_MASK    (0xFu << 16)
#define C1_DTOCV_MAX     (0xEu << 16) /* Max timeout = 2^27 TMCLK cycles (matches GPU firmware) */
#define C1_SRST_HC       (1u << 24)
#define C1_SRST_CMD      (1u << 25)
#define C1_SRST_DATA     (1u << 26)

/* CONTROL0 power bits (SDHCI power control byte at offset 0x29) */
#define CTRL0_POWER_MASK  (0xFu << 8)
#define CTRL0_POWER_330   (0x0Fu << 8)

/* STATUS bits */
#define STATUS_CMD_INHIBIT  (1u << 0)
#define STATUS_DATA_INHIBIT (1u << 1)

/* INTERRUPT bits */
#define INT_CMD_DONE     (1u << 0)
#define INT_DATA_DONE    (1u << 1)
#define INT_WRITE_RDY    (1u << 4)
#define INT_READ_RDY     (1u << 5)
#define INT_ERROR_MASK      0xFFFF0000
#define INT_CMD_ERROR_MASK  (0x000F0000)  /* Bits 16-19: CMD timeout, CRC, end bit, index */
#define INT_DATA_ERROR_MASK (0x00700000)  /* Bits 20-22: DATA timeout, CRC, end bit */
#define INT_ADMA_ERR        (1u << 25)    /* ADMA Error */
#define INT_CMD_TIMEOUT  (1u << 16)
#define INT_CMD_CRC      (1u << 17)
#define INT_CMD_END_BIT  (1u << 18)
#define INT_CMD_INDEX    (1u << 19)
#define INT_DATA_TIMEOUT (1u << 20)
#define INT_DATA_CRC     (1u << 21)
#define INT_DATA_END_BIT (1u << 22)

/* CMDTM bits */
#define CMDTM_CMD_INDEX_SHIFT 24
#define CMDTM_RSPNS_TYPE_NONE (0u << 16)
#define CMDTM_RSPNS_TYPE_136  (1u << 16)
#define CMDTM_RSPNS_TYPE_48   (2u << 16)
#define CMDTM_RSPNS_TYPE_48B  (3u << 16)
#define CMDTM_CRCCHK_EN       (1u << 19)
#define CMDTM_IXCHK_EN        (1u << 20)
#define CMDTM_ISDATA          (1u << 21)
#define CMDTM_DAT_DIR_READ    (1u << 4)
#define CMDTM_BLKCNT_EN       (1u << 1)

/* SD commands */
#define CMD0_GO_IDLE      0
#define CMD2_ALL_SEND_CID 2
#define CMD3_SEND_REL_ADDR 3
#define CMD6_SWITCH_FUNC  6
#define CMD7_SELECT_CARD  7
#define CMD8_SEND_IF_COND 8
#define CMD9_SEND_CSD     9
#define CMD12_STOP_TRANSMISSION 12
#define CMD16_SET_BLOCKLEN 16
#define CMD17_READ_SINGLE 17
#define CMD18_READ_MULTIPLE 18
#define CMD24_WRITE_SINGLE 24
#define CMD25_WRITE_MULTIPLE 25
#define CMD55_APP_CMD     55
#define ACMD6_SET_BUS_WIDTH 6
#define ACMD41_SD_OP_COND 41

/* CONTROL0 bus width bits */
#define CTRL0_4BIT_MODE   (1u << 1)   /* 4-bit data bus width */
#define CTRL0_HISPD_EN    (1u << 2)   /* High Speed Enable */

/* CMDTM multi-block bits */
#define CMDTM_AUTO_CMD12  (1u << 2)
#define CMDTM_MULTI_BLOCK (1u << 5)
#define CMDTM_DMA_EN      (1u << 0)  /* DMA Enable for data transfer */

/* Clock */
#define EMMC_BASE_CLOCK_HZ 50000000u

/* Retry count for transient errors */
#define EMMC_MAX_RETRIES 3

/* ================================================================
 * ADMA2 DMA Support (SDHCI 3.0 spec, 32-bit addressing)
 * ================================================================
 * ADMA2 uses a scatter-gather descriptor table in RAM.
 * Each descriptor is 8 bytes:
 *   - attr (16 bits): Valid, End, Int, Act[1:0]
 *   - length (16 bits): 0 = 65536 bytes, otherwise actual
 *   - address (32 bits): physical address of data buffer
 */

#if EMMC_USE_ADMA2

/* Include DMA allocator for proper physical address handling */
#include "dma_alloc.h"

/* ADMA2 Attribute bits */
#define ADMA2_ATTR_VALID   (1u << 0)
#define ADMA2_ATTR_END     (1u << 1)
#define ADMA2_ATTR_INT     (1u << 2)
#define ADMA2_ACT_NOP      (0u << 4)
#define ADMA2_ACT_TRAN     (2u << 4)  /* Transfer data */
#define ADMA2_ACT_LINK     (3u << 4)  /* Link to next descriptor table */

/* ADMA2 Descriptor (must be 8-byte aligned per SDHCI spec) */
typedef struct __attribute__((packed, aligned(8))) {
    uint16_t attr;       /* Attribute + reserved bits */
    uint16_t length;     /* Transfer length (0 = 65536) */
    uint32_t address;    /* 32-bit physical address */
} adma2_desc_t;

/* Maximum descriptors (one per 64KB chunk, 32 = 2MB max transfer) */
#define ADMA2_MAX_DESCRIPTORS 32

/*
 * DMA buffers - NOW DYNAMICALLY ALLOCATED via dma_alloc()
 *
 * Week 36 hardware test proved vaddr != paddr (not identity mapped):
 *   jarvis-sel4 paddr = 0x1257000..0x1480fff
 *   jarvis-sel4 vaddr = 0x400000..0x629fff
 *
 * Static buffers have UNKNOWN paddr. The DMA engine needs REAL physical
 * addresses. The ONLY way to know paddr is to track it when allocating
 * from untyped memory via seL4_Untyped_Retype().
 */
static adma2_desc_t *adma2_desc_table = NULL;  /* Allocated by dma_alloc() */
static uint8_t *adma2_buffer = NULL;           /* Allocated by dma_alloc() */
static uintptr_t adma2_desc_paddr = 0;         /* Tracked by DMA allocator */
static uintptr_t adma2_buffer_paddr = 0;       /* Tracked by DMA allocator */
static bool adma2_initialized = false;

/* Maximum physical address we trust for DMA (1GB - typical Pi 4 low memory) */
#define ADMA2_MAX_PADDR 0x40000000UL

/* Forward declarations for debug helpers (defined later) */
static void debug_puts(const char *s);
static void debug_hex(uint32_t val);

/* ARM64 cache maintenance for DMA coherency.
 * Cortex-A72 cache line size is 64 bytes.
 *
 * CRITICAL: Proper barrier sequence for DMA:
 * - Before DMA read (device->RAM): invalidate buffer, dsb, start DMA
 * - After DMA read: dsb, invalidate again (paranoia for speculative fills)
 * - Before DMA write (RAM->device): clean buffer, dsb, start DMA
 */

/* Data Synchronization Barrier - ensures all cache ops complete */
static inline void dsb_sy(void)
{
    asm volatile("dsb sy" ::: "memory");
}

/* Instruction Synchronization Barrier - flushes pipeline */
static inline void isb(void)
{
    asm volatile("isb" ::: "memory");
}

/* Clean + Invalidate single cache line (write back dirty, then discard) */
static inline void dcache_civac(void *addr)
{
    asm volatile("dc civac, %0" :: "r"(addr) : "memory");
}

/*
 * Invalidate single cache line (discard, use before DMA read).
 *
 * NOTE: Uses DC CIVAC (clean+invalidate) instead of DC IVAC (invalidate only)
 * because DC IVAC is EL1-only by default on ARMv8. seL4 runs the rootserver
 * at EL0 where DC IVAC traps (ESR EC=0x18). DC CIVAC is EL0-accessible and
 * is the safe equivalent - it writes back dirty data first, then invalidates.
 */
static inline void dcache_ivac(void *addr)
{
    asm volatile("dc civac, %0" :: "r"(addr) : "memory");
}

/* Clean region - write back dirty data to RAM before DMA write */
static void dcache_clean_region(void *addr, size_t size)
{
    uintptr_t start = (uintptr_t)addr & ~63UL;  /* 64-byte cache line */
    uintptr_t end = ((uintptr_t)addr + size + 63) & ~63UL;

    for (uintptr_t a = start; a < end; a += 64) {
        dcache_civac((void *)a);
    }
    dsb_sy();  /* Ensure all cleans complete before proceeding */
}

/* Invalidate region - discard cached data before reading DMA'd data */
static void dcache_invalidate_region(void *addr, size_t size)
{
    uintptr_t start = (uintptr_t)addr & ~63UL;
    uintptr_t end = ((uintptr_t)addr + size + 63) & ~63UL;

    for (uintptr_t a = start; a < end; a += 64) {
        dcache_ivac((void *)a);
    }
    dsb_sy();  /* Ensure all invalidates complete before proceeding */
}

#endif /* EMMC_USE_ADMA2 */

static volatile uint32_t *emmc_mmio_base;
static uint32_t emmc_rca;
static bool emmc_high_capacity;
static uint32_t emmc_last_cmd;
static uint32_t emmc_last_arg;
static uint32_t emmc_last_wait_inhibit;
static uint32_t emmc_cid[4];    /* Card ID from CMD2 */
static uint64_t emmc_capacity;  /* Card capacity in bytes (from CSD or OCR) */

static inline void emmc_dsb(void)
{
    asm volatile("dsb sy" ::: "memory");
}

static inline uint32_t emmc_reg_read(uint32_t offset)
{
    uint32_t val = emmc_mmio_base[offset / 4];
    emmc_dsb();
    return val;
}

static inline void emmc_reg_write(uint32_t offset, uint32_t val)
{
    emmc_mmio_base[offset / 4] = val;
    emmc_dsb();
}

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            seL4_DebugPutChar('\r');
        }
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

static void emmc_dump_state(const char *tag)
{
    if (!emmc_mmio_base) {
        return;
    }
    debug_puts("[EMMC] ");
    debug_puts(tag);
    debug_puts(" cmd=");
    debug_hex(emmc_last_cmd);
    debug_puts(" arg=");
    debug_hex(emmc_last_arg);
    debug_puts(" status=");
    debug_hex(emmc_reg_read(EMMC_STATUS));
    debug_puts(" irpt=");
    debug_hex(emmc_reg_read(EMMC_INTERRUPT));
    debug_puts(" ctrl1=");
    debug_hex(emmc_reg_read(EMMC_CONTROL1));
    debug_puts("\n");
}

#if EMMC_DEBUG
static void emmc_dbg_dump_regs(const char *tag)
{
    debug_puts("[EMMC][DUMP] ");
    debug_puts(tag);
    debug_puts(" irpt=");
    debug_hex(emmc_reg_read(EMMC_INTERRUPT));
    debug_puts(" status=");
    debug_hex(emmc_reg_read(EMMC_STATUS));
    debug_puts(" ctrl0=");
    debug_hex(emmc_reg_read(EMMC_CONTROL0));
    debug_puts(" ctrl1=");
    debug_hex(emmc_reg_read(EMMC_CONTROL1));
    debug_puts("\n");

    debug_puts("[EMMC][DUMP] ");
    debug_puts(tag);
    debug_puts(" blksizecnt=");
    debug_hex(emmc_reg_read(EMMC_BLKSIZECNT));
    debug_puts(" arg1=");
    debug_hex(emmc_reg_read(EMMC_ARG1));
    debug_puts(" cmdtm=");
    debug_hex(emmc_reg_read(EMMC_CMDTM));
    debug_puts(" adma=");
    debug_hex(emmc_reg_read(EMMC_ADMA_SYS_ADDR));
    debug_puts("\n");
}

static void emmc_dbg_decode_irpt(uint32_t irpt)
{
    debug_puts("[EMMC][DBG] IRPT bits:");
    if (irpt & INT_CMD_DONE) debug_puts(" CMD_DONE");
    if (irpt & INT_WRITE_RDY) debug_puts(" WRITE_RDY");
    if (irpt & INT_READ_RDY) debug_puts(" READ_RDY");
    if (irpt & INT_DATA_DONE) debug_puts(" DATA_DONE");
    if (irpt & INT_ERROR_MASK) debug_puts(" ERROR");
    if (irpt & INT_CMD_TIMEOUT) debug_puts(" CMD_TIMEOUT");
    if (irpt & INT_CMD_CRC) debug_puts(" CMD_CRC");
    if (irpt & INT_CMD_END_BIT) debug_puts(" CMD_END_BIT");
    if (irpt & INT_CMD_INDEX) debug_puts(" CMD_INDEX");
    if (irpt & INT_DATA_TIMEOUT) debug_puts(" DATA_TIMEOUT");
    if (irpt & INT_DATA_CRC) debug_puts(" DATA_CRC");
    if (irpt & INT_DATA_END_BIT) debug_puts(" DATA_END_BIT");
    debug_puts("\n");
}
#else
static inline void emmc_dbg_dump_regs(const char *tag) { (void)tag; }
static inline void emmc_dbg_decode_irpt(uint32_t irpt) { (void)irpt; }
#endif
static void emmc_dump_identity(void)
{
    debug_puts("[EMMC] ID CAP0=");
    debug_hex(emmc_reg_read(EMMC_CAPABILITIES));
    debug_puts(" CAP1=");
    debug_hex(emmc_reg_read(EMMC_CAPABILITIES_1));
    debug_puts(" SLOTISR_VER=");
    debug_hex(emmc_reg_read(EMMC_SLOTISR_VER));
    debug_puts("\n");
}

static void emmc_dump_cmd_prep(const char *tag, uint32_t cmdtm, uint32_t arg)
{
    uint32_t status = emmc_reg_read(EMMC_STATUS);
    debug_puts("[EMMC] ");
    debug_puts(tag);
    debug_puts(" status=");
    debug_hex(status);
    debug_puts(" ctrl0=");
    debug_hex(emmc_reg_read(EMMC_CONTROL0));
    debug_puts(" ctrl1=");
    debug_hex(emmc_reg_read(EMMC_CONTROL1));
    debug_puts(" irpt=");
    debug_hex(emmc_reg_read(EMMC_INTERRUPT));
    debug_puts(" irpt_en=");
    debug_hex(emmc_reg_read(EMMC_IRPT_EN));
    debug_puts(" irpt_mask=");
    debug_hex(emmc_reg_read(EMMC_IRPT_MASK));
    debug_puts(" cmd_inh=");
    debug_hex((status & STATUS_CMD_INHIBIT) ? 1 : 0);
    debug_puts(" dat_inh=");
    debug_hex((status & STATUS_DATA_INHIBIT) ? 1 : 0);
    debug_puts(" arg=");
    debug_hex(arg);
    debug_puts(" cmdtm=");
    debug_hex(cmdtm);
    debug_puts("\n");
}

static void emmc_set_power(void)
{
    uint32_t ctrl0 = emmc_reg_read(EMMC_CONTROL0);
    ctrl0 &= ~CTRL0_POWER_MASK;
    ctrl0 |= CTRL0_POWER_330;
    emmc_reg_write(EMMC_CONTROL0, ctrl0);

    debug_puts("[EMMC] CONTROL0 power=");
    debug_hex(ctrl0);
    debug_puts("\n");
}

void emmc_set_mmio_base(volatile uint32_t *base)
{
    emmc_mmio_base = base;
}

bool emmc_is_mapped(void)
{
    return emmc_mmio_base != NULL;
}

bool emmc_map_device_frame(void)
{
    if (emmc_mmio_base) {
        return true;
    }
    if (!uart_device_map_ready()) {
        debug_puts("[EMMC] ERROR: device mapper not ready\n");
        return false;
    }

    debug_puts("[EMMC] Mapping EMMC2 at paddr=");
    debug_hex(EMMC2_BASE);
    debug_puts("\n");

    if (!uart_device_map_page(EMMC2_BASE, EMMC2_MMIO_VADDR, &emmc_mmio_base)) {
        debug_puts("[EMMC] ERROR: MMIO map failed\n");
        return false;
    }
    emmc_set_mmio_base(emmc_mmio_base);
    debug_puts("[EMMC] MMIO mapped vaddr=");
    debug_hex((uint32_t)(seL4_Word)emmc_mmio_base);
    debug_puts("\n");

    /* Verify mapping by reading EMMC capability register (should not be 0x6770696f "gpio") */
    uint32_t cap0 = emmc_reg_read(0x40);  /* EMMC_CAP0 offset */
    debug_puts("[EMMC] Verify CAP0=");
    debug_hex(cap0);
    if (cap0 == 0x6770696f) {
        debug_puts(" ERROR: got 'gpio' - wrong paddr!\n");
    } else {
        debug_puts(" OK\n");
    }

    return true;
}

static bool emmc_wait_inhibit(uint32_t mask, uint32_t timeout)
{
    uint32_t waited = 0;
    while (timeout--) {
        waited++;
        if ((emmc_reg_read(EMMC_STATUS) & mask) == 0) {
            emmc_last_wait_inhibit = waited;
            return true;
        }
    }
    emmc_last_wait_inhibit = waited;
    return false;
}

static bool emmc_wait_interrupt(uint32_t mask, uint32_t timeout)
{
    while (timeout--) {
        uint32_t irpt = emmc_reg_read(EMMC_INTERRUPT);
        if (irpt & INT_ERROR_MASK) {
            emmc_dump_state("INT_ERROR");
            emmc_reg_write(EMMC_INTERRUPT, irpt);
            return false;
        }
        if (irpt & mask) {
            emmc_reg_write(EMMC_INTERRUPT, mask);
            return true;
        }
    }
    return false;
}

/* Wait for interrupt during write operations.
 * Ignores DATA_TIMEOUT (bit 20) because BCM2711 EMMC2 continuously
 * re-asserts DATA_TIMEOUT during write command data phase - this is a
 * hardware quirk, not a real timeout.  Only non-DATA_TIMEOUT errors
 * (CMD errors, DATA_CRC, DATA_END_BIT, ADMA) are treated as fatal.
 * When the desired bit fires, any stale DATA_TIMEOUT is cleared too. */
static bool emmc_wait_interrupt_write(uint32_t mask, uint32_t timeout)
{
    while (timeout--) {
        uint32_t irpt = emmc_reg_read(EMMC_INTERRUPT);
        /* Check for the desired bit FIRST (before errors) so that
         * a simultaneous WRITE_RDY + DATA_TIMEOUT is treated as success */
        if (irpt & mask) {
            /* Clear requested bit + any spurious DATA_TIMEOUT */
            emmc_reg_write(EMMC_INTERRUPT, mask | INT_DATA_TIMEOUT);
            return true;
        }
        /* Check for real errors (exclude DATA_TIMEOUT) */
        uint32_t real_errors = irpt & (INT_ERROR_MASK & ~INT_DATA_TIMEOUT);
        if (real_errors) {
            emmc_dump_state("INT_ERROR (write)");
            emmc_reg_write(EMMC_INTERRUPT, irpt);
            return false;
        }
    }
    return false;
}

/* Wait for CMD_DONE with selective error checking.
 * Only checks command-phase errors (bits 16-19), ignores DATA_TIMEOUT (bit 20).
 * This is needed for BCM2711 SDHCI quirk where DATA_TIMEOUT can fire during
 * the command phase for write commands with ISDATA flag. */
static bool emmc_wait_cmd_done(uint32_t timeout)
{
    while (timeout--) {
        uint32_t irpt = emmc_reg_read(EMMC_INTERRUPT);
        /* Only check command-phase errors, ignore DATA_TIMEOUT */
        if (irpt & INT_CMD_ERROR_MASK) {
            emmc_dump_state("CMD_ERROR");
            emmc_reg_write(EMMC_INTERRUPT, irpt);
            return false;
        }
        if (irpt & INT_CMD_DONE) {
            /* Clear CMD_DONE and any stale data errors from command phase
             * (BCM2711 quirk: DATA_TIMEOUT can fire during command phase for writes) */
            emmc_reg_write(EMMC_INTERRUPT, INT_CMD_DONE | INT_DATA_ERROR_MASK);
            return true;
        }
    }
    return false;
}

static bool emmc_reset(void)
{
    uint32_t ctrl1 = emmc_reg_read(EMMC_CONTROL1);
    ctrl1 |= C1_SRST_HC | C1_SRST_CMD | C1_SRST_DATA;
    emmc_reg_write(EMMC_CONTROL1, ctrl1);

    for (uint32_t i = 0; i < 1000000; i++) {
        ctrl1 = emmc_reg_read(EMMC_CONTROL1);
        if ((ctrl1 & (C1_SRST_HC | C1_SRST_CMD | C1_SRST_DATA)) == 0) {
            return true;
        }
    }
    emmc_dump_state("RESET timeout");
    return false;
}

static bool emmc_set_clock(uint32_t freq_hz)
{
    if (freq_hz == 0) {
        return false;
    }

    uint32_t div = (EMMC_BASE_CLOCK_HZ + (freq_hz * 2 - 1)) / (freq_hz * 2);
    if (div < 2) {
        div = 2;
    }
    if (div > 0x3FF) {
        div = 0x3FF;
    }

    uint32_t div_lo = div & 0xFF;
    uint32_t div_hi = (div >> 8) & 0x3;

    uint32_t ctrl1 = emmc_reg_read(EMMC_CONTROL1);
    /* Clear clock divider, clock enable, AND data timeout - then set them */
    ctrl1 &= ~((0xFFu << 8) | (0x3u << 6) | C1_CLK_EN | C1_DTOCV_MASK);
    /* Week 36 fix: Set DTOCV to max (0xE) to avoid DATA_TIMEOUT on writes
     * This matches GPU firmware setting (C1: 0x000e0207) */
    ctrl1 |= (div_lo << 8) | (div_hi << 6) | C1_CLK_INTLEN | C1_DTOCV_MAX;
    emmc_reg_write(EMMC_CONTROL1, ctrl1);

    bool stable = false;
    for (uint32_t i = 0; i < 1000000; i++) {
        if (emmc_reg_read(EMMC_CONTROL1) & C1_CLK_STABLE) {
            stable = true;
            break;
        }
    }
    if (!stable) {
        debug_puts("[EMMC] ERROR: clock not stable\n");
        return false;
    }

    ctrl1 |= C1_CLK_EN;
    emmc_reg_write(EMMC_CONTROL1, ctrl1);
    debug_puts("[EMMC] CONTROL1 clock=");
    debug_hex(emmc_reg_read(EMMC_CONTROL1));
    debug_puts("\n");
    return true;
}

static bool emmc_send_cmd(uint32_t cmd_idx, uint32_t arg, uint32_t cmdtm, uint32_t *resp)
{
    emmc_last_cmd = cmd_idx;
    emmc_last_arg = arg;

    if (!emmc_wait_inhibit(STATUS_CMD_INHIBIT | STATUS_DATA_INHIBIT, 1000000)) {
        uint32_t status = emmc_reg_read(EMMC_STATUS);

        /* Week 36 fix: Reset DATA line if DAT_INHIBIT is stuck */
        if (status & STATUS_DATA_INHIBIT) {
            debug_puts("[EMMC] WARN: DAT_INHIBIT stuck, resetting DATA line\n");
            uint32_t ctrl1 = emmc_reg_read(EMMC_CONTROL1) | C1_SRST_DATA;
            emmc_reg_write(EMMC_CONTROL1, ctrl1);
            for (uint32_t i = 0; i < 1000000; i++) {
                if ((emmc_reg_read(EMMC_CONTROL1) & C1_SRST_DATA) == 0) {
                    break;
                }
            }
            /* Clear any pending interrupts after DATA reset */
            emmc_reg_write(EMMC_INTERRUPT, 0xFFFFFFFF);
            /* Re-enable interrupt status bits (DATA reset clears IRPT_EN/IRPT_MASK) */
            emmc_reg_write(EMMC_IRPT_EN, INT_CMD_DONE | INT_DATA_DONE | INT_READ_RDY | INT_WRITE_RDY | INT_ERROR_MASK);
            emmc_reg_write(EMMC_IRPT_MASK, INT_CMD_DONE | INT_DATA_DONE | INT_READ_RDY | INT_WRITE_RDY | INT_ERROR_MASK);
        }

        /* Reset CMD line if CMD_INHIBIT is stuck */
        if (status & STATUS_CMD_INHIBIT) {
            debug_puts("[EMMC] WARN: CMD_INHIBIT stuck, resetting CMD line\n");
            uint32_t ctrl1 = emmc_reg_read(EMMC_CONTROL1) | C1_SRST_CMD;
            emmc_reg_write(EMMC_CONTROL1, ctrl1);
            for (uint32_t i = 0; i < 1000000; i++) {
                if ((emmc_reg_read(EMMC_CONTROL1) & C1_SRST_CMD) == 0) {
                    break;
                }
            }
            /* Re-enable interrupt status bits (CMD reset clears IRPT_EN/IRPT_MASK) */
            emmc_reg_write(EMMC_IRPT_EN, INT_CMD_DONE | INT_DATA_DONE | INT_READ_RDY | INT_WRITE_RDY | INT_ERROR_MASK);
            emmc_reg_write(EMMC_IRPT_MASK, INT_CMD_DONE | INT_DATA_DONE | INT_READ_RDY | INT_WRITE_RDY | INT_ERROR_MASK);
        }

        if (!emmc_wait_inhibit(STATUS_CMD_INHIBIT | STATUS_DATA_INHIBIT, 1000000)) {
            debug_puts("[EMMC] ERROR: inhibit wait timeout, waited=");
            debug_hex(emmc_last_wait_inhibit);
            debug_puts("\n");
            emmc_dump_state("CMD inhibit timeout");
            return false;
        }
    }

    if ((emmc_reg_read(EMMC_STATUS) & (STATUS_CMD_INHIBIT | STATUS_DATA_INHIBIT)) != 0) {
        debug_puts("[EMMC] ERROR: inhibit still set after wait\n");
        emmc_dump_state("CMD inhibit timeout");
        return false;
    }

#if EMMC_DEBUG
    debug_puts("[EMMC] inhibit wait=");
    debug_hex(emmc_last_wait_inhibit);
    debug_puts("\n");
#endif

#if EMMC_DEBUG
    if (cmd_idx == CMD24_WRITE_SINGLE) {
        uint32_t irpt_pre = emmc_reg_read(EMMC_INTERRUPT);
        debug_puts("[EMMC][DBG] CMD24 irpt pre-clear=");
        debug_hex(irpt_pre);
        debug_puts("\n");
        emmc_dbg_decode_irpt(irpt_pre);
    }
#endif

    emmc_reg_write(EMMC_INTERRUPT, 0xFFFFFFFF);

#if EMMC_DEBUG
    if (cmd_idx == CMD24_WRITE_SINGLE) {
        uint32_t irpt_post = emmc_reg_read(EMMC_INTERRUPT);
        debug_puts("[EMMC][DBG] CMD24 irpt post-clear=");
        debug_hex(irpt_post);
        debug_puts("\n");
        emmc_dbg_decode_irpt(irpt_post);
        emmc_dbg_dump_regs("before CMD24");
    }
#endif
    emmc_reg_write(EMMC_ARG1, arg);
    emmc_reg_write(EMMC_CMDTM, (cmd_idx << CMDTM_CMD_INDEX_SHIFT) | cmdtm);

    /* Use selective error checking for CMD_DONE to ignore DATA_TIMEOUT
     * (BCM2711 SDHCI quirk: DATA_TIMEOUT can fire during command phase for writes) */
    if (!emmc_wait_cmd_done(1000000)) {
        emmc_dump_state("CMD timeout");
        return false;
    }

#if EMMC_DEBUG
    if (cmd_idx == CMD24_WRITE_SINGLE) {
        uint32_t irpt_after = emmc_reg_read(EMMC_INTERRUPT);
        debug_puts("[EMMC][DBG] CMD24 irpt after CMD_DONE=");
        debug_hex(irpt_after);
        debug_puts("\n");
        emmc_dbg_decode_irpt(irpt_after);
        emmc_dbg_dump_regs("after CMD_DONE CMD24");
    }
#endif

    if (resp) {
        resp[0] = emmc_reg_read(EMMC_RESP0);
        resp[1] = emmc_reg_read(EMMC_RESP1);
        resp[2] = emmc_reg_read(EMMC_RESP2);
        resp[3] = emmc_reg_read(EMMC_RESP3);
    }
    return true;
}

bool emmc_init(void)
{
    if (!emmc_mmio_base) {
        debug_puts("[EMMC] ERROR: MMIO not mapped\n");
        return false;
    }

    debug_puts("[EMMC] Reset controller...\n");
    if (!emmc_reset()) {
        debug_puts("[EMMC] ERROR: reset timeout\n");
        return false;
    }

    /* Mask interrupts, then enable core interrupt sources (Week 36: added INT_WRITE_RDY for CMD24) */
    emmc_reg_write(EMMC_IRPT_MASK, 0x0);
    emmc_reg_write(EMMC_IRPT_EN, INT_CMD_DONE | INT_DATA_DONE | INT_READ_RDY | INT_WRITE_RDY | INT_ERROR_MASK);
    emmc_reg_write(EMMC_IRPT_MASK, INT_CMD_DONE | INT_DATA_DONE | INT_READ_RDY | INT_WRITE_RDY | INT_ERROR_MASK);

    emmc_set_power();

    if (!emmc_set_clock(400000)) {
        debug_puts("[EMMC] ERROR: clock init failed\n");
        return false;
    }

    emmc_dump_identity();

    /* CMD0: reset card */
#if EMMC_DEBUG
    emmc_dump_cmd_prep("PRE CMD0", CMDTM_RSPNS_TYPE_NONE, 0);
#endif
    if (!emmc_send_cmd(CMD0_GO_IDLE, 0, CMDTM_RSPNS_TYPE_NONE, NULL)) {
        debug_puts("[EMMC] ERROR: CMD0 failed\n");
        emmc_dump_state("CMD0");
        return false;
    }

    /* CMD8: check voltage */
    uint32_t resp[4] = {0};
    if (!emmc_send_cmd(CMD8_SEND_IF_COND, 0x1AA,
                       CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN, resp)) {
        debug_puts("[EMMC] ERROR: CMD8 failed\n");
        emmc_dump_state("CMD8");
        return false;
    }

    /* ACMD41: initialize card */
    uint32_t ocr = 0;
    bool ready = false;
    for (int i = 0; i < 1000; i++) {
        if (!emmc_send_cmd(CMD55_APP_CMD, 0, CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN, resp)) {
            debug_puts("[EMMC] ERROR: CMD55 failed\n");
            emmc_dump_state("CMD55");
            return false;
        }
        if (!emmc_send_cmd(ACMD41_SD_OP_COND, 0x40300000, CMDTM_RSPNS_TYPE_48, resp)) {
            debug_puts("[EMMC] ERROR: ACMD41 failed\n");
            emmc_dump_state("ACMD41");
            return false;
        }
        ocr = resp[0];
        if (ocr & 0x80000000u) {
            ready = true;
            break;
        }
    }
    if (!ready) {
        debug_puts("[EMMC] ERROR: ACMD41 timeout\n");
        emmc_dump_state("ACMD41 timeout");
        return false;
    }
    emmc_high_capacity = (ocr & 0x40000000u) != 0;

    /* CMD2: CID */
    if (!emmc_send_cmd(CMD2_ALL_SEND_CID, 0, CMDTM_RSPNS_TYPE_136, resp)) {
        debug_puts("[EMMC] ERROR: CMD2 failed\n");
        emmc_dump_state("CMD2");
        return false;
    }
    /* Store CID for later retrieval */
    emmc_cid[0] = resp[0];
    emmc_cid[1] = resp[1];
    emmc_cid[2] = resp[2];
    emmc_cid[3] = resp[3];

    /* CMD3: RCA */
    if (!emmc_send_cmd(CMD3_SEND_REL_ADDR, 0, CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN, resp)) {
        debug_puts("[EMMC] ERROR: CMD3 failed\n");
        emmc_dump_state("CMD3");
        return false;
    }
    emmc_rca = resp[0] & 0xFFFF0000u;

    /* CMD9: Read CSD register to get actual card capacity.
     * Must be sent while card is in standby state (before CMD7 select). */
    if (!emmc_send_cmd(CMD9_SEND_CSD, emmc_rca, CMDTM_RSPNS_TYPE_136, resp)) {
        debug_puts("[EMMC] WARNING: CMD9 (CSD) failed, using capacity estimate\n");
        emmc_capacity = emmc_high_capacity ? (32ULL * 1024 * 1024 * 1024) : (2ULL * 1024 * 1024 * 1024);
    } else {
        /* Parse CSD to extract capacity.
         * SDHCI 136-bit response register mapping (after CRC strip + shift):
         *   resp[0] = RESP0 (offset 0x10) = CSD bits [39:8]
         *   resp[1] = RESP1 (offset 0x14) = CSD bits [71:40]
         *   resp[2] = RESP2 (offset 0x18) = CSD bits [103:72]
         *   resp[3] = RESP3 (offset 0x1C) = CSD bits [127:104] in bits [23:0]
         *
         * Dump raw CSD for diagnostics on first boot. */
        debug_puts("[EMMC] CSD raw: ");
        debug_hex(resp[3]); debug_puts(" ");
        debug_hex(resp[2]); debug_puts(" ");
        debug_hex(resp[1]); debug_puts(" ");
        debug_hex(resp[0]); debug_puts("\n");

        /* CSD_STRUCTURE = CSD[127:126] → resp[3] bits [23:22] */
        uint32_t csd_ver = (resp[3] >> 22) & 0x3;

        if (csd_ver == 1 && emmc_high_capacity) {
            /* CSD Version 2.0 (SDHC/SDXC):
             * C_SIZE = CSD[69:48] = 22 bits
             *   CSD[69:40] in resp[1], so C_SIZE = resp[1] bits [29:8]
             * Capacity = (C_SIZE + 1) * 512 KB */
            uint32_t c_size = (resp[1] >> 8) & 0x3FFFFFu;
            emmc_capacity = (uint64_t)(c_size + 1) * 512 * 1024;
            debug_puts("[EMMC] CSD v2: C_SIZE=");
            debug_hex(c_size);
            debug_puts(" capacity_MB=");
            debug_hex((uint32_t)(emmc_capacity / (1024 * 1024)));
            debug_puts("\n");
        } else if (csd_ver == 0) {
            /* CSD Version 1.0 (SDSC):
             * READ_BL_LEN = CSD[83:80] → resp[2] bits [11:8]
             * C_SIZE = CSD[73:62] → resp[1] bits [1:0] + resp[0] bits [31:22]
             * C_SIZE_MULT = CSD[49:47] → resp[1] bits [9:7]
             * Capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN */
            uint32_t read_bl_len = (resp[2] >> 8) & 0xF;
            uint32_t c_size = ((resp[1] & 0x3) << 10) | (resp[0] >> 22);
            uint32_t c_size_mult = (resp[1] >> 7) & 0x7;
            emmc_capacity = (uint64_t)(c_size + 1) * (1u << (c_size_mult + 2)) * (1u << read_bl_len);
            debug_puts("[EMMC] CSD v1: capacity_MB=");
            debug_hex((uint32_t)(emmc_capacity / (1024 * 1024)));
            debug_puts("\n");
        } else {
            debug_puts("[EMMC] WARNING: Unknown CSD version ");
            debug_hex(csd_ver);
            debug_puts(", using estimate\n");
            emmc_capacity = emmc_high_capacity ? (32ULL * 1024 * 1024 * 1024) : (2ULL * 1024 * 1024 * 1024);
        }
    }

    /* CMD7: select card */
    if (!emmc_send_cmd(CMD7_SELECT_CARD, emmc_rca, CMDTM_RSPNS_TYPE_48B | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN, resp)) {
        debug_puts("[EMMC] ERROR: CMD7 failed\n");
        emmc_dump_state("CMD7");
        return false;
    }

    /* Set block size (CMD16). For SDHC, 512-byte is fixed but safe. */
    if (!emmc_send_cmd(CMD16_SET_BLOCKLEN, 512, CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN, resp)) {
        debug_puts("[EMMC] ERROR: CMD16 failed\n");
        emmc_dump_state("CMD16");
        return false;
    }

    /* Enable 4-bit bus width (ACMD6) */
    if (!emmc_send_cmd(CMD55_APP_CMD, emmc_rca, CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN, resp)) {
        debug_puts("[EMMC] ERROR: CMD55 for ACMD6 failed\n");
    } else if (!emmc_send_cmd(ACMD6_SET_BUS_WIDTH, 2, CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN, resp)) {
        debug_puts("[EMMC] ERROR: ACMD6 (4-bit) failed\n");
    } else {
        /* Set 4-bit mode in host controller */
        uint32_t ctrl0 = emmc_reg_read(EMMC_CONTROL0);
        ctrl0 |= CTRL0_4BIT_MODE;
        emmc_reg_write(EMMC_CONTROL0, ctrl0);
        debug_puts("[EMMC] 4-bit bus enabled\n");
    }

    /* Switch to High Speed mode (50 MHz) */
    if (!emmc_set_clock(50000000)) {
        debug_puts("[EMMC] WARNING: 50MHz clock failed, using 25MHz\n");
        if (!emmc_set_clock(25000000)) {
            debug_puts("[EMMC] ERROR: data clock failed\n");
            return false;
        }
    } else {
        /* Enable High Speed in host controller */
        uint32_t ctrl0 = emmc_reg_read(EMMC_CONTROL0);
        ctrl0 |= CTRL0_HISPD_EN;
        emmc_reg_write(EMMC_CONTROL0, ctrl0);
        debug_puts("[EMMC] High Speed 50MHz enabled\n");
    }

    debug_puts("[EMMC] Card init OK (");
    debug_puts(emmc_high_capacity ? "SDHC/SDXC" : "SDSC");
    debug_puts(")\n");
    return true;
}

void emmc_get_cid(uint32_t cid_out[4])
{
    if (cid_out) {
        cid_out[0] = emmc_cid[0];
        cid_out[1] = emmc_cid[1];
        cid_out[2] = emmc_cid[2];
        cid_out[3] = emmc_cid[3];
    }
}

uint64_t emmc_get_capacity_bytes(void)
{
    return emmc_capacity;
}

/* Internal single-attempt read (no retry) */
static bool emmc_read_block_once(uint32_t lba, uint8_t *out_512)
{
    uint32_t addr = emmc_high_capacity ? lba : (lba * 512);
    uint32_t resp[4] = {0};

    emmc_reg_write(EMMC_BLKSIZECNT, (1u << 16) | 512);

    uint32_t cmdtm = CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN |
                     CMDTM_ISDATA | CMDTM_DAT_DIR_READ | CMDTM_BLKCNT_EN;

    if (!emmc_send_cmd(CMD17_READ_SINGLE, addr, cmdtm, resp)) {
        debug_puts("[EMMC] read_once: CMD17 FAILED\n");
        return false;
    }

#if EMMC_DEBUG
    /* Diagnostic: dump state between CMD_DONE and READ_RDY wait */
    {
        uint32_t irpt = emmc_reg_read(EMMC_INTERRUPT);
        debug_puts("[EMMC] CMD17 OK irpt=");
        debug_hex(irpt);
        debug_puts(" en=");
        debug_hex(emmc_reg_read(EMMC_IRPT_EN));
        debug_puts(" mask=");
        debug_hex(emmc_reg_read(EMMC_IRPT_MASK));
        debug_puts(" status=");
        debug_hex(emmc_reg_read(EMMC_STATUS));
        debug_puts(" resp0=");
        debug_hex(resp[0]);
        debug_puts("\n");
    }
#endif

    if (!emmc_wait_interrupt(INT_READ_RDY, 1000000)) {
        debug_puts("[EMMC] read_once: READ_RDY timeout\n");
        emmc_dump_state("READ_RDY");
        return false;
    }

    for (int i = 0; i < 128; i++) {
        uint32_t word = emmc_reg_read(EMMC_DATA);
        out_512[i * 4 + 0] = (uint8_t)(word & 0xFF);
        out_512[i * 4 + 1] = (uint8_t)((word >> 8) & 0xFF);
        out_512[i * 4 + 2] = (uint8_t)((word >> 16) & 0xFF);
        out_512[i * 4 + 3] = (uint8_t)((word >> 24) & 0xFF);
    }

    if (!emmc_wait_interrupt(INT_DATA_DONE, 1000000)) {
        return false;
    }

    return true;
}

bool emmc_read_block(uint32_t lba, uint8_t *out_512)
{
    if (!emmc_mmio_base || !out_512) {
        return false;
    }

    for (int retry = 0; retry < EMMC_MAX_RETRIES; retry++) {
        if (emmc_read_block_once(lba, out_512)) {
            return true;
        }
        if (retry < EMMC_MAX_RETRIES - 1) {
            debug_puts("[EMMC] Read retry...\n");
        }
    }

    debug_puts("[EMMC] ERROR: CMD17 failed after retries\n");
    emmc_dump_state("CMD17 retries exhausted");
    return false;
}

/*
 * Read multiple 512-byte blocks starting at lba using CMD18 + AUTO_CMD12.
 * Returns true on success, false on any failure.
 */
bool emmc_read_blocks(uint32_t lba, uint32_t count, uint8_t *out_buf)
{
    if (!emmc_mmio_base || !out_buf || count == 0) {
        return false;
    }

    /* For single block, use CMD17 (simpler path) */
    if (count == 1) {
        return emmc_read_block(lba, out_buf);
    }

    uint32_t addr = emmc_high_capacity ? lba : (lba * 512);
    uint32_t resp[4] = {0};

    /* Set block count and size */
    emmc_reg_write(EMMC_BLKSIZECNT, (count << 16) | 512);

    /* CMD18 with AUTO_CMD12 for automatic stop */
    uint32_t cmdtm = CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN |
                     CMDTM_ISDATA | CMDTM_DAT_DIR_READ | CMDTM_BLKCNT_EN |
                     CMDTM_MULTI_BLOCK | CMDTM_AUTO_CMD12;

    if (!emmc_send_cmd(CMD18_READ_MULTIPLE, addr, cmdtm, resp)) {
        debug_puts("[EMMC] ERROR: CMD18 failed\n");
        emmc_dump_state("CMD18");
        return false;
    }

    /* Read each block via PIO */
    for (uint32_t blk = 0; blk < count; blk++) {
        if (!emmc_wait_interrupt(INT_READ_RDY, 1000000)) {
            debug_puts("[EMMC] ERROR: READ_RDY timeout (multi)\n");
            emmc_dump_state("READ_RDY multi");
            return false;
        }

        uint8_t *dst = out_buf + (blk * 512);
        for (int i = 0; i < 128; i++) {
            uint32_t word = emmc_reg_read(EMMC_DATA);
            dst[i * 4 + 0] = (uint8_t)(word & 0xFF);
            dst[i * 4 + 1] = (uint8_t)((word >> 8) & 0xFF);
            dst[i * 4 + 2] = (uint8_t)((word >> 16) & 0xFF);
            dst[i * 4 + 3] = (uint8_t)((word >> 24) & 0xFF);
        }
    }

    /* Wait for transfer complete */
    if (!emmc_wait_interrupt(INT_DATA_DONE, 1000000)) {
        debug_puts("[EMMC] ERROR: DATA_DONE timeout (multi)\n");
        emmc_dump_state("DATA_DONE multi");
        return false;
    }

    return true;
}

/* Internal single-attempt write (no retry) */
static bool emmc_write_block_once(uint32_t lba, const uint8_t *data_512)
{
    uint32_t addr = emmc_high_capacity ? lba : (lba * 512);
    uint32_t resp[4] = {0};

    /* Set block count=1, size=512 */
    emmc_reg_write(EMMC_BLKSIZECNT, (1u << 16) | 512);

    /* CMD24: no DAT_DIR_READ bit = write direction
     * Week 36 fix: Disable IXCHK_EN to avoid Command Index Error at 50MHz.
     * Some SD cards have response timing issues at high speed that cause
     * the index check to fail even with correct responses.
     * CRC check is sufficient for data integrity. */
    uint32_t cmdtm = CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN |
                     CMDTM_ISDATA | CMDTM_BLKCNT_EN;

    if (!emmc_send_cmd(CMD24_WRITE_SINGLE, addr, cmdtm, resp)) {
        debug_puts("[EMMC] ERROR: CMD24 send failed\n");
#if EMMC_DEBUG
        {
            uint32_t irpt_fail = emmc_reg_read(EMMC_INTERRUPT);
            debug_puts("[EMMC][DBG] CMD24 irpt=");
            debug_hex(irpt_fail);
            debug_puts("\n");
            emmc_dbg_decode_irpt(irpt_fail);
            emmc_dbg_dump_regs("CMD24 FAIL");
        }
#endif
        emmc_dump_state("CMD24");
        return false;
    }

    /* Use write-specific wait that ignores BCM2711's spurious DATA_TIMEOUT.
     * The hardware continuously re-asserts DATA_TIMEOUT during write command
     * data phase - clearing it is futile as it fires again immediately. */
    if (!emmc_wait_interrupt_write(INT_WRITE_RDY, 1000000)) {
        debug_puts("[EMMC] WRITE_RDY timeout\n");
        emmc_dump_state("CMD24 WRITE_RDY");
        return false;
    }

    /* Write 128 words (512 bytes) to DATA register */
    for (int i = 0; i < 128; i++) {
        uint32_t word = (uint32_t)data_512[i * 4 + 0] |
                        ((uint32_t)data_512[i * 4 + 1] << 8) |
                        ((uint32_t)data_512[i * 4 + 2] << 16) |
                        ((uint32_t)data_512[i * 4 + 3] << 24);
        emmc_reg_write(EMMC_DATA, word);
    }

    /* Wait for transfer complete (also ignore spurious DATA_TIMEOUT) */
    if (!emmc_wait_interrupt_write(INT_DATA_DONE, 1000000)) {
        debug_puts("[EMMC] DATA_DONE timeout after write\n");
        emmc_dump_state("CMD24 DATA_DONE");
        return false;
    }

    /* Post-write cleanup: clear stale interrupts from write phase, then
     * wait for card to finish programming (DAT0 held low during flash write).
     * No DATA+CMD reset - it clears IRPT_EN/IRPT_MASK and causes more
     * problems than it solves. DATA_DONE already means busy deassertion
     * per SDHCI spec, but BCM2711 may not wait for busy on writes. */
    emmc_reg_write(EMMC_INTERRUPT, 0xFFFFFFFF);

    /* Long wait for card programming: SD cards can take up to 250ms per block.
     * 50M iterations at ~5ns/iter = ~250ms worst case. */
    if (!emmc_wait_inhibit(STATUS_DATA_INHIBIT, 50000000)) {
        debug_puts("[EMMC] WARN: DAT_INHIBIT still set after long wait\n");
    }

    /* Clear any final interrupts */
    emmc_reg_write(EMMC_INTERRUPT, 0xFFFFFFFF);
#if EMMC_DEBUG
    debug_puts("[EMMC] post-write irpt_en=");
    debug_hex(emmc_reg_read(EMMC_IRPT_EN));
    debug_puts(" irpt_mask=");
    debug_hex(emmc_reg_read(EMMC_IRPT_MASK));
    debug_puts(" status=");
    debug_hex(emmc_reg_read(EMMC_STATUS));
    debug_puts("\n");
#endif

    return true;
}

/*
 * Write a single 512-byte block at lba using CMD24 with retry.
 * Returns true on success, false on failure.
 *
 * WARNING: Writing to wrong LBAs can corrupt filesystem!
 * Only use with known-safe LBAs (e.g., beyond partition end).
 */
bool emmc_write_block(uint32_t lba, const uint8_t *data_512)
{
    if (!emmc_mmio_base || !data_512) {
        return false;
    }

    for (int retry = 0; retry < EMMC_MAX_RETRIES; retry++) {
        if (emmc_write_block_once(lba, data_512)) {
            return true;
        }
        if (retry < EMMC_MAX_RETRIES - 1) {
            debug_puts("[EMMC] Write retry...\n");
        }
    }

    debug_puts("[EMMC] ERROR: CMD24 failed after retries\n");
    emmc_dump_state("CMD24 retries exhausted");
    return false;
}

/*
 * Write multiple 512-byte blocks starting at lba using CMD25 + AUTO_CMD12.
 * Returns true on success, false on failure.
 *
 * WARNING: Writing to wrong LBAs can corrupt filesystem!
 * Only use with known-safe LBAs (e.g., beyond partition end).
 */
bool emmc_write_blocks(uint32_t lba, uint32_t count, const uint8_t *data)
{
    if (!emmc_mmio_base || !data || count == 0) {
        return false;
    }

    /* For single block, use CMD24 (simpler path) */
    if (count == 1) {
        return emmc_write_block(lba, data);
    }

    uint32_t addr = emmc_high_capacity ? lba : (lba * 512);
    uint32_t resp[4] = {0};

    /* Set block count and size */
    emmc_reg_write(EMMC_BLKSIZECNT, (count << 16) | 512);

    /* CMD25 with AUTO_CMD12 for automatic stop, no DAT_DIR_READ = write
     * Week 36 fix: Disable IXCHK_EN (same as CMD24 fix for Command Index Error) */
    uint32_t cmdtm = CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN |
                     CMDTM_ISDATA | CMDTM_BLKCNT_EN |
                     CMDTM_MULTI_BLOCK | CMDTM_AUTO_CMD12;

    if (!emmc_send_cmd(CMD25_WRITE_MULTIPLE, addr, cmdtm, resp)) {
        debug_puts("[EMMC] ERROR: CMD25 failed\n");
        emmc_dump_state("CMD25");
        return false;
    }

    /* Write each block via PIO (use write-specific wait that ignores
     * BCM2711's spurious DATA_TIMEOUT during write data phase) */
    for (uint32_t blk = 0; blk < count; blk++) {
        if (!emmc_wait_interrupt_write(INT_WRITE_RDY, 1000000)) {
            debug_puts("[EMMC] ERROR: WRITE_RDY timeout (multi)\n");
            emmc_dump_state("WRITE_RDY multi");
            return false;
        }

        const uint8_t *src = data + (blk * 512);
        for (int i = 0; i < 128; i++) {
            uint32_t word = (uint32_t)src[i * 4 + 0] |
                            ((uint32_t)src[i * 4 + 1] << 8) |
                            ((uint32_t)src[i * 4 + 2] << 16) |
                            ((uint32_t)src[i * 4 + 3] << 24);
            emmc_reg_write(EMMC_DATA, word);
        }
    }

    /* Wait for transfer complete (ignore spurious DATA_TIMEOUT) */
    if (!emmc_wait_interrupt_write(INT_DATA_DONE, 1000000)) {
        debug_puts("[EMMC] ERROR: DATA_DONE timeout (multi write)\n");
        emmc_dump_state("DATA_DONE multi write");
        return false;
    }

    /* Post-write cleanup: same as single write - no DATA+CMD reset */
    emmc_reg_write(EMMC_INTERRUPT, 0xFFFFFFFF);
    if (!emmc_wait_inhibit(STATUS_DATA_INHIBIT, 50000000)) {
        debug_puts("[EMMC] WARN: DAT_INHIBIT still set after long wait (multi)\n");
    }
    emmc_reg_write(EMMC_INTERRUPT, 0xFFFFFFFF);

    return true;
}

/* ================================================================
 * ADMA2 DMA Implementation
 * ================================================================ */

#if EMMC_USE_ADMA2

/*
 * Build ADMA2 descriptor table for a contiguous buffer.
 * Each descriptor handles up to 65536 bytes.
 * Returns number of descriptors used, or 0 on error.
 */
static int emmc_adma2_build_desc(uintptr_t paddr, size_t bytes)
{
    if (bytes == 0 || bytes > (ADMA2_MAX_DESCRIPTORS * 65536UL)) {
        debug_puts("[EMMC] ADMA2: invalid transfer size\n");
        return 0;
    }

    int desc_idx = 0;
    size_t remaining = bytes;
    uintptr_t addr = paddr;

    while (remaining > 0) {
        if (desc_idx >= ADMA2_MAX_DESCRIPTORS) {
            debug_puts("[EMMC] ADMA2: too many descriptors\n");
            return 0;
        }

        size_t chunk = (remaining > 65536) ? 65536 : remaining;

        adma2_desc_table[desc_idx].address = (uint32_t)addr;
        /* Length: 0 means 65536, otherwise actual value */
        adma2_desc_table[desc_idx].length = (chunk == 65536) ? 0 : (uint16_t)chunk;
        adma2_desc_table[desc_idx].attr = ADMA2_ATTR_VALID | ADMA2_ACT_TRAN;

        remaining -= chunk;
        addr += chunk;
        desc_idx++;
    }

    /* Mark last descriptor with END and INT flags */
    if (desc_idx > 0) {
        adma2_desc_table[desc_idx - 1].attr |= ADMA2_ATTR_END | ADMA2_ATTR_INT;
    }

    /* No cache maintenance needed: DMA buffers mapped uncacheable */

    return desc_idx;
}

/*
 * Read multiple blocks using ADMA2 DMA.
 * Buffer must be within adma2_buffer range.
 * Returns true on success, false on error (falls back to PIO).
 *
 * DMA barrier sequence:
 * 1. Invalidate buffer (discard stale cache)
 * 2. dsb_sy (ensure invalidate completes)
 * 3. Start DMA transfer
 * 4. Wait for completion
 * 5. dsb_sy (ensure DMA writes visible)
 * 6. Invalidate buffer again (paranoia for speculative fills)
 */
static bool emmc_read_blocks_adma2_internal(uint32_t lba, uint32_t count, uint8_t *out_buf)
{
    size_t bytes = (size_t)count * 512;

    /* Calculate physical address offset within DMA buffer */
    uintptr_t buf_paddr = adma2_buffer_paddr + (uintptr_t)(out_buf - adma2_buffer);

    /* Build descriptor table (includes dcache_clean for descriptors) */
    int num_desc = emmc_adma2_build_desc(buf_paddr, bytes);
    if (num_desc == 0) {
        debug_puts("[EMMC] ADMA2: descriptor build failed\n");
        return false;
    }

    /* DMA buffers mapped uncacheable - no cache maintenance needed */
    dsb_sy();  /* Ensure prior writes complete before DMA starts */

    /* Program ADMA System Address register with descriptor table paddr */
    emmc_reg_write(EMMC_ADMA_SYS_ADDR, (uint32_t)adma2_desc_paddr);

    /* Prepare command */
    uint32_t addr = emmc_high_capacity ? lba : (lba * 512);
    uint32_t resp[4] = {0};

    /* Set block count and size */
    emmc_reg_write(EMMC_BLKSIZECNT, (count << 16) | 512);

    /* CMD18 with DMA enable, AUTO_CMD12 for stop */
    uint32_t cmdtm = CMDTM_RSPNS_TYPE_48 | CMDTM_CRCCHK_EN | CMDTM_IXCHK_EN |
                     CMDTM_ISDATA | CMDTM_DAT_DIR_READ | CMDTM_BLKCNT_EN |
                     CMDTM_MULTI_BLOCK | CMDTM_AUTO_CMD12 | CMDTM_DMA_EN;

    /* Step 3: Start DMA transfer */
    if (!emmc_send_cmd(CMD18_READ_MULTIPLE, addr, cmdtm, resp)) {
        debug_puts("[EMMC] ADMA2: CMD18 failed\n");
        emmc_dump_state("ADMA2 CMD18");
        return false;
    }

    /* Step 4: Wait for DMA complete (DATA_DONE interrupt) */
    if (!emmc_wait_interrupt(INT_DATA_DONE, 2000000)) {
        /* Check for ADMA error */
        uint32_t adma_err = emmc_reg_read(EMMC_ADMA_ERR);
        debug_puts("[EMMC] ADMA2: timeout, ADMA_ERR=");
        debug_hex(adma_err);
        debug_puts(" INT=");
        debug_hex(emmc_reg_read(EMMC_INTERRUPT));
        debug_puts("\n");
        emmc_dump_state("ADMA2 timeout");
        return false;
    }

    /* Check for ADMA error after completion */
    uint32_t irpt = emmc_reg_read(EMMC_INTERRUPT);
    if (irpt & INT_ADMA_ERR) {
        uint32_t adma_err = emmc_reg_read(EMMC_ADMA_ERR);
        debug_puts("[EMMC] ADMA2: error after transfer, ADMA_ERR=");
        debug_hex(adma_err);
        debug_puts("\n");
        emmc_reg_write(EMMC_INTERRUPT, INT_ADMA_ERR);
        return false;
    }

    /* DMA buffers mapped uncacheable - CPU sees DMA'd data directly */
    dsb_sy();  /* Ensure DMA writes are visible */

    return true;
}

/*
 * Initialize ADMA2 DMA mode.
 * Must be called after emmc_init() and dma_alloc_init() succeed.
 * Returns true on success.
 *
 * Uses dma_alloc() to get buffers with KNOWN physical addresses.
 * This is the correct way to do DMA on seL4 - tracking paddr at allocation time.
 */
bool emmc_adma2_init(void)
{
    if (adma2_initialized) {
        return true;
    }

    if (!emmc_mmio_base) {
        debug_puts("[EMMC] ADMA2: EMMC not initialized\n");
        return false;
    }

    /* Check DMA allocator is ready */
    if (!dma_alloc_is_ready()) {
        debug_puts("[EMMC] ADMA2: DMA allocator not ready!\n");
        debug_puts("[EMMC] ADMA2: Call dma_alloc_init() first.\n");
        return false;
    }

    debug_puts("[EMMC] ADMA2: Allocating DMA buffers via dma_alloc()...\n");

    /* Allocate descriptor table (256 bytes for 32 descriptors, 8-byte aligned) */
    size_t desc_size = ADMA2_MAX_DESCRIPTORS * sizeof(adma2_desc_t);
    adma2_desc_table = (adma2_desc_t *)dma_alloc(desc_size, &adma2_desc_paddr);
    if (!adma2_desc_table) {
        debug_puts("[EMMC] ADMA2: Failed to allocate descriptor table\n");
        return false;
    }
    debug_puts("[EMMC] ADMA2: desc_table vaddr=");
    debug_hex((uint32_t)(uintptr_t)adma2_desc_table);
    debug_puts(" paddr=");
    debug_hex((uint32_t)adma2_desc_paddr);
    debug_puts("\n");

    /* Allocate DMA buffer (128KB for throughput test) */
    adma2_buffer = (uint8_t *)dma_alloc(EMMC_ADMA2_BUF_SIZE, &adma2_buffer_paddr);
    if (!adma2_buffer) {
        debug_puts("[EMMC] ADMA2: Failed to allocate DMA buffer\n");
        return false;
    }
    debug_puts("[EMMC] ADMA2: buffer vaddr=");
    debug_hex((uint32_t)(uintptr_t)adma2_buffer);
    debug_puts(" paddr=");
    debug_hex((uint32_t)adma2_buffer_paddr);
    debug_puts("\n");

    /* Verify paddrs are valid for DMA (non-zero, < 4GB, 4-byte aligned) */
    if (adma2_desc_paddr == 0 || adma2_buffer_paddr == 0) {
        debug_puts("[EMMC] ADMA2: ERROR - paddr is zero!\n");
        return false;
    }
    if (adma2_desc_paddr >= ADMA2_MAX_PADDR || adma2_buffer_paddr >= ADMA2_MAX_PADDR) {
        debug_puts("[EMMC] ADMA2: ERROR - paddr >= 1GB, may be out of DMA range\n");
        return false;
    }
    if ((adma2_desc_paddr & 0x3) != 0 || (adma2_buffer_paddr & 0x3) != 0) {
        debug_puts("[EMMC] ADMA2: ERROR - paddr not 4-byte aligned\n");
        return false;
    }
    debug_puts("[EMMC] ADMA2: paddr validation PASSED (real addresses from untyped)\n");

    /* Read and log current CONTROL0 before modification */
    uint32_t ctrl0_before = emmc_reg_read(EMMC_CONTROL0);
    debug_puts("[EMMC] ADMA2: CONTROL0 before=");
    debug_hex(ctrl0_before);
    debug_puts("\n");

    /* Enable ADMA2 mode in CONTROL0 (Host Control 1, bits 4:3 = 2) */
    uint32_t ctrl0 = ctrl0_before;
    ctrl0 &= ~CTRL0_DMA_SEL_MASK;
    ctrl0 |= CTRL0_DMA_SEL_ADMA2;
    emmc_reg_write(EMMC_CONTROL0, ctrl0);

    /* Verify the write took effect */
    uint32_t ctrl0_after = emmc_reg_read(EMMC_CONTROL0);
    debug_puts("[EMMC] ADMA2: CONTROL0 after=");
    debug_hex(ctrl0_after);
    debug_puts("\n");

    if ((ctrl0_after & CTRL0_DMA_SEL_MASK) != CTRL0_DMA_SEL_ADMA2) {
        debug_puts("[EMMC] ADMA2: ERROR - DMA select bits not set correctly!\n");
        debug_puts("[EMMC]   Expected bits 4:3 = 2, got ");
        debug_hex((ctrl0_after & CTRL0_DMA_SEL_MASK) >> 3);
        debug_puts("\n");
        return false;
    }

    /* Enable ADMA error interrupt */
    uint32_t irpt_en = emmc_reg_read(EMMC_IRPT_EN);
    irpt_en |= INT_ADMA_ERR;
    emmc_reg_write(EMMC_IRPT_EN, irpt_en);
    emmc_reg_write(EMMC_IRPT_MASK, irpt_en);

    debug_puts("[EMMC] ADMA2 initialized successfully\n");

    adma2_initialized = true;
    return true;
}

/*
 * Check if ADMA2 is initialized and ready.
 */
bool emmc_adma2_is_enabled(void)
{
    return adma2_initialized;
}

/*
 * Get pointer to the ADMA2 DMA buffer.
 * Use this buffer for throughput tests to ensure DMA-capable memory.
 */
uint8_t *emmc_adma2_get_buffer(void)
{
    return adma2_buffer;
}

/*
 * ADMA2-aware multi-block read.
 * Uses ADMA2 if buffer is within DMA range, otherwise falls back to PIO.
 */
bool emmc_read_blocks_adma2(uint32_t lba, uint32_t count, uint8_t *out_buf)
{
    if (!adma2_initialized) {
        debug_puts("[EMMC] ADMA2: not initialized, using PIO\n");
        return emmc_read_blocks(lba, count, out_buf);
    }

    /* Check if buffer is within our DMA buffer range */
    if (out_buf >= adma2_buffer &&
        out_buf + (count * 512) <= adma2_buffer + EMMC_ADMA2_BUF_SIZE) {
        /* Use ADMA2 DMA path */
        if (emmc_read_blocks_adma2_internal(lba, count, out_buf)) {
            return true;
        }
        debug_puts("[EMMC] ADMA2: DMA failed, falling back to PIO\n");
    }

    /* Fall back to PIO for buffers outside DMA range or on error */
    return emmc_read_blocks(lba, count, out_buf);
}

#endif /* EMMC_USE_ADMA2 */
