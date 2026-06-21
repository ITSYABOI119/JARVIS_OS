/**
 * net_udp.c - Minimal Ethernet/IPv4/UDP broadcast frame builder
 *
 * Pure logic, no dependencies (only <string.h>), host-testable. Builds a valid
 * limited-broadcast UDP/IPv4 frame so a remote console on the same L2 segment can
 * receive the box's telemetry with NO ARP and NO IP stack on the box.
 *
 * Byte order: x86 is little-endian; all on-wire header fields are big-endian, so
 * every multi-byte field is written byte-by-byte explicitly (no host casts).
 *
 * JARVIS AI-OS - Phase 4 (goal #2b N-b)
 */

#include "net_udp.h"
#include <string.h>

#define ETH_HDR_LEN     14
#define IP_HDR_LEN      20
#define UDP_HDR_LEN     8
#define HDRS_LEN        (ETH_HDR_LEN + IP_HDR_LEN + UDP_HDR_LEN)  /* 42 */
#define ETH_MIN_FRAME   60
#define IP_PROTO_UDP    17
#define IP_TTL          64
/* Max payload that still fits a standard 1500-byte MTU IP datagram */
#define UDP_MAX_PAYLOAD (1500 - IP_HDR_LEN - UDP_HDR_LEN)  /* 1472 */

uint16_t net_ip_checksum(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;

    /* Sum consecutive 16-bit big-endian words. */
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | (uint32_t)p[1];
        p += 2;
        len -= 2;
    }
    /* Odd trailing byte (not hit for a 20-byte IP header) -> high byte. */
    if (len == 1)
        sum += (uint32_t)p[0] << 8;

    /* Fold 32-bit accumulator down to 16 bits. */
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum & 0xFFFF);
}

int net_build_udp_broadcast(uint8_t *out, uint32_t out_cap,
                            const uint8_t src_mac[6], uint32_t src_ip,
                            uint16_t src_port, uint16_t dst_port,
                            const void *payload, uint16_t payload_len)
{
    if (!out || !src_mac)
        return -1;
    if (payload_len > UDP_MAX_PAYLOAD)
        return -1;
    if (payload_len > 0 && !payload)
        return -1;

    uint32_t total     = HDRS_LEN + (uint32_t)payload_len;
    uint32_t frame_len = total < ETH_MIN_FRAME ? ETH_MIN_FRAME : total;
    if (out_cap < frame_len)
        return -1;

    /* --- Ethernet (14B) --- */
    uint8_t *eth = out;
    memset(eth, 0xFF, 6);              /* dst = broadcast */
    memcpy(eth + 6, src_mac, 6);       /* src */
    eth[12] = 0x08; eth[13] = 0x00;    /* ethertype = IPv4 */

    /* --- IPv4 (20B, no options) --- */
    uint8_t *ip = out + ETH_HDR_LEN;
    uint16_t ip_total = (uint16_t)(IP_HDR_LEN + UDP_HDR_LEN + payload_len);
    ip[0]  = 0x45;                                  /* version 4, IHL 5 (20B) */
    ip[1]  = 0x00;                                  /* DSCP/ECN */
    ip[2]  = (uint8_t)(ip_total >> 8);              /* total length (BE) */
    ip[3]  = (uint8_t)(ip_total & 0xFF);
    ip[4]  = 0x00; ip[5] = 0x00;                    /* identification */
    ip[6]  = 0x00; ip[7] = 0x00;                    /* flags + fragment offset */
    ip[8]  = IP_TTL;                                /* TTL */
    ip[9]  = IP_PROTO_UDP;                          /* protocol = UDP */
    ip[10] = 0x00; ip[11] = 0x00;                   /* header checksum (zero for compute) */
    ip[12] = (uint8_t)((src_ip >> 24) & 0xFF);      /* source IP (BE) */
    ip[13] = (uint8_t)((src_ip >> 16) & 0xFF);
    ip[14] = (uint8_t)((src_ip >> 8)  & 0xFF);
    ip[15] = (uint8_t)( src_ip        & 0xFF);
    ip[16] = 0xFF; ip[17] = 0xFF;                   /* dst = 255.255.255.255 */
    ip[18] = 0xFF; ip[19] = 0xFF;
    {
        uint16_t ck = net_ip_checksum(ip, IP_HDR_LEN);
        ip[10] = (uint8_t)(ck >> 8);
        ip[11] = (uint8_t)(ck & 0xFF);
    }

    /* --- UDP (8B) --- */
    uint8_t *udp = ip + IP_HDR_LEN;
    uint16_t udp_len = (uint16_t)(UDP_HDR_LEN + payload_len);
    udp[0] = (uint8_t)(src_port >> 8); udp[1] = (uint8_t)(src_port & 0xFF);
    udp[2] = (uint8_t)(dst_port >> 8); udp[3] = (uint8_t)(dst_port & 0xFF);
    udp[4] = (uint8_t)(udp_len  >> 8); udp[5] = (uint8_t)(udp_len  & 0xFF);
    udp[6] = 0x00; udp[7] = 0x00;                   /* UDP checksum disabled (legal for IPv4) */

    /* --- payload --- */
    if (payload_len > 0)
        memcpy(udp + UDP_HDR_LEN, payload, payload_len);

    /* --- zero-pad to the 60-byte Ethernet minimum (NIC appends the 4B FCS) --- */
    if (total < ETH_MIN_FRAME)
        memset(out + total, 0, (size_t)(ETH_MIN_FRAME - total));

    return (int)frame_len;
}
