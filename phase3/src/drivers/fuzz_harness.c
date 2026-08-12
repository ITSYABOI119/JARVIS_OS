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
/* EXACT-SIZE HEAP DELIVERY — the whole reason this harness can see anything.
 *
 * The net_stack sub-tests used to declare `uint8_t frame[2048]` / `reply[2048]`
 * as STACK arrays and hand the same oversized buffers to every call. Sub-test 6
 * deliberately generates len = 1536..2047, i.e. exactly the oversized frames
 * that make handle_icmp's ICMP checksum read past NET_MAX_FRAME — and then let
 * it read into 512 bytes of slack the harness itself owned. In-bounds for ASan,
 * out-of-contract for the function: ~900K sanitized iterations could never have
 * reported it. This is the SECOND instance of that class in this repo; the
 * first was fuzz_control_in.c, hardened to exact-len heap buffers at 6-5/M1,
 * which immediately caught a 1-byte parser OOB.
 *
 * So: the frame is an exact-`len` heap allocation (a read past the declared
 * length is redzoned) and the reply is exactly the documented contract size,
 * min(len, NET_MAX_FRAME) (a write OR read past what a conforming caller
 * provides is redzoned). Iteration counts are unchanged — this buys visibility,
 * not coverage. */
static void nfs_call(const uint8_t *src, uint32_t len) {
    uint32_t rcap = len < NET_MAX_FRAME ? len : NET_MAX_FRAME;
    uint8_t *frame = (uint8_t *)malloc(len ? len : 1);
    uint8_t *reply = (uint8_t *)malloc(rcap ? rcap : 1);
    uint32_t reply_len = 0;
    if (!frame || !reply) { free(frame); free(reply); return; }
    if (len) memcpy(frame, src, len);
    net_process_frame(frame, len, reply, &reply_len);
    free(frame);
    free(reply);
}

static void fuzz_net_stack(void) {
    printf("[FUZZ] net_stack: ");
    fflush(stdout);

    /* Initialize net stack (required before process_frame) */
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    net_init(0x0A000001, mac);  /* 10.0.0.1 */

    /* Scratch only — every call is delivered through nfs_call's exact-size
     * heap buffers, never from these. */
    uint8_t frame[2048];

    /* Sub-test 1: Zero-length frame */
    for (int i = 0; i < 1000; i++) {
        nfs_call(frame, 0);
    }

    /* Sub-test 2: Truncated Ethernet (1-13 bytes) */
    for (int i = 0; i < 10000; i++) {
        uint32_t len = rand_range(13) + 1;
        fill_random(frame, len);
        nfs_call(frame, len);
    }

    /* Sub-test 3: Valid ARP ethertype, truncated body.
     *
     * AIM FIX 2026-08-11: this generated 14 + rand_range(28) = 14..41 bytes —
     * ONE BYTE short of the ETH_HLEN + sizeof(net_arp_t) = 42 that handle_arp
     * requires, so its length guard rejected EVERY frame and the ARP body was
     * never entered in 10,000 iterations. rand_range(29) gives 14..42, so
     * roughly 1 in 29 now reaches the htype/ptype/hlen/plen/oper checks while
     * every previously-generated length is still produced. Iteration count
     * unchanged; this is aim, not volume. */
    for (int i = 0; i < 10000; i++) {
        fill_random(frame, 42);
        frame[12] = 0x08; frame[13] = 0x06;  /* ARP ethertype */
        uint32_t len = 14 + rand_range(29);   /* 14 to 42 bytes (42 = a full ARP frame) */
        nfs_call(frame, len);
    }

    /* Sub-test 4: Valid IP ethertype, bad headers */
    for (int i = 0; i < 10000; i++) {
        fill_random(frame, 128);
        frame[12] = 0x08; frame[13] = 0x00;  /* IPv4 ethertype */
        /* Random IHL, total_len, flags */
        uint32_t len = 14 + rand_range(114);  /* 14 to 127 bytes */
        nfs_call(frame, len);
    }

    /* Sub-test 5: IP with fragment flags (SEC-025) */
    for (int i = 0; i < 10000; i++) {
        fill_random(frame, 64);
        frame[12] = 0x08; frame[13] = 0x00;  /* IPv4 */
        frame[14] = 0x45;                     /* IHL=5, version=4 */
        frame[20] = 0x20;                     /* MF flag set */
        nfs_call(frame, 64);
    }

    /* Sub-test 6: Oversized frames */
    for (int i = 0; i < 5000; i++) {
        uint32_t len = 1536 + rand_range(512);  /* 1536-2047 */
        fill_random(frame, len);
        nfs_call(frame, len);
    }

    /* Sub-test 7: Fully random (any length, any content) */
    for (int i = 0; i < 53000; i++) {
        uint32_t len = rand_range(2048);
        fill_random(frame, len);
        nfs_call(frame, len);
    }

    /* Sub-test 8 (DIRECTED — runs LAST and consumes NO RNG, so sub-tests 1-7
     * and the later targets draw the identical stream they drew before it
     * existed): AN OVERSIZED, WELL-FORMED ICMP ECHO REQUEST.
     *
     * WHY A DIRECTED CASE IS REQUIRED HERE, and it is the finding rather than a
     * convenience: tightening the buffers above made the ICMP checksum overrun
     * VISIBLE, but nothing in the random corpus can WALK INTO it. Reaching
     * handle_icmp's reply builder demands, simultaneously, a valid IP header
     * checksum (~2^-16), dst_ip == our 10.0.0.1 (~2^-32), protocol == ICMP
     * (~2^-8), no fragment bits, and ICMP type 8 / code 0 (~2^-16) — on a frame
     * over 1536 bytes. The corpus has never once satisfied that conjunction,
     * which is exactly what the coverage instrument independently measured (the
     * rate limiter and both clamps at :261/:272/:273 are reached only by
     * unit-suite frames). Visibility and reachability are two different
     * problems and this defect needed both solved.
     *
     * The frame: len 1600, ip_total 1586 (<= len - ETH_HLEN, so the :230 guard
     * passes). total_len clamps to NET_MAX_FRAME = 1536, so the memcpy fits the
     * contract-sized reply exactly — while icmp_len = ip_total - ip_hdr_len =
     * 1566 makes the checksum read reply[34 .. 1600), i.e. 64 bytes past. */
    {
        const uint32_t dlen = 1600, ip_total = 1586;
        uint8_t *d = (uint8_t *)malloc(dlen);
        if (d) {
            memset(d, 0, dlen);
            memset(d + 0, 0x02, 6);            /* dst MAC (unchecked by handle_ip) */
            memset(d + 6, 0x03, 6);            /* src MAC */
            d[12] = 0x08; d[13] = 0x00;        /* IPv4 */
            d[14] = 0x45;                      /* version 4, IHL 5 -> 20 B header */
            d[16] = (uint8_t)(ip_total >> 8);  /* total_len, big endian */
            d[17] = (uint8_t)(ip_total & 0xFF);
            d[20] = 0x00; d[21] = 0x00;        /* no MF, no fragment offset (SEC-025) */
            d[22] = 64;                        /* ttl */
            d[23] = IP_PROTO_ICMP;
            d[26] = 0x7F; d[27] = 0; d[28] = 0; d[29] = 2;  /* some src */
            /* dst MUST equal our_ip. Take it from net_get_ip() and memcpy the
             * raw bytes: handle_ip compares a memcpy'd uint32 against the
             * stored one, so writing "10.0.0.1" by hand only matches on a
             * big-endian host (net_init here is handed the host constant
             * 0x0A000001). Copying the getter's bytes is endianness-proof and
             * is what the comparison itself does. */
            uint32_t oip = net_get_ip();
            memcpy(d + 30, &oip, 4);
            d[24] = 0; d[25] = 0;              /* checksum field zeroed before compute */
            /* Store the sum big-endian by hand rather than via htons: the mock
             * build has no <arpa/inet.h>, and the two explicit bytes are the
             * same on either host endianness (handle_ip compares
             * htons(net_checksum(...)) against the raw field). */
            uint16_t ck = net_checksum(d + 14, 20);
            d[24] = (uint8_t)(ck >> 8);
            d[25] = (uint8_t)(ck & 0xFF);
            d[34] = ICMP_ECHO_REQUEST; d[35] = 0;          /* type 8, code 0 */
            nfs_call(d, dlen);
            free(d);
        }
    }

    /* If we got here, no crashes */
    /* 99,000, not the 100,000 this printed until 2026-08-12: the sub-test
     * bounds above sum to 1000+10000+10000+10000+10000+5000+53000 = 99,000.
     * The LABEL was wrong, not the loops — a bound change would change the
     * fuzzing, so only the number printed here moved. */
    printf("99000 random frames + 1 directed oversized ICMP echo ... 0 crashes  PASS\n");
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

    /* Sub-test 7 (DIRECTED — runs LAST and consumes NO RNG, so
     * fuzz_gguf_parser draws the identical stream it drew before this
     * existed): the 148551a ring-size guards.
     *
     * The random sub-tests cannot reach these outcomes: send is only ever
     * called after shmem_ipc_init (header.size == 15, both guard outcomes
     * impossible), and sub-test 6's fully-random header makes size == 0 a
     * 2^-32 event on recv. The coverage instrument measured exactly that —
     * send's guard (shmem_ipc.c:69/:70) never taken, recv's size==0
     * outcome (:109) never taken. Directed corrupted sizes close them.
     *
     * Contract asserted: a corrupted size is rejected (-1) by BOTH send and
     * recv, and a normal round-trip still works afterwards — the positive
     * control proving the rejects corrupted nothing. */
    {
        static const uint32_t bad_sizes[] = { 0, SHMEM_RING_SLOTS + 1, 255 };
        uint8_t dpayload[16] = "ring-size-guard";
        int directed_ok = 1;
        shmem_ipc_init(&ring);
        for (size_t k = 0; k < sizeof(bad_sizes) / sizeof(bad_sizes[0]); k++) {
            ring.header.size = bad_sizes[k];
            if (shmem_ipc_send(&ring, MSG_QUERY, 77, dpayload, 16) != -1)
                directed_ok = 0;
            uint8_t rtype; uint16_t rseq, rlen;
            uint8_t rbuf[SHMEM_MAX_PAYLOAD];
            if (shmem_ipc_recv(&ring, &rtype, &rseq, rbuf, &rlen) != -1)
                directed_ok = 0;
        }
        /* Positive control: restore the size; the rejected calls must have
         * left the ring intact (indices unmoved, slots untouched). */
        ring.header.size = SHMEM_RING_SLOTS;
        uint8_t rtype = 0; uint16_t rseq = 0, rlen = 0;
        uint8_t rbuf[SHMEM_MAX_PAYLOAD];
        if (shmem_ipc_send(&ring, MSG_QUERY, 78, dpayload, 16) != 0)
            directed_ok = 0;
        if (shmem_ipc_recv(&ring, &rtype, &rseq, rbuf, &rlen) != 0 ||
            rtype != MSG_QUERY || rseq != 78 || rlen != 16 ||
            memcmp(rbuf, dpayload, 16) != 0)
            directed_ok = 0;
        if (!directed_ok) {
            printf("directed ring-size guard cases FAILED\n");
            fail_count++;
            return;
        }
    }

    printf("100000 malformed messages + 7 directed guard cases "
           "(3 sizes x send+recv + round-trip) ... 0 crashes, %u CRC rejects  PASS\n",
           crc_rejects);
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

    /* Sub-test 4: Truncated KV pairs.
     *
     * AIM FIX 2026-08-11 — THE ONE THAT HID ~99,560 ITERATIONS OF NOTHING.
     * This sub-test is the only one shaped to reach KV parsing (small counts
     * that clear the SEC-008 caps), but it wrote just `buf[4] = 3` and left
     * bytes 5-7 as fill_random's garbage. The version is read as a LITTLE-
     * ENDIAN u32, so the value was 3 | (random << 8) and parse_gguf's
     * `version < 2 || version > 3` rejected it with probability ~1 - 2^-24.
     * Measured consequence: across ~99,560 fuzz opens gguf_open_memory
     * returned success ZERO times, and the entire parser body — read_kv_pair,
     * skip_gguf_value, read_gguf_string's over-long handler, read_tensor_info
     * including its never-fired n_dims bounds guard, both type switches — was
     * covered by unit fixtures alone.
     *
     * The minimal header is read from parse_gguf, not guessed: magic (4) +
     * version u32 LE in [2,3] + n_tensors u64 <= GGUF_MAX_TENSORS + n_kv u64
     * <= GGUF_MAX_KV_PAIRS. The counts below were already small enough; only
     * the version word was wrong. Iteration count unchanged. */
    for (int i = 0; i < 10000; i++) {
        size_t len = 24 + rand_range(200);
        fill_random(buf, len);
        buf[0] = 0x47; buf[1] = 0x47; buf[2] = 0x55; buf[3] = 0x46;
        buf[4] = 3; buf[5] = 0; buf[6] = 0; buf[7] = 0;   /* the FULL LE version word */
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

    /* 299,008 = 299,000 random + 8 directed. The random figure is
     * 99,000 (net_stack) + 100,000 (shmem_ipc) + 100,000 (gguf_parser),
     * re-derived from the loop bounds 2026-08-12; the long-printed "300,000"
     * over-counted net_stack by 1,000. Label only — no bound was touched. */
    printf("\n=== Fuzz: %d/%d PASS (299008 iterations: 299000 random + 8 directed "
           "[7 ring-guard, 1 oversized ICMP], 0 crashes) ===\n",
           pass_count, pass_count + fail_count);
    return fail_count > 0 ? 1 : 0;
}
