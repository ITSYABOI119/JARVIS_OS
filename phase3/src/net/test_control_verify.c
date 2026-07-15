/*
 * test_control_verify.c — host test for the control-IN orchestrator (6-5).
 *
 * Builds a REAL Ethernet/IPv4/UDP frame carrying a REAL JCTL message signed
 * with a REAL HMAC-SHA256 over the canonical span, then drives control_verify()
 * for the accept path and one-field mutations for each drop path. Also proves
 * the hard invariant: a bad-MAC frame does NOT advance replay->seq_floor.
 *
 * Build/run:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/net -I phase3/src/crypto \
 *       phase3/src/net/test_control_verify.c phase3/src/net/control_verify.c \
 *       phase3/src/net/control_parser.c phase3/src/net/control_replay.c \
 *       phase3/src/net/control_ratelimit.c phase3/src/crypto/hmac_sha256.c \
 *       phase3/src/crypto/sha256.c -o /tmp/t_cv && /tmp/t_cv
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "control_verify.h"
#include "control_msg.h"
#include "hmac_sha256.h"

/* ---- test bookkeeping ---- */
static int g_pass = 0;
static int g_fail = 0;
static void check(int cond, const char *what)
{
    if (cond) { g_pass++; }
    else { g_fail++; printf("FAIL: %s\n", what); }
}

/* ---- shared test key ---- */
static const uint8_t KEY[32] = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
    0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
    0x0F,0x1E,0x2D,0x3C,0x4B,0x5A,0x69,0x78,
    0x87,0x96,0xA5,0xB4,0xC3,0xD2,0xE1,0xF0
};

/* Big-endian store helpers. */
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

/* RFC1071 ones-complement IPv4 header checksum (matches control_parser.c). */
static uint16_t ip_cksum(const uint8_t *p, size_t len)
{
    uint32_t sum = 0;
    while (len > 1) { sum += ((uint32_t)p[0] << 8) | (uint32_t)p[1]; p += 2; len -= 2; }
    if (len == 1) sum += (uint32_t)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFF);
}

/*
 * Build a full frame: Eth(14) + IPv4(20) + UDP(8) + JCTL message.
 * The JCTL message is 36-byte header + query + 32-byte HMAC tag.
 *
 *   frag_flags : written into the IPv4 flags/frag-offset field (0 = not a
 *                fragment; 0x2000 sets MF to exercise the fragment reject).
 *   corrupt_tag: if non-zero, flips one bit of the tag AFTER signing (bad MAC).
 *
 * Returns total frame length; frame[] must be >= 300 bytes.
 */
static size_t build_frame(uint8_t *frame,
                          uint64_t seq, uint32_t epoch,
                          const uint8_t nonce[CONTROL_NONCE_LEN],
                          const uint8_t *query, uint16_t qlen,
                          uint16_t frag_flags, int corrupt_tag)
{
    /* --- JCTL message (built in a scratch, then copied into the UDP payload) --- */
    uint8_t msg[CONTROL_MSG_MAX];
    size_t msg_len = (size_t)CONTROL_HDR_LEN + qlen + CONTROL_TAG_LEN;

    put_be32(msg + CTRL_OFF_MAGIC, CONTROL_MAGIC);
    msg[CTRL_OFF_VERSION] = (uint8_t)CONTROL_VERSION;
    msg[CTRL_OFF_FLAGS]   = 0;
    put_be64(msg + CTRL_OFF_SEQ, seq);
    put_be32(msg + CTRL_OFF_EPOCH, epoch);
    memcpy(msg + CTRL_OFF_NONCE, nonce, CONTROL_NONCE_LEN);
    put_be16(msg + CTRL_OFF_QLEN, qlen);
    if (qlen) memcpy(msg + CTRL_OFF_QUERY, query, qlen);

    /* Real HMAC over the canonical span: header + query (all but the tag). */
    uint8_t tag[32];
    hmac_sha256(KEY, sizeof(KEY), msg, (size_t)CONTROL_HDR_LEN + qlen, tag);
    memcpy(msg + CONTROL_HDR_LEN + qlen, tag, CONTROL_TAG_LEN);
    if (corrupt_tag)
        msg[CONTROL_HDR_LEN + qlen] ^= 0x01;  /* flip one tag bit */

    /* --- assemble the frame --- */
    uint8_t *eth = frame;
    memset(eth, 0, 14);
    eth[0] = 0x02; eth[6] = 0x02;             /* arbitrary locally-administered MACs */
    put_be16(eth + 12, 0x0800);               /* EtherType IPv4 */

    uint8_t *ip = frame + 14;
    uint16_t total_len = (uint16_t)(20u + 8u + msg_len);
    ip[0] = 0x45;                             /* ver 4, ihl 5 */
    ip[1] = 0x00;
    put_be16(ip + 2, total_len);
    put_be16(ip + 4, 0x1234);                 /* id */
    put_be16(ip + 6, frag_flags);             /* flags / fragment offset */
    ip[8] = 64;                               /* ttl */
    ip[9] = 17;                               /* proto UDP */
    put_be16(ip + 10, 0);                     /* checksum placeholder */
    put_be32(ip + 12, 0xC0A864C0);            /* src 192.168.100.192 */
    put_be32(ip + 16, 0xC0A8648F);            /* dst 192.168.100.143 */
    put_be16(ip + 10, ip_cksum(ip, 20));      /* real header checksum */

    uint8_t *udp = frame + 34;
    put_be16(udp + 0, 40000);                 /* src port */
    put_be16(udp + 2, (uint16_t)CONTROL_PORT);/* dst port 51001 */
    put_be16(udp + 4, (uint16_t)(8u + msg_len));
    put_be16(udp + 6, 0);                     /* UDP checksum 0 (allowed for IPv4) */

    memcpy(frame + 42, msg, msg_len);
    return 42u + msg_len;
}

int main(void)
{
    const uint32_t EPOCH = 7;
    const uint8_t query[] = "what is my uptime";
    const uint16_t qlen = (uint16_t)(sizeof(query) - 1);
    uint8_t nonce[CONTROL_NONCE_LEN];
    for (int i = 0; i < CONTROL_NONCE_LEN; i++) nonce[i] = (uint8_t)(0xA0 + i);

    uint8_t frame[300];
    control_result_t res;

    /* ---- Test 1: valid frame -> CV_ACCEPT + exact query bytes ---- */
    {
        control_replay_t rp; control_replay_init(&rp, EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        size_t flen = build_frame(frame, 5, EPOCH, nonce, query, qlen, 0, 0);
        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_ACCEPT, "valid -> CV_ACCEPT");
        check(res.verdict == CV_ACCEPT, "valid -> out.verdict CV_ACCEPT");
        check(res.query != NULL && res.query_len == qlen, "valid -> query_len exact");
        check(res.query && memcmp(res.query, query, qlen) == 0, "valid -> query bytes exact");
        check(rp.seq_floor == 5, "valid -> seq_floor advanced to 5");
    }

    /* ---- Test 2: bad MAC -> CV_DROP_AUTH, and seq_floor NOT advanced ---- */
    {
        control_replay_t rp; control_replay_init(&rp, EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        uint64_t floor_before = rp.seq_floor;
        size_t flen = build_frame(frame, 5, EPOCH, nonce, query, qlen, 0, 1 /*corrupt*/);
        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_AUTH, "bad-MAC -> CV_DROP_AUTH");
        check(res.query == NULL && res.query_len == 0, "bad-MAC -> silent (no query)");
        check(rp.seq_floor == floor_before, "bad-MAC -> seq_floor UNCHANGED (invariant)");
    }

    /* ---- Test 3: replayed seq -> CV_DROP_REPLAY ---- */
    {
        control_replay_t rp; control_replay_init(&rp, EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        size_t flen = build_frame(frame, 9, EPOCH, nonce, query, qlen, 0, 0);
        control_verdict_t v1 = control_verify(frame, flen, KEY, sizeof(KEY),
                                              &rp, &rl, 1000, &res);
        check(v1 == CV_ACCEPT, "replay-setup -> first accepted");
        /* re-send the SAME seq=9 (<= floor now 9) */
        size_t flen2 = build_frame(frame, 9, EPOCH, nonce, query, qlen, 0, 0);
        control_verdict_t v2 = control_verify(frame, flen2, KEY, sizeof(KEY),
                                              &rp, &rl, 1000, &res);
        check(v2 == CV_DROP_REPLAY, "replayed-seq -> CV_DROP_REPLAY");
        check(res.replay_verdict == CR_REJECT_SEQ, "replayed-seq -> CR_REJECT_SEQ diag");
        check(res.query == NULL, "replayed-seq -> silent");
    }

    /* ---- Test 4: stale/wrong epoch -> CV_DROP_REPLAY ---- */
    {
        control_replay_t rp; control_replay_init(&rp, EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        size_t flen = build_frame(frame, 5, EPOCH + 1 /*wrong*/, nonce, query, qlen, 0, 0);
        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_REPLAY, "stale-epoch -> CV_DROP_REPLAY");
        check(res.replay_verdict == CR_REJECT_EPOCH, "stale-epoch -> CR_REJECT_EPOCH diag");
        check(rp.seq_floor == 0, "stale-epoch -> seq_floor untouched");
    }

    /* ---- Test 5: IP fragment -> CV_DROP_PARSE ---- */
    {
        control_replay_t rp; control_replay_init(&rp, EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        size_t flen = build_frame(frame, 5, EPOCH, nonce, query, qlen, 0x2000 /*MF*/, 0);
        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_PARSE, "IP-fragment -> CV_DROP_PARSE");
        check(res.parse_status == CTRL_IP_FRAGMENT, "IP-fragment -> CTRL_IP_FRAGMENT diag");
        check(res.query == NULL, "IP-fragment -> silent");
        /* fragment is rejected BEFORE ratelimit: bucket must be untouched (full=8). */
        check(rl.tokens_milli == CONTROL_RL_CAPACITY * 1000u,
              "IP-fragment -> ratelimit NOT consumed (parse is first)");
    }

    /* ---- Test 6: flood -> exhaust the bucket -> CV_DROP_RATELIMIT ---- */
    {
        control_replay_t rp; control_replay_init(&rp, EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000); /* full = 8 tokens */
        control_verdict_t last = CV_ACCEPT;
        /* CONTROL_RL_CAPACITY passes consume all tokens (same now_ms => no refill). */
        for (uint32_t i = 0; i < CONTROL_RL_CAPACITY; i++) {
            size_t flen = build_frame(frame, 100 + i, EPOCH, nonce, query, qlen, 0, 0);
            last = control_verify(frame, flen, KEY, sizeof(KEY), &rp, &rl, 1000, &res);
        }
        check(last != CV_DROP_RATELIMIT, "flood-setup -> first CAP frames not yet rate-limited");
        /* The next call has no token left. */
        size_t flen = build_frame(frame, 200, EPOCH, nonce, query, qlen, 0, 0);
        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_RATELIMIT, "flood -> CV_DROP_RATELIMIT");
        check(res.query == NULL, "flood -> silent");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
