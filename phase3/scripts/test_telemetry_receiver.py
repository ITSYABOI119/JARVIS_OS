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

import json
import os
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telemetry_receiver import (  # noqa: E402
    decode_packet, packet_to_record, iter_pcap_telemetry, FMT, MAGIC, PKT_SIZE)
from telemetry_fixture import (  # noqa: E402  -- shared packer (moved out of this file)
    _DEFAULTS, build_packet, _build_pcap_one, build_pcap_many, REQUIRED_RECORD_KEYS,
    frame_to_packet)

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

    # --- N-c-3a: packet_to_record (the /events SSE record) ---
    rec = packet_to_record(decode_packet(pkt))
    json.dumps(rec)  # must be JSON-serializable (raises on failure)
    check(rec['crc_ok'] is True, "record crc_ok True")
    check(rec['kind_name'] == 'STATS', "record kind_name == STATS")
    check(rec['num_nodes'] == 6, "record num_nodes == 6")
    check(rec['model_name'] == 'Gemma 4 E2B', "record model_name round-trips")
    check(rec.get('recv_ts') == 0, "record recv_ts defaults to 0")
    check(not any(k in rec for k in ('tok_s', 'tokps', 'gpu', 'tier', 'agents', 'shield_blocked')),
          "record has NO fabricated keys")

    rec_bad = packet_to_record(decode_packet(bytes(ba)))  # ba = corrupted packet from above
    json.dumps(rec_bad)
    check(rec_bad['crc_ok'] is False, "corrupt-packet record crc_ok False (still serializes)")

    # --- N-c-3a: iter_pcap_telemetry on a synthetic 1-packet pcap ---
    pcap = _build_pcap_one(pkt, ts_s=1700000001)
    tf = tempfile.NamedTemporaryFile(suffix='.pcap', delete=False)
    try:
        tf.write(pcap)
        tf.close()
        got = list(iter_pcap_telemetry(tf.name))
    finally:
        os.unlink(tf.name)
    check(len(got) == 1, "iter_pcap_telemetry yields exactly 1 packet")
    if got:
        ts, dp = got[0]
        check(dp['magic'] == MAGIC and dp['seq'] == 42, "replayed packet magic + seq correct")
        check(dp['model_name'] == 'Gemma 4 E2B', "replayed packet model_name correct")
        check(ts == 1700000001.0, "replayed recv_ts correct")

    # --- Layer A: golden key-contract (every frame round-trips; key set locked) ---
    golden_path = os.path.normpath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..',
        'phase4', 'console', 'fixtures', 'golden_telemetry.json'))
    with open(golden_path, 'r', encoding='utf-8') as gf:
        golden = json.load(gf)
    meta_keys = set(golden['meta']['keys'])
    check(meta_keys == set(REQUIRED_RECORD_KEYS),
          "golden meta.keys == REQUIRED_RECORD_KEYS (fixture matches receiver output)")
    check(golden['meta']['size'] == 200 and golden['meta']['fmt'] == FMT,
          "golden meta fmt/size match the wire format")

    kind_expect = {1: 'STATS', 2: 'INFER', 3: 'STATE'}
    internal = ('magic', 'crc32', 'crc_calc', 'reserved_t', 'reserved_i', 'reserved2_0', 'reserved2_1')
    for fr in golden['frames']:
        label = fr.get('label', '?')
        payload, corrupt = frame_to_packet(fr)
        rec = packet_to_record(decode_packet(payload))
        json.dumps(rec)  # must be JSON-serializable
        check(set(rec.keys()) == meta_keys, "[%s] record keys == meta.keys" % label)
        check(rec['crc_ok'] is (not corrupt), "[%s] crc_ok == %s" % (label, not corrupt))
        check(rec['kind_name'] == kind_expect.get(fr['kind'], '?'),
              "[%s] kind_name derived (%s)" % (label, rec['kind_name']))
        check(set(rec['flags_list']) == set(fr.get('flags', [])),
              "[%s] flags_list derived" % label)
        rt_ok = all(rec[k] == v for k, v in fr.items()
                    if k not in ('label', 'ts_s', 'ts_frac', 'corrupt', 'flags') and k in rec)
        check(rt_ok, "[%s] scalar fields round-trip value-exactly" % label)
        check(not any(k in rec for k in internal), "[%s] no internal keys leak into record" % label)

    print("\n== Results: %d PASS, %d FAIL ==" % (_PASS, _FAIL))
    return 1 if _FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
