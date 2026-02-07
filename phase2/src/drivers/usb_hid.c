/*
 * JARVIS AI-OS - DWC2 USB Host Controller + HID Keyboard Driver
 *
 * Week 40: USB HID keyboard support via DWC2 OTG controller on BCM2711.
 *
 * Architecture:
 *   - DWC2 at paddr 0xFE980000 (main peripheral range, needs own untyped search)
 *   - 1 x 4KB page mapped to vaddr 0x60A000 (after GENET at 0x604000-0x609FFF)
 *   - Slave mode (no DMA) - data transferred via FIFO registers
 *   - Host channel 0 for control transfers, channel 1 for interrupt IN
 *   - Polled operation (no IRQ handler needed in seL4 rootserver)
 *   - Boot protocol keyboard: 8-byte reports via interrupt IN endpoint
 *
 * The DWC2 on Pi 4 connects to the USB-C port (OTG mode).
 * USB-A ports use the VL805 xHCI controller (not supported here).
 * A USB-C OTG adapter is needed to connect a keyboard.
 */

#include "usb_hid.h"
#include "uart_pl011.h"
#include "slot_alloc.h"
#include "bcm2711_timer.h"

#include <sel4/sel4.h>
#include <sel4runtime.h>
#include <string.h>

/* ================================================================
 * Internal State
 * ================================================================ */

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

#ifndef seL4_ARM_Device_VMAttributes
#define seL4_ARM_Device_VMAttributes 0
#endif

#define PAGE_SIZE_4K    0x1000u
#define PAGE_BITS_4K    12

/* MMIO base pointer */
static volatile uint8_t *usb_mmio_base = NULL;

/* Driver state */
static usb_hid_state_t hid_state;

/* Transfer buffer (static, 256 bytes - enough for descriptors) */
static uint8_t usb_xfer_buf[256] __attribute__((aligned(4)));

/* Number of host channels (read from hardware) */
#define USB_MAX_CHANNELS    16

/* Timeout for register polls (microseconds) */
#define USB_RESET_TIMEOUT_US    100000  /* 100ms */
#define USB_XFER_TIMEOUT_US     500000  /* 500ms */

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

static void debug_hex32(uint32_t val)
{
    char buf[9];
    const char *hex = "0123456789abcdef";
    buf[8] = '\0';
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xf];
        val >>= 4;
    }
    debug_puts("0x");
    debug_puts(buf);
}

static void debug_dec(uint32_t val)
{
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    if (val == 0) {
        buf[--i] = '0';
    } else {
        while (val > 0 && i > 0) {
            buf[--i] = '0' + (val % 10);
            val /= 10;
        }
    }
    debug_puts(&buf[i]);
}

/* ================================================================
 * MMIO Accessors
 * ================================================================ */

static inline uint32_t usb_reg_read(uint32_t offset)
{
    uint32_t val = *(volatile uint32_t *)(usb_mmio_base + offset);
    __asm__ volatile("dsb sy" ::: "memory");
    return val;
}

static inline void usb_reg_write(uint32_t offset, uint32_t val)
{
    *(volatile uint32_t *)(usb_mmio_base + offset) = val;
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ================================================================
 * Delay (via BCM2711 System Timer)
 * ================================================================ */

static void delay_us(uint32_t us)
{
    if (!systimer_is_initialized()) {
        /* Fallback: spin loop (~1us per iteration at 1.8GHz, approximate) */
        for (volatile uint32_t i = 0; i < us * 2; i++);
        return;
    }
    uint64_t start = systimer_read();
    while ((systimer_read() - start) < (uint64_t)us) {
        /* spin */
    }
}

static void delay_ms(uint32_t ms)
{
    delay_us(ms * 1000);
}

/* ================================================================
 * MMIO Mapping
 *
 * Map DWC2 register pages using the shared device mapper from
 * uart_pl011.c. The DWC2 at 0xFE980000 is in the same device untyped
 * (0xFE000000, 16MB) as UART and EMMC. The shared device mapper
 * tracks the forward-only watermark correctly, so we just call
 * uart_device_map_page() for each page (same approach as EMMC).
 * ================================================================ */

static bool usb_map_mmio(seL4_BootInfo *bi)
{
    (void)bi;  /* uart_device_map_page uses its own cached bootinfo */

    for (int i = 0; i < USB_DWC2_MMIO_PAGES; i++) {
        seL4_Word paddr = USB_DWC2_BASE + (i * PAGE_SIZE_4K);
        seL4_Word vaddr = USB_DWC2_VADDR_BASE + (i * PAGE_SIZE_4K);
        volatile uint32_t *page_out = NULL;

        if (!uart_device_map_page(paddr, vaddr, &page_out)) {
            debug_puts("[USB] ERROR: Failed to map page ");
            debug_dec(i);
            debug_puts(" at paddr=");
            debug_hex(paddr);
            debug_puts("\n");
            return false;
        }

        debug_puts("[USB] Mapped page ");
        debug_dec(i);
        debug_puts(": paddr=");
        debug_hex(paddr);
        debug_puts(" -> vaddr=");
        debug_hex(vaddr);
        debug_puts("\n");
    }

    usb_mmio_base = (volatile uint8_t *)USB_DWC2_VADDR_BASE;

    debug_puts("[USB] MMIO mapped: vbase=");
    debug_hex(USB_DWC2_VADDR_BASE);
    debug_puts("\n");

    return true;
}

/* ================================================================
 * HPRT0 Safe Read-Modify-Write
 *
 * HPRT0 has W1C bits that must be masked to avoid accidental clears.
 * ================================================================ */

static uint32_t hprt0_read_safe(void)
{
    return usb_reg_read(HPRT0);
}

static void hprt0_write_safe(uint32_t val)
{
    /* Mask out W1C bits to prevent accidental clear */
    val &= ~HPRT0_W1C_MASK;
    usb_reg_write(HPRT0, val);
}

/* ================================================================
 * DWC2 Core Initialization
 * ================================================================ */

static bool dwc2_core_init(void)
{
    /* Step 1: Read and verify silicon ID */
    uint32_t snpsid = usb_reg_read(GSNPSID);
    debug_puts("[USB] GSNPSID=");
    debug_hex32(snpsid);
    debug_puts("\n");

    /* DWC2 OTG cores report 0x4F54xxxx ("OT" in ASCII + version) */
    if ((snpsid & 0xFFFF0000u) != 0x4F540000u) {
        debug_puts("[USB] ERROR: Not a DWC2 OTG core (expected 0x4F54xxxx)\n");
        return false;
    }

    /* Step 2: Core soft reset */
    debug_puts("[USB] Waiting for AHB idle...\n");
    uint64_t t0 = systimer_read();
    while (!(usb_reg_read(GRSTCTL) & GRSTCTL_AHBIDLE)) {
        if (systimer_elapsed_us(t0) > USB_RESET_TIMEOUT_US) {
            debug_puts("[USB] ERROR: AHB idle timeout\n");
            return false;
        }
    }

    debug_puts("[USB] Asserting core soft reset...\n");
    usb_reg_write(GRSTCTL, GRSTCTL_CSFTRST);

    t0 = systimer_read();
    while (usb_reg_read(GRSTCTL) & GRSTCTL_CSFTRST) {
        if (systimer_elapsed_us(t0) > USB_RESET_TIMEOUT_US) {
            debug_puts("[USB] ERROR: Core reset timeout\n");
            return false;
        }
    }

    /* Wait for AHB idle after reset */
    t0 = systimer_read();
    while (!(usb_reg_read(GRSTCTL) & GRSTCTL_AHBIDLE)) {
        if (systimer_elapsed_us(t0) > USB_RESET_TIMEOUT_US) {
            debug_puts("[USB] ERROR: AHB idle timeout post-reset\n");
            return false;
        }
    }

    debug_puts("[USB] Core reset complete\n");

    /* Step 3: Force host mode */
    uint32_t usbcfg = usb_reg_read(GUSBCFG);
    usbcfg &= ~GUSBCFG_FORCEDEVMODE;
    usbcfg |= GUSBCFG_FORCEHOSTMODE;
    /* PHY config: BCM2711 internal HS PHY, UTMI+ 8-bit */
    usbcfg &= ~GUSBCFG_ULPI_UTMI_SEL;     /* Select UTMI+ */
    usbcfg &= ~GUSBCFG_PHYIF16;            /* 8-bit interface */
    usbcfg &= ~GUSBCFG_PHYSEL;             /* HS PHY */
    /* USB turnaround time = 5 (for 8-bit UTMI+) */
    usbcfg &= ~GUSBCFG_USBTRDTIM_MASK;
    usbcfg |= (5u << GUSBCFG_USBTRDTIM_SHIFT);
    usb_reg_write(GUSBCFG, usbcfg);

    /* Must wait 25ms for mode switch to take effect */
    delay_ms(25);

    debug_puts("[USB] Forced host mode\n");

    /* Step 4: AHB configuration - slave mode (no DMA) */
    uint32_t ahbcfg = usb_reg_read(GAHBCFG);
    ahbcfg &= ~GAHBCFG_DMA_EN;
    ahbcfg |= GAHBCFG_GLBL_INTR_EN;
    ahbcfg &= ~GAHBCFG_HBSTLEN_MASK;
    ahbcfg |= (GAHBCFG_HBSTLEN_INCR4 << GAHBCFG_HBSTLEN_SHIFT);
    usb_reg_write(GAHBCFG, ahbcfg);

    /* Step 5: Host configuration */
    uint32_t hcfg = usb_reg_read(HCFG);
    hcfg |= HCFG_FSLSSUPP;
    hcfg &= ~HCFG_FSLSPCLKSEL_MASK;
    hcfg |= (1u << HCFG_FSLSPCLKSEL_SHIFT);    /* 48 MHz for FS */
    usb_reg_write(HCFG, hcfg);

    /* Step 6: FIFO sizing */
    usb_reg_write(GRXFSIZ, 256);    /* RX: 256 words = 1024 bytes */
    usb_reg_write(GNPTXFSIZ, (256u << FIFOSIZE_DEPTH_SHIFT) |
                              (256u << FIFOSIZE_STARTADDR_SHIFT));
    usb_reg_write(HPTXFSIZ, (256u << FIFOSIZE_DEPTH_SHIFT) |
                             (512u << FIFOSIZE_STARTADDR_SHIFT));

    /* Step 7: Flush all FIFOs */
    usb_reg_write(GRSTCTL, GRSTCTL_TXFFLSH | (0x10u << GRSTCTL_TXFNUM_SHIFT));
    t0 = systimer_read();
    while (usb_reg_read(GRSTCTL) & GRSTCTL_TXFFLSH) {
        if (systimer_elapsed_us(t0) > USB_RESET_TIMEOUT_US) {
            debug_puts("[USB] WARNING: TX FIFO flush timeout\n");
            break;
        }
    }

    usb_reg_write(GRSTCTL, GRSTCTL_RXFFLSH);
    t0 = systimer_read();
    while (usb_reg_read(GRSTCTL) & GRSTCTL_RXFFLSH) {
        if (systimer_elapsed_us(t0) > USB_RESET_TIMEOUT_US) {
            debug_puts("[USB] WARNING: RX FIFO flush timeout\n");
            break;
        }
    }

    debug_puts("[USB] FIFOs flushed\n");

    /* Step 8: Halt all host channels */
    for (int ch = 0; ch < USB_MAX_CHANNELS; ch++) {
        uint32_t hcchar = usb_reg_read(HCCHAR(ch));
        if (hcchar & HCCHAR_CHENA) {
            hcchar |= HCCHAR_CHDIS;
            hcchar &= ~HCCHAR_EPDIR;
            usb_reg_write(HCCHAR(ch), hcchar);

            t0 = systimer_read();
            while (usb_reg_read(HCCHAR(ch)) & HCCHAR_CHENA) {
                if (systimer_elapsed_us(t0) > USB_RESET_TIMEOUT_US) {
                    break;
                }
            }
        }
        usb_reg_write(HCINT(ch), 0xFFFFFFFF);
    }

    debug_puts("[USB] All channels halted\n");

    /* Step 9: Clear all pending interrupts */
    usb_reg_write(GINTSTS, 0xFFFFFFFF);

    /* Enable host-mode interrupts we care about */
    uint32_t intmsk = GINTSTS_RXFLVL
                    | GINTSTS_HCHINT
                    | GINTSTS_PRTINT
                    | GINTSTS_DISCONNINT;
    usb_reg_write(GINTMSK, intmsk);

    /* Enable channel 0 and 1 in HAINTMSK */
    usb_reg_write(HAINTMSK, BIT(0) | BIT(1));

    debug_puts("[USB] Core init complete\n");
    return true;
}

/* ================================================================
 * Port Power and Reset
 * ================================================================ */

static bool dwc2_port_power_on(void)
{
    uint32_t hprt0 = hprt0_read_safe();
    hprt0 &= ~HPRT0_W1C_MASK;
    hprt0 |= HPRT0_PWR;
    usb_reg_write(HPRT0, hprt0);

    /* Wait for power stabilization */
    delay_ms(20);

    debug_puts("[USB] Port power ON\n");
    return true;
}

static bool dwc2_port_reset(void)
{
    /* Check if device is connected */
    uint32_t hprt0 = hprt0_read_safe();
    if (!(hprt0 & HPRT0_CONNSTS)) {
        debug_puts("[USB] No device connected (HPRT0=");
        debug_hex32(hprt0);
        debug_puts(")\n");
        return false;
    }

    debug_puts("[USB] Device detected, asserting port reset...\n");

    /* Assert reset */
    hprt0 = hprt0_read_safe();
    hprt0 &= ~HPRT0_W1C_MASK;
    hprt0 |= HPRT0_RST;
    usb_reg_write(HPRT0, hprt0);

    /* Hold reset for 50ms */
    delay_ms(50);

    /* De-assert reset */
    hprt0 = hprt0_read_safe();
    hprt0 &= ~HPRT0_W1C_MASK;
    hprt0 &= ~HPRT0_RST;
    usb_reg_write(HPRT0, hprt0);

    /* Wait for reset recovery */
    delay_ms(20);

    /* Read port status */
    hprt0 = hprt0_read_safe();
    debug_puts("[USB] Post-reset HPRT0=");
    debug_hex32(hprt0);
    debug_puts("\n");

    if (!(hprt0 & HPRT0_ENA)) {
        debug_puts("[USB] WARNING: Port not enabled after reset\n");
        /* Some devices need longer - try again */
        delay_ms(50);
        hprt0 = hprt0_read_safe();
        if (!(hprt0 & HPRT0_ENA)) {
            debug_puts("[USB] Port still not enabled\n");
            return false;
        }
    }

    /* Read speed */
    hid_state.port_speed = (hprt0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
    debug_puts("[USB] Port speed: ");
    switch (hid_state.port_speed) {
        case HPRT0_SPD_HIGH_SPEED: debug_puts("High Speed (480 Mbps)"); break;
        case HPRT0_SPD_FULL_SPEED: debug_puts("Full Speed (12 Mbps)"); break;
        case HPRT0_SPD_LOW_SPEED:  debug_puts("Low Speed (1.5 Mbps)"); break;
        default: debug_puts("Unknown"); break;
    }
    debug_puts("\n");

    /* Clear connect detect W1C bit */
    hprt0 = hprt0_read_safe();
    hprt0 &= ~(HPRT0_ENACHG | HPRT0_OVRCURRCHG);
    hprt0 |= HPRT0_CONNDET;    /* Write 1 to clear */
    usb_reg_write(HPRT0, hprt0);

    hid_state.device_connected = true;
    return true;
}

/* ================================================================
 * Host Channel Transfer Helpers (Slave Mode)
 * ================================================================ */

/* Wait for channel transfer to complete. Returns HCINT value. */
static uint32_t usb_wait_channel(int ch, uint32_t timeout_us)
{
    uint64_t t0 = systimer_read();
    uint32_t hcint;

    while (1) {
        hcint = usb_reg_read(HCINT(ch));
        if (hcint & (HCINTMSK_XFERCOMPL | HCINTMSK_CHHLTD |
                     HCINTMSK_STALL | HCINTMSK_XACTERR |
                     HCINTMSK_BBLERR | HCINTMSK_NAK)) {
            break;
        }
        if (systimer_elapsed_us(t0) > timeout_us) {
            debug_puts("[USB] Channel ");
            debug_dec(ch);
            debug_puts(" timeout, HCINT=");
            debug_hex32(hcint);
            debug_puts("\n");
            return 0;
        }
    }
    return hcint;
}

/* Read data from RX FIFO (slave mode). Returns bytes read. */
static int usb_read_rx_fifo(uint8_t *buf, int max_len)
{
    uint64_t t0 = systimer_read();

    /* Wait for RX FIFO non-empty */
    while (!(usb_reg_read(GINTSTS) & GINTSTS_RXFLVL)) {
        if (systimer_elapsed_us(t0) > USB_XFER_TIMEOUT_US) {
            debug_puts("[USB] RX FIFO empty timeout\n");
            return -1;
        }
    }

    /* Pop RX status */
    uint32_t grxsts = usb_reg_read(GRXSTSP);
    uint32_t bcnt = (grxsts & GRXSTS_BYTECNT_MASK) >> GRXSTS_BYTECNT_SHIFT;
    uint32_t pktsts = (grxsts & GRXSTS_PKTSTS_MASK) >> GRXSTS_PKTSTS_SHIFT;

    if (pktsts == GRXSTS_PKTSTS_HCHIN) {
        /* IN data packet received - read from FIFO */
        uint32_t words = (bcnt + 3) / 4;
        int bytes_read = (int)(bcnt > (uint32_t)max_len ? (uint32_t)max_len : bcnt);

        /* Read full words from FIFO */
        uint32_t temp[64];  /* Max 256 bytes */
        uint32_t read_words = words > 64 ? 64 : words;
        for (uint32_t i = 0; i < read_words; i++) {
            temp[i] = usb_reg_read(EPFIFO(0));
        }
        memcpy(buf, temp, bytes_read);

        return bytes_read;
    } else if (pktsts == GRXSTS_PKTSTS_HCHIN_XFER_COMP) {
        /* Transfer complete notification - no data to read */
        return 0;
    } else if (pktsts == GRXSTS_PKTSTS_HCHHALTED) {
        return 0;
    }

    /* Drain any remaining FIFO data for other packet statuses */
    if (bcnt > 0) {
        uint32_t words = (bcnt + 3) / 4;
        for (uint32_t i = 0; i < words; i++) {
            (void)usb_reg_read(EPFIFO(0));
        }
    }

    return 0;
}

/* ================================================================
 * USB Control Transfer (SETUP - DATA - STATUS)
 *
 * Uses host channel 0 for all control transfers.
 * Slave mode: data goes through FIFO registers.
 * ================================================================ */

static bool usb_control_transfer(uint8_t dev_addr, usb_setup_packet_t *setup,
                                  uint8_t *data, uint16_t data_len, bool is_in)
{
    uint32_t hcchar, hctsiz, hcint;
    uint8_t mps = (hid_state.max_packet_size > 0) ? hid_state.max_packet_size : 8;

    /* ---- Phase 1: SETUP (8-byte setup packet, OUT, PID=SETUP) ---- */

    /* Configure channel 0 for SETUP */
    hcchar = ((uint32_t)dev_addr << HCCHAR_DEVADDR_SHIFT)
           | (0u << HCCHAR_EPNUM_SHIFT)             /* EP 0 */
           | (USB_EP_TYPE_CONTROL << HCCHAR_EPTYPE_SHIFT)
           | ((uint32_t)mps & HCCHAR_MPS_MASK)
           | (1u << HCCHAR_MULTICNT_SHIFT);          /* 1 transaction */
    /* EPDIR = 0 (OUT) for SETUP */
    if (hid_state.port_speed == HPRT0_SPD_LOW_SPEED) {
        hcchar |= HCCHAR_LSPDDEV;
    }
    usb_reg_write(HCCHAR(0), hcchar);

    /* Transfer size: 8 bytes, 1 packet, PID=SETUP */
    hctsiz = (8u << TSIZ_XFERSIZE_SHIFT)
           | (1u << TSIZ_PKTCNT_SHIFT)
           | ((uint32_t)TSIZ_SC_MC_PID_SETUP << TSIZ_SC_MC_PID_SHIFT);
    usb_reg_write(HCTSIZ(0), hctsiz);

    /* Enable channel interrupt mask */
    usb_reg_write(HCINTMSK(0), HCINTMSK_XFERCOMPL | HCINTMSK_CHHLTD
                              | HCINTMSK_STALL | HCINTMSK_NAK
                              | HCINTMSK_XACTERR);

    /* Clear pending interrupts */
    usb_reg_write(HCINT(0), 0xFFFFFFFF);

    /* Start transfer */
    hcchar = usb_reg_read(HCCHAR(0));
    hcchar |= HCCHAR_CHENA;
    usb_reg_write(HCCHAR(0), hcchar);

    /* Write 8-byte setup packet to TX FIFO (2 words) */
    uint32_t setup_words[2];
    memcpy(setup_words, setup, 8);
    usb_reg_write(EPFIFO(0), setup_words[0]);
    usb_reg_write(EPFIFO(0), setup_words[1]);

    /* Wait for SETUP phase completion */
    hcint = usb_wait_channel(0, USB_XFER_TIMEOUT_US);
    if (!hcint || (hcint & (HCINTMSK_STALL | HCINTMSK_XACTERR))) {
        debug_puts("[USB] SETUP phase failed, HCINT=");
        debug_hex32(hcint);
        debug_puts("\n");
        usb_reg_write(HCINT(0), 0xFFFFFFFF);
        return false;
    }
    usb_reg_write(HCINT(0), 0xFFFFFFFF);

    /* ---- Phase 2: DATA (optional) ---- */
    if (data_len > 0 && data != NULL) {
        if (is_in) {
            /* DATA IN: read response from device */
            hcchar = usb_reg_read(HCCHAR(0));
            hcchar |= HCCHAR_EPDIR;        /* IN direction */
            usb_reg_write(HCCHAR(0), hcchar);

            hctsiz = ((uint32_t)data_len << TSIZ_XFERSIZE_SHIFT)
                   | (1u << TSIZ_PKTCNT_SHIFT)
                   | ((uint32_t)TSIZ_SC_MC_PID_DATA1 << TSIZ_SC_MC_PID_SHIFT);
            usb_reg_write(HCTSIZ(0), hctsiz);

            usb_reg_write(HCINT(0), 0xFFFFFFFF);
            hcchar = usb_reg_read(HCCHAR(0));
            hcchar |= HCCHAR_CHENA;
            usb_reg_write(HCCHAR(0), hcchar);

            /* Read data from RX FIFO */
            int bytes = usb_read_rx_fifo(data, data_len);
            if (bytes < 0) {
                debug_puts("[USB] DATA IN FIFO read failed\n");
                usb_reg_write(HCINT(0), 0xFFFFFFFF);
                return false;
            }

            /* Wait for transfer complete */
            hcint = usb_wait_channel(0, USB_XFER_TIMEOUT_US);
            if (!hcint || (hcint & (HCINTMSK_STALL | HCINTMSK_XACTERR))) {
                debug_puts("[USB] DATA IN failed, HCINT=");
                debug_hex32(hcint);
                debug_puts("\n");
                usb_reg_write(HCINT(0), 0xFFFFFFFF);
                return false;
            }

            /* Drain any remaining RX FIFO entries (transfer complete status) */
            while (usb_reg_read(GINTSTS) & GINTSTS_RXFLVL) {
                uint32_t grxsts_drain = usb_reg_read(GRXSTSP);
                uint32_t bc = (grxsts_drain & GRXSTS_BYTECNT_MASK) >> GRXSTS_BYTECNT_SHIFT;
                if (bc > 0) {
                    uint32_t w = (bc + 3) / 4;
                    for (uint32_t i = 0; i < w; i++) {
                        (void)usb_reg_read(EPFIFO(0));
                    }
                }
            }

            usb_reg_write(HCINT(0), 0xFFFFFFFF);

        } else {
            /* DATA OUT: send data to device */
            hcchar = usb_reg_read(HCCHAR(0));
            hcchar &= ~HCCHAR_EPDIR;       /* OUT direction */
            usb_reg_write(HCCHAR(0), hcchar);

            hctsiz = ((uint32_t)data_len << TSIZ_XFERSIZE_SHIFT)
                   | (1u << TSIZ_PKTCNT_SHIFT)
                   | ((uint32_t)TSIZ_SC_MC_PID_DATA1 << TSIZ_SC_MC_PID_SHIFT);
            usb_reg_write(HCTSIZ(0), hctsiz);

            usb_reg_write(HCINT(0), 0xFFFFFFFF);
            hcchar = usb_reg_read(HCCHAR(0));
            hcchar |= HCCHAR_CHENA;
            usb_reg_write(HCCHAR(0), hcchar);

            /* Write data to TX FIFO */
            uint32_t words = (data_len + 3) / 4;
            uint32_t temp[64];
            memset(temp, 0, sizeof(temp));
            memcpy(temp, data, data_len);
            uint32_t write_words = words > 64 ? 64 : words;
            for (uint32_t i = 0; i < write_words; i++) {
                usb_reg_write(EPFIFO(0), temp[i]);
            }

            hcint = usb_wait_channel(0, USB_XFER_TIMEOUT_US);
            if (!hcint || (hcint & (HCINTMSK_STALL | HCINTMSK_XACTERR))) {
                debug_puts("[USB] DATA OUT failed, HCINT=");
                debug_hex32(hcint);
                debug_puts("\n");
                usb_reg_write(HCINT(0), 0xFFFFFFFF);
                return false;
            }
            usb_reg_write(HCINT(0), 0xFFFFFFFF);
        }
    }

    /* ---- Phase 3: STATUS (opposite direction, zero-length, PID=DATA1) ---- */
    hcchar = usb_reg_read(HCCHAR(0));
    if (is_in || data_len == 0) {
        /* After IN data (or no data): STATUS is OUT */
        hcchar &= ~HCCHAR_EPDIR;
    } else {
        /* After OUT data: STATUS is IN */
        hcchar |= HCCHAR_EPDIR;
    }
    usb_reg_write(HCCHAR(0), hcchar);

    hctsiz = (0u << TSIZ_XFERSIZE_SHIFT)
           | (1u << TSIZ_PKTCNT_SHIFT)
           | ((uint32_t)TSIZ_SC_MC_PID_DATA1 << TSIZ_SC_MC_PID_SHIFT);
    usb_reg_write(HCTSIZ(0), hctsiz);

    usb_reg_write(HCINT(0), 0xFFFFFFFF);
    hcchar = usb_reg_read(HCCHAR(0));
    hcchar |= HCCHAR_CHENA;
    usb_reg_write(HCCHAR(0), hcchar);

    /* For STATUS IN, drain any RX FIFO zero-length entry */
    if (!is_in && data_len > 0) {
        /* STATUS is IN - may get zero-length data in RX FIFO */
        uint64_t t0 = systimer_read();
        while (usb_reg_read(GINTSTS) & GINTSTS_RXFLVL) {
            (void)usb_reg_read(GRXSTSP);
            if (systimer_elapsed_us(t0) > 10000) break;
        }
    }

    hcint = usb_wait_channel(0, USB_XFER_TIMEOUT_US);
    if (!hcint || (hcint & (HCINTMSK_STALL | HCINTMSK_XACTERR))) {
        debug_puts("[USB] STATUS phase failed, HCINT=");
        debug_hex32(hcint);
        debug_puts("\n");
        usb_reg_write(HCINT(0), 0xFFFFFFFF);
        return false;
    }

    /* Drain any remaining RX FIFO entries */
    while (usb_reg_read(GINTSTS) & GINTSTS_RXFLVL) {
        (void)usb_reg_read(GRXSTSP);
    }

    usb_reg_write(HCINT(0), 0xFFFFFFFF);
    return true;
}

/* ================================================================
 * USB Enumeration
 * ================================================================ */

bool usb_hid_enumerate(void)
{
    usb_setup_packet_t setup;

    debug_puts("[USB] Starting enumeration...\n");

    /*
     * Step 1: GET_DESCRIPTOR (Device) at address 0
     * Read first 8 bytes to learn bMaxPacketSize0
     */
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 8;

    memset(usb_xfer_buf, 0, sizeof(usb_xfer_buf));
    if (!usb_control_transfer(0, &setup, usb_xfer_buf, 8, true)) {
        debug_puts("[USB] GET_DESCRIPTOR (device, 8B) failed\n");
        return false;
    }

    usb_device_descriptor_t *dev_desc = (usb_device_descriptor_t *)usb_xfer_buf;
    hid_state.max_packet_size = dev_desc->bMaxPacketSize0;
    if (hid_state.max_packet_size == 0) {
        hid_state.max_packet_size = 8;
    }

    debug_puts("[USB] Device EP0 max_pkt=");
    debug_dec(hid_state.max_packet_size);
    debug_puts("\n");

    /*
     * Step 2: SET_ADDRESS (assign address 1)
     */
    hid_state.device_address = 1;

    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = hid_state.device_address;
    setup.wIndex = 0;
    setup.wLength = 0;

    if (!usb_control_transfer(0, &setup, NULL, 0, false)) {
        debug_puts("[USB] SET_ADDRESS failed\n");
        return false;
    }

    /* Wait for address to take effect */
    delay_ms(2);

    debug_puts("[USB] Address set to ");
    debug_dec(hid_state.device_address);
    debug_puts("\n");

    /*
     * Step 3: GET_DESCRIPTOR (Device) at new address - full 18 bytes
     */
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_DEVICE << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 18;

    memset(usb_xfer_buf, 0, sizeof(usb_xfer_buf));
    if (!usb_control_transfer(hid_state.device_address, &setup, usb_xfer_buf, 18, true)) {
        debug_puts("[USB] GET_DESCRIPTOR (device, 18B) failed\n");
        return false;
    }

    dev_desc = (usb_device_descriptor_t *)usb_xfer_buf;
    debug_puts("[USB] Device: VID=");
    debug_hex32(dev_desc->idVendor);
    debug_puts(" PID=");
    debug_hex32(dev_desc->idProduct);
    debug_puts(" class=");
    debug_dec(dev_desc->bDeviceClass);
    debug_puts(" configs=");
    debug_dec(dev_desc->bNumConfigurations);
    debug_puts("\n");

    /*
     * Step 4: GET_DESCRIPTOR (Configuration) - header first, then full
     */
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = (USB_DESC_CONFIGURATION << 8) | 0;
    setup.wIndex = 0;
    setup.wLength = 9;   /* Just the config header */

    memset(usb_xfer_buf, 0, sizeof(usb_xfer_buf));
    if (!usb_control_transfer(hid_state.device_address, &setup, usb_xfer_buf, 9, true)) {
        debug_puts("[USB] GET_DESCRIPTOR (config header) failed\n");
        return false;
    }

    usb_config_descriptor_t *cfg_desc = (usb_config_descriptor_t *)usb_xfer_buf;
    uint16_t total_len = cfg_desc->wTotalLength;
    uint8_t config_val = cfg_desc->bConfigurationValue;

    debug_puts("[USB] Config: total_len=");
    debug_dec(total_len);
    debug_puts(" interfaces=");
    debug_dec(cfg_desc->bNumInterfaces);
    debug_puts(" config_val=");
    debug_dec(config_val);
    debug_puts("\n");

    /* Clamp to buffer size */
    if (total_len > sizeof(usb_xfer_buf)) {
        total_len = sizeof(usb_xfer_buf);
    }

    /* Read full configuration descriptor */
    setup.wLength = total_len;
    memset(usb_xfer_buf, 0, sizeof(usb_xfer_buf));
    if (!usb_control_transfer(hid_state.device_address, &setup, usb_xfer_buf, total_len, true)) {
        debug_puts("[USB] GET_DESCRIPTOR (config full) failed\n");
        return false;
    }

    /*
     * Step 5: Parse config descriptor to find HID keyboard interface + endpoint
     */
    bool found_hid = false;
    uint8_t hid_iface = 0;
    int offset = 0;

    while (offset < total_len - 1) {
        uint8_t desc_len = usb_xfer_buf[offset];
        uint8_t desc_type = usb_xfer_buf[offset + 1];

        if (desc_len == 0) break;   /* Prevent infinite loop */

        if (desc_type == USB_DESC_INTERFACE && desc_len >= 9) {
            usb_interface_descriptor_t *iface =
                (usb_interface_descriptor_t *)&usb_xfer_buf[offset];

            debug_puts("[USB] Interface ");
            debug_dec(iface->bInterfaceNumber);
            debug_puts(": class=");
            debug_dec(iface->bInterfaceClass);
            debug_puts(" sub=");
            debug_dec(iface->bInterfaceSubClass);
            debug_puts(" proto=");
            debug_dec(iface->bInterfaceProtocol);
            debug_puts("\n");

            if (iface->bInterfaceClass == USB_CLASS_HID &&
                iface->bInterfaceSubClass == USB_SUBCLASS_BOOT &&
                iface->bInterfaceProtocol == USB_PROTOCOL_KEYBOARD) {
                found_hid = true;
                hid_iface = iface->bInterfaceNumber;
                hid_state.hid_iface_num = hid_iface;
                debug_puts("[USB] Found HID Boot Keyboard at interface ");
                debug_dec(hid_iface);
                debug_puts("\n");
            }
        }

        if (desc_type == USB_DESC_ENDPOINT && desc_len >= 7 && found_hid) {
            usb_endpoint_descriptor_t *ep =
                (usb_endpoint_descriptor_t *)&usb_xfer_buf[offset];

            /* Check for interrupt IN endpoint */
            if ((ep->bmAttributes & 0x03) == 0x03 &&    /* Interrupt */
                (ep->bEndpointAddress & 0x80)) {         /* IN */
                hid_state.hid_ep_addr = ep->bEndpointAddress & 0x0F;
                hid_state.hid_ep_interval = ep->bInterval;
                hid_state.hid_ep_max_pkt = ep->wMaxPacketSize;

                debug_puts("[USB] HID interrupt IN EP");
                debug_dec(hid_state.hid_ep_addr);
                debug_puts(": interval=");
                debug_dec(hid_state.hid_ep_interval);
                debug_puts("ms max_pkt=");
                debug_dec(hid_state.hid_ep_max_pkt);
                debug_puts("\n");
            }
        }

        offset += desc_len;
    }

    if (!found_hid || hid_state.hid_ep_addr == 0) {
        debug_puts("[USB] No HID boot keyboard found in descriptors\n");
        return false;
    }

    /*
     * Step 6: SET_CONFIGURATION
     */
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = config_val;
    setup.wIndex = 0;
    setup.wLength = 0;

    if (!usb_control_transfer(hid_state.device_address, &setup, NULL, 0, false)) {
        debug_puts("[USB] SET_CONFIGURATION failed\n");
        return false;
    }

    delay_ms(10);
    debug_puts("[USB] Configuration set to ");
    debug_dec(config_val);
    debug_puts("\n");

    /*
     * Step 7: SET_PROTOCOL (boot protocol = 0)
     * Class request to HID interface
     */
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    setup.bRequest = HID_REQ_SET_PROTOCOL;
    setup.wValue = 0;           /* 0 = Boot Protocol */
    setup.wIndex = hid_iface;
    setup.wLength = 0;

    if (!usb_control_transfer(hid_state.device_address, &setup, NULL, 0, false)) {
        debug_puts("[USB] SET_PROTOCOL (boot) failed\n");
        return false;
    }

    debug_puts("[USB] Boot protocol set\n");

    /*
     * Step 8: SET_IDLE (rate=0 => report only on change)
     */
    memset(&setup, 0, sizeof(setup));
    setup.bmRequestType = USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE;
    setup.bRequest = HID_REQ_SET_IDLE;
    setup.wValue = 0;           /* Duration=0, ReportID=0 */
    setup.wIndex = hid_iface;
    setup.wLength = 0;

    /* SET_IDLE may STALL on some keyboards - this is non-fatal */
    if (!usb_control_transfer(hid_state.device_address, &setup, NULL, 0, false)) {
        debug_puts("[USB] SET_IDLE failed (non-fatal, some keyboards STALL this)\n");
    }

    hid_state.next_data_toggle = 0;    /* Start with DATA0 */
    hid_state.initialized = true;

    debug_puts("[USB] Enumeration complete - HID keyboard ready\n");
    return true;
}

/* ================================================================
 * HID Keyboard Polling (Interrupt IN Transfer)
 * ================================================================ */

bool usb_hid_poll_keyboard(hid_keyboard_report_t *report)
{
    if (!hid_state.initialized || !hid_state.device_connected) {
        return false;
    }
    if (!report) {
        return false;
    }

    uint32_t ep_num = hid_state.hid_ep_addr;
    uint32_t ep_mps = hid_state.hid_ep_max_pkt;
    if (ep_mps == 0) ep_mps = 8;
    if (ep_mps > 64) ep_mps = 64;

    /* Configure channel 1 for interrupt IN transfer */
    uint32_t hcchar = ((uint32_t)hid_state.device_address << HCCHAR_DEVADDR_SHIFT)
                    | (ep_num << HCCHAR_EPNUM_SHIFT)
                    | HCCHAR_EPDIR                          /* IN */
                    | (USB_EP_TYPE_INTERRUPT << HCCHAR_EPTYPE_SHIFT)
                    | (ep_mps & HCCHAR_MPS_MASK)
                    | (1u << HCCHAR_MULTICNT_SHIFT);
    if (hid_state.port_speed == HPRT0_SPD_LOW_SPEED) {
        hcchar |= HCCHAR_LSPDDEV;
    }
    /* Alternate odd/even frame for periodic transfers */
    uint32_t fnum = usb_reg_read(HFNUM);
    if (fnum & 1) {
        hcchar |= HCCHAR_ODDFRM;
    }
    usb_reg_write(HCCHAR(1), hcchar);

    /* Transfer size: 8 bytes, 1 packet, toggle DATA0/DATA1 */
    uint32_t pid = hid_state.next_data_toggle ?
                   TSIZ_SC_MC_PID_DATA1 : TSIZ_SC_MC_PID_DATA0;
    uint32_t hctsiz = (8u << TSIZ_XFERSIZE_SHIFT)
                    | (1u << TSIZ_PKTCNT_SHIFT)
                    | (pid << TSIZ_SC_MC_PID_SHIFT);
    usb_reg_write(HCTSIZ(1), hctsiz);

    /* Enable interrupt mask for channel 1 */
    usb_reg_write(HCINTMSK(1), HCINTMSK_XFERCOMPL | HCINTMSK_CHHLTD
                              | HCINTMSK_STALL | HCINTMSK_NAK
                              | HCINTMSK_XACTERR | HCINTMSK_BBLERR
                              | HCINTMSK_DATATGLERR);

    /* Clear pending */
    usb_reg_write(HCINT(1), 0xFFFFFFFF);

    /* Start transfer */
    hcchar = usb_reg_read(HCCHAR(1));
    hcchar |= HCCHAR_CHENA;
    usb_reg_write(HCCHAR(1), hcchar);

    /* Wait for RX FIFO or channel event (short timeout for polling) */
    uint64_t t0 = systimer_read();
    uint32_t hcint = 0;
    bool got_data = false;

    while (systimer_elapsed_us(t0) < 50000) {  /* 50ms timeout */
        /* Check for RX FIFO data */
        if (usb_reg_read(GINTSTS) & GINTSTS_RXFLVL) {
            uint32_t grxsts = usb_reg_read(GRXSTSP);
            uint32_t bcnt = (grxsts & GRXSTS_BYTECNT_MASK) >> GRXSTS_BYTECNT_SHIFT;
            uint32_t pktsts = (grxsts & GRXSTS_PKTSTS_MASK) >> GRXSTS_PKTSTS_SHIFT;
            uint32_t ch = (grxsts & GRXSTS_HCHNUM_MASK) >> GRXSTS_HCHNUM_SHIFT;

            if (ch == 1 && pktsts == GRXSTS_PKTSTS_HCHIN && bcnt >= 8) {
                /* Read 8-byte keyboard report from FIFO */
                uint32_t temp[2];
                temp[0] = usb_reg_read(EPFIFO(1));
                temp[1] = usb_reg_read(EPFIFO(1));
                memcpy(report, temp, sizeof(hid_keyboard_report_t));
                got_data = true;

                /* Drain remaining FIFO entries if bcnt > 8 */
                if (bcnt > 8) {
                    uint32_t extra = (bcnt - 8 + 3) / 4;
                    for (uint32_t i = 0; i < extra; i++) {
                        (void)usb_reg_read(EPFIFO(1));
                    }
                }
            } else if (bcnt > 0) {
                /* Drain FIFO for non-data packets */
                uint32_t words = (bcnt + 3) / 4;
                for (uint32_t i = 0; i < words; i++) {
                    (void)usb_reg_read(EPFIFO(1));
                }
            }
        }

        /* Check channel interrupt */
        hcint = usb_reg_read(HCINT(1));
        if (hcint & (HCINTMSK_XFERCOMPL | HCINTMSK_CHHLTD | HCINTMSK_NAK |
                     HCINTMSK_STALL | HCINTMSK_XACTERR | HCINTMSK_BBLERR)) {
            break;
        }
    }

    /* Drain any remaining RX FIFO */
    while (usb_reg_read(GINTSTS) & GINTSTS_RXFLVL) {
        uint32_t grxsts = usb_reg_read(GRXSTSP);
        uint32_t bcnt = (grxsts & GRXSTS_BYTECNT_MASK) >> GRXSTS_BYTECNT_SHIFT;
        if (bcnt > 0) {
            uint32_t words = (bcnt + 3) / 4;
            for (uint32_t i = 0; i < words; i++) {
                (void)usb_reg_read(EPFIFO(1));
            }
        }
    }

    usb_reg_write(HCINT(1), 0xFFFFFFFF);

    if (hcint & HCINTMSK_NAK) {
        /* NAK = no new data, normal for keyboard polling */
        return false;
    }

    if (hcint & (HCINTMSK_STALL | HCINTMSK_XACTERR | HCINTMSK_BBLERR)) {
        debug_puts("[USB] Poll error, HCINT=");
        debug_hex32(hcint);
        debug_puts("\n");
        return false;
    }

    if (got_data) {
        /* Toggle DATA PID for next transfer */
        hid_state.next_data_toggle ^= 1;
        return true;
    }

    return false;
}

/* ================================================================
 * Scan Code to ASCII Lookup Tables
 * ================================================================ */

/* USB HID Usage Table (Keyboard/Keypad page 0x07) - unshifted */
static const char scancode_to_ascii[128] = {
    /* 0x00-0x03: no event, error */
    0, 0, 0, 0,
    /* 0x04-0x1D: a-z */
    'a','b','c','d','e','f','g','h','i','j','k','l','m',
    'n','o','p','q','r','s','t','u','v','w','x','y','z',
    /* 0x1E-0x27: 1-9, 0 */
    '1','2','3','4','5','6','7','8','9','0',
    /* 0x28-0x2C: Enter, Esc, Backspace, Tab, Space */
    '\n', 0x1B, '\b', '\t', ' ',
    /* 0x2D-0x38: punctuation */
    '-','=','[',']','\\',
    0,  /* 0x32: non-US # */
    ';','\'','`',',','.','/',
    /* 0x39+: caps lock, F keys, etc - zero */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x39-0x45: Caps, F1-F12 */
    0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x46-0x4E: PrtSc..Ins,Home,PgUp,Del,End,PgDn */
    0, 0, 0, 0,  /* 0x4F-0x52: arrows */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x53-0x62: numpad etc */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x63-0x72 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0x73-0x7F */
};

/* Shifted versions */
static const char scancode_to_ascii_shifted[128] = {
    /* 0x00-0x03: no event, error */
    0, 0, 0, 0,
    /* 0x04-0x1D: A-Z */
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    /* 0x1E-0x27: !@#$%^&*() */
    '!','@','#','$','%','^','&','*','(',')',
    /* 0x28-0x2C: Enter, Esc, Backspace, Tab, Space */
    '\n', 0x1B, '\b', '\t', ' ',
    /* 0x2D-0x38: shifted punctuation */
    '_','+','{','}','|',
    0,  /* 0x32: non-US ~ */
    ':','"','~','<','>','?',
    /* 0x39+: same as unshifted (special keys) */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

char usb_hid_scancode_to_ascii(uint8_t scancode, uint8_t modifier)
{
    if (scancode >= 128) return 0;
    bool shift = (modifier & (MOD_LSHIFT | MOD_RSHIFT)) != 0;
    return shift ? scancode_to_ascii_shifted[scancode] : scancode_to_ascii[scancode];
}

/* ================================================================
 * Public API: usb_hid_init
 * ================================================================ */

bool usb_hid_init(seL4_BootInfo *bi)
{
    if (hid_state.initialized) {
        return true;
    }

    debug_puts("[USB] ========================================\n");
    debug_puts("[USB] DWC2 USB HID Driver Init\n");
    debug_puts("[USB] ========================================\n");

    if (!bi) {
        bi = sel4runtime_bootinfo();
    }
    if (!bi) {
        debug_puts("[USB] ERROR: bootinfo is NULL\n");
        return false;
    }

    /* Verify slot allocator is ready */
    if (!slot_alloc_ready()) {
        debug_puts("[USB] ERROR: slot_alloc not initialized\n");
        return false;
    }

    /* Verify system timer is available (needed for delays) */
    if (!systimer_is_initialized()) {
        debug_puts("[USB] WARNING: System timer not initialized, using spin delays\n");
    }

    /* Initialize driver state */
    memset(&hid_state, 0, sizeof(hid_state));
    hid_state.max_packet_size = 8;  /* Default for EP0 */

    /* Step 1: Map DWC2 MMIO registers */
    if (!usb_map_mmio(bi)) {
        debug_puts("[USB] ERROR: Failed to map DWC2 MMIO\n");
        return false;
    }

    /* Step 2: Initialize DWC2 core */
    if (!dwc2_core_init()) {
        debug_puts("[USB] ERROR: DWC2 core init failed\n");
        return false;
    }

    /* Step 3: Power on the port */
    if (!dwc2_port_power_on()) {
        debug_puts("[USB] ERROR: Port power on failed\n");
        return false;
    }

    /* Step 4: Wait a bit for device to attach, then reset */
    debug_puts("[USB] Waiting for device connection...\n");
    delay_ms(100);

    if (!dwc2_port_reset()) {
        debug_puts("[USB] No device detected (or reset failed)\n");
        debug_puts("[USB] NOTE: DWC2 = USB-C port. Use USB-C OTG adapter for keyboard.\n");
        debug_puts("[USB] USB-A ports use VL805 xHCI (not supported yet).\n");
        /* Non-fatal: driver is initialized, device may connect later */
        return true;
    }

    /* Step 5: Enumerate the device */
    if (!usb_hid_enumerate()) {
        debug_puts("[USB] Enumeration failed (device may not be HID keyboard)\n");
        return true;   /* Driver initialized but no keyboard found */
    }

    debug_puts("[USB] ========================================\n");
    debug_puts("[USB] USB HID Keyboard READY\n");
    debug_puts("[USB] ========================================\n");

    return true;
}

/* ================================================================
 * Public API: usb_hid_device_connected
 * ================================================================ */

bool usb_hid_device_connected(void)
{
    return hid_state.initialized && hid_state.device_connected;
}
