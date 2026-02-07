/*
 * JARVIS AI-OS - BCM2711 GENET v5 Ethernet Controller
 *
 * Week 37: TX-only bare-metal driver for Raspberry Pi 4.
 * Based on Linux drivers/net/ethernet/broadcom/genet/bcmgenet.h
 *
 * The BCM2711 GENET is at ARM physical address 0xFD580000 (separate
 * from the 0xFE000000 main peripheral range). It uses RGMII to an
 * external BCM54213PE Gigabit PHY.
 */

#ifndef BCM_GENET_H
#define BCM_GENET_H

#include <stdint.h>
#include <stdbool.h>

/* BCM2711 GENET base address (ARM physical) */
#define GENET_BASE              0xFD580000u

/* MMIO: 6 contiguous 4KB pages covering offsets 0x0000-0x5FFF */
#define GENET_MMIO_PAGES        6
#define GENET_MMIO_VADDR_BASE   0x604000u   /* After DMA pool */

/* Default PHY address for BCM54213PE on Pi 4 */
#ifndef GENET_PHY_ADDR
#define GENET_PHY_ADDR          1
#endif

/* Enable/disable GENET test in main */
#ifndef GENET_TEST
#define GENET_TEST              1
#endif

/* ================================================================
 * Register Block Offsets (from GENET base)
 * ================================================================ */
#define GENET_SYS_OFF           0x0000
#define GENET_GR_BRIDGE_OFF     0x0040
#define GENET_EXT_OFF           0x0080
#define GENET_INTRL2_0_OFF      0x0200
#define GENET_INTRL2_1_OFF      0x0240
#define GENET_RBUF_OFF          0x0300
#define GENET_UMAC_OFF          0x0800
#define GENET_RDMA_OFF          0x2000
#define GENET_TDMA_OFF          0x4000

/* ================================================================
 * DMA Descriptor Format (GENET v5: 12 bytes / 3 words)
 * ================================================================ */
#define DMA_DESC_LENGTH_STATUS  0x00
#define DMA_DESC_ADDRESS_LO     0x04
#define DMA_DESC_ADDRESS_HI     0x08
#define DMA_DESC_SIZE           12

/* Total descriptors in GENET hardware RAM */
#define GENET_TOTAL_DESC        256
#define GENET_DESC_INDEX        16      /* Default ring index */
#define GENET_WORDS_PER_BD      (DMA_DESC_SIZE / 4)

/* DMA ring register base (after descriptor RAM) */
#define GENET_TDMA_REG_OFF      (GENET_TDMA_OFF + GENET_TOTAL_DESC * DMA_DESC_SIZE)  /* 0x4C00 */
#define GENET_RDMA_REG_OFF      (GENET_RDMA_OFF + GENET_TOTAL_DESC * DMA_DESC_SIZE)  /* 0x2C00 */

/* ================================================================
 * Ring Registers (per ring, stride 0x40)
 * ================================================================ */
#define DMA_RING_STRIDE         0x40
#define RING_READ_PTR           0x00
#define RING_READ_PTR_HI        0x04
#define RING_CONS_INDEX         0x08
#define RING_PROD_INDEX         0x0C
#define RING_BUF_SIZE           0x10
#define RING_START_ADDR         0x14
#define RING_START_ADDR_HI      0x18
#define RING_END_ADDR           0x1C
#define RING_END_ADDR_HI        0x20
#define RING_MBUF_DONE_THRESH   0x24
#define RING_FLOW_PERIOD        0x28
#define RING_WRITE_PTR          0x2C
#define RING_WRITE_PTR_HI       0x30

/* ================================================================
 * Global DMA Registers (after 17 rings: 0-16)
 * From Linux genet_dma_regs_v3plus[] - DO NOT reorder
 * ================================================================ */
#define DMA_NUM_RINGS           17
#define DMA_RINGS_SIZE          (DMA_NUM_RINGS * DMA_RING_STRIDE)   /* 0x440 */
#define DMA_RING_CFG_OFF        0x00    /* Ring enable bitmask */
#define DMA_CTRL_OFF            0x04    /* DMA_EN + per-ring enable bits */
#define DMA_STATUS_OFF          0x08
#define DMA_SCB_BURST_SIZE_OFF  0x0C

/* DMA_CTRL bits */
#define DMA_EN                  (1u << 0)
#define DMA_RING_BUF_EN_SHIFT   1

/* DMA burst size */
#define DMA_MAX_BURST_LENGTH    0x08

/* Ring buffer size register: (desc_count << 16) | buf_length */
#define DMA_RING_SIZE_SHIFT     16
#define DMA_MAX_BUF_LENGTH      2048

/* Producer/consumer index mask */
#define DMA_INDEX_MASK          0xFFFFu

/* ================================================================
 * SYS Registers (offset from GENET_SYS_OFF)
 * ================================================================ */
#define SYS_REV_CTRL            0x00
#define SYS_PORT_CTRL           0x04
#define SYS_RBUF_FLUSH_CTRL     0x08
#define SYS_TBUF_FLUSH_CTRL     0x0C

/* SYS_PORT_CTRL: external gigabit PHY mode */
#define PORT_MODE_EXT_GPHY      3

/* ================================================================
 * EXT Registers (offset from GENET_EXT_OFF)
 * ================================================================ */
#define EXT_EXT_PWR_MGMT       0x00
#define EXT_RGMII_OOB_CTRL     0x0C
#define EXT_GPHY_CTRL           0x1C

/* EXT_EXT_PWR_MGMT bits */
#define EXT_PWR_DOWN_BIAS       (1u << 0)
#define EXT_PWR_DOWN_DLL        (1u << 1)
#define EXT_PWR_DN_EN_LD        (1u << 3)
#define EXT_ENERGY_DET          (1u << 4)
#define EXT_IDDQ_FROM_PHY       (1u << 5)
#define EXT_IDDQ_GLBL_PWR       (1u << 7)
#define EXT_PHY_RESET           (1u << 8)
#define EXT_ENERGY_DET_MASK     (1u << 12)
/* GENET v5 power-down bits */
#define EXT_PWR_DOWN_PHY_TX     (1u << 16)
#define EXT_PWR_DOWN_PHY_RX     (1u << 17)
#define EXT_PWR_DOWN_PHY_SD     (1u << 18)
#define EXT_PWR_DOWN_PHY_RD     (1u << 19)
#define EXT_PWR_DOWN_PHY_EN     (1u << 20)

/* EXT_GPHY_CTRL bits */
#define EXT_CFG_IDDQ_BIAS       (1u << 0)
#define EXT_CFG_PWR_DOWN        (1u << 1)
#define EXT_CFG_IDDQ_GLOBAL_PWR (1u << 3)
#define EXT_CK25_DIS            (1u << 4)
#define EXT_GPHY_RESET          (1u << 5)

/* EXT_RGMII_OOB_CTRL bits */
#define RGMII_LINK              (1u << 4)
#define OOB_DISABLE             (1u << 5)
#define RGMII_MODE_EN           (1u << 6)
#define ID_MODE_DIS             (1u << 16)

/* ================================================================
 * RBUF Registers (offset from GENET_RBUF_OFF)
 * ================================================================ */
#define RBUF_CTRL               0x00
#define RBUF_TBUF_SIZE_CTRL     0xB4    /* v3+ offset */

#define RBUF_ALIGN_2B           (1u << 0)
#define RBUF_64B_EN             (1u << 1)

/* RBUF prepend: 2-byte status + 64-byte readahead when RBUF_64B_EN is set.
 * The hardware DMA length includes this prepend + 4-byte FCS at the end. */
#define RBUF_STATUS_PREPEND     2
#define RBUF_64B_PREPEND        64
#define RBUF_RX_OFFSET          (RBUF_STATUS_PREPEND + RBUF_64B_PREPEND)  /* 66 bytes */
#define RBUF_FCS_LEN            4

/* ================================================================
 * UMAC Registers (offset from GENET_UMAC_OFF)
 * ================================================================ */
#define UMAC_CMD                0x008
#define UMAC_MAC0               0x00C   /* MAC addr bytes [5:2] */
#define UMAC_MAC1               0x010   /* MAC addr bytes [1:0] */
#define UMAC_MAX_FRAME_LEN      0x014
#define UMAC_TX_FLUSH           0x334
#define UMAC_MIB_CTRL           0x580
#define UMAC_MDIO_CMD           0x614

/* UMAC_CMD bits */
#define CMD_TX_EN               (1u << 0)
#define CMD_RX_EN               (1u << 1)
#define CMD_SPEED_10            (0u << 2)
#define CMD_SPEED_100           (1u << 2)
#define CMD_SPEED_1000          (2u << 2)
#define CMD_SPEED_MASK          (3u << 2)
#define CMD_PROMISC             (1u << 4)
#define CMD_PAD_EN              (1u << 5)
#define CMD_CRC_FWD             (1u << 6)
#define CMD_HD_EN               (1u << 10)
#define CMD_SW_RESET            (1u << 13)
#define CMD_LCL_LOOP_EN        (1u << 15)

/* MIB_CTRL bits */
#define MIB_RESET_RX            (1u << 0)
#define MIB_RESET_RUNT          (1u << 1)
#define MIB_RESET_TX            (1u << 2)

/* Max frame length */
#define ENET_MAX_MTU_SIZE       1536

/* ================================================================
 * MDIO (via UMAC_MDIO_CMD)
 * ================================================================ */
#define MDIO_START_BUSY         (1u << 29)
#define MDIO_READ_FAIL          (1u << 28)
#define MDIO_RD                 (2u << 26)
#define MDIO_WR                 (1u << 26)
#define MDIO_PMD_SHIFT          21
#define MDIO_REG_SHIFT          16

/* ================================================================
 * DMA Descriptor Status Bits (length_status word)
 * ================================================================ */
#define DMA_EOP                 0x4000u
#define DMA_SOP                 0x2000u
#define DMA_TX_APPEND_CRC       0x0040u
#define DMA_TX_DO_CSUM          0x0020u
#define DMA_TX_QTAG_SHIFT       7
#define DMA_BUFLENGTH_SHIFT     16
#define DMA_BUFLENGTH_MASK      0x0FFF0000u

/* ================================================================
 * INTRL2 Registers (offset from INTRL2_x_OFF)
 * ================================================================ */
#define INTRL2_CPU_STAT         0x00
#define INTRL2_CPU_CLEAR        0x08
#define INTRL2_CPU_MASK_SET     0x10
#define INTRL2_CPU_MASK_CLEAR   0x14

/* ================================================================
 * Standard MII PHY Registers
 * ================================================================ */
#define MII_BMCR                0x00
#define MII_BMSR                0x01
#define MII_PHYSID1             0x02
#define MII_PHYSID2             0x03

#define BMCR_RESET              (1u << 15)
#define BMCR_ANENABLE           (1u << 12)
#define BMCR_ANRESTART          (1u << 9)
#define BMCR_FULLDPLX           (1u << 8)
#define BMCR_SPEED1000          (1u << 6)

#define BMSR_LSTATUS            (1u << 2)
#define BMSR_ANEGCOMPLETE       (1u << 5)

/* ================================================================
 * TX Ring State
 * ================================================================ */
typedef struct {
    uint32_t prod_index;
    uint32_t size;
} genet_tx_ring_t;

/* ================================================================
 * Public API
 * ================================================================ */

/* Initialize GENET controller (map MMIO, reset, configure UMAC).
 * Requires: slot_alloc_init() and dma_alloc_init() already called. */
bool genet_init(void);

/* Check if GENET is initialized */
bool genet_is_initialized(void);

/* Set MAC address (6 bytes) */
void genet_set_mac(const uint8_t mac[6]);

/* Initialize PHY via MDIO (reset, read ID, auto-negotiate).
 * Returns true if PHY responds. Link may not be up yet. */
bool genet_phy_init(void);

/* Initialize TX DMA ring 16 and enable TDMA engine */
bool genet_tx_ring_init(void);

/* Send a raw Ethernet frame (14-1514 bytes, must include Ethernet header).
 * Hardware appends CRC. Returns true if DMA accepted the frame. */
bool genet_tx_send(const uint8_t *frame, uint32_t len);

/* Read GENET revision register (for mapping verification) */
uint32_t genet_read_rev(void);

/* ================================================================
 * RX Ring State
 * ================================================================ */

/* RX buffer pool: 32 buffers of 2048 bytes each (16 x 4KB pages) */
#define GENET_RX_DESC_COUNT     32
#define GENET_RX_BUF_SIZE       2048

typedef struct {
    uint32_t cons_index;    /* Software consumer index */
    uint32_t prod_index;    /* Hardware producer index (read from register) */
    uint32_t size;          /* Number of descriptors */
} genet_rx_ring_t;

/* RX DMA descriptor status bits */
#define DMA_RX_OWN              0x8000u     /* Hardware owns this descriptor */
#define DMA_RX_CHK_V3PLUS       0x0040u     /* RX checksum valid (v3+) */
#define DMA_RX_CRC_ERROR        0x0001u
#define DMA_RX_RXER             0x0004u
#define DMA_RX_OV               0x0010u     /* Overflow */
#define DMA_RX_LG               0x0020u     /* Frame too long */

/* ================================================================
 * RX Public API
 * ================================================================ */

/* Initialize RX DMA ring 16 and enable RDMA + UMAC RX.
 * Requires: genet_init() already called. */
bool genet_rx_ring_init(void);

/* Receive a frame. Copies frame data to buf (up to buf_size bytes).
 * Sets *len_out to actual frame length. Returns true if frame received. */
bool genet_rx_recv(uint8_t *buf, uint32_t buf_size, uint32_t *len_out);

/* Check if any RX frames are pending (non-destructive). */
bool genet_rx_poll(void);

#endif /* BCM_GENET_H */
