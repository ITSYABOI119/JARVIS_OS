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

import argparse
import hashlib
import hmac
import inspect
import io
import json
import os
import re
import struct
import sys
import tempfile
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import telemetry_receiver as tr_mod  # noqa: E402  (module handle: guard-#2 wiring test stubs into it)
from telemetry_receiver import (  # noqa: E402
    decode_packet, packet_to_record, iter_pcap_telemetry, FMT, MAGIC, PKT_SIZE, FLAG_NAMES,
    FMT_V10, FMT_V11, PKT_SIZE_V10, PKT_SIZE_V11, BANNED_RECORD_KEYS,
    # --- 6-5/M3-4b: the two-way SEND path ---
    build_control_frame, decode_control_reply, validate_query, resolve_http_bind,
    reply_to_record, ControlSender, TelemetryHub, _SSEHandler,
    CONTROL_MAGIC, CONTROL_QUERY_MAX, CONTROL_HDR_LEN, CONTROL_TEST_EPOCH, VERDICT_NAMES,
    host_is_loopback, allowed_origins, CONTROL_REPLY_PORT, BOX_MAC, BOX_IP)
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


# --- 6-5/M3-4b helpers -----------------------------------------------------
class _FakeSender:
    """Records what it was asked to sign/send, so a guard test can prove NOTHING was."""

    def __init__(self, key=bytes(range(1, 33))):
        self.key = key
        self.calls = []

    def send(self, query_bytes):
        self.calls.append(query_bytes)
        return 4242


class _FakeServer:
    """Minimal stand-in so do_POST can read the bound port for allowed_origins()."""

    def __init__(self, port=8800):
        self.server_address = ('127.0.0.1', port)


def _fake_post(peer, sender, body=None, path='/send', raw=None, headers=None, port=8800):
    """Drive _SSEHandler.do_POST without a socket/server.

    __new__ (never __init__ — BaseHTTPRequestHandler.__init__ would try to serve a real
    request). _send_json is shadowed per instance to capture (status, json-body).

    Defaults model a WELL-FORMED same-origin request from the real console (loopback Host +
    application/json), so a test that is not about guard #3 does not have to restate them.
    `headers` overrides/extends them — pass a value of None to DELETE a default header, which
    is how the guard-#3 tests build hostile requests."""
    h = _SSEHandler.__new__(_SSEHandler)
    h.client_address = peer
    h.path = path
    h.sender = sender
    h.server = _FakeServer(port)
    if raw is None:
        raw = b'' if body is None else json.dumps(body).encode('utf-8')
    hdrs = {'Content-Length': str(len(raw)),
            'Host': '127.0.0.1:%d' % port,
            'Content-Type': 'application/json'}
    for k, v in (headers or {}).items():
        if v is None:
            hdrs.pop(k, None)
        else:
            hdrs[k] = v
    h.headers = hdrs
    h.rfile = io.BytesIO(raw)
    out = []
    h._send_json = lambda code, obj: out.append((code, obj))
    h.do_POST()
    return out[0] if out else (None, None)


# 6-5/M4b: the SHARED GOLDEN VECTOR. The box embeds the same 97 bytes in C; the coupling
# test below asserts the two are byte-identical, pinning builder against verifier.
REPLY_KEY = bytes(range(1, 33))
GOLDEN_REPLY_VERDICT = 0
GOLDEN_REPLY_SEQ = 4242
GOLDEN_REPLY_TEXT = b'a page fault is a memory access to an unmapped page'


def _build_reply(verdict, seq, text, key=REPLY_KEY, ver=2, corrupt_crc=False, tag=True):
    """Build a JRPL v2 reply exactly as the box's ctrl_send_reply() does (LITTLE-endian).

        magic|ver|verdict|seq|tlen | text | crc32(payload[:10+tlen]) | HMAC(payload[:14+tlen])

    ver=1 / tag=False reproduce the pre-M4b unauthenticated shapes for the negative tests."""
    body = b'JRPL' + bytes([ver, verdict]) + struct.pack('<HH', seq, len(text)) + text
    crc = zlib.crc32(body) & 0xFFFFFFFF
    if corrupt_crc:
        crc ^= 0xFFFFFFFF
    body += struct.pack('<I', crc)
    if not tag:
        return body
    return body + hmac.new(bytes(key), body, hashlib.sha256).digest()


def _flip(buf, off):
    """Flip one bit of one byte -- the minimal tamper."""
    b = bytearray(buf)
    b[off] ^= 0x01
    return bytes(b)


def main():
    print("== telemetry receiver wire-compat ==")

    # Layout (v11 = the current wire)
    check(struct.calcsize(FMT) == 254, "struct.calcsize(FMT) == 254 (v11)")
    check(PKT_SIZE == 254, "PKT_SIZE == 254 (v11)")
    check(PKT_SIZE_V10 == 246 and PKT_SIZE_V11 == 254, "version-tolerant sizes: v10=246, v11=254")

    # Canonical zlib CRC vector — same CRC the C side proved (jarvis_telemetry.c)
    check(zlib.crc32(b"123456789") & 0xFFFFFFFF == 0xCBF43926,
          "canonical zlib CRC of \"123456789\" == 0xCBF43926")

    # Valid packet round-trips
    pkt = build_packet()
    check(len(pkt) == 254, "built packet is 254 bytes (v11)")
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
    check(raises_valueerror(lambda: decode_packet(pkt + b'\x00')), "over-long input raises ValueError")

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

    # --- G3/M4: retrieval_hits/retrieval_latency_us + TLM_F_RETRIEVAL (v3), then the v4 216->218
    # bump adding infer_last_tok_x100 (real measured tok/s*100; fabricated aliases stay banned) ---
    pkt_retr = build_packet(retrieval_hits=3, retrieval_latency_us=40, flags=0x01 | 0x10 | 0x80)
    dretr = decode_packet(pkt_retr)
    check(dretr['crc_ok'] is True, "retrieval packet crc_ok True (CRC over [:PKT_SIZE-4])")
    check(dretr["version"] == 11, "v11 packet version == 11")
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

    # --- P5 #5/M2: shield_learn_keys/shield_learn_max_risk_x100 + TLM_F_SHIELD_LEARN (the v5
    # 218->222 size-bump). MONITOR-ONLY fields: learned-risk counts, NEVER a block count — the
    # fabricated 'shield_blocked' key stays banned. ---
    pkt_sl = build_packet(shield_learn_keys=1, shield_learn_max_risk_x100=20,
                          flags=0x01 | 0x10 | 0x200)
    dsl = decode_packet(pkt_sl)
    check(dsl['crc_ok'] is True, "v5 shield-learn packet crc_ok True (CRC over [:218])")
    check(dsl['shield_learn_keys'] == 1 and dsl['shield_learn_max_risk_x100'] == 20,
          "shield_learn_keys/shield_learn_max_risk_x100 decode")
    check('SHIELD_LEARN' in dsl['flags_list'], "TLM_F_SHIELD_LEARN 0x200 -> 'SHIELD_LEARN' in flags_list")
    rec_sl = packet_to_record(dsl)
    check(rec_sl['shield_learn_keys'] == 1 and rec_sl['shield_learn_max_risk_x100'] == 20,
          "record carries the shield-learn monitor fields")
    check('shield_learn_keys' in REQUIRED_RECORD_KEYS and
          'shield_learn_max_risk_x100' in REQUIRED_RECORD_KEYS,
          "shield_learn_keys/shield_learn_max_risk_x100 are REQUIRED_RECORD_KEYs")
    check(FLAG_NAMES.get(0x200) == 'SHIELD_LEARN' and FLAG_BITS.get('SHIELD_LEARN') == 0x200,
          "FLAG_NAMES/FLAG_BITS both carry 0x200 SHIELD_LEARN")
    check('shield_blocked' not in rec_sl, "no fabricated 'shield_blocked' key (monitor-only, ban holds)")

    # --- P5 #4/M2: semantic_fact_count + TLM_F_SEMANTIC (added at the v6 222->224 bump; now rides
    # inside the packet). The distilled-fact count — observable repeated Q&A patterns. ---
    pkt_sem = build_packet(semantic_fact_count=1, flags=0x01 | 0x10 | 0x400)
    dsem = decode_packet(pkt_sem)
    check(dsem['crc_ok'] is True, "v6 semantic packet crc_ok True (CRC over [:220])")
    check(dsem['semantic_fact_count'] == 1, "semantic_fact_count decodes")
    check('SEMANTIC' in dsem['flags_list'], "TLM_F_SEMANTIC 0x400 -> 'SEMANTIC' in flags_list")
    rec_sem = packet_to_record(dsem)
    check(rec_sem['semantic_fact_count'] == 1, "record carries semantic_fact_count")
    check('semantic_fact_count' in REQUIRED_RECORD_KEYS, "semantic_fact_count is a REQUIRED_RECORD_KEY")
    check(FLAG_NAMES.get(0x400) == 'SEMANTIC' and FLAG_BITS.get('SEMANTIC') == 0x400,
          "FLAG_NAMES/FLAG_BITS both carry 0x400 SEMANTIC")

    # --- P6 K/M3: restart_count/actions_fired/actions_blocked + TLM_F_ACTIONS (the v7 224->232
    # size-bump). The self-heal/action-gate activity — actions_blocked is the SEPARATE action gate,
    # a real allowed key distinct from the still-banned query-path 'shield_blocked'. ---
    check(PKT_SIZE >= 232, "v7 fields present (the v8 bump keeps them at the same offsets)")
    pkt_act = build_packet(restart_count=3, actions_fired=3, actions_blocked=2,
                           flags=0x01 | 0x10 | 0x800)
    dact = decode_packet(pkt_act)
    check(dact['crc_ok'] is True, "v7 actions packet crc_ok True (CRC over [:PKT_SIZE-4])")
    check(dact['restart_count'] == 3 and dact['actions_fired'] == 3 and dact['actions_blocked'] == 2,
          "restart_count/actions_fired/actions_blocked decode")
    check('ACTIONS' in dact['flags_list'], "TLM_F_ACTIONS 0x800 -> 'ACTIONS' in flags_list")
    rec_act = packet_to_record(dact)
    check(rec_act['restart_count'] == 3 and rec_act['actions_fired'] == 3 and rec_act['actions_blocked'] == 2,
          "record carries restart_count/actions_fired/actions_blocked")
    check('restart_count' in REQUIRED_RECORD_KEYS and 'actions_fired' in REQUIRED_RECORD_KEYS and
          'actions_blocked' in REQUIRED_RECORD_KEYS,
          "restart_count/actions_fired/actions_blocked are REQUIRED_RECORD_KEYs")
    check(FLAG_NAMES.get(0x800) == 'ACTIONS' and FLAG_BITS.get('ACTIONS') == 0x800,
          "FLAG_NAMES/FLAG_BITS both carry 0x800 ACTIONS")
    check('actions_blocked' not in BANNED_RECORD_KEYS and 'shield_blocked' in BANNED_RECORD_KEYS,
          "actions_blocked is allowed; the query-path shield_blocked stays banned")

    # --- P6 6-1/M3: monitors_fired/last_monitor_event + TLM_F_MONITORS (the v8 232->236
    # size-bump). A NEUTRAL monitor NOTIFY event count (a mix of degradation + benign liveness
    # events) — never "anomalies/problems detected". ---
    check(PKT_SIZE >= 236, "v8 fields present (the v9 bump keeps them at the same offsets)")
    pkt_mon = build_packet(monitors_fired=5, last_monitor_event=3,
                           flags=0x01 | 0x10 | 0x1000)
    dmon = decode_packet(pkt_mon)
    check(dmon['crc_ok'] is True, "v8 monitors packet crc_ok True (CRC over [:PKT_SIZE-4])")
    check(dmon['version'] == 11, "monitors packet version == 11 (v11 wire)")
    check(dmon['monitors_fired'] == 5 and dmon['last_monitor_event'] == 3,
          "monitors_fired/last_monitor_event decode")
    check('MONITORS' in dmon['flags_list'], "TLM_F_MONITORS 0x1000 -> 'MONITORS' in flags_list")
    rec_mon = packet_to_record(dmon)
    check(rec_mon['monitors_fired'] == 5 and rec_mon['last_monitor_event'] == 3,
          "record carries monitors_fired/last_monitor_event")
    check('monitors_fired' in REQUIRED_RECORD_KEYS and 'last_monitor_event' in REQUIRED_RECORD_KEYS,
          "monitors_fired/last_monitor_event are REQUIRED_RECORD_KEYs")
    check(FLAG_NAMES.get(0x1000) == 'MONITORS' and FLAG_BITS.get('MONITORS') == 0x1000,
          "FLAG_NAMES/FLAG_BITS both carry 0x1000 MONITORS")

    # --- P6 6-2/M3: wakes_fired/last_wake_event + TLM_F_WAKE (the v9 236->240
    # size-bump — event-triggered CONSULT activity; DISPATCHED consults only,
    # never suppressed/refused; a real allowed key, distinct from every ban) ---
    check(PKT_SIZE >= 240, "v9 fields present (the v10 bump keeps them at the same offsets)")
    pkt_wake = build_packet(wakes_fired=3, last_wake_event=1,
                            flags=0x01 | 0x10 | 0x2000)
    dwake = decode_packet(pkt_wake)
    check(dwake['crc_ok'] is True, "wake packet crc_ok True (CRC over [:PKT_SIZE-4])")
    check(dwake['version'] == 11, "v11 packet version == 11")
    check(dwake['wakes_fired'] == 3 and dwake['last_wake_event'] == 1,
          "wakes_fired/last_wake_event decode")
    check('WAKE' in dwake['flags_list'], "TLM_F_WAKE 0x2000 -> 'WAKE' in flags_list")
    rec_wake = packet_to_record(dwake, 1700000000.0)
    check(rec_wake['wakes_fired'] == 3 and rec_wake['last_wake_event'] == 1,
          "record carries wakes_fired/last_wake_event")
    check('wakes_fired' in REQUIRED_RECORD_KEYS and 'last_wake_event' in REQUIRED_RECORD_KEYS,
          "wakes_fired/last_wake_event are REQUIRED_RECORD_KEYs")
    check(FLAG_NAMES.get(0x2000) == 'WAKE' and FLAG_BITS.get('WAKE') == 0x2000,
          "FLAG_NAMES/FLAG_BITS both carry 0x2000 WAKE")
    check('wakes_fired' not in BANNED_RECORD_KEYS,
          "wakes_fired is a REAL allowed key (NOT banned — the monitors_fired precedent)")

    # --- P6 6-3/M3: behaviors_fired/behaviors_mask/last_behavior + TLM_F_PROACTIVE (the v10
    # 240->246 size-bump — proactive-behavior INFORM activity; behaviors_fired = USER
    # INTERRUPTS (cap-suppressed never counts), mask bit id-1 = behavior fired this boot;
    # a B1/B2 consult bumps BOTH wakes_fired and behaviors_fired BY DESIGN — two honest
    # views of one event, never summed; REAL allowed keys, distinct from every ban) ---
    check(PKT_SIZE >= 246, "v10 behavior fields present (the v11 bump keeps them at the same offsets)")
    pkt_beh = build_packet(behaviors_fired=3, behaviors_mask=21, last_behavior=5,
                           flags=0x01 | 0x10 | 0x4000)
    dbeh = decode_packet(pkt_beh)
    check(dbeh['crc_ok'] is True, "v10 behavior packet crc_ok True (CRC over [:242])")
    check(dbeh['version'] == 11, "v11 packet version == 11")
    check(dbeh['behaviors_fired'] == 3 and dbeh['behaviors_mask'] == 21
          and dbeh['last_behavior'] == 5,
          "behaviors_fired/behaviors_mask/last_behavior decode (mask 21 = 0b10101 = B1+B3+B5)")
    check('PROACTIVE' in dbeh['flags_list'], "TLM_F_PROACTIVE 0x4000 -> 'PROACTIVE' in flags_list")
    rec_beh = packet_to_record(dbeh, 1700000000.0)
    check(rec_beh['behaviors_fired'] == 3 and rec_beh['behaviors_mask'] == 21
          and rec_beh['last_behavior'] == 5,
          "record carries behaviors_fired/behaviors_mask/last_behavior")
    check('behaviors_fired' in REQUIRED_RECORD_KEYS and 'behaviors_mask' in REQUIRED_RECORD_KEYS
          and 'last_behavior' in REQUIRED_RECORD_KEYS,
          "behaviors_fired/behaviors_mask/last_behavior are REQUIRED_RECORD_KEYs")
    check(FLAG_NAMES.get(0x4000) == 'PROACTIVE' and FLAG_BITS.get('PROACTIVE') == 0x4000,
          "FLAG_NAMES/FLAG_BITS both carry 0x4000 PROACTIVE")
    check('behaviors_fired' not in BANNED_RECORD_KEYS and 'behaviors_mask' not in BANNED_RECORD_KEYS,
          "behaviors_fired/behaviors_mask are REAL allowed keys (NOT banned — the wakes_fired precedent)")

    # --- P6 6-5/M3-4a: control_in_answered/blocked/dropped + TLM_F_CONTROL_IN (the v11 246->254
    # size-bump). control_in_blocked is a DEFINED-ABUSE-CLASS refuse count, NEVER "injection blocked";
    # the flag means the CHANNEL is up. Fill gated -> the deploy emits v11 with 0s + flag clear. ---
    pkt_ci = build_packet(control_in_answered=3, control_in_blocked=1, control_in_dropped=5,
                          flags=0x01 | 0x10 | 0x8000)
    dci = decode_packet(pkt_ci)
    check(len(pkt_ci) == 254, "v11 control-IN packet is 254 bytes")
    check(dci['crc_ok'] is True, "v11 control-IN packet crc_ok True (CRC over [:250])")
    check(dci['control_in_answered'] == 3 and dci['control_in_blocked'] == 1
          and dci['control_in_dropped'] == 5,
          "control_in_answered/blocked/dropped decode (3/1/5)")
    check('CONTROL_IN' in dci['flags_list'], "TLM_F_CONTROL_IN 0x8000 -> 'CONTROL_IN' in flags_list")
    rec_ci = packet_to_record(dci)
    check(rec_ci['control_in_answered'] == 3 and rec_ci['control_in_blocked'] == 1
          and rec_ci['control_in_dropped'] == 5, "record carries the 3 control_in fields")
    check(all(k in REQUIRED_RECORD_KEYS for k in
              ('control_in_answered', 'control_in_blocked', 'control_in_dropped')),
          "control_in_answered/blocked/dropped are REQUIRED_RECORD_KEYs")
    check(FLAG_NAMES.get(0x8000) == 'CONTROL_IN' and FLAG_BITS.get('CONTROL_IN') == 0x8000,
          "FLAG_NAMES/FLAG_BITS both carry 0x8000 CONTROL_IN")
    check('control_in_blocked' not in BANNED_RECORD_KEYS,
          "control_in_blocked is a REAL allowed key (a defined-abuse-class refuse count, not a ban)")

    # VERSION-TOLERANCE (the deploy-deferral requirement): the LIVE deployed box stays on v10 (246 B)
    # while the code emits v11 — a v10 packet must decode CLEANLY with the 3 control_in fields ABSENT
    # (None) and TLM_F_CONTROL_IN not set. Append-only layout makes this a pure length/version guard.
    pkt_v10 = build_packet(wire_version=10, flags=0x01 | 0x10)
    dv10 = decode_packet(pkt_v10)
    check(len(pkt_v10) == 246, "synthetic v10 packet is 246 bytes")
    check(dv10['crc_ok'] is True, "v10 packet crc_ok True (CRC over [:242], NOT [:250] — per-version)")
    check(dv10['version'] == 10, "v10 packet version byte == 10")
    check(dv10['control_in_answered'] is None and dv10['control_in_blocked'] is None
          and dv10['control_in_dropped'] is None,
          "v10 packet: control_in fields ABSENT (None) — no misparse of the live deployed box")
    check('CONTROL_IN' not in dv10['flags_list'], "v10 packet: TLM_F_CONTROL_IN not set")
    rec_v10 = packet_to_record(dv10)
    check(set(rec_v10.keys()) == set(REQUIRED_RECORD_KEYS),
          "v10 record still carries the full key contract (control_in keys present, valued None)")

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
    check(golden['meta']['size'] == 254 and golden['meta']['fmt'] == FMT,
          "golden meta fmt/size match the wire format")

    kind_expect = {1: 'STATS', 2: 'INFER', 3: 'STATE'}
    internal = ('magic', 'crc32', 'crc_calc')
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

    # =====================================================================
    # P6 6-5/M3-4b: the two-way SEND path (RECEIVER-AS-SIGNER).
    # The browser can hold neither the HMAC key nor a raw L2 socket, so the receiver
    # signs. These tests pin the wire bytes, the HMAC, both localhost guards, the reply
    # decode, and the hub-state isolation.
    # =====================================================================
    print("\n== 6-5/M3-4b: control-IN signing + /send + reply decode ==")

    # (a) SIGNING GOLDEN + HMAC CORRECTNESS.
    # WHY this proves box-acceptability: the box accepts a frame iff it is structurally
    # correct AND the HMAC over payload[0 : 36+qlen] verifies (control_verify.c). The M1
    # host suite already proves the C side ACCEPTS any such frame (test_control_verify +
    # the 300K-iteration differential fuzz vs Python HMAC vectors). So pinning the exact
    # offsets + an independently recomputed tag here pins box-acceptability without a box.
    KEY = bytes(range(1, 33))            # == the box JKEY test slot
    NONCE = bytes(range(0x10, 0x20))     # fixed -> deterministic frame
    SEQ = 1700000000123
    QUERY = b'status'
    frame = build_control_frame(KEY, SEQ, CONTROL_TEST_EPOCH, QUERY, NONCE)
    check(len(frame) == CONTROL_HDR_LEN + len(QUERY) + 32, "frame is 36 + qlen + 32 bytes")
    check(struct.unpack_from('>I', frame, 0)[0] == CONTROL_MAGIC,
          "magic@0 (BE) == 0x4A43544C 'JCTL'")
    check(frame[4] == 1, "version@4 == 1")
    check(frame[5] == 0, "flags@5 == 0 (reserved, MUST be 0)")
    check(struct.unpack_from('>Q', frame, 6)[0] == SEQ, "seq@6 (BE u64) round-trips")
    check(struct.unpack_from('>I', frame, 14)[0] == CONTROL_TEST_EPOCH,
          "boot_epoch@14 (BE u32) == CONTROL_TEST_EPOCH")
    check(frame[18:34] == NONCE, "nonce@18 (16 B) round-trips")
    check(struct.unpack_from('>H', frame, 34)[0] == len(QUERY), "query_len@34 (BE u16) correct")
    check(frame[36:36 + len(QUERY)] == QUERY, "query@36 round-trips")
    indep_tag = hmac.new(KEY, frame[:CONTROL_HDR_LEN + len(QUERY)], hashlib.sha256).digest()
    check(frame[-32:] == indep_tag,
          "tag == INDEPENDENTLY recomputed HMAC-SHA256(key, payload[0:36+qlen])")
    check(len(indep_tag) == 32 and frame[:-32] == frame[:CONTROL_HDR_LEN + len(QUERY)],
          "the tag is never part of its own HMAC input")
    GOLDEN = ('4a43544c01000000018bcfe5687b4a320001101112131415161718191a1b1c1d1e1f'
              '000673746174757355c3948581fd8580cd1fad222e8e7e370b00586270cf8ffdd802722721e7d13d')
    check(frame.hex() == GOLDEN, "GOLDEN full-frame byte match (deterministic signing)")
    check(build_control_frame(KEY, SEQ, CONTROL_TEST_EPOCH, QUERY, NONCE) == frame,
          "signing is deterministic for a fixed (key, seq, epoch, query, nonce)")

    # (a2) CROSS-LANGUAGE COUPLING — the C differential test (test_control_signer_differential.c)
    # feeds a HAND-COPIED constant to the real box verifier. If the signer changes and only this
    # Python golden is regenerated, BOTH suites stay green while the C side keeps blessing a frame
    # the live signer no longer emits — the drift would surface only at the flip, on hardware.
    # So assert the two constants are the same bytes, mechanically.
    _cpath = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          '..', 'src', 'net', 'test_control_signer_differential.c')
    if os.path.isfile(_cpath):
        with open(_cpath, encoding='utf-8') as _fh:
            _csrc = _fh.read()
        _m = re.search(r'GOLDEN_JCTL\[\]\s*=\s*\{(.*?)\}\s*;', _csrc, re.S)
        _cbytes = bytes(int(b, 16) for b in re.findall(r'0[xX]([0-9a-fA-F]{2})', _m.group(1))) \
            if _m else b''
        check(_cbytes == frame,
              "C differential GOLDEN_JCTL[] is byte-identical to the Python signer's output "
              "(%d vs %d bytes)" % (len(_cbytes), len(frame)))
    else:
        check(False, "test_control_signer_differential.c not found (the C coupling is unchecked)")

    # (b) Bounds: the exact box limits, enforced BEFORE anything reaches the wire.
    check(CONTROL_QUERY_MAX == 172, "CONTROL_QUERY_MAX == 172 (240 - 36 - 32)")
    check(raises_valueerror(lambda: build_control_frame(KEY, 1, 0, b'x' * 173, NONCE)),
          "query > 172 bytes raises ValueError")
    check(build_control_frame(KEY, 1, 0, b'x' * 172, NONCE)[-32:] != b'',
          "query == 172 bytes (the boundary) is accepted")
    check(raises_valueerror(lambda: build_control_frame(b'short', 1, 0, b'x', NONCE)),
          "key != 32 bytes raises ValueError")
    check(raises_valueerror(lambda: build_control_frame(KEY, 1, 0, b'x', b'\x00' * 15)),
          "nonce != 16 bytes raises ValueError")

    # (c) Sequence monotonicity. Timestamp-derived (ms), so it always lands far above the
    # box's persisted replay floor (M3-3) — but two sends in the SAME millisecond must
    # still strictly increase, or the second would be dropped as a replay.
    snd = ControlSender(KEY)
    s1, s2 = snd.next_seq(), snd.next_seq()
    check(s2 > s1, "two rapid next_seq() calls strictly increase")
    snd._last_seq = int(time.time() * 1000) + 100000  # force a same-ms collision
    c1, c2, c3 = snd.next_seq(), snd.next_seq(), snd.next_seq()
    check(c1 < c2 < c3, "same-millisecond next_seq() still strictly increases (no replay drop)")

    # (d) LOCALHOST GUARD #1 (per-request peer check).
    fs = _FakeSender()
    code, body = _fake_post(('192.168.100.50', 51234), fs, {'query': 'status'})
    check(code == 403, "non-loopback POST /send -> 403")
    check('error' in body, "403 body is JSON with an 'error' key")
    check(fs.calls == [], "GUARD #1: non-loopback request signed/sent NOTHING")
    code, body = _fake_post(('10.0.0.7', 40000), fs, {'query': 'status'})
    check(code == 403 and fs.calls == [], "a second non-loopback peer is refused too, still nothing sent")
    code, body = _fake_post(('127.0.0.1', 51234), fs, {'query': 'status'})
    check(code == 200 and body.get('sent') is True and body.get('seq') == 4242,
          "127.0.0.1 reaches the send stage -> 200 {'sent': true, 'seq': N}")
    check(fs.calls == [b'status'], "the loopback request signed exactly the posted query")
    code, body = _fake_post(('127.0.0.1', 1), fs, {'query': 'x'}, path='/nope')
    check(code == 404, "a non-/send POST path -> 404")
    code, body = _fake_post(('127.0.0.1', 1), None, {'query': 'x'})
    check(code == 503 and 'error' in body, "no sender configured (--send absent) -> 503")
    code, body = _fake_post(('127.0.0.1', 1), _FakeSender(key=None), {'query': 'x'})
    check(code == 500 and 'error' in body, "sender present but NO valid key -> 500 (fail-closed)")

    # (e) LOCALHOST GUARD #2 (bind).
    addr, err = resolve_http_bind(True, '0.0.0.0', '')
    check(addr is None and err, "--send + explicit --http-bind 0.0.0.0 -> refused (error)")
    addr, err = resolve_http_bind(True, '', '192.168.100.146')
    check(addr is None and err, "--send + a non-loopback --bind for HTTP -> refused (error)")
    addr, err = resolve_http_bind(True, '', '')
    check(addr == '127.0.0.1' and err is None, "--send + default bind -> FORCED to 127.0.0.1")
    addr, err = resolve_http_bind(True, '127.0.0.1', '0.0.0.0')
    check(addr == '127.0.0.1' and err is None, "--send + explicit loopback --http-bind is honored")
    addr, err = resolve_http_bind(False, '', '')
    check(addr == '' and err is None, "no --send: default bind unchanged (legacy behaviour)")
    addr, err = resolve_http_bind(False, '', '0.0.0.0')
    check(addr == '0.0.0.0' and err is None, "no --send: --bind still drives HTTP (legacy)")
    addr, err = resolve_http_bind(True, '::1', '')
    check(addr is None and err,
          "--send + '::1' refused (the server is AF_INET; accepting it would crash in bind())")

    # (e2) GUARD #2 IS ACTUALLY WIRED — resolve_http_bind() is useless if _run_sse ignores it.
    # Without this, deleting the wiring leaves the suite green while the signing endpoint binds
    # every interface and the two-layer defence silently degrades to one.
    captured = {}

    class _StubHTTPD:
        def __init__(self, addr_port, handler):
            captured['addr'] = addr_port
            raise KeyboardInterrupt   # unwind before serve_forever(); _run_sse must not hang

        def server_close(self):
            pass

    _real_httpd = tr_mod._QuietThreadingHTTPServer
    _real_listener = tr_mod._reply_listener
    tr_mod._QuietThreadingHTTPServer = _StubHTTPD
    tr_mod._reply_listener = lambda *a, **k: None          # never bind :51002 in a test
    try:
        keyf = os.path.join(tempfile.gettempdir(), 'm34b_bindwire_key.bin')
        with open(keyf, 'wb') as fh:
            fh.write(bytes(range(1, 33)))
        args = argparse.Namespace(
            replay=None, replay_rate=1.0, bind='', port=51000, http_port=8801,
            http_bind='', web_dir='.', send=True, key_file=keyf,
            epoch=CONTROL_TEST_EPOCH, box_mac=BOX_MAC, box_ip=BOX_IP,
            reply_port=CONTROL_REPLY_PORT)
        try:
            tr_mod._run_sse(args)
        except KeyboardInterrupt:
            pass
        os.remove(keyf)
    finally:
        tr_mod._QuietThreadingHTTPServer = _real_httpd
        tr_mod._reply_listener = _real_listener
    check(captured.get('addr') == ('127.0.0.1', 8801),
          "GUARD #2 WIRED: --send + default --bind actually binds HTTP to 127.0.0.1 (got %r)"
          % (captured.get('addr'),))

    # (e3) LOCALHOST GUARD #3 — the confused-deputy / CSRF guard.
    # Guards #1/#2 only prove the TCP peer is local. A page on ANY site the operator visits can
    # make their BROWSER post here; that request has peer 127.0.0.1 and passes both. These
    # assertions pin the request-provenance checks that stop it. Each hostile request must sign
    # NOTHING (fs.calls stays empty) — a 403/415 that still signed would be worthless.
    fs3 = _FakeSender()
    code, body = _fake_post(('127.0.0.1', 51234), fs3, {'query': 'x'},
                            headers={'Origin': 'https://evil.example'})
    check(code == 403 and fs3.calls == [],
          "GUARD #3: cross-origin POST (hostile Origin) -> 403, signed NOTHING")
    code, body = _fake_post(('127.0.0.1', 51234), fs3, {'query': 'x'},
                            headers={'Sec-Fetch-Site': 'cross-site'})
    check(code == 403 and fs3.calls == [],
          "GUARD #3: Sec-Fetch-Site cross-site -> 403, signed NOTHING")
    # The exact browser vector: text/plain is CORS-safelisted, so a cross-origin POST carrying a
    # JSON body is a SIMPLE request and is delivered with no preflight. Requiring application/json
    # forces a preflight, and there is no do_OPTIONS, so it never lands.
    code, body = _fake_post(('127.0.0.1', 51234), fs3, {'query': 'x'},
                            headers={'Content-Type': 'text/plain;charset=UTF-8'})
    check(code == 415 and fs3.calls == [],
          "GUARD #3: text/plain simple-request POST -> 415, signed NOTHING (the CSRF vector)")
    code, body = _fake_post(('127.0.0.1', 51234), fs3, {'query': 'x'},
                            headers={'Content-Type': None})
    check(code == 415 and fs3.calls == [],
          "GUARD #3: a missing Content-Type -> 415, signed NOTHING")
    code, body = _fake_post(('127.0.0.1', 51234), fs3, {'query': 'x'},
                            headers={'Host': 'rebind.evil.example:8800'})
    check(code == 403 and fs3.calls == [],
          "GUARD #3: DNS-rebinding Host -> 403, signed NOTHING")
    check(not host_is_loopback('rebind.evil.example:8800') and host_is_loopback('127.0.0.1:8800')
          and host_is_loopback('localhost') and host_is_loopback('[::1]:8800'),
          "host_is_loopback: loopback literals accepted, a rebound name refused")
    # ...and the LEGITIMATE console request (same-origin, application/json) still works.
    code, body = _fake_post(('127.0.0.1', 51234), fs3, {'query': 'status'},
                            headers={'Origin': 'http://127.0.0.1:8800',
                                     'Sec-Fetch-Site': 'same-origin'})
    check(code == 200 and fs3.calls == [b'status'],
          "GUARD #3: the real console's same-origin JSON POST is still served")
    # A non-browser local client (curl / a script) sends no Origin or Sec-Fetch-Site.
    fs3.calls = []
    code, body = _fake_post(('127.0.0.1', 51234), fs3, {'query': 'uptime'},
                            headers={'Origin': None, 'Sec-Fetch-Site': None})
    check(code == 200 and fs3.calls == [b'uptime'],
          "GUARD #3: a local non-browser client (no Origin/Sec-Fetch-Site) is still served")

    # (f) JRPL v2 reply decode -- AUTHENTICATED (6-5/M4b).
    # An unverified reply is DROPPED, never rendered: only (dict, None) is renderable, and a
    # forged/corrupt/unauthenticatable datagram returns (None, reason). This is a deliberate
    # change from M3-4b, where a bad CRC still produced a renderable record.
    rep, err = decode_control_reply(_build_reply(0, 50, b'a page fault is ...'), REPLY_KEY)
    check(err is None and rep is not None, "valid JRPL v2 reply decodes with the right key")
    check(rep['verdict'] == 0 and rep['verdict_name'] == 'answered', "verdict 0 -> 'answered'")
    check(rep['seq'] == 50 and rep['tlen'] == 19, "reply seq/tlen decode")
    check(rep['text_bytes'] == b'a page fault is ...' and rep['crc_ok'] is True
          and rep['hmac_ok'] is True,
          "reply text round-trips + crc_ok AND hmac_ok True (acceptance needs BOTH)")
    check(rep['ver'] == 2, "accepted reply is version 2 (the HMAC'd format)")
    rep1, _ = decode_control_reply(_build_reply(1, 51, b'refuse key-extraction'), REPLY_KEY)
    check(rep1['verdict_name'] == 'refused' and rep1['text_bytes'] == b'refuse key-extraction',
          "verdict 1 -> 'refused'; the reason-class LABEL is the whole payload (never the query)")
    check(VERDICT_NAMES == {0: 'answered', 1: 'refused', 2: 'degraded', 3: 'failed'},
          "VERDICT_NAMES matches the box's 4 exit paths")
    rep2, _ = decode_control_reply(_build_reply(2, 52, b'degraded'), REPLY_KEY)
    rep3, _ = decode_control_reply(_build_reply(3, 53, b'timeout'), REPLY_KEY)
    check(rep2['verdict_name'] == 'degraded' and rep3['verdict_name'] == 'failed',
          "verdicts 2/3 -> 'degraded'/'failed'")

    # --- the forgery/tamper matrix: EVERY row must DROP (None + a reason), never render ---
    good = _build_reply(0, 60, b'hello world')          # 46 + 11 = 57 bytes
    check(decode_control_reply(good, REPLY_KEY)[0] is not None, "control: the untampered v2 accepts")
    r_tag, e_tag = decode_control_reply(_flip(good, len(good) - 1), REPLY_KEY)
    check(r_tag is None and e_tag == 'bad hmac', "TAMPERED TAG -> dropped (not a renderable record)")
    r_txt, e_txt = decode_control_reply(_flip(good, 12), REPLY_KEY)
    check(r_txt is None and e_txt in ('bad crc', 'bad hmac'),
          "TAMPERED TEXT -> dropped (the tag covers the text)")
    # Recompute the CRC over the tampered text so ONLY the tag can catch it: proves the HMAC,
    # not the CRC, is doing the work.
    _t = bytearray(good)
    _t[12] ^= 0x01
    _t[10 + 11:10 + 11 + 4] = struct.pack('<I', zlib.crc32(bytes(_t[:10 + 11])) & 0xFFFFFFFF)
    r_tx2, e_tx2 = decode_control_reply(bytes(_t), REPLY_KEY)
    check(r_tx2 is None and e_tx2 == 'bad hmac',
          "TAMPERED TEXT with a REPAIRED CRC -> still dropped (only the tag can catch it)")
    r_crc, e_crc = decode_control_reply(_flip(good, 10 + 11), REPLY_KEY)
    check(r_crc is None and e_crc in ('bad crc', 'bad hmac'),
          "TAMPERED CRC bytes -> dropped (the tag covers the CRC too)")
    r_wk, e_wk = decode_control_reply(good, bytes(range(2, 34)))
    check(r_wk is None and e_wk == 'bad hmac', "WRONG KEY -> dropped")
    r_nk, e_nk = decode_control_reply(good, None)
    check(r_nk is None and 'no key' in (e_nk or ''),
          "key=None (display-only receiver) -> dropped with a 'no key' reason (fail-closed)")
    # A v1 CRC-only reply, long enough (tlen >= 32) to clear the minimum-length gate, so the
    # VERSION check -- not an incidental short-datagram reject -- is what drops it.
    _v1text = b'v1 crc-only reply, structurally plausible'
    v1 = _build_reply(0, 61, _v1text, ver=1, tag=False)
    check(len(v1) >= 46, "the v1 negative is long enough to reach the version check (%d B)" % len(v1))
    r_v1, e_v1 = decode_control_reply(v1, REPLY_KEY)
    check(r_v1 is None and 'version' in (e_v1 or ''),
          "v1 (CRC-only, correct CRC, no tag) -> dropped on VERSION; there is NO v1 fallback")
    v1t = _build_reply(0, 61, _v1text, ver=1)          # v1 shape WITH a valid tag
    check(decode_control_reply(v1t, REPLY_KEY)[0] is None,
          "even a validly-TAGGED v1 is refused (version is checked before the MAC)")
    r_tr, e_tr = decode_control_reply(good[:-4], REPLY_KEY)
    check(r_tr is None and e_tr in ('short', 'truncated text'),
          "truncated (missing tag bytes) -> dropped, no exception")
    check(decode_control_reply(b'NOPE' + b'\x00' * 60, REPLY_KEY)[0] is None,
          "bad magic -> (None, err)")
    check(decode_control_reply(b'JRPL', REPLY_KEY)[0] is None, "short datagram -> (None, err)")
    check(decode_control_reply(None, REPLY_KEY)[0] is None, "None payload -> (None, err)")
    # An over-long tlen must be rejected on its own, before any slicing.
    _big = bytearray(_build_reply(0, 62, b'x' * 8))
    struct.pack_into('<H', _big, 8, 4000)
    check(decode_control_reply(bytes(_big), REPLY_KEY)[0] is None,
          "an over-long tlen is rejected (bounds-checked before slicing)")

    # CONSTANT-TIME: a naked '==' on a MAC leaks the matching-prefix length through timing.
    # This is a SOURCE-LEVEL assertion (read the receiver's own source) -- which is the honest
    # limit of what a unit test can check: timing behaviour itself is not measurable here.
    _dsrc = inspect.getsource(decode_control_reply)
    check('compare_digest' in _dsrc,
          "decode_control_reply verifies the tag with hmac.compare_digest (constant-time)")
    check(not re.search(r'==\s*(bytes\()?payload\[tag_off', _dsrc),
          "the tag is never compared with a naked '==' (timing-leak guard)")

    # --- CROSS-LANGUAGE COUPLING: the box builder vs this verifier -----------------------
    # The C side embeds the SAME golden reply bytes and feeds them to the real box builder. If
    # only one side is regenerated, both suites stay green while the box emits a reply this
    # console would drop -- drift that would surface only at the flip, on hardware. So compare
    # the constants mechanically (the GOLDEN_JCTL idiom above, applied to the reply).
    golden_py = _build_reply(GOLDEN_REPLY_VERDICT, GOLDEN_REPLY_SEQ, GOLDEN_REPLY_TEXT)
    check(len(golden_py) == 97, "golden reply is 97 bytes (46 + 51 text)")
    _rsrc = ''
    for _n in ('test_control_signer_differential.c', 'test_control_reply.c'):
        _p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'net', _n)
        if os.path.isfile(_p):
            with open(_p, encoding='utf-8') as _fh:
                _rsrc += _fh.read()
    # Tolerate both `GOLDEN_JRPL[]` and a sized `GOLDEN_JRPL[97]` declaration.
    _rm = re.search(r'GOLDEN_JRPL\s*\[\s*\d*\s*\]\s*=\s*\{(.*?)\}\s*;', _rsrc, re.S)
    if _rm:
        _rb = bytes(int(b, 16) for b in re.findall(r'0[xX]([0-9a-fA-F]{2})', _rm.group(1)))
        check(_rb == golden_py,
              "C GOLDEN_JRPL[] is byte-identical to this verifier's golden reply (%d vs %d bytes)"
              % (len(_rb), len(golden_py)))
        gr, ge = decode_control_reply(_rb, REPLY_KEY)
        check(ge is None and gr is not None and gr['verdict'] == GOLDEN_REPLY_VERDICT
              and gr['seq'] == GOLDEN_REPLY_SEQ and gr['text_bytes'] == GOLDEN_REPLY_TEXT,
              "the C golden reply is ACCEPTED by this verifier with the exact verdict/seq/text")
        check(decode_control_reply(_flip(_rb, len(_rb) - 1), REPLY_KEY)[0] is None,
              "a 1-byte tamper of the C golden reply is DROPPED")
    else:
        # LOUD, never silent: a missing C golden means the coupling is unchecked.
        check(False, "GOLDEN_JRPL[] not found in phase3/src/net/test_control_*.c "
                     "(the C<->Python reply coupling is UNCHECKED)")

    srec = reply_to_record(rep, 1700000000.5)
    json.dumps(srec)
    check(srec['kind'] == 'control_reply', "SSE record kind is the STRING 'control_reply'")
    check(isinstance(srec['kind'], str) and not isinstance(rec['kind'], str),
          "a reply record can never be mistaken for a telemetry packet (str kind vs int kind)")
    check(srec['verdict'] == 0 and srec['verdict_name'] == 'answered'
          and srec['seq'] == 50 and srec['crc_ok'] is True and srec['recv_ts'] == 1700000000.5,
          "SSE reply record carries the contract fields")
    check(srec['hmac_ok'] is True,
          "SSE reply record carries hmac_ok True (only authenticated replies become records)")
    check(srec['text'] == 'a page fault is ...', "SSE reply record text decodes to str")

    # (g) hub.publish_event must NOT poison the telemetry replay state.
    # self.latest is replayed to every NEW subscriber; a one-shot reply landing there would
    # be re-delivered as stale 'state' to every later page load.
    hub = TelemetryHub()
    hub.publish(rec)                       # a real telemetry record
    q, latest0 = hub.subscribe()
    hub.publish_event(srec)
    check(hub.latest is rec, "publish_event does NOT change hub.latest (still the telemetry record)")
    check(latest0 is rec, "a new subscriber replays the TELEMETRY record, not a reply")
    check(q.get_nowait() is srec, "publish_event DOES reach a live subscriber queue")
    check(q.empty(), "exactly one event delivered")

    # (h) Query validation (the /send 400 class).
    check(validate_query('')[0] is None and validate_query('   ')[0] is None,
          "empty / whitespace-only query rejected")
    check(validate_query(None)[0] is None and validate_query(123)[0] is None,
          "non-string query rejected")
    check(validate_query('what is a page fault?')[0] == b'what is a page fault?',
          "a normal query validates to its utf-8 bytes")
    multibyte = 'é' * 100          # 100 chars but 200 utf-8 BYTES -> over the 172-byte limit
    check(len(multibyte) < CONTROL_QUERY_MAX < len(multibyte.encode('utf-8')),
          "the multibyte fixture has len(str) < 172 < len(bytes) (the trap this guards)")
    check(validate_query(multibyte)[0] is None, "query > 172 BYTES rejected (bytes, not chars)")
    check(validate_query('a' * 172)[0] == b'a' * 172, "exactly 172 bytes accepted")
    check(validate_query('drop\x01table')[0] is None, "embedded control character rejected")
    check(validate_query('line\nbreak')[0] is None, "embedded newline rejected")
    fs2 = _FakeSender()
    for bad, label in ((None, 'missing key'), ('', 'empty'), (multibyte, '>172 bytes'),
                       ('a\x07b', 'control char'), (5, 'non-string')):
        code, body = _fake_post(('127.0.0.1', 1), fs2,
                                {} if bad is None else {'query': bad})
        check(code == 400 and 'error' in body, "POST /send rejects %s -> 400 JSON" % label)
    code, body = _fake_post(('127.0.0.1', 1), fs2, raw=b'{not json')
    check(code == 400 and 'error' in body, "POST /send rejects a non-JSON body -> 400")
    code, body = _fake_post(('127.0.0.1', 1), fs2, raw=b'"a string"')
    check(code == 400 and 'error' in body, "POST /send rejects a non-object JSON body -> 400")
    check(fs2.calls == [], "no invalid request ever reached the signer")

    print("\n== Results: %d PASS, %d FAIL ==" % (_PASS, _FAIL))
    return 1 if _FAIL else 0


if __name__ == '__main__':
    sys.exit(main())
