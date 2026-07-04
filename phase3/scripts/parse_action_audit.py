#!/usr/bin/env python3
"""Parse the JARVIS action-audit store from raw sector data (Phase 6 K/M0).

The action-audit store (phase3/src/ai/action_audit.{c,h}) is a raw-LBA circular
store: a 512-byte JACT header sector at ACT_AUDIT_BASE_LBA followed by fixed
512-byte action_audit_rec_t records at +1..+N. Mirrors parse_episodic.py (same
XOR header checksum, same wrap-order reconstruction) and decodes the audit
record schema — the durable evidence base of the it-acts spine (every action
decision: EXECUTED / BLOCKED / PROPOSED, with trust, risk, outcome, trigger).

Usage:
    python3 parse_action_audit.py <file>            # read from a file
    python3 parse_action_audit.py < raw.bin         # read from stdin
    sudo dd if=/dev/nvme0n1 bs=512 skip=21120000 count=4097 | python3 phase3/scripts/parse_action_audit.py
    python3 parse_action_audit.py <file> --json     # dump records as JSON

Importable: parse_header(data) and iter_records(data) are pure stdlib.

NOTE: a partial dump can only reconstruct wrap-order if it contains the WHOLE
ring. Once the buffer has wrapped, dump the full region:
ACT_AUDIT_MAX_ENTRIES + 1 sectors (header + 4096 slots) = 4097.
"""
import json
import struct
import sys

ACT_AUDIT_MAGIC = 0x4A414354          # "JACT"
ACT_RECORD_SIZE = 512
REC_FMT = '<IIQ6H'                     # boot_id,seq,t_ms,action_id,trust,verdict,risk_x100,outcome,trigger_len
REC_HDR_SIZE = struct.calcsize(REC_FMT)   # 28
ACT_TRIGGER_OFF = 28
ACT_TRIGGER_MAX = 456

TRUSTS   = {0: 'AUTO', 1: 'NOTIFY', 2: 'REQUEST', 3: 'REQUIRE'}
VERDICTS = {0: 'EXECUTED', 1: 'BLOCKED', 2: 'PROPOSED'}
OUTCOMES = {0: 'OK', 1: 'FAIL', 2: 'NA'}


def parse_header(data):
    """Parse the 512-byte JACT header. Returns a dict (does not raise on bad magic):
    {magic, version, cursor, total, boot_id, checksum, checksum_ok}."""
    if len(data) < 512:
        raise ValueError("need at least 512 bytes (1 header sector)")
    magic, version, cursor, total, boot_id = struct.unpack_from('<IIIII', data, 0)
    checksum = struct.unpack_from('<I', data, 60)[0]
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
    """Oldest->newest slot order across the wrap (mirrors parse_episodic.py)."""
    if slots <= 0 or total == 0:
        return []
    if total > slots:
        return [(cursor + i) % slots for i in range(slots)]
    return list(range(min(total, slots)))


def iter_records(data):
    """Decode every record present in `data`, oldest->newest (wrap-aware)."""
    hdr = parse_header(data)
    slots = (len(data) // 512) - 1
    order = _wrap_order(hdr['cursor'], hdr['total'], slots)
    out = []
    for slot in order:
        off = (slot + 1) * 512
        if off + 512 > len(data):
            continue
        (boot_id, seq, t_ms, action_id, trust, verdict,
         risk_x100, outcome, trigger_len) = struct.unpack_from(REC_FMT, data, off)
        t = min(trigger_len, ACT_TRIGGER_MAX)
        trigger = data[off + ACT_TRIGGER_OFF:off + ACT_TRIGGER_OFF + t].decode('utf-8', errors='replace')
        out.append({
            'boot_id': boot_id,
            'seq': seq,
            't_ms': t_ms,
            'action_id': action_id,
            'trust_level': trust,
            'trust_name': TRUSTS.get(trust, f'TRUST_{trust}'),
            'verdict': verdict,
            'verdict_name': VERDICTS.get(verdict, f'VERDICT_{verdict}'),
            'risk_x100': risk_x100,
            'outcome': outcome,
            'outcome_name': OUTCOMES.get(outcome, f'OUT_{outcome}'),
            'trigger_len': trigger_len,
            'trigger': trigger,
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
    if hdr['magic'] != ACT_AUDIT_MAGIC:
        print(f"ERROR: bad magic 0x{hdr['magic']:08X} (expected 0x{ACT_AUDIT_MAGIC:08X})")
        sys.exit(1)

    recs = iter_records(data)

    if as_json:
        print(json.dumps(recs, indent=2))
        return

    chk = "OK" if hdr['checksum_ok'] else "MISMATCH"
    print("=== JARVIS Action-Audit Store ===")
    print(f"  Version:   {hdr['version']}")
    print(f"  Boot ID:   {hdr['boot_id']}")
    print(f"  Cursor:    {hdr['cursor']}")
    print(f"  Total:     {hdr['total']}")
    print(f"  Checksum:  0x{hdr['checksum']:08X} ({chk})")
    print(f"  Sectors:   {len(data) // 512} read")
    print(f"  Records:   {len(recs)} decoded")
    print()
    for e in recs:
        print(f"  [{e['boot_id']}:{e['seq']:05d}] action={e['action_id']} "
              f"{e['trust_name']}/{e['verdict_name']}/{e['outcome_name']} "
              f"risk={e['risk_x100']} trigger=\"{e['trigger']}\"")


if __name__ == '__main__':
    main()
