#!/usr/bin/env python3
"""Parse the JARVIS embedding vector store (JVEC) from raw sector data.

The vector store (phase3/src/ai/embed_store.{c,h}) is a raw-LBA circular store:
a 512-byte JVEC header sector at EMBED_STORE_BASE_LBA followed by 1024-byte
embed_rec_t records, TWO sectors each, at +1.. . This mirrors parse_episodic.py
(same XOR header checksum, same wrap-order reconstruction) and decodes the
vector-record schema.

WHY THIS EXISTS: every other raw-LBA store had an off-box reader (nvme_log,
episodic, JACT) and this one did not, so the box's stored vectors could only be
inspected by re-embedding on the host — which carries the measured 0.0094
box-vs-host cosine delta into decisions taken against a 0.55 floor.

Usage:
    python3 parse_embed_store.py <file>              # human-readable
    python3 parse_embed_store.py <file> --json       # machine-readable
    python3 parse_embed_store.py /dev/stdin          # from a pipe

Recovery recipe (the whole region; 1 header + 4096 slots x 2 sectors):
    sudo dd if=/dev/nvme0n1 bs=512 skip=21150000 count=8193 \
      | python3 phase3/scripts/parse_embed_store.py /dev/stdin

Importable: parse_header(data) and iter_records(data) are pure stdlib.

NOTE: as with the episodic parser, a PARTIAL dump cannot reconstruct wrap order —
that needs the whole ring. The wrap modulus is derived from the buffer handed in
(slots = (len - 512) // 1024), so a truncated capture rotates by a different
modulus than the writer used and both sides look self-consistent while being
wrong. Dump all 8193 sectors.
"""
import json
import struct
import sys

EMBED_STORE_MAGIC = 0x4A564543        # "JVEC"
EMBED_HDR_SIZE = 512
EMBED_REC_SIZE = 1024                 # EMBED_STORE_SECTORS_PER_REC (2) * 512
EMBED_DIM = 128

# Field offsets — mirrors the C _Static_asserts in embed_store.h, which pin every
# one of these. The struct is deliberately NOT packed (it has no holes by
# construction); the asserts are the contract, so these numbers are quoted from
# them rather than inferred from a struct layout.
OFF_VEC        = 0      # float32 * 128  (one full sector)
OFF_OWNER_KEY  = 512    # uint64
OFF_BOOT_ID    = 520    # uint32
OFF_SEQ        = 524    # uint32
OFF_OWNER_SEQ  = 528    # uint32
OFF_DIM        = 532    # uint32
OFF_STATUS     = 536    # uint16
OFF_RESERVED16 = 538    # uint16
OFF_CHECKSUM   = 540    # uint32

STATUS_NAMES = {0: 'UNSET', 1: 'VALID'}


def _xor_words(buf, nwords, skip_word=None):
    """XOR of `nwords` little-endian uint32 words, optionally skipping one index.

    Reproduces compute_header_checksum() / embed_rec_checksum() from
    embed_store.c EXACTLY (both are memcpy-based XOR folds). Kept as one helper
    because the two differ only in width and skip index — writing them twice is
    how the two drift apart.
    """
    cs = 0
    for i in range(nwords):
        if skip_word is not None and i == skip_word:
            continue
        cs ^= struct.unpack_from('<I', buf, i * 4)[0]
    return cs


def parse_header(data):
    """Decode the JVEC header sector. Returns a dict; 'checksum_ok' is computed."""
    if len(data) < EMBED_HDR_SIZE:
        raise ValueError('short read: %d bytes, need at least %d' % (len(data), EMBED_HDR_SIZE))
    magic, version, cursor, total, boot_id, dim = struct.unpack_from('<IIIIII', data, 0)
    stored_cs = struct.unpack_from('<I', data, 60)[0]     # word 15
    calc_cs = _xor_words(data, 15)                        # words 0..14 (embed_store.c:26-33)
    return {
        'magic': magic,
        # THE ON-DISK BYTE ORDER, not the mnemonic. EMBED_STORE_MAGIC is 0x4A564543,
        # which spells "JVEC" only when written big-endian; stored as a LE u32 the
        # bytes on disk are 43 45 56 4A = "CEVJ". Checking a dump for a literal "JVEC"
        # at offset 0 therefore REJECTS a perfectly good dump (the episodic store is
        # the same: "IPEJ" on disk, not "JEPI"). This project has now hit the
        # little-endian-magic trap three times; the field is named for what it is.
        'magic_bytes_on_disk': struct.pack('<I', magic).decode('ascii', 'replace'),
        'version': version,
        'cursor': cursor,
        'total_entries': total,
        'boot_id': boot_id,
        'dim': dim,
        'checksum': stored_cs,
        'checksum_ok': stored_cs == calc_cs,
        'magic_ok': magic == EMBED_STORE_MAGIC,
    }


def _wrap_order(cursor, total, slots):
    """Logical (oldest->newest) index -> physical slot, mirroring
    logical_to_slot() in embed_store.c: identity until the ring has wrapped,
    then (cursor + i) % slots."""
    if slots <= 0:
        return []
    if total < slots:
        return list(range(min(total, slots)))
    return [(cursor + i) % slots for i in range(slots)]


def parse_record(rec):
    """Decode one 1024-byte embed_rec_t. 'checksum_ok' is computed."""
    owner_key = struct.unpack_from('<Q', rec, OFF_OWNER_KEY)[0]
    boot_id   = struct.unpack_from('<I', rec, OFF_BOOT_ID)[0]
    seq       = struct.unpack_from('<I', rec, OFF_SEQ)[0]
    owner_seq = struct.unpack_from('<I', rec, OFF_OWNER_SEQ)[0]
    dim       = struct.unpack_from('<I', rec, OFF_DIM)[0]
    status    = struct.unpack_from('<H', rec, OFF_STATUS)[0]
    stored_cs = struct.unpack_from('<I', rec, OFF_CHECKSUM)[0]
    # embed_rec_checksum(): XOR over all 256 words with the checksum word skipped.
    calc_cs = _xor_words(rec, EMBED_REC_SIZE // 4, skip_word=OFF_CHECKSUM // 4)
    vec = list(struct.unpack_from('<%df' % EMBED_DIM, rec, OFF_VEC))
    # Sum in double (Python floats are doubles) for the same reason g3_vec_is_unit
    # does: a float32 accumulator over 128 terms drifts enough to make a genuinely
    # unit vector look non-unit.
    l2 = sum(float(x) * float(x) for x in vec) ** 0.5
    return {
        'owner_seq': owner_seq,
        'owner_key': owner_key,
        'boot_id': boot_id,
        'seq': seq,
        'dim': dim,
        'status': status,
        'checksum': stored_cs,
        'checksum_ok': stored_cs == calc_cs,
        'l2_norm': l2,
        'vec': vec,
    }


def iter_records(data):
    """Yield (logical_index, record_dict) in oldest->newest order."""
    hdr = parse_header(data)
    slots = (len(data) - EMBED_HDR_SIZE) // EMBED_REC_SIZE
    for li, slot in enumerate(_wrap_order(hdr['cursor'], hdr['total_entries'], slots)):
        off = EMBED_HDR_SIZE + slot * EMBED_REC_SIZE
        if off + EMBED_REC_SIZE > len(data):
            break
        yield li, parse_record(data[off:off + EMBED_REC_SIZE])


def main():
    argv = list(sys.argv[1:])
    as_json = '--json' in argv
    argv = [a for a in argv if a != '--json']
    if argv:
        with open(argv[0], 'rb') as fh:
            data = fh.read()
    else:
        data = sys.stdin.buffer.read()

    hdr = parse_header(data)
    recs = [r for _, r in iter_records(data)]

    if as_json:
        out = {'header': hdr, 'records': recs}
        json.dump(out, sys.stdout, indent=2)
        sys.stdout.write('\n')
        return 0 if hdr['magic_ok'] and hdr['checksum_ok'] else 1

    print('=== JARVIS Embedding Vector Store (JVEC) ===')
    print('  Magic:     0x%08X (on-disk bytes %r; the mnemonic JVEC is the BE spelling)%s'
          % (hdr['magic'], hdr['magic_bytes_on_disk'],
             '' if hdr['magic_ok'] else '  *** BAD ***'))
    print('  Version:   %d' % hdr['version'])
    print('  Cursor:    %d' % hdr['cursor'])
    print('  Total:     %d' % hdr['total_entries'])
    print('  Boot ID:   %d' % hdr['boot_id'])
    print('  Dim:       %d' % hdr['dim'])
    print('  Checksum:  0x%08X (%s)' % (hdr['checksum'], 'OK' if hdr['checksum_ok'] else 'BAD'))
    print('  Sectors:   %d read' % (len(data) // 512))
    print('  Records:   %d decoded' % len(recs))
    print()
    for i, r in enumerate(recs):
        print('  [%4d] owner_seq=%-6d owner_key=0x%016x boot=%-3d seq=%-6d dim=%-4d '
              'status=%d(%s) cksum=%s |v|=%.6f'
              % (i, r['owner_seq'], r['owner_key'], r['boot_id'], r['seq'], r['dim'],
                 r['status'], STATUS_NAMES.get(r['status'], '?'),
                 'OK' if r['checksum_ok'] else 'BAD', r['l2_norm']))
    return 0 if hdr['magic_ok'] and hdr['checksum_ok'] else 1


if __name__ == '__main__':
    sys.exit(main())
