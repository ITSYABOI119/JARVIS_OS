#!/usr/bin/env python3
"""
gen_golden_pcap.py - regenerate phase4/console/fixtures/golden.pcap.

Reads golden_telemetry.json and packs each frame via telemetry_fixture
(build_packet / build_pcap_many). The "corrupt" frame is packed finalize=False
with a deliberately wrong CRC so it decodes to crc_ok=False downstream.

Guards before writing: struct.calcsize(fmt)==200 (both FMT and the fixture's
meta.fmt) AND the canonical zlib CRC vector — so a struct change can't silently
emit a valid-but-wrong-shape golden. CI gates that the committed golden.pcap
matches this output (golden-fixture-drift gate). Stdlib only.

Run: python3 phase3/scripts/gen_golden_pcap.py
"""

import json
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from telemetry_fixture import FMT, build_pcap_many, frame_to_packet  # noqa: E402

_HERE = os.path.dirname(os.path.abspath(__file__))
_FIX = os.path.normpath(os.path.join(_HERE, '..', '..', 'phase4', 'console', 'fixtures'))
JSON_PATH = os.path.join(_FIX, 'golden_telemetry.json')
PCAP_PATH = os.path.join(_FIX, 'golden.pcap')


def main():
    with open(JSON_PATH, 'r', encoding='utf-8') as f:
        spec = json.load(f)
    meta = spec['meta']

    # guards: the packet shape can't have drifted under us
    assert struct.calcsize(FMT) == 200, "FMT no longer 200 bytes"
    assert struct.calcsize(meta['fmt']) == 200, "meta.fmt no longer 200 bytes"
    assert meta['size'] == 200, "meta.size != 200"
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
