/**
 * nic_rtl8168.h - Realtek RTL8168/8111 Gigabit Ethernet Driver Header (x86)
 *
 * Driver for the Realtek RTL8111H GbE NIC found on the Gigabyte B550M K
 * motherboard. The RTL8111 family presents as PCI device 10EC:8168.
 * Register-compatible with RTL8111B/C/D/E/F/G/H and RTL8168B+.
 *
 * The RTL8111/8168 uses a C+ descriptor-based DMA engine with separate
 * TX and RX descriptor rings (each 16 bytes per entry). Descriptors are
 * 256-byte aligned. The NIC supports Gigabit Ethernet, VLAN tagging,
 * hardware checksum offload, and jumbo frames up to ~9K.
 *
 * References:
 *   - RTL8111B/RTL8168B Registers DataSheet 1.0 (Realtek/FreeBSD)
 *   - OSDev Wiki: RTL8169 (register-compatible family)
 *   - Linux r8169 driver (torvalds/linux drivers/net/ethernet/realtek/)
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_NIC_RTL8168_H
#define JARVIS_NIC_RTL8168_H

#include <stdint.h>

/* ========================================================================
 * PCI Identification
 *
 * The RTL8111 family reports vendor 0x10EC (Realtek) and device 0x8168.
 * The Gigabyte B550M K uses an RTL8111H which uses this same PCI ID.
 * Some older variants may report 0x8169 or 0x8161.
 * ======================================================================== */

#define RTL_VENDOR_ID           0x10EC
#define RTL_DEVICE_ID_8168      0x8168  /* RTL8111/8168 GbE (most common) */
#define RTL_DEVICE_ID_8169      0x8169  /* RTL8169 GbE (legacy) */
#define RTL_DEVICE_ID_8161      0x8161  /* RTL8168/8111 GbE (alt ID) */

/* ========================================================================
 * MMIO Register Offsets
 *
 * Accessible via PCI BAR0 (I/O) or BAR2 (memory-mapped).
 * We use MMIO (BAR2) for all register access.
 *
 * Register map from RTL8111B/8168B datasheet + OSDev RTL8169 wiki.
 * ======================================================================== */

/* --- MAC Address (6 bytes) --- */
#define RTL_IDR0                0x00    /* MAC address bytes 0-3 (32-bit) */
#define RTL_IDR4                0x04    /* MAC address bytes 4-5 (16-bit) */

/* --- Multicast Address Filter --- */
#define RTL_MAR0                0x08    /* Multicast filter hash [31:0] */
#define RTL_MAR4                0x0C    /* Multicast filter hash [63:32] */

/* --- Descriptor Addresses --- */
#define RTL_DTCCR               0x10    /* Dump Tally Counter Command Reg */
#define RTL_TNPDS               0x20    /* TX Normal Priority Desc Start (64-bit) */
#define RTL_TNPDS_LO            0x20    /* TX Normal Priority Desc Start Low 32 */
#define RTL_TNPDS_HI            0x24    /* TX Normal Priority Desc Start High 32 */
#define RTL_THPDS               0x28    /* TX High Priority Desc Start (64-bit) */
#define RTL_THPDS_LO            0x28    /* TX High Priority Desc Start Low 32 */
#define RTL_THPDS_HI            0x2C    /* TX High Priority Desc Start High 32 */

/* --- Command and Status --- */
#define RTL_CR                  0x37    /* Command Register (8-bit) */
#define RTL_TPPOLL              0x38    /* TX Priority Polling (8-bit) */
#define RTL_IMR                 0x3C    /* Interrupt Mask Register (16-bit) */
#define RTL_ISR                 0x3E    /* Interrupt Status Register (16-bit) */

/* --- TX Configuration --- */
#define RTL_TCR                 0x40    /* TX Configuration Register (32-bit) */

/* --- RX Configuration --- */
#define RTL_RCR                 0x44    /* RX Configuration Register (32-bit) */

/* --- Timer --- */
#define RTL_TCTR                0x48    /* Timer Count Register (32-bit) */
#define RTL_TIMER_INT           0x54    /* Timer Interrupt Register (32-bit) */

/* --- Config Registers --- */
#define RTL_9346CR              0x50    /* 93C46 (EEPROM) Command Register (8-bit) */
#define RTL_CONFIG0             0x51    /* Config Register 0 (8-bit) */
#define RTL_CONFIG1             0x52    /* Config Register 1 (8-bit) */
#define RTL_CONFIG2             0x53    /* Config Register 2 (8-bit) */
#define RTL_CONFIG3             0x54    /* Config Register 3 (8-bit) */
#define RTL_CONFIG4             0x55    /* Config Register 4 (8-bit) */
#define RTL_CONFIG5             0x56    /* Config Register 5 (8-bit) */

/* --- PHY --- */
#define RTL_PHYAR               0x60    /* PHY Access Register (32-bit) */
#define RTL_PHY_STATUS          0x6C    /* PHY Status Register (8-bit) */

/* --- RX Descriptor Address --- */
#define RTL_RDSAR               0xE4    /* RX Descriptor Start Address (64-bit) */
#define RTL_RDSAR_LO            0xE4    /* RX Descriptor Start Address Low 32 */
#define RTL_RDSAR_HI            0xE8    /* RX Descriptor Start Address High 32 */

/* --- RX Max Size --- */
#define RTL_RMS                 0xDA    /* RX Max packet Size (16-bit) */

/* --- C+ Command --- */
#define RTL_CPCR                0xE0    /* C+ Command Register (16-bit) */

/* --- Misc --- */
#define RTL_MTPS                0xEC    /* Max TX Packet Size (8-bit) */

/* ========================================================================
 * Command Register (RTL_CR, offset 0x37) Bit Definitions
 * ======================================================================== */

#define RTL_CR_RST              (1 << 4)    /* Reset: 1 = software reset */
#define RTL_CR_RE               (1 << 3)    /* Receiver Enable */
#define RTL_CR_TE               (1 << 2)    /* Transmitter Enable */

/* ========================================================================
 * TX Priority Polling (RTL_TPPOLL, offset 0x38) Bit Definitions
 * ======================================================================== */

#define RTL_TPPOLL_HPQ          (1 << 7)    /* High Priority Queue polling */
#define RTL_TPPOLL_NPQ          (1 << 6)    /* Normal Priority Queue polling */

/* ========================================================================
 * Interrupt Mask/Status (RTL_IMR/RTL_ISR) Bit Definitions
 * ======================================================================== */

#define RTL_INT_ROK             (1 << 0)    /* RX OK */
#define RTL_INT_RER             (1 << 1)    /* RX Error */
#define RTL_INT_TOK             (1 << 2)    /* TX OK */
#define RTL_INT_TER             (1 << 3)    /* TX Error */
#define RTL_INT_RDU             (1 << 4)    /* RX Descriptor Unavailable */
#define RTL_INT_LINK_CHG        (1 << 5)    /* Link Change */
#define RTL_INT_FOVW            (1 << 6)    /* RX FIFO Overflow */
#define RTL_INT_TDU             (1 << 7)    /* TX Descriptor Unavailable */
#define RTL_INT_SW              (1 << 8)    /* Software Interrupt */
#define RTL_INT_TIMEOUT         (1 << 14)   /* Timer expired */
#define RTL_INT_SERR            (1 << 15)   /* System Error */

/* ========================================================================
 * TX Configuration Register (RTL_TCR, offset 0x40) Bit Definitions
 * ======================================================================== */

#define RTL_TCR_IFG_SHIFT       24          /* Inter Frame Gap (bits 25:24) */
#define RTL_TCR_IFG_STD         (3 << 24)   /* Standard IFG (96 bit times) */
#define RTL_TCR_MXDMA_SHIFT     8           /* Max DMA burst (bits 10:8) */
#define RTL_TCR_MXDMA_UNLIM     (7 << 8)    /* Unlimited DMA burst */

/* ========================================================================
 * RX Configuration Register (RTL_RCR, offset 0x44) Bit Definitions
 * ======================================================================== */

#define RTL_RCR_AAP             (1 << 0)    /* Accept All Packets (promisc) */
#define RTL_RCR_APM             (1 << 1)    /* Accept Physical Match */
#define RTL_RCR_AM              (1 << 2)    /* Accept Multicast */
#define RTL_RCR_AB              (1 << 3)    /* Accept Broadcast */
#define RTL_RCR_AR              (1 << 4)    /* Accept Runt (<64 bytes) */
#define RTL_RCR_AER             (1 << 5)    /* Accept Error packets */
#define RTL_RCR_MXDMA_SHIFT     8           /* Max DMA burst (bits 10:8) */
#define RTL_RCR_MXDMA_UNLIM     (7 << 8)    /* Unlimited DMA burst */
#define RTL_RCR_RXFTH_SHIFT     13          /* RX FIFO threshold (bits 15:13) */
#define RTL_RCR_RXFTH_UNLIM     (7 << 13)   /* No FIFO threshold */

/* ========================================================================
 * C+ Command Register (RTL_CPCR, offset 0xE0) Bit Definitions
 * ======================================================================== */

#define RTL_CPCR_RXVLAN         (1 << 6)    /* RX VLAN de-tag enable */
#define RTL_CPCR_RXCHKSUM       (1 << 5)    /* RX Checksum offload enable */
#define RTL_CPCR_PCIMRW         (1 << 3)    /* PCI Multi-R/W enable */

/* ========================================================================
 * PHY Status Register (RTL_PHY_STATUS, offset 0x6C) Bit Definitions
 * ======================================================================== */

#define RTL_PHY_LINK_UP         (1 << 1)    /* Link is up */
#define RTL_PHY_SPEED_10        (1 << 2)    /* 10 Mbps */
#define RTL_PHY_SPEED_100       (1 << 3)    /* 100 Mbps */
#define RTL_PHY_SPEED_1000      (1 << 4)    /* 1000 Mbps (Gigabit) */
#define RTL_PHY_FULL_DUPLEX     (1 << 0)    /* Full duplex */
#define RTL_PHY_TX_FLOW         (1 << 6)    /* TX flow control */
#define RTL_PHY_RX_FLOW         (1 << 7)    /* RX flow control */

/* ========================================================================
 * PHY Access Register (RTL_PHYAR, offset 0x60) Bit Definitions
 * ======================================================================== */

#define RTL_PHYAR_FLAG          (1U << 31)  /* 1=write, 0=read; cleared on read done */
#define RTL_PHYAR_REG_SHIFT     16          /* PHY register address (bits 20:16) */
#define RTL_PHYAR_DATA_MASK     0xFFFF      /* PHY register data (bits 15:0) */

/* ========================================================================
 * 93C46 Command Register (RTL_9346CR, offset 0x50) Bit Definitions
 * Used to unlock config registers for writing.
 * ======================================================================== */

#define RTL_9346CR_LOCK         0x00    /* Lock config registers */
#define RTL_9346CR_UNLOCK       0xC0    /* Unlock config registers */

/* ========================================================================
 * TX Descriptor (16 bytes)
 *
 * Descriptor ring must be 256-byte aligned. Each entry is 16 bytes.
 * The OWN bit (bit 31 of opts1) indicates NIC ownership.
 * The EOR bit (bit 30 of opts1) marks the last descriptor in the ring.
 * ======================================================================== */

/* TX descriptor opts1 (command/status) bits */
#define RTL_TX_OWN              (1U << 31)  /* NIC owns this descriptor */
#define RTL_TX_EOR              (1U << 30)  /* End Of Ring */
#define RTL_TX_FS               (1U << 29)  /* First Segment of packet */
#define RTL_TX_LS               (1U << 28)  /* Last Segment of packet */
#define RTL_TX_LGSEN            (1U << 27)  /* Large Send (TSO) enable */
#define RTL_TX_IPCS             (1U << 18)  /* IP Checksum offload */
#define RTL_TX_UDPCS            (1U << 17)  /* UDP Checksum offload */
#define RTL_TX_TCPCS            (1U << 16)  /* TCP Checksum offload */
#define RTL_TX_LEN_MASK         0x3FFF      /* Bits 13:0 = frame length */

typedef struct {
    uint32_t opts1;         /* OWN | EOR | FS | LS | length */
    uint32_t opts2;         /* VLAN tag, checksum offload flags */
    uint32_t addr_lo;       /* Buffer physical address (low 32) */
    uint32_t addr_hi;       /* Buffer physical address (high 32) */
} __attribute__((packed)) rtl_tx_desc_t;

/* ========================================================================
 * RX Descriptor (16 bytes)
 *
 * Same layout as TX. OWN=1 means NIC can use the buffer.
 * After receiving, NIC clears OWN and writes frame length into opts1.
 * ======================================================================== */

/* RX descriptor opts1 (status) bits */
#define RTL_RX_OWN              (1U << 31)  /* NIC owns this descriptor */
#define RTL_RX_EOR              (1U << 30)  /* End Of Ring */
#define RTL_RX_FS               (1U << 29)  /* First Segment */
#define RTL_RX_LS               (1U << 28)  /* Last Segment */
#define RTL_RX_MAR              (1U << 26)  /* Multicast Address Received */
#define RTL_RX_PAM              (1U << 25)  /* Physical Address Matched */
#define RTL_RX_BAR              (1U << 24)  /* Broadcast Address Received */
#define RTL_RX_RWT              (1U << 22)  /* RX Watchdog Timer expired */
#define RTL_RX_RES              (1U << 21)  /* RX Error Summary */
#define RTL_RX_RUNT             (1U << 20)  /* Runt packet */
#define RTL_RX_CRC              (1U << 19)  /* CRC error */
#define RTL_RX_LEN_MASK         0x3FFF      /* Bits 13:0 = frame length */

typedef struct {
    uint32_t opts1;         /* OWN | EOR | FS | LS | status | length */
    uint32_t opts2;         /* VLAN tag, checksum status */
    uint32_t addr_lo;       /* Buffer physical address (low 32) */
    uint32_t addr_hi;       /* Buffer physical address (high 32) */
} __attribute__((packed)) rtl_rx_desc_t;

/* ========================================================================
 * Driver Constants
 * ======================================================================== */

#define RTL_TX_RING_SIZE        256     /* Number of TX descriptors */
#define RTL_RX_RING_SIZE        256     /* Number of RX descriptors */
#define RTL_DESC_ALIGN          256     /* Descriptor ring alignment */
#define RTL_RX_BUF_SIZE         2048    /* Per-RX-buffer size */
#define RTL_TX_BUF_SIZE         2048    /* Per-TX-buffer size */
#define RTL_MAX_MTU             1500    /* Standard Ethernet MTU */
#define RTL_MAX_FRAME_SIZE      1536    /* MTU + headers + padding */

#define RTL_RESET_TIMEOUT_US    100000  /* 100ms reset timeout */

/* ========================================================================
 * Driver State
 * ======================================================================== */

typedef struct {
    uint64_t        mmio_base;          /* MMIO base (from PCI BAR2) */

    /* MAC address */
    uint8_t         mac[6];

    /* TX ring */
    rtl_tx_desc_t  *tx_ring;           /* 256-entry TX descriptor ring */
    uint32_t        tx_head;           /* Next descriptor to fill */
    uint32_t        tx_tail;           /* Next descriptor to reclaim */
    uint64_t        tx_ring_phys;      /* Physical address of TX ring */

    /* RX ring */
    rtl_rx_desc_t  *rx_ring;           /* 256-entry RX descriptor ring */
    uint32_t        rx_cur;            /* Next descriptor to check */
    uint64_t        rx_ring_phys;      /* Physical address of RX ring */

    /* RX buffer pool — one buffer per descriptor */
    uint8_t        *rx_bufs[RTL_RX_RING_SIZE];      /* Virtual addresses (for memcpy) */
    uint64_t        rx_bufs_phys[RTL_RX_RING_SIZE];  /* Physical addresses (for DMA) */

    /* Link state */
    int             link_up;           /* 1 if link is up */
    uint32_t        link_speed;        /* 10, 100, or 1000 Mbps */
    int             full_duplex;       /* 1 if full duplex */

    /* Statistics */
    uint64_t        tx_packets;
    uint64_t        rx_packets;
    uint64_t        tx_bytes;
    uint64_t        rx_bytes;
    uint64_t        tx_errors;
    uint64_t        rx_errors;
} rtl_nic_t;

/* ========================================================================
 * Function Prototypes
 * ======================================================================== */

/**
 * rtl_nic_init - Initialize the RTL8168/8111 NIC from MMIO base
 * @nic:       Driver state to initialize
 * @mmio_base: Physical MMIO base address (PCI BAR2, memory-mapped)
 *
 * Performs software reset, reads MAC address, initializes TX/RX descriptor
 * rings, configures DMA, and enables the transmitter and receiver.
 * Returns 0 on success, -1 on error (e.g., reset timeout).
 */
int rtl_nic_init(rtl_nic_t *nic, uint64_t mmio_base);

/**
 * rtl_nic_probe_pci - Find and initialize RTL8168 via PCI enumeration
 * @nic: Driver state to fill
 *
 * Uses pci_scan() to find a Realtek 10EC:8168 device, enables bus mastering,
 * extracts the MMIO base from BAR2, and calls rtl_nic_init().
 * Returns 0 on success, -1 if device not found.
 */
int rtl_nic_probe_pci(rtl_nic_t *nic);

/**
 * rtl_nic_get_mac - Read the 6-byte MAC address from hardware
 * @nic: Initialized driver state
 * @mac: Output buffer (6 bytes)
 */
void rtl_nic_get_mac(rtl_nic_t *nic, uint8_t mac[6]);

/**
 * rtl_nic_send - Transmit a frame
 * @nic: Initialized driver state
 * @buf: Frame data (Ethernet header + payload)
 * @len: Frame length in bytes (14..1536)
 *
 * Builds a TX descriptor with OWN, FS, LS bits set, writes the buffer
 * address, and kicks the TX poll register. Advances tx_head.
 * Returns 0 on success, -1 if ring full or len invalid.
 */
int rtl_nic_send(rtl_nic_t *nic, const void *buf, uint32_t len);

/**
 * rtl_nic_recv - Receive a frame (polled)
 * @nic:     Initialized driver state
 * @buf:     Output buffer for received frame
 * @max_len: Size of output buffer
 *
 * Checks the current RX descriptor. If OWN is clear (NIC has written a
 * frame), copies data into buf, re-arms the descriptor, and advances rx_cur.
 * Returns frame length on success, 0 if no frame available, -1 on error.
 */
int rtl_nic_recv(rtl_nic_t *nic, void *buf, uint32_t max_len);

/**
 * rtl_nic_set_rx_buffer - Assign a DMA buffer to an RX descriptor
 * @nic:   NIC state
 * @index: Descriptor index (0 to RTL_RX_RING_SIZE-1)
 * @virt:  Virtual address of the buffer (for memcpy to caller)
 * @phys:  Physical address (for DMA — written to descriptor addr_lo/addr_hi)
 * @size:  Buffer size (must be >= RTL_RX_BUF_SIZE)
 */
void rtl_nic_set_rx_buffer(rtl_nic_t *nic, uint32_t index,
                            void *virt, uint64_t phys, uint32_t size);

/**
 * rtl_nic_link_status - Read current link status from PHY
 * @nic: Initialized driver state
 *
 * Reads RTL_PHY_STATUS register. Updates nic->link_up, link_speed,
 * full_duplex fields.
 * Returns 1 if link is up, 0 if down.
 */
int rtl_nic_link_status(rtl_nic_t *nic);

/**
 * rtl_nic_enable_interrupts - Enable NIC interrupt sources
 * @nic:  Initialized driver state
 * @mask: Bitmask of RTL_INT_* flags to enable
 */
void rtl_nic_enable_interrupts(rtl_nic_t *nic, uint16_t mask);

/**
 * rtl_nic_disable_interrupts - Disable all NIC interrupts
 * @nic: Initialized driver state
 */
void rtl_nic_disable_interrupts(rtl_nic_t *nic);

/**
 * rtl_nic_handle_interrupt - Read and acknowledge pending interrupts
 * @nic: Initialized driver state
 *
 * Reads ISR, writes back to acknowledge. Returns the ISR bitmask.
 */
uint16_t rtl_nic_handle_interrupt(rtl_nic_t *nic);

/**
 * rtl_nic_print_info - Print NIC status information
 * @nic: Initialized driver state
 */
void rtl_nic_print_info(rtl_nic_t *nic);

#endif /* JARVIS_NIC_RTL8168_H */
