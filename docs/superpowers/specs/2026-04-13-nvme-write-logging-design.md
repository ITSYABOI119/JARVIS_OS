# NVMe Write Logging — Persistent Telemetry for Bare-Metal seL4

**Date:** 2026-04-13
**Status:** Approved for implementation

## Goal

Enable persistent logging during bare-metal seL4 operation on the JARVIS PC.
Currently the only output is VGA text (80x25 screen, photographed by user).
The system needs to write boot logs, inference results, and stability metrics
to NVMe so they can be recovered from Ubuntu after a test run.

## Approach: Raw Sector Logging (Approach A)

Compared three options:

| Approach | LOC | Risk | Recovery |
|----------|-----|------|----------|
| A. Raw sector logging | ~50 | Zero — writes past all partitions | `dd` from Ubuntu |
| B. Pre-allocated file overwrite | ~150 | Low — overwrites data clusters only | `cat` from Ubuntu |
| C. Full FAT32 write | ~500-800 | High — FAT metadata corruption on power loss | Native file access |

**Decision: Approach A.** Rationale:
1. **Proven pattern.** Phase 2's `warm_reboot.c` does exactly this on SD card (raw sector at known LBA with magic/checksum header). 30+ days of stability testing without corruption.
2. **Zero corruption risk.** Writes to LBAs past all partitions. The 770MB model file on FAT32 is never endangered.
3. **~50 LOC total.** nvme_write_sectors is ~25 LOC (one opcode change from nvme_read_sectors). Log module is ~25 LOC more.
4. **Self-initializing.** No pre-creation step from Ubuntu needed. Invalid magic → fresh header.

## NVMe Write Implementation

`nvme_write_sectors()` is identical to `nvme_read_sectors()` except one line:
```c
cmd.opcode = NVME_IO_WRITE;  /* was NVME_IO_READ */
```

`NVME_IO_WRITE = 0x01` is already defined in `nvme.h:49`. PRP setup, CDW10/11/12 encoding, 8KB-per-command limit, polled completion — all identical.

## Log Region Layout

Reserve 256 MB (512K sectors) starting at a fixed LBA well past all partitions.
Two options for base LBA:
- Fixed: `0x10000000` (128GB offset — safe on 1TB drive)
- Dynamic: `ns_lba_count - 524288` (last 256MB of drive)

```
Sector 0 (header):
  [0..3]   magic         0x4A524C47 ("JRLG" — JARVIS Log)
  [4..7]   version       1
  [8..11]  write_cursor  (next sector offset from sector 1)
  [12..15] total_entries
  [16..19] boot_id       (incremented each boot)
  [20..59] reserved
  [60..63] checksum      (XOR of words 0..14)

Sectors 1..N (log entries, 512 bytes each):
  [0..3]   timestamp_ms  (RDTSC-derived)
  [4..7]   boot_id
  [8..11]  entry_type    (BOOT, SELF_TEST, MODEL_LOAD, QUERY, RESPONSE, STATS, ERROR)
  [12..15] payload_len
  [16..511] payload       (text, NUL-padded)
```

## Log Size Estimate (30-day stability test)

| Parameter | Value |
|-----------|-------|
| Queries/hour (Llama 1B @ 1 tok/s) | ~180 |
| Records/query (request + response) | 2 |
| Records/day | ~8,640 |
| Records/30 days | ~259,200 |
| Bytes (512 B/record) | ~127 MB |
| Reserved region | 256 MB (2x margin) |

## Recovery from Ubuntu

```bash
# Read log header
sudo dd if=/dev/nvme0n1 bs=512 skip=$((0x10000000)) count=1 | hexdump -C

# Extract all log records
sudo dd if=/dev/nvme0n1 bs=512 skip=$((0x10000000 + 1)) count=259200 | strings > jarvis_log.txt

# Or use a Python parser for structured extraction
python3 phase3/scripts/parse_nvme_log.py /dev/nvme0n1
```

## Files to Create/Modify

| File | Change |
|------|--------|
| `phase3/src/drivers/nvme.c` | Add `nvme_write_sectors()` (~25 LOC) |
| `phase3/src/drivers/nvme.h` | Add write function declaration |
| `phase3/src/drivers/nvme_log.c` | New: log init/append/flush (~80 LOC) |
| `phase3/src/drivers/nvme_log.h` | New: log API |
| `phase3/src/sel4/main_x86.c` | Call log_init after NVMe init, log events throughout |
| `phase3/scripts/parse_nvme_log.py` | New: extract + format log from raw device |

## Testing Plan

1. **QEMU:** qemu_test.sh already creates a FAT32 NVMe image. Add write test: write a sector, read it back, verify. Run `hexdump` on the image file to confirm.
2. **Bare metal:** Write a test log entry during self-test. Reboot to Ubuntu, `dd` the log region, verify magic + content.
3. **30-day integration:** Log module called from IPC workload loop. Recovery script runs daily from Ubuntu (cron or manual).

## QEMU Verification

The existing `phase3/scripts/qemu_test.sh` creates an NVMe disk image. Write tests can verify against it:
```bash
# After QEMU run, check the log region in the disk image:
dd if=/tmp/jarvis_nvme.img bs=512 skip=$((0x10000000)) count=10 | hexdump -C
```
