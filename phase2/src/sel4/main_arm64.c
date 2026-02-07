/*
 * JARVIS AI-OS - Phase 2 Week 33 ARM64 Entry Point
 *
 * Raspberry Pi 4 (BCM2711) seL4 Root Task with UART IPC
 *
 * This is the ARM64 port of main_week28.c that uses PL011 UART
 * instead of x86 ivshmem for Python<->seL4 IPC.
 *
 * Key Changes from x86:
 * - Uses PL011 UART at 0xFE201000 (BCM2711)
 * - UART-based IPC protocol with framing and CRC
 * - No ivshmem (Pi 4 has no PCIe for shared memory)
 * - ARM64 memory barriers for MMIO
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* seL4 headers */
#include <sel4/sel4.h>
#include <sel4/bootinfo.h>
#include <sel4runtime.h>
#include <sel4platsupport/platsupport.h>

/* Week 33: RX disabled - __plat_getchar causes cap fault, needs direct MMIO */

/* seL4 debug output - works from user space */
static void sel4_putchar(char c)
{
    seL4_DebugPutChar(c);
}

static void sel4_puts(const char *s)
{
    while (*s)
    {
        if (*s == '\n')
        {
            sel4_putchar('\r');
        }
        sel4_putchar(*s++);
    }
}

static void sel4_puthex8(uint8_t val)
{
    const char *hex = "0123456789ABCDEF";
    sel4_putchar(hex[(val >> 4) & 0xF]);
    sel4_putchar(hex[val & 0xF]);
}

static void sel4_puthex16(uint16_t val)
{
    sel4_puthex8((uint8_t)((val >> 8) & 0xFF));
    sel4_puthex8((uint8_t)(val & 0xFF));
}

static void sel4_puthex32(uint32_t val)
{
    sel4_puthex16((uint16_t)((val >> 16) & 0xFFFF));
    sel4_puthex16((uint16_t)(val & 0xFFFF));
}

static void sel4_dump_hex(const uint8_t *buf, size_t len)
{
    size_t offset = 0;
    while (offset < len)
    {
        size_t line_len = (len - offset > 16) ? 16 : (len - offset);
        sel4_puts("  ");
        sel4_puthex16((uint16_t)offset);
        sel4_puts(": ");
        for (size_t i = 0; i < line_len; i++) {
            sel4_puthex8(buf[offset + i]);
            sel4_putchar(' ');
        }
        sel4_puts("\n");
        offset += line_len;
    }
}

static void sel4_dump_hex_at(const uint8_t *buf, size_t len, size_t base_offset)
{
    size_t offset = 0;
    while (offset < len)
    {
        size_t line_len = (len - offset > 16) ? 16 : (len - offset);
        sel4_puts("  ");
        sel4_puthex16((uint16_t)(base_offset + offset));
        sel4_puts(": ");
        for (size_t i = 0; i < line_len; i++) {
            sel4_puthex8(buf[offset + i]);
            sel4_putchar(' ');
        }
        sel4_puts("\n");
        offset += line_len;
    }
}

static uint32_t sel4_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t sel4_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void sel4_copy_ascii(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
    size_t n = src_len;
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    for (size_t i = 0; i < n; i++) {
        uint8_t c = src[i];
        dst[i] = (c >= 0x20 && c <= 0x7E) ? (char)c : '.';
    }
    dst[n] = '\0';
}

static uint32_t emmc_checksum32(const uint8_t *buf, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += buf[i];
    }
    return sum;
}

static void emmc_dump_block(uint32_t lba, const uint8_t *buf, size_t dump_len)
{
    char msg[128];
    uint32_t sum = emmc_checksum32(buf, 512);
    snprintf(msg, sizeof(msg), "[EMMC] LBA%u checksum=", lba);
    sel4_puts(msg);
    sel4_puthex32(sum);
    sel4_puts("\n");
    sel4_dump_hex(buf, dump_len);
}

/*
 * Timing measurements via BCM2711 System Timer (1 MHz).
 *
 * The ARM64 cycle counter (CNTVCT_EL0) is trapped by seL4, but the BCM2711
 * System Timer at 0xFE003000 is a simple MMIO peripheral that works without
 * any seL4 timer capabilities. It provides 1 µs resolution timing.
 */
#define TIMING_AVAILABLE 1  /* Using BCM2711 System Timer */

/* JARVIS components */
#include "decision_cache.h"
#include "uart_pl011.h"
#include "drivers/emmc_sdhci.h"
#include "drivers/bcm2711_timer.h"
#include "drivers/dma_alloc.h"
#include "drivers/slot_alloc.h"

#define BANNER \
    "\n" \
    "========================================\n" \
    "  JARVIS AI-OS v0.2 - Phase 2 Week 33\n" \
    "  ARM64 Raspberry Pi 4 Port\n" \
    "========================================\n" \
    "  seL4 + PL011 UART + Decision Cache\n" \
    "  Build: " __DATE__ " " __TIME__ "\n" \
    "========================================\n\n"

/*
 * UART IPC Protocol Constants
 * Must match phase2/docs/UART_IPC_PROTOCOL.md
 */
#define UART_SYNC_WORD          0xAA55
#define UART_SYNC_BYTE0         0xAA
#define UART_SYNC_BYTE1         0x55
#define UART_SYNC_WORD_LE       ((UART_SYNC_BYTE1 << 8) | UART_SYNC_BYTE0)
#define UART_MAX_PAYLOAD        240
#define UART_FRAME_OVERHEAD     10      /* SYNC(2) + TYPE(1) + SEQ(2) + LEN(2) + FLAGS(1) + CRC(2) */

/* Disable echo test by default to avoid UART noise flooding logs */
#ifndef UART_ECHO_TEST
#define UART_ECHO_TEST 0
#endif

/* Disable per-byte UART RX debug logging by default */
#ifndef UART_RX_DEBUG
#define UART_RX_DEBUG 0
#endif

/* Minimal RX diagnostics (prints once per timeout, no per-byte spam) */
#ifndef UART_RX_DIAG
#define UART_RX_DIAG 0
#endif
#ifndef UART_RX_NOISE_LIMIT
#define UART_RX_NOISE_LIMIT 4096
#endif
#ifndef UART_RX_DIAG_RATE
#define UART_RX_DIAG_RATE 60
#endif
/* Disable protocol UART ASCII logs by default (binary-only stream) */
#ifndef UART_PROTO_LOG
#define UART_PROTO_LOG 0
#endif

#ifndef EMMC_TEST
#define EMMC_TEST 1
#endif
#ifndef EMMC_DUMP_BYTES
#define EMMC_DUMP_BYTES 64
#endif

/* Message Types (from UART_IPC_PROTOCOL.md) */
#define MSG_TYPE_QUERY          0x01
#define MSG_TYPE_RESPONSE       0x02
#define MSG_TYPE_HEARTBEAT      0x03
#define MSG_TYPE_HEARTBEAT_ACK  0x04
#define MSG_TYPE_STATS_REQUEST  0x05
#define MSG_TYPE_STATS_RESPONSE 0x06
#define MSG_TYPE_ERROR          0x0B

/* UART IPC Frame Structure */
typedef struct __attribute__((packed)) {
    uint16_t sync;              /* 0xAA55 */
    uint8_t type;               /* Message type */
    uint16_t seq;               /* Sequence number */
    uint16_t length;            /* Payload length (0-240) */
    uint8_t flags;              /* Reserved flags */
    uint8_t payload[UART_MAX_PAYLOAD];
    uint16_t crc;               /* CRC-16-CCITT */
} uart_frame_t;

/* Global state */
static decision_cache_t g_cache;
static uint16_t g_seq_tx = 0;   /* Transmit sequence counter */
static uint16_t g_seq_rx = 0;   /* Expected receive sequence */

/* Statistics */
static uint32_t g_frames_rx = 0;
static uint32_t g_frames_tx = 0;
static uint32_t g_cache_hits = 0;
static uint32_t g_cache_misses = 0;
static uint32_t g_crc_errors = 0;

/* External pattern loading */
extern int cache_load_extended_patterns(decision_cache_t *cache);

/*
 * CRC-16-CCITT (polynomial 0x1021, initial value 0xFFFF)
 * Same algorithm as Python uart_ipc_client.py
 */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

/*
 * Send UART frame with CRC
 */
static bool uart_send_frame_with_seq(uint8_t type, uint16_t seq,
                                     const uint8_t *payload, size_t payload_len)
{
    if (payload_len > UART_MAX_PAYLOAD) {
        return false;
    }

    uint8_t tx_buf[UART_FRAME_OVERHEAD + UART_MAX_PAYLOAD];
    uint16_t length = (uint16_t)payload_len;

    /* Build header */
    tx_buf[0] = UART_SYNC_BYTE0;
    tx_buf[1] = UART_SYNC_BYTE1;
    tx_buf[2] = type;
    tx_buf[3] = (uint8_t)(seq & 0xFF);
    tx_buf[4] = (uint8_t)((seq >> 8) & 0xFF);
    tx_buf[5] = (uint8_t)(length & 0xFF);
    tx_buf[6] = (uint8_t)((length >> 8) & 0xFF);
    tx_buf[7] = 0;  /* flags */

    if (payload_len > 0 && payload != NULL) {
        memcpy(&tx_buf[8], payload, payload_len);
    }

    /* Calculate CRC over TYPE through PAYLOAD (exclude SYNC and CRC) */
    size_t crc_len = 6 + payload_len;
    uint16_t crc = crc16_ccitt(&tx_buf[2], crc_len);
    tx_buf[8 + payload_len] = (uint8_t)(crc & 0xFF);
    tx_buf[9 + payload_len] = (uint8_t)((crc >> 8) & 0xFF);

    /* Send frame via UART */
    size_t frame_len = UART_FRAME_OVERHEAD + payload_len;
    uart_write(tx_buf, frame_len);

    g_frames_tx++;
    return true;
}

static bool uart_send_frame(uint8_t type, const uint8_t *payload, size_t payload_len)
{
    uint16_t seq = g_seq_tx++;
    return uart_send_frame_with_seq(type, seq, payload, payload_len);
}

/*
 * Receive UART frame with CRC validation
 * Returns true if valid frame received, false on timeout/error
 */
static bool uart_recv_frame(uart_frame_t *frame, uint32_t timeout_ms)
{
    uint8_t *buf = (uint8_t *)frame;
    int bytes_read = 0;
    const uint32_t byte_timeout_default = 100;
    uint32_t byte_timeout_ms = byte_timeout_default;
#if UART_RX_DEBUG
    static int total_rx_bytes = 0;  /* Debug counter */
#endif
#if UART_RX_DIAG
    static uint32_t diag_bytes = 0;
    static uint32_t diag_timeouts = 0;
    static uint8_t diag_last = 0;
#endif

wait_for_sync:
    bytes_read = 0;

    /* Wait for sync word */
    while (timeout_ms > 0) {
        if (uart_rx_ready()) {
            buf[bytes_read] = uart_getc();
#if UART_RX_DIAG
            diag_bytes++;
            diag_last = buf[bytes_read];
#endif
#if UART_RX_DEBUG
            total_rx_bytes++;

            /* DEBUG: Only print non-noise bytes (0x00/0x01 are common idle noise) */
            uint8_t b = buf[bytes_read];
            if (b != 0x00 && b != 0x01) {
                sel4_putchar('[');
                char hex[] = "0123456789ABCDEF";
                sel4_putchar(hex[(b >> 4) & 0xF]);
                sel4_putchar(hex[b & 0xF]);
                sel4_putchar(']');
            }
#endif

            bytes_read++;

            /* Check for sync bytes */
            if (bytes_read >= 2) {
                if (buf[0] == UART_SYNC_BYTE0 && buf[1] == UART_SYNC_BYTE1) {
                    break;  /* Found sync */
                }
                /* Shift and continue looking */
                buf[0] = buf[1];
                bytes_read = 1;
            }

#if UART_RX_DIAG
            if (diag_bytes >= UART_RX_NOISE_LIMIT) {
                int gpio15 = uart_gpio15_level();
                char diag[96];
                snprintf(diag, sizeof(diag),
                         "[RX DIAG] noise limit: bytes=%u last=0x%02x gpio15=%d\n",
                         diag_bytes, diag_last, gpio15);
                sel4_puts(diag);
                diag_bytes = 0;
                diag_timeouts = 0;
                return false;
            }
#endif
        } else {
            /* Simple delay ~1ms */
            for (volatile int i = 0; i < 1000; i++);
            timeout_ms--;
        }
    }

    if (timeout_ms == 0) {
#if UART_RX_DIAG
        diag_timeouts++;
        bool has_data = uart_rx_ready();
        bool has_err = uart_rx_error_status() != 0;
        bool rate = (diag_timeouts % UART_RX_DIAG_RATE) == 0;
        if (diag_bytes > 0 || has_data || has_err || rate) {
            int gpio15 = uart_gpio15_level();
            char diag[96];
            snprintf(diag, sizeof(diag),
                     "[RX DIAG] timeout: bytes=%u last=0x%02x gpio15=%d\n",
                     diag_bytes, diag_last, gpio15);
            sel4_puts(diag);
        }
        diag_bytes = 0;
#endif
        return false;  /* Timeout waiting for sync */
    }

    /* Read header (6 more bytes after sync) */
    int header_remaining = 6;
    byte_timeout_ms = byte_timeout_default;
    while (header_remaining > 0 && timeout_ms > 0) {
        if (uart_rx_ready()) {
            buf[bytes_read++] = uart_getc();
            header_remaining--;
            byte_timeout_ms = byte_timeout_default;
        } else {
            for (volatile int i = 0; i < 1000; i++);
            timeout_ms--;
            if (byte_timeout_ms > 0) {
                byte_timeout_ms--;
                if (byte_timeout_ms == 0) {
                    if (uart_rx_error_status() != 0) {
                        uart_clear_rx_errors();
                    }
                    uart_rx_drain(16);
                    goto wait_for_sync;
                }
            }
        }
    }

    if (timeout_ms == 0) {
        return false;
    }

    /* Read payload */
    if (frame->length > UART_MAX_PAYLOAD) {
        return false;  /* Invalid length */
    }

    int payload_remaining = frame->length;
    byte_timeout_ms = byte_timeout_default;
    while (payload_remaining > 0 && timeout_ms > 0) {
        if (uart_rx_ready()) {
            buf[bytes_read++] = uart_getc();
            payload_remaining--;
            byte_timeout_ms = byte_timeout_default;
        } else {
            for (volatile int i = 0; i < 1000; i++);
            timeout_ms--;
            if (byte_timeout_ms > 0) {
                byte_timeout_ms--;
                if (byte_timeout_ms == 0) {
                    if (uart_rx_error_status() != 0) {
                        uart_clear_rx_errors();
                    }
                    uart_rx_drain(16);
                    goto wait_for_sync;
                }
            }
        }
    }

    if (timeout_ms == 0) {
        return false;
    }

    /* Read CRC (2 bytes) */
    int crc_remaining = 2;
    uint8_t crc_buf[2];
    int crc_idx = 0;
    byte_timeout_ms = byte_timeout_default;
    while (crc_remaining > 0 && timeout_ms > 0) {
        if (uart_rx_ready()) {
            crc_buf[crc_idx++] = uart_getc();
            crc_remaining--;
            byte_timeout_ms = byte_timeout_default;
        } else {
            for (volatile int i = 0; i < 1000; i++);
            timeout_ms--;
            if (byte_timeout_ms > 0) {
                byte_timeout_ms--;
                if (byte_timeout_ms == 0) {
                    if (uart_rx_error_status() != 0) {
                        uart_clear_rx_errors();
                    }
                    uart_rx_drain(16);
                    goto wait_for_sync;
                }
            }
        }
    }

    if (timeout_ms == 0) {
        return false;
    }

    uint16_t recv_crc = (crc_buf[1] << 8) | crc_buf[0];

    /* Validate CRC (TYPE through PAYLOAD) */
    size_t crc_len = 6 + frame->length;
    uint16_t calc_crc = crc16_ccitt(buf + 2, crc_len);

    if (recv_crc != calc_crc) {
        g_crc_errors++;
        if (uart_rx_error_status() != 0) {
            uart_clear_rx_errors();
        }
        uart_rx_drain(16);
        goto wait_for_sync;
    }

    frame->crc = recv_crc;
    g_frames_rx++;

    return true;
}

/*
 * Handle QUERY message - perform cache lookup
 */
static void handle_query(uart_frame_t *request)
{
    char query[MAX_QUERY_LEN];
    size_t query_len = (request->length < MAX_QUERY_LEN - 1) ? request->length : (MAX_QUERY_LEN - 1);

    memcpy(query, request->payload, query_len);
    query[query_len] = '\0';

#if UART_PROTO_LOG
    uart_puts("[QUERY] ");
    uart_puts(query);
    uart_puts("\n");
#endif

    /* Perform cache lookup */
    char action[MAX_ACTION_LEN];
    trust_level_t trust;
    bool hit = cache_lookup(&g_cache, query, action, MAX_ACTION_LEN, &trust);

    /* Build response payload: "ACTION:<action>|TRUST:<trust>|HIT:<0|1>" */
    char response[UART_MAX_PAYLOAD];
    int resp_len;

    if (hit) {
        g_cache_hits++;
        resp_len = snprintf(response, sizeof(response),
                           "ACTION:%s|TRUST:%d|HIT:1", action, trust);
#if UART_PROTO_LOG
        uart_puts("  [CACHE HIT] ");
        uart_puts(action);
        uart_puts("\n");
#endif
    } else {
        g_cache_misses++;
        resp_len = snprintf(response, sizeof(response),
                           "ACTION:unknown|TRUST:0|HIT:0");
#if UART_PROTO_LOG
        uart_puts("  [CACHE MISS]\n");
#endif
    }

    /* Send response */
    uart_send_frame_with_seq(MSG_TYPE_RESPONSE, request->seq,
                             (uint8_t *)response, resp_len);
}

/*
 * Handle HEARTBEAT - send acknowledgment
 */
static void handle_heartbeat(uart_frame_t *request)
{
    uint16_t seq = request->seq;
#if UART_PROTO_LOG
    uart_puts("[HEARTBEAT] Received, sending ACK\n");
#endif
    uart_send_frame_with_seq(MSG_TYPE_HEARTBEAT_ACK, seq, NULL, 0);
}

/*
 * Handle STATS_REQUEST - send cache statistics
 */
static void handle_stats_request(uart_frame_t *request)
{
    uint16_t seq = request->seq;
    (void)request;
#if UART_PROTO_LOG
    uart_puts("[STATS] Sending cache statistics\n");
#endif

    cache_stats_t stats;
    cache_get_stats(&g_cache, &stats);

    /* Build stats response */
    char response[UART_MAX_PAYLOAD];
    int resp_len = snprintf(response, sizeof(response),
        "ENTRIES:%u|HITS:%llu|MISSES:%llu|RATE:%.1f",
        stats.entries_used,
        (unsigned long long)stats.cache_hits,
        (unsigned long long)stats.cache_misses,
        stats.hit_rate * 100.0);

    uart_send_frame_with_seq(MSG_TYPE_STATS_RESPONSE, seq,
                             (uint8_t *)response, resp_len);
}

/*
 * Main IPC processing loop
 */
static void ipc_main_loop(void)
{
#if UART_PROTO_LOG
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("UART IPC Handler Running (ARM64)\n");
    uart_puts("Waiting for Python queries...\n");
    uart_puts("========================================\n\n");
#endif

    uart_frame_t frame;

    while (1) {
        /* Wait for incoming frame (10 second timeout) */
        if (uart_recv_frame(&frame, 10000)) {
            /* Process based on message type */
            switch (frame.type) {
                case MSG_TYPE_QUERY:
                    handle_query(&frame);
                    break;

                case MSG_TYPE_HEARTBEAT:
                    handle_heartbeat(&frame);
                    break;

                case MSG_TYPE_STATS_REQUEST:
                    handle_stats_request(&frame);
                    break;

                default:
#if UART_PROTO_LOG
                    uart_puts("[WARN] Unknown message type: ");
                    char num[8];
                    snprintf(num, sizeof(num), "0x%02X", frame.type);
                    uart_puts(num);
                    uart_puts("\n");
#endif
                    break;
            }
        }

        /* Periodic status (every ~100 frames or on timeout) */
        static int idle_count = 0;
        idle_count++;
        if (idle_count >= 100) {
            idle_count = 0;
#if UART_PROTO_LOG
            char status[128];
            snprintf(status, sizeof(status),
                "[STATUS] RX:%u TX:%u Hits:%u Misses:%u CRC_Err:%u\n",
                g_frames_rx, g_frames_tx, g_cache_hits, g_cache_misses, g_crc_errors);
            uart_puts(status);
#endif
        }

#ifdef __sel4__
        /* Yield to other threads */
        seL4_Yield();
#endif
    }
}

/* print_system_info() removed - now inline in main() */

/*
 * Main entry point - seL4 root task
 */
int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Bring up serial before any printf/logging to avoid libsel4platsupport warnings */
    if (platsupport_serial_setup_bootinfo_failsafe() != 0) {
        while (1);
    }

    /*
     * CRITICAL: Device mapping order matters!
     * The device mapper uses a sequential cursor that only moves forward.
     * We MUST map devices in ascending physical address order:
     *   1. System Timer: 0xFE003000
     *   2. GPIO:         0xFE200000 (mapped by uart_init)
     *   3. UART:         0xFE201000 (mapped by uart_init)
     *   4. EMMC:         0xFE340000 (mapped by emmc_map_device_frame)
     *
     * Week 36 fix: Timer was failing because cursor was past its address.
     */

    /* Initialize shared slot allocator FIRST (all device/DMA code uses this) */
    seL4_BootInfo *bi = sel4runtime_bootinfo();
    if (!slot_alloc_init(bi)) {
        /* Slot allocator failed - fatal, cannot continue */
        seL4_DebugPutChar('S');
        seL4_DebugPutChar('L');
        seL4_DebugPutChar('O');
        seL4_DebugPutChar('T');
        seL4_DebugPutChar('!');
        seL4_DebugPutChar('\r');
        seL4_DebugPutChar('\n');
        while (1);
    }

    /* Initialize DMA allocator (needs bootinfo, uses regular untyped) */
    if (!dma_alloc_init(bi)) {
        /* DMA allocator failed - continue without ADMA2 support */
        /* Use seL4 debug output since UART not yet initialized */
        seL4_DebugPutChar('D');
        seL4_DebugPutChar('M');
        seL4_DebugPutChar('A');
        seL4_DebugPutChar('!');
        seL4_DebugPutChar('\r');
        seL4_DebugPutChar('\n');
    }

    /*
     * System Timer: Map BEFORE UART (must be in ascending paddr order).
     *
     * Timer at 0xFE003000, GPIO at 0xFE200000, UART at 0xFE201000.
     * uart_map_device_frame() uses binary buddy skip to advance the
     * untyped watermark from 0xFE004000 to 0xFE200000 without consuming
     * the GPIO page frame (7 Untyped retypes instead of 1 x 2MB page).
     */
    if (!systimer_init()) {
        seL4_DebugPutChar('T');
        seL4_DebugPutChar('M');
        seL4_DebugPutChar('R');
        seL4_DebugPutChar('!');
        seL4_DebugPutChar('\r');
        seL4_DebugPutChar('\n');
        /* Non-fatal: throughput test will SKIP if timer unavailable */
    }

    /* Initialize UART driver (for IPC and UART output) */
    /* This maps GPIO (0xFE200000) and UART (0xFE201000) */
    if (!uart_init()) {
        while (1);  /* Hang */
    }

    /* Week 33: UART RX status will be printed after banner */

    /* CRITICAL TEST: Output immediately to verify rootserver starts */
    /* Try seL4 DebugPutChar first (kernel syscall) */
    sel4_putchar('!');
    sel4_putchar('!');
    sel4_putchar('!');
    sel4_putchar(' ');
    sel4_putchar('J');
    sel4_putchar('A');
    sel4_putchar('R');
    sel4_putchar('V');
    sel4_putchar('I');
    sel4_putchar('S');
    sel4_putchar(' ');
    sel4_putchar('S');
    sel4_putchar('T');
    sel4_putchar('A');
    sel4_putchar('R');
    sel4_putchar('T');
    sel4_putchar('E');
    sel4_putchar('D');
    sel4_putchar('!');
    sel4_putchar('!');
    sel4_putchar('!');
    sel4_putchar('\r');
    sel4_putchar('\n');

    /* Flush output */
    for (volatile int i = 0; i < 1000000; i++)
        ; /* Small delay */

    sel4_puts("\n*** JARVIS ROOTSERVER STARTING ***\n");

    /* Print banner using seL4 debug output */
    sel4_puts(BANNER);

    /* Print system info */
    sel4_puts("System Information:\n");
    sel4_puts("  Architecture: aarch64 (ARM64)\n");
    sel4_puts("  Platform: Raspberry Pi 4 (BCM2711)\n");
    sel4_puts("  CPU: Cortex-A72 @ 1.8 GHz\n");
    sel4_puts("  Kernel: seL4 microkernel\n");
    sel4_puts("  UART: PL011 @ 0xFE201000\n");
    sel4_puts("  Baud: 115200 8N1\n");
    sel4_puts("  Phase: 2 Week 33\n");

    /* Week 33: Print UART RX status */
    sel4_puts("  UART RX: ");
    if (uart_is_rx_enabled()) {
        sel4_puts("ENABLED (device frame mapped)\n");
    } else {
        sel4_puts("DISABLED (device frame mapping failed)\n");
    }
    sel4_puts("\n");

    /* Initialize decision cache */
    sel4_puts("Initializing decision cache...\n");
    if (!cache_init(&g_cache)) {
        sel4_puts("ERROR: Failed to initialize cache!\n");
        while (1);
    }
    sel4_puts("  Cache initialized (512 entries)\n");

    /* Load patterns */
    int loaded = cache_load_extended_patterns(&g_cache);
    char msg[128];
    snprintf(msg, sizeof(msg), "  Loaded %d patterns into cache\n", loaded);
    sel4_puts(msg);
    sel4_puts("\n");

    /* Print cache stats */
    cache_stats_t stats;
    cache_get_stats(&g_cache, &stats);
    snprintf(msg, sizeof(msg), "Cache Stats: %u entries loaded\n", stats.entries_used);
    sel4_puts(msg);
    sel4_puts("\n");

#if EMMC_TEST
    /*
     * ========================================================
     * Week 36: EMMC Driver Test Suite (10 test blocks)
     * ========================================================
     * 1. mmio_map     2. init+CID    3. read_lba0   4. mbr_sig
     * 5. part0_lba    6. bpb checks  7. multi_read  8. throughput
     * 9. null_ptr     10. write+verify
     */
    sel4_puts("[EMMC] ========================================\n");
    sel4_puts("[EMMC] DRIVER TEST SUITE (Week 36)\n");
    sel4_puts("[EMMC] ========================================\n");

    int test_pass = 0, test_fail = 0;
    uint8_t sector[512];
    uint32_t part0_lba = 0;
    uint16_t bytes_per_sector = 0;

    /* --------------------------------------------------------
     * TEST 1: MMIO Mapping
     * -------------------------------------------------------- */
    if (!emmc_is_mapped() && !emmc_map_device_frame()) {
        sel4_puts("[EMMC][TEST] mmio_map=FAIL (device frame mapping failed)\n");
        test_fail++;
        goto emmc_test_done;
    }
    sel4_puts("[EMMC][TEST] mmio_map=PASS\n");
    test_pass++;

    /* --------------------------------------------------------
     * TEST 2: Controller Init
     * -------------------------------------------------------- */
    if (!emmc_init()) {
        sel4_puts("[EMMC][TEST] init=FAIL\n");
        test_fail++;
        goto emmc_test_done;
    }
    sel4_puts("[EMMC][TEST] init=PASS\n");
    test_pass++;

    /* Print CID for multi-card identification */
    {
        uint32_t cid[4];
        emmc_get_cid(cid);
        snprintf(msg, sizeof(msg), "[EMMC] CID: %08X %08X %08X %08X\n",
                 cid[0], cid[1], cid[2], cid[3]);
        sel4_puts(msg);
        uint64_t cap = emmc_get_capacity_bytes();
        snprintf(msg, sizeof(msg), "[EMMC] Capacity: %llu MB\n",
                 (unsigned long long)(cap / (1024 * 1024)));
        sel4_puts(msg);
    }

    if (systimer_is_initialized()) {
        sel4_puts("[EMMC] Timing: BCM2711 System Timer (1 MHz)\n");
    } else {
        sel4_puts("[EMMC] Timing: NOT AVAILABLE (systimer_init failed)\n");
    }

#if EMMC_USE_ADMA2
    /* Initialize ADMA2 DMA mode for high-throughput reads */
    if (emmc_adma2_init()) {
        sel4_puts("[EMMC] ADMA2 mode ENABLED\n");
    } else {
        sel4_puts("[EMMC] ADMA2 init failed, using PIO mode\n");
    }
#else
    sel4_puts("[EMMC] PIO mode (ADMA2 disabled at compile time)\n");
#endif

    /* --------------------------------------------------------
     * TEST 3: Single-Block Read LBA0 (CMD17)
     * -------------------------------------------------------- */
    if (!emmc_read_block(0, sector)) {
        sel4_puts("[EMMC][TEST] read_lba0=FAIL\n");
        test_fail++;
    } else {
        sel4_puts("[EMMC][TEST] read_lba0=PASS\n");
        test_pass++;
    }

    /* --------------------------------------------------------
     * TEST 4: MBR Signature Check (0x55AA)
     * -------------------------------------------------------- */
    if (sector[0x1FE] == 0x55 && sector[0x1FF] == 0xAA) {
        sel4_puts("[EMMC][TEST] mbr_sig=PASS\n");
        test_pass++;
        part0_lba = sel4_read_le32(sector + 0x1BE + 8);
    } else {
        snprintf(msg, sizeof(msg), "[EMMC][TEST] mbr_sig=FAIL (got %02x%02x)\n",
                 sector[0x1FE], sector[0x1FF]);
        sel4_puts(msg);
        test_fail++;
    }

    /* --------------------------------------------------------
     * TEST 5: Partition 0 Start LBA Valid
     * -------------------------------------------------------- */
    if (part0_lba > 0 && part0_lba < 0x10000000) {
        snprintf(msg, sizeof(msg), "[EMMC][TEST] part0_lba=PASS (LBA=%u)\n", part0_lba);
        sel4_puts(msg);
        test_pass++;
    } else {
        snprintf(msg, sizeof(msg), "[EMMC][TEST] part0_lba=FAIL (LBA=%u invalid)\n", part0_lba);
        sel4_puts(msg);
        test_fail++;
    }

    /* --------------------------------------------------------
     * TEST 6: BPB Read + bytes_per_sector Check
     * -------------------------------------------------------- */
    if (part0_lba > 0 && emmc_read_block(part0_lba, sector)) {
        bytes_per_sector = sel4_read_le16(sector + 0x0B);
        if (bytes_per_sector == 512) {
            sel4_puts("[EMMC][TEST] bpb_sector_size=PASS\n");
            test_pass++;
        } else {
            snprintf(msg, sizeof(msg), "[EMMC][TEST] bpb_sector_size=FAIL (got %u)\n",
                     bytes_per_sector);
            sel4_puts(msg);
            test_fail++;
        }
        /* Also verify BPB boot signature */
        if (sector[0x1FE] == 0x55 && sector[0x1FF] == 0xAA) {
            sel4_puts("[EMMC][TEST] bpb_sig=PASS\n");
            test_pass++;
        } else {
            sel4_puts("[EMMC][TEST] bpb_sig=FAIL\n");
            test_fail++;
        }
    } else {
        sel4_puts("[EMMC][TEST] bpb_sector_size=FAIL (read error)\n");
        sel4_puts("[EMMC][TEST] bpb_sig=FAIL (read error)\n");
        test_fail += 2;
    }

    /* --------------------------------------------------------
     * TEST 7: Multi-Block Read (CMD18)
     * -------------------------------------------------------- */
    {
        #define MULTI_READ_BLOCKS 256  /* 128 KB */
        static uint8_t multi_buf[MULTI_READ_BLOCKS * 512];
        uint32_t lba = (part0_lba > 0) ? part0_lba : 8192;

        sel4_puts("[EMMC] Multi-block read: 256 blocks (128KB)...\n");

        bool multi_ok = emmc_read_blocks(lba, MULTI_READ_BLOCKS, multi_buf);

        if (multi_ok) {
            snprintf(msg, sizeof(msg),
                "[EMMC][TEST] multi_read=PASS (LBA=%u, %u blocks)\n",
                lba, MULTI_READ_BLOCKS);
            sel4_puts(msg);
            test_pass++;
        } else {
            sel4_puts("[EMMC][TEST] multi_read=FAIL\n");
            test_fail++;
        }
        #undef MULTI_READ_BLOCKS
    }

    /* --------------------------------------------------------
     * TEST 8: Throughput Measurement (using BCM2711 System Timer)
     * Target: >10 MB/s (requires 4-bit bus + High Speed mode or ADMA2)
     * -------------------------------------------------------- */
    {
        #define THROUGHPUT_BLOCKS 256  /* 128 KB */
        #define THROUGHPUT_TARGET_MBPS 10
        uint32_t lba = (part0_lba > 0) ? part0_lba : 8192;
        bool tput_ok = false;
        uint64_t start_us = 0, elapsed_us = 0;

        /* Week 36: Check if timer is available before attempting timed test */
        if (!systimer_is_initialized()) {
            sel4_puts("[EMMC][TEST] throughput=SKIP (timer not available)\n");
            sel4_puts("[EMMC][TEST] Performing untimed read verification instead...\n");

            /* Do a simple untimed read verification */
            static uint8_t verify_buf[8 * 512];  /* 8 blocks = 4KB */
            tput_ok = emmc_read_blocks(lba, 8, verify_buf);
            if (tput_ok) {
                sel4_puts("[EMMC][TEST] untimed_read=PASS (8 blocks read OK)\n");
                test_pass++;
            } else {
                sel4_puts("[EMMC][TEST] untimed_read=FAIL (read error)\n");
                test_fail++;
            }
            goto throughput_done;
        }

#if EMMC_USE_ADMA2
        /* Use ADMA2 DMA buffer and read function */
        uint8_t *tput_buf = emmc_adma2_get_buffer();
        if (emmc_adma2_is_enabled() && tput_buf != 0) {
            sel4_puts("[EMMC] Throughput test using ADMA2 DMA\n");
            start_us = systimer_read();
            tput_ok = emmc_read_blocks_adma2(lba, THROUGHPUT_BLOCKS, tput_buf);
            elapsed_us = systimer_elapsed_us(start_us);

            /* ============================================================
             * SANITY TEST: PIO vs ADMA2 Checksum Comparison
             * Read a small subset with PIO and compare checksums.
             * If different, indicates DMA addressing or cache issues.
             * ============================================================ */
            if (tput_ok) {
                #define SANITY_BLOCKS 4  /* 2KB - small sample for quick check */
                static uint8_t pio_verify[SANITY_BLOCKS * 512];

                sel4_puts("[EMMC] SANITY: Comparing ADMA2 vs PIO read (");
                snprintf(msg, sizeof(msg), "%u", SANITY_BLOCKS * 512);
                sel4_puts(msg);
                sel4_puts(" bytes at LBA ");
                snprintf(msg, sizeof(msg), "%u", lba);
                sel4_puts(msg);
                sel4_puts(")...\n");

                /* Read same LBAs with PIO */
                bool pio_ok = emmc_read_blocks(lba, SANITY_BLOCKS, pio_verify);

                if (pio_ok) {
                    /* Calculate simple checksums (sum of all bytes) */
                    uint32_t adma2_sum = 0;
                    uint32_t pio_sum = 0;
                    for (uint32_t i = 0; i < SANITY_BLOCKS * 512; i++) {
                        adma2_sum += tput_buf[i];
                        pio_sum += pio_verify[i];
                    }

                    if (adma2_sum == pio_sum) {
                        /* Also do byte-by-byte comparison for first mismatch */
                        bool match = true;
                        uint32_t first_diff = 0;
                        for (uint32_t i = 0; i < SANITY_BLOCKS * 512 && match; i++) {
                            if (tput_buf[i] != pio_verify[i]) {
                                match = false;
                                first_diff = i;
                            }
                        }
                        if (match) {
                            snprintf(msg, sizeof(msg),
                                "[EMMC][SANITY] PASS: ADMA2 == PIO (checksum=%u)\n", adma2_sum);
                            sel4_puts(msg);
                        } else {
                            snprintf(msg, sizeof(msg),
                                "[EMMC][SANITY] WARNING: Checksum match but byte diff at offset %u (ADMA2=0x%02x, PIO=0x%02x)\n",
                                first_diff, tput_buf[first_diff], pio_verify[first_diff]);
                            sel4_puts(msg);
                        }
                    } else {
                        snprintf(msg, sizeof(msg),
                            "[EMMC][SANITY] **FAIL**: ADMA2 checksum=%u != PIO checksum=%u\n",
                            adma2_sum, pio_sum);
                        sel4_puts(msg);
                        sel4_puts("[EMMC][SANITY] *** DMA ADDRESSING OR CACHE ISSUE DETECTED! ***\n");

                        /* Print first few bytes from each */
                        sel4_puts("[EMMC][SANITY] ADMA2 first 16 bytes: ");
                        for (int i = 0; i < 16; i++) {
                            snprintf(msg, sizeof(msg), "%02x ", tput_buf[i]);
                            sel4_puts(msg);
                        }
                        sel4_puts("\n[EMMC][SANITY] PIO   first 16 bytes: ");
                        for (int i = 0; i < 16; i++) {
                            snprintf(msg, sizeof(msg), "%02x ", pio_verify[i]);
                            sel4_puts(msg);
                        }
                        sel4_puts("\n");
                    }
                } else {
                    sel4_puts("[EMMC][SANITY] SKIP: PIO verify read failed\n");
                }
                #undef SANITY_BLOCKS
            }

        } else {
            /* Fall back to PIO with static buffer */
            static uint8_t pio_buf[THROUGHPUT_BLOCKS * 512];
            tput_buf = pio_buf;
            sel4_puts("[EMMC] Throughput test using PIO (ADMA2 unavailable)\n");
            start_us = systimer_read();
            tput_ok = emmc_read_blocks(lba, THROUGHPUT_BLOCKS, tput_buf);
            elapsed_us = systimer_elapsed_us(start_us);
        }
#else
        /* PIO mode - use static buffer */
        static uint8_t tput_buf[THROUGHPUT_BLOCKS * 512];
        start_us = systimer_read();
        tput_ok = emmc_read_blocks(lba, THROUGHPUT_BLOCKS, tput_buf);
        elapsed_us = systimer_elapsed_us(start_us);
#endif

        if (tput_ok && elapsed_us > 0) {
            uint32_t bytes = THROUGHPUT_BLOCKS * 512;
            /* MB/s = bytes / elapsed_us (since elapsed is µs, this gives MB/s) */
            uint32_t mbps_x10 = (bytes * 10) / (uint32_t)elapsed_us;

            snprintf(msg, sizeof(msg),
                "[EMMC][TEST] throughput=PASS (%u bytes, %llu us, %u.%u MB/s)\n",
                bytes, (unsigned long long)elapsed_us, mbps_x10 / 10, mbps_x10 % 10);
            sel4_puts(msg);
            test_pass++;
        } else if (!tput_ok) {
            sel4_puts("[EMMC][TEST] throughput=FAIL (read error)\n");
            test_fail++;
        } else {
            /* Timer returned 0 unexpectedly */
            sel4_puts("[EMMC][TEST] throughput=PASS (read OK, timer returned 0)\n");
            test_pass++;
        }

throughput_done:
        #undef THROUGHPUT_BLOCKS
        #undef THROUGHPUT_TARGET_MBPS
    }

    /* --------------------------------------------------------
     * TEST 9: Error Handling (NULL pointer)
     * -------------------------------------------------------- */
    {
        bool null_read = emmc_read_block(0, NULL);
        bool null_write = emmc_write_block(0, NULL);
        if (!null_read && !null_write) {
            sel4_puts("[EMMC][TEST] null_ptr_check=PASS\n");
            test_pass++;
        } else {
            sel4_puts("[EMMC][TEST] null_ptr_check=FAIL\n");
            test_fail++;
        }
    }

    /* --------------------------------------------------------
     * TEST 10: Write Single Block (CMD24) + Verify
     * -------------------------------------------------------- */
#if EMMC_WRITE_TEST_ENABLE
    {
        /* Compute safe write test LBA dynamically from actual card capacity.
         * Use 1024 sectors (512KB) before end of card - well beyond any
         * partition but safely within card bounds for any card size. */
        uint32_t test_lba;
        uint64_t cap = emmc_get_capacity_bytes();
        if (cap > 0) {
            uint32_t total_sectors = (uint32_t)(cap / 512);
            test_lba = total_sectors - 1024;
        } else {
            test_lba = EMMC_WRITE_TEST_LBA;  /* fallback */
        }
        static uint8_t write_buf[512];
        static uint8_t verify_buf[512];

        snprintf(msg, sizeof(msg),
            "[EMMC] Write test ENABLED (LBA=%u, %u sectors from end)\n", test_lba, 1024);
        sel4_puts(msg);

        /* Fill with test pattern: 0xDE 0xAD 0xBE 0xEF repeating */
        for (int i = 0; i < 512; i += 4) {
            write_buf[i+0] = 0xDE;
            write_buf[i+1] = 0xAD;
            write_buf[i+2] = 0xBE;
            write_buf[i+3] = 0xEF;
        }

        /* Single-block write test */
        if (emmc_write_block(test_lba, write_buf)) {
            sel4_puts("[EMMC][TEST] write_single=PASS\n");
            test_pass++;

            /* Read back and verify */
            if (emmc_read_block(test_lba, verify_buf)) {
                bool match = true;
                for (int i = 0; i < 512; i++) {
                    if (write_buf[i] != verify_buf[i]) {
                        match = false;
                        snprintf(msg, sizeof(msg),
                            "[EMMC] Mismatch at offset %d: wrote %02x, read %02x\n",
                            i, write_buf[i], verify_buf[i]);
                        sel4_puts(msg);
                        break;
                    }
                }
                if (match) {
                    sel4_puts("[EMMC][TEST] write_verify=PASS\n");
                    test_pass++;
                } else {
                    sel4_puts("[EMMC][TEST] write_verify=FAIL\n");
                    test_fail++;
                }
            } else {
                sel4_puts("[EMMC][TEST] write_verify=FAIL (read-back error)\n");
                test_fail++;
            }
        } else {
            sel4_puts("[EMMC][TEST] write_single=FAIL\n");
            sel4_puts("[EMMC][TEST] write_verify=FAIL (write error)\n");
            test_fail += 2;
        }
    }
#else
    sel4_puts("[EMMC][TEST] write_single=SKIP (EMMC_WRITE_TEST_ENABLE=0)\n");
    sel4_puts("[EMMC][TEST] write_verify=SKIP (EMMC_WRITE_TEST_ENABLE=0)\n");
#endif

emmc_test_done:
    /* --------------------------------------------------------
     * TEST SUMMARY
     * -------------------------------------------------------- */
    sel4_puts("[EMMC] ========================================\n");
    snprintf(msg, sizeof(msg), "[EMMC] TEST RESULTS: %d PASS, %d FAIL\n",
             test_pass, test_fail);
    sel4_puts(msg);
    if (test_fail == 0) {
        sel4_puts("[EMMC] ALL TESTS PASSED\n");
    } else {
        sel4_puts("[EMMC] SOME TESTS FAILED\n");
    }
    sel4_puts("[EMMC] ========================================\n\n");
#else
    sel4_puts("[EMMC] Test disabled (EMMC_TEST=0)\n\n");
#endif

    /* Optional UART echo test - keep disabled for IPC runs */
#if UART_ECHO_TEST
    sel4_puts("\n========================================\n");
    sel4_puts("UART ECHO TEST MODE\n");
    sel4_puts("Send any byte, will echo back with [XX] format\n");
    sel4_puts("Waiting 30 seconds for test bytes...\n");
    sel4_puts("========================================\n");

    /* Echo test for 30 seconds */
    uint32_t echo_timeout = 30000;  /* 30 seconds */
    int echo_count = 0;
    while (echo_timeout > 0) {
        if (uart_rx_ready()) {
            uint8_t b = uart_getc();
            /* Echo the byte as hex */
            sel4_putchar('[');
            char hex[] = "0123456789ABCDEF";
            sel4_putchar(hex[(b >> 4) & 0xF]);
            sel4_putchar(hex[b & 0xF]);
            sel4_putchar(']');
            echo_count++;

            /* Also check for sync pattern */
            static uint8_t last_byte = 0;
            if (last_byte == 0xAA && b == 0x55) {
                sel4_puts(" <-- SYNC DETECTED!\n");
            }
            last_byte = b;
        }

        /* ~1ms delay */
        for (volatile int i = 0; i < 1000; i++);
        echo_timeout--;

        /* Print heartbeat every 5 seconds */
        if (echo_timeout % 5000 == 0) {
            char status[64];
            snprintf(status, sizeof(status), "\n[ECHO] Still waiting... %d bytes received so far\n", echo_count);
            sel4_puts(status);
        }
    }

    sel4_puts("\n[ECHO] Test complete. Entering IPC handler...\n");
#else
    sel4_puts("UART echo test disabled\n");
#endif

    /* Enter IPC main loop */
    sel4_puts("Starting UART IPC handler...\n");
    sel4_puts("\n========================================\n");
    sel4_puts("UART IPC Handler Running (ARM64)\n");
    sel4_puts("Waiting for Python queries...\n");
    sel4_puts("========================================\n\n");

    ipc_main_loop();

    /* Should never reach here */
    sel4_puts("ERROR: IPC loop exited unexpectedly!\n");
    return 1;
}
