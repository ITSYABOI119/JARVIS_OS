#!/usr/bin/env python3
"""
test_telemetry_receiver.py - host test for the N-c-2 telemetry receiver.

Proves C<->Python wire-compatibility with the box-side telemetry_packet_t
(phase3/src/drivers/jarvis_telemetry.h): the struct layout sums to 200 bytes,
the CRC is standard zlib (canonical 0xCBF43926 vector), and decode_packet
round-trips a valid packet while rejecting corrupt / wrong-magic / wrong-size
input the right way (crc_ok=False vs ValueError).

Run: python3 phase3/scripts/test_telemetry_receiver.py  (exit nonzero on FAIL)
"""

import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telemetry_receiver import decode_packet, FMT, MAGIC, PKT_SIZE  # noqa: E402

_PASS = 0
_FAIL = 0


def check(cond, msg):
    global _PASS, _FAIL
    if cond:
        _PASS += 1
        print("  PASS: %s" % msg)
    else:
        _FAIL += 1
        print("  FAIL: %s" % msg)


def raises_valueerror(fn):
    try:
        fn()
        return False
    except ValueError:
        return True


# Field order matches FMT / jarvis_telemetry.h exactly.
_DEFAULTS = dict(
    magic=MAGIC, version=1, kind=1, flags=0x01 | 0x10, boot_id=1, seq=42,
    uptime_ms=120000, reserved_t=0,
    q_total=289, q_hits=211, q_infer=29, q_heartbeat=40, q_shield=9, q_errors=0,
    num_nodes=6, model_load_pct=100, fb_bpp=32, selftest_score=5,
    fb_w=1024, fb_h=768, model_size_mb=2962, total_ram_mb=0,
    infer_gen_tokens=0, reserved_i=0,
    last_text=b"hello", model_name=b"Gemma 4 E2B",
    reserved2_0=0, reserved2_1=0, crc32=0,
)


def build_packet(finalize=True, **overrides):
    """Pack a packet; when finalize, stamp a valid zlib CRC over [:196]."""
    v = dict(_DEFAULTS)
    v.update(overrides)
    body = struct.pack(
        FMT, v['magic'], v['version'], v['kind'], v['flags'], v['boot_id'], v['seq'],
        v['uptime_ms'], v['reserved_t'],
        v['q_total'], v['q_hits'], v['q_infer'], v['q_heartbeat'], v['q_shield'], v['q_errors'],
        v['num_nodes'], v['model_load_pct'], v['fb_bpp'], v['selftest_score'],
        v['fb_w'], v['fb_h'], v['model_size_mb'], v['total_ram_mb'],
        v['infer_gen_tokens'], v['reserved_i'], v['last_text'], v['model_name'],
        v['reserved2_0'], v['reserved2_1'], v['crc32'])
    if finalize:
        crc = zlib.crc32(body[:196]) & 0xFFFFFFFF
        body = body[:196] + struct.pack('<I', crc)
    return body


def main():
    print("== telemetry receiver wire-compat ==")

    # Layout
    check(struct.calcsize(FMT) == 200, "struct.calcsize(FMT) == 200")
    check(PKT_SIZE == 200, "PKT_SIZE == 200")

    # Canonical zlib CRC vector — same CRC the C side proved (jarvis_telemetry.c)
    check(zlib.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926,
          "canonical zlib CRC of \"123456789\" == 0xCBF43926")

    # Valid packet round-trips
    pkt = build_packet()
    check(len(pkt) == 200, "built packet is 200 bytes")
    d = decode_packet(pkt)
    check(d['crc_ok'] is True, "valid packet crc_ok True")
    check(d['kind_name'] == 'STATS', "kind_name == STATS")
    check('MODEL_LOADED' in d['flags_list'], "MODEL_LOADED in flags_list")
    check('SELFTEST_PASS' in d['flags_list'], "SELFTEST_PASS in flags_list")
    check('HAS_ERROR' not in d['flags_list'], "HAS_ERROR not set")
    check(d['model_name'] == 'Gemma 4 E2B', "model_name round-trips")
    check(d['last_text'] == 'hello', "last_text round-trips")
    check(d['seq'] == 42 and d['boot_id'] == 1, "seq/boot_id round-trip")
    check(d['q_total'] == 289 and d['q_hits'] == 211 and d['q_infer'] == 29 and d['q_errors'] == 0,
          "q_* counters round-trip")
    check(d['num_nodes'] == 6 and d['model_load_pct'] == 100, "num_nodes/load round-trip")
    check(d['fb_w'] == 1024 and d['fb_h'] == 768 and d['fb_bpp'] == 32, "fb geometry round-trips")
    check(d['selftest_score'] == 5, "selftest_score round-trips")

    # Corrupt one byte in [0:196] -> crc_ok False (NOT a crash, NOT ValueError)
    ba = bytearray(pkt)
    ba[50] ^= 0xFF
    dc = decode_packet(bytes(ba))
    check(dc['crc_ok'] is False, "single-byte corruption -> crc_ok False (no crash)")

    # Wrong magic -> ValueError
    check(raises_valueerror(lambda: decode_packet(build_packet(magic=0xDEADBEEF))),
          "wrong magic raises ValueError")

    # Wrong length -> ValueError
    check(raises_valueerror(lambda: decode_packet(pkt[:199]), ), "199-byte input raises ValueError")
    check(raises_valueerror(lambda: decode_packet(pkt + b'\x00')), "201-byte input raises ValueError")

    print("\n== Results: %d PASS, %d FAIL ==" % (_PASS, _FAIL))
    return 1 if _FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
