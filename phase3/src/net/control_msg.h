/*
 * control_msg.h — JARVIS control-IN wire format (Phase 6 goal 6-5).
 *
 * Authoritative wire-format header for the untrusted inbound control message
 * (the FIRST inbound the box accepts). Host-pure, no seL4 dep, no .c — this
 * header only declares the layout, offsets, and big-endian field readers.
 * Follows the host-pure precedent of km2b_miss.h / wake.h / monitors.h.
 *
 * ── Wire layout (all multi-byte fields network BIG-ENDIAN) ──────────────────
 *
 *   off  size  field
 *   ----------------------------------------------------------------------
 *     0    4    magic       u32   "JCTL" = 0x4A43544C
 *     4    1    version     u8    = 1
 *     5    1    flags       u8    reserved, MUST be 0
 *     6    8    seq         u64   monotonic sender sequence (replay floor)
 *    14    4    boot_epoch  u32   sender boot epoch
 *    18   16    nonce[16]         per-message nonce
 *    34    2    query_len   u16   <= CONTROL_QUERY_MAX (172)
 *    36    Q    query[Q]          the control-IN query text (Q = query_len)
 *  36+Q   32    tag[32]           HMAC-SHA256 authentication tag (LAST)
 *
 *   Header (fixed prefix) = 36 bytes. Tag = 32 bytes, always LAST.
 *   Whole message = 36 + query_len + 32, and MUST be <= 240 bytes.
 *   Minimum message (query_len == 0) = 36 + 0 + 32 = 68 bytes.
 *
 * ── CANONICAL HMAC INPUT ────────────────────────────────────────────────────
 *
 *   The HMAC covers  payload[0 .. payload_len - 32)  — i.e. EVERY byte of the
 *   received datagram EXCEPT the trailing 32-byte tag. Equivalently it is the
 *   36-byte header plus the query bytes:
 *
 *       mac_base = payload  (start of the datagram)
 *       mac_len  = CONTROL_HDR_LEN + query_len   (== 36 + query_len)
 *
 *   The tag itself is NEVER included in its own HMAC input. Verification
 *   recomputes HMAC over [mac_base, mac_base + mac_len) and compares (in
 *   constant time) against the trailing 32-byte tag.
 */

#ifndef JARVIS_CONTROL_MSG_H
#define JARVIS_CONTROL_MSG_H

#include <stdint.h>
#include <stddef.h>

/* Magic "JCTL" (ASCII, big-endian on the wire). */
#define CONTROL_MAGIC 0x4A43544Cu
#define CONTROL_VERSION 1u

#ifndef CONTROL_PORT
#define CONTROL_PORT 51001u
#endif

#define CONTROL_NONCE_LEN 16u
#define CONTROL_TAG_LEN 32u
#define CONTROL_HDR_LEN 36u
#define CONTROL_MSG_MAX 240u

#ifndef CONTROL_QUERY_MAX
#define CONTROL_QUERY_MAX (CONTROL_MSG_MAX - CONTROL_HDR_LEN - CONTROL_TAG_LEN) /* = 172 */
#endif

#define CONTROL_MSG_MIN (CONTROL_HDR_LEN + CONTROL_TAG_LEN) /* = 68 */

/* Field offsets (bytes from the start of the datagram). */
#define CTRL_OFF_MAGIC 0
#define CTRL_OFF_VERSION 4
#define CTRL_OFF_FLAGS 5
#define CTRL_OFF_SEQ 6
#define CTRL_OFF_EPOCH 14
#define CTRL_OFF_NONCE 18
#define CTRL_OFF_QLEN 34
#define CTRL_OFF_QUERY 36

/* Big-endian readers (bounds are the caller's responsibility). */
static inline uint16_t ctrl_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t ctrl_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static inline uint64_t ctrl_be64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

/*
 * Parsed view of a control message. The pointer fields (nonce/query/tag/
 * mac_base) point INTO the original datagram buffer — no copies. mac_base and
 * mac_len describe the canonical HMAC input (see the header comment above):
 * mac_base = payload start, mac_len = CONTROL_HDR_LEN + query_len.
 */
typedef struct control_msg_t {
    uint8_t version;
    uint8_t flags;
    uint64_t seq;
    uint32_t boot_epoch;
    const uint8_t *nonce;     /* CONTROL_NONCE_LEN bytes */
    const uint8_t *query;     /* query_len bytes         */
    uint16_t query_len;
    const uint8_t *tag;       /* CONTROL_TAG_LEN bytes    */
    const uint8_t *mac_base;  /* start of HMAC input (= payload start) */
    size_t mac_len;           /* CONTROL_HDR_LEN + query_len            */
} control_msg_t;

#endif /* JARVIS_CONTROL_MSG_H */
