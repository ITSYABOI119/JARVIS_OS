/*
 * test_control_parser.c — host tests for the hardened control-IN parser.
 *
 * Builds a valid Eth/IPv4/UDP/JCTL frame (with a correct IP header checksum),
 * proves CTRL_OK + correct extraction, then drives targeted negatives that hit
 * EACH control_status_t code.
 *
 * Compile+run:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/net \
 *     phase3/src/net/test_control_parser.c phase3/src/net/control_parser.c \
 *     -o /tmp/t_cp && /tmp/t_cp
 */

#include "control_parser.h"

#include <stdio.h>
#include <string.h>

static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { \
        printf("PASS: %s\n", (msg)); \
    } else { \
        printf("FAIL: %s\n", (msg)); \
        g_fail = 1; \
    } \
} while (0)

#define ETH 14u
#define IP 20u
#define UDP 8u

/* Local copy of the RFC1071 checksum for building test frames. */
static uint16_t t_ip_cksum(const uint8_t *p, size_t len)
{
    uint32_t sum = 0;
    while (len > 1) { sum += ((uint32_t)p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len == 1) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static void put_be64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i));
}

/*
 * Build a full frame. ihl_words>=5 (options zero-filled). query_len bytes of
 * query filler + 32-byte tag filler. Sets the standard non-fragment IP flags,
 * dst UDP port = CONTROL_PORT, and a correct IP checksum. Returns total length.
 * Callers may then mutate individual bytes for negative cases.
 */
static size_t build_frame(uint8_t *buf, size_t cap,
                          uint16_t query_len, uint8_t ihl_words)
{
    size_t ihl = (size_t)ihl_words * 4u;
    size_t msg_len = (size_t)CONTROL_HDR_LEN + query_len + CONTROL_TAG_LEN;
    size_t total = ETH + ihl + UDP + msg_len;
    if (total > cap) { fprintf(stderr, "build_frame overflow\n"); return 0; }
    memset(buf, 0, total);

    /* Ethernet */
    memset(buf, 0xFF, 6);            /* dst */
    memset(buf + 6, 0x11, 6);        /* src */
    put_be16(buf + 12, 0x0800);      /* IPv4 */

    /* IPv4 */
    uint8_t *ip = buf + ETH;
    ip[0] = (uint8_t)(0x40 | (ihl_words & 0x0F));   /* v4 + IHL */
    ip[1] = 0x00;
    put_be16(ip + 2, (uint16_t)(ihl + UDP + msg_len)); /* total_length */
    put_be16(ip + 4, 0x0000);                        /* id */
    put_be16(ip + 6, 0x0000);                        /* flags/frag: not fragmented */
    ip[8] = 64;                                      /* TTL */
    ip[9] = 17;                                      /* UDP */
    put_be16(ip + 10, 0x0000);                       /* checksum placeholder */
    put_be32(ip + 12, 0xC0A864A0);                   /* src 192.168.100.160 */
    put_be32(ip + 16, 0xC0A8648F);                   /* dst 192.168.100.143 */
    /* options (if ihl>5) already zeroed by memset */
    put_be16(ip + 10, t_ip_cksum(ip, ihl));

    /* UDP */
    uint8_t *udp = ip + ihl;
    put_be16(udp + 0, 40000);               /* src port */
    put_be16(udp + 2, (uint16_t)CONTROL_PORT);
    put_be16(udp + 4, (uint16_t)(UDP + msg_len));  /* udp length */
    put_be16(udp + 6, 0x0000);              /* udp cksum disabled */

    /* JCTL message */
    uint8_t *m = udp + UDP;
    put_be32(m + CTRL_OFF_MAGIC, CONTROL_MAGIC);
    m[CTRL_OFF_VERSION] = (uint8_t)CONTROL_VERSION;
    m[CTRL_OFF_FLAGS] = 0;
    put_be64(m + CTRL_OFF_SEQ, 0x0102030405060708ull);
    put_be32(m + CTRL_OFF_EPOCH, 0xDEADBEEFu);
    for (int i = 0; i < (int)CONTROL_NONCE_LEN; i++) m[CTRL_OFF_NONCE + i] = (uint8_t)(0xA0 + i);
    put_be16(m + CTRL_OFF_QLEN, query_len);
    for (uint16_t i = 0; i < query_len; i++) m[CTRL_OFF_QUERY + i] = (uint8_t)('a' + (i % 26));
    for (int i = 0; i < (int)CONTROL_TAG_LEN; i++) m[CTRL_OFF_QUERY + query_len + i] = (uint8_t)(0x50 + i);

    return total;
}

/* Recompute + patch the IP checksum after mutating IP header bytes. */
static void fix_ip_cksum(uint8_t *buf, uint8_t ihl_words)
{
    uint8_t *ip = buf + ETH;
    put_be16(ip + 10, 0x0000);
    put_be16(ip + 10, t_ip_cksum(ip, (size_t)ihl_words * 4u));
}

int main(void)
{
    uint8_t buf[512];
    const uint8_t *pl = NULL;
    size_t plen = 0;
    control_status_t st;
    control_msg_t msg;

    /* ---------- Positive: valid frame, IHL=5, query_len=10 ---------- */
    size_t n = build_frame(buf, sizeof buf, 10, 5);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_OK, "valid frame -> CTRL_OK");
    CHECK(plen == (size_t)CONTROL_HDR_LEN + 10 + CONTROL_TAG_LEN, "frame udp_len == msg len");
    CHECK(pl == buf + ETH + 20 + UDP, "udp_payload points past headers");

    st = control_parse_msg(pl, plen, &msg);
    CHECK(st == CTRL_OK, "valid msg -> CTRL_OK");
    CHECK(msg.version == CONTROL_VERSION, "msg version==1");
    CHECK(msg.flags == 0, "msg flags==0");
    CHECK(msg.seq == 0x0102030405060708ull, "msg seq extracted");
    CHECK(msg.boot_epoch == 0xDEADBEEFu, "msg boot_epoch extracted");
    CHECK(msg.query_len == 10, "msg query_len==10");
    CHECK(msg.nonce == pl + CTRL_OFF_NONCE, "nonce aliases payload");
    CHECK(msg.query == pl + CTRL_OFF_QUERY, "query aliases payload");
    CHECK(msg.tag == pl + CONTROL_HDR_LEN + 10, "tag aliases payload (after query)");
    CHECK(msg.mac_base == pl, "mac_base == payload start");
    CHECK(msg.mac_len == (size_t)CONTROL_HDR_LEN + 10, "mac_len == 36+query_len");
    CHECK(msg.nonce[0] == 0xA0 && msg.query[0] == 'a' && msg.tag[0] == 0x50,
          "aliased content matches built bytes");

    /* ---------- Positive: IHL=6 (one 4-byte option word) ---------- */
    n = build_frame(buf, sizeof buf, 0, 6);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_OK, "valid frame with IP options (IHL=6) -> CTRL_OK");
    CHECK(pl == buf + ETH + 24 + UDP, "udp_payload accounts for 24B IP header");
    st = control_parse_msg(pl, plen, &msg);
    CHECK(st == CTRL_OK && msg.query_len == 0, "min msg (query_len=0) over IHL=6 -> OK");

    /* ---------- Frame negatives ---------- */

    /* short frame len 13 (< 14) */
    n = build_frame(buf, sizeof buf, 10, 5);
    st = control_parse_frame(buf, 13, &pl, &plen);
    CHECK(st == CTRL_TOO_SHORT, "len 13 -> CTRL_TOO_SHORT");

    /* short IPv4 frames (14..33 = < 14 + 20-byte minimum IP header): must reject
     * as CTRL_TOO_SHORT WITHOUT reading past the frame (the len==14 ip[0] over-read
     * fix — the fuzz proves the no-OOB half under ASan; here we lock the verdict). */
    n = build_frame(buf, sizeof buf, 10, 5);
    st = control_parse_frame(buf, 14, &pl, &plen);
    CHECK(st == CTRL_TOO_SHORT, "len 14 (no IP byte) -> CTRL_TOO_SHORT");
    st = control_parse_frame(buf, 20, &pl, &plen);
    CHECK(st == CTRL_TOO_SHORT, "len 20 (6 IP bytes) -> CTRL_TOO_SHORT");
    st = control_parse_frame(buf, 33, &pl, &plen);
    CHECK(st == CTRL_TOO_SHORT, "len 33 (19 IP bytes, one short of min hdr) -> CTRL_TOO_SHORT");

    /* bad ethertype */
    n = build_frame(buf, sizeof buf, 10, 5);
    put_be16(buf + 12, 0x0806);   /* ARP */
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_BAD_ETHERTYPE, "ethertype 0x0806 -> CTRL_BAD_ETHERTYPE");

    /* not IPv4 (version nibble 6) */
    n = build_frame(buf, sizeof buf, 10, 5);
    buf[ETH] = (uint8_t)(0x60 | 0x05);
    fix_ip_cksum(buf, 5);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_NOT_IPV4, "IP version 6 -> CTRL_NOT_IPV4");

    /* IHL < 5 */
    n = build_frame(buf, sizeof buf, 10, 5);
    buf[ETH] = (uint8_t)(0x40 | 0x04);
    fix_ip_cksum(buf, 4);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_IP_IHL_BAD, "IHL 4 -> CTRL_IP_IHL_BAD");

    /* IHL claims more header than captured -> TOO_SHORT (after_eth < ihl) */
    n = build_frame(buf, sizeof buf, 10, 5);
    buf[ETH] = (uint8_t)(0x40 | 0x0F);   /* IHL=15 -> 60B header, capture too small */
    fix_ip_cksum(buf, 15);               /* not strictly needed; length check first */
    st = control_parse_frame(buf, ETH + 20, &pl, &plen);
    CHECK(st == CTRL_TOO_SHORT, "IHL exceeds captured -> CTRL_TOO_SHORT");

    /* bad IP checksum */
    n = build_frame(buf, sizeof buf, 10, 5);
    buf[ETH + 10] ^= 0xFF;   /* corrupt checksum field */
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_IP_CKSUM_BAD, "corrupt IP checksum -> CTRL_IP_CKSUM_BAD");

    /* fragment: MF flag set */
    n = build_frame(buf, sizeof buf, 10, 5);
    put_be16(buf + ETH + 6, 0x2000);   /* MF */
    fix_ip_cksum(buf, 5);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_IP_FRAGMENT, "MF flag set -> CTRL_IP_FRAGMENT");

    /* fragment: nonzero offset */
    n = build_frame(buf, sizeof buf, 10, 5);
    put_be16(buf + ETH + 6, 0x0025);   /* offset != 0 */
    fix_ip_cksum(buf, 5);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_IP_FRAGMENT, "fragment offset != 0 -> CTRL_IP_FRAGMENT");

    /* not UDP */
    n = build_frame(buf, sizeof buf, 10, 5);
    buf[ETH + 9] = 6;   /* TCP */
    fix_ip_cksum(buf, 5);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_NOT_UDP, "protocol TCP -> CTRL_NOT_UDP");

    /* IP total_length > captured -> IP_LEN_BAD */
    n = build_frame(buf, sizeof buf, 10, 5);
    put_be16(buf + ETH + 2, 2000);   /* claim 2000B but capture is ~n */
    fix_ip_cksum(buf, 5);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_IP_LEN_BAD, "total_length > captured -> CTRL_IP_LEN_BAD");

    /* IP total_length < ihl+8 -> IP_LEN_BAD */
    n = build_frame(buf, sizeof buf, 10, 5);
    put_be16(buf + ETH + 2, 20);   /* only the IP header, no UDP room; capture bigger */
    fix_ip_cksum(buf, 5);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_IP_LEN_BAD, "total_length < ihl+8 -> CTRL_IP_LEN_BAD");

    /* wrong dst port */
    n = build_frame(buf, sizeof buf, 10, 5);
    put_be16(buf + ETH + 20 + 2, 51000);   /* telemetry port, not control */
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_BAD_PORT, "wrong dst port -> CTRL_BAD_PORT");

    /* UDP length < 8 */
    n = build_frame(buf, sizeof buf, 10, 5);
    put_be16(buf + ETH + 20 + 4, 4);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_UDP_LEN_BAD, "udp_length 4 -> CTRL_UDP_LEN_BAD");

    /* UDP length overruns IP total_length -> UDP_LEN_BAD */
    n = build_frame(buf, sizeof buf, 10, 5);
    put_be16(buf + ETH + 20 + 4, 1000);   /* > total_length - ihl */
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_UDP_LEN_BAD, "udp_length overruns IP window -> CTRL_UDP_LEN_BAD");

    /* ---------- Message negatives ---------- */

    /* short payload len 67 (< 68) */
    n = build_frame(buf, sizeof buf, 10, 5);
    control_parse_frame(buf, n, &pl, &plen);
    st = control_parse_msg(pl, 67, &msg);
    CHECK(st == CTRL_TOO_SHORT, "msg len 67 -> CTRL_TOO_SHORT");

    /* bad magic */
    n = build_frame(buf, sizeof buf, 0, 5);
    control_parse_frame(buf, n, &pl, &plen);
    ((uint8_t *)pl)[CTRL_OFF_MAGIC] ^= 0xFF;
    st = control_parse_msg(pl, plen, &msg);
    CHECK(st == CTRL_BAD_MAGIC, "bad magic -> CTRL_BAD_MAGIC");

    /* bad version */
    n = build_frame(buf, sizeof buf, 0, 5);
    control_parse_frame(buf, n, &pl, &plen);
    ((uint8_t *)pl)[CTRL_OFF_VERSION] = 2;
    st = control_parse_msg(pl, plen, &msg);
    CHECK(st == CTRL_BAD_VERSION, "version 2 -> CTRL_BAD_VERSION");

    /* bad flags */
    n = build_frame(buf, sizeof buf, 0, 5);
    control_parse_frame(buf, n, &pl, &plen);
    ((uint8_t *)pl)[CTRL_OFF_FLAGS] = 1;
    st = control_parse_msg(pl, plen, &msg);
    CHECK(st == CTRL_BAD_FLAGS, "flags 1 -> CTRL_BAD_FLAGS");

    /* query_len 173 (> 172). Build a big enough frame so only qlen is at fault
     * (LEN_MISMATCH is checked AFTER query_len, so make len match 36+173+32). */
    n = build_frame(buf, sizeof buf, 173, 5);
    st = control_parse_frame(buf, n, &pl, &plen);
    CHECK(st == CTRL_OK, "frame carrying qlen=173 parses at frame stage");
    st = control_parse_msg(pl, plen, &msg);
    CHECK(st == CTRL_QUERY_TOO_LONG, "query_len 173 -> CTRL_QUERY_TOO_LONG");

    /* LEN_MISMATCH: msg 1 byte short (declared qlen 10, payload = 36+10+32-1). */
    n = build_frame(buf, sizeof buf, 10, 5);
    control_parse_frame(buf, n, &pl, &plen);
    st = control_parse_msg(pl, plen - 1, &msg);
    CHECK(st == CTRL_LEN_MISMATCH, "msg 1 byte short -> CTRL_LEN_MISMATCH");

    /* LEN_MISMATCH: msg 1 byte long. plen must still be a valid slice of buf;
     * build_frame(query_len=11) gives a payload one byte longer, but declare
     * qlen=10 by patching the qlen field. */
    n = build_frame(buf, sizeof buf, 11, 5);
    control_parse_frame(buf, n, &pl, &plen);
    put_be16((uint8_t *)pl + CTRL_OFF_QLEN, 10);   /* declare 10, payload is 36+11+32 */
    st = control_parse_msg(pl, plen, &msg);
    CHECK(st == CTRL_LEN_MISMATCH, "msg 1 byte long -> CTRL_LEN_MISMATCH");

    /* ---------- summary ---------- */
    if (g_fail) {
        printf("\nRESULT: FAIL\n");
        return 1;
    }
    printf("\nRESULT: ALL PASS\n");
    return 0;
}
