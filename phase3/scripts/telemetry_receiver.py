#!/usr/bin/env python3
"""
telemetry_receiver.py - JARVIS Remote Telemetry Console receiver (goal #2b N-c-2)

Main-PC Python UDP receiver for the box-side telemetry stream. The JARVIS box
(headless appliance) broadcasts a 254-byte (v11) binary `telemetry_packet_t` over UDP
to 255.255.255.255:51000 at ~1 Hz (see phase3/src/drivers/jarvis_telemetry.h and
the N-c-1 emit site in phase3/src/sel4/main_x86.c). This tool binds the port,
decodes each datagram, validates the zlib CRC-32 over the first 250 bytes (v11) / 242 (v10),
and pretty-prints honest live box state.

Wire format (little-endian, packed, no padding — see FMT below; sizes are DERIVED,
never hardcoded here):
    crc32 is the last 4 bytes; valid iff zlib.crc32(pkt[:PKT_SIZE-4]) == pkt.crc32.

Usage:
    python3 telemetry_receiver.py [--bind ADDR] [--port 51000] [--once] [--follow] [--json]
      --bind    interface address to bind (default: '' = all interfaces)
      --port    UDP port (default: 51000)
      --once    print one valid packet, then exit
      --follow  stream continuously (default behaviour)
      --json    emit one JSON object per line (for the N-c-3 console bridge)

    Two-way SEND path (Phase 6 goal 6-5 / M3-4b — RECEIVER-AS-SIGNER):
    python3 telemetry_receiver.py --sse --send --key-file <32-byte key>
      --send      enable POST /send: sign a JCTL control-IN frame and transmit it to the box,
                  and listen on UDP 51002 for the box's JRPL reply (pushed to the browser over
                  the SAME /events SSE stream). Requires --sse.
      --key-file  path to the 32-byte raw HMAC-SHA256 key (the box's JKEY slot). FAIL-CLOSED:
                  without a valid key the send path is disabled and the DISPLAY half still runs.
      --epoch     sender boot epoch (default: 0x4A320001 == CONTROL_TEST_EPOCH)
      --box-mac / --box-ip / --reply-port / --http-bind

    NOTE --send needs ELEVATION: the frame is transmitted as a raw L2 Ethernet frame via scapy
    (Windows also needs Npcap), because the box has no ARP and the destination MAC is provisioned.
    The signing endpoint is bound to LOOPBACK ONLY and additionally refuses any non-loopback
    client per request — the browser talks to it over 127.0.0.1, never the LAN. The browser never
    sees the key. Only the telemetry UDP socket keeps listening on the LAN.

HONESTY (send path): a 'refused' verdict from the box is a DEFINED-ABUSE-CLASS refusal. General
prompt injection is contained STRUCTURALLY (inbound text can never mint an action; answers go only
to the provisioned console) — it is NOT detected. Since 6-5/M4b the box->console reply is
AUTHENTICATED: JRPL v2 carries an HMAC-SHA256 tag over the whole CRC'd payload, verified here in
constant time, and a reply that fails auth (or that arrives with no key to check it against) is
DROPPED, never rendered. That is SIGNED, NOT ENCRYPTED — the answer text is plaintext on the wire,
so M4b stops a FORGED reply, not eavesdropping. Telemetry-OUT is unchanged: still a CRC-only
broadcast, by design (non-sensitive).

HONESTY: only real fields are shown — no GPU, no "SHIELD blocked" (shield= is
a check COUNT, not a block count), no "formally verified". Since v4 a REAL
measured last-inference tok/s exists (infer_last_tok_x100, RDTSC-measured in
Process B — never the 5.46 benchmark constant); the fabricated 'tok_s'/'tokps'
aliases stay banned. The v5 shield_learn_* fields are the SHIELD
failure-learning MONITOR signal (learned-risk counts — queries are never blocked;
'shield_blocked' stays banned). The v6 semantic_fact_count is the distilled
semantic-fact count (observable repeated Q&A patterns compacted by the
deterministic distill — never "knows preferences"). The v7
restart_count/actions_fired/actions_blocked are the Phase-6 self-heal/ACTION-gate
activity (PB restarts + allowlisted actions executed/blocked by the SEPARATE
action gate); actions_blocked is a real, allowed key — distinct from the still-banned
query-path 'shield_blocked'. The v8 monitors_fired/last_monitor_event are the
always-on-monitor NOTIFY activity (a NEUTRAL debounced event count — a mix of
degradation and benign liveness events, never "anomalies detected"). The v9
wakes_fired/last_wake_event are the event-driven-wake CONSULT activity
(DISPATCHED consults only — a consult is a fixed, human-reviewed question per
monitor event, cache-served or one bounded inference — never
"thinking"/"reasoning"). The v10 behaviors_fired/behaviors_mask/last_behavior
are the proactive-behavior INFORM activity (behaviors_fired = user interrupts;
mask bit id-1 = behavior id has fired this boot; a B1/B2 consult bumps BOTH
wakes_fired and behaviors_fired by design — two honest views of one event,
never summed). The uptime is from an uncalibrated TSC, shown with "≈".
"""

import argparse
import hashlib
import hmac
import json
import mimetypes
import os
import queue
import random
import socket
import struct
import sys
import threading
import time
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MAGIC = 0x4A54454C            # "JTEL" (LE on the wire: 4C 45 54 4A)
# VERSION-TOLERANT (6-5/M3-4a, extended 6-6/B/M2): the wire is APPEND-ONLY, so the common prefix
# through beh_pad is shared; v11 appends control_in_answered/blocked/dropped and v12 further appends
# route_sysfacts/decline/infer/inited/pad, each before crc32. The deploy-deferral keeps a LIVE v11
# box on the wire while the code emits v12 — the receiver must decode ALL THREE by length, and an
# older packet must NOT misparse (its newer fields -> None, never a fabricated 0).
FMT_COMMON = '<IBBHIIIBBH6QBBBBHHIIHH56s40s6IHHHHIHHHBBHBBHHBB'   # through beh_pad, NO crc
FMT_CTRL_IN = 'HHI'             # control_in_answered(H)/blocked(H)/dropped(I)  (v11+)
FMT_ROUTE   = 'HHHBB'           # route_sysfacts(H)/decline(H)/infer(H)/inited(B)/pad(B)  (v12+)
FMT_V10 = FMT_COMMON + 'I'                            # v10: + crc32
FMT_V11 = FMT_COMMON + FMT_CTRL_IN + 'I'              # v11: + control_in_* + crc32
FMT_V12 = FMT_COMMON + FMT_CTRL_IN + FMT_ROUTE + 'I'  # v12: + route_* + crc32
PKT_SIZE_V10 = struct.calcsize(FMT_V10)   # 246
PKT_SIZE_V11 = struct.calcsize(FMT_V11)   # 254
PKT_SIZE_V12 = struct.calcsize(FMT_V12)   # 262
# CURRENT-wire aliases (the code emits v12 now) — derived, never a hardcoded size.
FMT = FMT_V12
PKT_SIZE = PKT_SIZE_V12
LOG_MAX_ENTRIES = 2700        # NVME_LOG_MAX_ENTRIES (no-wrap durable telemetry log)

FLAG_NAMES = {
    0x01: 'MODEL_LOADED',
    0x02: 'FB_DRAWABLE',
    0x04: 'FB_MAPPED',
    0x08: 'HAS_ERROR',
    0x10: 'SELFTEST_PASS',
    0x20: 'MEMORY',
    0x40: 'CONTEXT',
    0x80: 'RETRIEVAL',
    0x100: 'CACHE_GROWTH',
    0x200: 'SHIELD_LEARN',
    0x400: 'SEMANTIC',
    0x800: 'ACTIONS',
    0x1000: 'MONITORS',
    0x2000: 'WAKE',
    0x4000: 'PROACTIVE',
    0x8000: 'CONTROL_IN',   # v11 (6-5/M3-4a): the two-way control-IN channel is up (gated off in the deploy)
}
KIND_NAMES = {1: 'STATS', 2: 'INFER', 3: 'STATE'}

DEFAULT_PORT = 51000

# ---------------------------------------------------------------------------
# 6-5/M3-4b: control-IN wire constants.
# AUTHORITATIVE SOURCE: phase3/src/net/control_msg.h (outbound JCTL request) and the
# ctrl_send_reply() builder in phase3/src/net/control_reply.c (inbound JRPL reply). Keep in sync.
#
# 6-5/M4b — the JRPL reply is v2 and AUTHENTICATED (HMAC-SHA256):
#     0      4     "JRPL"
#     4      1     version = 2
#     5      1     verdict (0 answered / 1 refused / 2 degraded / 3 failed)
#     6      2     seq   u16 LE (the request seq truncated to u16)
#     8      2     tlen  u16 LE
#     10     tlen  text (printable-sanitized, <= CTRL_REPLY_TEXT_MAX)
#     10+tlen  4   crc32 u32 LE = zlib.crc32(payload[0 : 10+tlen])
#     14+tlen  32  tag = HMAC-SHA256(key, payload[0 : 14+tlen])
#   TOTAL = 46 + tlen.
# The HMAC span DELIBERATELY INCLUDES the CRC bytes, so the tag authenticates the exact bytes
# the receiver parses. Acceptance requires BOTH crc_ok AND hmac_ok.
#
# SIGNED, NOT ENCRYPTED. The tag proves the box authored these bytes and that nobody altered
# them; the text itself is PLAINTEXT on the wire and anyone who captures the frame can read it.
# M4b stops SPOOFING (a forged "answer from JARVIS"), not eavesdropping.
# ---------------------------------------------------------------------------
CONTROL_MAGIC = 0x4A43544C        # "JCTL", BIG-endian on the wire
CONTROL_VERSION = 1
CONTROL_HDR_LEN = 36              # magic..query_len inclusive; query starts at 36
CONTROL_NONCE_LEN = 16
CONTROL_TAG_LEN = 32
CONTROL_KEY_LEN = 32              # HMAC-SHA256 key (the box's JKEY slot)
CONTROL_MSG_MAX = 240
CONTROL_QUERY_MAX = CONTROL_MSG_MAX - CONTROL_HDR_LEN - CONTROL_TAG_LEN   # 172
CONTROL_PORT = 51001              # box inbound (console -> box)
CONTROL_REPLY_PORT = 51002        # box -> console
CONTROL_TEST_EPOCH = 0x4A320001   # == CONTROL_TEST_EPOCH in main_x86.c
REPLY_MAGIC = b'JRPL'             # reply header is LITTLE-endian
REPLY_HDR_LEN = 10                # magic(4) ver(1) verdict(1) seq(2) tlen(2); text follows
REPLY_VERSION = 2                 # v2 = HMAC'd. v1 (CRC-only) was NEVER DEPLOYED -> no fallback.
REPLY_CRC_LEN = 4
REPLY_TAG_LEN = 32                # HMAC-SHA256 tag, trailing
CTRL_REPLY_TEXT_MAX = 512         # == CTRL_REPLY_TEXT_MAX box-side
BOX_MAC = '0c:9d:92:0e:39:9a'     # the box I211 MAC (RAL0/RAH0) — provisioned, never resolved
BOX_IP = '192.168.100.143'

# A refused verdict is a DEFINED-ABUSE-CLASS refusal (the box's query SHIELD), never a claim
# that an attack or an injection was detected.
VERDICT_NAMES = {0: 'answered', 1: 'refused', 2: 'degraded', 3: 'failed'}

# LOCALHOST GUARD #1 (per-request peer): the peer addresses the signing endpoint will serve.
LOOPBACK_PEERS = ('127.0.0.1', '::1', '::ffff:127.0.0.1')
# LOCALHOST GUARD #2 (bind): addresses accepted as an explicit loopback HTTP bind. The server
# is AF_INET, so '::1' would pass this check and then fail in bind() with a gaierror — it is
# deliberately NOT listed.
LOOPBACK_BINDS = ('127.0.0.1', 'localhost')
# LOCALHOST GUARD #3 (request provenance): Host names accepted for the signing endpoint.
LOOPBACK_HOSTS = ('127.0.0.1', 'localhost', '[::1]')


def host_is_loopback(host_header):
    """True iff a Host header names a loopback literal (the DNS-rebinding guard).

    Guards #1/#2 prove the TCP connection is local; they cannot tell whether the OPERATOR
    asked for the request. A hostile DNS name that resolves to 127.0.0.1 makes an attacker's
    page genuinely same-origin with this endpoint, so its Origin header would be legitimate
    and pass. Only the Host NAME exposes the rebind.
    """
    h = (host_header or '').strip()
    if h.startswith('['):                 # bracketed IPv6 literal, e.g. [::1]:8800
        h = h.split(']', 1)[0] + ']'
    elif h.count(':') == 1:               # host:port
        h = h.rsplit(':', 1)[0]
    return h.lower() in LOOPBACK_HOSTS


def allowed_origins(port):
    """The only Origins the signing endpoint accepts — the console is served from, and is
    therefore same-origin with, this very endpoint."""
    return ('http://127.0.0.1:%d' % port, 'http://localhost:%d' % port)

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
    # Version-tolerant by LENGTH (the append-only wire): v12 (262 B) has the control_in fields AND
    # the route_* fields; v11 (254 B, still emitted by the live deployed box until the B flip) has
    # only control_in; v10 (246 B) has neither. An older packet decodes cleanly with its newer
    # fields == None — never a fabricated 0. CRC region: @242 (v10) / @250 (v11) / @258 (v12).
    # route_* are ROUTING DECISIONS at classification time, NOT a breakdown of control_in_answered:
    # an INFER decision that later degrades or times out is counted in route_infer but never
    # reaches the answered exit, so the three do not sum to control_in_answered.
    n = len(data)
    if n == PKT_SIZE_V12:
        fields = struct.unpack(FMT_V12, data)
        control_in_answered, control_in_blocked, control_in_dropped = fields[-9:-6]
        route_sysfacts, route_decline, route_infer, route_inited, _route_pad = fields[-6:-1]
        crc32_field = fields[-1]
        common = fields[:-9]
    elif n == PKT_SIZE_V11:
        fields = struct.unpack(FMT_V11, data)
        control_in_answered, control_in_blocked, control_in_dropped = fields[-4:-1]
        route_sysfacts = route_decline = route_infer = route_inited = None
        crc32_field = fields[-1]
        common = fields[:-4]
    elif n == PKT_SIZE_V10:
        fields = struct.unpack(FMT_V10, data)
        control_in_answered = control_in_blocked = control_in_dropped = None
        route_sysfacts = route_decline = route_infer = route_inited = None
        crc32_field = fields[-1]
        common = fields[:-1]
    else:
        raise ValueError("bad length %d (expected %d, %d or %d)"
                         % (n, PKT_SIZE_V10, PKT_SIZE_V11, PKT_SIZE_V12))

    (magic, version, kind, flags, boot_id, seq,
     uptime_ms, infer_active, infer_duty_pct, log_cursor,
     q_total, q_hits, q_infer, q_heartbeat, q_shield, q_errors,
     num_nodes, model_load_pct, fb_bpp, selftest_score,
     fb_w, fb_h, model_size_mb, total_ram_mb,
     infer_gen_tokens, cache_growth_count, last_text_raw, model_name_raw,
     nvme_total_mb, episodic_count, pool_events, pool_decisions,
     retrieval_hits, retrieval_latency_us, infer_last_tok_x100,
     shield_learn_keys, shield_learn_max_risk_x100, semantic_fact_count,
     restart_count, actions_fired, actions_blocked,
     monitors_fired, last_monitor_event, _mon_pad,
     wakes_fired, last_wake_event, _wake_pad,
     behaviors_fired, behaviors_mask, last_behavior, _beh_pad) = common

    if magic != MAGIC:
        raise ValueError("bad magic 0x%08X (expected 0x%08X)" % (magic, MAGIC))

    crc_calc = zlib.crc32(data[:n - 4]) & 0xFFFFFFFF   # offsetof(crc32): 242 (v10) / 250 (v11) / 258 (v12)
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
        'pool_events': pool_events,
        'pool_decisions': pool_decisions,
        'retrieval_hits': retrieval_hits,
        'retrieval_latency_us': retrieval_latency_us,
        'infer_last_tok_x100': infer_last_tok_x100,
        'shield_learn_keys': shield_learn_keys,
        'shield_learn_max_risk_x100': shield_learn_max_risk_x100,
        'semantic_fact_count': semantic_fact_count,
        'restart_count': restart_count,
        'actions_fired': actions_fired,
        'actions_blocked': actions_blocked,
        'monitors_fired': monitors_fired,
        'last_monitor_event': last_monitor_event,
        'wakes_fired': wakes_fired,
        'last_wake_event': last_wake_event,
        'behaviors_fired': behaviors_fired,
        'behaviors_mask': behaviors_mask,
        'last_behavior': last_behavior,
        # v11 (6-5/M3-4a): None on a v10 packet from the live deployed box (append-only, deploy-deferred).
        # control_in_blocked is a DEFINED-ABUSE-CLASS refuse count, NEVER "injection blocked".
        'control_in_answered': control_in_answered,
        'control_in_blocked': control_in_blocked,
        'control_in_dropped': control_in_dropped,
        # v12 (6-6/B/M2): ROUTING DECISIONS on the control-IN path, counted at classification
        # time. NOT a breakdown of control_in_answered — never render/sum them as one.
        'route_sysfacts': route_sysfacts,
        'route_decline': route_decline,
        'route_infer': route_infer,
        'route_inited': route_inited,
        'cache_growth_count': cache_growth_count,
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
    raw check COUNT, not a block count; the v5 shield_learn_* fields are the
    failure-learning MONITOR signal (learned-risk counts, never blocks).
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
        'pool_events': d['pool_events'],
        'pool_decisions': d['pool_decisions'],
        'retrieval_hits': d['retrieval_hits'],
        'retrieval_latency_us': d['retrieval_latency_us'],
        'infer_last_tok_x100': d['infer_last_tok_x100'],
        'shield_learn_keys': d['shield_learn_keys'],
        'shield_learn_max_risk_x100': d['shield_learn_max_risk_x100'],
        'semantic_fact_count': d['semantic_fact_count'],
        'restart_count': d['restart_count'],
        'actions_fired': d['actions_fired'],
        'actions_blocked': d['actions_blocked'],
        'monitors_fired': d['monitors_fired'],
        'last_monitor_event': d['last_monitor_event'],
        'wakes_fired': d['wakes_fired'],
        'last_wake_event': d['last_wake_event'],
        'behaviors_fired': d['behaviors_fired'],
        'behaviors_mask': d['behaviors_mask'],
        'last_behavior': d['last_behavior'],
        'control_in_answered': d['control_in_answered'],
        'control_in_blocked': d['control_in_blocked'],
        'control_in_dropped': d['control_in_dropped'],
        # v12: routing DECISIONS (see decode_packet) — not a breakdown of answered.
        'route_sysfacts': d['route_sysfacts'],
        'route_decline': d['route_decline'],
        'route_infer': d['route_infer'],
        'route_inited': d['route_inited'],
        'cache_growth_count': d['cache_growth_count'],
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
    with the JTEL magic and is >= PKT_SIZE bytes, then decode_packet()s the
    first PKT_SIZE bytes. For box-free development against a captured stream.
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


# ---------------------------------------------------------------------------
# 6-5/M3-4b: the SEND half — sign a JCTL control-IN frame, transmit it, decode the
# box's JRPL reply. RECEIVER-AS-SIGNER: the browser cannot hold the HMAC key and cannot
# emit raw L2 frames, so the signing lives here behind a loopback-only endpoint.
# ---------------------------------------------------------------------------


class ScapyUnavailable(RuntimeError):
    """Raised when the raw L2 send path is unusable (scapy/Npcap missing, no route,
    not elevated). Mapped to HTTP 503 — an environment fact, not a bad request."""


def build_control_frame(key: bytes, seq: int, epoch: int, query: bytes, nonce: bytes) -> bytes:
    """Build a signed JCTL control-IN datagram.

    Layout is AUTHORITATIVELY defined by phase3/src/net/control_msg.h (all multi-byte
    header fields network BIG-endian):

        0   4   magic "JCTL" | 4  1  version=1 | 5  1  flags=0 | 6  8  seq (u64)
        14  4   boot_epoch   | 18 16 nonce     | 34 2  query_len (u16) | 36 Q query
        36+Q 32 tag = HMAC-SHA256(key, payload[0 : 36+Q])   <- the tag is never in its own input

    Deterministic: the caller supplies the nonce, so a fixed (key, seq, epoch, query,
    nonce) always yields a byte-identical frame (pinned by the golden test).
    """
    if not isinstance(query, (bytes, bytearray)):
        raise ValueError("query must be bytes")
    if not isinstance(key, (bytes, bytearray)) or len(key) != CONTROL_KEY_LEN:
        raise ValueError("key must be exactly %d bytes" % CONTROL_KEY_LEN)
    if not isinstance(nonce, (bytes, bytearray)) or len(nonce) != CONTROL_NONCE_LEN:
        raise ValueError("nonce must be exactly %d bytes" % CONTROL_NONCE_LEN)
    if len(query) > CONTROL_QUERY_MAX:
        raise ValueError("query is %d bytes (max %d)" % (len(query), CONTROL_QUERY_MAX))
    if not (0 <= int(seq) <= 0xFFFFFFFFFFFFFFFF):
        raise ValueError("seq out of u64 range")
    if not (0 <= int(epoch) <= 0xFFFFFFFF):
        raise ValueError("epoch out of u32 range")

    payload = struct.pack('>IBB', CONTROL_MAGIC, CONTROL_VERSION, 0)
    payload += struct.pack('>Q', int(seq))
    payload += struct.pack('>I', int(epoch))
    payload += bytes(nonce)
    payload += struct.pack('>H', len(query))
    payload += bytes(query)
    tag = hmac.new(bytes(key), payload, hashlib.sha256).digest()
    return payload + tag


def decode_control_reply(payload: bytes, key):
    """Decode + AUTHENTICATE a JRPL v2 reply -> (dict, None) or (None, 'reason').

    Layout / HMAC span: see the wire block by REPLY_MAGIC above.

    FAIL-CLOSED, and a DELIBERATE CHANGE from the M3-4b behaviour where a bad CRC still
    produced a renderable dict: a reply that cannot be AUTHENTICATED must never reach the
    console at all. Only (dict, None) is renderable; every other outcome is (None, reason)
    and the caller drops it.

      - version != 2            -> dropped. v1 was CRC-only and was NEVER DEPLOYED (the box
                                   has been gated off throughout), so there is no compatibility
                                   burden. Accepting a v1 reply would reopen exactly the
                                   spoofing hole M4b closes, so there is NO v1 fallback.
      - key is None             -> dropped ('no key'). A display-only receiver (no --key-file)
                                   cannot tell a real reply from a forged one, and an
                                   unauthenticatable reply is indistinguishable from a forgery.
      - bad tag / bad CRC       -> dropped. Acceptance needs BOTH.

    The tag is verified CONSTANT-TIME with hmac.compare_digest: a naked '==' on a MAC leaks
    the matching-prefix length through timing and would be a real bug here.
    """
    if payload is None or len(payload) < REPLY_HDR_LEN + REPLY_CRC_LEN + REPLY_TAG_LEN:
        return None, 'short'
    if payload[0:4] != REPLY_MAGIC:
        return None, 'bad magic'
    ver = payload[4]
    if ver != REPLY_VERSION:
        return None, 'bad version %d (want %d; v1 is unauthenticated and never accepted)' % (
            ver, REPLY_VERSION)
    verdict = payload[5]
    seq, tlen = struct.unpack_from('<HH', payload, 6)
    if tlen > CTRL_REPLY_TEXT_MAX:
        return None, 'tlen %d > %d' % (tlen, CTRL_REPLY_TEXT_MAX)
    crc_off = REPLY_HDR_LEN + tlen
    tag_off = crc_off + REPLY_CRC_LEN
    # Bounds-check BEFORE slicing: tlen is attacker-controlled.
    if len(payload) < tag_off + REPLY_TAG_LEN:
        return None, 'truncated text'
    if key is None:
        # Fail-closed: no key => no authentication => nothing honest to render.
        return None, 'no key (reply cannot be authenticated)'
    got_crc = struct.unpack_from('<I', payload, crc_off)[0]
    calc_crc = zlib.crc32(payload[:crc_off]) & 0xFFFFFFFF
    if got_crc != calc_crc:
        return None, 'bad crc'
    want_tag = hmac.new(bytes(key), bytes(payload[:tag_off]), hashlib.sha256).digest()
    if not hmac.compare_digest(want_tag, bytes(payload[tag_off:tag_off + REPLY_TAG_LEN])):
        return None, 'bad hmac'
    return {
        'ver': ver,
        'verdict': verdict,
        'verdict_name': VERDICT_NAMES.get(verdict, '?%d' % verdict),
        'seq': seq,
        'tlen': tlen,
        'text_bytes': bytes(payload[REPLY_HDR_LEN:crc_off]),
        'crc_ok': True,
        'hmac_ok': True,
    }, None


def load_control_key(path):
    """Read the 32-byte raw HMAC key. Returns bytes, or None with an honest printed
    reason. FAIL-CLOSED: no key => the send path is disabled (the display half is
    unaffected). The key bytes are NEVER printed or logged."""
    if not path:
        print("[send] no --key-file given: POST /send disabled (display half unaffected)", flush=True)
        return None
    try:
        with open(path, 'rb') as fh:
            raw = fh.read()
    except OSError as e:
        print("[send] cannot read key file '%s': %s -- POST /send disabled" % (path, e), flush=True)
        return None
    if len(raw) != CONTROL_KEY_LEN:
        print("[send] key file '%s' is %d bytes, expected exactly %d -- POST /send disabled"
              % (path, len(raw), CONTROL_KEY_LEN), flush=True)
        return None
    return raw


def validate_query(value):
    """Validate a browser-supplied query -> (bytes, None) or (None, 'reason').

    Bounded, printable, and <= CONTROL_QUERY_MAX *bytes* (never characters — a multibyte
    string is longer on the wire than len() suggests)."""
    if not isinstance(value, str):
        return None, "'query' must be a string"
    q = value.strip()
    if not q:
        return None, "'query' is empty"
    if not q.isprintable():
        return None, "'query' contains control characters"
    qb = q.encode('utf-8')
    if len(qb) > CONTROL_QUERY_MAX:
        return None, "'query' is %d bytes (max %d)" % (len(qb), CONTROL_QUERY_MAX)
    return qb, None


def resolve_http_bind(send, http_bind, bind):
    """LOCALHOST GUARD #2 (bind) -> (address, error_or_None).

    Without --send this is legacy behaviour (an explicit --http-bind, else --bind), so
    nothing changes for a display-only run. With --send the HTTP surface carries the
    signing endpoint, so it is pinned to loopback: the default ('' = all interfaces) is
    FORCED to 127.0.0.1, and an explicitly requested non-loopback address is REFUSED
    (the caller exits nonzero) rather than silently downgraded."""
    if not send:
        return (http_bind if http_bind else bind), None
    candidate = http_bind if http_bind else bind
    if candidate == '':
        return '127.0.0.1', None
    if candidate not in LOOPBACK_BINDS:
        return None, ("--send binds the signing endpoint to loopback only, but '%s' was "
                      "requested. Pass --http-bind 127.0.0.1 (the telemetry UDP socket keeps "
                      "using --bind, so it can still listen on the LAN), or run without "
                      "--send." % candidate)
    return candidate, None


class ControlSender:
    """Signs and transmits control-IN frames. Holds the key, the epoch, the provisioned
    box address, and the monotonic sequence state. Thread-safe (the HTTP server is
    threading, so two browser tabs can POST concurrently)."""

    def __init__(self, key, epoch=CONTROL_TEST_EPOCH, box_mac=BOX_MAC, box_ip=BOX_IP,
                 port=CONTROL_PORT):
        self.key = key
        self.epoch = epoch
        self.box_mac = box_mac
        self.box_ip = box_ip
        self.port = port
        self._iface = None
        self._lock = threading.RLock()   # reentrant: send() holds it across next_seq()
        self._last_seq = 0

    def next_seq(self):
        """Monotonic, TIMESTAMP-derived (ms since epoch) so it always sits far above the
        box's persisted replay floor (M3-3) — no floor coordination is needed. Strictly
        increasing even for several sends inside the same millisecond."""
        with self._lock:
            return self._next_seq_locked()

    def _next_seq_locked(self):
        """The seq allocator proper. Caller MUST hold self._lock (an RLock, so send() can
        hold it across allocate-and-transmit).

        CAVEAT (honest): the clock is the source. If it steps BACKWARD (NTP correction, a VM
        snapshot restore) far enough, a fresh process can allocate below the box's PERSISTED
        replay floor, and the box will silently reject every frame until the clock catches
        up. That is visible as a turn that never gets a reply, which the console reports."""
        s = int(time.time() * 1000)
        if s <= self._last_seq:
            s = self._last_seq + 1
        self._last_seq = s
        return s

    def send(self, query_bytes):
        """Sign + transmit one frame; returns the seq used. Raises ScapyUnavailable when
        the raw L2 path is unusable, ValueError for a bad frame."""
        # Lazy import: scapy (+ Npcap + elevation) is only needed for --send, and must not
        # become a hard dependency of the display-only receiver.
        try:
            from scapy.all import Ether, IP, UDP, Raw, sendp, conf
        except Exception as e:
            raise ScapyUnavailable("scapy is unavailable (%s); install scapy (+ Npcap on "
                                   "Windows) and run elevated" % e)
        if not self.key:
            raise ValueError("no HMAC key loaded")
        if self._iface is None:
            try:
                self._iface = conf.route.route(self.box_ip)[0]
            except Exception as e:
                raise ScapyUnavailable("no route to %s (%s)" % (self.box_ip, e))
        # Hold the lock across allocate-seq AND transmit. The box's replay floor is strictly
        # monotonic (control_replay.c: seq <= floor -> reject), so if two concurrent tabs
        # allocated N and N+1 and then raced onto the wire in the order N+1, N, the box would
        # silently drop N — no reply, no error, a turn that hangs "awaiting" forever. Sends
        # are human-paced, so serializing them costs nothing.
        with self._lock:
            seq = self._next_seq_locked()
            frame = build_control_frame(self.key, seq, self.epoch, query_bytes,
                                        os.urandom(CONTROL_NONCE_LEN))
            sport = random.randint(32768, 60999)
            try:
                sendp(Ether(dst=self.box_mac) / IP(dst=self.box_ip) /
                      UDP(sport=sport, dport=self.port) / Raw(frame),
                      iface=self._iface, verbose=False)
            except Exception as e:
                raise ScapyUnavailable("raw L2 send failed (%s); Npcap installed? running "
                                       "elevated?" % e)
        return seq


def reply_to_record(rep, recv_ts):
    """JRPL decode -> the SSE 'control_reply' record.

    The 'kind' is the STRING 'control_reply' (telemetry records carry an INTEGER kind),
    so no consumer can mistake a reply for a telemetry packet."""
    text = rep['text_bytes'].split(b'\x00', 1)[0].decode('utf-8', 'replace')
    return {
        'kind': 'control_reply',
        'verdict': rep['verdict'],
        'verdict_name': rep['verdict_name'],
        'seq': rep['seq'],
        'text': text,
        'crc_ok': rep['crc_ok'],
        # 6-5/M4b: only an AUTHENTICATED reply is ever turned into a record (decode drops the
        # rest), so this is True by construction — carried explicitly so the console can state
        # what it is showing rather than infer it.
        'hmac_ok': rep.get('hmac_ok', False),
        'recv_ts': recv_ts,
    }


# 6-5/M4b: dropped-reply accounting. A drop is the NORMAL outcome for a forged/corrupt/
# unauthenticatable datagram, so it is counted always and logged at a bounded rate.
g_reply_drops = {'total': 0}
REPLY_DROP_LOG_FIRST = 5     # log the first few verbatim (bring-up / misprovisioned key)
REPLY_DROP_LOG_EVERY = 100   # then one line per N (a flood must not spam the terminal)


def _reply_listener(hub, bind, port, box_ip=BOX_IP, key=None):
    """Bind UDP :51002 and fan every AUTHENTICATED JRPL reply out over the existing SSE
    stream. A bind failure is NON-FATAL (the display half keeps working).

    6-5/M4b: the reply now carries an HMAC-SHA256 tag under the SAME symmetric JKEY as the
    inbound direction, verified constant-time in decode_control_reply. THAT is the
    authentication: a reply that fails it is dropped here and never rendered, so a LAN host
    that can reach this port can no longer forge an "answer from JARVIS".

    The source-address check below is now only a cheap PRE-FILTER (it discards obvious
    strangers before the SHA-256 work); it is NOT the defence and must never be described as
    one — an on-path attacker can spoof a source address, but cannot produce a valid tag."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind((bind, port))
    except OSError as e:
        print("[send] cannot bind reply port %d (%s): replies will not be shown; "
              "sending still works" % (port, e), flush=True)
        sock.close()
        return
    print("[send] listening for box replies on udp :%d (HMAC-verified; key=%s)"
          % (port, 'loaded' if key else 'MISSING -> every reply dropped'), flush=True)
    while True:
        try:
            data, addr = sock.recvfrom(2048)
        except OSError:
            return
        if box_ip and addr[0] != box_ip:
            continue  # cheap pre-filter, not the defence (see the docstring)
        rep, err = decode_control_reply(data, key)
        if rep is None:
            # Unverified => never rendered. Counted always; LOGGED at a bounded rate so a
            # flood cannot spam the terminal (the control-IN stats cadence, mirrored).
            g_reply_drops['total'] += 1
            n = g_reply_drops['total']
            if n <= REPLY_DROP_LOG_FIRST or n % REPLY_DROP_LOG_EVERY == 0:
                print("[reply] DROP unverified (%s) from %s -- %d dropped so far"
                      % (err, addr[0], n), flush=True)
            continue
        # Only authenticated replies get here.
        hub.publish_event(reply_to_record(rep, time.time()))


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

    def publish_event(self, record):
        """Fan-out ONLY — deliberately does NOT touch self.latest.

        self.latest is replayed to every NEW subscriber as its initial record, so it must
        stay a TELEMETRY record. A one-shot event (a control-IN reply) that became
        'latest' would be re-delivered as stale state to every later page load, and would
        be handed to consumers expecting a telemetry packet. Events fan out live only."""
        with self._lock:
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
    # 6-5/M3-4b: set by _run_sse ONLY when --send was passed. While it is None the handler
    # never signs anything — a receiver started without --send has no send path at all.
    sender = None

    def log_message(self, *args):  # silence default request logging
        pass

    def do_GET(self):
        path = self.path.split('?', 1)[0]
        # In --send (two-way) mode the /events stream also carries control_reply records =
        # the box's ANSWER TEXT, and the server is loopback-bound anyway, so a non-loopback
        # Host can only be a DNS rebind. Display-only mode is untouched (a receiver bound to
        # a LAN address and browsed from another machine keeps working exactly as before).
        if self.sender is not None and not host_is_loopback(self.headers.get('Host')):
            self.send_error(403, 'Host must be a loopback name')
            return
        if path == '/events':
            self._serve_sse()
        else:
            self._serve_static(path)

    def do_POST(self):
        """POST /send {"query": "..."} -> sign + transmit one control-IN frame.

        Guarded by THREE independent layers: the server is bound to loopback (guard #2,
        resolve_http_bind), every request's peer is re-checked here (guard #1), and the
        request's browser-set provenance headers are checked (guard #3). The peer check
        comes FIRST — a non-loopback client never reaches signing, never transmits, and
        never consumes a sequence number."""
        path = self.path.split('?', 1)[0]
        if path != '/send':
            self._send_json(404, {'error': 'not found'})
            return

        # LOCALHOST GUARD #1 (per-request peer) — before ANY parsing, signing, or sending.
        peer = self.client_address[0] if self.client_address else ''
        if peer not in LOOPBACK_PEERS:
            print("[send] REFUSED non-loopback POST /send from %s" % peer, flush=True)
            self._send_json(403, {'error': 'the signing endpoint serves loopback clients only'})
            return

        # LOCALHOST GUARD #3 (request provenance — the confused-deputy guard).
        #
        # Guards #1/#2 prove the TCP connection is local. They do NOT prove the operator
        # asked for it: a page on ANY site the operator visits can make their browser POST
        # here, and that request arrives with peer 127.0.0.1 and passes both. Without this
        # block, any website could spend the operator's HMAC key on arbitrary queries and —
        # via the /events stream — read the box's answers back. So the REQUEST's provenance
        # is checked too, using headers page JS cannot forge:
        #   Host             — a rebound DNS name would otherwise be legitimately same-origin
        #   Sec-Fetch-Site   — a forbidden header name; page JS cannot set or clear it
        #   Origin           — sent by the browser on every cross-origin POST
        #   Content-Type     — application/json is NOT CORS-safelisted, so a cross-origin
        #                      POST must preflight; there is no do_OPTIONS, so it never lands
        # A non-browser local client (curl, a test) sends no Origin/Sec-Fetch-Site and is
        # still served, provided it asks correctly.
        if not host_is_loopback(self.headers.get('Host')):
            print("[send] REFUSED POST /send with non-loopback Host %r"
                  % self.headers.get('Host'), flush=True)
            self._send_json(403, {'error': 'Host must be a loopback name'})
            return
        sfs = (self.headers.get('Sec-Fetch-Site') or '').strip().lower()
        if sfs and sfs not in ('same-origin', 'none'):
            print("[send] REFUSED cross-site POST /send (Sec-Fetch-Site: %s)" % sfs, flush=True)
            self._send_json(403, {'error': 'cross-site requests are refused'})
            return
        origin = self.headers.get('Origin')
        if origin is not None and origin.strip().rstrip('/').lower() not in \
                allowed_origins(self.server.server_address[1]):
            print("[send] REFUSED cross-origin POST /send from %r" % origin, flush=True)
            self._send_json(403, {'error': 'cross-origin requests are refused'})
            return
        ctype = (self.headers.get('Content-Type') or '').split(';')[0].strip().lower()
        if ctype != 'application/json':
            self._send_json(415, {'error': 'Content-Type must be application/json'})
            return

        try:
            clen = int(self.headers.get('Content-Length') or 0)
        except (TypeError, ValueError):
            clen = -1
        if clen < 0 or clen > 65536:
            self._send_json(400, {'error': 'missing, malformed, or oversized Content-Length'})
            return
        try:
            body = json.loads(self.rfile.read(clen).decode('utf-8')) if clen else None
        except Exception:
            self._send_json(400, {'error': 'body must be UTF-8 JSON'})
            return
        if not isinstance(body, dict):
            self._send_json(400, {'error': 'body must be a JSON object'})
            return
        qb, err = validate_query(body.get('query'))
        if err:
            self._send_json(400, {'error': err})
            return

        sender = self.sender
        if sender is None:
            self._send_json(503, {'error': 'the send path is not enabled (start the receiver '
                                           'with --send)'})
            return
        if not sender.key:
            self._send_json(500, {'error': 'no valid HMAC key loaded (see --key-file); the '
                                           'send path is disabled'})
            return
        try:
            seq = sender.send(qb)
        except ScapyUnavailable as e:
            self._send_json(503, {'error': 'cannot transmit: %s' % e})
            return
        except ValueError as e:
            self._send_json(400, {'error': str(e)})
            return
        except Exception:
            # Never leak a traceback (it can carry paths/config) — the console gets an
            # honest, bounded message and the operator gets the detail on the terminal.
            print("[send] unexpected send failure", flush=True)
            self._send_json(500, {'error': 'send failed (see the receiver terminal)'})
            return
        print("[send] sent control-IN frame seq=%d (%d B query)" % (seq, len(qb)), flush=True)
        self._send_json(200, {'sent': True, 'seq': seq})

    def _send_json(self, code, obj):
        body = json.dumps(obj).encode('utf-8')
        try:
            self.send_response(code)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass  # client went away mid-response

    def _serve_sse(self):
        self.send_response(200)
        self.send_header('Content-Type', 'text/event-stream')
        self.send_header('Cache-Control', 'no-cache')
        self.send_header('Connection', 'keep-alive')
        # The permissive CORS header stays for DISPLAY-ONLY runs (pre-existing behaviour: the
        # stream is read-only telemetry, and box-free consumers rely on it). In --send mode
        # the SAME stream also carries control_reply records = the box's ANSWER TEXT, so a
        # cross-origin reader would turn the console into a remote read channel. The real
        # console is served from this very origin and needs no CORS header at all.
        if self.sender is None:
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
    # LOCALHOST GUARD #2 (bind): resolved BEFORE any socket is opened, so a refused
    # configuration never briefly exposes the signing endpoint.
    http_bind, bind_err = resolve_http_bind(args.send, args.http_bind, args.bind)
    if bind_err:
        print("error: %s" % bind_err, file=sys.stderr, flush=True)
        return 2

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

    if args.send:
        # FAIL-CLOSED: a missing/invalid key leaves sender.key None -> POST /send answers
        # 500 while the whole DISPLAY half keeps running.
        key = load_control_key(args.key_file)
        _SSEHandler.sender = ControlSender(key, epoch=args.epoch, box_mac=args.box_mac,
                                           box_ip=args.box_ip)
        # The reply arrives from the box over the LAN, so this socket uses --bind (only the
        # HTTP/signing surface is loopback-pinned).
        # The SAME symmetric key authenticates BOTH directions (M4b): no key => replies are
        # dropped rather than shown unverified.
        threading.Thread(target=_reply_listener,
                         args=(hub, args.bind, args.reply_port, args.box_ip, key),
                         daemon=True).start()
        print("[send] POST /send enabled on http://%s:%d/send (loopback only; key=%s; "
              "box %s / %s :%d)"
              % (http_bind, args.http_port, 'loaded' if key else 'MISSING -> disabled',
                 args.box_mac, args.box_ip, CONTROL_PORT), flush=True)

    httpd = _QuietThreadingHTTPServer((http_bind, args.http_port), _SSEHandler)
    print("SSE bridge: http://%s:%d  (/events stream; static from '%s'; source: %s)"
          % (http_bind or '0.0.0.0', args.http_port, args.web_dir, source), flush=True)
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
    # --- 6-5/M3-4b: the two-way SEND path (requires --sse; needs elevation + scapy/Npcap) ---
    ap.add_argument('--send', action='store_true',
                    help="enable POST /send (sign+transmit a control-IN frame) and the reply "
                         "listener; pins the HTTP/signing surface to loopback")
    ap.add_argument('--key-file', help="path to the 32-byte raw HMAC-SHA256 key (the box JKEY slot)")
    ap.add_argument('--epoch', type=lambda s: int(s, 0), default=CONTROL_TEST_EPOCH,
                    help="sender boot epoch (default: 0x%08X)" % CONTROL_TEST_EPOCH)
    ap.add_argument('--box-mac', default=BOX_MAC, help="box MAC (provisioned: %s)" % BOX_MAC)
    ap.add_argument('--box-ip', default=BOX_IP, help="box IP (provisioned: %s)" % BOX_IP)
    ap.add_argument('--http-bind', default='',
                    help="HTTP/SSE bind address (default: follow --bind; forced to loopback with --send)")
    ap.add_argument('--reply-port', type=int, default=CONTROL_REPLY_PORT,
                    help="UDP port for the box's replies (default: %d)" % CONTROL_REPLY_PORT)
    args = ap.parse_args(argv)

    if args.send and not args.sse:
        print("error: --send requires --sse (the send path lives in the SSE bridge, which is "
              "what the browser talks to)", file=sys.stderr, flush=True)
        return 2

    if args.sse:
        return _run_sse(args)
    if args.replay:
        return _run_replay_stdout(args)
    return _run_live_stdout(args)


if __name__ == '__main__':
    sys.exit(main())
