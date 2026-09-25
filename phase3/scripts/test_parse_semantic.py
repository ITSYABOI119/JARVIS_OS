#!/usr/bin/env python3
"""C -> Python round trip for the semantic store (JSEM), Phase 7 MS3a.

The C test (test_semantic_store.c --dump <path>) writes a FIXED fixture through
the REAL semantic_store.c - three appends and one upsert - and saves the whole
region. This reads it with parse_semantic.py and asserts every field, so the
512-byte semantic_fact_t layout, the header, and the upsert's rules (support
max, seq kept, boot_id restamped) are pinned across C and Python. It also
proves the reader's refusals and its wrap mapping on buffers built here.

Usage: python3 test_parse_semantic.py <fixture.bin>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_semantic as ps  # noqa: E402  (resolved alongside this script)

# MUST match test_semantic_store.c dump_fixture() exactly:
# (slot, boot_id, seq, t_ms, key, fact_type, support, conf, text_len, text)
FIXTURE = [
    (0, 1, 0, 1000, 0x1, 1, 3, 100, 16, 'fixture fact one'),
    (1, 1, 1, 2500, 0x2, 1, 4, 80, 25, 'fixture fact two, updated'),
    (2, 1, 2, 3000, 0xff, 2, 1, 81, 21, 'fixture profile three'),
]
FIELDS = ('slot', 'boot_id', 'seq', 't_ms', 'key', 'fact_type', 'support_count', 'confidence_x100',
          'text_len', 'text')

_passed = 0
_failed = 0


def check(cond, msg):
    global _passed, _failed
    if cond:
        _passed += 1
        print('  PASS %s' % msg)
    else:
        _failed += 1
        print('  FAIL %s' % msg)


def header_words(magic, version, cursor, total, boot_id):
    """The 16 header words with a correctly recomputed XOR checksum."""
    words = [magic, version, cursor, total, boot_id] + [0] * 10
    cs = 0
    for w in words:
        cs ^= w
    return struct.pack('<16I', *(words + [cs]))


def main():
    if len(sys.argv) != 2:
        print('usage: test_parse_semantic.py <fixture.bin>')
        sys.exit(2)
    with open(sys.argv[1], 'rb') as fh:
        buf = fh.read()

    print('== 1. the header')
    check(len(buf) == ps.REGION_BYTES, 'the fixture is exactly %d bytes (got %d)' % (ps.REGION_BYTES, len(buf)))
    h = ps.parse_header(buf)
    check(h['magic'] == ps.MAGIC and struct.pack('<I', h['magic']) == b'MESJ',
          'magic is JSEM (0x%08X)' % h['magic'])
    check(h['version'] == 1, 'version 1 (got %d)' % h['version'])
    check(h['cursor'] == 3, 'cursor 3 (got %d)' % h['cursor'])
    check(h['total_entries'] == 3, 'total_entries 3 (got %d)' % h['total_entries'])
    check(h['boot_id'] == 1, 'boot_id 1 (got %d)' % h['boot_id'])
    check(h['checksum_ok'] is True, 'checksum_ok')

    print('== 2. the three records')
    recs = ps.iter_records(buf)
    check(len(recs) == 3, 'three records (got %d)' % len(recs))
    for want in FIXTURE:
        got = recs[want[0]] if want[0] < len(recs) else {}
        exp = dict(zip(FIELDS, want))
        bad = {k: (got.get(k), v) for k, v in exp.items() if got.get(k) != v}
        check(not bad, 'record %d field for field%s' % (want[0], '' if not bad else ' - differs %r' % bad))
    up = recs[1] if len(recs) > 1 else {}
    check(up.get('support_count') == max(4, 2), 'the upsert raises support to max(4, 2) = 4')
    check(up.get('seq') == 1, "the upsert keeps the fact's seq (1)")
    check(up.get('boot_id') == 1, "the upsert restamps boot_id to the store's current boot, 1, not the incoming 0")

    print('== 3. refusal')
    try:
        ps.iter_records(buf[:-ps.SECTOR])
        check(False, 'a buffer one sector short raises ValueError')
    except ValueError as exc:
        check('wrong modulus' in str(exc), 'a buffer one sector short raises ValueError naming the modulus trap')
    try:
        ps.parse_header(buf[:-ps.SECTOR])
        check(False, 'parse_header refuses a buffer one sector short')
    except ValueError:
        check(True, 'parse_header refuses a buffer one sector short')

    print('== 4. a flipped checksum bit')
    flip = bytearray(buf)
    flip[60] ^= 0x01
    hf = ps.parse_header(bytes(flip))
    check(hf['checksum_ok'] is False, 'checksum_ok false after one bit flips in byte 60')
    check(ps.iter_records(bytes(flip)) == [], 'zero records under a bad checksum')

    print('== 5. the wrap mapping')
    img = bytearray(ps.REGION_BYTES)
    img[0:64] = header_words(ps.MAGIC, 1, 4, 4100, 7)
    for slot in range(ps.MAX_FACTS):
        off = (slot + 1) * ps.SECTOR
        img[off:off + 32] = struct.pack('<IIQQHHHH', 0, 0, 0, slot, 1, 0, 0, 0)
    hw = ps.parse_header(bytes(img))
    check(hw['checksum_ok'] is True and hw['total_entries'] == 4100 and hw['cursor'] == 4,
          'the built wrap header: total 4100, cursor 4, checksum recomputed and valid')
    wr = ps.iter_records(bytes(img))
    check(len(wr) == ps.MAX_FACTS, 'a wrapped store reads 4096 records (got %d)' % len(wr))
    check(bool(wr) and wr[0]['slot'] == 4 and wr[0]['key'] == 4, 'logical 0 is slot 4 (key 4)')
    check(len(wr) == ps.MAX_FACTS and wr[4091]['key'] == 4095 and wr[4092]['key'] == 0 and wr[-1]['key'] == 3,
          'logical 4091 is slot 4095, logical 4092 wraps to slot 0, logical 4095 is slot 3')
    check([r['key'] for r in wr] == [(4 + i) % ps.MAX_FACTS for i in range(ps.MAX_FACTS)],
          'every logical i reads slot (4 + i) % 4096')

    total = _passed + _failed
    print()
    print('%d/%d checks passed' % (_passed, total))
    sys.exit(1 if _failed else 0)


if __name__ == '__main__':
    main()
