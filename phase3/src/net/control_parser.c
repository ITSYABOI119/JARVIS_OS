/*
 * control_parser.c — hardened, copy-free inbound control parser (Phase 6 6-5).
 *
 * See control_parser.h for the contract. Design rules held throughout:
 *   - NEVER read past data[len-1]. Every bound is computed in size_t with a
 *     pre-subtraction form (a >= b + c  =>  a - b >= c, guarding overflow by
 *     only subtracting quantities already proven <= the running total).
 *   - No allocation, no memcpy, no reassembly — outputs alias the input.
 *   - Cheapest checks first; fragments rejected before any UDP/HMAC work.
 */

#include "control_parser.h"

#define ETH_HDR_LEN   14u
#define ETHERTYPE_IPV4 0x0800u
#define IP_PROTO_UDP  17u
#define IP_MIN_HDR    20u   /* minimum IPv4 header (IHL=5); every fixed IP field lives in [0,20) */

/*
 * Inlined RFC1071 ones-complement IPv4 header checksum. Sums 16-bit big-endian
 * words, folds carries, returns ~sum. Bit-for-bit equivalent to net_udp.c's
 * net_ip_checksum() (kept local so this module has no link dependency).
 *
 * A correct header (checksum field included) sums to 0x0000 here.
 */
static uint16_t ip_checksum(const uint8_t *p, size_t len)
{
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | (uint32_t)p[1];
        p += 2;
        len -= 2;
    }
    if (len == 1)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

control_status_t control_parse_frame(const uint8_t *data, size_t len,
                                     const uint8_t **udp_payload,
                                     size_t *udp_len)
{
    if (!data || !udp_payload || !udp_len)
        return CTRL_TOO_SHORT;

    /* --- Ethernet (14B) --- */
    if (len < ETH_HDR_LEN)
        return CTRL_TOO_SHORT;

    uint16_t ethertype = ctrl_be16(data + 12);
    if (ethertype != ETHERTYPE_IPV4)
        return CTRL_BAD_ETHERTYPE;

    /* --- IPv4 fixed prefix --- */
    const uint8_t *ip = data + ETH_HDR_LEN;
    size_t after_eth = len - ETH_HDR_LEN;        /* bytes available from `ip` */

    /* Require the full 20-byte minimum IPv4 header BEFORE reading any IP field.
     * ip[0] (version/IHL), ip[2] (total_length), ip[6] (frag), ip[9] (proto) all
     * live in [0,20); with only len >= 14 a 14..33-byte frame would read past
     * data[len-1] (e.g. len==14 => after_eth==0 => ip[0] is 1 byte OOB). A valid
     * IPv4 header is always >= 20 bytes, so this rejects nothing legitimate. */
    if (after_eth < IP_MIN_HDR)
        return CTRL_TOO_SHORT;

    uint8_t ver = (uint8_t)(ip[0] >> 4);
    if (ver != 4)
        return CTRL_NOT_IPV4;

    uint8_t ihl_words = (uint8_t)(ip[0] & 0x0F);
    if (ihl_words < 5)
        return CTRL_IP_IHL_BAD;
    size_t ihl = (size_t)ihl_words * 4u;         /* IP header length in bytes */

    /* Need the full IP header (incl. options) captured. */
    if (after_eth < ihl)
        return CTRL_TOO_SHORT;

    /* Verify the IPv4 header checksum over exactly `ihl` bytes. */
    if (ip_checksum(ip, ihl) != 0)
        return CTRL_IP_CKSUM_BAD;

    /* Fragment reject: MF flag set (0x2000) OR fragment offset != 0 (0x1FFF). */
    uint16_t frag = ctrl_be16(ip + 6);
    if ((frag & 0x2000u) || (frag & 0x1FFFu))
        return CTRL_IP_FRAGMENT;

    if (ip[9] != IP_PROTO_UDP)
        return CTRL_NOT_UDP;

    /* total_length must fit inside the captured bytes and cover IP hdr + UDP hdr. */
    uint16_t total_length = ctrl_be16(ip + 2);
    if (after_eth < (size_t)total_length)        /* captured IP payload too short */
        return CTRL_IP_LEN_BAD;
    if ((size_t)total_length < ihl + 8u)         /* no room for a UDP header */
        return CTRL_IP_LEN_BAD;

    /* --- UDP (8B) --- */
    const uint8_t *udp = ip + ihl;               /* within capture: ihl <= after_eth */

    uint16_t dst_port = ctrl_be16(udp + 2);
    if (dst_port != (uint16_t)CONTROL_PORT)
        return CTRL_BAD_PORT;

    uint16_t udp_length = ctrl_be16(udp + 4);
    if (udp_length < 8u)
        return CTRL_UDP_LEN_BAD;
    /* The UDP datagram (from `udp`) must fit inside the IP total_length window:
     * ihl + udp_length <= total_length. Rearranged to avoid overflow: both
     * ihl and udp_length are already bounded (<= total_length individually is
     * not guaranteed, so subtract the proven-small ihl from total_length). */
    if ((size_t)udp_length > (size_t)total_length - ihl)
        return CTRL_UDP_LEN_BAD;

    *udp_payload = udp + 8u;
    *udp_len = (size_t)udp_length - 8u;
    return CTRL_OK;
}

control_status_t control_parse_msg(const uint8_t *payload, size_t len,
                                   control_msg_t *out)
{
    if (!payload || !out)
        return CTRL_TOO_SHORT;

    if (len < CONTROL_MSG_MIN)                   /* 68 = 36 hdr + 32 tag */
        return CTRL_TOO_SHORT;

    if (ctrl_be32(payload + CTRL_OFF_MAGIC) != CONTROL_MAGIC)
        return CTRL_BAD_MAGIC;

    uint8_t version = payload[CTRL_OFF_VERSION];
    if (version != CONTROL_VERSION)
        return CTRL_BAD_VERSION;

    uint8_t flags = payload[CTRL_OFF_FLAGS];
    if (flags != 0)
        return CTRL_BAD_FLAGS;

    uint16_t query_len = ctrl_be16(payload + CTRL_OFF_QLEN);
    if (query_len > CONTROL_QUERY_MAX)           /* 172 */
        return CTRL_QUERY_TOO_LONG;

    /* EXACT length: 36 + query_len + 32 must equal len (no trailing, no short).
     * query_len <= 172 so the sum cannot overflow size_t. */
    if ((size_t)CONTROL_HDR_LEN + (size_t)query_len + (size_t)CONTROL_TAG_LEN != len)
        return CTRL_LEN_MISMATCH;

    out->version    = version;
    out->flags      = flags;
    out->seq        = ctrl_be64(payload + CTRL_OFF_SEQ);
    out->boot_epoch = ctrl_be32(payload + CTRL_OFF_EPOCH);
    out->nonce      = payload + CTRL_OFF_NONCE;
    out->query      = payload + CTRL_OFF_QUERY;
    out->query_len  = query_len;
    out->tag        = payload + CONTROL_HDR_LEN + query_len;
    out->mac_base   = payload;
    out->mac_len    = (size_t)CONTROL_HDR_LEN + (size_t)query_len;
    return CTRL_OK;
}
