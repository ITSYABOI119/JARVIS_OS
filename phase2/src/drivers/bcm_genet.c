/*
 * JARVIS AI-OS - BCM2711 GENET v5 Ethernet Controller Driver
 *
 * Week 37: TX-only bare-metal driver for Raspberry Pi 4.
 * Based on Linux drivers/net/ethernet/broadcom/genet/bcmgenet.c
 *
 * Architecture:
 *   - GENET at paddr 0xFD580000 (separate device untyped from 0xFE peripherals)
 *   - 6 contiguous 4KB pages mapped to vaddr 0x604000-0x609FFF
 *   - Uses its own device untyped search (cannot reuse UART's device mapper)
 *   - TX DMA ring 16 (default ring) with all 256 descriptors
 *   - DMA descriptors live in GENET MMIO space (hardware RAM)
 *   - TX data buffers allocated via dma_alloc() (system RAM, uncacheable)
 *   - PHY: BCM54213PE at MDIO address 1, RGMII interface
 */

#include "bcm_genet.h"
#include "slot_alloc.h"
#include "dma_alloc.h"

#include <sel4/sel4.h>
#include <sel4runtime.h>
#include <string.h>

/* ================================================================
 * Internal State
 * ================================================================ */

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

/* Device memory attributes for MMIO mapping (not defined in all seL4 versions) */
#ifndef seL4_ARM_Device_VMAttributes
#define seL4_ARM_Device_VMAttributes 0
#endif

#define PAGE_SIZE_4K    0x1000u
#define PAGE_BITS_4K    12

static volatile uint8_t *genet_mmio_base = NULL;
static bool genet_initialized = false;

/* TX ring state */
static genet_tx_ring_t tx_ring;

/* TX DMA buffer (from dma_alloc) */
static uint8_t *tx_dma_buf = NULL;
static uintptr_t tx_dma_paddr = 0;

/* RX ring state */
static genet_rx_ring_t rx_ring;

/* RX DMA buffer pool */
static uint8_t *rx_dma_buf = NULL;
static uintptr_t rx_dma_paddr = 0;
static uint32_t rx_dma_buf_size = 0;

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
 * MMIO Accessors
 * ================================================================ */

static inline uint32_t genet_reg_read(uint32_t offset)
{
    uint32_t val = *(volatile uint32_t *)(genet_mmio_base + offset);
    __asm__ volatile("dsb sy" ::: "memory");
    return val;
}

static inline void genet_reg_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(genet_mmio_base + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ================================================================
 * Device Untyped Search (GENET-specific)
 *
 * GENET at 0xFD580000 is in a DIFFERENT device untyped than the
 * main peripherals at 0xFE000000. The UART driver's device mapper
 * cursor is already past 0xFE342000, so we cannot reuse it.
 * ================================================================ */

static seL4_CPtr genet_find_device_untyped(seL4_BootInfo *bi, seL4_Word paddr,
                                           seL4_Word *base_out, int *size_bits_out)
{
    for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++) {
        seL4_UntypedDesc *ud = &bi->untypedList[i];

        if (!ud->isDevice) {
            continue;
        }

        seL4_Word base = ud->paddr;
        seL4_Word size = BIT(ud->sizeBits);

        if (paddr >= base && paddr < base + size) {
            if (base_out) *base_out = base;
            if (size_bits_out) *size_bits_out = ud->sizeBits;
            return bi->untyped.start + i;
        }
    }
    return seL4_CapNull;
}

/* ================================================================
 * MMIO Mapping
 *
 * Map 6 contiguous 4KB pages from GENET physical base to virtual
 * address space. Uses binary buddy skip to advance from untyped
 * base to GENET_BASE, then retypes 6 small pages.
 * ================================================================ */

static bool genet_map_mmio(void)
{
    seL4_BootInfo *bi = sel4runtime_bootinfo();
    if (!bi) {
        debug_puts("[GENET] ERROR: bootinfo is NULL\n");
        return false;
    }

    /* Find device untyped covering GENET_BASE */
    seL4_Word ut_base = 0;
    int size_bits = 0;
    seL4_CPtr ut = genet_find_device_untyped(bi, GENET_BASE, &ut_base, &size_bits);

    if (ut == seL4_CapNull) {
        debug_puts("[GENET] ERROR: No device untyped covers ");
        debug_hex(GENET_BASE);
        debug_puts("\n");
        return false;
    }

    debug_puts("[GENET] Found device untyped: base=");
    debug_hex(ut_base);
    debug_puts(" size=");
    debug_hex(BIT(size_bits));
    debug_puts(" cap=");
    debug_hex(ut);
    debug_puts("\n");

    /* Verify GENET pages fit within the untyped */
    seL4_Word ut_size = BIT(size_bits);
    seL4_Word genet_end = GENET_BASE + (GENET_MMIO_PAGES * PAGE_SIZE_4K);
    if (genet_end > ut_base + ut_size) {
        debug_puts("[GENET] ERROR: GENET pages exceed device untyped\n");
        return false;
    }

    /* Binary buddy skip: advance cursor from ut_base to GENET_BASE */
    seL4_Word cursor = ut_base;
    seL4_Word target = GENET_BASE;

    debug_puts("[GENET] Buddy skip from ");
    debug_hex(cursor);
    debug_puts(" to ");
    debug_hex(target);
    debug_puts("\n");

    while (cursor < target) {
        seL4_Word gap = target - cursor;
        /* Find largest power-of-2 aligned at cursor that fits in gap */
        int skip_bits = PAGE_BITS_4K;
        while (skip_bits < 30 &&
               ((1UL << (skip_bits + 1)) <= gap) &&
               ((cursor & ((1UL << (skip_bits + 1)) - 1)) == 0)) {
            skip_bits++;
        }

        seL4_CPtr skip_cap = slot_alloc_next("GENET buddy skip");
        if (skip_cap == seL4_CapNull) {
            debug_puts("[GENET] ERROR: slot alloc failed for buddy skip\n");
            return false;
        }

        debug_puts("[GENET] Buddy retype bits=");
        debug_hex(skip_bits);
        debug_puts(" at ");
        debug_hex(cursor);
        debug_puts("\n");

        seL4_Error err = seL4_Untyped_Retype(
            ut,
            seL4_UntypedObject,
            skip_bits,
            seL4_CapInitThreadCNode,
            0, 0,
            skip_cap,
            1
        );

        if (err != seL4_NoError) {
            debug_puts("[GENET] ERROR: Buddy skip retype failed, err=");
            debug_hex(err);
            debug_puts("\n");
            return false;
        }

        cursor += (1UL << skip_bits);
    }

    debug_puts("[GENET] Buddy skip complete, cursor at ");
    debug_hex(cursor);
    debug_puts("\n");

    /* Now retype and map 6 consecutive 4KB device pages */
    for (int i = 0; i < GENET_MMIO_PAGES; i++) {
        seL4_CPtr frame_cap = slot_alloc_next("GENET frame");
        if (frame_cap == seL4_CapNull) {
            debug_puts("[GENET] ERROR: slot alloc failed for frame ");
            debug_hex(i);
            debug_puts("\n");
            return false;
        }

        seL4_Error err = seL4_Untyped_Retype(
            ut,
            seL4_ARM_SmallPageObject,
            PAGE_BITS_4K,
            seL4_CapInitThreadCNode,
            0, 0,
            frame_cap,
            1
        );

        if (err != seL4_NoError) {
            debug_puts("[GENET] ERROR: Frame retype failed for page ");
            debug_hex(i);
            debug_puts(", err=");
            debug_hex(err);
            debug_puts("\n");
            return false;
        }

        seL4_Word vaddr = GENET_MMIO_VADDR_BASE + (i * PAGE_SIZE_4K);

        err = seL4_ARM_Page_Map(
            frame_cap,
            seL4_CapInitThreadVSpace,
            vaddr,
            seL4_AllRights,
            seL4_ARM_Device_VMAttributes
        );

        if (err != seL4_NoError) {
            debug_puts("[GENET] ERROR: Page_Map failed for page ");
            debug_hex(i);
            debug_puts(" vaddr=");
            debug_hex(vaddr);
            debug_puts(", err=");
            debug_hex(err);
            debug_puts("\n");
            return false;
        }

        debug_puts("[GENET] Mapped page ");
        debug_hex(i);
        debug_puts(": paddr=");
        debug_hex(GENET_BASE + i * PAGE_SIZE_4K);
        debug_puts(" -> vaddr=");
        debug_hex(vaddr);
        debug_puts("\n");
    }

    genet_mmio_base = (volatile uint8_t *)GENET_MMIO_VADDR_BASE;

    debug_puts("[GENET] MMIO mapped: vbase=");
    debug_hex(GENET_MMIO_VADDR_BASE);
    debug_puts("\n");

    return true;
}

/* ================================================================
 * Delay Helpers
 * ================================================================ */

static void genet_udelay(uint32_t us)
{
    /* ~1us per iteration at ~1GHz Cortex-A72 */
    for (volatile uint32_t i = 0; i < us * 10; i++);
}

static void genet_mdelay(uint32_t ms)
{
    genet_udelay(ms * 1000);
}

/* ================================================================
 * PHY Power-Up Sequence (GENET v5 / BCM2711)
 * Must be called before MDIO access to wake the external PHY.
 * ================================================================ */

static void genet_phy_power_up(void)
{
    uint32_t reg;

    /* Step 1: Enable 25MHz clock */
    reg = genet_reg_read(GENET_EXT_OFF + EXT_GPHY_CTRL);
    reg &= ~EXT_CK25_DIS;
    genet_reg_write(GENET_EXT_OFF + EXT_GPHY_CTRL, reg);
    genet_mdelay(1);

    /* Step 2: Clear power-down bits, assert GPHY reset */
    reg &= ~(EXT_CFG_IDDQ_BIAS | EXT_CFG_PWR_DOWN | EXT_CFG_IDDQ_GLOBAL_PWR);
    reg |= EXT_GPHY_RESET;
    genet_reg_write(GENET_EXT_OFF + EXT_GPHY_CTRL, reg);
    genet_mdelay(1);

    /* Step 3: De-assert GPHY reset */
    reg &= ~EXT_GPHY_RESET;
    genet_reg_write(GENET_EXT_OFF + EXT_GPHY_CTRL, reg);
    genet_udelay(60);

    /* Step 4: Clear EXT_PWR_MGMT power-down bits (v5 specific) */
    reg = genet_reg_read(GENET_EXT_OFF + EXT_EXT_PWR_MGMT);
    reg &= ~(EXT_PWR_DOWN_DLL | EXT_PWR_DOWN_BIAS | EXT_ENERGY_DET_MASK);
    reg &= ~(EXT_PWR_DOWN_PHY_EN | EXT_PWR_DOWN_PHY_RD |
             EXT_PWR_DOWN_PHY_SD | EXT_PWR_DOWN_PHY_RX |
             EXT_PWR_DOWN_PHY_TX | EXT_IDDQ_GLBL_PWR);
    reg |= EXT_PHY_RESET;
    genet_reg_write(GENET_EXT_OFF + EXT_EXT_PWR_MGMT, reg);
    genet_mdelay(1);

    /* De-assert PHY reset */
    reg &= ~EXT_PHY_RESET;
    genet_reg_write(GENET_EXT_OFF + EXT_EXT_PWR_MGMT, reg);

    debug_puts("[GENET] PHY power-up done\n");
}

/* ================================================================
 * UMAC Reset Sequence (from Linux bcmgenet_umac_reset())
 * ================================================================ */

static void genet_umac_reset(void)
{
    /* Clear RBUF flush first */
    genet_reg_write(GENET_SYS_OFF + SYS_RBUF_FLUSH_CTRL, 0);
    genet_udelay(10);

    /* Software reset UMAC with local loopback */
    genet_reg_write(GENET_UMAC_OFF + UMAC_CMD, CMD_SW_RESET | CMD_LCL_LOOP_EN);
    genet_udelay(2);
    genet_reg_write(GENET_UMAC_OFF + UMAC_CMD, 0);

    /* Toggle RBUF flush bit 1 */
    uint32_t reg = genet_reg_read(GENET_SYS_OFF + SYS_RBUF_FLUSH_CTRL);
    reg |= (1u << 1);
    genet_reg_write(GENET_SYS_OFF + SYS_RBUF_FLUSH_CTRL, reg);
    genet_udelay(10);
    reg &= ~(1u << 1);
    genet_reg_write(GENET_SYS_OFF + SYS_RBUF_FLUSH_CTRL, reg);
    genet_udelay(10);

    /* Disable TDMA/RDMA via both RING_CFG and DMA_CTRL */
    uint32_t tdma_global = GENET_TDMA_REG_OFF + DMA_RINGS_SIZE;
    uint32_t rdma_global = GENET_RDMA_REG_OFF + DMA_RINGS_SIZE;

    genet_reg_write(tdma_global + DMA_RING_CFG_OFF, 0);
    genet_reg_write(tdma_global + DMA_CTRL_OFF, 0);
    genet_reg_write(rdma_global + DMA_RING_CFG_OFF, 0);
    genet_reg_write(rdma_global + DMA_CTRL_OFF, 0);
}

/* ================================================================
 * genet_init()
 * ================================================================ */

bool genet_init(void)
{
    if (genet_initialized) {
        return true;
    }

    debug_puts("[GENET] Initializing BCM2711 GENET v5 Ethernet\n");

    /* Verify dependencies */
    if (!slot_alloc_ready()) {
        debug_puts("[GENET] ERROR: slot_alloc not initialized\n");
        return false;
    }
    if (!dma_alloc_is_ready()) {
        debug_puts("[GENET] ERROR: dma_alloc not initialized\n");
        return false;
    }

    /*
     * IMPORTANT: Allocate DMA buffer BEFORE any GENET register writes.
     * Hardware testing revealed that after GENET PHY/UMAC register writes,
     * the next seL4_Untyped_Retype() causes a kernel halt (irq 27).
     * Pre-allocating the DMA buffer avoids this issue.
     */
    debug_puts("[GENET] Allocating TX DMA buffer (pre-register-write)...\n");
    tx_dma_buf = (uint8_t *)dma_alloc(PAGE_SIZE_4K, &tx_dma_paddr);
    if (!tx_dma_buf) {
        debug_puts("[GENET] ERROR: Failed to allocate TX DMA buffer\n");
        return false;
    }
    debug_puts("[GENET] TX DMA buf: vaddr=");
    debug_hex((uintptr_t)tx_dma_buf);
    debug_puts(" paddr=");
    debug_hex(tx_dma_paddr);
    debug_puts("\n");

    /* Pre-allocate RX DMA buffers (MUST be before MMIO mapping) */
    rx_dma_buf_size = GENET_RX_DESC_COUNT * GENET_RX_BUF_SIZE;  /* 32 * 2048 = 64KB = 16 pages */
    debug_puts("[GENET] Allocating RX DMA buffers (pre-register-write)...\n");
    rx_dma_buf = (uint8_t *)dma_alloc(rx_dma_buf_size, &rx_dma_paddr);
    if (!rx_dma_buf) {
        debug_puts("[GENET] ERROR: Failed to allocate RX DMA buffers\n");
        return false;
    }
    debug_puts("[GENET] RX DMA buf: vaddr=");
    debug_hex((uintptr_t)rx_dma_buf);
    debug_puts(" paddr=");
    debug_hex(rx_dma_paddr);
    debug_puts(" size=");
    debug_hex(rx_dma_buf_size);
    debug_puts("\n");

    /* Map GENET MMIO registers */
    if (!genet_map_mmio()) {
        debug_puts("[GENET] ERROR: MMIO mapping failed\n");
        return false;
    }

    /* Read and display revision register */
    uint32_t rev = genet_reg_read(GENET_SYS_OFF + SYS_REV_CTRL);
    debug_puts("[GENET] REV_CTRL = ");
    debug_hex(rev);
    debug_puts("\n");

    /* GENET v5: major version field (bits 27:24). BCM2711 reports 6,
     * which Linux normalizes to GENET v5 semantics. */
    uint32_t major = (rev >> 24) & 0xF;
    debug_puts("[GENET] Version major=");
    debug_hex(major);
    debug_puts("\n");

    /* Power up external PHY before any MDIO access */
    genet_phy_power_up();

    /* Reset UMAC */
    genet_umac_reset();
    debug_puts("[GENET] UMAC reset done\n");

    /* Clear MIB counters */
    debug_puts("[GENET] MIB clear...\n");
    genet_reg_write(GENET_UMAC_OFF + UMAC_MIB_CTRL,
                    MIB_RESET_RX | MIB_RESET_RUNT | MIB_RESET_TX);
    genet_reg_write(GENET_UMAC_OFF + UMAC_MIB_CTRL, 0);

    /* Set max frame length */
    debug_puts("[GENET] Max frame len...\n");
    genet_reg_write(GENET_UMAC_OFF + UMAC_MAX_FRAME_LEN, ENET_MAX_MTU_SIZE);

    /* Configure RBUF: enable 64-byte readahead for DMA */
    debug_puts("[GENET] RBUF config...\n");
    uint32_t rbuf = genet_reg_read(GENET_RBUF_OFF + RBUF_CTRL);
    rbuf |= RBUF_64B_EN;
    genet_reg_write(GENET_RBUF_OFF + RBUF_CTRL, rbuf);

    /* Set RBUF/TBUF size (default) */
    debug_puts("[GENET] TBUF size ctrl...\n");
    genet_reg_write(GENET_RBUF_OFF + RBUF_TBUF_SIZE_CTRL, 1);

    /* Set port mode to external GPHY (RGMII) */
    debug_puts("[GENET] Port mode...\n");
    genet_reg_write(GENET_SYS_OFF + SYS_PORT_CTRL, PORT_MODE_EXT_GPHY);

    /* Configure EXT RGMII for external PHY */
    debug_puts("[GENET] RGMII OOB...\n");
    uint32_t oob = genet_reg_read(GENET_EXT_OFF + EXT_RGMII_OOB_CTRL);
    oob |= RGMII_MODE_EN;
    oob |= ID_MODE_DIS;
    oob &= ~OOB_DISABLE;
    oob |= RGMII_LINK;
    genet_reg_write(GENET_EXT_OFF + EXT_RGMII_OOB_CTRL, oob);

    /* Mask all interrupts (polled TX) */
    debug_puts("[GENET] Mask interrupts...\n");
    genet_reg_write(GENET_INTRL2_0_OFF + INTRL2_CPU_MASK_SET, 0xFFFFFFFF);
    genet_reg_write(GENET_INTRL2_1_OFF + INTRL2_CPU_MASK_SET, 0xFFFFFFFF);

    /* Clear any pending interrupts */
    genet_reg_write(GENET_INTRL2_0_OFF + INTRL2_CPU_CLEAR, 0xFFFFFFFF);
    genet_reg_write(GENET_INTRL2_1_OFF + INTRL2_CPU_CLEAR, 0xFFFFFFFF);

    genet_initialized = true;
    debug_puts("[GENET] Init complete\n");
    return true;
}

bool genet_is_initialized(void)
{
    return genet_initialized;
}

/* ================================================================
 * genet_read_rev()
 * ================================================================ */

uint32_t genet_read_rev(void)
{
    if (!genet_mmio_base) {
        return 0;
    }
    return genet_reg_read(GENET_SYS_OFF + SYS_REV_CTRL);
}

/* ================================================================
 * genet_set_mac()
 * ================================================================ */

void genet_set_mac(const uint8_t mac[6])
{
    if (!genet_initialized) {
        return;
    }

    /* MAC0 = bytes[5:2], MAC1 = bytes[1:0] (big-endian in register) */
    uint32_t mac0 = ((uint32_t)mac[0] << 24) |
                    ((uint32_t)mac[1] << 16) |
                    ((uint32_t)mac[2] << 8)  |
                    ((uint32_t)mac[3]);
    uint32_t mac1 = ((uint32_t)mac[4] << 8) |
                    ((uint32_t)mac[5]);

    genet_reg_write(GENET_UMAC_OFF + UMAC_MAC0, mac0);
    genet_reg_write(GENET_UMAC_OFF + UMAC_MAC1, mac1);

    debug_puts("[GENET] MAC set to ");
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        if (i) seL4_DebugPutChar(':');
        seL4_DebugPutChar(hex[mac[i] >> 4]);
        seL4_DebugPutChar(hex[mac[i] & 0xF]);
    }
    debug_puts("\n");
}

/* ================================================================
 * MDIO Read/Write (via UMAC_MDIO_CMD)
 * ================================================================ */

static bool genet_mdio_wait(void)
{
    /* Poll MDIO_START_BUSY until cleared (or timeout) */
    for (int i = 0; i < 100000; i++) {
        uint32_t cmd = genet_reg_read(GENET_UMAC_OFF + UMAC_MDIO_CMD);
        if (!(cmd & MDIO_START_BUSY)) {
            return true;
        }
        for (volatile int d = 0; d < 100; d++);
    }
    debug_puts("[GENET] MDIO timeout\n");
    return false;
}

static uint16_t genet_mdio_read(uint8_t phy_addr, uint8_t reg)
{
    uint32_t cmd = MDIO_START_BUSY | MDIO_RD |
                   ((uint32_t)phy_addr << MDIO_PMD_SHIFT) |
                   ((uint32_t)reg << MDIO_REG_SHIFT);

    genet_reg_write(GENET_UMAC_OFF + UMAC_MDIO_CMD, cmd);

    if (!genet_mdio_wait()) {
        return 0xFFFF;
    }

    uint32_t result = genet_reg_read(GENET_UMAC_OFF + UMAC_MDIO_CMD);
    if (result & MDIO_READ_FAIL) {
        debug_puts("[GENET] MDIO read fail reg=");
        debug_hex(reg);
        debug_puts("\n");
        return 0xFFFF;
    }

    return (uint16_t)(result & 0xFFFF);
}

static void genet_mdio_write(uint8_t phy_addr, uint8_t reg, uint16_t val)
{
    uint32_t cmd = MDIO_START_BUSY | MDIO_WR |
                   ((uint32_t)phy_addr << MDIO_PMD_SHIFT) |
                   ((uint32_t)reg << MDIO_REG_SHIFT) |
                   val;

    genet_reg_write(GENET_UMAC_OFF + UMAC_MDIO_CMD, cmd);
    genet_mdio_wait();
}

/* ================================================================
 * genet_phy_init()
 * ================================================================ */

bool genet_phy_init(void)
{
    if (!genet_initialized) {
        return false;
    }

    debug_puts("[GENET] PHY init (addr=");
    debug_hex(GENET_PHY_ADDR);
    debug_puts(")\n");

    /* BCM2711 workaround: dummy MDIO read before first real access */
    (void)genet_mdio_read(GENET_PHY_ADDR, MII_BMSR);

    /* Read PHY ID */
    uint16_t id1 = genet_mdio_read(GENET_PHY_ADDR, MII_PHYSID1);
    uint16_t id2 = genet_mdio_read(GENET_PHY_ADDR, MII_PHYSID2);

    debug_puts("[GENET] PHY ID: ");
    debug_hex(id1);
    debug_puts(":");
    debug_hex(id2);
    debug_puts("\n");

    /* Check if PHY responds (0xFFFF = no PHY) */
    if (id1 == 0xFFFF && id2 == 0xFFFF) {
        debug_puts("[GENET] ERROR: No PHY detected at addr ");
        debug_hex(GENET_PHY_ADDR);
        debug_puts("\n");
        return false;
    }

    /* BCM54213PE: Broadcom OUI = 0x0143, model varies */
    debug_puts("[GENET] PHY detected (OUI=");
    debug_hex((id1 << 6) | ((id2 >> 10) & 0x3F));
    debug_puts(")\n");

    /* Reset PHY */
    genet_mdio_write(GENET_PHY_ADDR, MII_BMCR, BMCR_RESET);

    /* Wait for reset to complete */
    for (int i = 0; i < 1000; i++) {
        uint16_t bmcr = genet_mdio_read(GENET_PHY_ADDR, MII_BMCR);
        if (!(bmcr & BMCR_RESET)) {
            break;
        }
        for (volatile int d = 0; d < 10000; d++);
    }

    /* Enable auto-negotiation */
    genet_mdio_write(GENET_PHY_ADDR, MII_BMCR,
                     BMCR_ANENABLE | BMCR_ANRESTART);

    debug_puts("[GENET] PHY auto-negotiation started\n");

    /* Read BMSR to check link status */
    uint16_t bmsr = genet_mdio_read(GENET_PHY_ADDR, MII_BMSR);
    debug_puts("[GENET] BMSR = ");
    debug_hex(bmsr);
    if (bmsr & BMSR_LSTATUS) {
        debug_puts(" (link UP)\n");
    } else {
        debug_puts(" (link DOWN - cable may not be connected)\n");
    }

    return true;
}

/* ================================================================
 * genet_tx_ring_init()
 *
 * Configure TX DMA ring 16 (default ring) with all 256 descriptors.
 * Ring 16's registers are at TDMA_REG_OFF + 16 * 0x40.
 * ================================================================ */

bool genet_tx_ring_init(void)
{
    if (!genet_initialized) {
        return false;
    }

    debug_puts("[GENET] TX ring init (ring 16, 256 descs)\n");

    /* Ring 16 register base */
    uint32_t ring_base = GENET_TDMA_REG_OFF + (GENET_DESC_INDEX * DMA_RING_STRIDE);

    /* Global TDMA control register */
    uint32_t tdma_global = GENET_TDMA_REG_OFF + DMA_RINGS_SIZE;

    /* Disable TDMA first */
    genet_reg_write(tdma_global + DMA_CTRL_OFF, 0);

    /* Flush TX */
    genet_reg_write(GENET_UMAC_OFF + UMAC_TX_FLUSH, 1);
    for (volatile int d = 0; d < 10000; d++);
    genet_reg_write(GENET_UMAC_OFF + UMAC_TX_FLUSH, 0);

    /* Zero all descriptor entries in ring 16 region.
     * Descriptors for ring 16 start at TDMA offset 0 (desc 0) and use
     * all 256 descriptors. Each is 12 bytes (3 words). */
    for (int i = 0; i < GENET_TOTAL_DESC; i++) {
        uint32_t desc_off = GENET_TDMA_OFF + (i * DMA_DESC_SIZE);
        genet_reg_write(desc_off + DMA_DESC_LENGTH_STATUS, 0);
        genet_reg_write(desc_off + DMA_DESC_ADDRESS_LO, 0);
        genet_reg_write(desc_off + DMA_DESC_ADDRESS_HI, 0);
    }

    /* Configure ring 16 registers */
    /* Read/write pointers and indices start at 0 */
    genet_reg_write(ring_base + RING_READ_PTR, 0);
    genet_reg_write(ring_base + RING_READ_PTR_HI, 0);
    genet_reg_write(ring_base + RING_WRITE_PTR, 0);
    genet_reg_write(ring_base + RING_WRITE_PTR_HI, 0);
    genet_reg_write(ring_base + RING_PROD_INDEX, 0);
    genet_reg_write(ring_base + RING_CONS_INDEX, 0);

    /* Ring buffer size: (desc_count << 16) | buf_length */
    genet_reg_write(ring_base + RING_BUF_SIZE,
                    (GENET_TOTAL_DESC << DMA_RING_SIZE_SHIFT) | DMA_MAX_BUF_LENGTH);

    /* Start/end address cover all descriptors: 0 to (256 * 12 - 1) in 3-word units */
    genet_reg_write(ring_base + RING_START_ADDR, 0);
    genet_reg_write(ring_base + RING_START_ADDR_HI, 0);
    genet_reg_write(ring_base + RING_END_ADDR,
                    GENET_TOTAL_DESC * GENET_WORDS_PER_BD - 1);
    genet_reg_write(ring_base + RING_END_ADDR_HI, 0);

    /* Completion threshold = 1 (one frame at a time) */
    genet_reg_write(ring_base + RING_MBUF_DONE_THRESH, 1);

    /* Flow period = 0 */
    genet_reg_write(ring_base + RING_FLOW_PERIOD, 0);

    /* Set SCB burst size */
    genet_reg_write(tdma_global + DMA_SCB_BURST_SIZE_OFF, DMA_MAX_BURST_LENGTH);

    /* Enable ring 16 in DMA_RING_CFG bitmask */
    uint32_t ring_mask = (1u << GENET_DESC_INDEX);
    genet_reg_write(tdma_global + DMA_RING_CFG_OFF, ring_mask);

    /* Enable TDMA: DMA_EN | ring 16 buffer enable */
    uint32_t dma_ctrl = DMA_EN | (ring_mask << DMA_RING_BUF_EN_SHIFT);
    genet_reg_write(tdma_global + DMA_CTRL_OFF, dma_ctrl);

    debug_puts("[GENET] TDMA enabled, ring_cfg=");
    debug_hex(ring_mask);
    debug_puts(" ctrl=");
    debug_hex(dma_ctrl);
    debug_puts("\n");

    /* Enable UMAC TX */
    uint32_t cmd = genet_reg_read(GENET_UMAC_OFF + UMAC_CMD);
    cmd |= CMD_TX_EN;
    genet_reg_write(GENET_UMAC_OFF + UMAC_CMD, cmd);

    /* Initialize software ring state */
    tx_ring.prod_index = 0;
    tx_ring.size = GENET_TOTAL_DESC;

    debug_puts("[GENET] TX ring init done\n");
    return true;
}

/* ================================================================
 * genet_tx_send()
 *
 * Send a raw Ethernet frame. Copies frame to DMA buffer, writes
 * one DMA descriptor (SOP | EOP | CRC_APPEND), and bumps the
 * producer index as doorbell.
 * ================================================================ */

bool genet_tx_send(const uint8_t *frame, uint32_t len)
{
    if (!genet_initialized || !tx_dma_buf) {
        debug_puts("[GENET] TX: not initialized\n");
        return false;
    }

    /* Validate frame length (14 = Ethernet header, 1514 = max with header) */
    if (len < 14 || len > 1514) {
        debug_puts("[GENET] TX: bad length ");
        debug_hex(len);
        debug_puts("\n");
        return false;
    }

    /* Check ring space: compare prod_index with cons_index */
    uint32_t ring_base = GENET_TDMA_REG_OFF + (GENET_DESC_INDEX * DMA_RING_STRIDE);
    uint32_t cons = genet_reg_read(ring_base + RING_CONS_INDEX) & DMA_INDEX_MASK;
    uint32_t prod = tx_ring.prod_index & DMA_INDEX_MASK;

    /* Ring full when (prod + 1) % size == cons */
    if (((prod + 1) % tx_ring.size) == cons) {
        debug_puts("[GENET] TX: ring full\n");
        return false;
    }

    /* Copy frame data to DMA buffer */
    memcpy(tx_dma_buf, frame, len);

    /* Compute descriptor index within ring */
    uint32_t desc_idx = prod % tx_ring.size;
    uint32_t desc_off = GENET_TDMA_OFF + (desc_idx * DMA_DESC_SIZE);

    /* Write DMA descriptor:
     *   address_lo = low 32 bits of DMA buffer paddr
     *   address_hi = high 32 bits (0 for Pi 4, all RAM < 4GB)
     *   length_status = (len << 16) | SOP | EOP | CRC_APPEND | (ring_idx << QTAG_SHIFT)
     */
    genet_reg_write(desc_off + DMA_DESC_ADDRESS_LO, (uint32_t)(tx_dma_paddr & 0xFFFFFFFF));
    genet_reg_write(desc_off + DMA_DESC_ADDRESS_HI, (uint32_t)(tx_dma_paddr >> 32));

    uint32_t length_status = ((uint32_t)len << DMA_BUFLENGTH_SHIFT) |
                             DMA_SOP | DMA_EOP | DMA_TX_APPEND_CRC |
                             ((uint32_t)GENET_DESC_INDEX << DMA_TX_QTAG_SHIFT);
    genet_reg_write(desc_off + DMA_DESC_LENGTH_STATUS, length_status);

    /* Doorbell: increment producer index */
    tx_ring.prod_index++;
    genet_reg_write(ring_base + RING_PROD_INDEX, tx_ring.prod_index & DMA_INDEX_MASK);

    debug_puts("[GENET] TX: sent ");
    debug_hex(len);
    debug_puts(" bytes, desc=");
    debug_hex(desc_idx);
    debug_puts(" prod=");
    debug_hex(tx_ring.prod_index);
    debug_puts("\n");

    /* Poll for completion (optional, best-effort for diagnostics) */
    for (int i = 0; i < 100000; i++) {
        uint32_t new_cons = genet_reg_read(ring_base + RING_CONS_INDEX) & DMA_INDEX_MASK;
        if (new_cons != cons) {
            debug_puts("[GENET] TX: DMA consumed desc, cons=");
            debug_hex(new_cons);
            debug_puts("\n");
            return true;
        }
        for (volatile int d = 0; d < 10; d++);
    }

    debug_puts("[GENET] TX: DMA completion timeout (frame may still send)\n");
    return true;  /* Frame submitted; timeout doesn't mean failure */
}

/* ================================================================
 * genet_rx_ring_init()
 *
 * Configure RX DMA ring 16 with GENET_RX_DESC_COUNT descriptors.
 * Each descriptor points to a pre-allocated DMA buffer.
 * ================================================================ */

bool genet_rx_ring_init(void)
{
    if (!genet_initialized || !rx_dma_buf) {
        debug_puts("[GENET] RX ring: not initialized or no DMA buf\n");
        return false;
    }

    debug_puts("[GENET] RX ring init (ring 16, ");
    debug_hex(GENET_RX_DESC_COUNT);
    debug_puts(" descs)\n");

    /* Ring 16 register base (RDMA side) */
    uint32_t ring_base = GENET_RDMA_REG_OFF + (GENET_DESC_INDEX * DMA_RING_STRIDE);

    /* Global RDMA control register base */
    uint32_t rdma_global = GENET_RDMA_REG_OFF + DMA_RINGS_SIZE;

    /* Disable RDMA first */
    genet_reg_write(rdma_global + DMA_CTRL_OFF, 0);

    /* Zero all RX descriptors first */
    for (uint32_t i = 0; i < GENET_RX_DESC_COUNT; i++) {
        uint32_t desc_off = GENET_RDMA_OFF + (i * DMA_DESC_SIZE);
        genet_reg_write(desc_off + DMA_DESC_LENGTH_STATUS, 0);
        genet_reg_write(desc_off + DMA_DESC_ADDRESS_LO, 0);
        genet_reg_write(desc_off + DMA_DESC_ADDRESS_HI, 0);
    }

    /* Assign a DMA buffer to each descriptor */
    for (uint32_t i = 0; i < GENET_RX_DESC_COUNT; i++) {
        uint32_t desc_off = GENET_RDMA_OFF + (i * DMA_DESC_SIZE);
        uintptr_t buf_paddr = rx_dma_paddr + (i * GENET_RX_BUF_SIZE);

        /* Write buffer physical address */
        genet_reg_write(desc_off + DMA_DESC_ADDRESS_LO, (uint32_t)(buf_paddr & 0xFFFFFFFF));
        genet_reg_write(desc_off + DMA_DESC_ADDRESS_HI, 0);
    }

    /* Configure ring 16 registers */
    genet_reg_write(ring_base + RING_READ_PTR, 0);
    genet_reg_write(ring_base + RING_READ_PTR_HI, 0);
    genet_reg_write(ring_base + RING_WRITE_PTR, 0);
    genet_reg_write(ring_base + RING_WRITE_PTR_HI, 0);
    genet_reg_write(ring_base + RING_PROD_INDEX, 0);
    genet_reg_write(ring_base + RING_CONS_INDEX, 0);

    /* Ring buffer size: (desc_count << 16) | buf_length */
    genet_reg_write(ring_base + RING_BUF_SIZE,
                    (GENET_RX_DESC_COUNT << DMA_RING_SIZE_SHIFT) | GENET_RX_BUF_SIZE);

    /* Start/end address in 3-word (descriptor) units */
    genet_reg_write(ring_base + RING_START_ADDR, 0);
    genet_reg_write(ring_base + RING_START_ADDR_HI, 0);
    genet_reg_write(ring_base + RING_END_ADDR,
                    GENET_RX_DESC_COUNT * GENET_WORDS_PER_BD - 1);
    genet_reg_write(ring_base + RING_END_ADDR_HI, 0);

    /* Completion threshold */
    genet_reg_write(ring_base + RING_MBUF_DONE_THRESH, 1);
    genet_reg_write(ring_base + RING_FLOW_PERIOD, 0);

    /* Set SCB burst size for RDMA */
    genet_reg_write(rdma_global + DMA_SCB_BURST_SIZE_OFF, DMA_MAX_BURST_LENGTH);

    /* Enable ring 16 in RDMA ring config */
    uint32_t ring_mask = (1u << GENET_DESC_INDEX);
    genet_reg_write(rdma_global + DMA_RING_CFG_OFF, ring_mask);

    /* Enable RDMA: DMA_EN | ring 16 buffer enable */
    uint32_t dma_ctrl = DMA_EN | (ring_mask << DMA_RING_BUF_EN_SHIFT);
    genet_reg_write(rdma_global + DMA_CTRL_OFF, dma_ctrl);

    debug_puts("[GENET] RDMA enabled, ring_cfg=");
    debug_hex(ring_mask);
    debug_puts(" ctrl=");
    debug_hex(dma_ctrl);
    debug_puts("\n");

    /* Enable UMAC RX */
    uint32_t cmd = genet_reg_read(GENET_UMAC_OFF + UMAC_CMD);
    cmd |= CMD_RX_EN;
    genet_reg_write(GENET_UMAC_OFF + UMAC_CMD, cmd);

    /* Initialize software ring state */
    rx_ring.cons_index = 0;
    rx_ring.prod_index = 0;
    rx_ring.size = GENET_RX_DESC_COUNT;

    debug_puts("[GENET] RX ring init done\n");
    return true;
}

/* ================================================================
 * genet_rx_poll() - Check if frames are pending
 * ================================================================ */

bool genet_rx_poll(void)
{
    if (!genet_initialized) {
        return false;
    }

    uint32_t ring_base = GENET_RDMA_REG_OFF + (GENET_DESC_INDEX * DMA_RING_STRIDE);
    uint32_t hw_prod = genet_reg_read(ring_base + RING_PROD_INDEX) & DMA_INDEX_MASK;

    return (hw_prod != (rx_ring.cons_index & DMA_INDEX_MASK));
}

/* ================================================================
 * genet_rx_recv() - Receive a frame from RX ring
 * ================================================================ */

bool genet_rx_recv(uint8_t *buf, uint32_t buf_size, uint32_t *len_out)
{
    if (!genet_initialized || !rx_dma_buf || !buf || !len_out) {
        return false;
    }

    /* Check if any frames are available */
    uint32_t ring_base = GENET_RDMA_REG_OFF + (GENET_DESC_INDEX * DMA_RING_STRIDE);
    uint32_t hw_prod = genet_reg_read(ring_base + RING_PROD_INDEX) & DMA_INDEX_MASK;

    if (hw_prod == (rx_ring.cons_index & DMA_INDEX_MASK)) {
        return false;  /* No frames pending */
    }

    /* Read descriptor at consumer index */
    uint32_t desc_idx = rx_ring.cons_index % rx_ring.size;
    uint32_t desc_off = GENET_RDMA_OFF + (desc_idx * DMA_DESC_SIZE);
    uint32_t length_status = genet_reg_read(desc_off + DMA_DESC_LENGTH_STATUS);

    /* Extract DMA length from bits [31:16].
     * Hardware includes RBUF prepend (66 bytes with 64B_EN) + FCS (4 bytes).
     * Actual Ethernet frame = dma_len - RBUF_RX_OFFSET - RBUF_FCS_LEN */
    uint32_t dma_len = (length_status >> DMA_BUFLENGTH_SHIFT) & 0xFFF;
    if (dma_len <= RBUF_RX_OFFSET + RBUF_FCS_LEN) {
        debug_puts("[GENET] RX: runt frame, dma_len=");
        debug_hex(dma_len);
        debug_puts("\n");
        rx_ring.cons_index++;
        genet_reg_write(ring_base + RING_CONS_INDEX, rx_ring.cons_index & DMA_INDEX_MASK);
        return false;
    }
    uint32_t frame_len = dma_len - RBUF_RX_OFFSET - RBUF_FCS_LEN;

    /* Check for errors */
    if (length_status & (DMA_RX_CRC_ERROR | DMA_RX_OV | DMA_RX_RXER)) {
        debug_puts("[GENET] RX: error status=");
        debug_hex(length_status);
        debug_puts("\n");
        /* Still advance consumer to free the descriptor */
        rx_ring.cons_index++;
        genet_reg_write(ring_base + RING_CONS_INDEX, rx_ring.cons_index & DMA_INDEX_MASK);
        return false;
    }

    /* Validate SOP+EOP (single-buffer frame) */
    if (!(length_status & DMA_SOP) || !(length_status & DMA_EOP)) {
        debug_puts("[GENET] RX: multi-buffer frame not supported\n");
        rx_ring.cons_index++;
        genet_reg_write(ring_base + RING_CONS_INDEX, rx_ring.cons_index & DMA_INDEX_MASK);
        return false;
    }

    /* Copy frame data from DMA buffer to caller's buffer.
     * Skip RBUF_RX_OFFSET bytes of prepended status data. */
    uint32_t copy_len = (frame_len < buf_size) ? frame_len : buf_size;
    uint8_t *src = rx_dma_buf + (desc_idx * GENET_RX_BUF_SIZE) + RBUF_RX_OFFSET;
    memcpy(buf, src, copy_len);
    *len_out = frame_len;

    /* Advance consumer index (doorbell) */
    rx_ring.cons_index++;
    genet_reg_write(ring_base + RING_CONS_INDEX, rx_ring.cons_index & DMA_INDEX_MASK);

    return true;
}
