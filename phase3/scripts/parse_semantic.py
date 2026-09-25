#!/usr/bin/env python3
"""Parse the JARVIS semantic fact store (JSEM) from raw sector data.

The semantic store (phase3/src/ai/semantic_store.{c,h}) is a raw-LBA circular
store: a 512-byte JSEM header sector at SEM_STORE_BASE_LBA (21,110,000)
followed by 4096 fixed 512-byte semantic_fact_t records at +1..+4096. The
header's checksum is the XOR of its first 15 little-endian u32 words, compared
with the word at offset 60 - the episodic/nvme_log discipline.

The region has two writers: the box's own gated distill (JARVIS_SEMANTIC, 0 in
every deployed image) and the Main PC's offline projection
(phase7/memory/jarvis_memory/project.py, fact_type SEM_FACT_PROFILE = 2).

Usage:
    python3 parse_semantic.py <file>            # read from a file
    python3 parse_semantic.py < raw.bin         # read from stdin
    python3 parse_semantic.py - --json          # stdin, records as JSON
    dd if=/dev/nvme0n1 bs=512 skip=21110000 count=4097 iflag=direct | python3 parse_semantic.py

Importable: parse_header(buf) and iter_records(buf) are pure stdlib.

The buffer must be the WHOLE region, exactly 4097 sectors: a partial dump
reconstructs the wrap order against the wrong modulus - parse_episodic.py's
recorded trap - so both functions refuse any other length.
"""
import json
import struct
import sys

SECTOR = 512
MAX_FACTS = 4096
MAGIC = 0x4A53454D            # "JSEM"
VERSION = 1
TEXT_MAX = 440
REGION_BYTES = (MAX_FACTS + 1) * SECTOR    # 2,097,664

REC_FMT = '<IIQQHHHH'         # boot_id, seq, t_ms, key, fact_type, support_count, confidence_x100, text_len
REC_HDR_SIZE = struct.calcsize(REC_FMT)    # 32 - the text starts here
FACT_TYPES = {1: 'QA', 2: 'PROFILE'}


def _check_len(buf):
    if len(buf) != REGION_BYTES:
        raise ValueError(
            'need exactly %d bytes (the whole JSEM region, 4097 sectors), got %d: a partial dump '
            'reconstructs the wrap order against the wrong modulus' % (REGION_BYTES, len(buf)))


def parse_header(buf):
    """The JSEM header: {magic, version, cursor, total_entries, boot_id, checksum, checksum_ok}."""
    _check_len(buf)
    magic, version, cursor, total, boot_id = struct.unpack_from('<IIIII', buf, 0)
    checksum = struct.unpack_from('<I', buf, 60)[0]
    computed = 0
    for w in struct.unpack_from('<15I', buf, 0):
        computed ^= w
    return {
        'magic': magic,
        'version': version,
        'cursor': cursor,
        'total_entries': total,
        'boot_id': boot_id,
        'checksum': checksum,
        'checksum_ok': computed == checksum,
    }


def iter_records(buf):
    """The stored facts, oldest to newest, exactly as the box's sem_store_read would return them.

    [] unless magic, version and checksum all hold - the box treats any other header as absent.
    Otherwise min(total_entries, 4096) records; logical i lives in slot i before the store wraps
    and in slot (cursor + i) % 4096 after it.
    """
    hdr = parse_header(buf)
    if not (hdr['magic'] == MAGIC and hdr['version'] == VERSION and hdr['checksum_ok']):
        return []
    total = hdr['total_entries']
    count = min(total, MAX_FACTS)
    cursor = hdr['cursor'] % MAX_FACTS     # the box clamps a stale cursor the same way
    out = []
    for i in range(count):
        slot = i if total < MAX_FACTS else (cursor + i) % MAX_FACTS
        off = (slot + 1) * SECTOR
        (boot_id, seq, t_ms, key, fact_type, support, conf, text_len) = struct.unpack_from(REC_FMT, buf, off)
        n = min(text_len, TEXT_MAX)
        text = buf[off + REC_HDR_SIZE:off + REC_HDR_SIZE + n].decode('ascii', errors='replace')
        out.append({
            'slot': slot,
            'boot_id': boot_id,
            'seq': seq,
            't_ms': t_ms,
            'key': key,
            'fact_type': fact_type,
            'support_count': support,
            'confidence_x100': conf,
            'text_len': text_len,
            'text': text,
        })
    return out


def main(argv=None):
    args = list(sys.argv[1:] if argv is None else argv)
    as_json = '--json' in args
    args = [a for a in args if a != '--json']
    if len(args) > 1:
        print('usage: parse_semantic.py [--json] [FILE]', file=sys.stderr)
        return 64
    if not args or args[0] == '-':
        buf = sys.stdin.buffer.read()
    else:
        with open(args[0], 'rb') as fh:
            buf = fh.read()
    try:
        hdr = parse_header(buf)
    except ValueError as exc:
        print('ERROR: %s' % exc, file=sys.stderr)
        return 1
    recs = iter_records(buf)
    if as_json:
        print(json.dumps({'header': hdr, 'records': recs}, indent=2))
        return 0
    ok = hdr['magic'] == MAGIC and hdr['version'] == VERSION and hdr['checksum_ok']
    print('=== JARVIS Semantic Store (JSEM) ===')
    print('  Magic:     0x%08X (%s)' % (hdr['magic'], 'OK' if hdr['magic'] == MAGIC else 'expected 0x%08X' % MAGIC))
    print('  Version:   %d' % hdr['version'])
    print('  Boot ID:   %d' % hdr['boot_id'])
    print('  Cursor:    %d' % hdr['cursor'])
    print('  Total:     %d' % hdr['total_entries'])
    print('  Checksum:  0x%08X (%s)' % (hdr['checksum'], 'OK' if hdr['checksum_ok'] else 'MISMATCH'))
    print('  Records:   %d decoded%s' % (len(recs), '' if ok else ' (the box treats this header as ABSENT)'))
    print()
    for r in recs:
        print('  [slot %4d] boot %d seq %d t_ms %d key 0x%016x %s support %d conf %d len %d  %s' % (
            r['slot'], r['boot_id'], r['seq'], r['t_ms'], r['key'],
            FACT_TYPES.get(r['fact_type'], 'type_%d' % r['fact_type']), r['support_count'],
            r['confidence_x100'], r['text_len'], r['text']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
