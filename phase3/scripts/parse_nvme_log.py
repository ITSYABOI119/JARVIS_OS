#!/usr/bin/env python3
"""Parse JARVIS NVMe log from raw sector data (stdin)."""
import struct
import sys

MAGIC = 0x4A524C47
TYPES = {1:'BOOT', 2:'SELFTEST', 3:'MODEL_LOAD', 4:'INFER', 5:'IPC_STATS', 6:'ERROR', 7:'HEARTBEAT'}

def main():
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

    print(f"=== JARVIS NVMe Log ===")
    print(f"Version: {ver}")
    print(f"Boot ID: {boot_id}")
    print(f"Cursor:  {cursor}")
    print(f"Total:   {total}")
    print(f"Checksum: 0x{checksum:08X}")
    print()

    # Parse entries
    n_entries = min(cursor, (len(data) // 512) - 1)
    for i in range(n_entries):
        off = (i + 1) * 512
        if off + 512 > len(data):
            break
        e_boot, e_seq, e_type, e_len = struct.unpack_from('<IIHH', data, off)
        payload = data[off+12:off+12+e_len].decode('utf-8', errors='replace').rstrip('\x00')
        tname = TYPES.get(e_type, f'UNK_{e_type}')
        print(f"[{e_boot}:{e_seq:05d}] {tname:12s} {payload}")

if __name__ == '__main__':
    main()
