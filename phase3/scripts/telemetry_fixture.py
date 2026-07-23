#!/usr/bin/env python3
"""
telemetry_fixture.py - shared packer for the telemetry_packet_t (jarvis_telemetry.h).

Single source for building 262-byte (v12) telemetry packets and legacy-pcap captures,
used by both test_telemetry_receiver.py (host wire-compat) and gen_golden_pcap.py
(the golden fixture). Stdlib only.

REQUIRED_RECORD_KEYS is DERIVED at import time from packet_to_record() (build a
sample packet -> decode -> record -> its key set), so the key contract can never
drift from the actual receiver output or a guessed hardcoded count.
"""

import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telemetry_receiver import (  # noqa: E402
    FMT_V10, FMT_V11, FMT_V12, MAGIC, PKT_SIZE_V10, PKT_SIZE_V11, PKT_SIZE_V12,
    decode_packet, packet_to_record)

# Field order matches FMT_V12 / jarvis_telemetry.h exactly.
_DEFAULTS = dict(
    magic=MAGIC, version=12, kind=1, flags=0x01 | 0x10, boot_id=1, seq=42,
    uptime_ms=120000, infer_active=0, infer_duty_pct=18, log_cursor=137,
    q_total=289, q_hits=211, q_infer=29, q_heartbeat=40, q_shield=9, q_errors=0,
    num_nodes=6, model_load_pct=100, fb_bpp=32, selftest_score=5,
    fb_w=1024, fb_h=768, model_size_mb=2962, total_ram_mb=30000,
    infer_gen_tokens=0, cache_growth_count=0,
    last_text=b"hello", model_name=b"Gemma 4 E2B",
    nvme_total_mb=1953892, episodic_count=0, pool_events=0, pool_decisions=0,
    retrieval_hits=0, retrieval_latency_us=0, infer_last_tok_x100=0,
    shield_learn_keys=0, shield_learn_max_risk_x100=0, semantic_fact_count=0,
    restart_count=0, actions_fired=0, actions_blocked=0,
    monitors_fired=0, last_monitor_event=0, mon_pad=0,
    wakes_fired=0, last_wake_event=0, wake_pad=0,
    behaviors_fired=0, behaviors_mask=0, last_behavior=0, beh_pad=0,
    control_in_answered=0, control_in_blocked=0, control_in_dropped=0,   # v11 (6-5/M3-4a)
    # v12 (6-6/B/M2) — ROUTING DECISIONS at classification time; NOT a breakdown of
    # control_in_answered (an INFER decision that later degrades/times out never reaches it).
    route_sysfacts=0, route_decline=0, route_infer=0, route_inited=0, route_pad=0,
    crc32=0,
)


def build_packet(finalize=True, wire_version=12, **overrides):
    """Pack a telemetry packet; when finalize, stamp a valid zlib CRC over [:size-4].

    wire_version=12 (default) -> a 262-byte v12 packet (+ route_sysfacts/decline/infer/inited/pad);
    wire_version=11 -> a 254-byte v11 packet (the live deployed box's shape until the B flip, for
    the deploy-deferral version-tolerance test); wire_version=10 -> a 246-byte v10 packet. The
    version BYTE tracks wire_version unless explicitly overridden.
    Pass finalize=False (with an explicit crc32=...) to forge a corrupt packet.
    String fields (last_text/model_name) accept bytes or str.
    """
    v = dict(_DEFAULTS)
    v.update(overrides)
    if 'version' not in overrides:
        v['version'] = wire_version
    for k in ('last_text', 'model_name'):
        if isinstance(v[k], str):
            v[k] = v[k].encode('ascii', 'replace')
    common = (
        v['magic'], v['version'], v['kind'], v['flags'], v['boot_id'], v['seq'],
        v['uptime_ms'], v['infer_active'], v['infer_duty_pct'], v['log_cursor'],
        v['q_total'], v['q_hits'], v['q_infer'], v['q_heartbeat'], v['q_shield'], v['q_errors'],
        v['num_nodes'], v['model_load_pct'], v['fb_bpp'], v['selftest_score'],
        v['fb_w'], v['fb_h'], v['model_size_mb'], v['total_ram_mb'],
        v['infer_gen_tokens'], v['cache_growth_count'], v['last_text'], v['model_name'],
        v['nvme_total_mb'], v['episodic_count'], v['pool_events'], v['pool_decisions'],
        v['retrieval_hits'], v['retrieval_latency_us'], v['infer_last_tok_x100'],
        v['shield_learn_keys'], v['shield_learn_max_risk_x100'], v['semantic_fact_count'],
        v['restart_count'], v['actions_fired'], v['actions_blocked'],
        v['monitors_fired'], v['last_monitor_event'], v['mon_pad'],
        v['wakes_fired'], v['last_wake_event'], v['wake_pad'],
        v['behaviors_fired'], v['behaviors_mask'], v['last_behavior'], v['beh_pad'])
    if wire_version == 10:
        body = struct.pack(FMT_V10, *common, v['crc32'])
        size = PKT_SIZE_V10
    elif wire_version == 11:
        body = struct.pack(FMT_V11, *common,
                           v['control_in_answered'], v['control_in_blocked'],
                           v['control_in_dropped'], v['crc32'])
        size = PKT_SIZE_V11
    else:
        body = struct.pack(FMT_V12, *common,
                           v['control_in_answered'], v['control_in_blocked'],
                           v['control_in_dropped'],
                           v['route_sysfacts'], v['route_decline'], v['route_infer'],
                           v['route_inited'], v['route_pad'], v['crc32'])
        size = PKT_SIZE_V12
    if finalize:
        crc = zlib.crc32(body[:size - 4]) & 0xFFFFFFFF   # v12: 258 / v11: 250 / v10: 242
        body = body[:size - 4] + struct.pack('<I', crc)
    return body


def _build_pcap_one(payload, ts_s=1700000001, ts_frac=0):
    """A 1-record legacy pcap (little-endian, microsecond): 42-byte zero
    L2/L3/L4 header + the telemetry payload, for iter_pcap_telemetry."""
    return build_pcap_many([(payload, ts_s, ts_frac)])


def build_pcap_many(frames, ts_start=1700000000):
    """Build one legacy pcap (LE, microsecond) from N frames.

    frames: iterable of (payload_bytes, ts_s, ts_frac). When ts_s is None the
    timestamp auto-increments from ts_start so per-frame timestamps strictly
    increase. Each frame is a 42-byte zero L2/L3/L4 header + the payload.
    """
    glob = struct.pack('<IHHiIII', 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1)
    out = [glob]
    for i, (payload, ts_s, ts_frac) in enumerate(frames):
        if ts_s is None:
            ts_s = ts_start + i
        frame = b'\x00' * 42 + payload
        out.append(struct.pack('<IIII', ts_s, ts_frac or 0, len(frame), len(frame)) + frame)
    return b''.join(out)


# flag name -> bit (mirrors jarvis_telemetry.h TLM_F_*).
FLAG_BITS = {
    'MODEL_LOADED': 0x01, 'FB_DRAWABLE': 0x02, 'FB_MAPPED': 0x04,
    'HAS_ERROR': 0x08, 'SELFTEST_PASS': 0x10, 'MEMORY': 0x20, 'CONTEXT': 0x40,
    'RETRIEVAL': 0x80, 'CACHE_GROWTH': 0x100, 'SHIELD_LEARN': 0x200, 'SEMANTIC': 0x400,
    'ACTIONS': 0x800,
    'MONITORS': 0x1000,
    'WAKE': 0x2000,
    'PROACTIVE': 0x4000,
    'CONTROL_IN': 0x8000,   # v11 (6-5/M3-4a)
}
_CORRUPT_CRC = 0xDEADBEEF  # deliberately wrong CRC for "corrupt" golden frames


def frame_to_packet(frame):
    """Golden-fixture frame dict -> (payload_bytes, is_corrupt).

    Single interpreter for a golden frame, shared by gen_golden_pcap.py and the
    key-contract test so they can never disagree. Non-packet keys
    (label/ts_s/ts_frac/corrupt) are stripped; 'flags' may be a list of names or
    an int. A corrupt frame is packed finalize=False with a wrong CRC so it
    decodes to crc_ok=False downstream.
    """
    f = dict(frame)
    for k in ('label', 'ts_s', 'ts_frac'):
        f.pop(k, None)
    corrupt = bool(f.pop('corrupt', False))
    flags = f.get('flags', 0)
    if isinstance(flags, (list, tuple)):
        bits = 0
        for name in flags:
            bits |= FLAG_BITS[name]
        f['flags'] = bits
    if corrupt:
        f['crc32'] = _CORRUPT_CRC
        return build_packet(finalize=False, **f), True
    return build_packet(finalize=True, **f), False


# Derived at import time from the actual receiver output — the /events key contract.
REQUIRED_RECORD_KEYS = tuple(packet_to_record(decode_packet(build_packet())).keys())
