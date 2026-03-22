/**
 * nic_rtl8168.c - Realtek RTL8168/8111 Gigabit Ethernet Driver (x86-64)
 *
 * Implements init, TX/RX via C+ descriptor rings, link status, and
 * PCI probe for the Realtek RTL8111H NIC (Gigabyte B550M K motherboard).
 *
 * The RTL8111/8168 uses 16-byte TX/RX descriptors in 256-byte aligned
 * rings. Each descriptor has an OWN bit (bit 31) indicating NIC ownership,
 * and an EOR bit (bit 30) marking the end of the ring for wraparound.
 *
 * References:
 *   - RTL8111B/RTL8168B Registers DataSheet 1.0 (Realtek/FreeBSD)
 *   - OSDev Wiki: RTL8169
 *   - Linux r8169 driver
 *
 * JARVIS AI-OS - Phase 3
 */

#include "nic_rtl8168.h"
#include "pci.h"
#include <stdio.h>
#include <string.h>

/* ========================================================================
 * MMIO Access
 * ======================================================================== */

#ifndef JARVIS_TEST_MOCK

static inline uint8_t mmio_read8(volatile void *addr)
{
    return *(volatile uint8_t *)addr;
}

static inline uint16_t mmio_read16(volatile void *addr)
{
    return *(volatile uint16_t *)addr;
}

static inline uint32_t mmio_read32(volatile void *addr)
{
    return *(volatile uint32_t *)addr;
}

static inline void mmio_write8(volatile void *addr, uint8_t val)
{
    *(volatile uint8_t *)addr = val;
}

static inline void mmio_write16(volatile void *addr, uint16_t val)
{
    *(volatile uint16_t *)addr = val;
}

static inline void mmio_write32(volatile void *addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
}

#else
/* Mock MMIO for testing -- defined in test file */
extern uint8_t  mmio_read8(volatile void *addr);
extern uint16_t mmio_read16(volatile void *addr);
extern uint32_t mmio_read32(volatile void *addr);
extern void     mmio_write8(volatile void *addr, uint8_t val);
extern void     mmio_write16(volatile void *addr, uint16_t val);
extern void     mmio_write32(volatile void *addr, uint32_t val);
#endif

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

static uint8_t nic_read8(rtl_nic_t *nic, uint32_t offset)
{
    return mmio_read8((volatile void *)(uintptr_t)(nic->mmio_base + offset));
}

static uint16_t nic_read16(rtl_nic_t *nic, uint32_t offset)
{
    return mmio_read16((volatile void *)(uintptr_t)(nic->mmio_base + offset));
}

static uint32_t nic_read32(rtl_nic_t *nic, uint32_t offset)
{
    return mmio_read32((volatile void *)(uintptr_t)(nic->mmio_base + offset));
}

static void nic_write8(rtl_nic_t *nic, uint32_t offset, uint8_t val)
{
    mmio_write8((volatile void *)(uintptr_t)(nic->mmio_base + offset), val);
}

static void nic_write16(rtl_nic_t *nic, uint32_t offset, uint16_t val)
{
    mmio_write16((volatile void *)(uintptr_t)(nic->mmio_base + offset), val);
}

static void nic_write32(rtl_nic_t *nic, uint32_t offset, uint32_t val)
{
    mmio_write32((volatile void *)(uintptr_t)(nic->mmio_base + offset), val);
}

/* ========================================================================
 * Software Reset
 * ======================================================================== */

/**
 * Perform a software reset and wait for completion.
 * The RST bit in CR auto-clears when reset is complete.
 * Returns 0 on success, -1 on timeout.
 */
static int rtl_software_reset(rtl_nic_t *nic)
{
    int timeout;

    /* Set RST bit in Command Register */
    nic_write8(nic, RTL_CR, RTL_CR_RST);

    /* Wait for RST bit to auto-clear (hardware signals reset complete) */
    timeout = RTL_RESET_TIMEOUT_US / 10;
    while (timeout > 0) {
        if (!(nic_read8(nic, RTL_CR) & RTL_CR_RST))
            return 0;
        timeout--;
    }

    return -1;  /* Reset timed out */
}

/* ========================================================================
 * MAC Address
 * ======================================================================== */

/**
 * Read the hardware MAC address from IDR0/IDR4 registers.
 */
static void rtl_read_mac(rtl_nic_t *nic)
{
    uint32_t lo = nic_read32(nic, RTL_IDR0);
    uint16_t hi = nic_read16(nic, RTL_IDR4);

    nic->mac[0] = (uint8_t)(lo & 0xFF);
    nic->mac[1] = (uint8_t)((lo >> 8) & 0xFF);
    nic->mac[2] = (uint8_t)((lo >> 16) & 0xFF);
    nic->mac[3] = (uint8_t)((lo >> 24) & 0xFF);
    nic->mac[4] = (uint8_t)(hi & 0xFF);
    nic->mac[5] = (uint8_t)((hi >> 8) & 0xFF);
}

/* ========================================================================
 * Descriptor Ring Setup
 * ======================================================================== */

/**
 * Initialize the TX descriptor ring. Mark all descriptors as host-owned
 * (OWN=0). Set the EOR bit on the last descriptor for ring wraparound.
 */
static void rtl_init_tx_ring(rtl_nic_t *nic)
{
    if (!nic->tx_ring)
        return;

    memset(nic->tx_ring, 0, sizeof(rtl_tx_desc_t) * RTL_TX_RING_SIZE);

    /* Mark the last descriptor with EOR for ring wrap */
    nic->tx_ring[RTL_TX_RING_SIZE - 1].opts1 = RTL_TX_EOR;

    nic->tx_head = 0;
    nic->tx_tail = 0;

    /* Program TX descriptor ring address into NIC */
    nic_write32(nic, RTL_TNPDS_LO, (uint32_t)(nic->tx_ring_phys & 0xFFFFFFFF));
    nic_write32(nic, RTL_TNPDS_HI, (uint32_t)(nic->tx_ring_phys >> 32));
}

/**
 * Initialize the RX descriptor ring. Set OWN=1 on all descriptors so the
 * NIC can fill them. Set EOR on the last descriptor.
 * Each descriptor points to a pre-allocated RX buffer.
 */
static void rtl_init_rx_ring(rtl_nic_t *nic)
{
    uint32_t i;

    if (!nic->rx_ring)
        return;

    memset(nic->rx_ring, 0, sizeof(rtl_rx_desc_t) * RTL_RX_RING_SIZE);

    for (i = 0; i < RTL_RX_RING_SIZE; i++) {
        uint32_t flags = RTL_RX_OWN | (RTL_RX_BUF_SIZE & RTL_RX_LEN_MASK);

        /* Last descriptor gets EOR bit for ring wrap */
        if (i == RTL_RX_RING_SIZE - 1)
            flags |= RTL_RX_EOR;

        nic->rx_ring[i].opts1 = flags;
        /* Buffer addresses would be set by the caller who allocates
         * the DMA buffers. For now, addr_lo/addr_hi remain 0. */
    }

    nic->rx_cur = 0;

    /* Program RX descriptor ring address into NIC */
    nic_write32(nic, RTL_RDSAR_LO, (uint32_t)(nic->rx_ring_phys & 0xFFFFFFFF));
    nic_write32(nic, RTL_RDSAR_HI, (uint32_t)(nic->rx_ring_phys >> 32));
}

/* ========================================================================
 * Public API
 * ======================================================================== */

int rtl_nic_init(rtl_nic_t *nic, uint64_t mmio_base)
{
    rtl_tx_desc_t *saved_tx_ring;
    rtl_rx_desc_t *saved_rx_ring;
    uint64_t saved_tx_phys, saved_rx_phys;

    if (!nic)
        return -1;

    /* Preserve caller-supplied ring pointers across the zero-init */
    saved_tx_ring = nic->tx_ring;
    saved_rx_ring = nic->rx_ring;
    saved_tx_phys = nic->tx_ring_phys;
    saved_rx_phys = nic->rx_ring_phys;

    memset(nic, 0, sizeof(*nic));
    nic->mmio_base     = mmio_base;
    nic->tx_ring       = saved_tx_ring;
    nic->rx_ring       = saved_rx_ring;
    nic->tx_ring_phys  = saved_tx_phys;
    nic->rx_ring_phys  = saved_rx_phys;

    /* 1. Software reset */
    if (rtl_software_reset(nic) < 0)
        return -1;

    /* 2. Read MAC address from hardware registers */
    rtl_read_mac(nic);

    /* 3. Unlock config registers (needed for some writes) */
    nic_write8(nic, RTL_9346CR, RTL_9346CR_UNLOCK);

    /* 4. Enable C+ mode features: RX checksum, PCI multi-R/W */
    nic_write16(nic, RTL_CPCR, RTL_CPCR_RXCHKSUM | RTL_CPCR_PCIMRW);

    /* 5. Set RX max packet size */
    nic_write16(nic, RTL_RMS, RTL_MAX_FRAME_SIZE);

    /* 6. Set max TX packet size (in 128-byte units) */
    nic_write8(nic, RTL_MTPS, RTL_MAX_FRAME_SIZE / 128);

    /* 7. Configure TX: standard IFG, unlimited DMA burst */
    nic_write32(nic, RTL_TCR, RTL_TCR_IFG_STD | RTL_TCR_MXDMA_UNLIM);

    /* 8. Configure RX: accept broadcast + unicast, unlimited DMA burst */
    nic_write32(nic, RTL_RCR, RTL_RCR_APM | RTL_RCR_AB |
                               RTL_RCR_MXDMA_UNLIM | RTL_RCR_RXFTH_UNLIM);

    /* 9. Initialize TX and RX descriptor rings */
    rtl_init_tx_ring(nic);
    rtl_init_rx_ring(nic);

    /* 10. Lock config registers */
    nic_write8(nic, RTL_9346CR, RTL_9346CR_LOCK);

    /* 11. Disable all interrupts initially (polled mode) */
    nic_write16(nic, RTL_IMR, 0x0000);

    /* 12. Clear any pending interrupts */
    nic_write16(nic, RTL_ISR, 0xFFFF);

    /* 13. Enable TX and RX in command register */
    nic_write8(nic, RTL_CR, RTL_CR_TE | RTL_CR_RE);

    /* 14. Read initial link status */
    rtl_nic_link_status(nic);

    return 0;
}

int rtl_nic_probe_pci(rtl_nic_t *nic)
{
    pci_device_t devices[32];
    int count, i;
    uint32_t bar_addr;

    if (!nic)
        return -1;

    /* Scan PCI bus for devices */
    count = pci_scan(devices, 32);

    /* Look for Realtek 10EC:8168 */
    for (i = 0; i < count; i++) {
        if (devices[i].vendor_id == RTL_VENDOR_ID &&
            devices[i].device_id == RTL_DEVICE_ID_8168) {

            /* Enable bus mastering for DMA */
            pci_enable_bus_master(&devices[i]);

            /* Get MMIO base from BAR2 (memory-mapped) */
            if (pci_is_bar_mmio(&devices[i], 2)) {
                bar_addr = pci_get_bar_address(&devices[i], 2);
            } else if (pci_is_bar_mmio(&devices[i], 0)) {
                /* Some variants use BAR0 for MMIO */
                bar_addr = pci_get_bar_address(&devices[i], 0);
            } else {
                return -1;  /* No MMIO BAR found */
            }

            return rtl_nic_init(nic, (uint64_t)bar_addr);
        }
    }

    return -1;  /* Device not found */
}

void rtl_nic_get_mac(rtl_nic_t *nic, uint8_t mac[6])
{
    if (!nic || !mac)
        return;

    memcpy(mac, nic->mac, 6);
}

int rtl_nic_send(rtl_nic_t *nic, const void *buf, uint32_t len)
{
    rtl_tx_desc_t *desc;
    uint32_t idx;

    if (!nic || !buf || len == 0 || len > RTL_MAX_FRAME_SIZE)
        return -1;

    idx = nic->tx_head;
    desc = &nic->tx_ring[idx];

    /* Check if descriptor is available (OWN must be 0 = host owns it) */
    if (desc->opts1 & RTL_TX_OWN)
        return -1;  /* Ring full */

    /* Build descriptor:
     *  - OWN=1 (give to NIC)
     *  - FS=1 (first segment)
     *  - LS=1 (last segment, single-buffer frame)
     *  - Length in bits 13:0
     *  - Preserve EOR bit if this is the last descriptor in the ring
     */
    desc->opts1 = RTL_TX_OWN | RTL_TX_FS | RTL_TX_LS |
                  (len & RTL_TX_LEN_MASK);

    /* Preserve EOR bit on the last ring entry */
    if (idx == RTL_TX_RING_SIZE - 1)
        desc->opts1 |= RTL_TX_EOR;

    desc->opts2 = 0;  /* No VLAN tag or checksum offload */

    /* In a real driver, addr_lo/addr_hi would be the DMA-mapped physical
     * address of the packet buffer. For skeleton purposes, we store the
     * virtual address truncated to 32-bit (real driver needs proper DMA). */
    desc->addr_lo = (uint32_t)(uintptr_t)buf;
    desc->addr_hi = (uint32_t)((uintptr_t)buf >> 32);

    /* Advance TX head with wraparound */
    nic->tx_head = (idx + 1) % RTL_TX_RING_SIZE;

    /* Kick the NIC to poll the TX descriptor ring */
    nic_write8(nic, RTL_TPPOLL, RTL_TPPOLL_NPQ);

    nic->tx_packets++;
    nic->tx_bytes += len;

    return 0;
}

int rtl_nic_recv(rtl_nic_t *nic, void *buf, uint32_t max_len)
{
    rtl_rx_desc_t *desc;
    uint32_t idx, frame_len;

    if (!nic || !buf || max_len == 0)
        return -1;

    idx = nic->rx_cur;
    desc = &nic->rx_ring[idx];

    /* Check if NIC has released this descriptor (OWN=0 means host owns) */
    if (desc->opts1 & RTL_RX_OWN)
        return 0;  /* No frame available */

    /* Check for RX errors */
    if (desc->opts1 & RTL_RX_RES) {
        nic->rx_errors++;

        /* Re-arm descriptor: set OWN=1, buffer size, preserve EOR */
        desc->opts1 = RTL_RX_OWN | (RTL_RX_BUF_SIZE & RTL_RX_LEN_MASK);
        if (idx == RTL_RX_RING_SIZE - 1)
            desc->opts1 |= RTL_RX_EOR;

        nic->rx_cur = (idx + 1) % RTL_RX_RING_SIZE;
        return -1;  /* Error frame */
    }

    /* Extract frame length (bits 13:0), subtract 4-byte FCS */
    frame_len = (desc->opts1 & RTL_RX_LEN_MASK);
    if (frame_len >= 4)
        frame_len -= 4;  /* Strip FCS */

    /* Clamp to output buffer size */
    if (frame_len > max_len)
        frame_len = max_len;

    /* In a real driver, we would DMA-unmap and copy from the physical
     * RX buffer. For skeleton, the test mock will set up the buffer. */
    /* memcpy(buf, rx_buffer_for_desc[idx], frame_len); */
    (void)buf;  /* Suppresses unused warning in skeleton */

    nic->rx_packets++;
    nic->rx_bytes += frame_len;

    /* Re-arm descriptor: set OWN=1 so NIC can reuse it */
    desc->opts1 = RTL_RX_OWN | (RTL_RX_BUF_SIZE & RTL_RX_LEN_MASK);
    if (idx == RTL_RX_RING_SIZE - 1)
        desc->opts1 |= RTL_RX_EOR;

    /* Advance RX cursor with wraparound */
    nic->rx_cur = (idx + 1) % RTL_RX_RING_SIZE;

    return (int)frame_len;
}

int rtl_nic_link_status(rtl_nic_t *nic)
{
    uint8_t phy;

    if (!nic)
        return 0;

    phy = nic_read8(nic, RTL_PHY_STATUS);

    nic->link_up     = (phy & RTL_PHY_LINK_UP) ? 1 : 0;
    nic->full_duplex = (phy & RTL_PHY_FULL_DUPLEX) ? 1 : 0;

    if (phy & RTL_PHY_SPEED_1000)
        nic->link_speed = 1000;
    else if (phy & RTL_PHY_SPEED_100)
        nic->link_speed = 100;
    else if (phy & RTL_PHY_SPEED_10)
        nic->link_speed = 10;
    else
        nic->link_speed = 0;

    return nic->link_up;
}

void rtl_nic_enable_interrupts(rtl_nic_t *nic, uint16_t mask)
{
    if (!nic)
        return;
    nic_write16(nic, RTL_IMR, mask);
}

void rtl_nic_disable_interrupts(rtl_nic_t *nic)
{
    if (!nic)
        return;
    nic_write16(nic, RTL_IMR, 0x0000);
}

uint16_t rtl_nic_handle_interrupt(rtl_nic_t *nic)
{
    uint16_t status;

    if (!nic)
        return 0;

    /* Read ISR */
    status = nic_read16(nic, RTL_ISR);

    /* Acknowledge by writing back */
    if (status)
        nic_write16(nic, RTL_ISR, status);

    return status;
}

void rtl_nic_print_info(rtl_nic_t *nic)
{
    if (!nic)
        return;

    printf("RTL8168/8111 NIC Info:\n");
    printf("  MMIO Base: 0x%lX\n", (unsigned long)nic->mmio_base);
    printf("  MAC:       %02X:%02X:%02X:%02X:%02X:%02X\n",
           nic->mac[0], nic->mac[1], nic->mac[2],
           nic->mac[3], nic->mac[4], nic->mac[5]);
    printf("  Link:      %s\n", nic->link_up ? "UP" : "DOWN");
    if (nic->link_up) {
        printf("  Speed:     %u Mbps %s\n", nic->link_speed,
               nic->full_duplex ? "Full-Duplex" : "Half-Duplex");
    }
    printf("  TX Ring:   head=%u tail=%u\n", nic->tx_head, nic->tx_tail);
    printf("  RX Ring:   cur=%u\n", nic->rx_cur);
    printf("  Stats:     TX %lu pkts / %lu bytes, RX %lu pkts / %lu bytes\n",
           (unsigned long)nic->tx_packets, (unsigned long)nic->tx_bytes,
           (unsigned long)nic->rx_packets, (unsigned long)nic->rx_bytes);
    printf("  Errors:    TX %lu, RX %lu\n",
           (unsigned long)nic->tx_errors, (unsigned long)nic->rx_errors);
}
