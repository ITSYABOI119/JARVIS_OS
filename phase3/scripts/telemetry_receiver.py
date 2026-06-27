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
    struct format = '<IBBHIIIBBH6QBBBBHHIIHH56s40s2II'  (struct.calcsize == 200)
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
import mimetypes
import os
import queue
import socket
import struct
import sys
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAGIC = 0x4A54454C            # "JTEL" (LE on the wire: 4C 45 54 4A)
FMT = '<IBBHIIIBBH6QBBBBHHIIHH56s40s2II'
PKT_SIZE = 200
LOG_MAX_ENTRIES = 2700        # NVME_LOG_MAX_ENTRIES (no-wrap durable telemetry log)

FLAG_NAMES = {
    0x01: 'MODEL_LOADED',
    0x02: 'FB_DRAWABLE',
    0x04: 'FB_MAPPED',
    0x08: 'HAS_ERROR',
    0x10: 'SELFTEST_PASS',
    0x20: 'MEMORY',
}
KIND_NAMES = {1: 'STATS', 2: 'INFER', 3: 'STATE'}

DEFAULT_PORT = 51000

# Script-relative default so the console serves from ANY working directory
# (repo/phase4/console). An explicit --web-dir is honored as-is.
_DEFAULT_WEB_DIR = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', 'phase4', 'console'))


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
     uptime_ms, infer_active, infer_duty_pct, log_cursor,
     q_total, q_hits, q_infer, q_heartbeat, q_shield, q_errors,
     num_nodes, model_load_pct, fb_bpp, selftest_score,
     fb_w, fb_h, model_size_mb, total_ram_mb,
     infer_gen_tokens, reserved_i, last_text_raw, model_name_raw,
     nvme_total_mb, episodic_count, crc32_field) = struct.unpack(FMT, data)

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
        'infer_active': infer_active,
        'infer_duty_pct': infer_duty_pct,
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
        'nvme_total_mb': nvme_total_mb,
        'episodic_count': episodic_count,
        'log_cursor': log_cursor,
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
    line += ("  RAM=%dMB NVMe=%dMB log=%d/%d  infer=%s duty=%d%%"
             % (d['total_ram_mb'], d['nvme_total_mb'], d['log_cursor'], LOG_MAX_ENTRIES,
                'ACTIVE' if d['infer_active'] else 'idle', d['infer_duty_pct']))
    if d['last_text']:
        line += '  last="%s"' % d['last_text']
    return line


# ---------------------------------------------------------------------------
# N-c-3a: SSE bridge (UDP -> HTTP/SSE) + pcap replay, so a browser console can
# consume the live telemetry (browsers can't read UDP). Pure-logic helpers
# (packet_to_record, iter_pcap_telemetry) are host-tested; the server/producer
# are exercised by the box-free replay smoke.
# ---------------------------------------------------------------------------

# Keys that must NEVER appear in a streamed record (no fabricated telemetry).
BANNED_RECORD_KEYS = ('tok_s', 'tokps', 'gpu', 'tier', 'agents', 'shield_blocked')


def packet_to_record(d: dict, recv_ts: float = 0) -> dict:
    """Decoded-packet dict -> JSON-serializable record for /events.

    ONLY real fields (+ crc_ok, kind_name, flags_list, recv_ts). No fabricated
    keys (no tok/s, GPU, tier, agent grid, "SHIELD blocked"). 'q_shield' is the
    raw check COUNT, not a block count.
    """
    return {
        'recv_ts': recv_ts,
        'version': d['version'],
        'kind': d['kind'],
        'kind_name': d['kind_name'],
        'flags': d['flags'],
        'flags_list': list(d['flags_list']),
        'boot_id': d['boot_id'],
        'seq': d['seq'],
        'uptime_ms': d['uptime_ms'],
        'infer_active': d['infer_active'],
        'infer_duty_pct': d['infer_duty_pct'],
        'q_total': d['q_total'],
        'q_hits': d['q_hits'],
        'q_infer': d['q_infer'],
        'q_heartbeat': d['q_heartbeat'],
        'q_shield': d['q_shield'],
        'q_errors': d['q_errors'],
        'num_nodes': d['num_nodes'],
        'model_load_pct': d['model_load_pct'],
        'fb_w': d['fb_w'],
        'fb_h': d['fb_h'],
        'fb_bpp': d['fb_bpp'],
        'selftest_score': d['selftest_score'],
        'model_size_mb': d['model_size_mb'],
        'total_ram_mb': d['total_ram_mb'],
        'nvme_total_mb': d['nvme_total_mb'],
        'episodic_count': d['episodic_count'],
        'log_cursor': d['log_cursor'],
        'infer_gen_tokens': d['infer_gen_tokens'],
        'model_name': d['model_name'],
        'last_text': d['last_text'],
        'crc_ok': d['crc_ok'],
    }


# legacy pcap global-header magic -> (struct endian prefix, nanosecond timestamps)
_PCAP_MAGICS = {
    b'\xd4\xc3\xb2\xa1': ('<', False),
    b'\xa1\xb2\xc3\xd4': ('>', False),
    b'\x4d\x3c\xb2\xa1': ('<', True),
    b'\xa1\xb2\x3c\x4d': ('>', True),
}
_ETH_IP_UDP_HDR = 42  # Ethernet(14) + IPv4(20) + UDP(8) before the payload
_MAGIC_BYTES = struct.pack('<I', MAGIC)  # JTEL on the wire (b'LETJ')


def iter_pcap_telemetry(path):
    """Yield (recv_ts, decoded_dict) for each telemetry datagram in a legacy pcap.

    Parses the classic pcap format (24-byte global header + 16-byte per-record
    header + frame). Keeps only frames whose UDP payload (frame[42:]) starts
    with the JTEL magic and is >= 200 bytes, then decode_packet()s the first 200
    bytes. For box-free development against a captured stream.
    """
    with open(path, 'rb') as f:
        data = f.read()
    if len(data) < 24:
        raise ValueError("not a pcap (too short)")
    endian_nano = _PCAP_MAGICS.get(data[:4])
    if endian_nano is None:
        raise ValueError("not a pcap (bad global magic)")
    endian, nano = endian_nano
    divisor = 1e9 if nano else 1e6
    rec_hdr = endian + 'IIII'
    off, n = 24, len(data)
    while off + 16 <= n:
        ts_s, ts_frac, incl, _orig = struct.unpack_from(rec_hdr, data, off)
        off += 16
        frame = data[off:off + incl]
        off += incl
        if len(frame) < incl:
            break  # truncated capture
        payload = frame[_ETH_IP_UDP_HDR:]
        if len(payload) >= PKT_SIZE and payload[:4] == _MAGIC_BYTES:
            try:
                d = decode_packet(payload[:PKT_SIZE])
            except ValueError:
                continue
            yield (ts_s + ts_frac / divisor, d)


class TelemetryHub:
    """Thread-safe latest-record store + SSE client fan-out."""

    def __init__(self):
        self._lock = threading.Lock()
        self.latest = None
        self._clients = []  # list[queue.Queue]

    def subscribe(self):
        q = queue.Queue(maxsize=256)
        with self._lock:
            self._clients.append(q)
            return q, self.latest

    def unsubscribe(self, q):
        with self._lock:
            if q in self._clients:
                self._clients.remove(q)

    def publish(self, record):
        with self._lock:
            self.latest = record
            for q in self._clients:
                try:
                    q.put_nowait(record)
                except queue.Full:
                    pass  # slow client: drop rather than block the producer


def _udp_producer(hub, sock):
    while True:
        try:
            data, _addr = sock.recvfrom(2048)
        except OSError:
            return
        try:
            d = decode_packet(data)
        except ValueError:
            continue
        if d['crc_ok']:
            hub.publish(packet_to_record(d, recv_ts=time.time()))


def _replay_producer(hub, path, rate):
    while True:
        emitted = False
        for recv_ts, d in iter_pcap_telemetry(path):
            emitted = True
            if d['crc_ok']:
                hub.publish(packet_to_record(d, recv_ts=recv_ts))
            time.sleep(max(0.0, rate))
        if not emitted:
            time.sleep(1.0)  # empty / non-telemetry pcap: don't busy-loop


class _SSEHandler(BaseHTTPRequestHandler):
    hub = None
    web_dir = _DEFAULT_WEB_DIR

    def log_message(self, *args):  # silence default request logging
        pass

    def do_GET(self):
        path = self.path.split('?', 1)[0]
        if path == '/events':
            self._serve_sse()
        else:
            self._serve_static(path)

    def _serve_sse(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Connection', 'keep-alive')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        q, latest = self.hub.subscribe()
        last_keepalive = time.time()
        try:
            if latest is not None:
                self._send_record(latest)
            while True:
                try:
                    self._send_record(q.get(timeout=1.0))
                except queue.Empty:
                    pass
                if time.time() - last_keepalive >= 15.0:
                    self.wfile.write(b': keepalive\n\n')
                    self.wfile.flush()
                    last_keepalive = time.time()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass  # client went away
        finally:
            self.hub.unsubscribe(q)

    def _send_record(self, rec):
        self.wfile.write(b'data: ' + json.dumps(rec).encode('utf-8') + b'\n\n')
        self.wfile.flush()

    def _serve_static(self, path):
        rel = path.lstrip('/') or 'index.html'
        if '..' in rel.split('/'):
            self.send_error(403, 'forbidden')
            return
        base = os.path.abspath(self.web_dir)
        full = os.path.abspath(os.path.join(base, rel))
        if full != base and not full.startswith(base + os.sep):
            self.send_error(403, 'forbidden')
            return
        if not os.path.isdir(base):
            body = ("[web] web root not found: %s -- pass --web-dir <dir>; "
                    "the /events SSE stream still works.\n" % self.web_dir).encode('utf-8')
            self.send_response(404)
            self.send_header('Content-Type', 'text/plain; charset=utf-8')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if not os.path.isfile(full):
            self.send_error(404, 'not found')
            return
        ctype = mimetypes.guess_type(full)[0] or 'application/octet-stream'
        with open(full, 'rb') as fh:
            body = fh.read()
        self.send_response(200)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class _QuietThreadingHTTPServer(ThreadingHTTPServer):
    """Swallow benign client-disconnect tracebacks (browser closed the page/SSE
    mid request-read — WinError 10053 etc.). Every other error still propagates
    to the default handler."""

    def handle_error(self, request, client_address):
        import sys
        if isinstance(sys.exc_info()[1],
                      (ConnectionAbortedError, ConnectionResetError, BrokenPipeError)):
            return   # benign: client (browser) closed the connection
        super().handle_error(request, client_address)


def _run_sse(args) -> int:
    hub = TelemetryHub()
    if args.replay:
        producer = threading.Thread(target=_replay_producer,
                                    args=(hub, args.replay, args.replay_rate), daemon=True)
        source = "replay %s @ %.2fs" % (args.replay, args.replay_rate)
    else:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((args.bind, args.port))
        producer = threading.Thread(target=_udp_producer, args=(hub, sock), daemon=True)
        source = "live UDP :%d" % args.port
    producer.start()
    _SSEHandler.hub = hub
    _SSEHandler.web_dir = args.web_dir
    httpd = _QuietThreadingHTTPServer((args.bind, args.http_port), _SSEHandler)
    print("SSE bridge: http://%s:%d  (/events stream; static from '%s'; source: %s)"
          % (args.bind or '0.0.0.0', args.http_port, args.web_dir, source), flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0


def _run_replay_stdout(args) -> int:
    last_seq = {}
    while True:
        emitted = False
        for recv_ts, d in iter_pcap_telemetry(args.replay):
            emitted = True
            if not d['crc_ok']:
                print("[CRC-FAIL] seq=%d (replay)" % d['seq'], flush=True)
                continue
            prev = last_seq.get(d['boot_id'])
            if prev is not None and d['seq'] > prev + 1:
                print("[drop] %d packets (seq %d->%d)" % (d['seq'] - prev - 1, prev, d['seq']), flush=True)
            last_seq[d['boot_id']] = d['seq']
            if args.json:
                print(json.dumps(packet_to_record(d, recv_ts)), flush=True)
            else:
                print(format_human(d), flush=True)
            if args.once:
                return 0
            time.sleep(max(0.0, args.replay_rate))
        if not emitted:
            print("[replay] no telemetry packets in %s" % args.replay, flush=True)
            return 1


def _run_live_stdout(args) -> int:
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


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="JARVIS telemetry UDP receiver + SSE bridge (decode/CRC-validate the N-c-1 telemetry_packet_t).")
    ap.add_argument('--bind', default='', help="interface address to bind (default: all)")
    ap.add_argument('--port', type=int, default=DEFAULT_PORT, help="UDP port (default: %d)" % DEFAULT_PORT)
    ap.add_argument('--once', action='store_true', help="print one valid packet, then exit")
    ap.add_argument('--follow', action='store_true', help="stream continuously (default)")
    ap.add_argument('--json', action='store_true', help="emit one JSON object per line")
    ap.add_argument('--sse', action='store_true', help="run the HTTP/SSE bridge for the browser console")
    ap.add_argument('--http-port', type=int, default=8800, help="SSE/HTTP port (default: 8800)")
    ap.add_argument('--web-dir', default=_DEFAULT_WEB_DIR,
                    help="static web root (default: the repo's phase4/console, script-relative)")
    ap.add_argument('--replay', metavar='PCAP', help="replay a captured pcap instead of live UDP (box-free dev)")
    ap.add_argument('--replay-rate', type=float, default=1.0, help="seconds between replayed packets (default: 1.0)")
    args = ap.parse_args(argv)

    if args.sse:
        return _run_sse(args)
    if args.replay:
        return _run_replay_stdout(args)
    return _run_live_stdout(args)


if __name__ == '__main__':
    sys.exit(main())
