/*
 * JARVIS AI-OS - DWC2 USB Host Controller + HID Keyboard Driver
 *
 * Week 40: USB HID keyboard support via DWC2 OTG controller on BCM2711.
 * Uses slave mode (no DMA) for simplicity. Supports boot protocol keyboards.
 *
 * The DWC2 on Pi 4 connects to the USB-C port (OTG). USB-A ports use the
 * VL805 xHCI controller (not supported here). A USB-C OTG adapter is needed
 * to connect a USB-A keyboard to the USB-C port.
 *
 * Based on Linux drivers/usb/dwc2/hw.h and Synopsys DWC2 OTG databook.
 */

#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include <stdbool.h>
#include <sel4/sel4.h>

/* ================================================================
 * DWC2 USB Controller Base (BCM2711 ARM physical address)
 * ================================================================ */
#define USB_DWC2_BASE           0xFE980000u
#define USB_DWC2_MMIO_PAGES     3           /* 3 x 4KB pages: core regs + ch0 FIFO + ch1 FIFO */
#define USB_DWC2_VADDR_BASE     0x60A000u   /* After GENET at 0x604000-0x609FFF */

/* ================================================================
 * Core Global Registers
 * ================================================================ */
#define GOTGCTL                 0x000   /* OTG Control and Status */
#define GOTGINT                 0x004   /* OTG Interrupt */
#define GAHBCFG                 0x008   /* AHB Configuration */
#define GUSBCFG                 0x00C   /* USB Configuration */
#define GRSTCTL                 0x010   /* Reset Control */
#define GINTSTS                 0x014   /* Interrupt Status */
#define GINTMSK                 0x018   /* Interrupt Mask */
#define GRXSTSR                 0x01C   /* RX Status Read (no pop) */
#define GRXSTSP                 0x020   /* RX Status Pop */
#define GRXFSIZ                 0x024   /* RX FIFO Size */
#define GNPTXFSIZ               0x028   /* Non-Periodic TX FIFO Size */
#define GNPTXSTS                0x02C   /* Non-Periodic TX FIFO/Queue Status */
#define GSNPSID                 0x040   /* Synopsys ID (silicon version) */
#define GHWCFG1                 0x044   /* HW Config 1 */
#define GHWCFG2                 0x048   /* HW Config 2 */
#define GHWCFG3                 0x04C   /* HW Config 3 */
#define GHWCFG4                 0x050   /* HW Config 4 */
#define GLPMCFG                 0x054   /* LPM Configuration */
#define GDFIFOCFG               0x05C   /* DFIFO Configuration */
#define HPTXFSIZ                0x100   /* Host Periodic TX FIFO Size */
#define PCGCTL                  0xE00   /* Power and Clock Gating Control */

/* ================================================================
 * GAHBCFG Bits
 * ================================================================ */
#define GAHBCFG_GLBL_INTR_EN   BIT(0)
#define GAHBCFG_HBSTLEN_SHIFT  1
#define GAHBCFG_HBSTLEN_MASK   (0xFu << 1)
#define GAHBCFG_DMA_EN         BIT(5)
#define GAHBCFG_HBSTLEN_INCR4  3

/* ================================================================
 * GUSBCFG Bits
 * ================================================================ */
#define GUSBCFG_TOUTCAL_MASK    (0x7u << 0)
#define GUSBCFG_PHYIF16         BIT(3)
#define GUSBCFG_ULPI_UTMI_SEL   BIT(4)
#define GUSBCFG_PHYSEL          BIT(6)
#define GUSBCFG_USBTRDTIM_SHIFT 10
#define GUSBCFG_USBTRDTIM_MASK  (0xFu << 10)
#define GUSBCFG_FORCEHOSTMODE   BIT(29)
#define GUSBCFG_FORCEDEVMODE    BIT(30)

/* ================================================================
 * GRSTCTL Bits
 * ================================================================ */
#define GRSTCTL_CSFTRST         BIT(0)
#define GRSTCTL_HSFTRST         BIT(1)
#define GRSTCTL_FRMCNTRRST      BIT(2)
#define GRSTCTL_RXFFLSH         BIT(4)
#define GRSTCTL_TXFFLSH         BIT(5)
#define GRSTCTL_TXFNUM_SHIFT    6
#define GRSTCTL_TXFNUM_MASK     (0x1Fu << 6)
#define GRSTCTL_AHBIDLE         BIT(31)

/* ================================================================
 * GINTSTS / GINTMSK Bits (host-relevant)
 * ================================================================ */
#define GINTSTS_CURMODE_HOST    BIT(0)
#define GINTSTS_MODEMIS         BIT(1)
#define GINTSTS_SOF             BIT(3)
#define GINTSTS_RXFLVL          BIT(4)
#define GINTSTS_NPTXFEMP        BIT(5)
#define GINTSTS_PRTINT          BIT(24)
#define GINTSTS_HCHINT          BIT(25)
#define GINTSTS_PTXFEMP         BIT(26)
#define GINTSTS_DISCONNINT      BIT(29)

/* ================================================================
 * GRXSTSP Fields (Slave Mode RX FIFO Pop)
 * ================================================================ */
#define GRXSTS_HCHNUM_MASK      (0xFu << 0)
#define GRXSTS_HCHNUM_SHIFT     0
#define GRXSTS_BYTECNT_MASK     (0x7FFu << 4)
#define GRXSTS_BYTECNT_SHIFT    4
#define GRXSTS_DPID_MASK        (0x3u << 15)
#define GRXSTS_DPID_SHIFT       15
#define GRXSTS_PKTSTS_MASK      (0xFu << 17)
#define GRXSTS_PKTSTS_SHIFT     17
#define GRXSTS_PKTSTS_HCHIN             2
#define GRXSTS_PKTSTS_HCHIN_XFER_COMP  3
#define GRXSTS_PKTSTS_HCHHALTED         7

/* ================================================================
 * FIFO Size Register Fields
 * ================================================================ */
#define FIFOSIZE_DEPTH_SHIFT        16
#define FIFOSIZE_DEPTH_MASK         (0xFFFFu << 16)
#define FIFOSIZE_STARTADDR_SHIFT    0
#define FIFOSIZE_STARTADDR_MASK     (0xFFFFu << 0)

/* ================================================================
 * Data FIFO (Slave Mode) - per channel
 * ================================================================ */
#define EPFIFO(ch)              (0x1000 + ((ch) * 0x1000))

/* ================================================================
 * Host Mode Registers
 * ================================================================ */
#define HCFG                    0x400
#define HFIR                    0x404
#define HFNUM                   0x408
#define HPTXSTS                 0x410
#define HAINT                   0x414
#define HAINTMSK                0x418
#define HPRT0                   0x440

/* ================================================================
 * HCFG Bits
 * ================================================================ */
#define HCFG_FSLSPCLKSEL_SHIFT  0
#define HCFG_FSLSPCLKSEL_MASK   (0x3u << 0)
#define HCFG_FSLSSUPP           BIT(2)

/* ================================================================
 * HPRT0 Bits (Host Port Control/Status)
 *
 * IMPORTANT: Bits 1, 3, 5 are W1C (write-1-to-clear).
 * When modifying HPRT0, ALWAYS mask out W1C bits first:
 *   hprt0 &= ~(HPRT0_CONNDET | HPRT0_ENACHG | HPRT0_OVRCURRCHG);
 * ================================================================ */
#define HPRT0_CONNSTS           BIT(0)      /* Port connect status (RO) */
#define HPRT0_CONNDET           BIT(1)      /* Connect detected (W1C) */
#define HPRT0_ENA               BIT(2)      /* Port enable */
#define HPRT0_ENACHG            BIT(3)      /* Port enable change (W1C) */
#define HPRT0_OCA               BIT(4)      /* Over-current active */
#define HPRT0_OVRCURRCHG        BIT(5)      /* Over-current change (W1C) */
#define HPRT0_RES               BIT(6)      /* Resume */
#define HPRT0_SUSP              BIT(7)      /* Suspend */
#define HPRT0_RST               BIT(8)      /* Port reset */
#define HPRT0_PWR               BIT(12)     /* Port power */
#define HPRT0_TSTCTL_SHIFT      13
#define HPRT0_TSTCTL_MASK       (0xFu << 13)
#define HPRT0_SPD_SHIFT         17
#define HPRT0_SPD_MASK          (0x3u << 17)

/* W1C bitmask for safe read-modify-write */
#define HPRT0_W1C_MASK          (HPRT0_CONNDET | HPRT0_ENACHG | HPRT0_OVRCURRCHG)

/* Port speed values */
#define HPRT0_SPD_HIGH_SPEED    0
#define HPRT0_SPD_FULL_SPEED    1
#define HPRT0_SPD_LOW_SPEED     2

/* ================================================================
 * Host Channel Registers (per channel, stride=0x20)
 * ================================================================ */
#define HCCHAR(ch)              (0x500 + 0x20 * (ch))
#define HCSPLT(ch)              (0x504 + 0x20 * (ch))
#define HCINT(ch)               (0x508 + 0x20 * (ch))
#define HCINTMSK(ch)            (0x50C + 0x20 * (ch))
#define HCTSIZ(ch)              (0x510 + 0x20 * (ch))
#define HCDMA(ch)               (0x514 + 0x20 * (ch))

/* ================================================================
 * HCCHAR Bits (Host Channel Characteristics)
 * ================================================================ */
#define HCCHAR_MPS_MASK         (0x7FFu << 0)
#define HCCHAR_MPS_SHIFT        0
#define HCCHAR_EPNUM_SHIFT      11
#define HCCHAR_EPNUM_MASK       (0xFu << 11)
#define HCCHAR_EPDIR            BIT(15)     /* 1=IN, 0=OUT */
#define HCCHAR_LSPDDEV          BIT(17)     /* Low-speed device */
#define HCCHAR_EPTYPE_SHIFT     18
#define HCCHAR_EPTYPE_MASK      (0x3u << 18)
#define HCCHAR_MULTICNT_SHIFT   20
#define HCCHAR_MULTICNT_MASK    (0x3u << 20)
#define HCCHAR_DEVADDR_SHIFT    22
#define HCCHAR_DEVADDR_MASK     (0x7Fu << 22)
#define HCCHAR_ODDFRM           BIT(29)
#define HCCHAR_CHDIS            BIT(30)
#define HCCHAR_CHENA            BIT(31)

/* EP type values for HCCHAR EPTYPE field */
#define USB_EP_TYPE_CONTROL     0
#define USB_EP_TYPE_ISO         1
#define USB_EP_TYPE_BULK        2
#define USB_EP_TYPE_INTERRUPT   3

/* ================================================================
 * HCTSIZ Bits (Host Channel Transfer Size)
 * ================================================================ */
#define TSIZ_XFERSIZE_MASK      (0x7FFFFu << 0)
#define TSIZ_XFERSIZE_SHIFT     0
#define TSIZ_PKTCNT_SHIFT       19
#define TSIZ_PKTCNT_MASK        (0x3FFu << 19)
#define TSIZ_SC_MC_PID_SHIFT    29
#define TSIZ_SC_MC_PID_MASK     (0x3u << 29)

/* PID values */
#define TSIZ_SC_MC_PID_DATA0    0
#define TSIZ_SC_MC_PID_DATA2    1
#define TSIZ_SC_MC_PID_DATA1    2
#define TSIZ_SC_MC_PID_MDATA    3
#define TSIZ_SC_MC_PID_SETUP    3

/* ================================================================
 * HCINT / HCINTMSK Bits (Host Channel Interrupt)
 * ================================================================ */
#define HCINTMSK_XFERCOMPL      BIT(0)
#define HCINTMSK_CHHLTD         BIT(1)
#define HCINTMSK_AHBERR         BIT(2)
#define HCINTMSK_STALL          BIT(3)
#define HCINTMSK_NAK            BIT(4)
#define HCINTMSK_ACK            BIT(5)
#define HCINTMSK_NYET           BIT(6)
#define HCINTMSK_XACTERR        BIT(7)
#define HCINTMSK_BBLERR         BIT(8)
#define HCINTMSK_FRMOVRUN       BIT(9)
#define HCINTMSK_DATATGLERR     BIT(10)

/* ================================================================
 * USB Standard Request Constants
 * ================================================================ */

/* bmRequestType direction */
#define USB_DIR_OUT             0x00
#define USB_DIR_IN              0x80

/* bmRequestType type */
#define USB_TYPE_STANDARD       0x00
#define USB_TYPE_CLASS          0x20

/* bmRequestType recipient */
#define USB_RECIP_DEVICE        0x00
#define USB_RECIP_INTERFACE     0x01

/* Standard requests (bRequest) */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_SET_CONFIGURATION 0x09

/* Descriptor types (wValue high byte) */
#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIGURATION  0x02
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05

/* HID class requests */
#define HID_REQ_SET_PROTOCOL    0x0B
#define HID_REQ_SET_IDLE        0x0A

/* HID interface class/subclass/protocol */
#define USB_CLASS_HID           0x03
#define USB_SUBCLASS_BOOT       0x01
#define USB_PROTOCOL_KEYBOARD   0x01

/* ================================================================
 * USB Standard Descriptors
 * ================================================================ */

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_device_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_config_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_interface_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_endpoint_descriptor_t;

/* USB Setup Packet (8 bytes) */
typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

/* ================================================================
 * HID Boot Protocol Keyboard Report (8 bytes)
 * ================================================================ */

typedef struct __attribute__((packed)) {
    uint8_t modifier;       /* Bit0=LCtrl,1=LShift,2=LAlt,3=LGui,
                               4=RCtrl,5=RShift,6=RAlt,7=RGui */
    uint8_t reserved;
    uint8_t keys[6];        /* Up to 6 simultaneous scan codes */
} hid_keyboard_report_t;

/* Modifier key bits */
#define MOD_LCTRL               BIT(0)
#define MOD_LSHIFT              BIT(1)
#define MOD_LALT                BIT(2)
#define MOD_LGUI                BIT(3)
#define MOD_RCTRL               BIT(4)
#define MOD_RSHIFT              BIT(5)
#define MOD_RALT                BIT(6)
#define MOD_RGUI                BIT(7)

/* ================================================================
 * USB HID Driver State
 * ================================================================ */

typedef struct {
    bool     initialized;
    bool     device_connected;
    uint8_t  device_address;    /* Assigned USB address (1-127) */
    uint8_t  max_packet_size;   /* EP0 max packet size */
    uint8_t  hid_ep_addr;       /* Interrupt IN endpoint address */
    uint8_t  hid_ep_interval;   /* Polling interval (ms) */
    uint16_t hid_ep_max_pkt;    /* Interrupt EP max packet size */
    uint8_t  hid_iface_num;     /* HID interface number */
    uint8_t  port_speed;        /* 0=HS, 1=FS, 2=LS */
    uint8_t  next_data_toggle;  /* DATA0/DATA1 toggle for interrupt EP */
} usb_hid_state_t;

/* ================================================================
 * Public API
 * ================================================================ */

/* Initialize DWC2 USB controller and detect attached device.
 * Requires: slot_alloc_init() already called.
 * Maps DWC2 registers, performs core reset, powers port. */
bool usb_hid_init(seL4_BootInfo *bi);

/* Enumerate attached USB device and find HID keyboard.
 * Performs: GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION, SET_PROTOCOL.
 * Returns true if a boot protocol keyboard was found and configured. */
bool usb_hid_enumerate(void);

/* Poll keyboard for new report (non-blocking).
 * Returns true if new 8-byte report received, false if NAK or error. */
bool usb_hid_poll_keyboard(hid_keyboard_report_t *report);

/* Convert HID scan code to ASCII character.
 * Returns 0 for non-printable or unmapped keys. */
char usb_hid_scancode_to_ascii(uint8_t scancode, uint8_t modifier);

/* Check if a USB device is connected and enumerated. */
bool usb_hid_device_connected(void);

/* ================================================================
 * Special Key Codes (non-ASCII, returned by usb_hid_get_key())
 * ================================================================ */

#define USB_KEY_NONE        0
#define USB_KEY_UP          0x80
#define USB_KEY_DOWN        0x81
#define USB_KEY_LEFT        0x82
#define USB_KEY_RIGHT       0x83
#define USB_KEY_HOME        0x84
#define USB_KEY_END         0x85
#define USB_KEY_PAGEUP      0x86
#define USB_KEY_PAGEDOWN    0x87
#define USB_KEY_INSERT      0x88
#define USB_KEY_DELETE      0x89
#define USB_KEY_F1          0x90  /* F1-F12 = 0x90-0x9B */
#define USB_KEY_F2          0x91
#define USB_KEY_F3          0x92
#define USB_KEY_F4          0x93
#define USB_KEY_F5          0x94
#define USB_KEY_F6          0x95
#define USB_KEY_F7          0x96
#define USB_KEY_F8          0x97
#define USB_KEY_F9          0x98
#define USB_KEY_F10         0x99
#define USB_KEY_F11         0x9A
#define USB_KEY_F12         0x9B

/* Extended key conversion with debounce.
 * Compares current report to previous report to detect NEW key presses only.
 * Returns: ASCII char for printable keys, control char for Ctrl+letter,
 *          USB_KEY_* for special keys, or 0 if no new key. */
int usb_hid_get_key(const hid_keyboard_report_t *report,
                    const hid_keyboard_report_t *prev_report);

/* Get USB device status string for "usb status" command.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL). */
int usb_hid_get_status(char *output, uint32_t output_size);

/* Reset Caps Lock state (for testing) */
void usb_hid_reset_caps_lock(void);

#endif /* USB_HID_H */
