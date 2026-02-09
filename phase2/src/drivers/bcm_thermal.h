/*
 * JARVIS AI-OS - BCM2711 Thermal Monitoring via VideoCore Mailbox
 *
 * Week 44: GPU temperature reading using the VideoCore mailbox
 * property interface. Mailbox MMIO page at paddr 0xFE00B000,
 * mapped to explicit vaddr 0x610000 (before PM and UART).
 *
 * The mailbox is at offset 0x880 within the 0xFE00B000 page:
 *   MBOX_READ   = page_base + 0x880
 *   MBOX_STATUS  = page_base + 0x898
 *   MBOX_WRITE   = page_base + 0x8A0
 *
 * Property tag GET_TEMPERATURE (0x00030006) returns millidegrees C.
 * Uses DMA allocator for the property tag buffer (GPU needs bus address).
 *
 * Reference: https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 */

#ifndef BCM_THERMAL_H
#define BCM_THERMAL_H

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * Mailbox Register Offsets (within 4KB page at 0xFE00B000)
 * Mailbox base is at 0xFE00B880, so offset from page = 0x880
 * ================================================================ */
#define MBOX_PAGE_PADDR     0xFE00B000
#define MBOX_PAGE_VADDR     0x610000

/* Offsets from page base (0xFE00B000) */
#define MBOX_READ_OFF       0x880   /* Mailbox 0 read */
#define MBOX_RD_STATUS_OFF  0x898   /* Mailbox 0 status (check EMPTY before read) */
#define MBOX_WRITE_OFF      0x8A0   /* Mailbox 1 write */
#define MBOX_WR_STATUS_OFF  0x8B8   /* Mailbox 1 status (check FULL before write) */

/* Status register bits */
#define MBOX_FULL           0x80000000
#define MBOX_EMPTY          0x40000000

/* Mailbox channels */
#define MBOX_CHANNEL_PROP   8       /* Property tags (ARM -> VC) */

/* Property tag IDs - temperature */
#define TAG_GET_TEMPERATURE 0x00030006
#define TAG_GET_MAX_TEMP    0x0003000A
#define TAG_END             0x00000000

/* Property tag IDs - clock management (used by bcm_power.c) */
#define TAG_GET_CLOCK_RATE  0x00030002
#define TAG_SET_CLOCK_RATE  0x00038002
#define TAG_GET_MIN_CLOCK   0x00030007
#define TAG_GET_MAX_CLOCK   0x00030004

/* Property request/response codes */
#define MBOX_REQUEST        0x00000000
#define MBOX_RESPONSE_OK    0x80000000

/* BCM2711 bus address conversion: ARM paddr -> GPU bus address */
#define ARM_TO_BUS(paddr)   ((paddr) | 0xC0000000)

/* ================================================================
 * Public API
 * ================================================================ */

/*
 * Initialize thermal monitoring.
 * Maps mailbox MMIO page at 0xFE00B000 via uart_device_map_page().
 * Allocates DMA buffer for property tag messages.
 * MUST be called BEFORE watchdog_init() and uart_init().
 * Returns 0 on success, -1 on failure.
 */
int thermal_init(void);

/*
 * Read current GPU/SoC temperature.
 * Returns temperature in millidegrees C (e.g., 45200 = 45.2C),
 * or -1 on error.
 */
int thermal_get_temp(void);

/*
 * Get thermal status string for shell commands.
 * Writes formatted status into output buffer.
 * Returns bytes written (excluding NUL).
 */
int thermal_get_status(char *output, uint32_t output_size);

/*
 * Check if thermal monitoring is initialized.
 */
bool thermal_is_initialized(void);

/*
 * Generic mailbox property tag call.
 * Sends a single property tag with the given request data.
 * buf[] = input data words, buf_words = number of input words,
 * resp_words = number of expected response words.
 * Response is written back into buf[].
 * Returns 0 on success, -1 on failure.
 */
int thermal_mailbox_tag(uint32_t tag, uint32_t *buf, uint32_t buf_words, uint32_t resp_words);

#endif /* BCM_THERMAL_H */
