/**
 * pci.h - PCI Configuration Space Access Header (x86)
 *
 * Skeleton header for Phase 3 x86 PCI enumeration and configuration.
 * Register definitions, struct layouts, and function prototypes only.
 *
 * Reference: https://wiki.osdev.org/PCI
 *
 * JARVIS AI-OS - Phase 3 Pre-Work
 */

#ifndef JARVIS_PCI_H
#define JARVIS_PCI_H

#include <stdint.h>

/* ========================================================================
 * PCI Configuration I/O Ports
 * ======================================================================== */

#define PCI_CONFIG_ADDRESS  0x0CF8  /* 32-bit: configuration address */
#define PCI_CONFIG_DATA     0x0CFC  /* 32-bit: configuration data    */

/* ========================================================================
 * CONFIG_ADDRESS Register Format
 *
 *  31       | 30:24    | 23:16 | 15:11  | 10:8     | 7:2    | 1:0
 *  Enable   | Reserved | Bus   | Device | Function | Offset | 00
 * ======================================================================== */

#define PCI_ADDR_ENABLE     (1U << 31)

/* ========================================================================
 * Type 0 Configuration Space Header Register Offsets (bytes)
 * ======================================================================== */

#define PCI_VENDOR_ID       0x00    /* 16-bit: vendor ID */
#define PCI_DEVICE_ID       0x02    /* 16-bit: device ID */
#define PCI_COMMAND         0x04    /* 16-bit: command register */
#define PCI_STATUS          0x06    /* 16-bit: status register */
#define PCI_REVISION_ID     0x08    /*  8-bit: revision ID */
#define PCI_PROG_IF         0x09    /*  8-bit: programming interface */
#define PCI_SUBCLASS        0x0A    /*  8-bit: subclass code */
#define PCI_CLASS_CODE      0x0B    /*  8-bit: class code */
#define PCI_CACHE_LINE_SIZE 0x0C    /*  8-bit: cache line size (in 32-bit words) */
#define PCI_LATENCY_TIMER   0x0D    /*  8-bit: latency timer */
#define PCI_HEADER_TYPE     0x0E    /*  8-bit: header type */
#define PCI_BIST            0x0F    /*  8-bit: built-in self-test */
#define PCI_BAR0            0x10    /* 32-bit: Base Address Register 0 */
#define PCI_BAR1            0x14    /* 32-bit: Base Address Register 1 */
#define PCI_BAR2            0x18    /* 32-bit: Base Address Register 2 */
#define PCI_BAR3            0x1C    /* 32-bit: Base Address Register 3 */
#define PCI_BAR4            0x20    /* 32-bit: Base Address Register 4 */
#define PCI_BAR5            0x24    /* 32-bit: Base Address Register 5 */
#define PCI_CARDBUS_CIS     0x28    /* 32-bit: CardBus CIS pointer */
#define PCI_SUBSYS_VENDOR   0x2C    /* 16-bit: subsystem vendor ID */
#define PCI_SUBSYS_ID       0x2E    /* 16-bit: subsystem device ID */
#define PCI_ROM_BASE        0x30    /* 32-bit: expansion ROM base address */
#define PCI_CAP_PTR         0x34    /*  8-bit: capabilities pointer */
/* 0x35-0x3B: reserved */
#define PCI_INTERRUPT_LINE  0x3C    /*  8-bit: IRQ number */
#define PCI_INTERRUPT_PIN   0x3D    /*  8-bit: interrupt pin (1=INTA, 2=INTB, ...) */
#define PCI_MIN_GRANT       0x3E    /*  8-bit: minimum burst period (1/4 us units) */
#define PCI_MAX_LATENCY     0x3F    /*  8-bit: maximum latency (1/4 us units) */

/* ========================================================================
 * PCI Command Register Bits (offset 0x04, 16-bit)
 * ======================================================================== */

#define PCI_CMD_IO_SPACE        (1 << 0)    /* Enable I/O address decoding */
#define PCI_CMD_MEMORY_SPACE    (1 << 1)    /* Enable memory address decoding */
#define PCI_CMD_BUS_MASTER      (1 << 2)    /* Enable DMA / bus mastering */
#define PCI_CMD_SPECIAL_CYCLES  (1 << 3)    /* Monitor special cycle ops */
#define PCI_CMD_MWI_ENABLE      (1 << 4)    /* Memory Write and Invalidate */
#define PCI_CMD_VGA_SNOOP       (1 << 5)    /* VGA palette snoop */
#define PCI_CMD_PARITY_ERR      (1 << 6)    /* Parity error response */
/* Bit 7: reserved */
#define PCI_CMD_SERR_ENABLE     (1 << 8)    /* System error (SERR#) enable */
#define PCI_CMD_FAST_B2B        (1 << 9)    /* Fast back-to-back enable */
#define PCI_CMD_INT_DISABLE     (1 << 10)   /* Disable INTx# assertion */

/* ========================================================================
 * PCI Status Register Bits (offset 0x06, 16-bit)
 * ======================================================================== */

#define PCI_STATUS_INT_STATUS       (1 << 3)    /* Interrupt status */
#define PCI_STATUS_CAP_LIST         (1 << 4)    /* Capabilities list present */
#define PCI_STATUS_66MHZ            (1 << 5)    /* 66 MHz capable */
#define PCI_STATUS_FAST_B2B         (1 << 7)    /* Fast back-to-back capable */
#define PCI_STATUS_MASTER_PARITY    (1 << 8)    /* Master data parity error */
#define PCI_STATUS_DEVSEL_MASK      (3 << 9)    /* DEVSEL timing (00=fast) */
#define PCI_STATUS_SIG_TARGET_ABORT (1 << 11)   /* Signaled target abort */
#define PCI_STATUS_RCV_TARGET_ABORT (1 << 12)   /* Received target abort */
#define PCI_STATUS_RCV_MASTER_ABORT (1 << 13)   /* Received master abort */
#define PCI_STATUS_SIG_SYS_ERR      (1 << 14)   /* Signaled system error */
#define PCI_STATUS_PARITY_ERR       (1 << 15)   /* Detected parity error */

/* ========================================================================
 * Header Type Register (offset 0x0E)
 * ======================================================================== */

#define PCI_HEADER_TYPE_NORMAL      0x00    /* Standard device */
#define PCI_HEADER_TYPE_BRIDGE      0x01    /* PCI-to-PCI bridge */
#define PCI_HEADER_TYPE_CARDBUS     0x02    /* CardBus bridge */
#define PCI_HEADER_MULTIFUNCTION    0x80    /* Multi-function device flag */

/* ========================================================================
 * BAR (Base Address Register) Format
 *
 * Memory Space BAR (bit 0 = 0):
 *   Bits 31:4  = Base address (16-byte aligned)
 *   Bit  3     = Prefetchable
 *   Bits 2:1   = Type: 00=32-bit, 10=64-bit
 *   Bit  0     = 0 (Memory Space indicator)
 *
 * I/O Space BAR (bit 0 = 1):
 *   Bits 31:2  = Base address (4-byte aligned)
 *   Bit  1     = Reserved
 *   Bit  0     = 1 (I/O Space indicator)
 * ======================================================================== */

#define PCI_BAR_IO_SPACE        0x01    /* Bit 0: 1 = I/O space, 0 = memory */
#define PCI_BAR_MEM_TYPE_MASK   0x06    /* Bits 2:1: memory type */
#define PCI_BAR_MEM_TYPE_32     0x00    /* 32-bit address */
#define PCI_BAR_MEM_TYPE_1M     0x02    /* Below 1 MB (legacy, rarely used) */
#define PCI_BAR_MEM_TYPE_64     0x04    /* 64-bit address */
#define PCI_BAR_MEM_PREFETCH    0x08    /* Bit 3: prefetchable */
#define PCI_BAR_MEM_ADDR_MASK   0xFFFFFFF0U  /* Bits 31:4 */
#define PCI_BAR_IO_ADDR_MASK    0xFFFFFFFCU  /* Bits 31:2 */

/* ========================================================================
 * PCI Class Codes (class / subclass)
 *
 * Class codes relevant to JARVIS Phase 3 drivers.
 * ======================================================================== */

/* Mass Storage Controllers (class 0x01) */
#define PCI_CLASS_STORAGE       0x01
#define PCI_SUBCLASS_SCSI       0x00
#define PCI_SUBCLASS_IDE        0x01
#define PCI_SUBCLASS_FLOPPY     0x02
#define PCI_SUBCLASS_IPI        0x03
#define PCI_SUBCLASS_RAID       0x04
#define PCI_SUBCLASS_ATA        0x05
#define PCI_SUBCLASS_SATA       0x06    /* AHCI = prog_if 0x01 */
#define PCI_SUBCLASS_SAS        0x07
#define PCI_SUBCLASS_NVM        0x08    /* NVMe = prog_if 0x02 */

/* Network Controllers (class 0x02) */
#define PCI_CLASS_NETWORK       0x02
#define PCI_SUBCLASS_ETHERNET   0x00

/* Display Controllers (class 0x03) */
#define PCI_CLASS_DISPLAY       0x03
#define PCI_SUBCLASS_VGA        0x00
#define PCI_SUBCLASS_XGA        0x01
#define PCI_SUBCLASS_3D         0x02

/* Bridge Devices (class 0x06) */
#define PCI_CLASS_BRIDGE        0x06
#define PCI_SUBCLASS_HOST       0x00
#define PCI_SUBCLASS_ISA        0x01
#define PCI_SUBCLASS_PCI2PCI    0x04

/* Serial Bus Controllers (class 0x0C) */
#define PCI_CLASS_SERIAL_BUS    0x0C
#define PCI_SUBCLASS_FIREWIRE   0x00
#define PCI_SUBCLASS_USB        0x03    /* prog_if: 0x00=UHCI, 0x10=OHCI,
                                                     0x20=EHCI, 0x30=xHCI */
#define PCI_SUBCLASS_SMBUS      0x05

/* Programming interface codes for specific controllers */
#define PCI_PROG_IF_AHCI        0x01    /* SATA AHCI mode */
#define PCI_PROG_IF_NVME        0x02    /* NVMe controller */
#define PCI_PROG_IF_UHCI        0x00    /* USB UHCI */
#define PCI_PROG_IF_OHCI        0x10    /* USB OHCI */
#define PCI_PROG_IF_EHCI        0x20    /* USB EHCI */
#define PCI_PROG_IF_XHCI        0x30    /* USB xHCI */

/* ========================================================================
 * Maximum PCI Bus Topology
 * ======================================================================== */

#define PCI_MAX_BUS         256
#define PCI_MAX_DEVICE      32
#define PCI_MAX_FUNCTION    8

/* ========================================================================
 * PCI Device Descriptor
 * ======================================================================== */

typedef struct {
    uint8_t  bus;               /* PCI bus number (0-255) */
    uint8_t  device;            /* Device number (0-31) */
    uint8_t  function;          /* Function number (0-7) */
    uint8_t  header_type;       /* Header type (0x00, 0x01, 0x02) */
    uint16_t vendor_id;         /* Vendor ID (0xFFFF = no device) */
    uint16_t device_id;         /* Device ID */
    uint8_t  class_code;        /* Class code */
    uint8_t  subclass;          /* Subclass code */
    uint8_t  prog_if;           /* Programming interface */
    uint8_t  revision_id;       /* Revision ID */
    uint32_t bar[6];            /* Base Address Registers 0-5 */
    uint8_t  interrupt_line;    /* IRQ number */
    uint8_t  interrupt_pin;     /* Interrupt pin (0=none, 1=INTA, ...) */
} pci_device_t;

/* ========================================================================
 * Function Prototypes
 * ======================================================================== */

/**
 * pci_config_read - Read a 32-bit value from PCI configuration space
 * @bus:     PCI bus number (0-255)
 * @device:  Device number (0-31)
 * @func:    Function number (0-7)
 * @offset:  Register offset (must be 4-byte aligned)
 *
 * Writes CONFIG_ADDRESS, reads CONFIG_DATA. Returns 32-bit register value.
 */
uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t func,
                          uint8_t offset);

/**
 * pci_config_write - Write a 32-bit value to PCI configuration space
 * @bus:     PCI bus number (0-255)
 * @device:  Device number (0-31)
 * @func:    Function number (0-7)
 * @offset:  Register offset (must be 4-byte aligned)
 * @value:   32-bit value to write
 */
void pci_config_write(uint8_t bus, uint8_t device, uint8_t func,
                       uint8_t offset, uint32_t value);

/**
 * pci_scan - Enumerate all PCI devices on all buses
 * @devices:     Output array to fill with discovered devices
 * @max_devices: Maximum number of entries in the array
 *
 * Scans all buses/devices/functions. Skips vendor_id == 0xFFFF.
 * Returns number of devices found.
 */
int pci_scan(pci_device_t *devices, int max_devices);

/**
 * pci_find_device - Find a device by class code and subclass
 * @class_code:  PCI class code to match
 * @subclass:    PCI subclass to match
 * @devices:     Array of previously scanned devices
 * @count:       Number of devices in the array
 *
 * Returns pointer to first matching device, or NULL if not found.
 */
pci_device_t *pci_find_device(uint8_t class_code, uint8_t subclass,
                               pci_device_t *devices, int count);

/**
 * pci_get_bar_address - Extract the base address from a BAR
 * @dev:       PCI device
 * @bar_index: BAR index (0-5)
 *
 * Masks off type/flag bits. For 64-bit BARs, combines BAR[n] and BAR[n+1].
 * Returns the physical base address.
 */
uint64_t pci_get_bar_address(pci_device_t *dev, int bar_index);

/**
 * pci_is_bar_64bit - Check if a BAR uses 64-bit addressing
 * @dev:       PCI device
 * @bar_index: BAR index (0-5)
 *
 * Returns non-zero if BAR type bits indicate 64-bit (type = 0x04).
 * If true, BAR[index+1] contains the upper 32 bits.
 */
int pci_is_bar_64bit(pci_device_t *dev, int bar_index);

/**
 * pci_is_bar_mmio - Check if a BAR is memory-mapped I/O
 * @dev:       PCI device
 * @bar_index: BAR index (0-5)
 *
 * Returns non-zero if BAR is MMIO (bit 0 = 0), 0 if I/O space.
 */
int pci_is_bar_mmio(pci_device_t *dev, int bar_index);

/**
 * pci_enable_bus_master - Enable bus mastering (DMA) for a device
 * @dev: PCI device
 *
 * Sets PCI_CMD_BUS_MASTER in the Command register.
 */
void pci_enable_bus_master(pci_device_t *dev);

#endif /* JARVIS_PCI_H */
