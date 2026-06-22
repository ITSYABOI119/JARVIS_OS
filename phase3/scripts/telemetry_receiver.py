#!/usr/bin/env python3
"""
telemetry_receiver.py - JARVIS Remote Telemetry Console receiver (goal #2b N-c-2)

Main-PC Python UDP receiver for the box-side telemetry stream. The JARVIS box
(headless appliance) broadcasts a 200-byte binary `telemetry_packet_t` over UDP
to 255.255.255.255:51000 at ~1 Hz (see phase3/src/drivers/jarvis_telemetry.h and
the N-c-1 emit site in phase3/src/sel4/main_x86.c, shipped at b9d689a). This tool
binds the port, decodes each datagram, validates the zlib CRC-32 over the first
196 bytes, and pretty-prints honest live box state.

Wire format (little-endian, packed, no padding):
    struct format = '<IBBHIIII6QBBBBHHIIHH56s40s2II'  (struct.calcsize == 200)
    crc32 is the last 4 bytes; valid iff zlib.crc32(pkt[:196]) == pkt.crc32.

Usage:
    python3 telemetry_receiver.py [--bind ADDR] [--port 51000] [--once] [--follow] [--json]
      --bind    interface address to bind (default: '' = all interfaces)
      --port    UDP port (default: 51000)
      --once    print one valid packet, then exit
      --follow  stream continuously (default behaviour)
      --json    emit one JSON object per line (for the N-c-3 console bridge)

HONESTY: only real fields are shown — no tok/s, no GPU, no "SHIELD blocked"
(shield= is a check COUNT, not a block count), no "formally verified". The
uptime is from an uncalibrated TSC, shown with "≈".
"""

import argparse
import json
import socket
import struct
import sys
import zlib

MAGIC = 0x4A54454C            # "JTEL" (LE on the wire: 4C 45 54 4A)
FMT = '<IBBHIIII6QBBBBHHIIHH56s40s2II'
PKT_SIZE = 200

FLAG_NAMES = {
    0x01: 'MODEL_LOADED',
    0x02: 'FB_DRAWABLE',
    0x04: 'FB_MAPPED',
    0x08: 'HAS_ERROR',
    0x10: 'SELFTEST_PASS',
}
KIND_NAMES = {1: 'STATS', 2: 'INFER', 3: 'STATE'}

DEFAULT_PORT = 51000


def _cstr(raw: bytes) -> str:
    """Decode a NUL-terminated fixed-width field to a stripped ASCII string."""
    return raw.split(b'\x00', 1)[0].decode('ascii', 'replace').strip()


def decode_packet(data: bytes) -> dict:
    """Decode a telemetry datagram into a dict.

    Raises ValueError for structurally invalid input (wrong length or wrong
    magic). For a well-formed packet with a bad checksum it does NOT raise — it
    returns the dict with 'crc_ok' == False (so the caller can distinguish noise
    on the port from genuine corruption).
    """
    if len(data) != PKT_SIZE:
        raise ValueError("bad length %d (expected %d)" % (len(data), PKT_SIZE))

    (magic, version, kind, flags, boot_id, seq,
     uptime_ms, reserved_t,
     q_total, q_hits, q_infer, q_heartbeat, q_shield, q_errors,
     num_nodes, model_load_pct, fb_bpp, selftest_score,
     fb_w, fb_h, model_size_mb, total_ram_mb,
     infer_gen_tokens, reserved_i, last_text_raw, model_name_raw,
     reserved2_0, reserved2_1, crc32_field) = struct.unpack(FMT, data)

    if magic != MAGIC:
        raise ValueError("bad magic 0x%08X (expected 0x%08X)" % (magic, MAGIC))

    crc_calc = zlib.crc32(data[:196]) & 0xFFFFFFFF
    flags_list = [name for bit, name in FLAG_NAMES.items() if flags & bit]

    return {
        'magic': magic,
        'version': version,
        'kind': kind,
        'kind_name': KIND_NAMES.get(kind, '?%d' % kind),
        'flags': flags,
        'flags_list': flags_list,
        'boot_id': boot_id,
        'seq': seq,
        'uptime_ms': uptime_ms,
        'q_total': q_total,
        'q_hits': q_hits,
        'q_infer': q_infer,
        'q_heartbeat': q_heartbeat,
        'q_shield': q_shield,
        'q_errors': q_errors,
        'num_nodes': num_nodes,
        'model_load_pct': model_load_pct,
        'fb_bpp': fb_bpp,
        'selftest_score': selftest_score,
        'fb_w': fb_w,
        'fb_h': fb_h,
        'model_size_mb': model_size_mb,
        'total_ram_mb': total_ram_mb,
        'infer_gen_tokens': infer_gen_tokens,
        'last_text': _cstr(last_text_raw),
        'model_name': _cstr(model_name_raw),
        'crc32': crc32_field,
        'crc_calc': crc_calc,
        'crc_ok': crc_calc == crc32_field,
    }


def format_human(d: dict) -> str:
    """One compact, honest line. Only real fields — nothing fabricated."""
    line = (
        "[%s seq=%d boot=%d] up≈%.1fs  "
        "q=%d (hit=%d infer=%d hb=%d shield=%d err=%d)  "
        "NN=%d  model=\"%s\" load=%d%%  "
        "fb=%dx%dx%d  self=%d/5  CRC=%s"
        % (
            d['kind_name'], d['seq'], d['boot_id'], d['uptime_ms'] / 1000.0,
            d['q_total'], d['q_hits'], d['q_infer'], d['q_heartbeat'], d['q_shield'], d['q_errors'],
            d['num_nodes'], d['model_name'], d['model_load_pct'],
            d['fb_w'], d['fb_h'], d['fb_bpp'], d['selftest_score'],
            'OK' if d['crc_ok'] else 'FAIL',
        )
    )
    if d['last_text']:
        line += '  last="%s"' % d['last_text']
    return line


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="JARVIS telemetry UDP receiver (decode + CRC validate the N-c-1 telemetry_packet_t).")
    ap.add_argument('--bind', default='', help="interface address to bind (default: all)")
    ap.add_argument('--port', type=int, default=DEFAULT_PORT, help="UDP port (default: %d)" % DEFAULT_PORT)
    ap.add_argument('--once', action='store_true', help="print one valid packet, then exit")
    ap.add_argument('--follow', action='store_true', help="stream continuously (default)")
    ap.add_argument('--json', action='store_true', help="emit one JSON object per line")
    args = ap.parse_args(argv)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))
    if not args.json:
        print("listening on %s:%d (udp) ..." % (args.bind or '0.0.0.0', args.port), flush=True)

    last_seq = {}  # boot_id -> last seen seq
    try:
        while True:
            data, addr = sock.recvfrom(2048)
            src = "%s:%d" % addr
            try:
                d = decode_packet(data)
            except ValueError as e:
                print("[skip] %s from %s" % (e, src), flush=True)
                continue
            if not d['crc_ok']:
                print("[CRC-FAIL] seq=%d from %s" % (d['seq'], src), flush=True)
                continue
            prev = last_seq.get(d['boot_id'])
            if prev is not None and d['seq'] > prev + 1:
                print("[drop] %d packets (seq %d->%d)" % (d['seq'] - prev - 1, prev, d['seq']), flush=True)
            last_seq[d['boot_id']] = d['seq']

            if args.json:
                print(json.dumps(d), flush=True)
            else:
                print(format_human(d), flush=True)

            if args.once:
                break
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
