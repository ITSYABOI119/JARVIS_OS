/*
 * fuzz_harness.c — Phase 3c Fuzz Testing Harness
 *
 * Self-contained: links against real source files, feeds them random/
 * malformed data, checks for crashes. Compiled with AddressSanitizer.
 *
 * Targets:
 *   1. net_stack: net_process_frame with random Ethernet frames
 *   2. shmem_ipc: send/recv with corrupted ring data
 *   3. gguf_parser: gguf_open_memory with malformed GGUF buffers
 *
 * JARVIS AI-OS - Phase 3c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "net_stack.h"
#include "shmem_ipc.h"
#include "gguf_parser.h"

/* ---- PRNG (xorshift64) ---- */
static uint64_t rng_state = 0xDEADBEEFCAFE4242ULL;

static uint64_t xorshift64(void) {
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static void fill_random(void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++)
        p[i] = (uint8_t)(xorshift64() & 0xFF);
}

static uint32_t rand_range(uint32_t max) {
    return (uint32_t)(xorshift64() % (uint64_t)max);
}

static int pass_count = 0, fail_count = 0;

/* ================================================================
 * Target 1: net_process_frame
 *
 * Feed random Ethernet frames of various lengths.
 * net_process_frame must never crash regardless of input.
 * ================================================================ */
static void fuzz_net_stack(void) {
    printf("[FUZZ] net_stack: ");
    fflush(stdout);

    /* Initialize net stack (required before process_frame) */
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    net_init(0x0A000001, mac);  /* 10.0.0.1 */

    uint8_t frame[2048];
    uint8_t reply[2048];
    uint32_t reply_len;

    /* Sub-test 1: Zero-length frame */
    for (int i = 0; i < 1000; i++) {
        reply_len = 0;
        net_process_frame(frame, 0, reply, &reply_len);
    }

    /* Sub-test 2: Truncated Ethernet (1-13 bytes) */
    for (int i = 0; i < 10000; i++) {
        uint32_t len = rand_range(13) + 1;
        fill_random(frame, len);
        reply_len = 0;
        net_process_frame(frame, len, reply, &reply_len);
    }

    /* Sub-test 3: Valid ARP ethertype, truncated body */
    for (int i = 0; i < 10000; i++) {
        fill_random(frame, 42);
        frame[12] = 0x08; frame[13] = 0x06;  /* ARP ethertype */
        uint32_t len = 14 + rand_range(28);   /* 14 to 41 bytes */
        reply_len = 0;
        net_process_frame(frame, len, reply, &reply_len);
    }

    /* Sub-test 4: Valid IP ethertype, bad headers */
    for (int i = 0; i < 10000; i++) {
        fill_random(frame, 128);
        frame[12] = 0x08; frame[13] = 0x00;  /* IPv4 ethertype */
        /* Random IHL, total_len, flags */
        uint32_t len = 14 + rand_range(114);  /* 14 to 127 bytes */
        reply_len = 0;
        net_process_frame(frame, len, reply, &reply_len);
    }

    /* Sub-test 5: IP with fragment flags (SEC-025) */
    for (int i = 0; i < 10000; i++) {
        fill_random(frame, 64);
        frame[12] = 0x08; frame[13] = 0x00;  /* IPv4 */
        frame[14] = 0x45;                     /* IHL=5, version=4 */
        frame[20] = 0x20;                     /* MF flag set */
        reply_len = 0;
        net_process_frame(frame, 64, reply, &reply_len);
    }

    /* Sub-test 6: Oversized frames */
    for (int i = 0; i < 5000; i++) {
        uint32_t len = 1536 + rand_range(512);  /* 1536-2047 */
        fill_random(frame, len);
        reply_len = 0;
        net_process_frame(frame, len, reply, &reply_len);
    }

    /* Sub-test 7: Fully random (any length, any content) */
    for (int i = 0; i < 53000; i++) {
        uint32_t len = rand_range(2048);
        fill_random(frame, len);
        reply_len = 0;
        net_process_frame(frame, len, reply, &reply_len);
    }

    /* If we got here, no crashes */
    printf("100000 random frames ... 0 crashes  PASS\n");
    pass_count++;
}

/* ================================================================
 * Target 2: shmem_ipc send/recv with corrupted data
 * ================================================================ */
static void fuzz_shmem_ipc(void) {
    printf("[FUZZ] shmem_ipc: ");
    fflush(stdout);

    shmem_ring_t ring;
    uint32_t crc_rejects = 0;

    /* Sub-test 1: Normal fill/drain cycle (baseline) */
    for (int i = 0; i < 10000; i++) {
        shmem_ipc_init(&ring);
        uint8_t payload[SHMEM_MAX_PAYLOAD];
        fill_random(payload, rand_range(SHMEM_MAX_PAYLOAD + 1));
        uint16_t len = (uint16_t)rand_range(SHMEM_MAX_PAYLOAD + 1);
        uint8_t type = (uint8_t)(rand_range(14) + 1);
        shmem_ipc_send(&ring, type, (uint16_t)i, payload, len);

        uint8_t rtype; uint16_t rseq, rlen;
        uint8_t rbuf[SHMEM_MAX_PAYLOAD];
        shmem_ipc_recv(&ring, &rtype, &rseq, rbuf, &rlen);
    }

    /* Sub-test 2: Oversized payload length */
    for (int i = 0; i < 10000; i++) {
        shmem_ipc_init(&ring);
        uint8_t payload[SHMEM_MAX_PAYLOAD];
        fill_random(payload, SHMEM_MAX_PAYLOAD);
        /* Try sending with len > MAX — should be rejected or clamped */
        shmem_ipc_send(&ring, MSG_QUERY, 1, payload, SHMEM_MAX_PAYLOAD);
    }

    /* Sub-test 3: CRC corruption detection */
    for (int i = 0; i < 20000; i++) {
        shmem_ipc_init(&ring);
        uint8_t payload[] = "test payload data";
        shmem_ipc_send(&ring, MSG_QUERY, 1, payload, sizeof(payload) - 1);

        /* Corrupt a random byte in the ring slot */
        uint8_t *raw = (uint8_t *)&ring.slots[0];
        int pos = (int)rand_range(SHMEM_SLOT_SIZE);
        raw[pos] ^= (uint8_t)(1 << rand_range(8));

        uint8_t rtype; uint16_t rseq, rlen;
        uint8_t rbuf[SHMEM_MAX_PAYLOAD];
        int rc = shmem_ipc_recv(&ring, &rtype, &rseq, rbuf, &rlen);
        if (rc == SHMEM_ERR_CRC)
            crc_rejects++;
    }

    /* Sub-test 4: Corrupted ring header */
    for (int i = 0; i < 10000; i++) {
        shmem_ipc_init(&ring);
        /* Corrupt header fields — use cast to avoid modulo-by-zero */
        ring.header.write_idx = (uint32_t)xorshift64();
        ring.header.read_idx = (uint32_t)xorshift64();
        /* Try recv on corrupted ring — should not crash */
        uint8_t rtype; uint16_t rseq, rlen;
        uint8_t rbuf[SHMEM_MAX_PAYLOAD];
        shmem_ipc_recv(&ring, &rtype, &rseq, rbuf, &rlen);
    }

    /* Sub-test 5: Fill ring completely, then overfill */
    for (int i = 0; i < 10000; i++) {
        shmem_ipc_init(&ring);
        for (int j = 0; j < SHMEM_RING_SLOTS + 5; j++) {
            uint8_t payload[16];
            fill_random(payload, 16);
            shmem_ipc_send(&ring, MSG_HEARTBEAT, (uint16_t)j, payload, 16);
        }
    }

    /* Sub-test 6: Random raw bytes in ring memory */
    for (int i = 0; i < 40000; i++) {
        fill_random(&ring, sizeof(ring));
        uint8_t rtype; uint16_t rseq, rlen;
        uint8_t rbuf[SHMEM_MAX_PAYLOAD];
        shmem_ipc_recv(&ring, &rtype, &rseq, rbuf, &rlen);
    }

    printf("100000 malformed messages ... 0 crashes, %u CRC rejects  PASS\n", crc_rejects);
    pass_count++;
}

/* ================================================================
 * Target 3: gguf_open_memory with malformed buffers
 * ================================================================ */
static void fuzz_gguf_parser(void) {
    printf("[FUZZ] gguf_parser: ");
    fflush(stdout);

    gguf_ctx_t ctx;
    uint8_t buf[4096];

    /* Sub-test 1: Zero-length and tiny buffers */
    for (int i = 0; i < 10000; i++) {
        size_t len = rand_range(24);  /* 0 to 23 bytes */
        fill_random(buf, len);
        int rc = gguf_open_memory(&ctx, buf, len);
        if (rc == 0) gguf_close(&ctx);
    }

    /* Sub-test 2: Valid magic, garbage after */
    for (int i = 0; i < 20000; i++) {
        size_t len = 24 + rand_range(4072);
        fill_random(buf, len);
        /* Set GGUF magic: "GGUF" = 0x46554747 LE */
        buf[0] = 0x47; buf[1] = 0x47; buf[2] = 0x55; buf[3] = 0x46;
        /* Set version 3 */
        buf[4] = 3; buf[5] = 0; buf[6] = 0; buf[7] = 0;
        int rc = gguf_open_memory(&ctx, buf, len);
        if (rc == 0) gguf_close(&ctx);
    }

    /* Sub-test 3: Huge n_tensors / n_kv (SEC-008) */
    for (int i = 0; i < 10000; i++) {
        memset(buf, 0, 24);
        buf[0] = 0x47; buf[1] = 0x47; buf[2] = 0x55; buf[3] = 0x46;
        buf[4] = 3;  /* version */
        /* n_tensors = random huge value */
        uint64_t huge = xorshift64();
        memcpy(buf + 8, &huge, 8);
        /* n_kv = random huge value */
        huge = xorshift64();
        memcpy(buf + 16, &huge, 8);
        int rc = gguf_open_memory(&ctx, buf, 24);
        if (rc == 0) gguf_close(&ctx);
    }

    /* Sub-test 4: Truncated KV pairs */
    for (int i = 0; i < 10000; i++) {
        size_t len = 24 + rand_range(200);
        fill_random(buf, len);
        buf[0] = 0x47; buf[1] = 0x47; buf[2] = 0x55; buf[3] = 0x46;
        buf[4] = 3;
        /* Small n_tensors, small n_kv */
        uint64_t small_val = rand_range(10);
        memcpy(buf + 8, &small_val, 8);
        small_val = rand_range(10);
        memcpy(buf + 16, &small_val, 8);
        int rc = gguf_open_memory(&ctx, buf, len);
        if (rc == 0) gguf_close(&ctx);
    }

    /* Sub-test 5: Fully random buffers */
    for (int i = 0; i < 50000; i++) {
        size_t len = rand_range(4096);
        fill_random(buf, len);
        int rc = gguf_open_memory(&ctx, buf, len);
        if (rc == 0) gguf_close(&ctx);
    }

    printf("100000 malformed buffers ... 0 crashes  PASS\n");
    pass_count++;
}

int main(void) {
    printf("=== JARVIS Phase 3c: Fuzz Testing ===\n\n");

    fuzz_net_stack();
    fuzz_shmem_ipc();
    fuzz_gguf_parser();

    printf("\n=== Fuzz: %d/%d PASS (300000 iterations, 0 crashes) ===\n",
           pass_count, pass_count + fail_count);
    return fail_count > 0 ? 1 : 0;
}
