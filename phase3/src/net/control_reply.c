/*
 * control_reply.c — Phase 6 6-5/M4b: the authenticated box -> console reply payload (pure logic).
 * See control_reply.h for the wire format, the honesty boundary (SIGNED, not ENCRYPTED), and
 * why the build is factored out of the seL4-only ctrl_send_reply().
 * No I/O here — the caller frames + transmits.
 */
#include "control_reply.h"
#include "hmac_sha256.h"
#include <string.h>

/* Standard zlib/IEEE CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF), bitwise — no table, so
 * there is no static data to place and nothing to initialize. Bit-identical to
 * jarvis_tlm_crc32(); the canonical vector crc32("123456789") == 0xCBF43926 is pinned in
 * test_control_reply.c, which is what makes it interoperable with Python's zlib.crc32. */
uint32_t ctrl_reply_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

int ctrl_build_reply(const uint8_t key[32], uint8_t verdict, uint16_t seq,
                     const uint8_t *text, uint32_t text_len,
                     uint8_t *out, uint32_t out_cap)
{
    if (!key || !out) return -1;

    /* Clamp, never error: an over-long answer is truncated, not dropped. */
    uint32_t tlen = (text_len > CTRL_REPLY_TEXT_MAX) ? CTRL_REPLY_TEXT_MAX : text_len;
    uint32_t total = CTRL_REPLY_OVERHEAD + tlen;

    /* Capacity is checked BEFORE the first store, so a rejected build leaves out[] untouched. */
    if (out_cap < total) return -2;

    out[0] = 'J'; out[1] = 'R'; out[2] = 'P'; out[3] = 'L';
    out[4] = (uint8_t)CTRL_REPLY_VERSION;
    out[5] = verdict;
    out[6] = (uint8_t)(seq & 0xFFu);
    out[7] = (uint8_t)((seq >> 8) & 0xFFu);
    out[8] = (uint8_t)(tlen & 0xFFu);
    out[9] = (uint8_t)((tlen >> 8) & 0xFFu);

    /* PRINTABLE-SANITIZE lives here (not in the caller): the answered path carries
     * attacker-influenced model text, and a deterministic golden vector requires it. A NULL
     * text with tlen > 0 emits '.' rather than dereferencing. */
    for (uint32_t i = 0; i < tlen; i++) {
        uint8_t c = text ? text[i] : 0u;
        out[CTRL_REPLY_HDR_LEN + i] = (c >= 0x20u && c < 0x7Fu) ? c : (uint8_t)'.';
    }

    uint32_t crc_off = CTRL_REPLY_HDR_LEN + tlen;              /* 10 + tlen */
    uint32_t crc = ctrl_reply_crc32(out, crc_off);             /* over [0, 10+tlen) */
    out[crc_off + 0] = (uint8_t)(crc & 0xFFu);
    out[crc_off + 1] = (uint8_t)((crc >> 8) & 0xFFu);
    out[crc_off + 2] = (uint8_t)((crc >> 16) & 0xFFu);
    out[crc_off + 3] = (uint8_t)((crc >> 24) & 0xFFu);

    /* The tag covers header + text + CRC — i.e. EXACTLY the bytes the receiver will parse, so
     * tampered text cannot be laundered by recomputing the CRC. */
    uint32_t tag_off = crc_off + CTRL_REPLY_CRC_LEN;           /* 14 + tlen */
    hmac_sha256(key, 32u, out, (size_t)tag_off, out + tag_off);

    return (int)total;
}
