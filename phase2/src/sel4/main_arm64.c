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

/* JARVIS components */
#include "decision_cache.h"
#include "uart_pl011.h"

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

    /* Initialize UART driver (for IPC and UART output) */
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
