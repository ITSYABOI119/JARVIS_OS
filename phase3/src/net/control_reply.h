/*
 * control_reply.h — Phase 6 6-5/M4b: the AUTHENTICATED box -> console reply payload.
 *
 * WHAT M4b CLOSES: through M4a the JRPL reply was CRC-32 only. A CRC is not a MAC — any LAN
 * host able to reach the console's reply port could FORGE a JRPL datagram, fabricating an
 * "answer from JARVIS" (including invented "recalled memory") and, via the console's 16-bit
 * seq correlation, landing it on a real pending turn. v2 appends an HMAC-SHA256 tag over the
 * whole payload, verified CONSTANT-TIME in the receiver; a reply that fails auth is DROPPED
 * and never rendered. The key is the SAME symmetric JKEY the box and the operator's console
 * already share for the inbound (console -> box) direction.
 *
 * *** HONESTY (load-bearing, CI-enforced): the reply is AUTHENTICATED / SIGNED — it is NOT
 * ENCRYPTED. The text is plaintext on the wire; anyone who can capture the frame can READ it.
 * M4b stops SPOOFING (forging a reply), not eavesdropping. Never describe the reply as
 * "encrypted" / "private" / "secret" / a "secure channel". And unicast ADDRESSING is all that
 * is proven — never claim third-host delivery exclusivity (a switch property, not this code). ***
 *
 * SCOPE: the control-IN REPLY only. Telemetry-OUT (the ~1 Hz broadcast) stays CRC-only and
 * UNCHANGED by design — it is non-sensitive and broadcast on purpose.
 *
 * ── WIRE FORMAT v2 (little-endian; v1 was CRC-only and was NEVER DEPLOYED) ──────────────
 *
 *   off        size   field
 *   0          4      "JRPL" magic
 *   4          1      version = 2
 *   5          1      verdict   (0 answered / 1 refused / 2 degraded / 3 failed)
 *   6          2      seq       u16 LE   (the request seq, truncated to u16)
 *   8          2      tlen      u16 LE
 *   10         tlen   text      (printable-sanitized, <= CTRL_REPLY_TEXT_MAX)
 *   10+tlen    4      crc32     u32 LE = zlib/IEEE CRC-32 over payload[0 .. 10+tlen)
 *   14+tlen    32     tag       = HMAC-SHA256(key, payload[0 .. 14+tlen))
 *
 *   TOTAL = 46 + tlen.
 *
 * THE HMAC SPAN DELIBERATELY INCLUDES THE CRC BYTES: the tag authenticates the exact bytes the
 * receiver will parse, so an attacker cannot rewrite the CRC to match tampered text. The CRC is
 * retained as a cheap integrity check and the receiver requires BOTH crc_ok AND hmac_ok.
 *
 * WHY THIS IS A HOST-PURE MODULE: the box's ctrl_send_reply() is seL4-only (puts_serial /
 * net_build_udp_unicast / i211), so it can never be host-compiled or differential-tested.
 * Factoring the PAYLOAD BUILD out lets the box and a host test produce byte-identical replies —
 * the same discipline that made the inbound build_control_frame() differential-testable.
 * Depends only on <stdint.h> / <string.h> / hmac_sha256.h. No seL4, no device, no allocator.
 */
#ifndef JARVIS_CONTROL_REPLY_H
#define JARVIS_CONTROL_REPLY_H

#include <stdint.h>

/* 'J','R','P','L' — the four bytes appear ON THE WIRE IN ORDER (4A 52 50 4C), so this scalar is
 * their BIG-ENDIAN reading. Same convention as control_msg.h's CONTROL_MAGIC ('JCTL'); the rest of
 * the reply header (seq, tlen, crc) is LITTLE-endian, matching the receiver's struct.unpack('<...'). */
#define CTRL_REPLY_MAGIC     0x4A52504Cu
#define CTRL_REPLY_VERSION   2u            /* v1 = CRC-only, never deployed; v2 = HMAC'd  */
#define CTRL_REPLY_TAG_LEN   32u
#define CTRL_REPLY_HDR_LEN   10u
#define CTRL_REPLY_CRC_LEN   4u

/* MAX ANSWER TEXT, AND IT IS DERIVED FROM THE MTU — NOT A ROUND NUMBER.
 *
 * The reply leaves as ONE raw UDP unicast Ethernet frame (net_build_udp_unicast + i211_send_phys).
 * There is NO IP fragmentation on that path, so the MTU is a hard ceiling, not a tuning knob:
 *
 *     UDP payload max = MTU 1500 - IP_HDR_LEN 20 - UDP_HDR_LEN 8 = 1472
 *                       (net_udp.c already defines exactly this as UDP_MAX_PAYLOAD)
 *     JRPL overhead   = CTRL_REPLY_OVERHEAD = 10 hdr + 4 crc + 32 tag = 46
 *     => text max     = 1472 - 46 = 1426
 *
 * NOTE the Ethernet header (14 B) is NOT subtracted from the 1500: the MTU is the maximum IP
 * DATAGRAM, and the frame on the wire is 14 + 1500 = 1514, inside the driver's
 * I211_MAX_FRAME_SIZE 1536. An earlier estimate of ~1412 subtracted it twice.
 *
 * Raised 512 -> 1426 at M2 so a complete EXPLANATORY answer fits; short factual answers already
 * finished inside the old cap. At the measured ~4.9 bytes/token this is ~290 tokens, and
 * test_control_reply.c T7 pins CTRL_REPLY_MAX_LEN <= 1472 so a future bump cannot silently
 * produce a frame the NIC will not send. */
#ifndef CTRL_REPLY_TEXT_MAX
#define CTRL_REPLY_TEXT_MAX  1426u
#endif

/* worst case = 10 (hdr) + 512 (text) + 4 (crc) + 32 (tag) = 558 */
#define CTRL_REPLY_MAX_LEN (CTRL_REPLY_HDR_LEN + CTRL_REPLY_TEXT_MAX + \
                            CTRL_REPLY_CRC_LEN + CTRL_REPLY_TAG_LEN)

/* the fixed overhead around the text: 46 == 10 + 4 + 32 */
#define CTRL_REPLY_OVERHEAD (CTRL_REPLY_HDR_LEN + CTRL_REPLY_CRC_LEN + CTRL_REPLY_TAG_LEN)

/*
 * Build a complete, signed v2 reply payload into out[].
 *
 * Returns the total payload length (CTRL_REPLY_OVERHEAD + clamped tlen), or a NEGATIVE value if
 * `key` or `out` is NULL, or out_cap is too small for the payload that WOULD be produced. On a
 * negative return NOTHING is written to out[] — the caller can never send a half-built reply.
 *
 * text_len > CTRL_REPLY_TEXT_MAX is CLAMPED (never an error).
 *
 * SANITIZE: the caller need NOT pre-sanitize. Every text byte outside printable ASCII
 * [0x20, 0x7f) is replaced with '.' HERE, so the box cannot forget it on the answered path —
 * which carries attacker-influenced model output — and so the golden vector is deterministic.
 * A NULL `text` with text_len > 0 is SAFE and emits tlen bytes of '.' (no read of the NULL
 * pointer); it is a caller bug, not a crash.
 */
int ctrl_build_reply(const uint8_t key[32], uint8_t verdict, uint16_t seq,
                     const uint8_t *text, uint32_t text_len,
                     uint8_t *out, uint32_t out_cap);

/*
 * The standard zlib/IEEE CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF) used for the crc32
 * field — exposed so tests can pin it against the canonical vector crc32("123456789") ==
 * 0xCBF43926. Bit-identical to jarvis_tlm_crc32(); reimplemented locally to keep this module
 * host-pure with no drivers/ include path or unrelated TU linked in (the control_floor.c /
 * control_console.c self-contained-fold precedent).
 */
uint32_t ctrl_reply_crc32(const uint8_t *data, uint32_t len);

#endif /* JARVIS_CONTROL_REPLY_H */
