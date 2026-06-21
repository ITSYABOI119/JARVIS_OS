/**
 * test_net_udp.c - Unit tests for the Ethernet/IPv4/UDP broadcast frame builder
 *
 * Pure host tests (no hardware): the IP checksum against a known RFC1071 vector,
 * and a full frame build decoded field-by-field (big-endian), incl. verifying the
 * IP header checksum sums to 0 (the "valid checksum" property Wireshark checks).
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/drivers \
 *       phase3/src/drivers/test_net_udp.c phase3/src/drivers/net_udp.c \
 *       -o /tmp/test_net_udp && /tmp/test_net_udp
 *
 * JARVIS AI-OS - Phase 4 (goal #2b N-b)
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "net_udp.h"

static int pass = 0, fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { pass++; printf("  PASS: %s\n", msg); } \
    else      { fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* Read a big-endian 16-bit value from a byte buffer. */
static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

/* ---- Test 1: net_ip_checksum against the canonical RFC791/Wikipedia vector ----
 * IPv4 header 45 00 00 73 00 00 40 00 40 11 00 00 c0 a8 00 01 c0 a8 00 c7
 * (checksum field zeroed) has the well-known checksum 0xb861. */
static void test_ip_checksum_known_vector(void)
{
    uint8_t hdr[20] = {
        0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00,
        0x40, 0x11, 0x00, 0x00, 0xc0, 0xa8, 0x00, 0x01,
        0xc0, 0xa8, 0x00, 0xc7
    };

    uint16_t ck = net_ip_checksum(hdr, sizeof hdr);
    CHECK(ck == 0xb861, "IP checksum known vector == 0xb861");

    /* Store it and re-run: a valid header checksums to 0. */
    hdr[10] = (uint8_t)(ck >> 8);
    hdr[11] = (uint8_t)(ck & 0xFF);
    CHECK(net_ip_checksum(hdr, sizeof hdr) == 0,
          "IP checksum verify (completed header sums to 0)");
}

/* ---- Test 2: full UDP broadcast frame, decoded field-by-field ---- */
static void test_build_udp_broadcast(void)
{
    uint8_t out[256];
    const uint8_t mac[6] = { 0x0c, 0x9d, 0x92, 0x0e, 0x39, 0x9a };
    const char *payload = "JARVIS-TELEMETRY-HELLO";
    uint16_t plen = (uint16_t)strlen(payload);

    memset(out, 0xCC, sizeof out);
    int flen = net_build_udp_broadcast(out, sizeof out, mac,
                                       JARVIS_BOX_IP, JARVIS_TELEMETRY_PORT,
                                       JARVIS_TELEMETRY_PORT, payload, plen);

    int expect_total = 14 + 20 + 8 + (int)plen;            /* 42 + 22 = 64 */
    CHECK(flen == (expect_total >= 60 ? expect_total : 60), "frame length correct (>=60)");
    CHECK(flen >= 60, "frame length >= 60 (min Ethernet)");

    /* --- Ethernet --- */
    uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    CHECK(memcmp(out, bcast, 6) == 0, "Eth dst == FF:FF:FF:FF:FF:FF");
    CHECK(memcmp(out + 6, mac, 6) == 0, "Eth src == MAC");
    CHECK(be16(out + 12) == 0x0800, "Eth ethertype == 0x0800 (IPv4)");

    /* --- IPv4 --- */
    const uint8_t *ip = out + 14;
    CHECK(ip[0] == 0x45, "IP byte0 == 0x45 (v4, ihl5)");
    CHECK(ip[9] == 17, "IP proto == 17 (UDP)");
    CHECK(be16(ip + 2) == (uint16_t)(20 + 8 + plen), "IP total_len == 20+8+payload");
    CHECK(ip[8] == 64, "IP ttl == 64");
    /* src ip 192.168.100.143 (big-endian on the wire) */
    CHECK(ip[12] == 192 && ip[13] == 168 && ip[14] == 100 && ip[15] == 143,
          "IP src == 192.168.100.143");
    CHECK(ip[16] == 0xFF && ip[17] == 0xFF && ip[18] == 0xFF && ip[19] == 0xFF,
          "IP dst == 255.255.255.255");
    /* The completed 20-byte IP header must checksum to 0 (Wireshark "valid"). */
    CHECK(net_ip_checksum(ip, 20) == 0, "IP header checksum valid (sums to 0)");
    CHECK(!(ip[10] == 0 && ip[11] == 0), "IP checksum field is non-zero (was computed)");

    /* --- UDP --- */
    const uint8_t *udp = ip + 20;
    CHECK(be16(udp + 0) == JARVIS_TELEMETRY_PORT, "UDP src port");
    CHECK(be16(udp + 2) == JARVIS_TELEMETRY_PORT, "UDP dst port");
    CHECK(be16(udp + 4) == (uint16_t)(8 + plen), "UDP length == 8+payload");
    CHECK(be16(udp + 6) == 0x0000, "UDP checksum == 0 (disabled)");

    /* --- payload --- */
    CHECK(memcmp(udp + 8, payload, plen) == 0, "payload bytes copied");
}

/* ---- Test 3: guards (bad args / capacity / oversize payload) ---- */
static void test_guards(void)
{
    uint8_t out[256];
    const uint8_t mac[6] = { 1, 2, 3, 4, 5, 6 };

    CHECK(net_build_udp_broadcast(NULL, sizeof out, mac, JARVIS_BOX_IP,
                                  1, 1, "x", 1) == -1, "NULL out rejected");
    CHECK(net_build_udp_broadcast(out, 40, mac, JARVIS_BOX_IP,
                                  1, 1, "x", 1) == -1, "too-small out_cap rejected (<60)");
    CHECK(net_build_udp_broadcast(out, sizeof out, mac, JARVIS_BOX_IP,
                                  1, 1, NULL, 1473) == -1, "oversize payload rejected");
    /* zero-length payload is legal -> 42 bytes of headers, padded to 60 */
    int flen = net_build_udp_broadcast(out, sizeof out, mac, JARVIS_BOX_IP,
                                       1, 1, NULL, 0);
    CHECK(flen == 60, "zero payload -> padded to 60");
}

int main(void)
{
    printf("=== JARVIS AI-OS: net_udp (Eth/IPv4/UDP builder) Tests ===\n\n");
    test_ip_checksum_known_vector();
    test_build_udp_broadcast();
    test_guards();
    printf("\n=== Results: %d PASS, %d FAIL ===\n", pass, fail);
    return fail ? 1 : 0;
}
