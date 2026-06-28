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
    decode_packet, packet_to_record, iter_pcap_telemetry, FMT, MAGIC, PKT_SIZE, FLAG_NAMES)
from telemetry_fixture import (  # noqa: E402  -- shared packer (moved out of this file)
    _DEFAULTS, build_packet, _build_pcap_one, build_pcap_many, REQUIRED_RECORD_KEYS,
    frame_to_packet, FLAG_BITS)

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
    check(struct.calcsize(FMT) == 216, "struct.calcsize(FMT) == 216 (v3)")
    check(PKT_SIZE == 216, "PKT_SIZE == 216 (v3)")

    # Canonical zlib CRC vector — same CRC the C side proved (jarvis_telemetry.c)
    check(zlib.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926,
          "canonical zlib CRC of \"123456789\" == 0xCBF43926")

    # Valid packet round-trips
    pkt = build_packet()
    check(len(pkt) == 216, "built packet is 216 bytes (v3)")
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

    # Corrupt one byte in [0:212] -> crc_ok False (NOT a crash, NOT ValueError)
    ba = bytearray(pkt)
    ba[50] ^= 0xFF
    dc = decode_packet(bytes(ba))
    check(dc['crc_ok'] is False, "single-byte corruption -> crc_ok False (no crash)")

    # Wrong magic -> ValueError
    check(raises_valueerror(lambda: decode_packet(build_packet(magic=0xDEADBEEF))),
          "wrong magic raises ValueError")

    # Wrong length -> ValueError
    check(raises_valueerror(lambda: decode_packet(pkt[:207]), ), "207-byte input raises ValueError")
    check(raises_valueerror(lambda: decode_packet(pkt + b'\x00')), "217-byte input raises ValueError")

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

    # --- M4: episodic_count + TLM_F_MEMORY (reserved2 repurposed, no size bump) ---
    pkt_mem = build_packet(episodic_count=4242, flags=0x01 | 0x10 | 0x20)
    dmem = decode_packet(pkt_mem)
    check(dmem['crc_ok'] is True, "episodic packet crc_ok True")
    check(dmem['episodic_count'] == 4242, "episodic_count decodes (== 4242)")
    check('MEMORY' in dmem['flags_list'], "TLM_F_MEMORY 0x20 -> 'MEMORY' in flags_list")
    rec_mem = packet_to_record(dmem)
    check(rec_mem['episodic_count'] == 4242, "record carries episodic_count (contract key)")
    check('episodic_count' in REQUIRED_RECORD_KEYS, "episodic_count is a REQUIRED_RECORD_KEY")

    # --- G2/M4: pool_events/pool_decisions + TLM_F_CONTEXT (the v2 200->208 size-bump) ---
    pkt_ctx = build_packet(pool_events=314, pool_decisions=159, flags=0x01 | 0x10 | 0x40)
    dctx = decode_packet(pkt_ctx)
    check(dctx['crc_ok'] is True, "context packet crc_ok True (CRC over [:212] — the gotcha)")
    check(dctx['pool_events'] == 314 and dctx['pool_decisions'] == 159, "pool_events/pool_decisions decode")
    check('CONTEXT' in dctx['flags_list'], "TLM_F_CONTEXT 0x40 -> 'CONTEXT' in flags_list")
    rec_ctx = packet_to_record(dctx)
    check(rec_ctx['pool_events'] == 314 and rec_ctx['pool_decisions'] == 159, "record carries pool fields")
    check('pool_events' in REQUIRED_RECORD_KEYS and 'pool_decisions' in REQUIRED_RECORD_KEYS,
          "pool_events/pool_decisions are REQUIRED_RECORD_KEYs")

    # --- G3/M4: retrieval_hits/retrieval_latency_us + TLM_F_RETRIEVAL (the v3 208->216 size-bump) ---
    check(PKT_SIZE == 216, "v3 size-bump: PKT_SIZE == 216")
    pkt_retr = build_packet(retrieval_hits=3, retrieval_latency_us=40, flags=0x01 | 0x10 | 0x80)
    dretr = decode_packet(pkt_retr)
    check(dretr['crc_ok'] is True, "v3 retrieval packet crc_ok True (CRC over [:212])")
    check(dretr['version'] == 3, "v3 packet version == 3")
    check(dretr['retrieval_hits'] == 3 and dretr['retrieval_latency_us'] == 40,
          "retrieval_hits/retrieval_latency_us decode")
    check('RETRIEVAL' in dretr['flags_list'], "TLM_F_RETRIEVAL 0x80 -> 'RETRIEVAL' in flags_list")
    rec_retr = packet_to_record(dretr)
    check(rec_retr['retrieval_hits'] == 3 and rec_retr['retrieval_latency_us'] == 40,
          "record carries retrieval fields")
    check('retrieval_hits' in REQUIRED_RECORD_KEYS and 'retrieval_latency_us' in REQUIRED_RECORD_KEYS,
          "retrieval_hits/retrieval_latency_us are REQUIRED_RECORD_KEYs")
    check(FLAG_NAMES.get(0x80) == 'RETRIEVAL' and FLAG_BITS.get('RETRIEVAL') == 0x80,
          "FLAG_NAMES/FLAG_BITS both carry 0x80 RETRIEVAL")

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
    check(golden['meta']['size'] == 216 and golden['meta']['fmt'] == FMT,
          "golden meta fmt/size match the wire format")

    kind_expect = {1: 'STATS', 2: 'INFER', 3: 'STATE'}
    internal = ('magic', 'crc32', 'crc_calc', 'reserved_i')
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
