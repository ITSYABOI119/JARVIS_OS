#!/usr/bin/env python3
"""Parse JARVIS NVMe log from raw sector data.

Usage:
    python3 parse_nvme_log.py <file>       # read from file
    python3 parse_nvme_log.py < raw.bin    # read from stdin

Recovery from the box:
    sudo dd if=/dev/nvme0n1 bs=512 skip=4000794624 count=2701 | python3 parse_nvme_log.py

A6a: the decode is exposed as parse_header() / iter_records() so
phase3/scripts/test_parse_nvme_log.py can assert on it directly, matching
parse_episodic.py and parse_action_audit.py. main() is now only presentation --
a round-trip test that had to scrape stdout would be asserting on the print
format rather than on the decode.
"""
import struct
import sys

MAGIC = 0x4A524C47
TYPES = {1: 'BOOT', 2: 'SELFTEST', 3: 'MODEL_LOAD', 4: 'INFER',
         5: 'IPC_STATS', 6: 'ERROR', 7: 'HEARTBEAT'}

SECTOR = 512
HDR_FMT = '<IIIII'          # magic, version, cursor, total_entries, boot_id
ENTRY_FMT = '<IIHH'         # boot_id, seq, type, length
PAYLOAD_OFF = 16            # after boot_id:4, seq:4, type:2, length:2, reserved:4


def parse_header(data):
    """Decode the header sector. Raises ValueError on a short buffer or bad magic.

    Returns a dict; `checksum_ok` is the XOR of words 0..14 compared against word 15.
    """
    if len(data) < SECTOR:
        raise ValueError("need at least %d bytes (1 sector), got %d" % (SECTOR, len(data)))
    magic, ver, cursor, total, boot_id = struct.unpack_from(HDR_FMT, data, 0)
    if magic != MAGIC:
        raise ValueError("bad magic 0x%08X (expected 0x%08X)" % (magic, MAGIC))
    checksum = struct.unpack_from('<I', data, 60)[0]
    computed = 0
    for w in struct.unpack_from('<15I', data, 0):
        computed ^= w
    return {
        'magic': magic,
        'version': ver,
        'cursor': cursor,
        'total_entries': total,
        'boot_id': boot_id,
        'checksum': checksum,
        'checksum_computed': computed,
        'checksum_ok': computed == checksum,
        'slots': (len(data) // SECTOR) - 1,
        'wrapped': total > ((len(data) // SECTOR) - 1),
    }


def record_order(hdr):
    """The slot indices to read, OLDEST -> NEWEST.

    The modulus is `slots`, derived from the buffer actually supplied, NOT
    NVME_LOG_MAX_ENTRIES -- a dump can be shorter than the region. That means a
    WRAPPED capture only reproduces nvme_log.c's ordering when it contains every
    slot; a partial wrapped capture rotates by a different modulus. Fixtures in
    test_parse_nvme_log.py are written full-size for exactly this reason.
    """
    slots, total, cursor = hdr['slots'], hdr['total_entries'], hdr['cursor']
    if slots <= 0 or total == 0:
        return []
    if total > slots:
        # Wrapped: the buffer holds the latest `slots`; the OLDEST sits at `cursor`.
        return [(cursor + i) % slots for i in range(slots)]
    return list(range(min(total, slots)))


def iter_records(data, hdr=None):
    """Yield decoded entries in wrap order (oldest -> newest)."""
    if hdr is None:
        hdr = parse_header(data)
    for slot in record_order(hdr):
        off = (slot + 1) * SECTOR
        if off + SECTOR > len(data):
            continue
        e_boot, e_seq, e_type, e_len = struct.unpack_from(ENTRY_FMT, data, off)
        payload = data[off + PAYLOAD_OFF: off + PAYLOAD_OFF + e_len]
        yield {
            'slot': slot,
            'boot_id': e_boot,
            'seq': e_seq,
            'type': e_type,
            'type_name': TYPES.get(e_type, 'UNK_%d' % e_type),
            'length': e_len,
            'payload': payload.decode('utf-8', errors='replace').rstrip('\x00'),
        }


def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1], 'rb') as f:
            data = f.read()
    else:
        data = sys.stdin.buffer.read()

    try:
        hdr = parse_header(data)
    except ValueError as e:
        print("ERROR: %s" % e)
        sys.exit(1)

    chk = "OK" if hdr['checksum_ok'] else "MISMATCH (computed 0x%08X)" % hdr['checksum_computed']
    print("=== JARVIS NVMe Log ===")
    print("  Version:   %d" % hdr['version'])
    print("  Boot ID:   %d" % hdr['boot_id'])
    print("  Cursor:    %d" % hdr['cursor'])
    print("  Total:     %d" % hdr['total_entries'])
    print("  Checksum:  0x%08X (%s)" % (hdr['checksum'], chk))
    print("  Sectors:   %d read" % (len(data) // SECTOR))
    print()

    if hdr['slots'] <= 0 or hdr['total_entries'] == 0:
        print("(no log entries)")
    elif hdr['wrapped']:
        print("--- %d entries (rolling: wrapped, showing latest %d of %d lifetime) ---"
              % (hdr['slots'], hdr['slots'], hdr['total_entries']))
    else:
        print("--- %d entries ---" % min(hdr['total_entries'], hdr['slots']))

    for r in iter_records(data, hdr):
        print("  [%d:%05d] %-12s %s" % (r['boot_id'], r['seq'], r['type_name'], r['payload']))


if __name__ == '__main__':
    main()
