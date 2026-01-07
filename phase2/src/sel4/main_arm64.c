/*
 * JARVIS AI-OS - Phase 2 Week 32 ARM64 Entry Point
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
    "  JARVIS AI-OS v0.2 - Phase 2 Week 32\n" \
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
#define UART_MAX_PAYLOAD        240
#define UART_FRAME_OVERHEAD     10      /* SYNC(2) + TYPE(1) + SEQ(2) + LEN(2) + FLAGS(1) + CRC(2) */

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
static bool uart_send_frame(uint8_t type, const uint8_t *payload, size_t payload_len)
{
    if (payload_len > UART_MAX_PAYLOAD) {
        return false;
    }

    uart_frame_t frame;
    memset(&frame, 0, sizeof(frame));

    /* Build frame */
    frame.sync = UART_SYNC_WORD;
    frame.type = type;
    frame.seq = g_seq_tx++;
    frame.length = (uint16_t)payload_len;
    frame.flags = 0;

    if (payload_len > 0 && payload != NULL) {
        memcpy(frame.payload, payload, payload_len);
    }

    /* Calculate CRC over header + payload (excluding CRC field) */
    size_t crc_len = UART_FRAME_OVERHEAD - 2 + payload_len;
    frame.crc = crc16_ccitt((uint8_t *)&frame, crc_len);

    /* Send frame via UART */
    size_t frame_len = UART_FRAME_OVERHEAD + payload_len;
    uart_write((uint8_t *)&frame, frame_len);

    g_frames_tx++;
    return true;
}

/*
 * Receive UART frame with CRC validation
 * Returns true if valid frame received, false on timeout/error
 */
static bool uart_recv_frame(uart_frame_t *frame, uint32_t timeout_ms)
{
    uint8_t *buf = (uint8_t *)frame;
    int bytes_read = 0;

    /* Wait for sync word */
    while (timeout_ms > 0) {
        if (uart_rx_ready()) {
            buf[bytes_read++] = uart_getc();

            /* Check for sync word */
            if (bytes_read >= 2) {
                if (frame->sync == UART_SYNC_WORD) {
                    break;  /* Found sync */
                }
                /* Shift and continue looking */
                buf[0] = buf[1];
                bytes_read = 1;
            }
        } else {
            /* Simple delay ~1ms */
            for (volatile int i = 0; i < 1000; i++);
            timeout_ms--;
        }
    }

    if (timeout_ms == 0) {
        return false;  /* Timeout waiting for sync */
    }

    /* Read header (6 more bytes after sync) */
    int header_remaining = 6;
    while (header_remaining > 0 && timeout_ms > 0) {
        if (uart_rx_ready()) {
            buf[bytes_read++] = uart_getc();
            header_remaining--;
        } else {
            for (volatile int i = 0; i < 1000; i++);
            timeout_ms--;
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
    while (payload_remaining > 0 && timeout_ms > 0) {
        if (uart_rx_ready()) {
            buf[bytes_read++] = uart_getc();
            payload_remaining--;
        } else {
            for (volatile int i = 0; i < 1000; i++);
            timeout_ms--;
        }
    }

    if (timeout_ms == 0) {
        return false;
    }

    /* Read CRC (2 bytes) */
    int crc_remaining = 2;
    uint8_t crc_buf[2];
    int crc_idx = 0;
    while (crc_remaining > 0 && timeout_ms > 0) {
        if (uart_rx_ready()) {
            crc_buf[crc_idx++] = uart_getc();
            crc_remaining--;
        } else {
            for (volatile int i = 0; i < 1000; i++);
            timeout_ms--;
        }
    }

    if (timeout_ms == 0) {
        return false;
    }

    uint16_t recv_crc = (crc_buf[1] << 8) | crc_buf[0];

    /* Validate CRC */
    size_t crc_len = UART_FRAME_OVERHEAD - 2 + frame->length;
    uint16_t calc_crc = crc16_ccitt(buf, crc_len);

    if (recv_crc != calc_crc) {
        g_crc_errors++;
        return false;
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

    uart_puts("[QUERY] ");
    uart_puts(query);
    uart_puts("\n");

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
        uart_puts("  [CACHE HIT] ");
        uart_puts(action);
        uart_puts("\n");
    } else {
        g_cache_misses++;
        resp_len = snprintf(response, sizeof(response),
                           "ACTION:unknown|TRUST:0|HIT:0");
        uart_puts("  [CACHE MISS]\n");
    }

    /* Send response */
    uart_send_frame(MSG_TYPE_RESPONSE, (uint8_t *)response, resp_len);
}

/*
 * Handle HEARTBEAT - send acknowledgment
 */
static void handle_heartbeat(uart_frame_t *request)
{
    (void)request;
    uart_puts("[HEARTBEAT] Received, sending ACK\n");
    uart_send_frame(MSG_TYPE_HEARTBEAT_ACK, NULL, 0);
}

/*
 * Handle STATS_REQUEST - send cache statistics
 */
static void handle_stats_request(uart_frame_t *request)
{
    (void)request;
    uart_puts("[STATS] Sending cache statistics\n");

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

    uart_send_frame(MSG_TYPE_STATS_RESPONSE, (uint8_t *)response, resp_len);
}

/*
 * Main IPC processing loop
 */
static void ipc_main_loop(void)
{
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("UART IPC Handler Running (ARM64)\n");
    uart_puts("Waiting for Python queries...\n");
    uart_puts("========================================\n\n");

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
                    uart_puts("[WARN] Unknown message type: ");
                    char num[8];
                    snprintf(num, sizeof(num), "0x%02X", frame.type);
                    uart_puts(num);
                    uart_puts("\n");
                    break;
            }
        }

        /* Periodic status (every ~100 frames or on timeout) */
        static int idle_count = 0;
        idle_count++;
        if (idle_count >= 100) {
            idle_count = 0;
            char status[128];
            snprintf(status, sizeof(status),
                "[STATUS] RX:%u TX:%u Hits:%u Misses:%u CRC_Err:%u\n",
                g_frames_rx, g_frames_tx, g_cache_hits, g_cache_misses, g_crc_errors);
            uart_puts(status);
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
    sel4_puts("  Phase: 2 Week 32\n");
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
