#!/usr/bin/env python3
"""Parse JARVIS NVMe log from raw sector data.

Usage:
    python3 parse_nvme_log.py <file>       # read from file
    python3 parse_nvme_log.py < raw.bin    # read from stdin
"""
import struct
import sys

MAGIC = 0x4A524C47
TYPES = {1:'BOOT', 2:'SELFTEST', 3:'MODEL_LOAD', 4:'INFER', 5:'IPC_STATS', 6:'ERROR', 7:'HEARTBEAT'}

def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1], 'rb') as f:
            data = f.read()
    else:
        data = sys.stdin.buffer.read()
    if len(data) < 512:
        print("ERROR: need at least 512 bytes (1 sector)")
        sys.exit(1)

    # Parse header
    magic, ver, cursor, total, boot_id = struct.unpack_from('<IIIII', data, 0)
    checksum = struct.unpack_from('<I', data, 60)[0]

    if magic != MAGIC:
        print(f"ERROR: bad magic 0x{magic:08X} (expected 0x{MAGIC:08X})")
        sys.exit(1)

    # Verify checksum (XOR of words 0..14)
    words = struct.unpack_from('<15I', data, 0)
    computed = 0
    for w in words:
        computed ^= w
    chk_ok = "OK" if computed == checksum else f"MISMATCH (computed 0x{computed:08X})"

    print(f"=== JARVIS NVMe Log ===")
    print(f"  Version:   {ver}")
    print(f"  Boot ID:   {boot_id}")
    print(f"  Cursor:    {cursor}")
    print(f"  Total:     {total}")
    print(f"  Checksum:  0x{checksum:08X} ({chk_ok})")
    print(f"  Sectors:   {len(data) // 512} read")
    print()

    # Parse entries
    n_entries = min(cursor, (len(data) // 512) - 1)
    if n_entries == 0:
        print("(no log entries)")
    else:
        print(f"--- {n_entries} entries ---")
    for i in range(n_entries):
        off = (i + 1) * 512
        if off + 512 > len(data):
            break
        e_boot, e_seq, e_type, e_len = struct.unpack_from('<IIHH', data, off)
        # Payload starts at offset 16 (after boot_id:4, seq:4, type:2, length:2, reserved:4)
        payload = data[off+16:off+16+e_len].decode('utf-8', errors='replace').rstrip('\x00')
        tname = TYPES.get(e_type, f'UNK_{e_type}')
        print(f"  [{e_boot}:{e_seq:05d}] {tname:12s} {payload}")

if __name__ == '__main__':
    main()
