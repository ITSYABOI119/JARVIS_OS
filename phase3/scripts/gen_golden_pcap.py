#!/usr/bin/env python3
"""
gen_golden_pcap.py - regenerate phase4/console/fixtures/golden.pcap.

Reads golden_telemetry.json and packs each frame via telemetry_fixture
(build_packet / build_pcap_many). The "corrupt" frame is packed finalize=False
with a deliberately wrong CRC so it decodes to crc_ok=False downstream.

Guards before writing: the fixture's meta.fmt and meta.size must both agree with the
receiver's CURRENT-wire format (PKT_SIZE_V13 — derived, so no size literal here can
rot; this file previously carried three hardcoded 262s), AND the canonical zlib CRC
vector must hold — so a struct change can't silently emit a valid-but-wrong-shape
golden. CI gates that the committed golden.pcap matches this output
(golden-fixture-drift gate). Stdlib only.

Run: python3 phase3/scripts/gen_golden_pcap.py
"""

import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telemetry_fixture import (  # noqa: E402
    FMT_V13, PKT_SIZE_V13, build_pcap_many, frame_to_packet)

_HERE = os.path.dirname(os.path.abspath(__file__))
_FIX = os.path.normpath(os.path.join(_HERE, '..', '..', 'phase4', 'console', 'fixtures'))
JSON_PATH = os.path.join(_FIX, 'golden_telemetry.json')
PCAP_PATH = os.path.join(_FIX, 'golden.pcap')


def main():
    with open(JSON_PATH, 'r', encoding='utf-8') as f:
        spec = json.load(f)
    meta = spec['meta']

    # Guards: the fixture's declared shape must match the receiver's CURRENT wire. The expected
    # size is DERIVED from PKT_SIZE_V13, so a future append needs no edit here and no literal in
    # this file can go stale (it used to hardcode 262 in three places).
    assert struct.calcsize(FMT_V13) == PKT_SIZE_V13, "FMT_V13/PKT_SIZE_V13 disagree"
    assert struct.calcsize(meta['fmt']) == PKT_SIZE_V13, (
        "meta.fmt packs to %d, current wire is %d — regenerate the fixture meta"
        % (struct.calcsize(meta['fmt']), PKT_SIZE_V13))
    assert meta['size'] == PKT_SIZE_V13, (
        "meta.size %r != current wire %d" % (meta['size'], PKT_SIZE_V13))
    assert zlib.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926, "zlib CRC vector mismatch"

    frames = []
    for fr in spec['frames']:
        payload, _corrupt = frame_to_packet(fr)
        frames.append((payload, fr.get('ts_s'), fr.get('ts_frac', 0)))

    data = build_pcap_many(frames)
    with open(PCAP_PATH, 'wb') as f:
        f.write(data)
    print("wrote %s (%d frames, %d bytes)" % (PCAP_PATH, len(spec['frames']), len(data)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
