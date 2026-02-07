# Week 36 Status: SD/EMMC Storage Driver - Part 2

**Status:** COMPLETE
**Target Start:** January 17, 2026
**Actual Effort:** ~12 hours (including debug iterations across multiple sessions)
**Goal:** Complete SD/EMMC driver with write support and validate on hardware.

## Quick Summary

| Item | Value |
|------|-------|
| Latest Build | `Feb 7 2026` |
| Kernel Location | `phase2/firmware/kernel8.img` |
| Final Result | **12 PASS, 0 FAIL** |
| Throughput | **11.9 MB/s** (ADMA2, 131072 bytes in 10989 us) |
| All Blocking Issues | RESOLVED |
| Key Fixes | Write-specific interrupt wait, CSD capacity parsing, dynamic test LBA, uncacheable DMA |

---

## Final Results (February 6, 2026)

**Build:** `Feb 7 2026` (final build with timer + uncacheable DMA)

### Test Results
```
[EMMC] TEST RESULTS: 12 PASS, 0 FAIL
[EMMC] ALL TESTS PASSED
```

| # | Test Name | Description | Status |
|---|-----------|-------------|--------|
| 1 | mmio_map | Device frame mapping | PASS |
| 2 | init | Controller init (clock, card detect) | PASS |
| 3 | read_lba0 | CMD17 single-block read LBA0 | PASS |
| 4 | mbr_sig | MBR signature check (0x55AA) | PASS |
| 5 | part0_lba | Partition 0 start LBA validity | PASS |
| 6 | bpb_sector_size | BPB bytes_per_sector == 512 | PASS |
| 7 | bpb_sig | BPB boot signature (0x55AA) | PASS |
| 8 | multi_read | CMD18 multi-block read (256 blocks) | PASS |
| 9 | throughput | Timing measurement (11.9 MB/s) | **PASS** |
| 10 | null_ptr_check | NULL pointer error handling | PASS |
| 11 | write_single | CMD24 write at dynamic LBA | **PASS** |
| 12 | write_verify | Write read-back verification | **PASS** |

---

## Prerequisites (Week 35 COMPLETE)

- EMMC2 MMIO mapped at vaddr 0x5c2000
- Controller init (400kHz -> 50MHz clock)
- Card detected (SDHC/SDXC)
- Single-block read (CMD17) working
- Multi-block read (CMD18 + AUTO_CMD12) working
- FAT32 BPB parsed correctly
- ADMA2 initialized for reads

## Transfer Modes (Week 36)

| Operation | Mode | Function | Notes |
|-----------|------|----------|-------|
| Multi-block READ | **ADMA2 DMA** | `emmc_read_blocks_adma2()` | Scatter-gather, 128KB buffer |
| Single-block READ | PIO | `emmc_read_block()` | Fallback for small reads |
| Single-block WRITE | **PIO** | `emmc_write_block()` | CPU writes to DATA register |
| Multi-block WRITE | **PIO** | `emmc_write_blocks()` | Loop over single writes |

**Note:** ADMA2 for writes is deferred - PIO is sufficient for Phase 2 boot functionality.

---

## Bug Fix History

### Issue 1: BCM2711 DATA_TIMEOUT During Write (Rounds 1-5)

**Symptom:** `write_single=PASS` but `write_verify=FAIL` (or both FAIL)
**Root Cause:** BCM2711 SDHCI continuously re-asserts DATA_TIMEOUT during write data phase

| Round | Fix | Result |
|-------|-----|--------|
| 1 | DMA vaddr, Timer messaging, DAT_INHIBIT reset | Read tests PASS |
| 2 | DTOCV=max, IXCHK_EN removed | No change |
| 3 | Selective CMD error checking (`emmc_wait_cmd_done()`) | CMD phase OK, data phase fails |
| 4 | Clear stale DATA_TIMEOUT after CMD_DONE (Fix F) | write_single PASS, write_verify FAIL |
| 5 | **`emmc_wait_interrupt_write()`** - check desired bit FIRST, ignore DATA_TIMEOUT | **write_single PASS** |

**Final Solution:** Created `emmc_wait_interrupt_write()` that checks for the desired interrupt
bit (WRITE_RDY / DATA_DONE) BEFORE checking for errors, and excludes DATA_TIMEOUT from the
error mask entirely. This handles the BCM2711 quirk where DATA_TIMEOUT fires continuously
during the write data phase.

### Issue 2: OUT_OF_RANGE Error on Read-Back (Rounds 6-10)

**Symptom:** `write_single=PASS` but `write_verify=FAIL` with `resp0=80000900` (bit 31 = OUT_OF_RANGE)
**Root Cause:** Test LBA 32,000,000 exceeded 16GB SD card capacity (~31.2M sectors)

| Round | Fix | Result |
|-------|-----|--------|
| 6 | Re-enable IRPT_EN/IRPT_MASK after post-write DATA+CMD reset (Fix I) | No change |
| 7 | Re-enable IRPT_EN after every DATA/CMD reset in emmc_send_cmd (Fix J) | No change |
| 8 | Remove DATA+CMD reset entirely, long DAT_INHIBIT wait (Fix K) | Proved IRPT_EN was never the issue |
| 9 | Added CMD17 diagnostic instrumentation | **Revealed OUT_OF_RANGE (resp0 bit 31)** |
| 10 | **Dynamic LBA from CSD capacity + CMD9 parsing** | **12 PASS, 0 FAIL** |

**Key Diagnostic Finding:**
```
Successful read: resp0=00000900  status=1fff0a06 (BUFFER_READ_ENABLE set)
Failed read:     resp0=80000900  status=1fff0206 (no BUFFER_READ_ENABLE)
```
Bit 31 of R1 response = OUT_OF_RANGE. Card silently accepted write CMD24 to invalid LBA,
but refused to send data for read CMD17 at the same LBA.

**Previous Bug:** `emmc_capacity` was hardcoded as 32GB estimate for any SDHC card instead
of reading the actual CSD register. Fixed by implementing CMD9 (SEND_CSD) parsing.

### Issue 3: Hardcoded Test LBA (Design Fix)

**Problem:** `EMMC_WRITE_TEST_LBA` was hardcoded for a specific card size
**Fix:** Test LBA computed dynamically at runtime: `total_sectors - 1024`
- Works on any card (16GB, 32GB, 64GB, etc.)
- Falls back to compile-time constant only if capacity query fails

---

## CSD Capacity Parsing (New Feature)

Added CMD9 (SEND_CSD) during card initialization to read actual card capacity:

```
CSD raw: 00400e00 325b5900 0073df7f 800a4000
CSD v2: C_SIZE=000073df capacity_MB=000039f0 (14,832 MB)
```

- CSD Version 2.0: `C_SIZE` field → `(C_SIZE + 1) * 512KB`
- CSD Version 1.0: `C_SIZE + C_SIZE_MULT + READ_BL_LEN` formula
- Fallback to estimate if CMD9 fails

**Verified:** 16GB card correctly reports 14,832 MB (vs previous 32,768 MB estimate)

---

## Files Modified (Week 36)

| File | Changes | LOC |
|------|---------|-----|
| `emmc_sdhci.c` | CMD24/25 write, CMD9 CSD parsing, write-specific interrupt wait, diagnostics cleanup | ~1300 |
| `emmc_sdhci.h` | Write declarations, safety flags, dynamic LBA comment | 67 |
| `main_arm64.c` | 12-test suite with dynamic LBA computation | ~260 |
| `bcm2711_timer.c` | System Timer driver | 66 |
| `bcm2711_timer.h` | Timer API | 32 |
| `blk_dev.c` | Block device wrapper | 83 |
| `blk_dev.h` | Generic block device interface | 62 |
| `dma_alloc.c` | DMA memory allocator | 310 |
| `slot_alloc.c` | seL4 slot allocator | 99 |

**Total driver code:** ~2,280 LOC

---

## Build & Test

### Rebuild
```bash
wsl -e bash -lc "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/scripts && tr -d '\r' < build_and_copy_kernel.sh | bash"
```

### Copy to SD Card
```cmd
copy phase2\firmware\kernel8.img D:\
```

### Verify
Boot Pi 4 with serial console at 115200 baud. Look for:
- `[EMMC] CSD v2: C_SIZE=XXXXXX capacity_MB=XXXX` (actual card capacity)
- `[EMMC] Write test ENABLED (LBA=XXXXXX, 1024 sectors from end)` (dynamic)
- `[EMMC][TEST] write_single=PASS`
- `[EMMC][TEST] write_verify=PASS`
- `[EMMC] TEST RESULTS: 12 PASS, 0 FAIL`

---

## Week 36 Definition of Done

| Objective | Target | Status |
|-----------|--------|--------|
| Block write (CMD24) | Single 512-byte sector | **PASS** |
| Multi-block write (CMD25) | 8+ sectors | CODE COMPLETE |
| Write verification | Read-back matches written data | **PASS** |
| CSD capacity parsing | Actual card size from CMD9 | **PASS** |
| Dynamic test LBA | Works on any card size | **PASS** |
| Retry logic | 3 attempts on transient errors | IMPLEMENTED |
| Error handling | NULL pointer checks | **PASS** |
| 12-test driver suite | All tests PASS/FAIL/SKIP format | **12 PASS, 0 FAIL** |

**Success Criteria:** `12 PASS, 0 FAIL` on hardware - **MET**

---

## Lesson Learned: seL4 Cache Maintenance

**seL4 does NOT set `SCTLR_EL1.UCI=1`**, so ALL ARM64 cache maintenance instructions
(`DC IVAC`, `DC CIVAC`, `DC CVAC`, `DC CVAU`) trap from EL0 with ESR EC=0x18.

**Solution:** Map DMA buffers as **uncacheable** (`vm_attributes = 0` in `seL4_ARM_Page_Map`)
instead of using cache maintenance. CPU writes go directly to RAM, DMA controller sees them.

This applies to ALL future driver development on seL4 that uses DMA.

---

## BCM2711 System Timer + Throughput

- System Timer at 0xFE003000 mapped at vaddr 0x5c2000 (1 MHz free-running counter)
- Binary buddy skip (7 retypes) advances device untyped from 0xFE004000→0xFE200000
- ADMA2 throughput measured: **11.9 MB/s** (131072 bytes in 10989 us)
- ADMA2 vs PIO sanity check: PASS (matching checksums)

---

*Week 36 COMPLETE - February 7, 2026*
*12 PASS, 0 FAIL - All objectives met (including throughput measurement)*
