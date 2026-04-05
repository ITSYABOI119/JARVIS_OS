/**
 * nic_i211.h - Intel I211-AT Gigabit Ethernet Driver Header (x86)
 *
 * Driver for the Intel I211-AT GbE NIC found on the ASUS X470-F Gaming
 * motherboard. The I211-AT presents as PCI device 8086:1539.
 *
 * The I211 uses legacy 16-byte TX/RX descriptors with separate descriptor
 * rings. The register layout differs significantly from classic e1000/82540EM:
 *   - RX descriptors at 0xC000 (not 0x2800)
 *   - TX descriptors at 0xE000 (not 0x3800)
 *   - Interrupts at 0x1500 (not 0x00C0)
 *
 * References:
 *   - Intel I210/I211 Datasheet (Section 8: Register Descriptions)
 *   - Linux igb driver (torvalds/linux drivers/net/ethernet/intel/igb/)
 *   - OSDev Wiki: Intel Ethernet I217
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_NIC_I211_H
#define JARVIS_NIC_I211_H

#include <stdint.h>

/* ========================================================================
 * PCI Identification
 *
 * The I211-AT reports vendor 0x8086 (Intel) and device 0x1539.
 * The ASUS X470-F Gaming uses this NIC (verified via lspci).
 * ======================================================================== */

#define I211_VENDOR_ID          0x8086
#define I211_DEVICE_ID          0x1539  /* I211-AT Gigabit Ethernet */

/* ========================================================================
 * MMIO Register Offsets
 *
 * Accessible via PCI BAR0 (memory-mapped).
 *
 * CRITICAL: I211 uses DIFFERENT register addresses from classic e1000/82540EM.
 * The RX/TX descriptor registers are in the 0xC000/0xE000 range, and
 * interrupt registers are in the 0x1500 range.
 * ======================================================================== */

/* --- General Control/Status --- */
#define I211_CTRL               0x0000  /* Device Control Register (32-bit) */
#define I211_STATUS             0x0008  /* Device Status Register (32-bit) */

/* --- Receive Control --- */
#define I211_RCTL               0x0100  /* Receive Control Register (32-bit) */

/* --- Transmit Control --- */
#define I211_TCTL               0x0400  /* Transmit Control Register (32-bit) */

/* --- Interrupt Registers (I210/I211 layout) --- */
#define I211_ICR                0x1500  /* Interrupt Cause Read (32-bit) */
#define I211_ICS                0x1504  /* Interrupt Cause Set (32-bit) */
#define I211_IMS                0x1508  /* Interrupt Mask Set/Read (32-bit) */
#define I211_IMC                0x150C  /* Interrupt Mask Clear (32-bit) */

/* --- Receive Address (MAC) --- */
#define I211_RAL0               0x5400  /* Receive Address Low 0 (32-bit) */
#define I211_RAH0               0x5404  /* Receive Address High 0 (32-bit) */

/* --- RX Descriptor Ring (Queue 0) --- */
#define I211_RDBAL              0xC000  /* RX Desc Base Address Low (32-bit) */
#define I211_RDBAH              0xC004  /* RX Desc Base Address High (32-bit) */
#define I211_RDLEN              0xC008  /* RX Desc Length (32-bit, bytes) */
#define I211_SRRCTL             0xC00C  /* Split/Replication RX Ctrl (32-bit) */
#define I211_RDH                0xC010  /* RX Desc Head (32-bit) */
#define I211_RDT                0xC018  /* RX Desc Tail (32-bit) */
#define I211_RXDCTL             0xC028  /* RX Desc Control (32-bit) */

/* --- TX Descriptor Ring (Queue 0) --- */
#define I211_TDBAL              0xE000  /* TX Desc Base Address Low (32-bit) */
#define I211_TDBAH              0xE004  /* TX Desc Base Address High (32-bit) */
#define I211_TDLEN              0xE008  /* TX Desc Length (32-bit, bytes) */
#define I211_TDH                0xE010  /* TX Desc Head (32-bit) */
#define I211_TDT                0xE018  /* TX Desc Tail (32-bit) */
#define I211_TXDCTL             0xE028  /* TX Desc Control (32-bit) */

/* ========================================================================
 * Device Control Register (I211_CTRL, offset 0x0000) Bit Definitions
 * ======================================================================== */

#define I211_CTRL_FD            (1U << 0)   /* Full Duplex */
#define I211_CTRL_ASDE          (1U << 5)   /* Auto-Speed Detection Enable */
#define I211_CTRL_SLU           (1U << 6)   /* Set Link Up */
#define I211_CTRL_RST           (1U << 26)  /* Device Reset */

/* ========================================================================
 * Device Status Register (I211_STATUS, offset 0x0008) Bit Definitions
 * ======================================================================== */

#define I211_STATUS_FD          (1U << 0)   /* Full Duplex indication */
#define I211_STATUS_LU          (1U << 1)   /* Link Up indication */
#define I211_STATUS_SPEED_MASK  (3U << 6)   /* Speed indication (bits 7:6) */
#define I211_STATUS_SPEED_10    (0U << 6)   /* 10 Mbps */
#define I211_STATUS_SPEED_100   (1U << 6)   /* 100 Mbps */
#define I211_STATUS_SPEED_1000  (2U << 6)   /* 1000 Mbps */

/* ========================================================================
 * Receive Control Register (I211_RCTL, offset 0x0100) Bit Definitions
 * ======================================================================== */

#define I211_RCTL_EN            (1U << 1)   /* Receiver Enable */
#define I211_RCTL_BAM           (1U << 15)  /* Broadcast Accept Mode */
#define I211_RCTL_BSIZE_2K      (0U << 16)  /* Buffer Size 2048 bytes */
#define I211_RCTL_SECRC         (1U << 26)  /* Strip Ethernet CRC from frame */

/* ========================================================================
 * Transmit Control Register (I211_TCTL, offset 0x0400) Bit Definitions
 * ======================================================================== */

#define I211_TCTL_EN            (1U << 1)   /* Transmitter Enable */
#define I211_TCTL_PSP           (1U << 3)   /* Pad Short Packets */
#define I211_TCTL_CT_SHIFT      4           /* Collision Threshold (bits 11:4) */
#define I211_TCTL_COLD_SHIFT    12          /* Collision Distance (bits 21:12) */

/* ========================================================================
 * Legacy TX Descriptor (16 bytes)
 *
 * The I211 supports legacy descriptors for simple DMA operation.
 * Each descriptor points to a buffer and includes command/status fields.
 * ======================================================================== */

/* TX command byte bits */
#define I211_TX_CMD_EOP         (1U << 0)   /* End Of Packet */
#define I211_TX_CMD_IFCS        (1U << 1)   /* Insert FCS (CRC) */
#define I211_TX_CMD_RS          (1U << 3)   /* Report Status */

/* TX status byte bits */
#define I211_TX_STA_DD          (1U << 0)   /* Descriptor Done */

typedef struct {
    uint64_t addr;              /* Buffer physical address */
    uint16_t length;            /* Data length in bytes */
    uint8_t  cso;               /* Checksum Offset */
    uint8_t  cmd;               /* Command (EOP, IFCS, RS) */
    uint8_t  sta;               /* Status (DD) */
    uint8_t  css;               /* Checksum Start */
    uint16_t special;           /* Special / VLAN tag */
} __attribute__((packed)) i211_tx_desc_t;

_Static_assert(sizeof(i211_tx_desc_t) == 16, "TX descriptor must be 16 bytes");

/* ========================================================================
 * Legacy RX Descriptor (16 bytes)
 *
 * The NIC writes received frame data to the buffer address, then sets
 * the DD bit in the status field and writes the frame length.
 * ======================================================================== */

/* RX status byte bits */
#define I211_RX_STA_DD          (1U << 0)   /* Descriptor Done */
#define I211_RX_STA_EOP         (1U << 1)   /* End Of Packet */

typedef struct {
    uint64_t addr;              /* Buffer physical address */
    uint16_t length;            /* Received frame length */
    uint16_t checksum;          /* Packet checksum */
    uint8_t  status;            /* Status (DD, EOP) */
    uint8_t  errors;            /* Error flags */
    uint16_t special;           /* Special / VLAN tag */
} __attribute__((packed)) i211_rx_desc_t;

_Static_assert(sizeof(i211_rx_desc_t) == 16, "RX descriptor must be 16 bytes");

/* ========================================================================
 * Driver Constants
 * ======================================================================== */

#define I211_TX_RING_SIZE       256     /* Number of TX descriptors */
#define I211_RX_RING_SIZE       256     /* Number of RX descriptors */
#define I211_RX_BUF_SIZE        2048    /* Per-RX-buffer size */
#define I211_MAX_FRAME_SIZE     1536    /* MTU + headers + padding */
#define I211_RESET_TIMEOUT      100000  /* Reset poll iterations */

/* ========================================================================
 * Driver State
 * ======================================================================== */

typedef struct {
    uint64_t            mmio_base;          /* MMIO base (from PCI BAR0) */

    /* MAC address */
    uint8_t             mac[6];

    /* TX ring */
    i211_tx_desc_t     *tx_ring;            /* 256-entry TX descriptor ring */
    uint32_t            tx_head;            /* Next descriptor to fill */
    uint64_t            tx_ring_phys;       /* Physical address of TX ring */

    /* RX ring */
    i211_rx_desc_t     *rx_ring;            /* 256-entry RX descriptor ring */
    uint32_t            rx_cur;             /* Next descriptor to check */
    uint64_t            rx_ring_phys;       /* Physical address of RX ring */

    /* RX buffer pool -- one buffer per descriptor */
    uint8_t            *rx_bufs[I211_RX_RING_SIZE];      /* Virtual addresses */
    uint64_t            rx_bufs_phys[I211_RX_RING_SIZE]; /* Physical addresses */

    /* Link state */
    int                 link_up;            /* 1 if link is up */
    uint32_t            link_speed;         /* 10, 100, or 1000 Mbps */
    int                 full_duplex;        /* 1 if full duplex */

    /* Statistics */
    uint64_t            tx_packets;
    uint64_t            rx_packets;
    uint64_t            tx_bytes;
    uint64_t            rx_bytes;
    uint64_t            tx_errors;
    uint64_t            rx_errors;
} i211_nic_t;

/* ========================================================================
 * Function Prototypes
 * ======================================================================== */

/**
 * i211_nic_init - Initialize the I211-AT NIC from MMIO base
 * @nic:       Driver state to initialize
 * @mmio_base: Physical MMIO base address (PCI BAR0, memory-mapped)
 *
 * Disables interrupts, performs device reset, reads MAC address, sets up
 * TX/RX descriptor rings, and enables transmitter and receiver.
 * Returns 0 on success, -1 on error (e.g., reset timeout).
 */
int i211_nic_init(i211_nic_t *nic, uint64_t mmio_base);

/**
 * i211_nic_probe_pci - Find and initialize I211-AT via PCI enumeration
 * @nic: Driver state to fill
 *
 * Uses pci_scan() to find an Intel 8086:1539 device, enables bus mastering,
 * extracts the MMIO base from BAR0, and calls i211_nic_init().
 * Returns 0 on success, -1 if device not found.
 */
int i211_nic_probe_pci(i211_nic_t *nic);

/**
 * i211_nic_get_mac - Copy the 6-byte MAC address from driver state
 * @nic: Initialized driver state
 * @mac: Output buffer (6 bytes)
 */
void i211_nic_get_mac(i211_nic_t *nic, uint8_t mac[6]);

/**
 * i211_nic_send - Transmit a frame using legacy TX descriptors
 * @nic: Initialized driver state
 * @buf: Frame data (Ethernet header + payload)
 * @len: Frame length in bytes (1..1536)
 *
 * Builds a legacy TX descriptor with EOP, IFCS, RS bits set, writes
 * the buffer address and length, and advances TDT to kick the NIC.
 * Returns 0 on success, -1 if ring full or len invalid.
 */
int i211_nic_send(i211_nic_t *nic, const void *buf, uint32_t len);

/**
 * i211_nic_recv - Receive a frame (polled) using legacy RX descriptors
 * @nic:     Initialized driver state
 * @buf:     Output buffer for received frame
 * @max_len: Size of output buffer
 *
 * Checks the current RX descriptor. If DD is set (NIC has written a frame),
 * copies data into buf, clears the descriptor, and advances RDT.
 * Returns frame length on success, 0 if no frame available, -1 on error.
 */
int i211_nic_recv(i211_nic_t *nic, void *buf, uint32_t max_len);

/**
 * i211_nic_set_rx_buffer - Assign a DMA buffer to an RX descriptor
 * @nic:   NIC state
 * @index: Descriptor index (0 to I211_RX_RING_SIZE-1)
 * @virt:  Virtual address of the buffer (for memcpy to caller)
 * @phys:  Physical address (for DMA -- written to descriptor addr field)
 * @size:  Buffer size (must be >= I211_RX_BUF_SIZE)
 */
void i211_nic_set_rx_buffer(i211_nic_t *nic, uint32_t index,
                             void *virt, uint64_t phys, uint32_t size);

/**
 * i211_nic_link_status - Read current link status from STATUS register
 * @nic: Initialized driver state
 *
 * Reads I211_STATUS register. Updates nic->link_up, link_speed,
 * full_duplex fields.
 * Returns 1 if link is up, 0 if down.
 */
int i211_nic_link_status(i211_nic_t *nic);

#endif /* JARVIS_NIC_I211_H */
