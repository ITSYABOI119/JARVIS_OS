#!/usr/bin/env python3
"""Parse the JARVIS episodic memory store from raw sector data.

The episodic store (phase3/src/ai/episodic_store.{c,h}) is a raw-LBA circular
store: a 512-byte JEPI header sector at EPI_STORE_BASE_LBA followed by fixed
512-byte epi_record_t records at +1..+N. This mirrors parse_nvme_log.py (same
XOR header checksum, same wrap-order reconstruction) and decodes the episodic
record schema.

Usage:
    python3 parse_episodic.py <file>            # read from a file
    python3 parse_episodic.py < raw.bin         # read from stdin
    dd if=/dev/nvme0n1 bs=512 skip=21100000 count=N | python3 parse_episodic.py
    python3 parse_episodic.py <file> --json     # dump records as JSON

Importable: parse_header(data) and iter_records(data) are pure stdlib.

NOTE: a partial dump can only reconstruct wrap-order if it contains the WHOLE
ring. Once the buffer has wrapped, dump the full region to read every retained
record: EPI_STORE_MAX_ENTRIES + 1 sectors (header + 8192 slots).
"""
import json
import struct
import sys

EPI_STORE_MAGIC = 0x4A455049          # "JEPI"
EPI_RECORD_SIZE = 512
REC_FMT = '<IIQQHBBHH'                 # boot_id,seq,t_ms,query_key,action,outcome,feedback,query_len,resp_len
REC_HDR_SIZE = struct.calcsize(REC_FMT)   # 32
EPI_QUERY_OFF = 32
EPI_QUERY_MAX = 200
EPI_RESP_OFF = 232
EPI_RESP_MAX = 256

ACTIONS = {1: 'CACHE', 2: 'INFER'}
OUTCOMES = {0: 'OK', 1: 'ERROR', 2: 'BLOCKED'}


def parse_header(data):
    """Parse the 512-byte JEPI header. Returns a dict (does not raise on bad magic):
    {magic, version, cursor, total, boot_id, checksum, checksum_ok}."""
    if len(data) < 512:
        raise ValueError("need at least 512 bytes (1 header sector)")
    magic, version, cursor, total, boot_id = struct.unpack_from('<IIIII', data, 0)
    checksum = struct.unpack_from('<I', data, 60)[0]
    # checksum = XOR of words 0..14 (the first 60 bytes), same as nvme_log.
    words = struct.unpack_from('<15I', data, 0)
    computed = 0
    for w in words:
        computed ^= w
    return {
        'magic': magic,
        'version': version,
        'cursor': cursor,
        'total': total,
        'boot_id': boot_id,
        'checksum': checksum,
        'checksum_ok': computed == checksum,
    }


def _wrap_order(cursor, total, slots):
    """Oldest->newest slot order across the wrap (mirrors parse_nvme_log.py)."""
    if slots <= 0 or total == 0:
        return []
    if total > slots:
        # WRAPPED: holds the latest `slots`; oldest is at `cursor`.
        return [(cursor + i) % slots for i in range(slots)]
    # Not wrapped: slots 0..total-1 in write order.
    return list(range(min(total, slots)))


def iter_records(data):
    """Decode every record present in `data`, oldest->newest (wrap-aware).

    Returns a list of dicts; query/resp are decoded query[:query_len] /
    resp[:resp_len] (errors='replace'). Records whose sector is absent from a
    partial dump are skipped.
    """
    hdr = parse_header(data)
    slots = (len(data) // 512) - 1
    order = _wrap_order(hdr['cursor'], hdr['total'], slots)
    out = []
    for slot in order:
        off = (slot + 1) * 512
        if off + 512 > len(data):
            continue
        (boot_id, seq, t_ms, query_key, action,
         outcome, feedback, query_len, resp_len) = struct.unpack_from(REC_FMT, data, off)
        q = min(query_len, EPI_QUERY_MAX)
        r = min(resp_len, EPI_RESP_MAX)
        query = data[off + EPI_QUERY_OFF:off + EPI_QUERY_OFF + q].decode('utf-8', errors='replace')
        resp = data[off + EPI_RESP_OFF:off + EPI_RESP_OFF + r].decode('utf-8', errors='replace')
        out.append({
            'boot_id': boot_id,
            'seq': seq,
            't_ms': t_ms,
            'query_key': query_key,
            'action': action,
            'action_name': ACTIONS.get(action, f'ACT_{action}'),
            'outcome': outcome,
            'outcome_name': OUTCOMES.get(outcome, f'OUT_{outcome}'),
            'feedback': feedback,
            'query_len': query_len,
            'resp_len': resp_len,
            'query': query,
            'resp': resp,
        })
    return out


def main():
    args = list(sys.argv[1:])
    as_json = '--json' in args
    args = [a for a in args if a != '--json']

    if args:
        with open(args[0], 'rb') as f:
            data = f.read()
    else:
        data = sys.stdin.buffer.read()

    if len(data) < 512:
        print("ERROR: need at least 512 bytes (1 sector)")
        sys.exit(1)

    hdr = parse_header(data)
    if hdr['magic'] != EPI_STORE_MAGIC:
        print(f"ERROR: bad magic 0x{hdr['magic']:08X} (expected 0x{EPI_STORE_MAGIC:08X})")
        sys.exit(1)

    recs = iter_records(data)

    if as_json:
        print(json.dumps(recs, indent=2))
        return

    chk = "OK" if hdr['checksum_ok'] else "MISMATCH"
    print("=== JARVIS Episodic Store ===")
    print(f"  Version:   {hdr['version']}")
    print(f"  Boot ID:   {hdr['boot_id']}")
    print(f"  Cursor:    {hdr['cursor']}")
    print(f"  Total:     {hdr['total']}")
    print(f"  Checksum:  0x{hdr['checksum']:08X} ({chk})")
    print(f"  Sectors:   {len(data) // 512} read")
    print(f"  Records:   {len(recs)} decoded")
    print()
    for e in recs:
        print(f"  [{e['boot_id']}:{e['seq']:05d}] {e['action_name']}/{e['outcome_name']} "
              f"key=0x{e['query_key']:016X} q=\"{e['query']}\" r=\"{e['resp']}\"")


if __name__ == '__main__':
    main()
