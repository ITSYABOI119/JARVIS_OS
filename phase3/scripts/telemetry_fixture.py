#!/usr/bin/env python3
"""
telemetry_fixture.py - shared packer for the telemetry_packet_t (jarvis_telemetry.h).

Single source for building 200-byte telemetry packets and legacy-pcap captures,
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
    FMT, MAGIC, PKT_SIZE, decode_packet, packet_to_record)

# Field order matches FMT / jarvis_telemetry.h exactly.
_DEFAULTS = dict(
    magic=MAGIC, version=1, kind=1, flags=0x01 | 0x10, boot_id=1, seq=42,
    uptime_ms=120000, infer_active=0, infer_duty_pct=18, log_cursor=137,
    q_total=289, q_hits=211, q_infer=29, q_heartbeat=40, q_shield=9, q_errors=0,
    num_nodes=6, model_load_pct=100, fb_bpp=32, selftest_score=5,
    fb_w=1024, fb_h=768, model_size_mb=2962, total_ram_mb=30000,
    infer_gen_tokens=0, reserved_i=0,
    last_text=b"hello", model_name=b"Gemma 4 E2B",
    nvme_total_mb=1953892, episodic_count=0, crc32=0,
)


def build_packet(finalize=True, **overrides):
    """Pack a 200-byte packet; when finalize, stamp a valid zlib CRC over [:196].

    Pass finalize=False (with an explicit crc32=...) to forge a corrupt packet.
    String fields (last_text/model_name) accept bytes or str.
    """
    v = dict(_DEFAULTS)
    v.update(overrides)
    for k in ('last_text', 'model_name'):
        if isinstance(v[k], str):
            v[k] = v[k].encode('ascii', 'replace')
    body = struct.pack(
        FMT, v['magic'], v['version'], v['kind'], v['flags'], v['boot_id'], v['seq'],
        v['uptime_ms'], v['infer_active'], v['infer_duty_pct'], v['log_cursor'],
        v['q_total'], v['q_hits'], v['q_infer'], v['q_heartbeat'], v['q_shield'], v['q_errors'],
        v['num_nodes'], v['model_load_pct'], v['fb_bpp'], v['selftest_score'],
        v['fb_w'], v['fb_h'], v['model_size_mb'], v['total_ram_mb'],
        v['infer_gen_tokens'], v['reserved_i'], v['last_text'], v['model_name'],
        v['nvme_total_mb'], v['episodic_count'], v['crc32'])
    if finalize:
        crc = zlib.crc32(body[:196]) & 0xFFFFFFFF
        body = body[:196] + struct.pack('<I', crc)
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
    'HAS_ERROR': 0x08, 'SELFTEST_PASS': 0x10, 'MEMORY': 0x20,
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
