#!/usr/bin/env python3
"""C->Python round-trip for the episodic store (Phase 5 Goal #1 / M2).

The C test (test_episodic_store.c --dump <path>) writes a FIXED known fixture to
a raw region file; this parses it with parse_episodic.py and asserts every field
exact PLUS query_key == python_fnv1a(normalize(query)). That pins BOTH the
512-byte epi_record_t on-disk layout AND the decision-cache normalization parity
across C and Python — a layout shift or a normalization divergence fails the key
check.

Usage: python3 test_parse_episodic.py <fixture.bin>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_episodic  # noqa: E402  (resolved alongside this script)

EPI_STORE_MAGIC = 0x4A455049

# MUST match the fixture written by test_episodic_store.c dump_fixture() exactly.
# (t_ms, query, action, action_name, outcome, outcome_name, feedback, resp_or_None)
FIXTURE = [
    (100, "  Hello   World  ", 1, "CACHE", 0, "OK",    0, "hi there"),
    (200, "status check",      2, "INFER", 0, "OK",    0, "all systems nominal"),
    (300, "DELETE everything", 1, "CACHE", 1, "ERROR", 1, None),
    (400, "what time is it",   2, "INFER", 0, "OK",    0, "noon"),
    (500, "PING",              1, "CACHE", 0, "OK",    2, "pong"),
]

# ---- decision-cache parity: reimplement cache_normalize_query + cache_hash ----
_WS = b" \t\n\x0b\x0c\r"   # C-locale isspace()


def normalize(raw_bytes, max_len=128):
    """Mirror cache_normalize_query (C locale): skip leading ws, lowercase A-Z,
    collapse internal ws runs to a single space, strip trailing space, and cap at
    max_len-1 bytes (MAX_QUERY_LEN == 128)."""
    i, n = 0, len(raw_bytes)
    out = bytearray()
    while i < n and raw_bytes[i] in _WS:
        i += 1
    prev_space = False
    while i < n and len(out) < max_len - 1:
        c = raw_bytes[i]
        if c in _WS:
            if not prev_space and len(out) > 0:
                out.append(0x20)
                prev_space = True
        else:
            if 0x41 <= c <= 0x5A:   # A-Z -> a-z
                c += 0x20
            out.append(c)
            prev_space = False
        i += 1
    if out and out[-1] == 0x20:
        out.pop()
    return bytes(out)


def fnv1a64(data):
    h = 0xcbf29ce484222325
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


_passed = 0
_failed = 0


def check(cond, msg):
    global _passed, _failed
    if cond:
        _passed += 1
    else:
        _failed += 1
        print(f"  FAIL: {msg}")


def main():
    if len(sys.argv) < 2:
        print("usage: test_parse_episodic.py <fixture.bin>")
        sys.exit(2)
    with open(sys.argv[1], 'rb') as f:
        data = f.read()

    hdr = parse_episodic.parse_header(data)
    check(hdr['magic'] == EPI_STORE_MAGIC, f"header magic == JEPI (got 0x{hdr['magic']:08X})")
    check(hdr['checksum_ok'], "header checksum XOR matches")
    check(hdr['boot_id'] == 1, f"fresh boot_id == 1 (got {hdr['boot_id']})")
    check(hdr['total'] == len(FIXTURE), f"total == {len(FIXTURE)} (got {hdr['total']})")

    recs = parse_episodic.iter_records(data)
    check(len(recs) == len(FIXTURE), f"decoded {len(FIXTURE)} records (got {len(recs)})")

    if len(recs) == len(FIXTURE):
        for i, (t_ms, query, action, aname, outcome, oname, feedback, resp) in enumerate(FIXTURE):
            r = recs[i]
            exp_resp = resp if resp is not None else ""
            check(r['boot_id'] == 1, f"rec{i} boot_id == 1 (got {r['boot_id']})")
            check(r['seq'] == i, f"rec{i} seq == {i} (got {r['seq']})")
            check(r['t_ms'] == t_ms, f"rec{i} t_ms == {t_ms} (got {r['t_ms']})")
            check(r['action'] == action, f"rec{i} action == {action} (got {r['action']})")
            check(r['action_name'] == aname, f"rec{i} action_name == {aname} (got {r['action_name']})")
            check(r['outcome'] == outcome, f"rec{i} outcome == {outcome} (got {r['outcome']})")
            check(r['outcome_name'] == oname, f"rec{i} outcome_name == {oname} (got {r['outcome_name']})")
            check(r['feedback'] == feedback, f"rec{i} feedback == {feedback} (got {r['feedback']})")
            check(r['query'] == query, f"rec{i} query == {query!r} (got {r['query']!r})")
            check(r['resp'] == exp_resp, f"rec{i} resp == {exp_resp!r} (got {r['resp']!r})")
            # The C<->Python key-parity proof: pins on-disk layout AND normalization.
            expect_key = fnv1a64(normalize(query.encode('utf-8')))
            check(r['query_key'] == expect_key,
                  f"rec{i} query_key == fnv1a(normalize(query)) "
                  f"(got 0x{r['query_key']:016X}, want 0x{expect_key:016X})")

    total = _passed + _failed
    print(f"{_passed}/{total} PASS")
    sys.exit(1 if _failed else 0)


if __name__ == '__main__':
    main()
