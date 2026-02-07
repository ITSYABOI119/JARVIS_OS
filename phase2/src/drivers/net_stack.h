/*
 * JARVIS AI-OS - Basic Networking Stack
 *
 * Week 38: Minimal ARP + ICMP echo for bare-metal seL4 on Pi 4.
 * No heap allocation - all buffers passed by caller.
 */

#ifndef NET_STACK_H
#define NET_STACK_H

#include <stdint.h>
#include <stdbool.h>

/* Ethernet */
#define ETH_ALEN            6
#define ETH_HLEN            14
#define ETH_P_ARP           0x0806
#define ETH_P_IP            0x0800

/* IP */
#define IP_PROTO_ICMP       1
#define IP_HDR_MIN_LEN      20

/* ICMP */
#define ICMP_ECHO_REQUEST   8
#define ICMP_ECHO_REPLY     0

/* Max frame size */
#define NET_MAX_FRAME       1536

/* ================================================================
 * Structures (packed, network byte order in wire format)
 * ================================================================ */

typedef struct {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;     /* big-endian */
} __attribute__((packed)) net_eth_hdr_t;

typedef struct {
    uint16_t htype;         /* Hardware type (1 = Ethernet) */
    uint16_t ptype;         /* Protocol type (0x0800 = IPv4) */
    uint8_t  hlen;          /* Hardware addr len (6) */
    uint8_t  plen;          /* Protocol addr len (4) */
    uint16_t oper;          /* 1=request, 2=reply */
    uint8_t  sha[ETH_ALEN]; /* Sender hardware address */
    uint8_t  spa[4];        /* Sender protocol address */
    uint8_t  tha[ETH_ALEN]; /* Target hardware address */
    uint8_t  tpa[4];        /* Target protocol address */
} __attribute__((packed)) net_arp_t;

typedef struct {
    uint8_t  ver_ihl;       /* Version (4 bits) + IHL (4 bits) */
    uint8_t  dscp_ecn;
    uint16_t total_len;     /* big-endian */
    uint16_t ident;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;      /* big-endian */
    uint8_t  src_ip[4];
    uint8_t  dst_ip[4];
} __attribute__((packed)) net_ip_hdr_t;

typedef struct {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;      /* big-endian */
    uint16_t ident;         /* big-endian */
    uint16_t seq;           /* big-endian */
} __attribute__((packed)) net_icmp_echo_t;

/* ================================================================
 * Public API
 * ================================================================ */

/* Initialize networking stack with our IP and MAC */
void net_init(uint32_t ip_be, const uint8_t mac[ETH_ALEN]);

/* Set/update our IP (network byte order) */
void net_set_ip(uint32_t ip_be);

/* Set/update our MAC */
void net_set_mac(const uint8_t mac[ETH_ALEN]);

/* Process an incoming Ethernet frame.
 * If a reply is needed (ARP reply, ICMP echo reply), builds it in reply_buf
 * and sets *reply_len. Returns true if reply was generated. */
bool net_process_frame(const uint8_t *frame, uint32_t len,
                       uint8_t *reply_buf, uint32_t *reply_len);

/* Compute IP/ICMP checksum (ones-complement sum) */
uint16_t net_checksum(const void *data, uint32_t len);

#endif /* NET_STACK_H */
