/**
 * ahci.h - AHCI (SATA) Driver Header (x86)
 *
 * Skeleton header for Phase 3 x86 AHCI/SATA driver.
 * HBA register definitions, DMA structures, and function prototypes only.
 *
 * Reference: https://wiki.osdev.org/AHCI
 *
 * JARVIS AI-OS - Phase 3 Pre-Work
 */

#ifndef JARVIS_AHCI_H
#define JARVIS_AHCI_H

#include <stdint.h>

/* ========================================================================
 * SATA Device Signatures (read from PxSIG after device detection)
 * ======================================================================== */

#define SATA_SIG_ATA        0x00000101  /* SATA hard disk */
#define SATA_SIG_ATAPI      0xEB140101  /* SATA optical drive (ATAPI) */
#define SATA_SIG_SEMB       0xC33C0101  /* Enclosure management bridge */
#define SATA_SIG_PM         0x96690101  /* Port multiplier */

/* ========================================================================
 * ATA Command Codes
 * ======================================================================== */

#define ATA_CMD_IDENTIFY        0xEC    /* Identify Device */
#define ATA_CMD_READ_DMA_EXT    0x25    /* Read DMA Extended (LBA48) */
#define ATA_CMD_WRITE_DMA_EXT   0x35    /* Write DMA Extended (LBA48) */
#define ATA_CMD_FLUSH_CACHE     0xE7    /* Flush write cache to media */
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA    /* Flush write cache (48-bit) */
#define ATA_CMD_PACKET          0xA0    /* ATAPI packet command */
#define ATA_CMD_IDENTIFY_PACKET 0xA1    /* Identify Packet Device */

/* ========================================================================
 * FIS Types
 * ======================================================================== */

#define FIS_TYPE_REG_H2D    0x27    /* Register FIS - Host to Device */
#define FIS_TYPE_REG_D2H    0x34    /* Register FIS - Device to Host */
#define FIS_TYPE_DMA_ACT    0x39    /* DMA Activate FIS */
#define FIS_TYPE_DMA_SETUP  0x41    /* DMA Setup FIS - bidirectional */
#define FIS_TYPE_DATA       0x46    /* Data FIS - bidirectional */
#define FIS_TYPE_BIST       0x58    /* BIST Activate FIS */
#define FIS_TYPE_PIO_SETUP  0x5F    /* PIO Setup FIS - Device to Host */
#define FIS_TYPE_DEV_BITS   0xA1    /* Set Device Bits FIS */

/* ========================================================================
 * HBA Generic Host Control Register Offsets (from ABAR)
 * ======================================================================== */

#define HBA_REG_CAP         0x00    /* Host Capabilities */
#define HBA_REG_GHC         0x04    /* Global Host Control */
#define HBA_REG_IS          0x08    /* Interrupt Status (one bit per port) */
#define HBA_REG_PI          0x0C    /* Ports Implemented (bitmask) */
#define HBA_REG_VS          0x10    /* AHCI Version */
#define HBA_REG_CCC_CTL     0x14    /* Command Completion Coalescing Control */
#define HBA_REG_CCC_PORTS   0x18    /* Command Completion Coalescing Ports */
#define HBA_REG_EM_LOC      0x1C    /* Enclosure Management Location */
#define HBA_REG_EM_CTL      0x20    /* Enclosure Management Control */
#define HBA_REG_CAP2        0x24    /* Host Capabilities Extended */
#define HBA_REG_BOHC        0x28    /* BIOS/OS Handoff Control and Status */

/* Port registers start at offset 0x100, each port is 0x80 bytes */
#define HBA_PORT_BASE       0x100
#define HBA_PORT_SIZE       0x80

/* ========================================================================
 * GHC (Global Host Control) Bit Definitions  - Offset 0x04
 * ======================================================================== */

#define GHC_HR              (1U << 0)   /* HBA Reset */
#define GHC_IE              (1U << 1)   /* Interrupt Enable */
#define GHC_MRSM            (1U << 2)   /* MSI Revert to Single Message */
#define GHC_AE              (1U << 31)  /* AHCI Enable */

/* ========================================================================
 * CAP (Host Capabilities) Bit Definitions  - Offset 0x00
 * ======================================================================== */

#define CAP_NP_MASK         0x1F        /* Bits  4:0  - Number of Ports (0-based) */
#define CAP_SXS             (1U << 5)   /* Supports External SATA */
#define CAP_EMS             (1U << 6)   /* Enclosure Management Supported */
#define CAP_CCCS            (1U << 7)   /* Command Completion Coalescing */
#define CAP_NCS_SHIFT       8           /* Bits 12:8  - Number of Command Slots */
#define CAP_NCS_MASK        0x1F00      /*              (0-based, max 32) */
#define CAP_PSC             (1U << 13)  /* Partial State Capable */
#define CAP_SSC             (1U << 14)  /* Slumber State Capable */
#define CAP_PMD             (1U << 15)  /* PIO Multiple DRQ Block */
#define CAP_FBSS            (1U << 16)  /* FIS-Based Switching Supported */
#define CAP_SPM             (1U << 17)  /* Port Multiplier Supported */
#define CAP_SAM             (1U << 18)  /* AHCI-Only Mode */
#define CAP_ISS_SHIFT       20          /* Bits 23:20 - Interface Speed Support */
#define CAP_ISS_MASK        0x00F00000  /*   1=Gen1, 2=Gen2, 3=Gen3 */
#define CAP_SCLO            (1U << 24)  /* Command List Override Supported */
#define CAP_SAL             (1U << 25)  /* Activity LED Supported */
#define CAP_SALP            (1U << 26)  /* Aggressive Link PM Supported */
#define CAP_SSS             (1U << 27)  /* Staggered Spin-Up Supported */
#define CAP_SMPS            (1U << 28)  /* Mechanical Presence Switch */
#define CAP_SSNTF           (1U << 29)  /* SNotification Supported */
#define CAP_SNCQ            (1U << 30)  /* Native Command Queuing */
#define CAP_S64A            (1U << 31)  /* 64-Bit Addressing Supported */

/* ========================================================================
 * Port Register Offsets (relative to port base: ABAR + 0x100 + port*0x80)
 * ======================================================================== */

#define PORT_CLB            0x00    /* Command List Base Address (1K-aligned) */
#define PORT_CLBU           0x04    /* Command List Base Address Upper 32 bits */
#define PORT_FB             0x08    /* FIS Base Address (256-byte aligned) */
#define PORT_FBU            0x0C    /* FIS Base Address Upper 32 bits */
#define PORT_IS             0x10    /* Interrupt Status */
#define PORT_IE             0x14    /* Interrupt Enable */
#define PORT_CMD            0x18    /* Command and Status */
/* 0x1C reserved */
#define PORT_TFD            0x20    /* Task File Data */
#define PORT_SIG            0x24    /* Signature */
#define PORT_SSTS           0x28    /* SATA Status (SCR0: SStatus) */
#define PORT_SCTL           0x2C    /* SATA Control (SCR2: SControl) */
#define PORT_SERR           0x30    /* SATA Error (SCR1: SError) */
#define PORT_SACT           0x34    /* SATA Active (SCR3: SActive) */
#define PORT_CI             0x38    /* Command Issue */
#define PORT_SNTF           0x3C    /* SATA Notification (SCR4: SNotification) */
#define PORT_FBS            0x40    /* FIS-Based Switching Control */

/* ========================================================================
 * Port CMD (Command and Status) Bit Definitions  - Port + 0x18
 * ======================================================================== */

#define PORT_CMD_ST         (1U << 0)   /* Start (command processing) */
#define PORT_CMD_SUD        (1U << 1)   /* Spin-Up Device */
#define PORT_CMD_POD        (1U << 2)   /* Power On Device */
#define PORT_CMD_CLO        (1U << 3)   /* Command List Override */
#define PORT_CMD_FRE        (1U << 4)   /* FIS Receive Enable */
/* Bits 7:5 reserved */
#define PORT_CMD_CCS_SHIFT  8           /* Bits 12:8 - Current Command Slot */
#define PORT_CMD_CCS_MASK   0x1F00
#define PORT_CMD_MPSS       (1U << 13)  /* Mechanical Presence Switch State */
#define PORT_CMD_FR         (1U << 14)  /* FIS Receive Running */
#define PORT_CMD_CR         (1U << 15)  /* Command List Running */
#define PORT_CMD_CPS        (1U << 16)  /* Cold Presence State */
#define PORT_CMD_PMA        (1U << 17)  /* Port Multiplier Attached */
#define PORT_CMD_HPCP       (1U << 18)  /* Hot-Plug Capable Port */
#define PORT_CMD_MPSP       (1U << 19)  /* Mechanical Presence Switch */
#define PORT_CMD_CPD        (1U << 20)  /* Cold Presence Detection */
#define PORT_CMD_ESP        (1U << 21)  /* External SATA Port */
#define PORT_CMD_FBSCP      (1U << 22)  /* FIS-Based Switching Capable */
#define PORT_CMD_APSTE      (1U << 23)  /* Auto Partial to Slumber */
#define PORT_CMD_ATAPI      (1U << 24)  /* Device is ATAPI */
#define PORT_CMD_DLAE       (1U << 25)  /* Drive LED on ATAPI Enable */
#define PORT_CMD_ALPE       (1U << 26)  /* Aggressive Link PM Enable */
#define PORT_CMD_ASP        (1U << 27)  /* Aggressive Slumber/Partial */
#define PORT_CMD_ICC_SHIFT  28          /* Bits 31:28 - Interface Comm Control */
#define PORT_CMD_ICC_MASK   0xF0000000U

/* ========================================================================
 * Port SSTS (SATA Status / SStatus) Bit Definitions  - Port + 0x28
 * ======================================================================== */

/* Device Detection (bits 3:0) */
#define PORT_SSTS_DET_MASK      0x0F
#define PORT_SSTS_DET_NONE      0x00    /* No device detected */
#define PORT_SSTS_DET_PRESENT   0x01    /* Device present, no communication */
#define PORT_SSTS_DET_ACTIVE    0x03    /* Device present and communication ok */
#define PORT_SSTS_DET_OFFLINE   0x04    /* PHY offline mode */

/* Interface Speed (bits 7:4) */
#define PORT_SSTS_SPD_SHIFT     4
#define PORT_SSTS_SPD_MASK      0xF0
#define PORT_SSTS_SPD_NONE      0x00    /* No speed negotiated */
#define PORT_SSTS_SPD_GEN1      0x10    /* Gen 1 (1.5 Gbps) */
#define PORT_SSTS_SPD_GEN2      0x20    /* Gen 2 (3.0 Gbps) */
#define PORT_SSTS_SPD_GEN3      0x30    /* Gen 3 (6.0 Gbps) */

/* Interface Power Management (bits 11:8) */
#define PORT_SSTS_IPM_SHIFT     8
#define PORT_SSTS_IPM_MASK      0x0F00
#define PORT_SSTS_IPM_NONE      0x000   /* No device / not active */
#define PORT_SSTS_IPM_ACTIVE    0x100   /* Interface in active state */
#define PORT_SSTS_IPM_PARTIAL   0x200   /* Partial power state */
#define PORT_SSTS_IPM_SLUMBER   0x600   /* Slumber power state */

/* ========================================================================
 * Port TFD (Task File Data) Bit Definitions  - Port + 0x20
 * ======================================================================== */

#define PORT_TFD_STS_ERR    (1U << 0)   /* Error bit */
#define PORT_TFD_STS_DRQ    (1U << 3)   /* Data request */
#define PORT_TFD_STS_BSY    (1U << 7)   /* Device busy */
#define PORT_TFD_STS_MASK   0xFF        /* Status byte (bits 7:0) */
#define PORT_TFD_ERR_MASK   0xFF00      /* Error byte (bits 15:8) */

/* ========================================================================
 * Port Interrupt Status / Enable Bit Definitions  - Port + 0x10 / 0x14
 * ======================================================================== */

#define PORT_INT_DHRS       (1U << 0)   /* Device to Host Register FIS */
#define PORT_INT_PSS        (1U << 1)   /* PIO Setup FIS */
#define PORT_INT_DSS        (1U << 2)   /* DMA Setup FIS */
#define PORT_INT_SDBS       (1U << 3)   /* Set Device Bits */
#define PORT_INT_UFS        (1U << 4)   /* Unknown FIS */
#define PORT_INT_DPS        (1U << 5)   /* Descriptor Processed */
#define PORT_INT_PCS        (1U << 6)   /* Port Connect Change */
#define PORT_INT_DMPS       (1U << 7)   /* Device Mechanical Presence */
#define PORT_INT_PRCS       (1U << 22)  /* PhyRdy Change Status */
#define PORT_INT_IPMS       (1U << 23)  /* Incorrect Port Multiplier */
#define PORT_INT_OFS        (1U << 24)  /* Overflow Status */
#define PORT_INT_INFS       (1U << 26)  /* Interface Non-Fatal Error */
#define PORT_INT_IFS        (1U << 27)  /* Interface Fatal Error */
#define PORT_INT_HBDS       (1U << 28)  /* Host Bus Data Error */
#define PORT_INT_HBFS       (1U << 29)  /* Host Bus Fatal Error */
#define PORT_INT_TFES       (1U << 30)  /* Task File Error Status */
#define PORT_INT_CPDS       (1U << 31)  /* Cold Port Detect */

/* ========================================================================
 * Register FIS - Host to Device (H2D)  - 20 bytes / 5 DWORDs
 *
 * Used to issue ATA commands to the device via the Command Table CFIS area.
 * ======================================================================== */

typedef struct {
    /* DWORD 0 */
    uint8_t  fis_type;      /* FIS_TYPE_REG_H2D (0x27) */
    uint8_t  flags;         /* Bits 3:0 = port multiplier,
                               Bit  6   = reserved,
                               Bit  7   = 1 = command, 0 = control */
    uint8_t  command;       /* ATA command register */
    uint8_t  feature_lo;    /* Feature register (7:0) */

    /* DWORD 1 */
    uint8_t  lba0;          /* LBA (7:0) */
    uint8_t  lba1;          /* LBA (15:8) */
    uint8_t  lba2;          /* LBA (23:16) */
    uint8_t  device;        /* Device register */

    /* DWORD 2 */
    uint8_t  lba3;          /* LBA (31:24) */
    uint8_t  lba4;          /* LBA (39:32) */
    uint8_t  lba5;          /* LBA (47:40) */
    uint8_t  feature_hi;    /* Feature register (15:8) */

    /* DWORD 3 */
    uint8_t  count_lo;      /* Sector count (7:0) */
    uint8_t  count_hi;      /* Sector count (15:8) */
    uint8_t  icc;           /* Isochronous command completion */
    uint8_t  control;       /* Control register */

    /* DWORD 4 */
    uint8_t  reserved[4];   /* Reserved */
} __attribute__((packed)) fis_reg_h2d_t;

/* H2D FIS flags field */
#define FIS_H2D_CMD         (1 << 7)    /* 1 = command FIS, 0 = control FIS */

/* ========================================================================
 * Register FIS - Device to Host (D2H)  - 20 bytes / 5 DWORDs
 * ======================================================================== */

typedef struct {
    /* DWORD 0 */
    uint8_t  fis_type;      /* FIS_TYPE_REG_D2H (0x34) */
    uint8_t  flags;         /* Bits 3:0 = port multiplier, Bit 6 = interrupt */
    uint8_t  status;        /* Status register */
    uint8_t  error;         /* Error register */

    /* DWORD 1 */
    uint8_t  lba0;          /* LBA (7:0) */
    uint8_t  lba1;          /* LBA (15:8) */
    uint8_t  lba2;          /* LBA (23:16) */
    uint8_t  device;        /* Device register */

    /* DWORD 2 */
    uint8_t  lba3;          /* LBA (31:24) */
    uint8_t  lba4;          /* LBA (39:32) */
    uint8_t  lba5;          /* LBA (47:40) */
    uint8_t  reserved0;

    /* DWORD 3 */
    uint8_t  count_lo;      /* Sector count (7:0) */
    uint8_t  count_hi;      /* Sector count (15:8) */
    uint8_t  reserved1[2];

    /* DWORD 4 */
    uint8_t  reserved2[4];
} __attribute__((packed)) fis_reg_d2h_t;

/* ========================================================================
 * DMA Setup FIS - 28 bytes / 7 DWORDs
 * ======================================================================== */

typedef struct {
    /* DWORD 0 */
    uint8_t  fis_type;      /* FIS_TYPE_DMA_SETUP (0x41) */
    uint8_t  flags;         /* Bits 3:0 = PM port, Bit 5 = direction,
                               Bit 6 = interrupt, Bit 7 = auto-activate */
    uint8_t  reserved0[2];

    /* DWORD 1-2 */
    uint32_t dma_buf_id_lo; /* DMA Buffer Identifier Low */
    uint32_t dma_buf_id_hi; /* DMA Buffer Identifier High */

    /* DWORD 3 */
    uint32_t reserved1;

    /* DWORD 4 */
    uint32_t dma_buf_offset;/* Byte offset into buffer */

    /* DWORD 5 */
    uint32_t transfer_count;/* Number of bytes to transfer */

    /* DWORD 6 */
    uint32_t reserved2;
} __attribute__((packed)) fis_dma_setup_t;

/* ========================================================================
 * PIO Setup FIS - Device to Host - 20 bytes / 5 DWORDs
 * ======================================================================== */

typedef struct {
    /* DWORD 0 */
    uint8_t  fis_type;      /* FIS_TYPE_PIO_SETUP (0x5F) */
    uint8_t  flags;         /* Bits 3:0 = PM port, Bit 5 = direction,
                               Bit 6 = interrupt */
    uint8_t  status;        /* Status register */
    uint8_t  error;         /* Error register */

    /* DWORD 1 */
    uint8_t  lba0;
    uint8_t  lba1;
    uint8_t  lba2;
    uint8_t  device;

    /* DWORD 2 */
    uint8_t  lba3;
    uint8_t  lba4;
    uint8_t  lba5;
    uint8_t  reserved0;

    /* DWORD 3 */
    uint8_t  count_lo;
    uint8_t  count_hi;
    uint8_t  reserved1;
    uint8_t  e_status;      /* New status value */

    /* DWORD 4 */
    uint16_t transfer_count;/* Transfer count */
    uint8_t  reserved2[2];
} __attribute__((packed)) fis_pio_setup_t;

/* ========================================================================
 * Set Device Bits FIS - 8 bytes / 2 DWORDs
 * ======================================================================== */

typedef struct {
    /* DWORD 0 */
    uint8_t  fis_type;      /* FIS_TYPE_DEV_BITS (0xA1) */
    uint8_t  flags;         /* Bits 3:0 = PM port, Bit 6 = interrupt,
                               Bit 7 = notification */
    uint8_t  status;        /* Status register (hi:lo) */
    uint8_t  error;         /* Error register */

    /* DWORD 1 */
    uint32_t protocol;      /* Protocol specific */
} __attribute__((packed)) fis_dev_bits_t;

/* ========================================================================
 * Received FIS Structure (256 bytes, stored in memory per port)
 *
 * The HBA writes incoming FISes to this memory region.
 * Must be 256-byte aligned.
 * ======================================================================== */

typedef struct {
    /* 0x00: DMA Setup FIS (28 bytes) + 4 bytes padding */
    fis_dma_setup_t dsfis;
    uint8_t         pad0[4];

    /* 0x20: PIO Setup FIS (20 bytes) + 12 bytes padding */
    fis_pio_setup_t psfis;
    uint8_t         pad1[12];

    /* 0x40: Register D2H FIS (20 bytes) + 4 bytes padding */
    fis_reg_d2h_t   rfis;
    uint8_t         pad2[4];

    /* 0x58: Set Device Bits FIS (8 bytes) */
    fis_dev_bits_t  sdbfis;

    /* 0x60: Unknown FIS (64 bytes) */
    uint8_t         ufis[64];

    /* 0xA0: Reserved (96 bytes) */
    uint8_t         reserved[96];
} __attribute__((packed)) hba_fis_t;

/* ========================================================================
 * Command Header (32 bytes each, up to 32 entries per port)
 *
 * The command list is a 1K-aligned array of up to 32 command headers.
 * ======================================================================== */

typedef struct {
    /* DWORD 0 */
    uint16_t flags;         /* Bits  4:0  = Command FIS Length (DWORDs)
                               Bit   5    = ATAPI (1 = ATAPI command)
                               Bit   6    = Write (1 = H2D, 0 = D2H)
                               Bit   7    = Prefetchable
                               Bit   8    = Reset
                               Bit   9    = BIST
                               Bit  10    = Clear Busy upon R_OK
                               Bits 11    = Reserved
                               Bits 15:12 = Port Multiplier Port */
    uint16_t prdtl;         /* Physical Region Descriptor Table Length
                               (number of PRD entries, 0 = no data) */

    /* DWORD 1 */
    volatile uint32_t prdbc;/* PRD Byte Count (updated by HBA, read-only) */

    /* DWORD 2 */
    uint32_t ctba;          /* Command Table Base Address (128-byte aligned) */

    /* DWORD 3 */
    uint32_t ctbau;         /* Command Table Base Address Upper 32 bits */

    /* DWORD 4-7 */
    uint32_t reserved[4];   /* Reserved */
} __attribute__((packed)) hba_cmd_header_t;

/* Command Header flags field bit definitions */
#define CMD_HDR_CFL_MASK    0x001F  /* Bits  4:0  - Command FIS Length */
#define CMD_HDR_ATAPI       0x0020  /* Bit   5    - ATAPI */
#define CMD_HDR_WRITE       0x0040  /* Bit   6    - Write direction */
#define CMD_HDR_PREFETCH    0x0080  /* Bit   7    - Prefetchable */
#define CMD_HDR_RESET       0x0100  /* Bit   8    - Reset */
#define CMD_HDR_BIST        0x0200  /* Bit   9    - BIST */
#define CMD_HDR_CLR_BUSY    0x0400  /* Bit  10    - Clear Busy on R_OK */
#define CMD_HDR_PMP_SHIFT   12      /* Bits 15:12 - Port Multiplier Port */
#define CMD_HDR_PMP_MASK    0xF000

/* ========================================================================
 * PRD (Physical Region Descriptor) Entry - 16 bytes
 *
 * Each entry describes one scatter/gather DMA region.
 * ======================================================================== */

typedef struct {
    uint32_t dba;           /* Data Base Address (2-byte aligned) */
    uint32_t dbau;          /* Data Base Address Upper 32 bits */
    uint32_t reserved;
    uint32_t dbc;           /* Bits 21:0 = Byte Count (value = count-1,
                                           max 4 MB per entry)
                               Bit  31   = Interrupt on Completion */
} __attribute__((packed)) hba_prd_entry_t;

#define PRD_DBC_MASK        0x003FFFFF  /* Bits 21:0 - byte count (count-1) */
#define PRD_DBC_IOC         (1U << 31)  /* Bit 31 - Interrupt on Completion */
#define PRD_MAX_BYTES       (4 * 1024 * 1024)  /* 4 MB max per PRD entry */

/* ========================================================================
 * Command Table (variable size: 128 bytes + PRDT entries)
 *
 * Must be 128-byte aligned.  One per command slot.
 * ======================================================================== */

#define AHCI_MAX_PRDT_ENTRIES   65535

typedef struct {
    uint8_t         cfis[64];   /* Command FIS (up to 64 bytes) */
    uint8_t         acmd[16];   /* ATAPI Command (12 or 16 bytes) */
    uint8_t         reserved[48];
    hba_prd_entry_t prdt[];     /* PRD Table (0 to 65535 entries) */
} __attribute__((packed)) hba_cmd_table_t;

/* ========================================================================
 * HBA Port Registers Structure
 *
 * One per implemented port.  Located at ABAR + 0x100 + port_num * 0x80.
 * All registers are volatile (memory-mapped hardware).
 * ======================================================================== */

typedef volatile struct {
    uint32_t clb;           /* 0x00: Command List Base Address */
    uint32_t clbu;          /* 0x04: Command List Base Address Upper */
    uint32_t fb;            /* 0x08: FIS Base Address */
    uint32_t fbu;           /* 0x0C: FIS Base Address Upper */
    uint32_t is;            /* 0x10: Interrupt Status */
    uint32_t ie;            /* 0x14: Interrupt Enable */
    uint32_t cmd;           /* 0x18: Command and Status */
    uint32_t reserved0;     /* 0x1C: Reserved */
    uint32_t tfd;           /* 0x20: Task File Data */
    uint32_t sig;           /* 0x24: Signature */
    uint32_t ssts;          /* 0x28: SATA Status (SCR0: SStatus) */
    uint32_t sctl;          /* 0x2C: SATA Control (SCR2: SControl) */
    uint32_t serr;          /* 0x30: SATA Error (SCR1: SError) */
    uint32_t sact;          /* 0x34: SATA Active (SCR3: SActive) */
    uint32_t ci;            /* 0x38: Command Issue */
    uint32_t sntf;          /* 0x3C: SATA Notification */
    uint32_t fbs;           /* 0x40: FIS-Based Switching Control */
    uint32_t reserved1[11]; /* 0x44-0x6F: Reserved */
    uint32_t vendor[4];     /* 0x70-0x7F: Vendor Specific */
} hba_port_t;

/* ========================================================================
 * HBA Memory Registers Structure (Generic Host Control)
 *
 * Located at ABAR (PCI BAR5 for AHCI controllers).
 * Contains global registers followed by per-port register arrays.
 * ======================================================================== */

#define AHCI_MAX_PORTS  32

typedef volatile struct {
    /* Generic Host Control (0x00 - 0x2B) */
    uint32_t cap;           /* 0x00: Host Capabilities */
    uint32_t ghc;           /* 0x04: Global Host Control */
    uint32_t is;            /* 0x08: Interrupt Status */
    uint32_t pi;            /* 0x0C: Ports Implemented */
    uint32_t vs;            /* 0x10: Version */
    uint32_t ccc_ctl;       /* 0x14: CCC Control */
    uint32_t ccc_ports;     /* 0x18: CCC Ports */
    uint32_t em_loc;        /* 0x1C: EM Location */
    uint32_t em_ctl;        /* 0x20: EM Control */
    uint32_t cap2;          /* 0x24: Host Capabilities Extended */
    uint32_t bohc;          /* 0x28: BIOS/OS Handoff */
    uint8_t  reserved[0xA0 - 0x2C]; /* 0x2C-0x9F: Reserved */
    uint8_t  vendor[0x100 - 0xA0];  /* 0xA0-0xFF: Vendor Specific */

    /* Port Registers (0x100+) */
    hba_port_t ports[AHCI_MAX_PORTS];
} hba_mem_t;

/* ========================================================================
 * AHCI Constants
 * ======================================================================== */

#define AHCI_CMD_SLOTS      32      /* Max command slots per port */
#define AHCI_SECTOR_SIZE    512     /* Standard sector size */

/* ========================================================================
 * Function Prototypes
 * ======================================================================== */

/**
 * ahci_init - Initialize the AHCI HBA controller
 * @hba: Pointer to HBA memory registers (from PCI BAR5)
 *
 * Performs BIOS/OS handoff, enables AHCI mode, resets HBA if needed,
 * and enumerates implemented ports.
 * Returns 0 on success, negative on error.
 */
int ahci_init(hba_mem_t *hba);

/**
 * ahci_port_init - Initialize a single AHCI port
 * @port:     Pointer to the port registers
 * @port_num: Port number (0-31)
 *
 * Allocates command list and FIS receive buffers, starts command engine.
 * Returns 0 on success, negative on error.
 */
int ahci_port_init(hba_port_t *port, int port_num);

/**
 * ahci_identify - Send ATA IDENTIFY DEVICE command
 * @port: Pointer to port registers
 * @buf:  512-byte output buffer for identify data
 *
 * Returns 0 on success, negative on error.
 */
int ahci_identify(hba_port_t *port, void *buf);

/**
 * ahci_read - Read sectors from disk via DMA
 * @port:  Pointer to port registers
 * @lba:   Starting Logical Block Address (48-bit)
 * @count: Number of sectors to read
 * @buf:   Output buffer (must be at least count * 512 bytes)
 *
 * Uses READ DMA EXT (0x25).  Returns 0 on success, negative on error.
 */
int ahci_read(hba_port_t *port, uint64_t lba, uint32_t count, void *buf);

/**
 * ahci_write - Write sectors to disk via DMA
 * @port:  Pointer to port registers
 * @lba:   Starting Logical Block Address (48-bit)
 * @count: Number of sectors to write
 * @buf:   Input buffer (must be at least count * 512 bytes)
 *
 * Uses WRITE DMA EXT (0x35).  Returns 0 on success, negative on error.
 */
int ahci_write(hba_port_t *port, uint64_t lba, uint32_t count,
               const void *buf);

/**
 * ahci_flush - Flush the device write cache
 * @port: Pointer to port registers
 *
 * Sends FLUSH CACHE (0xE7).  Returns 0 on success, negative on error.
 */
int ahci_flush(hba_port_t *port);

/**
 * ahci_port_is_active - Check if a port has an active device
 * @port: Pointer to port registers
 *
 * Checks PxSSTS DET field for device present + communication established.
 * Returns non-zero if active, 0 if no device or offline.
 */
int ahci_port_is_active(hba_port_t *port);

#endif /* JARVIS_AHCI_H */
