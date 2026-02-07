/*
 * JARVIS AI-OS - Basic Networking Stack Implementation
 *
 * Week 38: ARP reply + ICMP echo reply for bare-metal seL4.
 * No heap, no OS sockets. Raw Ethernet frame processing.
 */

#include "net_stack.h"
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

    if (len < icmp_off + sizeof(net_icmp_echo_t)) {
        return false;
    }

    const net_icmp_echo_t *icmp = (const net_icmp_echo_t *)(frame + icmp_off);

    /* Only handle echo requests */
    if (icmp->type != ICMP_ECHO_REQUEST || icmp->code != 0) {
        return false;
    }

    debug_puts("[NET] ICMP echo request, sending reply\n");

    /* Copy entire frame as basis for reply */
    uint32_t total_len = ETH_HLEN + ip_total;
    if (total_len > NET_MAX_FRAME) {
        total_len = NET_MAX_FRAME;
    }
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
