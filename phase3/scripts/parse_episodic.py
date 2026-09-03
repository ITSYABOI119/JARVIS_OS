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

# Recall provenance (2026-08-01) — episodic_store.h carved 7 B out of the former pad[24].
# Offsets mirror the C _Static_asserts: recall_kind @488, recall_src_seq @489, cos @493.
EPI_PROV_OFF = 488
REC_PROV_FMT = '<BIH'                     # kind(B) src_seq(I) cos(H) — packed, deliberately unaligned
RECALL_KINDS = {0: 'none', 1: 'exact', 2: 'semantic'}

# The SELECTED-SET half (2026-09-03) — episodic_store.h took 7 more bytes from pad[17], leaving
# pad[10]. Same shape as the first triple, at +7: recall_sel_count @495, src2 @496, cos2 @500.
EPI_PROV2_OFF = 495
REC_PROV2_FMT = '<BIH'                    # sel_count(B) src2_seq(I) cos2(H)


def recall_sel_count_name(kind, sel_count):
    """Render the selected-set COUNT, refusing to guess the one value it cannot know.

    A record written between 2026-08-01 and 2026-09-03 has a recall kind but no count field,
    so the byte reads 0. That is NOT 'one'. [23:00110] is exactly such a record and its real
    count was TWO — the whole reason the field exists — so rendering 0 as 1, or as 'none',
    would restate the very error the widening removes.
    """
    if kind == 0:
        return 'n/a'              # no recall (or a legacy all-zero block): a count says nothing
    if sel_count == 0:
        return 'unknown'          # has a recall source but predates the count field
    return str(sel_count)


def recall_kind_name(kind, src_seq, cos_x1000):
    """Render provenance, distinguishing 'no recall' from 'record predates the field'.

    A record written before 2026-08-01 has pad all-zero, which decodes as kind 0 — the SAME
    bytes a genuine no-recall turn writes. That is genuinely ambiguous and there is deliberately
    no version byte to resolve it (boot_id identifies the era). So an all-zero provenance block
    renders 'unknown' rather than asserting 'none', because boot 46 really did have recall=2 and
    some of those stored zeros are therefore wrong.

    Going forward the ambiguity mostly evaporates on its own: a semantic MISS now stores a
    non-zero cosine, so ANY non-zero byte in the block proves the record is post-change.
    """
    if kind == 0 and src_seq == 0 and cos_x1000 == 0:
        return 'unknown'          # no recall, OR written before the field existed
    if kind == 0:
        return 'miss'             # lane ran, nothing cleared the floor — cos says how close
    return RECALL_KINDS.get(kind, 'kind_%d' % kind)
EPI_QUERY_OFF = 32
EPI_QUERY_MAX = 200
EPI_RESP_OFF = 232
EPI_RESP_MAX = 256

ACTIONS = {1: 'CACHE', 2: 'INFER', 3: 'CONTROLIN', 4: 'CTRLIN-LOCAL'}
# 3 = 6-5 inbound control-IN Q&A answered by the MODEL (EPI_ACT_CONTROL_IN) -- recallable.
# 4 = the same channel, but PA rendered the answer itself: a 6-6 SYSFACTS reply from live box
#     state, or the canned DECLINE (EPI_ACT_CONTROL_IN_LOCAL). Stored and auditable, NEVER
#     recalled -- an answer the box rendered is not a memory.
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
        # Recall provenance (2026-08-01), carved out of the former pad[24]. Read at fixed offsets
        # rather than extending REC_FMT, because REC_FMT deliberately covers only the 32-byte
        # header and query/resp are already read positionally the same way.
        rk_kind, rk_src, rk_cos = struct.unpack_from(REC_PROV_FMT, data, off + EPI_PROV_OFF)
        # Read positionally at the pinned offset, exactly like the first triple, rather than
        # extending REC_FMT — which deliberately covers only the 32-byte header.
        rk_n, rk_src2, rk_cos2 = struct.unpack_from(REC_PROV2_FMT, data, off + EPI_PROV2_OFF)
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
            'recall_kind': rk_kind,
            'recall_kind_name': recall_kind_name(rk_kind, rk_src, rk_cos),
            'recall_src_seq': rk_src,
            'recall_cos_x1000': rk_cos,
            'recall_sel_count': rk_n,
            'recall_sel_count_name': recall_sel_count_name(rk_kind, rk_n),
            'recall_src2_seq': rk_src2,
            'recall_cos2_x1000': rk_cos2,
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
    n_unknown = 0
    n_nocount = 0
    for e in recs:
        # Provenance, rendered compactly and only when it says something. 'unknown' is the
        # all-zero block (no recall OR pre-2026-08-01) and is left OFF the line to keep it
        # readable — it is summarised below instead, so the ambiguity is stated once, loudly,
        # rather than repeated on every historical record.
        k = e['recall_kind_name']
        if k == 'unknown':
            n_unknown += 1
            prov = ''
        elif k == 'miss':
            prov = f" recall=miss cos={e['recall_cos_x1000'] / 1000:.3f}"
        elif k == 'semantic':
            prov = (f" recall=semantic src={e['recall_src_seq']}"
                    f" cos={e['recall_cos_x1000'] / 1000:.3f}")
        else:
            prov = f" recall={k} src={e['recall_src_seq']}"
        # The selected-set half. Appended ONLY to a line that already reports a recall, so
        # legacy and miss lines render byte-identically to the pre-2026-09-03 output.
        if k not in ('unknown', 'miss'):
            cn = e['recall_sel_count_name']
            prov += ' n=?' if cn == 'unknown' else f" n={cn}"
            if cn == 'unknown':
                n_nocount += 1
            if e['recall_sel_count'] >= 2:
                prov += (f" src2={e['recall_src2_seq']}"
                         f" cos2={e['recall_cos2_x1000'] / 1000:.3f}")
        print(f"  [{e['boot_id']}:{e['seq']:05d}] {e['action_name']}/{e['outcome_name']} "
              f"key=0x{e['query_key']:016X}{prov} q=\"{e['query']}\" r=\"{e['resp']}\"")
    if n_nocount:
        print()
        print(f"  NOTE: {n_nocount} record(s) have a recall source but no selected-set count:")
        print("        written between 2026-08-01 and the 2026-09-03 widening. The count is")
        print("        UNKNOWN, not one — [23:00110] is such a record and its real count was two,")
        print("        which is why the field exists. Rendered n=? rather than guessed.")
    if n_unknown:
        print()
        print(f"  NOTE: {n_unknown} record(s) carry an all-zero provenance block, which is")
        print("        AMBIGUOUS: either the turn recalled nothing, or it was written before")
        print("        the provenance fields existed (2026-08-01). There is no version byte to")
        print("        tell them apart — boot_id identifies the era. Records from the first boot")
        print("        carrying this field onward are unambiguous, and any non-zero value in the")
        print("        block (a miss now stores its cosine) proves a record is post-change.")


if __name__ == '__main__':
    main()
