/*
 * JARVIS AI-OS - PL011 UART Driver Implementation
 * Phase 2 Week 33 (Direct MMIO Device Frame Mapping)
 *
 * BCM2711 PL011 UART driver for seL4 on Raspberry Pi 4.
 *
 * IMPLEMENTATION:
 * - TX: Uses seL4_DebugPutChar() kernel syscall (works in debug builds)
 * - RX: Uses direct MMIO via device frame mapping from bootinfo
 *
 * Week 33 Changes:
 * - Added uart_map_device_frame() to map UART MMIO registers
 * - RX now works via direct hardware register access
 * - No longer depends on __plat_getchar (which was never implemented)
 */

#include "uart_pl011.h"
#include <stdio.h>
#include <string.h>

#include <sel4/sel4.h>
#include <sel4/bootinfo.h>
#include <sel4runtime.h>
#include <sel4platsupport/platsupport.h>

/* Older libsel4 headers may not define device VM attributes. */
#ifndef seL4_ARM_Device_VMAttributes
#define seL4_ARM_Device_VMAttributes 0
#endif

/* BIT macro if not defined */
#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

/* seL4 page size - use libutils definitions if available */
#ifndef PAGE_BITS_4K
#define PAGE_BITS_4K 12
#endif
#ifndef PAGE_SIZE_4K
#define PAGE_SIZE_4K BIT(PAGE_BITS_4K)
#endif
#ifndef PAGE_BITS_2M
#define PAGE_BITS_2M 21
#endif
#ifndef PAGE_SIZE_2M
#define PAGE_SIZE_2M BIT(PAGE_BITS_2M)
#endif

/* Global UART state */
static uart_state_t uart_state = {0};
static int pending_char = -1;
static bool uart_rx_enabled = false;
static volatile uint32_t *uart_mmio_base = NULL;  /* Direct MMIO pointer */
static volatile uint32_t *gpio_mmio_base = NULL;  /* GPIO MMIO pointer */
static seL4_CPtr uart_frame_cap = seL4_CapNull;   /* Frame capability */
static seL4_CPtr gpio_frame_cap = seL4_CapNull;   /* GPIO frame capability */

static inline uint32_t uart_reg_read(uint32_t offset);
static inline void uart_reg_write(uint32_t offset, uint32_t value);
void uart_dump_regs(const char *tag);
uint32_t uart_rx_error_status(void);
static bool uart_mmio_rx_ready(void);
static int uart_mmio_getchar(void);
static void uart_enable_rx(void);
static void uart_pl011_init_hw(void);

/*
 * Find a device untyped covering the given physical address
 * Returns the untyped capability, or seL4_CapNull if not found
 */
static seL4_CPtr find_device_untyped(seL4_BootInfo *bi, seL4_Word paddr,
                                     seL4_Word *base_out, int *size_bits_out)
{
    for (seL4_Word i = 0; i < bi->untyped.end - bi->untyped.start; i++) {
        seL4_UntypedDesc *ud = &bi->untypedList[i];

        /* Only consider device untypeds */
        if (!ud->isDevice) {
            continue;
        }

        seL4_Word base = ud->paddr;
        seL4_Word size = BIT(ud->sizeBits);

        /* Check if paddr falls within this untyped */
        if (paddr >= base && paddr < base + size) {
            if (base_out) {
                *base_out = base;
            }
            if (size_bits_out) {
                *size_bits_out = ud->sizeBits;
            }
            return bi->untyped.start + i;
        }
    }
    return seL4_CapNull;
}

/*
 * Map UART device frame into virtual address space
 * This allows direct MMIO access to PL011 registers
 *
 * Returns: true on success, false on failure
 */
/* Debug helper - uses seL4_DebugPutChar for early debug output */
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

/*
 * Map GPIO device frame and configure pull-up on GPIO15 (UART RX)
 * This fixes the 0x01 noise issue caused by floating RX line
 *
 * BCM2711 GPIO_PUP_PDN_CNTRL_REG0 (offset 0xE4):
 * - GPIO15 uses bits [31:30]
 * - Value 01 = pull-up enabled
 */
static bool gpio_configure_pullup(void)
{
    debug_puts("[GPIO] Configuring pull-up on GPIO15 (UART RX)\n");

    if (!gpio_mmio_base) {
        debug_puts("[GPIO] ERROR: GPIO MMIO base not mapped\n");
        return false;
    }

    /* Configure GPIO15 pull-up via GPIO_PUP_PDN_CNTRL_REG0 */
    volatile uint32_t *pup_pdn_reg = (volatile uint32_t *)((seL4_Word)gpio_mmio_base + GPIO_PUP_PDN_CNTRL_REG0);
    volatile uint32_t *gpfsel1 = (volatile uint32_t *)((seL4_Word)gpio_mmio_base + GPIO_GPFSEL1);

    debug_puts("[GPIO] PUP_PDN_REG0 addr: ");
    debug_hex((seL4_Word)pup_pdn_reg);
    debug_puts("\n");

    /* Read current value */
    uint32_t val = *pup_pdn_reg;
    uint32_t field = (val >> 30) & 0x3;
    debug_puts("[GPIO] Current PUP_PDN_REG0: ");
    debug_hex(val);
    debug_puts("\n");
    debug_puts("[GPIO] GPIO15 PUP field: ");
    debug_hex(field);
    debug_puts("\n");

    /* Force toggle to prove writability: NONE -> UP */
    uint32_t val_none = (val & ~(0x3 << 30)) | (GPIO_PUD_NONE << 30);
    *pup_pdn_reg = val_none;
    __asm__ volatile("dsb sy" ::: "memory");
    uint32_t read_none = *pup_pdn_reg;
    uint32_t field_none = (read_none >> 30) & 0x3;
    debug_puts("[GPIO] GPIO15 PUP field after NONE: ");
    debug_hex(field_none);
    debug_puts("\n");

    uint32_t val_up = (read_none & ~(0x3 << 30)) | (GPIO_PUD_UP << 30);
    *pup_pdn_reg = val_up;
    __asm__ volatile("dsb sy" ::: "memory");
    uint32_t read_up = *pup_pdn_reg;
    uint32_t field_up = (read_up >> 30) & 0x3;
    debug_puts("[GPIO] GPIO15 PUP field after UP: ");
    debug_hex(field_up);
    debug_puts("\n");

    debug_puts("[GPIO] New PUP_PDN_REG0: ");
    debug_hex(read_up);
    debug_puts("\n");

    /* Verify write */
    uint32_t verify = *pup_pdn_reg;
    debug_puts("[GPIO] Verify PUP_PDN_REG0: ");
    debug_hex(verify);
    debug_puts("\n");

    if (field_none == GPIO_PUD_NONE && field_up == GPIO_PUD_UP) {
        debug_puts("[GPIO] SUCCESS: Pull-up enabled on GPIO15\n");
    }

    /* Ensure GPIO14/15 are set to ALT0 (UART0 TX/RX) */
    uint32_t fsel = *gpfsel1;
    debug_puts("[GPIO] GPFSEL1 before: ");
    debug_hex(fsel);
    debug_puts("\n");

    fsel &= ~((0x7 << 12) | (0x7 << 15)); /* Clear GPIO14/15 */
    fsel |= (GPIO_FSEL_ALT0 << 12) | (GPIO_FSEL_ALT0 << 15);
    *gpfsel1 = fsel;
    __asm__ volatile("dsb sy" ::: "memory");

    uint32_t fsel_verify = *gpfsel1;
    debug_puts("[GPIO] GPFSEL1 after: ");
    debug_hex(fsel_verify);
    debug_puts("\n");

    if (field_none != GPIO_PUD_NONE || field_up != GPIO_PUD_UP) {
        debug_puts("[GPIO] WARNING: Pull-up verify failed\n");
        return false;
    }

    return true;
}

void uart_dump_regs(const char *tag)
{
    if (!uart_mmio_base) {
        debug_puts("[UART] uart_dump_regs: MMIO not mapped\n");
        return;
    }

    if (tag && *tag) {
        debug_puts("[UART] ");
        debug_puts(tag);
        debug_puts("\n");
    }

    debug_puts("[UART] UART_CR: ");
    debug_hex(uart_reg_read(UART_CR));
    debug_puts("\n");

    debug_puts("[UART] UART_FR: ");
    debug_hex(uart_reg_read(UART_FR));
    debug_puts("\n");

    debug_puts("[UART] UART_IBRD: ");
    debug_hex(uart_reg_read(UART_IBRD));
    debug_puts("\n");

    debug_puts("[UART] UART_FBRD: ");
    debug_hex(uart_reg_read(UART_FBRD));
    debug_puts("\n");

    debug_puts("[UART] UART_LCRH: ");
    debug_hex(uart_reg_read(UART_LCRH));
    debug_puts("\n");

    debug_puts("[UART] UART_IMSC: ");
    debug_hex(uart_reg_read(UART_IMSC));
    debug_puts("\n");

    debug_puts("[UART] UART_RIS: ");
    debug_hex(uart_reg_read(UART_RIS));
    debug_puts("\n");

    debug_puts("[UART] UART_MIS: ");
    debug_hex(uart_reg_read(UART_MIS));
    debug_puts("\n");

    debug_puts("[UART] UART_RSRECR: ");
    debug_hex(uart_reg_read(UART_RSRECR));
    debug_puts("\n");
}

static void uart_pl011_init_hw(void)
{
    if (!uart_mmio_base || !gpio_mmio_base) {
        return;
    }

    debug_puts("[UART] Initializing PL011 registers\n");

    /* Disable UART while configuring */
    uart_reg_write(UART_CR, 0x0);
    __asm__ volatile("dsb sy" ::: "memory");

    /* Clear interrupts */
    uart_reg_write(UART_ICR, 0x7FF);

    /* Baud rate divisors for 115200 with 48MHz UART clock */
    uart_reg_write(UART_IBRD, 26);
    uart_reg_write(UART_FBRD, 3);

    /* 8N1, FIFO enabled */
    uart_reg_write(UART_LCRH, UART_LCRH_WLEN_8 | UART_LCRH_FEN);

    /* Mask all interrupts (polling) */
    uart_reg_write(UART_IMSC, 0x0);

    /* Enable UART, TX, RX */
    uart_reg_write(UART_CR, UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
    __asm__ volatile("dsb sy" ::: "memory");

    /* Debug: pinmux + GPIO level + UART state */
    volatile uint32_t *gpfsel1 = (volatile uint32_t *)((seL4_Word)gpio_mmio_base + GPIO_GPFSEL1);
    volatile uint32_t *gplev0 = (volatile uint32_t *)((seL4_Word)gpio_mmio_base + GPIO_GPLEV0);

    debug_puts("[UART] GPFSEL1: ");
    debug_hex(*gpfsel1);
    debug_puts("\n");

    debug_puts("[UART] GPLEV0: ");
    debug_hex(*gplev0);
    debug_puts("\n");

    uart_dump_regs("UART INIT");
}

bool uart_map_device_frame(void)
{
    debug_puts("[UART] uart_map_device_frame() starting\n");

    /* Use sel4runtime to get bootinfo (avoids deprecated seL4_GetBootInfo) */
    seL4_BootInfo *bi = sel4runtime_bootinfo();
    if (!bi) {
        debug_puts("[UART] ERROR: bootinfo is NULL\n");
        return false;
    }
    debug_puts("[UART] Got bootinfo\n");

    /* Debug: print untyped info */
    seL4_Word num_untypeds = bi->untyped.end - bi->untyped.start;
    debug_puts("[UART] Untypeds: ");
    debug_hex(num_untypeds);
    debug_puts("\n");

    /* Debug: count device untypeds and show their ranges */
    int dev_count = 0;
    for (seL4_Word i = 0; i < num_untypeds && i < 10; i++) {
        seL4_UntypedDesc *ud = &bi->untypedList[i];
        if (ud->isDevice) {
            dev_count++;
            debug_puts("[UART] DevUT[");
            debug_hex(i);
            debug_puts("]: paddr=");
            debug_hex(ud->paddr);
            debug_puts(" size=");
            debug_hex(BIT(ud->sizeBits));
            debug_puts("\n");
        }
    }
    debug_puts("[UART] Device untypeds found: ");
    debug_hex(dev_count);
    debug_puts("\n");

    debug_puts("[UART] Looking for UART at ");
    debug_hex(UART0_BASE);
    debug_puts("\n");

    seL4_Word ut_base = 0;
    int size_bits;
    seL4_CPtr ut = find_device_untyped(bi, UART0_BASE, &ut_base, &size_bits);
    if (ut == seL4_CapNull) {
        debug_puts("[UART] ERROR: No device untyped found covering UART\n");
        return false;
    }
    debug_puts("[UART] Found device untyped, cap=");
    debug_hex(ut);
    debug_puts(" size_bits=");
    debug_hex(size_bits);
    debug_puts("\n");

    if (ut_base == 0) {
        debug_puts("[UART] ERROR: Device untyped base unknown\n");
        return false;
    }

    seL4_Word ut_size = BIT(size_bits);
    if (UART0_BASE < ut_base || UART0_BASE >= ut_base + ut_size) {
        debug_puts("[UART] ERROR: UART outside device untyped range\n");
        return false;
    }
    if (GPIO_BASE < ut_base || GPIO_BASE >= ut_base + ut_size) {
        debug_puts("[UART] ERROR: GPIO outside device untyped range\n");
        return false;
    }

    seL4_Word gpio_offset = GPIO_BASE - ut_base;
    seL4_Word uart_offset = UART0_BASE - ut_base;

    if ((gpio_offset & (PAGE_SIZE_4K - 1)) != 0 ||
        (uart_offset & (PAGE_SIZE_4K - 1)) != 0) {
        debug_puts("[UART] ERROR: GPIO/UART not 4K aligned\n");
        return false;
    }

    if (uart_offset != gpio_offset + PAGE_SIZE_4K) {
        debug_puts("[UART] ERROR: Unexpected GPIO/UART offset spacing\n");
        return false;
    }

    if ((gpio_offset & (PAGE_SIZE_2M - 1)) != 0) {
        debug_puts("[UART] ERROR: GPIO not 2MB aligned\n");
        return false;
    }

    seL4_Word skip_large_pages = gpio_offset >> PAGE_BITS_2M;
    seL4_Word required_slots = skip_large_pages + 2;

    if (bi->empty.start >= bi->empty.end) {
        debug_puts("[UART] ERROR: No empty slots\n");
        return false;
    }

    seL4_Word empty_slots = bi->empty.end - bi->empty.start;
    if (required_slots > empty_slots) {
        debug_puts("[UART] ERROR: Not enough empty slots for device frames\n");
        debug_puts("[UART] Needed slots: ");
        debug_hex(required_slots);
        debug_puts(" Available: ");
        debug_hex(empty_slots);
        debug_puts("\n");
        return false;
    }

    seL4_CPtr next_slot = bi->empty.start;
    seL4_Error err;

    for (seL4_Word i = 0; i < skip_large_pages; i++) {
        seL4_CPtr skip_cap = next_slot++;
        debug_puts("[UART] Retyping 2MB page to skip base region...\n");
        err = seL4_Untyped_Retype(
            ut,                       /* Untyped capability */
            seL4_ARM_LargePageObject, /* Object type (2MB page) */
            PAGE_BITS_2M,             /* Size bits (21 for 2MB) */
            seL4_CapInitThreadCNode,  /* Root CNode */
            0,                        /* Node index (root) */
            0,                        /* Node depth (root) */
            skip_cap,                 /* Destination slot */
            1                         /* Number of objects */
        );

        if (err != seL4_NoError) {
            debug_puts("[UART] ERROR: Large page retype failed, err=");
            debug_hex(err);
            debug_puts("\n");
            return false;
        }
    }

    gpio_frame_cap = next_slot++;
    debug_puts("[GPIO] Retyping small page for GPIO...\n");
    err = seL4_Untyped_Retype(
        ut,                           /* Untyped capability */
        seL4_ARM_SmallPageObject,     /* Object type (4KB page) */
        PAGE_BITS_4K,                 /* Size bits (12 for 4KB) */
        seL4_CapInitThreadCNode,      /* Root CNode */
        0,                            /* Node index (root) */
        0,                            /* Node depth (root) */
        gpio_frame_cap,               /* Destination slot */
        1                             /* Number of objects */
    );

    if (err != seL4_NoError) {
        debug_puts("[GPIO] ERROR: Retype failed, err=");
        debug_hex(err);
        debug_puts("\n");
        return false;
    }

    uart_frame_cap = next_slot++;
    debug_puts("[UART] Retyping small page for UART...\n");
    err = seL4_Untyped_Retype(
        ut,                           /* Untyped capability */
        seL4_ARM_SmallPageObject,     /* Object type (4KB page) */
        PAGE_BITS_4K,                 /* Size bits (12 for 4KB) */
        seL4_CapInitThreadCNode,      /* Root CNode */
        0,                            /* Node index (root) */
        0,                            /* Node depth (root) */
        uart_frame_cap,               /* Destination slot */
        1                             /* Number of objects */
    );

    if (err != seL4_NoError) {
        debug_puts("[UART] ERROR: Retype failed, err=");
        debug_hex(err);
        debug_puts("\n");
        return false;
    }

    debug_puts("[UART] UART frame slot: ");
    debug_hex(uart_frame_cap);
    debug_puts(" GPIO frame slot: ");
    debug_hex(gpio_frame_cap);
    debug_puts("\n");

    /* Map UART frame into VSpace (within existing page table region) */
    seL4_Word uart_vaddr = 0x5c0000;
    debug_puts("[UART] Mapping UART to vaddr=");
    debug_hex(uart_vaddr);
    debug_puts("\n");

    err = seL4_ARM_Page_Map(
        uart_frame_cap,
        seL4_CapInitThreadVSpace,
        uart_vaddr,
        seL4_AllRights,
        seL4_ARM_Device_VMAttributes
    );

    if (err != seL4_NoError) {
        debug_puts("[UART] ERROR: UART Page_Map failed, err=");
        debug_hex(err);
        debug_puts("\n");
        return false;
    }
    debug_puts("[UART] UART Page_Map succeeded\n");
    uart_mmio_base = (volatile uint32_t *)uart_vaddr;

    /* Map GPIO frame into VSpace (adjacent page) */
    seL4_Word gpio_vaddr = 0x5c1000;
    debug_puts("[GPIO] Mapping GPIO to vaddr=");
    debug_hex(gpio_vaddr);
    debug_puts("\n");

    err = seL4_ARM_Page_Map(
        gpio_frame_cap,
        seL4_CapInitThreadVSpace,
        gpio_vaddr,
        seL4_AllRights,
        seL4_ARM_Device_VMAttributes
    );

    if (err != seL4_NoError) {
        debug_puts("[GPIO] ERROR: GPIO Page_Map failed, err=");
        debug_hex(err);
        debug_puts("\n");
        return false;
    }
    debug_puts("[GPIO] GPIO Page_Map succeeded\n");
    gpio_mmio_base = (volatile uint32_t *)gpio_vaddr;

    debug_puts("[UART] Mapped UART base: ");
    debug_hex((seL4_Word)uart_mmio_base);
    debug_puts("\n");

    debug_puts("[GPIO] Mapped GPIO base: ");
    debug_hex((seL4_Word)gpio_mmio_base);
    debug_puts("\n");

    if (gpio_mmio_base) {
        volatile uint32_t *gpfsel1 = (volatile uint32_t *)((seL4_Word)gpio_mmio_base + GPIO_GPFSEL1);
        volatile uint32_t *gplev0 = (volatile uint32_t *)((seL4_Word)gpio_mmio_base + GPIO_GPLEV0);
        debug_puts("[GPIO] GPFSEL1 initial: ");
        debug_hex(*gpfsel1);
        debug_puts("\n");
        debug_puts("[GPIO] GPLEV0 initial: ");
        debug_hex(*gplev0);
        debug_puts("\n");
    }

    if (uart_mmio_base) {
        uint32_t fr = uart_reg_read(UART_FR);
        debug_puts("[UART] UART_FR initial: ");
        debug_hex(fr);
        debug_puts("\n");

        if ((fr & UART_FR_RXFE) == 0) {
            uint32_t dr = uart_reg_read(UART_DR) & 0xFF;
            debug_puts("[UART] UART_DR initial: ");
            debug_hex(dr);
            debug_puts("\n");
        }
    }

    if (!gpio_configure_pullup()) {
        debug_puts("[GPIO] WARNING: Pull-up configuration failed\n");
    }

    uart_pl011_init_hw();
    uart_enable_rx();
    return true;
}

/*
 * Direct MMIO register read
 */
static inline uint32_t uart_reg_read(uint32_t offset)
{
    if (!uart_mmio_base) return 0;
    return uart_mmio_base[offset / 4];
}

/*
 * Direct MMIO register write
 */
static inline void uart_reg_write(uint32_t offset, uint32_t value)
{
    if (!uart_mmio_base) return;
    uart_mmio_base[offset / 4] = value;
}

int uart_gpio15_level(void)
{
    if (!gpio_mmio_base) {
        return -1;
    }

    volatile uint32_t *gplev0 = (volatile uint32_t *)((seL4_Word)gpio_mmio_base + GPIO_GPLEV0);
    uint32_t val = *gplev0;

    if (uart_mmio_base) {
        uint32_t fr = uart_reg_read(UART_FR);
        uint32_t rsrecr = uart_reg_read(UART_RSRECR);
        static uint32_t diag_count = 0;
        bool has_data = (fr & UART_FR_RXFE) == 0;
        bool has_err = (rsrecr != 0);
        bool rate = (++diag_count % 60) == 0;

        if (has_data || has_err || rate) {
            uart_dump_regs("RX DIAG");
            if (has_err) {
                debug_puts("[UART] Clearing UART_RSRECR\n");
                uart_reg_write(UART_RSRECR, 0);
                __asm__ volatile("dsb sy" ::: "memory");
            }
        }
    }

    return (val & (1u << 15)) ? 1 : 0;
}

uint32_t uart_rx_error_status(void)
{
    if (!uart_mmio_base) {
        return 0;
    }
    return uart_reg_read(UART_RSRECR);
}

void uart_clear_rx_errors(void)
{
    if (!uart_mmio_base) {
        return;
    }
    uart_reg_write(UART_RSRECR, 0);
    __asm__ volatile("dsb sy" ::: "memory");
}

void uart_rx_drain(int max_bytes)
{
    if (!uart_mmio_base || max_bytes <= 0) {
        return;
    }

    while (max_bytes-- > 0 && uart_mmio_rx_ready()) {
        (void)uart_mmio_getchar();
    }
}

static void uart_enable_rx(void)
{
    if (!uart_mmio_base) {
        return;
    }

    uint32_t cr = uart_reg_read(UART_CR);
    debug_puts("[UART] UART_CR before: ");
    debug_hex(cr);
    debug_puts("\n");

    if ((cr & UART_CR_UARTEN) == 0 || (cr & UART_CR_RXE) == 0) {
        cr |= UART_CR_UARTEN | UART_CR_RXE | UART_CR_TXE;
        uart_reg_write(UART_CR, cr);
        __asm__ volatile("dsb sy" ::: "memory");

        uint32_t verify = uart_reg_read(UART_CR);
        debug_puts("[UART] UART_CR after: ");
        debug_hex(verify);
        debug_puts("\n");
    } else {
        debug_puts("[UART] UART RX already enabled\n");
    }
}

/*
 * Check if RX FIFO has data (via direct MMIO)
 */
static bool uart_mmio_rx_ready(void)
{
    if (!uart_mmio_base) return false;
    uint32_t fr = uart_reg_read(UART_FR);
    return !(fr & UART_FR_RXFE);  /* RXFE=0 means data available */
}

/*
 * Read a character from RX FIFO (via direct MMIO)
 * Returns EOF if no data available
 */
static int uart_mmio_getchar(void)
{
    if (!uart_mmio_base) return EOF;
    if (!uart_mmio_rx_ready()) return EOF;
    return uart_reg_read(UART_DR) & 0xFF;
}

static int uart_try_getchar(void)
{
    if (!uart_rx_enabled) {
        return EOF;
    }

    /* Use direct MMIO if available */
    if (uart_mmio_base) {
        return uart_mmio_getchar();
    }

    /* Fallback: no RX available */
    return EOF;
}

bool uart_init(void)
{
    return uart_init_baud(UART_DEFAULT_BAUD);
}

bool uart_init_baud(uint32_t baud_rate)
{
    if (uart_state.initialized) {
        return true;
    }

    uart_state.base = NULL;
    uart_state.baud_rate = baud_rate;
    uart_state.tx_bytes = 0;
    uart_state.rx_bytes = 0;
    uart_state.rx_errors = 0;
    uart_state.tx_errors = 0;
    pending_char = -1;

    /* Map device frames first to avoid platsupport consuming device untypeds. */
    if (uart_map_device_frame()) {
        uart_rx_enabled = true;
        uart_state.base = uart_mmio_base;
        uart_state.initialized = true;
        /* Skip platsupport to avoid retyping device untypeds before manual mapping. */
        return true;
    }

    /* Device frame mapping failed - RX disabled. */
    uart_rx_enabled = false;

    /* Optional fallback for legacy console setup; RX remains disabled here. */
    if (platsupport_serial_setup_bootinfo_failsafe() != 0) {
        return false;
    }

    uart_state.initialized = true;
    return true;
}

void uart_shutdown(void)
{
    if (!uart_state.initialized) {
        return;
    }

    uart_flush_tx();
    uart_state.initialized = false;
    pending_char = -1;
}

void uart_putc(char c)
{
    if (!uart_state.initialized) {
        return;
    }

    seL4_DebugPutChar((int)c);
    uart_state.tx_bytes++;
}

char uart_getc(void)
{
    if (!uart_state.initialized) {
        return '\0';
    }
    if (!uart_rx_enabled) {
        return '\0';
    }

    if (pending_char != -1) {
        int ch = pending_char;
        pending_char = -1;
        uart_state.rx_bytes++;
        return (char)ch;
    }

    int ch;
    do {
        ch = uart_try_getchar();
    } while (ch == EOF);

    uart_state.rx_bytes++;
    return (char)ch;
}

bool uart_rx_ready(void)
{
    if (!uart_state.initialized) {
        return false;
    }
    if (!uart_rx_enabled) {
        return false;
    }

    if (pending_char != -1) {
        return true;
    }

    int ch = uart_try_getchar();
    if (ch == EOF) {
        return false;
    }

    pending_char = ch;
    return true;
}

bool uart_tx_ready(void)
{
    return uart_state.initialized;
}

void uart_puts(const char *s)
{
    if (!s) {
        return;
    }

    while (*s) {
        if (*s == '\n') {
            uart_putc('\r');
        }
        uart_putc(*s++);
    }
}

size_t uart_write(const uint8_t *data, size_t len)
{
    if (!data || !uart_state.initialized) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        uart_putc((char)data[i]);
    }
    return len;
}

static void uart_delay_us(uint32_t us)
{
    volatile uint32_t count = us * 100;
    while (count--) {
        __asm__ volatile("nop");
    }
}

int uart_read(uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!buf || !max_len || !uart_state.initialized) {
        return -1;
    }

    size_t received = 0;
    uint32_t elapsed_us = 0;
    uint32_t timeout_us = timeout_ms * 1000;

    while (received < max_len) {
        if (uart_rx_ready()) {
            buf[received++] = (uint8_t)uart_getc();
            elapsed_us = 0;
        } else if (timeout_ms > 0) {
            uart_delay_us(100);
            elapsed_us += 100;
            if (elapsed_us >= timeout_us) {
                break;
            }
        } else {
            break;
        }
    }

    return (int)received;
}

int uart_read_line(char *buf, size_t max_len, uint32_t timeout_ms)
{
    if (!buf || max_len < 2 || !uart_state.initialized) {
        return -1;
    }

    size_t pos = 0;
    uint32_t elapsed_us = 0;
    uint32_t timeout_us = timeout_ms * 1000;

    while (pos < max_len - 1) {
        if (uart_rx_ready()) {
            char c = uart_getc();
            elapsed_us = 0;

            if (c == '\r' || c == '\n') {
                break;
            }

            if (c == '\b' || c == 0x7F) {
                if (pos > 0) {
                    pos--;
                    uart_puts("\b \b");
                }
                continue;
            }

            if (c >= 0x20 && c < 0x7F) {
                buf[pos++] = c;
                uart_putc(c);
            }
        } else {
            uart_delay_us(100);
            elapsed_us += 100;
            if (timeout_ms > 0 && elapsed_us >= timeout_us) {
                buf[pos] = '\0';
                return -1;
            }
        }
    }

    buf[pos] = '\0';
    return (int)pos;
}

void uart_flush_tx(void)
{
    /* No-op: __plat_putchar is synchronous */
}

void uart_flush_rx(void)
{
    if (!uart_state.initialized) {
        return;
    }

    pending_char = -1;
    while (uart_try_getchar() != EOF) {
        /* drain */
    }
}

void uart_get_stats(uint64_t *tx_bytes, uint64_t *rx_bytes, uint32_t *rx_errors)
{
    if (tx_bytes) *tx_bytes = uart_state.tx_bytes;
    if (rx_bytes) *rx_bytes = uart_state.rx_bytes;
    if (rx_errors) *rx_errors = uart_state.rx_errors;
}

void uart_print_status(void)
{
    printf("\n========== UART STATUS ==========\n");
    printf("Initialized: %s\n", uart_state.initialized ? "Yes" : "No");
    printf("RX Enabled:  %s\n", uart_rx_enabled ? "Yes" : "No");
    printf("MMIO Base:   %p\n", (void*)uart_mmio_base);
    printf("Baud Rate:   %u\n", uart_state.baud_rate);
    printf("TX Bytes:    %llu\n", (unsigned long long)uart_state.tx_bytes);
    printf("RX Bytes:    %llu\n", (unsigned long long)uart_state.rx_bytes);
    printf("RX Errors:   %u\n", uart_state.rx_errors);
    printf("=================================\n\n");
}

bool uart_is_rx_enabled(void)
{
    return uart_rx_enabled && uart_mmio_base != NULL;
}
