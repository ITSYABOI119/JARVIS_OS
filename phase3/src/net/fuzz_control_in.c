/*
 * fuzz_control_in.c — ASan/UBSan fuzz target for the control-IN pipeline (6-5).
 *
 * The control-IN lane is the FIRST untrusted inbound the box accepts, so its
 * parser + auth + replay + rate-limit gauntlet must be crash-free for ANY byte
 * string and must return the EXACT verdict for every known-shaped attack. This
 * harness drives that gauntlet in three modes, ~100K iterations each:
 *
 *   (a) RAW        — random length (0..2048) + random bytes into control_verify
 *                    with a fixed key and a FRESH replay/ratelimit each iter.
 *                    Asserts only "no crash"; the verdict is whatever it is.
 *                    ASan/UBSan prove the parser never reads OOB / never UBs for
 *                    ANY input.
 *
 *   (b) STRUCTURED — build ONE valid, real-HMAC-signed Eth/IPv4/UDP+JCTL frame,
 *                    flip exactly one field, and assert the EXACT verdict:
 *                      bad-MAC        -> CV_DROP_AUTH
 *                      replayed-seq   -> CV_DROP_REPLAY (CR_REJECT_SEQ)
 *                      stale-epoch    -> CV_DROP_REPLAY (CR_REJECT_EPOCH)
 *                      dup-nonce      -> CV_DROP_REPLAY (CR_REJECT_NONCE)
 *                      oversized qlen -> CV_DROP_PARSE  (CTRL_QUERY_TOO_LONG)
 *                      truncated tag  -> CV_DROP_PARSE  (CTRL_LEN_MISMATCH)
 *                      MF/frag-offset -> CV_DROP_PARSE  (CTRL_IP_FRAGMENT)
 *                      seq-wrap       -> accept @ u64-max, then wrapped-0 rejects
 *                      nonce boundary -> N-th remembered vs N+1-th evicted
 *                    (plus an ACCEPT control so a "drop everything" bug can't
 *                    pass the drop assertions vacuously).
 *
 *   (c) DIFFERENTIAL — assert hmac_sha256()/hmac_sha256_verify() matches every
 *                    triple in hmac_vectors.h and sha256() matches every vector
 *                    in sha256_vectors.h (independent Python reference).
 *
 * On ANY assertion failure: print the iter + fixed seed and exit nonzero.
 * On success: print "FUZZ OK <iters> iters".
 *
 * Build/run (ASan/UBSan, -O1):
 *   gcc -Wall -Werror -O1 -std=c11 -fsanitize=address,undefined \
 *       -I phase3/src/net -I phase3/src/crypto \
 *       phase3/src/net/fuzz_control_in.c phase3/src/net/control_verify.c \
 *       phase3/src/net/control_parser.c phase3/src/net/control_replay.c \
 *       phase3/src/net/control_ratelimit.c phase3/src/crypto/hmac_sha256.c \
 *       phase3/src/crypto/sha256.c -lm -o /tmp/fuzz_ci && /tmp/fuzz_ci
 *
 * Host-pure (km2b_miss.c / wake.c / monitors.c precedent): no seL4/device/
 * allocator dep. The DEPLOYED modules under test are malloc-free; this TEST
 * DRIVER deliberately allocates each raw/short input in an EXACT-len heap buffer
 * (stdlib) so ASan's redzone sits at data[len] and catches any read PAST the
 * logical frame length — the class the review found the oversized fixed buffer
 * could not see (the len==14 IPv4-prefix over-read).
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>   /* malloc/free — exact-len fuzz inputs for ASan redzone coverage */

#include "control_verify.h"
#include "control_msg.h"
#include "control_parser.h"
#include "control_replay.h"
#include "control_ratelimit.h"
#include "hmac_sha256.h"
#include "sha256.h"

#include "hmac_vectors.h"
#include "sha256_vectors.h"

/* ---- fixed seed (fuzz_harness.c pattern) ---- */
#define FUZZ_SEED 0xDEADBEEFCAFE4242ULL
static uint64_t rng_state = FUZZ_SEED;

static uint64_t xorshift64(void)
{
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static void fill_random(void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++)
        p[i] = (uint8_t)(xorshift64() & 0xFF);
}

static uint32_t rand_range(uint32_t max) /* [0, max) */
{
    if (max == 0)
        return 0;
    return (uint32_t)(xorshift64() % (uint64_t)max);
}

/* ---- failure reporting: print iter + seed, bail nonzero ---- */
#define FUZZ_ASSERT(cond, mode, iter)                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("\nFUZZ FAIL [%s] iter=%lld seed=0x%016llx cond=(%s)\n",     \
                   (mode), (long long)(iter),                                  \
                   (unsigned long long)FUZZ_SEED, #cond);                      \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- big-endian store helpers (match test_control_verify.c) ---- */
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

/* ---- flexible signed-frame builder ---- */
typedef struct {
    const uint8_t *key;  size_t klen;
    uint64_t seq;        uint32_t epoch;
    const uint8_t *nonce;                 /* CONTROL_NONCE_LEN bytes */
    const uint8_t *query; uint16_t qlen;  /* 0..CONTROL_QUERY_MAX    */
    uint16_t frag_flags;                  /* IPv4 flags/frag-offset field */
    int corrupt_tag;                      /* 1 => flip one signed tag bit */
    int trunc_tag;                        /* >0 => drop this many trailing (tag) bytes */
    int qlen_field;                       /* <0 => real qlen; else override the field */
} build_opts_t;

/*
 * Assemble Eth(14)+IPv4(20)+UDP(8)+JCTL into `frame` (>= 320 B). The JCTL
 * message is 36-byte header + query + 32-byte HMAC tag, signed over
 * [0, 36+qlen). Returns total frame length.
 */
static size_t build_frame(uint8_t *frame, const build_opts_t *o)
{
    uint8_t msg[CONTROL_MSG_MAX];
    size_t full_msg_len = (size_t)CONTROL_HDR_LEN + o->qlen + CONTROL_TAG_LEN;

    put_be32(msg + CTRL_OFF_MAGIC, CONTROL_MAGIC);
    msg[CTRL_OFF_VERSION] = (uint8_t)CONTROL_VERSION;
    msg[CTRL_OFF_FLAGS]   = 0;
    put_be64(msg + CTRL_OFF_SEQ, o->seq);
    put_be32(msg + CTRL_OFF_EPOCH, o->epoch);
    memcpy(msg + CTRL_OFF_NONCE, o->nonce, CONTROL_NONCE_LEN);
    put_be16(msg + CTRL_OFF_QLEN,
             (o->qlen_field >= 0) ? (uint16_t)o->qlen_field : o->qlen);
    if (o->qlen)
        memcpy(msg + CTRL_OFF_QUERY, o->query, o->qlen);

    /* Real HMAC over the canonical span: 36-byte header + query (all but tag). */
    uint8_t tag[32];
    hmac_sha256(o->key, o->klen, msg, (size_t)CONTROL_HDR_LEN + o->qlen, tag);
    memcpy(msg + CONTROL_HDR_LEN + o->qlen, tag, CONTROL_TAG_LEN);
    if (o->corrupt_tag)
        msg[CONTROL_HDR_LEN + o->qlen] ^= 0x01;

    size_t msg_len = full_msg_len;
    if (o->trunc_tag > 0 && (size_t)o->trunc_tag < CONTROL_TAG_LEN)
        msg_len -= (size_t)o->trunc_tag;

    /* --- Ethernet --- */
    uint8_t *eth = frame;
    memset(eth, 0, 14);
    eth[0] = 0x02; eth[6] = 0x02;
    put_be16(eth + 12, 0x0800);

    /* --- IPv4 --- */
    uint8_t *ip = frame + 14;
    uint16_t total_len = (uint16_t)(20u + 8u + msg_len);
    ip[0] = 0x45;
    ip[1] = 0x00;
    put_be16(ip + 2, total_len);
    put_be16(ip + 4, 0x1234);
    put_be16(ip + 6, o->frag_flags);
    ip[8] = 64;
    ip[9] = 17;
    put_be16(ip + 10, 0);
    put_be32(ip + 12, 0xC0A864C0);
    put_be32(ip + 16, 0xC0A8648F);
    put_be16(ip + 10, ip_cksum(ip, 20));

    /* --- UDP --- */
    uint8_t *udp = frame + 34;
    put_be16(udp + 0, 40000);
    put_be16(udp + 2, (uint16_t)CONTROL_PORT);
    put_be16(udp + 4, (uint16_t)(8u + msg_len));
    put_be16(udp + 6, 0);

    memcpy(frame + 42, msg, msg_len);
    return 42u + msg_len;
}

/* ================================================================
 * Mode (a): RAW mutation — never crash for ANY input.
 * ================================================================ */
static const uint8_t RAW_KEY[32] = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
    0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
    0x0F,0x1E,0x2D,0x3C,0x4B,0x5A,0x69,0x78,
    0x87,0x96,0xA5,0xB4,0xC3,0xD2,0xE1,0xF0
};

static volatile uint32_t g_sink; /* defeat dead-code elimination of the verdict */

static int run_raw(long iters, long *out_done)
{
    for (long i = 0; i < iters; i++) {
        size_t len = (size_t)rand_range(2049); /* 0..2048 inclusive */
        /* EXACT-len heap allocation: ASan's redzone sits at buf[len], so any read
         * one byte past the logical frame (e.g. the len==14 IPv4-prefix over-read)
         * faults here instead of silently reading a fixed oversized buffer. */
        uint8_t *buf = (uint8_t *)malloc(len ? len : 1u);
        FUZZ_ASSERT(buf != NULL, "raw:oom", i);
        if (len)
            fill_random(buf, len);

        control_replay_t rp;   control_replay_init(&rp, (uint32_t)xorshift64());
        control_ratelimit_t rl; control_ratelimit_init(&rl, (uint32_t)xorshift64());
        control_result_t res;

        control_verdict_t v = control_verify(buf, len, RAW_KEY, sizeof(RAW_KEY),
                                             &rp, &rl, (uint32_t)xorshift64(), &res);
        g_sink += (uint32_t)v + (uint32_t)res.verdict + res.query_len;

        /* Contract sanity that must hold for ANY input: a non-ACCEPT verdict is
         * SILENT (no aliased query). Free BEFORE asserting so a failure path does
         * not also trip the ASan leak checker. */
        int silent_ok = (v == CV_ACCEPT) || (res.query == NULL && res.query_len == 0);
        free(buf);
        FUZZ_ASSERT(silent_ok, "raw:silent-drop", i);
    }
    *out_done = iters;
    return 0;
}

/* ================================================================
 * Mode (a2): SHORT IPv4 — deterministic. Frames of length 14..33 (< 14 +
 * IP_MIN_HDR) MUST reject as CV_DROP_PARSE / CTRL_TOO_SHORT WITHOUT reading past
 * the frame. Exact-len heap buffers make ASan catch the len==14 ip[0] over-read
 * (the review finding the random raw mode is far too sparse to hit: p ~ 1/2^27
 * per iter). Runs every invocation as a permanent regression tooth.
 * ================================================================ */
static int run_short_ipv4(long *out_done)
{
    static const uint8_t SK[16] = {0};
    long done = 0;
    for (size_t L = 14u; L <= 33u; L++) {
        uint8_t *buf = (uint8_t *)malloc(L);
        FUZZ_ASSERT(buf != NULL, "short:oom", (long)L);
        memset(buf, 0, L);
        buf[0] = 0x02; buf[6] = 0x02;
        put_be16(buf + 12, 0x0800);          /* IPv4 ethertype */
        if (L > 14u) buf[14] = 0x45;         /* plausible ver4/IHL5 IF the byte is read */

        control_replay_t rp;   control_replay_init(&rp, 7u);
        control_ratelimit_t rl; control_ratelimit_init(&rl, 1000000u);
        control_result_t res;
        control_verdict_t v = control_verify(buf, L, SK, sizeof(SK),
                                             &rp, &rl, 1000000u, &res);
        int ok = (v == CV_DROP_PARSE) &&
                 (res.parse_status == CTRL_TOO_SHORT) &&
                 (res.query == NULL);
        free(buf);
        FUZZ_ASSERT(ok, "short-ipv4:reject", (long)L);
        done++;
    }
    *out_done = done;
    return 0;
}

/* ================================================================
 * Mode (b): STRUCTURED — one signed frame, one field flipped, exact verdict.
 * ================================================================ */
enum {
    C_ACCEPT = 0,
    C_BADMAC,
    C_REPLAY_SEQ,
    C_STALE_EPOCH,
    C_DUP_NONCE,
    C_OVERSIZE_QLEN,
    C_TRUNC_TAG,
    C_FRAGMENT,
    C_SEQ_WRAP,
    C_NONCE_BOUNDARY,
    C_COUNT
};

static int run_structured(long iters, long *out_done)
{
    uint8_t frame[320];
    uint8_t key[80];
    uint8_t nonce[CONTROL_NONCE_LEN];
    uint8_t nonce2[CONTROL_NONCE_LEN];
    uint8_t query[CONTROL_QUERY_MAX];
    control_result_t res;

    const uint32_t BASE_MS = 1000000u; /* +1000ms/verify keeps the token bucket full */

    for (long i = 0; i < iters; i++) {
        /* Per-iteration randomized-but-valid material. */
        size_t klen = 1u + rand_range(80);
        fill_random(key, klen);
        fill_random(nonce, sizeof(nonce));
        uint16_t qlen = (uint16_t)rand_range(CONTROL_QUERY_MAX + 1); /* 0..172 */
        if (qlen)
            fill_random(query, qlen);
        uint32_t epoch = (uint32_t)xorshift64();
        uint64_t seq = 1u + (xorshift64() % 1000000u); /* > 0 so first accept works */

        build_opts_t o;
        memset(&o, 0, sizeof(o));
        o.key = key; o.klen = klen;
        o.seq = seq; o.epoch = epoch;
        o.nonce = nonce; o.query = query; o.qlen = qlen;
        o.qlen_field = -1;

        int cse = (int)rand_range(C_COUNT);
        uint32_t ms = BASE_MS;

        switch (cse) {
        case C_ACCEPT: {
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            size_t flen = build_frame(frame, &o);
            control_verdict_t v = control_verify(frame, flen, key, klen,
                                                 &rp, &rl, ms, &res);
            FUZZ_ASSERT(v == CV_ACCEPT, "accept:verdict", i);
            FUZZ_ASSERT(res.query_len == qlen, "accept:qlen", i);
            FUZZ_ASSERT(qlen == 0 || memcmp(res.query, query, qlen) == 0,
                        "accept:query-bytes", i);
            FUZZ_ASSERT(rp.seq_floor == seq, "accept:floor-advanced", i);
            break;
        }
        case C_BADMAC: {
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            o.corrupt_tag = 1;
            size_t flen = build_frame(frame, &o);
            uint64_t floor0 = rp.seq_floor;
            control_verdict_t v = control_verify(frame, flen, key, klen,
                                                 &rp, &rl, ms, &res);
            FUZZ_ASSERT(v == CV_DROP_AUTH, "badmac:verdict", i);
            FUZZ_ASSERT(res.query == NULL && res.query_len == 0, "badmac:silent", i);
            FUZZ_ASSERT(rp.seq_floor == floor0, "badmac:floor-untouched", i);
            break;
        }
        case C_REPLAY_SEQ: {
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            size_t flen = build_frame(frame, &o);
            control_verdict_t v1 = control_verify(frame, flen, key, klen,
                                                  &rp, &rl, ms, &res);
            FUZZ_ASSERT(v1 == CV_ACCEPT, "replay:setup-accept", i);
            /* re-send the identical frame (seq <= floor now) */
            size_t flen2 = build_frame(frame, &o);
            control_verdict_t v2 = control_verify(frame, flen2, key, klen,
                                                  &rp, &rl, ms + 1000u, &res);
            FUZZ_ASSERT(v2 == CV_DROP_REPLAY, "replay:verdict", i);
            FUZZ_ASSERT(res.replay_verdict == CR_REJECT_SEQ, "replay:diag", i);
            FUZZ_ASSERT(res.query == NULL, "replay:silent", i);
            break;
        }
        case C_STALE_EPOCH: {
            /* Frame is validly signed with `epoch`; the box holds a DIFFERENT
             * epoch, so the authenticated frame is rejected at replay. */
            uint32_t box_epoch = epoch ^ 0x1u; /* guaranteed != epoch */
            control_replay_t rp;   control_replay_init(&rp, box_epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            size_t flen = build_frame(frame, &o);
            control_verdict_t v = control_verify(frame, flen, key, klen,
                                                 &rp, &rl, ms, &res);
            FUZZ_ASSERT(v == CV_DROP_REPLAY, "epoch:verdict", i);
            FUZZ_ASSERT(res.replay_verdict == CR_REJECT_EPOCH, "epoch:diag", i);
            FUZZ_ASSERT(rp.seq_floor == 0, "epoch:floor-untouched", i);
            break;
        }
        case C_DUP_NONCE: {
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            size_t flen = build_frame(frame, &o);
            control_verdict_t v1 = control_verify(frame, flen, key, klen,
                                                  &rp, &rl, ms, &res);
            FUZZ_ASSERT(v1 == CV_ACCEPT, "dupnonce:setup-accept", i);
            /* Same nonce, strictly-higher seq (passes seq, trips nonce). */
            o.seq = seq + 1u;
            size_t flen2 = build_frame(frame, &o);
            control_verdict_t v2 = control_verify(frame, flen2, key, klen,
                                                  &rp, &rl, ms + 1000u, &res);
            FUZZ_ASSERT(v2 == CV_DROP_REPLAY, "dupnonce:verdict", i);
            FUZZ_ASSERT(res.replay_verdict == CR_REJECT_NONCE, "dupnonce:diag", i);
            break;
        }
        case C_OVERSIZE_QLEN: {
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            /* Keep the actual message small/consistent, but declare a query_len
             * field > CONTROL_QUERY_MAX -> CTRL_QUERY_TOO_LONG (pre-auth). */
            o.qlen = (uint16_t)rand_range(32);
            if (o.qlen)
                fill_random(query, o.qlen);
            o.qlen_field = (int)(CONTROL_QUERY_MAX + 1u + rand_range(60000));
            size_t flen = build_frame(frame, &o);
            control_verdict_t v = control_verify(frame, flen, key, klen,
                                                 &rp, &rl, ms, &res);
            FUZZ_ASSERT(v == CV_DROP_PARSE, "oversize:verdict", i);
            FUZZ_ASSERT(res.parse_status == CTRL_QUERY_TOO_LONG, "oversize:diag", i);
            FUZZ_ASSERT(res.query == NULL, "oversize:silent", i);
            break;
        }
        case C_TRUNC_TAG: {
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            /* Drop 1..31 trailing tag bytes; keep qlen >= trunc so the truncated
             * message stays >= CONTROL_MSG_MIN (68) and the failure is a clean
             * length mismatch (CTRL_LEN_MISMATCH), not a too-short envelope. */
            o.qlen = (uint16_t)(31u + rand_range(CONTROL_QUERY_MAX - 31u + 1u)); /* 31..172 */
            fill_random(query, o.qlen);
            o.trunc_tag = 1 + (int)rand_range(31); /* 1..31 <= qlen */
            size_t flen = build_frame(frame, &o);
            control_verdict_t v = control_verify(frame, flen, key, klen,
                                                 &rp, &rl, ms, &res);
            FUZZ_ASSERT(v == CV_DROP_PARSE, "trunc:verdict", i);
            FUZZ_ASSERT(res.parse_status == CTRL_LEN_MISMATCH, "trunc:diag", i);
            FUZZ_ASSERT(res.query == NULL, "trunc:silent", i);
            break;
        }
        case C_FRAGMENT: {
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            /* Either MF (0x2000) or a nonzero fragment offset (0x1FFF bits). */
            if (rand_range(2))
                o.frag_flags = 0x2000u;
            else
                o.frag_flags = (uint16_t)(1u + rand_range(0x1FFFu)); /* 1..8191 */
            size_t flen = build_frame(frame, &o);
            control_verdict_t v = control_verify(frame, flen, key, klen,
                                                 &rp, &rl, ms, &res);
            FUZZ_ASSERT(v == CV_DROP_PARSE, "fragment:verdict", i);
            FUZZ_ASSERT(res.parse_status == CTRL_IP_FRAGMENT, "fragment:diag", i);
            FUZZ_ASSERT(res.query == NULL, "fragment:silent", i);
            /* Fragment is rejected before ratelimit -> bucket untouched (full). */
            FUZZ_ASSERT(rl.tokens_milli == CONTROL_RL_CAPACITY * 1000u,
                        "fragment:ratelimit-untouched", i);
            break;
        }
        case C_SEQ_WRAP: {
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            /* Accept at seq = u64-max, then a wrapped-to-0 seq must reject. */
            o.seq = (uint64_t)-1; /* UINT64_MAX */
            size_t flen = build_frame(frame, &o);
            control_verdict_t v1 = control_verify(frame, flen, key, klen,
                                                  &rp, &rl, ms, &res);
            FUZZ_ASSERT(v1 == CV_ACCEPT, "wrap:setup-accept", i);
            FUZZ_ASSERT(rp.seq_floor == (uint64_t)-1, "wrap:floor-at-max", i);
            fill_random(nonce2, sizeof(nonce2)); /* distinct nonce */
            o.seq = 0; o.nonce = nonce2;
            size_t flen2 = build_frame(frame, &o);
            control_verdict_t v2 = control_verify(frame, flen2, key, klen,
                                                  &rp, &rl, ms + 1000u, &res);
            FUZZ_ASSERT(v2 == CV_DROP_REPLAY, "wrap:verdict", i);
            FUZZ_ASSERT(res.replay_verdict == CR_REJECT_SEQ, "wrap:diag", i);
            break;
        }
        case C_NONCE_BOUNDARY: {
            /* Fill the ring to exactly CONTROL_NONCE_WINDOW distinct nonces, then
             * prove the boundary: the oldest (N-th) is still remembered (dup ->
             * reject), but after one eviction it is forgotten (N+1-th -> accept). */
            const uint32_t W = CONTROL_NONCE_WINDOW;
            control_replay_t rp;   control_replay_init(&rp, epoch);
            control_ratelimit_t rl; control_ratelimit_init(&rl, ms);
            uint8_t rsuffix[CONTROL_NONCE_LEN];
            fill_random(rsuffix, sizeof(rsuffix));
            uint8_t nb[CONTROL_NONCE_LEN];
            uint32_t step = 0;

            for (uint32_t j = 0; j < W; j++) {
                memcpy(nb, rsuffix, CONTROL_NONCE_LEN);
                put_be32(nb, j); /* distinct per j (suffix fixed) */
                o.seq = (uint64_t)j + 1u;
                o.nonce = nb;
                size_t flen = build_frame(frame, &o);
                control_verdict_t v = control_verify(frame, flen, key, klen,
                                                     &rp, &rl, ms + 1000u * step, &res);
                step++;
                FUZZ_ASSERT(v == CV_ACCEPT, "boundary:fill-accept", i);
            }
            /* Oldest nonce (j=0) still in the ring: dup at seq W+1 -> nonce reject. */
            memcpy(nb, rsuffix, CONTROL_NONCE_LEN);
            put_be32(nb, 0);
            o.seq = (uint64_t)W + 1u;
            o.nonce = nb;
            size_t flenN = build_frame(frame, &o);
            control_verdict_t vN = control_verify(frame, flenN, key, klen,
                                                  &rp, &rl, ms + 1000u * step, &res);
            step++;
            FUZZ_ASSERT(vN == CV_DROP_REPLAY, "boundary:N-th-verdict", i);
            FUZZ_ASSERT(res.replay_verdict == CR_REJECT_NONCE, "boundary:N-th-diag", i);

            /* A fresh nonce (j=W) at seq W+1 accepts and EVICTS the oldest (j=0). */
            memcpy(nb, rsuffix, CONTROL_NONCE_LEN);
            put_be32(nb, W);
            o.seq = (uint64_t)W + 1u;
            o.nonce = nb;
            size_t flenE = build_frame(frame, &o);
            control_verdict_t vE = control_verify(frame, flenE, key, klen,
                                                  &rp, &rl, ms + 1000u * step, &res);
            step++;
            FUZZ_ASSERT(vE == CV_ACCEPT, "boundary:evict-accept", i);

            /* Now the once-remembered oldest (j=0) is forgotten: N+1-th accepts. */
            memcpy(nb, rsuffix, CONTROL_NONCE_LEN);
            put_be32(nb, 0);
            o.seq = (uint64_t)W + 2u;
            o.nonce = nb;
            size_t flenA = build_frame(frame, &o);
            control_verdict_t vA = control_verify(frame, flenA, key, klen,
                                                  &rp, &rl, ms + 1000u * step, &res);
            step++;
            FUZZ_ASSERT(vA == CV_ACCEPT, "boundary:reaccept-evicted", i);
            break;
        }
        default:
            break;
        }
    }
    *out_done = iters;
    return 0;
}

/* ================================================================
 * Mode (c): DIFFERENTIAL — crypto matches the independent reference vectors.
 * ================================================================ */
static int run_differential(long target_iters, long *out_done)
{
    long done = 0;
    uint8_t out[32];

    /* Full pass over every SHA-256 vector (guarantees each is checked >= once). */
    for (int i = 0; i < SHA256_VECTOR_COUNT; i++) {
        const sha256_vector_t *v = &SHA256_VECTORS[i];
        sha256(v->msg, (size_t)v->mlen, out);
        FUZZ_ASSERT(memcmp(out, v->exp, 32) == 0, "diff:sha256", i);
        done++;
    }

    /* Full pass over every HMAC-SHA256 triple: forward + verify + negative. */
    for (int i = 0; i < HMAC_VECTOR_COUNT; i++) {
        const hmac_vector_t *v = &HMAC_VECTORS[i];
        hmac_sha256(v->key, (size_t)v->klen, v->msg, (size_t)v->mlen, out);
        FUZZ_ASSERT(memcmp(out, v->tag, 32) == 0, "diff:hmac-forward", i);
        FUZZ_ASSERT(hmac_sha256_verify(v->key, (size_t)v->klen,
                                       v->msg, (size_t)v->mlen, v->tag) == 1,
                    "diff:hmac-verify-pos", i);
        uint8_t bad[32];
        memcpy(bad, v->tag, 32);
        bad[i % 32] ^= 0x01;
        FUZZ_ASSERT(hmac_sha256_verify(v->key, (size_t)v->klen,
                                       v->msg, (size_t)v->mlen, bad) == 0,
                    "diff:hmac-verify-neg", i);
        done++;
    }

    /* Random-index checks to reach ~target_iters differential iterations. */
    while (done < target_iters) {
        int idx = (int)rand_range((uint32_t)HMAC_VECTOR_COUNT);
        const hmac_vector_t *v = &HMAC_VECTORS[idx];
        hmac_sha256(v->key, (size_t)v->klen, v->msg, (size_t)v->mlen, out);
        FUZZ_ASSERT(memcmp(out, v->tag, 32) == 0, "diff:hmac-rand", done);
        done++;
    }

    *out_done = done;
    return 0;
}

int main(void)
{
    long total = 0, done = 0;

    if (run_raw(100000, &done))
        return 1;
    total += done;

    if (run_short_ipv4(&done))
        return 1;
    total += done;

    if (run_structured(100000, &done))
        return 1;
    total += done;

    if (run_differential(100000, &done))
        return 1;
    total += done;

    printf("FUZZ OK %ld iters\n", total);
    return 0;
}
