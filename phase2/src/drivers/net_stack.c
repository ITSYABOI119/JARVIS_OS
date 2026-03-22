/*
 * JARVIS AI-OS - Basic Networking Stack Implementation
 *
 * Week 38: ARP reply + ICMP echo reply for bare-metal seL4.
 * No heap, no OS sockets. Raw Ethernet frame processing.
 */

#include "net_stack.h"
#include "bcm_genet.h"
#include "bcm2711_timer.h"
#include <sel4/sel4.h>
#include <string.h>

/* ================================================================
 * Debug Output
 * ================================================================ */

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

static void debug_hex32(uint32_t val)
{
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        seL4_DebugPutChar(hex[(val >> (i * 4)) & 0xf]);
    }
}

/* ================================================================
 * Internal State
 * ================================================================ */

static uint32_t our_ip = 0;             /* Network byte order */
static uint8_t  our_mac[ETH_ALEN] = {0};
static bool     net_initialized = false;

/* ARP cache */
static net_arp_entry_t arp_cache[NET_ARP_CACHE_SIZE];
static uint32_t arp_cache_used = 0;

/* SEC-004: ARP pending request tracker - only accept replies for this IP */
static uint32_t arp_pending_ip = 0;

/* IP identification counter for outbound packets */
static uint16_t ip_ident_counter = 1;

/* SEC-006: ICMP rate limiting */
#define ICMP_MAX_REPLIES_PER_SEC 10

static uint32_t icmp_reply_count = 0;
static uint64_t icmp_window_start = 0;

/* ================================================================
 * Byte Order Helpers (ARM is little-endian, network is big-endian)
 * ================================================================ */

static inline uint16_t htons(uint16_t h)
{
    return (uint16_t)((h >> 8) | (h << 8));
}

static inline uint16_t ntohs(uint16_t n)
{
    return htons(n);
}

/* ================================================================
 * Checksum
 * ================================================================ */

uint16_t net_checksum(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;

    /* Sum 16-bit words */
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    /* Add odd byte */
    if (len == 1) {
        sum += (uint32_t)p[0] << 8;
    }
    /* Fold 32-bit sum to 16 bits */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFF);
}

/* ================================================================
 * Configuration
 * ================================================================ */

void net_init(uint32_t ip_be, const uint8_t mac[ETH_ALEN])
{
    our_ip = ip_be;
    memcpy(our_mac, mac, ETH_ALEN);
    net_initialized = true;
    /* Clear ARP cache on re-init (old entries are stale) */
    memset(arp_cache, 0, sizeof(arp_cache));
    arp_cache_used = 0;
    /* SEC-004: Reset pending IP on re-init */
    arp_pending_ip = 0;
    /* SEC-006: Reset ICMP rate limiter on re-init */
    icmp_reply_count = 0;
    icmp_window_start = 0;
    debug_puts("[NET] Initialized\n");
}

void net_set_ip(uint32_t ip_be)
{
    our_ip = ip_be;
}

void net_set_mac(const uint8_t mac[ETH_ALEN])
{
    memcpy(our_mac, mac, ETH_ALEN);
}

/* ================================================================
 * ARP Handler
 * ================================================================ */

static bool handle_arp(const uint8_t *frame, uint32_t len,
                       uint8_t *reply, uint32_t *reply_len)
{
    if (len < ETH_HLEN + sizeof(net_arp_t)) {
        return false;
    }

    const net_arp_t *arp = (const net_arp_t *)(frame + ETH_HLEN);

    /* Only handle Ethernet/IPv4 ARP requests */
    if (ntohs(arp->htype) != 1 || ntohs(arp->ptype) != ETH_P_IP) {
        return false;
    }
    if (arp->hlen != ETH_ALEN || arp->plen != 4) {
        return false;
    }
    if (ntohs(arp->oper) != 1) {  /* Not a request */
        return false;
    }

    /* Check if target IP matches ours */
    uint32_t target_ip;
    memcpy(&target_ip, arp->tpa, 4);
    if (target_ip != our_ip) {
        return false;
    }

    debug_puts("[NET] ARP request for our IP, sending reply\n");

    /* SEC-004: Only UPDATE existing cache entries from ARP requests.
     * Do NOT add new entries from unsolicited requests (prevents cache poisoning). */
    uint32_t sender_ip;
    memcpy(&sender_ip, arp->spa, 4);
    net_arp_update_existing(sender_ip, arp->sha);

    /* Build ARP reply */
    net_eth_hdr_t *eth_reply = (net_eth_hdr_t *)reply;
    net_arp_t *arp_reply = (net_arp_t *)(reply + ETH_HLEN);

    /* Ethernet header */
    memcpy(eth_reply->dst, arp->sha, ETH_ALEN);  /* Reply to sender */
    memcpy(eth_reply->src, our_mac, ETH_ALEN);
    eth_reply->ethertype = htons(ETH_P_ARP);

    /* ARP reply */
    arp_reply->htype = htons(1);
    arp_reply->ptype = htons(ETH_P_IP);
    arp_reply->hlen  = ETH_ALEN;
    arp_reply->plen  = 4;
    arp_reply->oper  = htons(2);  /* Reply */

    memcpy(arp_reply->sha, our_mac, ETH_ALEN);
    memcpy(arp_reply->spa, arp->tpa, 4);     /* Our IP */
    memcpy(arp_reply->tha, arp->sha, ETH_ALEN);
    memcpy(arp_reply->tpa, arp->spa, 4);     /* Requester's IP */

    *reply_len = ETH_HLEN + sizeof(net_arp_t);
    return true;
}

/* ================================================================
 * ICMP Handler
 * ================================================================ */

static bool handle_icmp(const uint8_t *frame, uint32_t len,
                        uint8_t *reply, uint32_t *reply_len)
{
    const net_eth_hdr_t *eth = (const net_eth_hdr_t *)frame;
    const net_ip_hdr_t *ip = (const net_ip_hdr_t *)(frame + ETH_HLEN);

    uint32_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    uint32_t ip_total = ntohs(ip->total_len);
    uint32_t icmp_off = ETH_HLEN + ip_hdr_len;

    /* Basic IPv4 header sanity: IHL is in 32-bit words, min 20 bytes, max 60. */
    if (ip_hdr_len < 20 || ip_hdr_len > 60) {
        return false;
    }

    /* Ensure claimed IP length is coherent and within received frame. */
    if (len < ETH_HLEN + ip_hdr_len) {
        return false;
    }
    uint32_t ip_available = len - ETH_HLEN;
    if (ip_total < ip_hdr_len || ip_total > ip_available) {
        return false;
    }

    if (len < icmp_off + sizeof(net_icmp_echo_t)) {
        return false;
    }

    const net_icmp_echo_t *icmp = (const net_icmp_echo_t *)(frame + icmp_off);

    /* Only handle echo requests */
    if (icmp->type != ICMP_ECHO_REQUEST || icmp->code != 0) {
        return false;
    }

    /* SEC-006: ICMP rate limiting */
    {
        uint64_t now = systimer_read();
        if (now - icmp_window_start > 1000000) {  /* 1 second in microseconds */
            icmp_window_start = now;
            icmp_reply_count = 0;
        }
        if (icmp_reply_count >= ICMP_MAX_REPLIES_PER_SEC) {
            return false;  /* Rate limited */
        }
        icmp_reply_count++;
    }

    debug_puts("[NET] ICMP echo request, sending reply\n");

    /* Copy entire frame as basis for reply.
     * Clamp to actual received length to prevent read past buffer. */
    uint32_t total_len = ETH_HLEN + ip_total;
    if (total_len > len) total_len = len;
    if (total_len > NET_MAX_FRAME) total_len = NET_MAX_FRAME;
    memcpy(reply, frame, total_len);

    /* Fix Ethernet header: swap src/dst */
    net_eth_hdr_t *rep_eth = (net_eth_hdr_t *)reply;
    memcpy(rep_eth->dst, eth->src, ETH_ALEN);
    memcpy(rep_eth->src, our_mac, ETH_ALEN);

    /* Fix IP header: swap src/dst, recalculate checksum */
    net_ip_hdr_t *rep_ip = (net_ip_hdr_t *)(reply + ETH_HLEN);
    memcpy(rep_ip->dst_ip, ip->src_ip, 4);
    memcpy(rep_ip->src_ip, &our_ip, 4);
    rep_ip->ttl = 64;
    rep_ip->checksum = 0;
    rep_ip->checksum = htons(net_checksum(rep_ip, ip_hdr_len));

    /* Fix ICMP: change type to reply, recalculate checksum */
    net_icmp_echo_t *rep_icmp = (net_icmp_echo_t *)(reply + icmp_off);
    rep_icmp->type = ICMP_ECHO_REPLY;
    rep_icmp->checksum = 0;

    uint32_t icmp_len = ip_total - ip_hdr_len;
    rep_icmp->checksum = htons(net_checksum(rep_icmp, icmp_len));

    *reply_len = total_len;
    return true;
}

/* ================================================================
 * IP Handler
 * ================================================================ */

static bool handle_ip(const uint8_t *frame, uint32_t len,
                      uint8_t *reply, uint32_t *reply_len)
{
    if (len < ETH_HLEN + IP_HDR_MIN_LEN) {
        return false;
    }

    const net_ip_hdr_t *ip = (const net_ip_hdr_t *)(frame + ETH_HLEN);

    /* Validate IPv4 */
    uint8_t version = (ip->ver_ihl >> 4) & 0x0F;
    uint8_t ihl = ip->ver_ihl & 0x0F;

    if (version != 4 || ihl < 5) {
        return false;
    }

    /* Verify IP header checksum */
    uint32_t ip_hdr_len = ihl * 4;
    uint16_t saved_cksum = ip->checksum;
    /* Create a temporary copy for checksum verification */
    uint8_t ip_copy[60];  /* Max IP header = 15*4 = 60 bytes */
    if (ip_hdr_len > 60 || ip_hdr_len > len - ETH_HLEN) {
        return false;
    }
    memcpy(ip_copy, ip, ip_hdr_len);
    ((net_ip_hdr_t *)ip_copy)->checksum = 0;
    uint16_t calc_cksum = htons(net_checksum(ip_copy, ip_hdr_len));
    if (calc_cksum != saved_cksum) {
        debug_puts("[NET] IP checksum mismatch\n");
        return false;
    }

    /* SEC-025: Reject fragmented IPv4 packets.
     * flags_frag field: bit 13 = MF (More Fragments), bits 0-12 = Fragment Offset.
     * If MF is set or fragment offset is non-zero, drop the packet. */
    {
        uint16_t flags_frag = ntohs(ip->flags_frag);
        if ((flags_frag & 0x2000) || (flags_frag & 0x1FFF)) {
            debug_puts("[NET] Fragmented IPv4 packet dropped\n");
            return false;
        }
    }

    /* Check if packet is for us */
    uint32_t dst_ip;
    memcpy(&dst_ip, ip->dst_ip, 4);
    if (dst_ip != our_ip) {
        return false;  /* Not for us */
    }

    /* Route by protocol */
    if (ip->protocol == IP_PROTO_ICMP) {
        return handle_icmp(frame, len, reply, reply_len);
    }

    return false;
}

/* ================================================================
 * Main Frame Dispatcher
 * ================================================================ */

bool net_process_frame(const uint8_t *frame, uint32_t len,
                       uint8_t *reply_buf, uint32_t *reply_len)
{
    if (!net_initialized || !frame || !reply_buf || !reply_len) {
        return false;
    }
    if (len < ETH_HLEN) {
        return false;
    }

    *reply_len = 0;

    const net_eth_hdr_t *eth = (const net_eth_hdr_t *)frame;
    uint16_t ethertype = ntohs(eth->ethertype);

    switch (ethertype) {
        case ETH_P_ARP:
            return handle_arp(frame, len, reply_buf, reply_len);
        case ETH_P_IP:
            return handle_ip(frame, len, reply_buf, reply_len);
        default:
            return false;
    }
}

/* ================================================================
 * Getters
 * ================================================================ */

uint32_t net_get_ip(void)
{
    return our_ip;
}

void net_get_mac(uint8_t mac_out[ETH_ALEN])
{
    memcpy(mac_out, our_mac, ETH_ALEN);
}

/* ================================================================
 * ARP Cache
 * ================================================================ */

bool net_arp_lookup(uint32_t ip_be, uint8_t mac_out[ETH_ALEN])
{
    for (uint32_t i = 0; i < arp_cache_used; i++) {
        if (arp_cache[i].ip == ip_be) {
            memcpy(mac_out, arp_cache[i].mac, ETH_ALEN);
            return true;
        }
    }
    return false;
}

void net_arp_add(uint32_t ip_be, const uint8_t mac[ETH_ALEN])
{
    if (ip_be == 0) return;

    /* Update existing entry */
    for (uint32_t i = 0; i < arp_cache_used; i++) {
        if (arp_cache[i].ip == ip_be) {
            memcpy(arp_cache[i].mac, mac, ETH_ALEN);
            return;
        }
    }

    /* Add new entry */
    if (arp_cache_used < NET_ARP_CACHE_SIZE) {
        arp_cache[arp_cache_used].ip = ip_be;
        memcpy(arp_cache[arp_cache_used].mac, mac, ETH_ALEN);
        arp_cache_used++;
    } else {
        /* SEC-004: Evict oldest entry, but skip index 0 if it's the gateway
         * (gateway entries are non-evictable to prevent ARP cache poisoning) */
        uint32_t evict_start = 0;
        if (arp_cache_used > 0 && arp_cache[0].ip != 0) {
            /* Check if index 0 looks like a gateway (first entry added).
             * If the cache is full and index 0 is populated, protect it. */
            evict_start = 1;
        }
        for (uint32_t i = evict_start; i < NET_ARP_CACHE_SIZE - 1; i++) {
            arp_cache[i] = arp_cache[i + 1];
        }
        arp_cache[NET_ARP_CACHE_SIZE - 1].ip = ip_be;
        memcpy(arp_cache[NET_ARP_CACHE_SIZE - 1].mac, mac, ETH_ALEN);
    }
}

/* SEC-004: Update ONLY existing ARP cache entries (no new additions).
 * Used by handle_arp() for incoming ARP requests to prevent cache poisoning
 * from unsolicited ARP requests. */
void net_arp_update_existing(uint32_t ip_be, const uint8_t mac[ETH_ALEN])
{
    if (ip_be == 0) return;

    for (uint32_t i = 0; i < arp_cache_used; i++) {
        if (arp_cache[i].ip == ip_be) {
            memcpy(arp_cache[i].mac, mac, ETH_ALEN);
            return;
        }
    }
    /* IP not in cache - do NOT add it */
}

uint32_t net_arp_cache_count(void)
{
    return arp_cache_used;
}

/* ================================================================
 * Outbound Frame Builders
 * ================================================================ */

uint32_t net_build_arp_request(uint32_t target_ip_be,
                               uint8_t *frame_buf, uint32_t buf_size)
{
    uint32_t frame_len = ETH_HLEN + sizeof(net_arp_t);  /* 14 + 28 = 42 */
    if (buf_size < frame_len || !net_initialized) {
        return 0;
    }

    /* SEC-004: Set pending IP so we only accept replies for this target */
    arp_pending_ip = target_ip_be;

    memset(frame_buf, 0, frame_len);

    /* Ethernet header: broadcast destination */
    net_eth_hdr_t *eth = (net_eth_hdr_t *)frame_buf;
    memset(eth->dst, 0xFF, ETH_ALEN);
    memcpy(eth->src, our_mac, ETH_ALEN);
    eth->ethertype = htons(ETH_P_ARP);

    /* ARP request (opcode=1) */
    net_arp_t *arp = (net_arp_t *)(frame_buf + ETH_HLEN);
    arp->htype = htons(1);
    arp->ptype = htons(ETH_P_IP);
    arp->hlen  = ETH_ALEN;
    arp->plen  = 4;
    arp->oper  = htons(1);  /* Request */

    memcpy(arp->sha, our_mac, ETH_ALEN);
    memcpy(arp->spa, &our_ip, 4);
    memset(arp->tha, 0, ETH_ALEN);  /* Unknown target MAC */
    memcpy(arp->tpa, &target_ip_be, 4);

    return frame_len;
}

uint32_t net_build_icmp_request(uint32_t target_ip_be,
                                const uint8_t dst_mac[ETH_ALEN],
                                uint16_t ident, uint16_t seq,
                                uint8_t *frame_buf, uint32_t buf_size)
{
    /* ETH(14) + IP(20) + ICMP(8) + 32 bytes data = 74 */
    uint32_t icmp_data_len = 32;
    uint32_t icmp_total = 8 + icmp_data_len;  /* 40 */
    uint32_t ip_total = 20 + icmp_total;       /* 60 */
    uint32_t frame_len = ETH_HLEN + ip_total;  /* 74 */

    if (buf_size < frame_len || !net_initialized) {
        return 0;
    }

    memset(frame_buf, 0, frame_len);

    /* Ethernet header */
    net_eth_hdr_t *eth = (net_eth_hdr_t *)frame_buf;
    memcpy(eth->dst, dst_mac, ETH_ALEN);
    memcpy(eth->src, our_mac, ETH_ALEN);
    eth->ethertype = htons(ETH_P_IP);

    /* IP header */
    net_ip_hdr_t *ip = (net_ip_hdr_t *)(frame_buf + ETH_HLEN);
    ip->ver_ihl   = 0x45;  /* IPv4, IHL=5 */
    ip->dscp_ecn  = 0;
    ip->total_len = htons((uint16_t)ip_total);
    ip->ident     = htons(ip_ident_counter++);
    ip->flags_frag = 0;
    ip->ttl       = 64;
    ip->protocol  = IP_PROTO_ICMP;
    ip->checksum  = 0;
    memcpy(ip->src_ip, &our_ip, 4);
    memcpy(ip->dst_ip, &target_ip_be, 4);

    /* IP checksum */
    ip->checksum = htons(net_checksum(ip, 20));

    /* ICMP echo request */
    net_icmp_echo_t *icmp = (net_icmp_echo_t *)(frame_buf + ETH_HLEN + 20);
    icmp->type     = ICMP_ECHO_REQUEST;
    icmp->code     = 0;
    icmp->checksum = 0;
    icmp->ident    = htons(ident);
    icmp->seq      = htons(seq);

    /* Fill 32 bytes of data */
    uint8_t *data = frame_buf + ETH_HLEN + 20 + 8;
    for (uint32_t i = 0; i < icmp_data_len; i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }

    /* ICMP checksum (header + data) */
    icmp->checksum = htons(net_checksum(icmp, icmp_total));

    return frame_len;
}

/* ================================================================
 * ARP Resolution
 * ================================================================ */

bool net_arp_resolve(uint32_t target_ip_be, uint8_t mac_out[ETH_ALEN],
                     uint32_t timeout_ms)
{
    /* Check cache first */
    if (net_arp_lookup(target_ip_be, mac_out)) {
        return true;
    }

    /* Build and send ARP request */
    uint8_t arp_frame[64];
    uint32_t arp_len = net_build_arp_request(target_ip_be, arp_frame, sizeof(arp_frame));
    if (arp_len == 0) {
        return false;
    }

    if (!genet_tx_send(arp_frame, arp_len)) {
        debug_puts("[NET] ARP resolve: TX failed\n");
        return false;
    }

    /* Poll for ARP reply */
    uint64_t start = systimer_read();
    uint64_t deadline = start + (uint64_t)timeout_ms * 1000;

    while (systimer_read() < deadline) {
        uint8_t rx_buf[NET_MAX_FRAME];
        uint32_t rx_len = 0;

        if (genet_rx_recv(rx_buf, sizeof(rx_buf), &rx_len)) {
            /* Check if this is an ARP reply */
            if (rx_len >= ETH_HLEN) {
                uint16_t ethertype = ((uint16_t)rx_buf[12] << 8) | rx_buf[13];
                if (ethertype == ETH_P_ARP && rx_len >= ETH_HLEN + sizeof(net_arp_t)) {
                    net_process_arp_reply(rx_buf, rx_len);

                    /* Check if we got what we needed */
                    if (net_arp_lookup(target_ip_be, mac_out)) {
                        return true;
                    }
                }
            }
        }

        /* Brief delay to avoid spinning too tight */
        for (volatile int d = 0; d < 100; d++);
    }

    debug_puts("[NET] ARP resolve: timeout\n");
    return false;
}

/* ================================================================
 * Process ARP Reply (update cache)
 * ================================================================ */

void net_process_arp_reply(const uint8_t *frame, uint32_t len)
{
    if (len < ETH_HLEN + sizeof(net_arp_t)) {
        return;
    }

    const net_arp_t *arp = (const net_arp_t *)(frame + ETH_HLEN);

    /* Validate Ethernet/IPv4 ARP reply */
    if (ntohs(arp->htype) != 1 || ntohs(arp->ptype) != ETH_P_IP) {
        return;
    }
    if (arp->hlen != ETH_ALEN || arp->plen != 4) {
        return;
    }
    if (ntohs(arp->oper) != 2) {  /* Not a reply */
        return;
    }

    /* SEC-004: Only accept ARP replies for IPs we have a pending request for */
    uint32_t sender_ip;
    memcpy(&sender_ip, arp->spa, 4);

    if (arp_pending_ip == 0 || sender_ip != arp_pending_ip) {
        debug_puts("[NET] ARP reply rejected: no pending request for this IP\n");
        return;
    }

    /* Add sender to cache */
    net_arp_add(sender_ip, arp->sha);

    /* Clear pending once we've accepted a reply */
    arp_pending_ip = 0;

    debug_puts("[NET] ARP reply cached\n");
}

/* ================================================================
 * ICMP Echo Reply Matching
 * ================================================================ */

bool net_is_icmp_echo_reply(const uint8_t *frame, uint32_t len,
                            uint16_t ident, uint16_t seq,
                            uint8_t *ttl_out)
{
    if (len < ETH_HLEN + IP_HDR_MIN_LEN + 8) {
        return false;
    }

    /* Check ethertype */
    uint16_t ethertype = ((uint16_t)frame[12] << 8) | frame[13];
    if (ethertype != ETH_P_IP) {
        return false;
    }

    const net_ip_hdr_t *ip = (const net_ip_hdr_t *)(frame + ETH_HLEN);

    /* Check IPv4 + ICMP */
    if ((ip->ver_ihl >> 4) != 4 || ip->protocol != IP_PROTO_ICMP) {
        return false;
    }

    uint32_t ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    uint32_t icmp_off = ETH_HLEN + ip_hdr_len;

    if (ip_hdr_len < 20 || ip_hdr_len > 60) {
        return false;
    }
    if (len < ETH_HLEN + ip_hdr_len) {
        return false;
    }

    if (len < icmp_off + 8) {
        return false;
    }

    const net_icmp_echo_t *icmp = (const net_icmp_echo_t *)(frame + icmp_off);

    /* Check type = echo reply, code = 0 */
    if (icmp->type != ICMP_ECHO_REPLY || icmp->code != 0) {
        return false;
    }

    /* Match ident and seq (both in network byte order) */
    if (ntohs(icmp->ident) != ident || ntohs(icmp->seq) != seq) {
        return false;
    }

    if (ttl_out) {
        *ttl_out = ip->ttl;
    }

    return true;
}

/* ================================================================
 * SEC-006: ICMP rate limiter reset (for testing)
 * ================================================================ */

void net_icmp_rate_reset(void)
{
    icmp_reply_count = 0;
    icmp_window_start = 0;
}

/* ================================================================
 * SEC-004: Get/set ARP pending IP (for testing)
 * ================================================================ */

uint32_t net_arp_get_pending_ip(void)
{
    return arp_pending_ip;
}

void net_arp_set_pending_ip(uint32_t ip_be)
{
    arp_pending_ip = ip_be;
}
