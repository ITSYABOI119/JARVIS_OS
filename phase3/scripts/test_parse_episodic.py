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
import struct
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
    (600, "semantic recall turn", 3, "CONTROLIN", 0, "OK", 0, "answered"),
    (700, "near miss turn",       3, "CONTROLIN", 0, "OK", 0, "un-augmented"),
]

# Recall provenance (2026-08-01), indexed to FIXTURE above:
#   (recall_kind, recall_kind_name, recall_src_seq, recall_cos_x1000)
# Records 0..4 carry the all-zero block a LEGACY record has, so they must render as
# 'unknown' — NOT 'none'. That distinction is the whole point of §4: the same bytes mean
# "did not recall" and "written before the field existed", and the parser must not pick one.
# Record 5 is a semantic hit; record 6 is a below-floor MISS that still carries its cosine,
# which is both the field's reason for existing and the thing that proves a record is
# post-change (any non-zero byte in the block disambiguates it from a legacy record).
FIXTURE_PROV = [
    (0, "unknown",  0,    0),
    (0, "unknown",  0,    0),
    (0, "unknown",  0,    0),
    (0, "unknown",  0,    0),
    (0, "unknown",  0,    0),
    (2, "semantic", 4242, 713),
    (0, "miss",     0,    494),
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


# ---- synthetic raw-region builders for the wrap-order + bad-checksum tests (G4/G5) ----
def _build_header(cursor, total, boot_id, magic=EPI_STORE_MAGIC, version=1):
    """A 512-byte JEPI header sector with a correct XOR checksum over words 0..14
    (mirrors parse_header's check: struct.unpack_from('<15I', ...) XOR == checksum@60)."""
    words = [magic, version, cursor, total, boot_id] + [0] * 10  # 15 checksummed words
    cks = 0
    for w in words:
        cks ^= w
    return struct.pack('<16I', *(words + [cks])) + b'\x00' * (512 - 64)


def _build_record(seq, boot_id, query=b''):
    """A 512-byte epi_record_t (REC_FMT) with a distinguishable seq/boot_id."""
    rec = bytearray(512)
    struct.pack_into('<IIQQHBBHH', rec, 0,
                     boot_id, seq, 1000 + seq, 0, 1, 0, 0, len(query), 0)
    rec[32:32 + len(query)] = query
    return bytes(rec)


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
            # Recall provenance (2026-08-01) — the C writer's bytes decoded at the pinned
            # offsets. This is the half of the round-trip that proves the Python offsets
            # (488/489/493) agree with the C _Static_asserts.
            pk, pname, psrc, pcos = FIXTURE_PROV[i]
            check(r['recall_kind'] == pk, f"rec{i} recall_kind == {pk} (got {r['recall_kind']})")
            check(r['recall_src_seq'] == psrc,
                  f"rec{i} recall_src_seq == {psrc} (got {r['recall_src_seq']})")
            check(r['recall_cos_x1000'] == pcos,
                  f"rec{i} recall_cos_x1000 == {pcos} (got {r['recall_cos_x1000']})")
            check(r['recall_kind_name'] == pname,
                  f"rec{i} recall_kind_name == {pname!r} (got {r['recall_kind_name']!r})")

        # §4 teeth, asserted once rather than per record: a LEGACY all-zero block must render
        # 'unknown', NEVER 'none'. Reading those bytes as "this turn did not recall" would be a
        # false claim about every record written before 2026-08-01 — and boot 46 genuinely had
        # recall=2, so some of those zeros ARE wrong.
        check(all(recs[i]['recall_kind_name'] == 'unknown' for i in range(5)),
              "legacy all-zero provenance renders 'unknown' (never asserts 'none')")
        check(recs[6]['recall_kind_name'] == 'miss' and recs[6]['recall_cos_x1000'] == 494,
              "a below-floor MISS renders 'miss' WITH its cosine — the near-miss made readable")
        check(recs[6]['recall_kind'] == 0 and recs[6]['recall_kind_name'] != 'unknown',
              "kind 0 + non-zero cosine is DISTINGUISHABLE from a legacy record")

    # ---- G4: wrapped region decodes oldest->newest (NOT raw slot order) ----
    # 4 record slots, total=6 (ring rolled twice), cursor=2 -> oldest is slot 2. A real store
    # writing seqs 0..5 round-robin lands [slot0=4, slot1=5, slot2=2, slot3=3]; the parser must
    # reconstruct oldest->newest = [2, 3, 4, 5] (raw slot order would be [4, 5, 2, 3]).
    region = _build_header(cursor=2, total=6, boot_id=2)
    region += b''.join(_build_record(s, boot_id=2) for s in (4, 5, 2, 3))
    wh = parse_episodic.parse_header(region)
    check(wh['checksum_ok'], "G4: wrapped header checksum OK")
    check(wh['cursor'] == 2 and wh['total'] == 6, "G4: wrapped header cursor/total parsed")
    wseqs = [r['seq'] for r in parse_episodic.iter_records(region)]
    check(wseqs == [2, 3, 4, 5],
          f"G4: wrapped region decodes oldest->newest (got {wseqs}, want [2, 3, 4, 5])")

    # ---- G5: a corrupted header byte -> checksum_ok False (no crash) ----
    valid = _build_header(cursor=0, total=2, boot_id=1) + _build_record(0, 1) + _build_record(1, 1)
    check(parse_episodic.parse_header(valid)['checksum_ok'], "G5: valid synthetic header checksum OK")
    bad = bytearray(valid)
    bad[16] ^= 0xFF  # flip a boot_id byte (inside words 0..14, not the checksum field @60)
    check(parse_episodic.parse_header(bytes(bad))['checksum_ok'] is False,
          "G5: corrupted header byte -> checksum_ok False")

    total = _passed + _failed
    print(f"{_passed}/{total} PASS")
    sys.exit(1 if _failed else 0)


if __name__ == '__main__':
    main()
