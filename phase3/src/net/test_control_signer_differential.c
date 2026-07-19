/*
 * test_control_signer_differential.c — CROSS-LANGUAGE differential harness
 * (Phase 6 goal 6-5 / M3-4b).
 *
 * ── WHAT THIS PROVES (and why it is stronger than the Python test alone) ─────
 *
 * M3-4b makes telemetry_receiver.py the SIGNER: the browser can hold neither the
 * HMAC key nor a raw L2 socket, so the receiver builds and signs the JCTL frame
 * that goes to the box. The Python side (test_telemetry_receiver.py) proves that
 * frame is SELF-CONSISTENT — right offsets, independently recomputed tag. That is
 * a Python-only claim: it cannot prove the REAL C verifier running on the box
 * accepts those exact bytes.
 *
 * This test closes that gap. It embeds the LITERAL frame bytes emitted by the
 * committed Python signer and feeds them to the REAL control_verify() — the same
 * translation unit the box runs (main_x86.c -> pa_verify_candidate). An ACCEPT
 * here is end-to-end evidence that browser -> receiver -> box will authenticate,
 * gathered before the JARVIS_CONTROL_IN flip and without touching the box.
 *
 * NOTHING here is box code: this is a host test over already-committed modules.
 *
 * ── THE CROSS-LANGUAGE COUPLING (read this before editing either side) ───────
 *
 * GOLDEN_JCTL[] below is byte-identical to the `GOLDEN` hex string pinned in
 *   phase3/scripts/test_telemetry_receiver.py
 * (section "6-5/M3-4b: control-IN signing + /send + reply decode", case (a)),
 * which is itself the output of build_control_frame() in
 *   phase3/scripts/telemetry_receiver.py
 * for the fixed vector:
 *
 *     key   = bytes(range(1, 33))          -> 0x01..0x20 (the box JKEY test slot)
 *     seq   = 1700000000123
 *     epoch = CONTROL_TEST_EPOCH = 0x4A320001
 *     query = "status"
 *     nonce = bytes(range(0x10, 0x20))     -> 0x10..0x1F (fixed => deterministic)
 *
 * THESE TWO CONSTANTS MUST STAY IDENTICAL. If the Python signer's framing ever
 * changes, its own golden assert fails first; if only this array is edited, T1
 * still passes but the differential claim is void. Change one => change both,
 * and re-verify with:
 *     py -3 -c "print(open(...).read())"  # or simply re-run both test suites
 * The two suites are the enforcement: Python pins the hex, C pins the array, and
 * T0 below re-derives the tag with the C HMAC so a silent drift in EITHER the
 * bytes or the crypto shows up as a FAIL rather than a false PASS.
 *
 * Build/run:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/net -I phase3/src/crypto \
 *       phase3/src/net/test_control_signer_differential.c \
 *       phase3/src/net/control_verify.c phase3/src/net/control_parser.c \
 *       phase3/src/net/control_replay.c phase3/src/net/control_ratelimit.c \
 *       phase3/src/crypto/hmac_sha256.c phase3/src/crypto/sha256.c \
 *       -o /tmp/t_sigdiff && /tmp/t_sigdiff
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "control_verify.h"
#include "control_msg.h"
#include "hmac_sha256.h"

/* ---- test bookkeeping (house style: PASS/FAIL per assert + a tally) ---- */
static int g_pass = 0;
static int g_fail = 0;
static void check(int cond, const char *what)
{
    if (cond) { g_pass++; printf("PASS: %s\n", what); }
    else      { g_fail++; printf("FAIL: %s\n", what); }
}

/*
 * The signing key the Python golden used: bytes(range(1, 33)) == 0x01..0x20.
 * This is the fixed TEST key (the real box key lives in the NVMe JKEY slot).
 */
static const uint8_t KEY[32] = {
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
    0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,
    0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
    0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20
};

/*
 * VERBATIM output of the committed Python signer for the vector documented in
 * the file header. 74 bytes = 36 (header) + 6 ("status") + 32 (HMAC tag).
 * Byte-identical to the `GOLDEN` hex in test_telemetry_receiver.py.
 */
static const uint8_t GOLDEN_JCTL[] = {
    0x4A,0x43,0x54,0x4C,                                 /* magic  "JCTL"    */
    0x01,                                                /* version 1        */
    0x00,                                                /* flags   0        */
    0x00,0x00,0x01,0x8B,0xCF,0xE5,0x68,0x7B,             /* seq (BE u64)     */
    0x4A,0x32,0x00,0x01,                                 /* epoch (BE u32)   */
    0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,             /* nonce[0..7]      */
    0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,             /* nonce[8..15]     */
    0x00,0x06,                                           /* query_len (BE)   */
    0x73,0x74,0x61,0x74,0x75,0x73,                       /* "status"         */
    0x55,0xC3,0x94,0x85,0x81,0xFD,0x85,0x80,             /* tag[0..7]        */
    0xCD,0x1F,0xAD,0x22,0x2E,0x8E,0x7E,0x37,
    0x0B,0x00,0x58,0x62,0x70,0xCF,0x8F,0xFD,
    0xD8,0x02,0x72,0x27,0x21,0xE7,0xD1,0x3D
};
#define GOLDEN_LEN   (sizeof(GOLDEN_JCTL))
#define GOLDEN_SEQ   1700000000123ULL
#define GOLDEN_EPOCH 0x4A320001u
#define GOLDEN_QUERY "status"
#define GOLDEN_QLEN  6u

/* Big-endian store helpers (same idiom as test_control_verify.c). */
static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
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
 * Wrap an OPAQUE UDP payload in Ethernet(14) + IPv4(20) + UDP(8), dport 51001 —
 * exactly the framing test_control_verify.c builds, except the payload is passed
 * in rather than synthesized, so the Python bytes travel through untouched.
 * Returns the total frame length; frame[] must be >= 42 + msg_len bytes.
 */
static size_t wrap_frame(uint8_t *frame, const uint8_t *msg, size_t msg_len)
{
    uint8_t *eth = frame;
    memset(eth, 0, 14);
    eth[0] = 0x02; eth[6] = 0x02;              /* locally-administered MACs */
    put_be16(eth + 12, 0x0800);                /* EtherType IPv4 */

    uint8_t *ip = frame + 14;
    uint16_t total_len = (uint16_t)(20u + 8u + msg_len);
    ip[0] = 0x45;                              /* ver 4, ihl 5 */
    ip[1] = 0x00;
    put_be16(ip + 2, total_len);
    put_be16(ip + 4, 0x1234);                  /* id */
    put_be16(ip + 6, 0);                       /* not a fragment */
    ip[8] = 64;                                /* ttl */
    ip[9] = 17;                                /* proto UDP */
    put_be16(ip + 10, 0);                      /* checksum placeholder */
    put_be32(ip + 12, 0xC0A86492);             /* src 192.168.100.146 (the console) */
    put_be32(ip + 16, 0xC0A8648F);             /* dst 192.168.100.143 (the box)     */
    put_be16(ip + 10, ip_cksum(ip, 20));

    uint8_t *udp = frame + 34;
    put_be16(udp + 0, 51002);                  /* src port (the console reply port) */
    put_be16(udp + 2, (uint16_t)CONTROL_PORT); /* dst port 51001 */
    put_be16(udp + 4, (uint16_t)(8u + msg_len));
    put_be16(udp + 6, 0);                      /* UDP checksum 0 (allowed for IPv4) */

    memcpy(frame + 42, msg, msg_len);
    return 42u + msg_len;
}

int main(void)
{
    uint8_t frame[300];
    control_result_t res;

    printf("== 6-5/M3-4b signer differential: committed Python frame -> real C control_verify ==\n\n");

    /* ---- T0: the embedded golden is structurally what control_msg.h defines ---- */
    {
        check(GOLDEN_LEN == (size_t)CONTROL_HDR_LEN + GOLDEN_QLEN + CONTROL_TAG_LEN,
              "T0 golden len == CONTROL_HDR_LEN + qlen + CONTROL_TAG_LEN (74)");
        check(ctrl_be32(GOLDEN_JCTL + CTRL_OFF_MAGIC) == CONTROL_MAGIC,
              "T0 magic@0 == CONTROL_MAGIC 'JCTL' (Python used the C offsets)");
        check(GOLDEN_JCTL[CTRL_OFF_VERSION] == CONTROL_VERSION, "T0 version@4 == CONTROL_VERSION");
        check(GOLDEN_JCTL[CTRL_OFF_FLAGS] == 0, "T0 flags@5 == 0 (reserved, MUST be 0)");
        check(ctrl_be64(GOLDEN_JCTL + CTRL_OFF_SEQ) == GOLDEN_SEQ, "T0 seq@6 (BE u64) decodes");
        check(ctrl_be32(GOLDEN_JCTL + CTRL_OFF_EPOCH) == GOLDEN_EPOCH,
              "T0 epoch@14 (BE u32) == CONTROL_TEST_EPOCH 0x4A320001");
        check(ctrl_be16(GOLDEN_JCTL + CTRL_OFF_QLEN) == GOLDEN_QLEN, "T0 query_len@34 (BE u16) == 6");
        check(memcmp(GOLDEN_JCTL + CTRL_OFF_QUERY, GOLDEN_QUERY, GOLDEN_QLEN) == 0,
              "T0 query@36 == \"status\"");

        /* THE cross-language crypto identity: the C HMAC over the canonical span
         * reproduces the tag Python appended. If either side's key schedule,
         * span, or byte order drifted, this is where it surfaces. */
        uint8_t tag[32];
        hmac_sha256(KEY, sizeof(KEY), GOLDEN_JCTL,
                    (size_t)CONTROL_HDR_LEN + GOLDEN_QLEN, tag);
        check(memcmp(tag, GOLDEN_JCTL + CONTROL_HDR_LEN + GOLDEN_QLEN, CONTROL_TAG_LEN) == 0,
              "T0 C hmac_sha256(key, payload[0:36+qlen]) == the tag Python signed");
    }

    /* ---- T1: THE differential — the real verifier ACCEPTS the Python frame ---- */
    {
        control_replay_t rp; control_replay_init(&rp, GOLDEN_EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        size_t flen = wrap_frame(frame, GOLDEN_JCTL, GOLDEN_LEN);

        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_ACCEPT, "T1 committed Python frame -> CV_ACCEPT (the differential proof)");
        check(res.parse_status == CTRL_OK, "T1 parse_status == CTRL_OK");
        check(res.seq == GOLDEN_SEQ, "T1 authenticated seq == what Python encoded");
        check(res.boot_epoch == GOLDEN_EPOCH, "T1 authenticated epoch == what Python encoded");
        check(res.query_len == GOLDEN_QLEN, "T1 query_len == what Python encoded");
        check(res.query != NULL && memcmp(res.query, GOLDEN_QUERY, GOLDEN_QLEN) == 0,
              "T1 query bytes == \"status\" (exact, no mangling)");
        check(rp.seq_floor == GOLDEN_SEQ, "T1 replay floor advanced to the authenticated seq");
    }

    /* ---- T2 (teeth): one flipped tag byte -> AUTH drop, floor NOT advanced ---- */
    {
        control_replay_t rp; control_replay_init(&rp, GOLDEN_EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        uint8_t bad[GOLDEN_LEN];
        memcpy(bad, GOLDEN_JCTL, GOLDEN_LEN);
        bad[GOLDEN_LEN - 1] ^= 0x01;           /* flip one bit of the LAST tag byte */
        size_t flen = wrap_frame(frame, bad, sizeof(bad));

        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_AUTH, "T2 one flipped tag byte -> CV_DROP_AUTH");
        check(res.query == NULL && res.query_len == 0, "T2 bad MAC -> silent (no query)");
        check(rp.seq_floor == 0, "T2 bad MAC -> seq_floor UNCHANGED (auth gates replay)");
    }

    /* ---- T3 (teeth): the tag is KEY-bound, not a structural fluke ---- */
    {
        control_replay_t rp; control_replay_init(&rp, GOLDEN_EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        uint8_t wrong_key[32];
        memcpy(wrong_key, KEY, sizeof(wrong_key));
        wrong_key[0] ^= 0x01;                  /* one bit different */
        size_t flen = wrap_frame(frame, GOLDEN_JCTL, GOLDEN_LEN);

        control_verdict_t v = control_verify(frame, flen, wrong_key, sizeof(wrong_key),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_AUTH, "T3 same frame, one-bit-different key -> CV_DROP_AUTH");
        check(rp.seq_floor == 0, "T3 wrong key -> seq_floor UNCHANGED");
    }

    /* ---- T4 (teeth): a SHORT CAPTURE is rejected at parse ---- */
    {
        control_replay_t rp; control_replay_init(&rp, GOLDEN_EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        size_t flen = wrap_frame(frame, GOLDEN_JCTL, GOLDEN_LEN);

        /* Headers still claim the full datagram; one captured byte is missing. */
        control_verdict_t v = control_verify(frame, flen - 1, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_PARSE, "T4 truncated capture -> CV_DROP_PARSE");
        check(res.parse_status == CTRL_IP_LEN_BAD,
              "T4 truncated capture -> CTRL_IP_LEN_BAD (IP total_length exceeds capture)");
        check(res.query == NULL, "T4 truncated -> silent");
        check(rl.tokens_milli == CONTROL_RL_CAPACITY * 1000u,
              "T4 truncated -> rate-limit NOT consumed (parse runs first)");
    }

    /* ---- T5 (teeth): a self-consistently truncated JCTL body -> LEN_MISMATCH ---- */
    {
        control_replay_t rp; control_replay_init(&rp, GOLDEN_EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        /* Drop one message byte AND fix IP/UDP lengths, so only the exact-length
         * rule in control_parse_msg() (36 + qlen + 32 == len) can catch it. */
        size_t flen = wrap_frame(frame, GOLDEN_JCTL, GOLDEN_LEN - 1);

        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_PARSE, "T5 short JCTL body (lengths fixed up) -> CV_DROP_PARSE");
        check(res.parse_status == CTRL_LEN_MISMATCH,
              "T5 short JCTL body -> CTRL_LEN_MISMATCH (exact 36+qlen+32 rule)");
        check(rp.seq_floor == 0, "T5 malformed -> seq_floor UNCHANGED");
    }

    /* ---- T6 (teeth): re-sending the SAME signed frame is a REPLAY ---- */
    {
        control_replay_t rp; control_replay_init(&rp, GOLDEN_EPOCH);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        size_t flen = wrap_frame(frame, GOLDEN_JCTL, GOLDEN_LEN);

        control_verdict_t v1 = control_verify(frame, flen, KEY, sizeof(KEY),
                                              &rp, &rl, 1000, &res);
        check(v1 == CV_ACCEPT, "T6 first send of the golden frame -> CV_ACCEPT");

        control_verdict_t v2 = control_verify(frame, flen, KEY, sizeof(KEY),
                                              &rp, &rl, 1000, &res);
        check(v2 == CV_DROP_REPLAY, "T6 byte-identical re-send -> CV_DROP_REPLAY");
        check(res.replay_verdict == CR_REJECT_SEQ,
              "T6 re-send -> CR_REJECT_SEQ (seq <= floor; this is why the signer's "
              "next_seq() must strictly increase)");
        check(res.query == NULL, "T6 replay -> silent");
    }

    /* ---- T7 (teeth): a stale epoch is rejected even with a VALID tag ---- */
    {
        /* Same authentic frame, a receiver state on a DIFFERENT boot epoch. */
        control_replay_t rp; control_replay_init(&rp, GOLDEN_EPOCH + 1u);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000);
        size_t flen = wrap_frame(frame, GOLDEN_JCTL, GOLDEN_LEN);

        control_verdict_t v = control_verify(frame, flen, KEY, sizeof(KEY),
                                             &rp, &rl, 1000, &res);
        check(v == CV_DROP_REPLAY, "T7 valid tag, wrong boot epoch -> CV_DROP_REPLAY");
        check(res.replay_verdict == CR_REJECT_EPOCH, "T7 wrong epoch -> CR_REJECT_EPOCH");
        check(rp.seq_floor == 0, "T7 wrong epoch -> seq_floor UNCHANGED");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
