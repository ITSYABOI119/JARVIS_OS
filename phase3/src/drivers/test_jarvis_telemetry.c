/**
 * test_jarvis_telemetry.c - Host tests for the JARVIS telemetry packet
 *
 * Verifies the on-wire layout (sizeof + field offsets), the zlib-compatible
 * CRC-32 (canonical 0xCBF43926 check value -> proves Python zlib.crc32 compat),
 * and the finalize round-trip (CRC matches + any byte flip breaks it).
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/drivers \
 *       phase3/src/drivers/test_jarvis_telemetry.c phase3/src/drivers/jarvis_telemetry.c \
 *       -o /tmp/test_jarvis_telemetry && /tmp/test_jarvis_telemetry
 *
 * JARVIS AI-OS - Phase 4 (goal #2b N-c)
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "jarvis_telemetry.h"

static int pass = 0, fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { pass++; printf("  PASS: %s\n", msg); } \
    else      { fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

#define OFF(field, want) \
    CHECK(offsetof(telemetry_packet_t, field) == (want), \
          "offsetof(" #field ") == " #want)

static void test_layout(void)
{
    CHECK(sizeof(telemetry_packet_t) == 200, "sizeof(telemetry_packet_t) == 200");
    OFF(magic, 0);
    OFF(flags, 6);
    OFF(boot_id, 8);
    OFF(seq, 12);
    OFF(uptime_ms, 16);
    OFF(q_total, 24);
    OFF(q_errors, 64);
    OFF(num_nodes, 72);
    OFF(model_size_mb, 80);
    OFF(last_text, 92);
    OFF(model_name, 148);
    OFF(reserved2, 188);
    OFF(crc32, 196);
}

static void test_crc_known_vector(void)
{
    /* Canonical zlib/IEEE CRC-32 check value: crc32("123456789") == 0xCBF43926.
     * Matching this proves wire-compat with Python zlib.crc32. */
    uint32_t crc = jarvis_tlm_crc32("123456789", 9);
    CHECK(crc == 0xCBF43926u, "crc32(\"123456789\") == 0xCBF43926 (zlib check value)");

    /* Empty input -> 0 (zlib convention). */
    CHECK(jarvis_tlm_crc32("", 0) == 0u, "crc32(\"\", 0) == 0");
}

static void test_finalize_roundtrip(void)
{
    telemetry_packet_t pkt;
    memset(&pkt, 0, sizeof pkt);
    pkt.kind = TLM_K_STATS;
    pkt.seq = 7;
    pkt.q_total = 12345;
    pkt.num_nodes = 6;
    pkt.model_size_mb = 2962;
    memcpy(pkt.model_name, "Gemma 4 E2B", 11);
    memcpy(pkt.last_text, "hello world", 11);

    jarvis_tlm_finalize(&pkt);

    CHECK(pkt.magic == JARVIS_TLM_MAGIC, "finalize sets magic == JTEL");
    CHECK(pkt.version == JARVIS_TLM_VERSION, "finalize sets version == 1");

    /* The stored crc matches a recompute over the first 196 bytes. */
    uint32_t recomputed = jarvis_tlm_crc32(&pkt, offsetof(telemetry_packet_t, crc32));
    CHECK(pkt.crc32 == recomputed, "stored crc32 == recompute over first 196 B");
    CHECK(pkt.crc32 != 0u, "crc32 is non-zero for a populated packet");

    /* The magic bytes are "JTEL" little-endian: 4C 45 54 4A. */
    uint8_t *raw = (uint8_t *)&pkt;
    CHECK(raw[0] == 0x4C && raw[1] == 0x45 && raw[2] == 0x54 && raw[3] == 0x4A,
          "magic on wire (LE) == 4C 45 54 4A (\"JTEL\")");

    /* Flipping any byte in [0,196) must break the CRC check. */
    int detected_all = 1;
    for (uint32_t i = 0; i < offsetof(telemetry_packet_t, crc32); i++) {
        raw[i] ^= 0xFF;
        if (jarvis_tlm_crc32(&pkt, offsetof(telemetry_packet_t, crc32)) == pkt.crc32)
            detected_all = 0;   /* a flip went undetected */
        raw[i] ^= 0xFF;         /* restore */
    }
    CHECK(detected_all, "every single-byte flip in [0,196) breaks the CRC");
}

int main(void)
{
    printf("=== JARVIS AI-OS: telemetry packet Tests ===\n\n");
    test_layout();
    test_crc_known_vector();
    test_finalize_roundtrip();
    printf("\n=== Results: %d PASS, %d FAIL ===\n", pass, fail);
    return fail ? 1 : 0;
}
