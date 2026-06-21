/**
 * nic_i211.c - Intel I211-AT Gigabit Ethernet Driver (x86-64)
 *
 * Implements init, TX/RX via legacy 16-byte descriptors, link status,
 * and PCI probe for the Intel I211-AT NIC (ASUS X470-F Gaming motherboard).
 *
 * The I211 uses legacy TX/RX descriptors (16 bytes each). TX uses a tail
 * pointer (TDT) doorbell instead of a polling register. RX descriptors
 * have DD+EOP status bits set by hardware after frame reception.
 *
 * Register layout differs from classic e1000/82540EM:
 *   - RX: 0xC000 range (RDBAL, RDBAH, RDLEN, RDH, RDT)
 *   - TX: 0xE000 range (TDBAL, TDBAH, TDLEN, TDH, TDT)
 *   - Interrupts: 0x1500 range (ICR, ICS, IMS, IMC)
 *
 * References:
 *   - Intel I210/I211 Datasheet
 *   - Linux igb driver
 *
 * JARVIS AI-OS - Phase 3
 */

#include "nic_i211.h"
#include "pci.h"
#include <string.h>

/* ========================================================================
 * MMIO Access
 * ======================================================================== */

#ifndef JARVIS_TEST_MOCK

static inline uint32_t nic_read32(uint64_t base, uint32_t reg)
{
    return *(volatile uint32_t *)(uintptr_t)(base + reg);
}

static inline void nic_write32(uint64_t base, uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)(uintptr_t)(base + reg) = val;
}

#else
/* Mock MMIO for testing -- defined in test file */
extern uint32_t nic_read32(uint64_t base, uint32_t reg);
extern void     nic_write32(uint64_t base, uint32_t reg, uint32_t val);
#endif

/* Coarse delay for the post-reset settle (B3). On real hardware the I210/I211
 * needs a few ms after CTRL.RST auto-clears before the register file (incl. the
 * RAL/RAH MAC, loaded from NVM) is stable. Bounded — never blocks indefinitely. */
#ifndef JARVIS_TEST_MOCK
static inline uint64_t i211_rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static void i211_delay_ms(uint32_t ms)
{
    /* ~3.7 GHz invariant TSC on the box; approximate is fine for a settle. */
    uint64_t target = (uint64_t)ms * 3700000ULL;
    uint64_t start = i211_rdtsc();
    while ((i211_rdtsc() - start) < target)
        __asm__ __volatile__("pause");
}
#else
static void i211_delay_ms(uint32_t ms) { (void)ms; }  /* no-op in unit tests */
#endif

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

/**
 * Perform a device reset and wait for completion.
 * The RST bit in CTRL auto-clears when reset is complete.
 * Returns 0 on success, -1 on timeout.
 */
static int i211_device_reset(i211_nic_t *nic)
{
    uint32_t ctrl;
    int timeout;

    /* Set RST bit in CTRL */
    ctrl = nic_read32(nic->mmio_base, I211_CTRL);
    nic_write32(nic->mmio_base, I211_CTRL, ctrl | I211_CTRL_RST);

    /* Wait for RST bit to auto-clear (hardware signals reset complete) */
    timeout = I211_RESET_TIMEOUT;
    while (timeout > 0) {
        ctrl = nic_read32(nic->mmio_base, I211_CTRL);
        if (!(ctrl & I211_CTRL_RST))
            return 0;
        timeout--;
    }

    return -1;  /* Reset timed out */
}

/**
 * Read the hardware MAC address from RAL0/RAH0 registers.
 * RAL0 contains bytes 0-3, RAH0 bits 15:0 contain bytes 4-5.
 */
static int i211_read_mac(i211_nic_t *nic)
{
    uint32_t ral = nic_read32(nic->mmio_base, I211_RAL0);
    uint32_t rah = nic_read32(nic->mmio_base, I211_RAH0);

    nic->mac[0] = (uint8_t)(ral & 0xFF);
    nic->mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    nic->mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    nic->mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    nic->mac[4] = (uint8_t)(rah & 0xFF);
    nic->mac[5] = (uint8_t)((rah >> 8) & 0xFF);

    /* B3: a valid MAC requires RAH0.AV set AND a non-zero, non-broadcast address.
     * MAC=0 (or all-FF) means the reset settle was too short / NVM not yet loaded. */
    {
        int all_zero = 1, all_ff = 1;
        for (int i = 0; i < 6; i++) {
            if (nic->mac[i] != 0x00) all_zero = 0;
            if (nic->mac[i] != 0xFF) all_ff = 0;
        }
        nic->mac_valid = ((rah & I211_RAH_AV) && !all_zero && !all_ff) ? 1 : 0;
    }
    return nic->mac_valid ? 0 : -1;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

int i211_nic_init(i211_nic_t *nic, uint64_t mmio_base)
{
    i211_tx_desc_t *saved_tx;
    i211_rx_desc_t *saved_rx;
    uint64_t saved_tx_phys, saved_rx_phys;
    uint32_t i;

    if (!nic)
        return -1;

    /* Preserve caller-supplied ring pointers across the zero-init */
    saved_tx      = nic->tx_ring;
    saved_rx      = nic->rx_ring;
    saved_tx_phys = nic->tx_ring_phys;
    saved_rx_phys = nic->rx_ring_phys;

    memset(nic, 0, sizeof(*nic));
    nic->mmio_base    = mmio_base;
    nic->tx_ring      = saved_tx;
    nic->rx_ring      = saved_rx;
    nic->tx_ring_phys = saved_tx_phys;
    nic->rx_ring_phys = saved_rx_phys;

    /* 1. Disable all interrupts */
    nic_write32(mmio_base, I211_IMC, 0xFFFFFFFF);

    /* 2. Device reset */
    if (i211_device_reset(nic) < 0)
        return -1;

    /* B3: post-reset settle + wait for STATUS.PF_RST_DONE before touching the
     * register file. Both bounded — a missing PF_RST_DONE logs late, never hangs. */
    i211_delay_ms(3);
    {
        int t = I211_RESET_TIMEOUT;
        while (t-- > 0) {
            if (nic_read32(mmio_base, I211_STATUS) & I211_STATUS_PF_RST_DONE)
                break;
        }
    }

    /* 3. Disable interrupts again (reset may re-enable) */
    nic_write32(mmio_base, I211_IMC, 0xFFFFFFFF);

    /* 4. Read MAC address from hardware registers (sets nic->mac_valid) */
    (void)i211_read_mac(nic);

    /* 5. Set link up: CTRL |= SLU | ASDE */
    {
        uint32_t ctrl = nic_read32(mmio_base, I211_CTRL);
        ctrl |= I211_CTRL_SLU | I211_CTRL_ASDE;
        nic_write32(mmio_base, I211_CTRL, ctrl);
    }

    /* 6. Setup RX descriptor ring */
    if (nic->rx_ring) {
        memset(nic->rx_ring, 0, sizeof(i211_rx_desc_t) * I211_RX_RING_SIZE);

        /* Wire pre-assigned buffer addresses into descriptors */
        for (i = 0; i < I211_RX_RING_SIZE; i++) {
            if (nic->rx_bufs_phys[i])
                nic->rx_ring[i].addr = nic->rx_bufs_phys[i];
        }

        nic->rx_cur = 0;

        /* Program RX ring base and length */
        nic_write32(mmio_base, I211_RDBAL,
                    (uint32_t)(nic->rx_ring_phys & 0xFFFFFFFF));
        nic_write32(mmio_base, I211_RDBAH,
                    (uint32_t)(nic->rx_ring_phys >> 32));
        nic_write32(mmio_base, I211_RDLEN,
                    (uint32_t)(I211_RX_RING_SIZE * sizeof(i211_rx_desc_t)));
        nic_write32(mmio_base, I211_RDH, 0);
        nic_write32(mmio_base, I211_RDT, I211_RX_RING_SIZE - 1);
    }

    /* 7. Enable RX: EN | BAM | BSIZE_2K | SECRC */
    nic_write32(mmio_base, I211_RCTL,
                I211_RCTL_EN | I211_RCTL_BAM |
                I211_RCTL_BSIZE_2K | I211_RCTL_SECRC);

    /* 8. Setup TX descriptor ring */
    if (nic->tx_ring) {
        memset(nic->tx_ring, 0, sizeof(i211_tx_desc_t) * I211_TX_RING_SIZE);

        nic->tx_head = 0;

        /* Program TX ring base and length */
        nic_write32(mmio_base, I211_TDBAL,
                    (uint32_t)(nic->tx_ring_phys & 0xFFFFFFFF));
        nic_write32(mmio_base, I211_TDBAH,
                    (uint32_t)(nic->tx_ring_phys >> 32));
        nic_write32(mmio_base, I211_TDLEN,
                    (uint32_t)(I211_TX_RING_SIZE * sizeof(i211_tx_desc_t)));
        nic_write32(mmio_base, I211_TDH, 0);
        nic_write32(mmio_base, I211_TDT, 0);
    }

    /* 9. Enable TX: EN | PSP | CT=0x0F | COLD=0x040 */
    nic_write32(mmio_base, I211_TCTL,
                I211_TCTL_EN | I211_TCTL_PSP |
                (0x0F << I211_TCTL_CT_SHIFT) |
                (0x040 << I211_TCTL_COLD_SHIFT));

    /* B2: enable the TX queue (TXDCTL.ENABLE, bit 25) and poll it back. Without
     * this the I210/I211 silently never transmits. Bounded poll — log-late, never hang. */
    if (nic->tx_ring) {
        uint32_t txdctl = nic_read32(mmio_base, I211_TXDCTL);
        nic_write32(mmio_base, I211_TXDCTL, txdctl | I211_TXDCTL_ENABLE);
        int t = I211_RESET_TIMEOUT;
        while (t-- > 0) {
            if (nic_read32(mmio_base, I211_TXDCTL) & I211_TXDCTL_ENABLE)
                break;
        }
    }

    /* 10. Read initial link status */
    i211_nic_link_status(nic);

    return 0;
}

int i211_nic_probe_pci(i211_nic_t *nic)
{
    pci_device_t devices[32];
    int count, i;
    uint64_t bar_addr;

    if (!nic)
        return -1;

    /* Scan PCI bus for devices */
    count = pci_scan(devices, 32);

    /* Look for Intel 8086:1539 */
    for (i = 0; i < count; i++) {
        if (devices[i].vendor_id == I211_VENDOR_ID &&
            devices[i].device_id == I211_DEVICE_ID) {

            /* Enable bus mastering for DMA */
            pci_enable_bus_master(&devices[i]);

            /* Get MMIO base from BAR0 */
            if (pci_is_bar_mmio(&devices[i], 0)) {
                bar_addr = pci_get_bar_address(&devices[i], 0);
            } else {
                return -1;  /* No MMIO BAR found */
            }

            return i211_nic_init(nic, bar_addr);
        }
    }

    return -1;  /* Device not found */
}

void i211_nic_get_mac(i211_nic_t *nic, uint8_t mac[6])
{
    if (!nic || !mac)
        return;

    memcpy(mac, nic->mac, 6);
}

int i211_nic_send(i211_nic_t *nic, const void *buf, uint32_t len)
{
    i211_tx_desc_t *desc;
    uint32_t idx;

    if (!nic || !buf || len == 0 || len > I211_MAX_FRAME_SIZE)
        return -1;

    if (!nic->tx_ring)
        return -1;

    idx = nic->tx_head;
    desc = &nic->tx_ring[idx];

    /* Check if descriptor is available (DD must be set or desc must be clear) */
    /* For first use after init, sta==0 is fine (host owns it) */

    /* Build legacy TX descriptor.
     * NOTE: this stores the buffer's VIRTUAL address — correct ONLY when the buffer
     * is identity-mapped (or in the mock test). Under KernelIOMMU=OFF seL4 the NIC
     * DMAs physical addresses, so the deployed path MUST use i211_send_phys() (below),
     * which writes the buffer's physical address into the descriptor. */
    desc->addr    = (uint64_t)(uintptr_t)buf;
    desc->length  = (uint16_t)len;
    desc->cso     = 0;
    desc->cmd     = I211_TX_CMD_EOP | I211_TX_CMD_IFCS | I211_TX_CMD_RS;
    desc->sta     = 0;
    desc->css     = 0;
    desc->special = 0;

    /* Advance TX head with wraparound */
    nic->tx_head = (idx + 1) % I211_TX_RING_SIZE;

    /* Write TDT to kick the NIC (doorbell) */
    nic_write32(nic->mmio_base, I211_TDT, nic->tx_head);

    nic->tx_packets++;
    nic->tx_bytes += len;

    return 0;
}

int i211_send_phys(i211_nic_t *nic, void *txbuf_vaddr, uint64_t txbuf_paddr,
                   const void *frame, uint32_t len)
{
    i211_tx_desc_t *desc;
    uint32_t idx;

    if (!nic || !txbuf_vaddr || !frame || len == 0 || len > I211_MAX_FRAME_SIZE)
        return -1;
    if (!nic->tx_ring)
        return -1;

    /* Copy the frame into the known-physical DMA TX buffer (the DMA source). */
    memcpy(txbuf_vaddr, frame, len);

    idx  = nic->tx_head;
    desc = &nic->tx_ring[idx];

    /* PHYSICAL address — IOMMU is off, so the NIC DMAs exactly what we write here. */
    desc->addr    = txbuf_paddr;
    desc->length  = (uint16_t)len;
    desc->cso     = 0;
    desc->cmd     = I211_TX_CMD_EOP | I211_TX_CMD_IFCS | I211_TX_CMD_RS;
    desc->sta     = 0;
    desc->css     = 0;
    desc->special = 0;

    nic->tx_head = (idx + 1) % I211_TX_RING_SIZE;

    /* Doorbell: advance TDT to the new head so the NIC fetches this descriptor. */
    nic_write32(nic->mmio_base, I211_TDT, nic->tx_head);

    nic->tx_packets++;
    nic->tx_bytes += len;

    return (int)idx;   /* caller polls tx_ring[idx].sta & I211_TX_STA_DD */
}

int i211_nic_recv(i211_nic_t *nic, void *buf, uint32_t max_len)
{
    i211_rx_desc_t *desc;
    uint32_t idx, frame_len;

    if (!nic || !buf || max_len == 0)
        return -1;

    if (!nic->rx_ring)
        return -1;

    idx = nic->rx_cur;
    desc = &nic->rx_ring[idx];

    /* Check if NIC has completed this descriptor (DD set) */
    if (!(desc->status & I211_RX_STA_DD))
        return 0;  /* No frame available */

    /* Check for end-of-packet */
    if (!(desc->status & I211_RX_STA_EOP)) {
        nic->rx_errors++;
        /* Clear and re-arm */
        desc->status = 0;
        desc->length = 0;
        nic->rx_cur = (idx + 1) % I211_RX_RING_SIZE;
        nic_write32(nic->mmio_base, I211_RDT, idx);
        return -1;
    }

    /* Check for errors */
    if (desc->errors) {
        nic->rx_errors++;
        desc->status = 0;
        desc->length = 0;
        nic->rx_cur = (idx + 1) % I211_RX_RING_SIZE;
        nic_write32(nic->mmio_base, I211_RDT, idx);
        return -1;
    }

    /* Extract frame length from descriptor */
    frame_len = desc->length;

    /* SEC-033: Clamp to BOTH output buffer size AND DMA buffer size.
     * A malicious/buggy NIC could report length > I211_RX_BUF_SIZE. */
    if (frame_len > I211_RX_BUF_SIZE)
        frame_len = I211_RX_BUF_SIZE;
    if (frame_len > max_len)
        frame_len = max_len;

    /* Copy received frame from DMA buffer to caller's buffer */
    if (nic->rx_bufs[idx]) {
        memcpy(buf, nic->rx_bufs[idx], frame_len);
    }

    nic->rx_packets++;
    nic->rx_bytes += frame_len;

    /* Clear and re-arm descriptor for NIC reuse */
    desc->status = 0;
    desc->length = 0;

    /* Advance RX cursor with wraparound */
    nic->rx_cur = (idx + 1) % I211_RX_RING_SIZE;

    /* Advance RDT to tell NIC this descriptor is available again */
    nic_write32(nic->mmio_base, I211_RDT, idx);

    return (int)frame_len;
}

void i211_nic_set_rx_buffer(i211_nic_t *nic, uint32_t index,
                             void *virt, uint64_t phys, uint32_t size)
{
    if (!nic || index >= I211_RX_RING_SIZE || !virt || size < I211_RX_BUF_SIZE)
        return;

    nic->rx_bufs[index] = (uint8_t *)virt;
    nic->rx_bufs_phys[index] = phys;

    if (nic->rx_ring) {
        nic->rx_ring[index].addr = phys;
    }
}

int i211_nic_link_status(i211_nic_t *nic)
{
    uint32_t status;

    if (!nic)
        return 0;

    status = nic_read32(nic->mmio_base, I211_STATUS);

    nic->link_up     = (status & I211_STATUS_LU) ? 1 : 0;
    nic->full_duplex = (status & I211_STATUS_FD) ? 1 : 0;

    switch (status & I211_STATUS_SPEED_MASK) {
    case I211_STATUS_SPEED_1000:
        nic->link_speed = 1000;
        break;
    case I211_STATUS_SPEED_100:
        nic->link_speed = 100;
        break;
    case I211_STATUS_SPEED_10:
        nic->link_speed = 10;
        break;
    default:
        nic->link_speed = 0;
        break;
    }

    return nic->link_up;
}
