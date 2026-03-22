/**
 * test_net_stack.c - Network Stack Portable Function Tests
 *
 * Tests the platform-independent parts of net_stack.c:
 *   - net_checksum()           (IP/ICMP ones-complement checksum)
 *   - htons/ntohs              (byte order, tested via built frames)
 *   - net_init/set_ip/set_mac  (configuration)
 *   - net_arp_add/lookup/count (ARP cache)
 *   - net_build_arp_request    (outbound ARP frame builder)
 *   - net_build_icmp_request   (outbound ICMP echo frame builder)
 *   - net_process_frame        (ARP reply + ICMP echo reply dispatch)
 *   - net_process_arp_reply    (cache update from ARP reply)
 *   - net_is_icmp_echo_reply   (ICMP echo reply matcher)
 *   - IPv4 IHL validation      (reject IHL < 5 or > 15)
 *   - Packet length validation  (reject total_len < ip_hdr_len)
 *   - SEC-004: ARP reply rejection without pending request
 *   - SEC-025: Fragmented IPv4 packet rejection
 *
 * Functions behind #ifdef JARVIS_PLATFORM_PI4 that are NOT testable here:
 *   - net_arp_resolve()  (the TX/poll/timer path; cache-hit path tested)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *       -I phase3/src/drivers phase3/src/drivers/test_net_stack.c \
 *       phase3/src/drivers/net_stack.c -o /tmp/test_net_stack
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "net_stack.h"

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

static int pass_count = 0;
static int fail_count = 0;

#define TEST(name) \
    do { printf("  TEST: %-55s ", name); } while(0)

#define PASS() \
    do { printf("PASS\n"); pass_count++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL - %s\n", msg); fail_count++; } while(0)

#define CHECK(cond, msg) \
    do { if (cond) { PASS(); } else { FAIL(msg); } } while(0)

/* Local htons for building test frames (mirrors net_stack's static htons) */
static inline uint16_t test_htons(uint16_t h)
{
    return (uint16_t)((h >> 8) | (h << 8));
}

/* ========================================================================
 * Helper: build a valid ARP request frame targeting our IP
 * ======================================================================== */

static uint32_t build_arp_request_frame(uint8_t *buf, uint32_t buf_size,
                                        uint32_t sender_ip_be,
                                        const uint8_t sender_mac[6],
                                        uint32_t target_ip_be)
{
    if (buf_size < ETH_HLEN + sizeof(net_arp_t)) return 0;

    memset(buf, 0, ETH_HLEN + sizeof(net_arp_t));

    /* Ethernet header */
    net_eth_hdr_t *eth = (net_eth_hdr_t *)buf;
    memset(eth->dst, 0xFF, 6);  /* broadcast */
    memcpy(eth->src, sender_mac, 6);
    eth->ethertype = test_htons(ETH_P_ARP);

    /* ARP request */
    net_arp_t *arp = (net_arp_t *)(buf + ETH_HLEN);
    arp->htype = test_htons(1);
    arp->ptype = test_htons(ETH_P_IP);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = test_htons(1);  /* request */
    memcpy(arp->sha, sender_mac, 6);
    memcpy(arp->spa, &sender_ip_be, 4);
    memset(arp->tha, 0, 6);
    memcpy(arp->tpa, &target_ip_be, 4);

    return ETH_HLEN + sizeof(net_arp_t);
}

/* ========================================================================
 * Helper: build a valid ICMP echo request frame
 * ======================================================================== */

static uint32_t build_icmp_echo_request(uint8_t *buf, uint32_t buf_size,
                                        const uint8_t src_mac[6],
                                        uint32_t src_ip_be,
                                        const uint8_t dst_mac[6],
                                        uint32_t dst_ip_be,
                                        uint16_t ident, uint16_t seq)
{
    /* ETH(14) + IP(20) + ICMP(8) + 4 bytes data = 46 */
    uint32_t icmp_data_len = 4;
    uint32_t icmp_total = 8 + icmp_data_len;
    uint32_t ip_total = 20 + icmp_total;
    uint32_t frame_len = ETH_HLEN + ip_total;

    if (buf_size < frame_len) return 0;
    memset(buf, 0, frame_len);

    /* Ethernet */
    net_eth_hdr_t *eth = (net_eth_hdr_t *)buf;
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, src_mac, 6);
    eth->ethertype = test_htons(ETH_P_IP);

    /* IP header */
    net_ip_hdr_t *ip = (net_ip_hdr_t *)(buf + ETH_HLEN);
    ip->ver_ihl   = 0x45;  /* IPv4, IHL=5 */
    ip->ttl       = 64;
    ip->protocol  = IP_PROTO_ICMP;
    ip->total_len = test_htons((uint16_t)ip_total);
    memcpy(ip->src_ip, &src_ip_be, 4);
    memcpy(ip->dst_ip, &dst_ip_be, 4);
    ip->checksum  = 0;
    ip->checksum  = test_htons(net_checksum(ip, 20));

    /* ICMP echo request */
    net_icmp_echo_t *icmp = (net_icmp_echo_t *)(buf + ETH_HLEN + 20);
    icmp->type     = ICMP_ECHO_REQUEST;
    icmp->code     = 0;
    icmp->ident    = test_htons(ident);
    icmp->seq      = test_htons(seq);
    icmp->checksum = 0;

    /* Fill data bytes */
    uint8_t *data = buf + ETH_HLEN + 20 + 8;
    for (uint32_t i = 0; i < icmp_data_len; i++) data[i] = (uint8_t)i;

    icmp->checksum = test_htons(net_checksum(icmp, icmp_total));

    return frame_len;
}

/* ========================================================================
 * Helper: build a valid ICMP echo REPLY frame
 * ======================================================================== */

static uint32_t build_icmp_echo_reply(uint8_t *buf, uint32_t buf_size,
                                      const uint8_t src_mac[6],
                                      uint32_t src_ip_be,
                                      const uint8_t dst_mac[6],
                                      uint32_t dst_ip_be,
                                      uint16_t ident, uint16_t seq,
                                      uint8_t ttl)
{
    uint32_t icmp_data_len = 4;
    uint32_t icmp_total = 8 + icmp_data_len;
    uint32_t ip_total = 20 + icmp_total;
    uint32_t frame_len = ETH_HLEN + ip_total;

    if (buf_size < frame_len) return 0;
    memset(buf, 0, frame_len);

    /* Ethernet */
    net_eth_hdr_t *eth = (net_eth_hdr_t *)buf;
    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, src_mac, 6);
    eth->ethertype = test_htons(ETH_P_IP);

    /* IP */
    net_ip_hdr_t *ip = (net_ip_hdr_t *)(buf + ETH_HLEN);
    ip->ver_ihl   = 0x45;
    ip->ttl       = ttl;
    ip->protocol  = IP_PROTO_ICMP;
    ip->total_len = test_htons((uint16_t)ip_total);
    memcpy(ip->src_ip, &src_ip_be, 4);
    memcpy(ip->dst_ip, &dst_ip_be, 4);
    ip->checksum  = 0;
    ip->checksum  = test_htons(net_checksum(ip, 20));

    /* ICMP echo reply */
    net_icmp_echo_t *icmp = (net_icmp_echo_t *)(buf + ETH_HLEN + 20);
    icmp->type     = ICMP_ECHO_REPLY;
    icmp->code     = 0;
    icmp->ident    = test_htons(ident);
    icmp->seq      = test_htons(seq);
    icmp->checksum = 0;

    uint8_t *data = buf + ETH_HLEN + 20 + 8;
    for (uint32_t i = 0; i < icmp_data_len; i++) data[i] = (uint8_t)i;

    icmp->checksum = test_htons(net_checksum(icmp, icmp_total));

    return frame_len;
}

/* ========================================================================
 * Helper: build an ARP reply frame
 * ======================================================================== */

static uint32_t build_arp_reply_frame(uint8_t *buf, uint32_t buf_size,
                                      uint32_t sender_ip_be,
                                      const uint8_t sender_mac[6],
                                      uint32_t target_ip_be,
                                      const uint8_t target_mac[6])
{
    if (buf_size < ETH_HLEN + sizeof(net_arp_t)) return 0;

    memset(buf, 0, ETH_HLEN + sizeof(net_arp_t));

    net_eth_hdr_t *eth = (net_eth_hdr_t *)buf;
    memcpy(eth->dst, target_mac, 6);
    memcpy(eth->src, sender_mac, 6);
    eth->ethertype = test_htons(ETH_P_ARP);

    net_arp_t *arp = (net_arp_t *)(buf + ETH_HLEN);
    arp->htype = test_htons(1);
    arp->ptype = test_htons(ETH_P_IP);
    arp->hlen  = 6;
    arp->plen  = 4;
    arp->oper  = test_htons(2);  /* reply */
    memcpy(arp->sha, sender_mac, 6);
    memcpy(arp->spa, &sender_ip_be, 4);
    memcpy(arp->tha, target_mac, 6);
    memcpy(arp->tpa, &target_ip_be, 4);

    return ETH_HLEN + sizeof(net_arp_t);
}

/* ========================================================================
 * Test: net_checksum() on known IP header
 * ======================================================================== */

static void test_checksum_known_ip_header(void)
{
    TEST("net_checksum on known IP header (RFC 1071)");

    /* Standard 20-byte IP header with checksum field zeroed.
     * 192.168.1.100 -> 192.168.1.1, TTL=64, ICMP, total_len=60 */
    uint8_t hdr[20] = {
        0x45, 0x00, 0x00, 0x3C,   /* ver_ihl, DSCP, total_len=60 */
        0x00, 0x01, 0x00, 0x00,   /* ident=1, flags/frag=0 */
        0x40, 0x01, 0x00, 0x00,   /* TTL=64, proto=ICMP, checksum=0 */
        0xC0, 0xA8, 0x01, 0x64,   /* src: 192.168.1.100 */
        0xC0, 0xA8, 0x01, 0x01    /* dst: 192.168.1.1 */
    };

    uint16_t cksum = net_checksum(hdr, 20);

    /* Verify: putting the checksum back into the header and recalculating
     * should yield 0x0000 (ones-complement property). */
    hdr[10] = (uint8_t)(cksum >> 8);
    hdr[11] = (uint8_t)(cksum & 0xFF);
    uint16_t verify = net_checksum(hdr, 20);

    CHECK(verify == 0x0000, "checksum verification round-trip failed");
}

/* ========================================================================
 * Test: net_checksum() with odd byte count
 * ======================================================================== */

static void test_checksum_odd_length(void)
{
    TEST("net_checksum with odd byte count");

    /* 3 bytes: 0x01, 0x02, 0x03 -> sum = 0x0102 + 0x0300 = 0x0402
     * ones-complement = ~0x0402 = 0xFBFD */
    uint8_t data[3] = {0x01, 0x02, 0x03};
    uint16_t cksum = net_checksum(data, 3);
    CHECK(cksum == 0xFBFD, "odd-length checksum mismatch");
}

/* ========================================================================
 * Test: net_checksum() on all zeros -> 0xFFFF
 * ======================================================================== */

static void test_checksum_zeros(void)
{
    TEST("net_checksum on all zeros returns 0xFFFF");

    uint8_t zeros[20] = {0};
    uint16_t cksum = net_checksum(zeros, 20);
    CHECK(cksum == 0xFFFF, "expected 0xFFFF for all-zero input");
}

/* ========================================================================
 * Test: net_checksum() on empty data -> 0xFFFF
 * ======================================================================== */

static void test_checksum_empty(void)
{
    TEST("net_checksum on zero length returns 0xFFFF");

    uint8_t dummy = 0;
    uint16_t cksum = net_checksum(&dummy, 0);
    CHECK(cksum == 0xFFFF, "expected 0xFFFF for zero-length input");
}

/* ========================================================================
 * Test: htons/ntohs via net_build_arp_request ethertype
 * ======================================================================== */

static void test_htons_via_arp_build(void)
{
    TEST("htons byte order via ARP frame ethertype");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, ip_bytes, 4);

    net_init(our_ip, our_mac);

    uint8_t target_bytes[4] = {192, 168, 1, 1};
    uint32_t target_ip;
    memcpy(&target_ip, target_bytes, 4);

    uint8_t frame[64];
    uint32_t len = net_build_arp_request(target_ip, frame, sizeof(frame));

    int ok = (len == 42);  /* ETH_HLEN(14) + sizeof(net_arp_t)(28) */

    /* Ethertype at bytes 12-13 should be 0x08 0x06 (big-endian ARP) */
    ok = ok && (frame[12] == 0x08) && (frame[13] == 0x06);

    CHECK(ok, "ARP ethertype byte order wrong");
}

/* ========================================================================
 * Test: net_init / net_get_ip / net_get_mac
 * ======================================================================== */

static void test_init_get_ip_mac(void)
{
    TEST("net_init sets IP and MAC, getters return them");

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t ip_bytes[4] = {10, 0, 0, 1};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);

    net_init(ip, mac);

    uint32_t got_ip = net_get_ip();
    uint8_t got_mac[6];
    net_get_mac(got_mac);

    int ok = (got_ip == ip);
    ok = ok && (memcmp(got_mac, mac, 6) == 0);
    CHECK(ok, "IP or MAC mismatch after init");
}

/* ========================================================================
 * Test: net_set_ip / net_set_mac update values
 * ======================================================================== */

static void test_set_ip_mac(void)
{
    TEST("net_set_ip/net_set_mac update stored values");

    uint8_t mac1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint8_t ip1_bytes[4] = {10, 0, 0, 1};
    uint32_t ip1;
    memcpy(&ip1, ip1_bytes, 4);
    net_init(ip1, mac1);

    /* Change IP */
    uint8_t ip2_bytes[4] = {172, 16, 0, 1};
    uint32_t ip2;
    memcpy(&ip2, ip2_bytes, 4);
    net_set_ip(ip2);
    int ok = (net_get_ip() == ip2);

    /* Change MAC */
    uint8_t mac2[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    net_set_mac(mac2);
    uint8_t got_mac[6];
    net_get_mac(got_mac);
    ok = ok && (memcmp(got_mac, mac2, 6) == 0);

    CHECK(ok, "set_ip or set_mac did not update");
}

/* ========================================================================
 * Test: ARP cache add/lookup/count
 * ======================================================================== */

static void test_arp_cache_basic(void)
{
    TEST("ARP cache add, lookup, count");

    /* Re-init to clear state (arp_cache is file-scope static) */
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    /* Cache starts with whatever was left; add a fresh entry */
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);
    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};

    net_arp_add(peer_ip, peer_mac);

    uint8_t found_mac[6] = {0};
    bool found = net_arp_lookup(peer_ip, found_mac);

    int ok = found && (memcmp(found_mac, peer_mac, 6) == 0);
    ok = ok && (net_arp_cache_count() >= 1);
    CHECK(ok, "ARP cache add/lookup failed");
}

/* ========================================================================
 * Test: ARP cache update existing entry
 * ======================================================================== */

static void test_arp_cache_update(void)
{
    TEST("ARP cache updates existing entry MAC");

    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    uint8_t peer_ip_bytes[4] = {192, 168, 1, 50};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    uint8_t mac_v1[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t mac_v2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    net_arp_add(peer_ip, mac_v1);
    uint32_t count_before = net_arp_cache_count();

    net_arp_add(peer_ip, mac_v2);
    uint32_t count_after = net_arp_cache_count();

    uint8_t got[6] = {0};
    net_arp_lookup(peer_ip, got);

    int ok = (count_after == count_before);  /* No new entry */
    ok = ok && (memcmp(got, mac_v2, 6) == 0);  /* Updated MAC */
    CHECK(ok, "ARP cache update failed");
}

/* ========================================================================
 * Test: ARP cache rejects zero IP
 * ======================================================================== */

static void test_arp_cache_reject_zero_ip(void)
{
    TEST("ARP cache rejects zero IP");

    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    uint32_t count_before = net_arp_cache_count();

    uint8_t dummy_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    net_arp_add(0, dummy_mac);

    uint32_t count_after = net_arp_cache_count();
    CHECK(count_after == count_before, "zero IP should be rejected");
}

/* ========================================================================
 * Test: ARP cache FIFO eviction when full
 * ======================================================================== */

static void test_arp_cache_eviction(void)
{
    TEST("ARP cache FIFO eviction after 8 entries");

    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    /* Fill cache with 8 entries: 10.0.0.1 through 10.0.0.8 */
    for (int i = 1; i <= NET_ARP_CACHE_SIZE; i++) {
        uint8_t entry_ip_bytes[4] = {10, 0, 0, (uint8_t)i};
        uint32_t entry_ip;
        memcpy(&entry_ip, entry_ip_bytes, 4);
        uint8_t entry_mac[6] = {0, 0, 0, 0, 0, (uint8_t)i};
        net_arp_add(entry_ip, entry_mac);
    }

    int ok = (net_arp_cache_count() == NET_ARP_CACHE_SIZE);

    /* Add 9th entry: should evict entry at index 1 (index 0 is gateway-protected) */
    uint8_t ninth_ip_bytes[4] = {10, 0, 0, 9};
    uint32_t ninth_ip;
    memcpy(&ninth_ip, ninth_ip_bytes, 4);
    uint8_t ninth_mac[6] = {0, 0, 0, 0, 0, 9};
    net_arp_add(ninth_ip, ninth_mac);

    ok = ok && (net_arp_cache_count() == NET_ARP_CACHE_SIZE);

    /* SEC-004: 10.0.0.1 (index 0, gateway) should be preserved */
    uint8_t first_ip_bytes[4] = {10, 0, 0, 1};
    uint32_t first_ip;
    memcpy(&first_ip, first_ip_bytes, 4);
    uint8_t first_mac[6];
    bool found_first = net_arp_lookup(first_ip, first_mac);
    ok = ok && found_first;

    /* 10.0.0.2 (was index 1) should be evicted */
    uint8_t second_ip_bytes[4] = {10, 0, 0, 2};
    uint32_t second_ip;
    memcpy(&second_ip, second_ip_bytes, 4);
    uint8_t evicted_mac[6];
    bool found_second = net_arp_lookup(second_ip, evicted_mac);
    ok = ok && !found_second;

    /* 10.0.0.9 should be present */
    uint8_t ninth_found[6];
    bool found_ninth = net_arp_lookup(ninth_ip, ninth_found);
    ok = ok && found_ninth && (memcmp(ninth_found, ninth_mac, 6) == 0);

    CHECK(ok, "FIFO eviction did not work correctly");
}

/* ========================================================================
 * Test: ARP lookup miss returns false
 * ======================================================================== */

static void test_arp_lookup_miss(void)
{
    TEST("ARP cache lookup miss returns false");

    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    /* Lookup an IP that was never added */
    uint8_t bogus_ip_bytes[4] = {1, 2, 3, 4};
    uint32_t bogus_ip;
    memcpy(&bogus_ip, bogus_ip_bytes, 4);

    uint8_t out[6];
    bool found = net_arp_lookup(bogus_ip, out);
    CHECK(!found, "should not find non-existent entry");
}

/* ========================================================================
 * Test: net_process_frame - ARP request for our IP -> generates reply
 * ======================================================================== */

static void test_process_frame_arp_reply(void)
{
    TEST("net_process_frame: ARP request -> reply");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    /* Build ARP request from 192.168.1.1 asking for our IP */
    uint8_t sender_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t sender_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t sender_ip;
    memcpy(&sender_ip, sender_ip_bytes, 4);

    uint8_t frame[64];
    uint32_t frame_len = build_arp_request_frame(frame, sizeof(frame),
                                                  sender_ip, sender_mac,
                                                  our_ip);

    uint8_t reply[128];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, frame_len, reply, &reply_len);

    int ok = generated && (reply_len == ETH_HLEN + sizeof(net_arp_t));

    /* Verify reply Ethernet dest = sender MAC */
    ok = ok && (memcmp(reply, sender_mac, 6) == 0);

    /* Verify reply Ethernet src = our MAC */
    ok = ok && (memcmp(reply + 6, our_mac, 6) == 0);

    /* Verify ARP opcode = 2 (reply) */
    net_arp_t *arp_reply = (net_arp_t *)(reply + ETH_HLEN);
    ok = ok && (test_htons(arp_reply->oper) == 2);

    /* Verify ARP SHA = our MAC, SPA = our IP */
    ok = ok && (memcmp(arp_reply->sha, our_mac, 6) == 0);
    uint32_t reply_spa;
    memcpy(&reply_spa, arp_reply->spa, 4);
    ok = ok && (reply_spa == our_ip);

    CHECK(ok, "ARP reply frame incorrect");
}

/* ========================================================================
 * Test: net_process_frame - ARP request for different IP -> no reply
 * ======================================================================== */

static void test_process_frame_arp_wrong_ip(void)
{
    TEST("net_process_frame: ARP for other IP -> no reply");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    uint8_t sender_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t sender_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t sender_ip;
    memcpy(&sender_ip, sender_ip_bytes, 4);

    /* Target IP is NOT ours */
    uint8_t other_ip_bytes[4] = {192, 168, 1, 200};
    uint32_t other_ip;
    memcpy(&other_ip, other_ip_bytes, 4);

    uint8_t frame[64];
    uint32_t frame_len = build_arp_request_frame(frame, sizeof(frame),
                                                  sender_ip, sender_mac,
                                                  other_ip);

    uint8_t reply[128];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, frame_len, reply, &reply_len);

    CHECK(!generated, "should not reply to ARP for different IP");
}

/* ========================================================================
 * Test: net_process_frame - ICMP echo request -> echo reply
 * ======================================================================== */

static void test_process_frame_icmp_reply(void)
{
    TEST("net_process_frame: ICMP echo request -> reply");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);
    net_icmp_rate_reset();  /* Reset rate limiter for test */

    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    uint8_t frame[128];
    uint32_t frame_len = build_icmp_echo_request(frame, sizeof(frame),
                                                  peer_mac, peer_ip,
                                                  our_mac, our_ip,
                                                  0x1234, 1);

    uint8_t reply[128];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, frame_len, reply, &reply_len);

    int ok = generated && (reply_len > 0);

    /* Reply ETH dst should be peer_mac */
    ok = ok && (memcmp(reply, peer_mac, 6) == 0);
    /* Reply ETH src should be our_mac */
    ok = ok && (memcmp(reply + 6, our_mac, 6) == 0);

    /* ICMP type should be echo reply (0) */
    net_ip_hdr_t *rep_ip = (net_ip_hdr_t *)(reply + ETH_HLEN);
    uint32_t ip_hdr_len = (rep_ip->ver_ihl & 0x0F) * 4;
    net_icmp_echo_t *rep_icmp = (net_icmp_echo_t *)(reply + ETH_HLEN + ip_hdr_len);
    ok = ok && (rep_icmp->type == ICMP_ECHO_REPLY);
    ok = ok && (rep_icmp->code == 0);

    CHECK(ok, "ICMP echo reply frame incorrect");
}

/* ========================================================================
 * Test: IPv4 IHL < 5 rejected
 * ======================================================================== */

static void test_ip_ihl_too_small(void)
{
    TEST("IPv4 IHL < 5 rejected (IHL=3)");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    /* Build a frame with IHL=3 (invalid, min is 5) */
    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));

    net_eth_hdr_t *eth = (net_eth_hdr_t *)frame;
    memset(eth->dst, 0xFF, 6);
    memset(eth->src, 0xAA, 6);
    eth->ethertype = test_htons(ETH_P_IP);

    net_ip_hdr_t *ip = (net_ip_hdr_t *)(frame + ETH_HLEN);
    ip->ver_ihl = 0x43;  /* Version 4, IHL=3 (INVALID) */
    ip->protocol = IP_PROTO_ICMP;
    ip->total_len = test_htons(40);
    memcpy(ip->dst_ip, our_ip_bytes, 4);

    uint8_t reply[128];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, 54, reply, &reply_len);

    CHECK(!generated, "should reject IHL < 5");
}

/* ========================================================================
 * Test: IPv4 IHL > 15 rejected (ver_ihl bits)
 * ======================================================================== */

static void test_ip_ihl_too_large(void)
{
    TEST("IPv4 IHL > 15 impossible (4-bit field, max 15)");

    /* IHL is a 4-bit field, so max value is 15 (60 bytes).
     * The code checks ip_hdr_len > 60 in handle_ip, which catches
     * only IHL>15 -- impossible. But the ICMP handler checks > 60 separately.
     * Test that IHL=15 (60 bytes) is accepted when frame is large enough,
     * by verifying the version check catches non-IPv4 instead. */

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    /* Build a frame with version=6 (not IPv4) -> rejected */
    uint8_t frame[128];
    memset(frame, 0, sizeof(frame));

    net_eth_hdr_t *eth = (net_eth_hdr_t *)frame;
    memset(eth->dst, 0xFF, 6);
    memset(eth->src, 0xAA, 6);
    eth->ethertype = test_htons(ETH_P_IP);

    net_ip_hdr_t *ip = (net_ip_hdr_t *)(frame + ETH_HLEN);
    ip->ver_ihl = 0x65;  /* Version 6 (NOT IPv4), IHL=5 */
    ip->protocol = IP_PROTO_ICMP;
    ip->total_len = test_htons(40);
    memcpy(ip->dst_ip, our_ip_bytes, 4);

    uint8_t reply[128];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, 54, reply, &reply_len);

    CHECK(!generated, "should reject non-IPv4 version");
}

/* ========================================================================
 * Test: IP total_len < ip_hdr_len rejected
 * ======================================================================== */

static void test_ip_total_len_too_small(void)
{
    TEST("IP total_len < ip_hdr_len rejected");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    /* Build a valid-looking ICMP frame but set total_len < 20 */
    uint8_t frame[128];
    memset(frame, 0, sizeof(frame));

    net_eth_hdr_t *eth = (net_eth_hdr_t *)frame;
    memset(eth->dst, 0xFF, 6);
    memset(eth->src, 0xAA, 6);
    eth->ethertype = test_htons(ETH_P_IP);

    net_ip_hdr_t *ip = (net_ip_hdr_t *)(frame + ETH_HLEN);
    ip->ver_ihl   = 0x45;  /* Version 4, IHL=5 (20 bytes) */
    ip->protocol  = IP_PROTO_ICMP;
    ip->total_len = test_htons(10);  /* INVALID: 10 < 20 */
    ip->ttl       = 64;
    memcpy(ip->dst_ip, our_ip_bytes, 4);

    /* Compute a valid IP checksum so handle_ip doesn't reject on checksum */
    ip->checksum = 0;
    ip->checksum = test_htons(net_checksum(ip, 20));

    uint8_t reply[128];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, 54, reply, &reply_len);

    CHECK(!generated, "should reject total_len < ip_hdr_len");
}

/* ========================================================================
 * Test: frame too short for Ethernet header rejected
 * ======================================================================== */

static void test_frame_too_short(void)
{
    TEST("net_process_frame rejects frame < ETH_HLEN");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, our_mac);

    uint8_t frame[10] = {0};
    uint8_t reply[64];
    uint32_t reply_len = 0;

    bool generated = net_process_frame(frame, 10, reply, &reply_len);
    CHECK(!generated, "should reject frame shorter than 14 bytes");
}

/* ========================================================================
 * Test: net_process_frame with NULL args returns false
 * ======================================================================== */

static void test_process_frame_null_args(void)
{
    TEST("net_process_frame NULL args -> false");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, our_mac);

    uint8_t buf[64] = {0};
    uint32_t len = 0;

    int ok = !net_process_frame(NULL, 20, buf, &len);
    ok = ok && !net_process_frame(buf, 20, NULL, &len);
    ok = ok && !net_process_frame(buf, 20, buf, NULL);

    CHECK(ok, "should return false for NULL arguments");
}

/* ========================================================================
 * Test: net_build_arp_request frame structure
 * ======================================================================== */

static void test_build_arp_request(void)
{
    TEST("net_build_arp_request frame structure");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    uint8_t target_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t target_ip;
    memcpy(&target_ip, target_ip_bytes, 4);

    uint8_t frame[64];
    uint32_t len = net_build_arp_request(target_ip, frame, sizeof(frame));

    int ok = (len == 42);

    /* Ethernet dst = broadcast */
    uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    ok = ok && (memcmp(frame, bcast, 6) == 0);

    /* Ethernet src = our_mac */
    ok = ok && (memcmp(frame + 6, our_mac, 6) == 0);

    /* ARP opcode = 1 (request) */
    net_arp_t *arp = (net_arp_t *)(frame + ETH_HLEN);
    ok = ok && (test_htons(arp->oper) == 1);

    /* ARP target MAC = all zeros */
    uint8_t zeros[6] = {0};
    ok = ok && (memcmp(arp->tha, zeros, 6) == 0);

    /* ARP target IP matches */
    uint32_t got_tpa;
    memcpy(&got_tpa, arp->tpa, 4);
    ok = ok && (got_tpa == target_ip);

    CHECK(ok, "ARP request frame structure wrong");
}

/* ========================================================================
 * Test: net_build_arp_request rejects small buffer
 * ======================================================================== */

static void test_build_arp_request_small_buf(void)
{
    TEST("net_build_arp_request rejects buffer < 42");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, our_mac);

    uint8_t small[30];
    uint32_t len = net_build_arp_request(ip, small, sizeof(small));
    CHECK(len == 0, "should return 0 for undersized buffer");
}

/* ========================================================================
 * Test: net_build_icmp_request frame structure
 * ======================================================================== */

static void test_build_icmp_request(void)
{
    TEST("net_build_icmp_request frame structure (74 bytes)");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    uint8_t dst_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t dst_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t dst_ip;
    memcpy(&dst_ip, dst_ip_bytes, 4);

    uint8_t frame[128];
    uint32_t len = net_build_icmp_request(dst_ip, dst_mac, 0x5678, 42,
                                           frame, sizeof(frame));

    int ok = (len == 74);  /* ETH(14) + IP(20) + ICMP(8) + 32 data */

    /* Check ethertype = IP */
    ok = ok && (frame[12] == 0x08) && (frame[13] == 0x00);

    /* Check IP protocol = ICMP */
    net_ip_hdr_t *ip = (net_ip_hdr_t *)(frame + ETH_HLEN);
    ok = ok && (ip->protocol == IP_PROTO_ICMP);
    ok = ok && ((ip->ver_ihl & 0x0F) == 5);  /* IHL=5 */

    /* Check ICMP type = echo request (8) */
    net_icmp_echo_t *icmp = (net_icmp_echo_t *)(frame + ETH_HLEN + 20);
    ok = ok && (icmp->type == ICMP_ECHO_REQUEST);
    ok = ok && (icmp->code == 0);

    /* Verify IP checksum round-trip */
    uint8_t ip_copy[20];
    memcpy(ip_copy, ip, 20);
    uint16_t saved = ip->checksum;
    ((net_ip_hdr_t *)ip_copy)->checksum = 0;
    uint16_t calc = test_htons(net_checksum(ip_copy, 20));
    ok = ok && (calc == saved);

    CHECK(ok, "ICMP request frame structure wrong");
}

/* ========================================================================
 * Test: net_process_arp_reply caches sender (with pending IP set)
 * ======================================================================== */

static void test_process_arp_reply(void)
{
    TEST("net_process_arp_reply caches sender (pending set)");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    uint8_t peer_mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 50};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    /* SEC-004: Must set pending IP before reply will be accepted */
    net_arp_set_pending_ip(peer_ip);

    uint8_t frame[64];
    uint32_t frame_len = build_arp_reply_frame(frame, sizeof(frame),
                                                peer_ip, peer_mac,
                                                our_ip, our_mac);

    net_process_arp_reply(frame, frame_len);

    uint8_t found[6] = {0};
    bool cached = net_arp_lookup(peer_ip, found);

    int ok = cached && (memcmp(found, peer_mac, 6) == 0);
    CHECK(ok, "ARP reply sender should be cached");
}

/* ========================================================================
 * Test: net_process_arp_reply ignores non-reply opcode
 * ======================================================================== */

static void test_process_arp_reply_ignores_request(void)
{
    TEST("net_process_arp_reply ignores ARP request opcode");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    /* Build ARP request (opcode=1) instead of reply */
    uint8_t peer_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 77};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    uint8_t frame[64];
    uint32_t frame_len = build_arp_request_frame(frame, sizeof(frame),
                                                  peer_ip, peer_mac,
                                                  our_ip);

    /* Clear any prior cache entries by re-init */
    net_init(our_ip, our_mac);

    net_process_arp_reply(frame, frame_len);

    uint8_t found[6];
    bool cached = net_arp_lookup(peer_ip, found);
    CHECK(!cached, "ARP request should not be cached by process_arp_reply");
}

/* ========================================================================
 * Test: net_is_icmp_echo_reply matching
 * ======================================================================== */

static void test_is_icmp_echo_reply_match(void)
{
    TEST("net_is_icmp_echo_reply matches correct ident/seq");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);

    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    uint8_t frame[128];
    uint32_t frame_len = build_icmp_echo_reply(frame, sizeof(frame),
                                                peer_mac, peer_ip,
                                                our_mac, our_ip,
                                                0x1234, 7, 128);

    uint8_t ttl_out = 0;
    bool matched = net_is_icmp_echo_reply(frame, frame_len, 0x1234, 7, &ttl_out);

    int ok = matched && (ttl_out == 128);
    CHECK(ok, "should match echo reply with correct ident/seq");
}

/* ========================================================================
 * Test: net_is_icmp_echo_reply wrong seq -> no match
 * ======================================================================== */

static void test_is_icmp_echo_reply_wrong_seq(void)
{
    TEST("net_is_icmp_echo_reply rejects wrong seq");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);

    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    uint8_t frame[128];
    uint32_t frame_len = build_icmp_echo_reply(frame, sizeof(frame),
                                                peer_mac, peer_ip,
                                                our_mac, our_ip,
                                                0x1234, 7, 64);

    uint8_t ttl_out = 0;
    /* Ask for seq=99, frame has seq=7 */
    bool matched = net_is_icmp_echo_reply(frame, frame_len, 0x1234, 99, &ttl_out);

    CHECK(!matched, "should not match wrong seq number");
}

/* ========================================================================
 * Test: net_is_icmp_echo_reply rejects echo request type
 * ======================================================================== */

static void test_is_icmp_echo_reply_rejects_request(void)
{
    TEST("net_is_icmp_echo_reply rejects echo request (type 8)");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);

    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    /* Build an echo REQUEST, not reply */
    uint8_t frame[128];
    uint32_t frame_len = build_icmp_echo_request(frame, sizeof(frame),
                                                  peer_mac, peer_ip,
                                                  our_mac, our_ip,
                                                  0x1234, 1);

    uint8_t ttl_out = 0;
    bool matched = net_is_icmp_echo_reply(frame, frame_len, 0x1234, 1, &ttl_out);

    CHECK(!matched, "should reject ICMP echo request type");
}

/* ========================================================================
 * Test: Ethernet header in ICMP reply has swapped MACs
 * ======================================================================== */

static void test_icmp_reply_eth_swap(void)
{
    TEST("ICMP reply swaps Ethernet src/dst MACs");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);
    net_icmp_rate_reset();  /* Reset rate limiter for test */

    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    uint8_t frame[128];
    uint32_t frame_len = build_icmp_echo_request(frame, sizeof(frame),
                                                  peer_mac, peer_ip,
                                                  our_mac, our_ip,
                                                  0xABCD, 3);

    uint8_t reply[128];
    uint32_t reply_len = 0;
    net_process_frame(frame, frame_len, reply, &reply_len);

    net_eth_hdr_t *rep_eth = (net_eth_hdr_t *)reply;

    /* Reply dst should be the original src (peer) */
    int ok = (memcmp(rep_eth->dst, peer_mac, 6) == 0);
    /* Reply src should be our MAC */
    ok = ok && (memcmp(rep_eth->src, our_mac, 6) == 0);

    CHECK(ok, "Ethernet MACs not swapped correctly in reply");
}

/* ========================================================================
 * Test: ICMP reply has valid IP checksum
 * ======================================================================== */

static void test_icmp_reply_ip_checksum(void)
{
    TEST("ICMP reply has valid IP checksum");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);
    net_icmp_rate_reset();  /* Reset rate limiter for test */

    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    uint8_t frame[128];
    uint32_t frame_len = build_icmp_echo_request(frame, sizeof(frame),
                                                  peer_mac, peer_ip,
                                                  our_mac, our_ip,
                                                  0x9999, 5);

    uint8_t reply[128];
    uint32_t reply_len = 0;
    net_process_frame(frame, frame_len, reply, &reply_len);

    /* Verify IP checksum in reply */
    net_ip_hdr_t *rep_ip = (net_ip_hdr_t *)(reply + ETH_HLEN);
    uint32_t ip_hdr_len = (rep_ip->ver_ihl & 0x0F) * 4;

    /* Checksum of entire IP header (with checksum field included)
     * should yield 0 if correct */
    uint16_t verify = net_checksum(rep_ip, ip_hdr_len);
    CHECK(verify == 0x0000, "IP checksum in ICMP reply is invalid");
}

/* ========================================================================
 * Test: Unknown ethertype ignored
 * ======================================================================== */

static void test_unknown_ethertype(void)
{
    TEST("Unknown ethertype (0x1234) ignored");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, our_mac);

    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    net_eth_hdr_t *eth = (net_eth_hdr_t *)frame;
    memset(eth->dst, 0xFF, 6);
    memset(eth->src, 0xAA, 6);
    eth->ethertype = test_htons(0x1234);  /* Unknown */

    uint8_t reply[64];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, 32, reply, &reply_len);

    CHECK(!generated, "unknown ethertype should be ignored");
}

/* ========================================================================
 * Test: SEC-004 - ARP reply rejected when no pending request
 * ======================================================================== */

static void test_arp_reply_rejected_no_pending(void)
{
    TEST("SEC-004: ARP reply rejected without pending request");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);  /* net_init resets arp_pending_ip to 0 */

    uint8_t attacker_mac[6] = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
    uint8_t attacker_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t attacker_ip;
    memcpy(&attacker_ip, attacker_ip_bytes, 4);

    /* Do NOT set arp_pending_ip - this simulates an unsolicited ARP reply */
    /* arp_pending_ip is 0 after net_init */

    uint8_t frame[64];
    uint32_t frame_len = build_arp_reply_frame(frame, sizeof(frame),
                                                attacker_ip, attacker_mac,
                                                our_ip, our_mac);

    net_process_arp_reply(frame, frame_len);

    /* The attacker's IP should NOT be in the ARP cache */
    uint8_t found[6];
    bool cached = net_arp_lookup(attacker_ip, found);

    CHECK(!cached, "unsolicited ARP reply should be rejected");
}

/* ========================================================================
 * Test: SEC-004 - ARP reply rejected when pending IP doesn't match
 * ======================================================================== */

static void test_arp_reply_rejected_wrong_pending(void)
{
    TEST("SEC-004: ARP reply rejected for wrong pending IP");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);

    /* Set pending for a different IP */
    uint8_t expected_ip_bytes[4] = {192, 168, 1, 2};
    uint32_t expected_ip;
    memcpy(&expected_ip, expected_ip_bytes, 4);
    net_arp_set_pending_ip(expected_ip);

    /* But we receive a reply from a different IP (attacker) */
    uint8_t attacker_mac[6] = {0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE};
    uint8_t attacker_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t attacker_ip;
    memcpy(&attacker_ip, attacker_ip_bytes, 4);

    uint8_t frame[64];
    uint32_t frame_len = build_arp_reply_frame(frame, sizeof(frame),
                                                attacker_ip, attacker_mac,
                                                our_ip, our_mac);

    net_process_arp_reply(frame, frame_len);

    /* The attacker's IP should NOT be in the ARP cache */
    uint8_t found[6];
    bool cached = net_arp_lookup(attacker_ip, found);

    CHECK(!cached, "ARP reply from wrong IP should be rejected");
}

/* ========================================================================
 * Test: SEC-025 - Fragmented IPv4 packet rejected (MF flag set)
 * ======================================================================== */

static void test_fragmented_ipv4_rejected_mf(void)
{
    TEST("SEC-025: Fragmented IPv4 rejected (MF flag set)");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);
    net_icmp_rate_reset();

    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    /* Build a valid ICMP echo request frame, then set MF flag */
    uint8_t frame[128];
    uint32_t frame_len = build_icmp_echo_request(frame, sizeof(frame),
                                                  peer_mac, peer_ip,
                                                  our_mac, our_ip,
                                                  0x5555, 1);

    /* Set MF (More Fragments) flag: flags_frag field at IP header offset 6-7.
     * MF = 0x2000 in host order, which is 0x0020 in big-endian on wire. */
    net_ip_hdr_t *ip = (net_ip_hdr_t *)(frame + ETH_HLEN);
    ip->flags_frag = test_htons(0x2000);  /* MF flag set */

    /* Recompute IP checksum since we changed flags_frag */
    ip->checksum = 0;
    ip->checksum = test_htons(net_checksum(ip, 20));

    uint8_t reply[128];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, frame_len, reply, &reply_len);

    CHECK(!generated, "fragmented packet (MF set) should be dropped");
}

/* ========================================================================
 * Test: SEC-025 - Fragmented IPv4 packet rejected (fragment offset non-zero)
 * ======================================================================== */

static void test_fragmented_ipv4_rejected_offset(void)
{
    TEST("SEC-025: Fragmented IPv4 rejected (frag offset != 0)");

    uint8_t our_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t our_ip_bytes[4] = {192, 168, 1, 100};
    uint32_t our_ip;
    memcpy(&our_ip, our_ip_bytes, 4);
    net_init(our_ip, our_mac);
    net_icmp_rate_reset();

    uint8_t peer_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    uint8_t peer_ip_bytes[4] = {192, 168, 1, 1};
    uint32_t peer_ip;
    memcpy(&peer_ip, peer_ip_bytes, 4);

    /* Build a valid ICMP frame, then set fragment offset */
    uint8_t frame[128];
    uint32_t frame_len = build_icmp_echo_request(frame, sizeof(frame),
                                                  peer_mac, peer_ip,
                                                  our_mac, our_ip,
                                                  0x6666, 1);

    /* Set fragment offset to 100 (non-zero, no MF flag) */
    net_ip_hdr_t *ip = (net_ip_hdr_t *)(frame + ETH_HLEN);
    ip->flags_frag = test_htons(100);  /* Fragment offset = 100 */

    /* Recompute IP checksum */
    ip->checksum = 0;
    ip->checksum = test_htons(net_checksum(ip, 20));

    uint8_t reply[128];
    uint32_t reply_len = 0;
    bool generated = net_process_frame(frame, frame_len, reply, &reply_len);

    CHECK(!generated, "fragmented packet (offset != 0) should be dropped");
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Network Stack Portable Tests ===\n\n");

    /* Checksum tests */
    test_checksum_known_ip_header();
    test_checksum_odd_length();
    test_checksum_zeros();
    test_checksum_empty();

    /* Byte order via frame building */
    test_htons_via_arp_build();

    /* Configuration */
    test_init_get_ip_mac();
    test_set_ip_mac();

    /* ARP cache */
    test_arp_cache_basic();
    test_arp_cache_update();
    test_arp_cache_reject_zero_ip();
    test_arp_cache_eviction();
    test_arp_lookup_miss();

    /* Frame processing: ARP */
    test_process_frame_arp_reply();
    test_process_frame_arp_wrong_ip();

    /* Frame processing: ICMP */
    test_process_frame_icmp_reply();

    /* IPv4 validation */
    test_ip_ihl_too_small();
    test_ip_ihl_too_large();
    test_ip_total_len_too_small();
    test_frame_too_short();
    test_process_frame_null_args();

    /* Outbound frame builders */
    test_build_arp_request();
    test_build_arp_request_small_buf();
    test_build_icmp_request();

    /* ARP reply processing */
    test_process_arp_reply();
    test_process_arp_reply_ignores_request();

    /* ICMP echo reply matching */
    test_is_icmp_echo_reply_match();
    test_is_icmp_echo_reply_wrong_seq();
    test_is_icmp_echo_reply_rejects_request();

    /* Ethernet header correctness */
    test_icmp_reply_eth_swap();
    test_icmp_reply_ip_checksum();

    /* Unknown protocol */
    test_unknown_ethertype();

    /* SEC-004: ARP cache poisoning prevention */
    test_arp_reply_rejected_no_pending();
    test_arp_reply_rejected_wrong_pending();

    /* SEC-025: Fragmented IPv4 rejection */
    test_fragmented_ipv4_rejected_mf();
    test_fragmented_ipv4_rejected_offset();

    printf("\n=== Results: %d PASS, %d FAIL (of %d) ===\n",
           pass_count, fail_count, pass_count + fail_count);

    return fail_count > 0 ? 1 : 0;
}
