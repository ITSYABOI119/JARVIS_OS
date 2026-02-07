# Week 35 Status: SD/EMMC Storage Driver - Part 1

**Status:** ✅ COMPLETE (January 16, 2026)
**Target Start:** January 13, 2026
**Actual Effort:** ~6 hours
**Goal:** Bring up BCM2711 EMMC2 (SDHCI), detect SD card, and read LBA0.

---

## Actual Results (Hardware Verified)

**All Week 35 Part 1 objectives met:**

| Objective | Status | Evidence |
|-----------|--------|----------|
| EMMC2 MMIO mapped | ✅ PASS | `MMIO mapped vaddr=005c2000` |
| Controller reset + clock init | ✅ PASS | `CONTROL1 clock=00003f07` (400kHz), then `00000207` (50MHz) |
| Card responds (CMD0/CMD8/ACMD41) | ✅ PASS | `Card init OK (SDHC/SDXC)` |
| Single-block read LBA0 | ✅ PASS | `Read LBA0 OK` checksum=0000070A |
| MBR signature valid | ✅ PASS | `MBR sig=55AA` |
| Partition table parsed | ✅ PASS | `PART0_START_LBA=8192` |
| FAT BPB parsed | ✅ PASS | `oem="mkfs.fat"`, `bytes_per_sector=512`, `BPB sig=55AA` |

**Bonus (Week 36 preview):**

| Objective | Status | Evidence |
|-----------|--------|----------|
| Multi-block read (CMD18) | ✅ PASS | `Multi-block: LBA=8192 count=8 checksum=c0c18158` |
| AUTO_CMD12 stop | ✅ PASS | No manual CMD12 needed, transfer completes cleanly |
| PIO throughput | ~0.5-2 MiB/s | Expected for polled I/O; DMA required for >10 MB/s |

**FAT32 Volume Details (Parsed from BPB):**
- OEM: `mkfs.fat`
- Bytes/sector: 512
- Sectors/cluster: 16
- Reserved sectors: 32
- FAT count: 2
- FAT size: 14808 sectors
- Total sectors: 30,354,658 (~14.4 GB)
- FAT begin LBA: 8224
- Data begin LBA: 37840

---

## Definition of Done

- EMMC2 MMIO mapped (device attributes) and base logged.
- Controller reset + clock init (400 kHz) completes.
- Card responds to CMD0/CMD8/ACMD41 and RCA is assigned.
- Single-block read of LBA 0 succeeds; first 64 bytes printed.

---

## Commands We Run

```bash
# Build + copy kernel image (WSL)
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/scripts
./build_and_copy_kernel.sh
```

```cmd
# Copy to SD card (Windows, U-Boot flow)
# config.txt loads u-boot.bin; boot.scr loads kernel8.img from the FAT partition
cd C:\Users\jluca\Documents\JARVIS_OS\phase2
copy firmware\kernel8.img D:\kernel8.img

# Optional full SD sync (keeps boot.scr + u-boot.bin in place)
copy_to_sd.bat D:
```

---

## Test Plan

1. Boot the Pi 4 with the updated kernel.
2. Watch the serial console log for EMMC lines.
3. Confirm `[EMMC] Card init OK` and `[EMMC] Read LBA0 OK`.
4. Validate the hex dump resembles an MBR (0x55 0xAA at offset 0x01FE).

---

## Expected Output (Success)

```
[EMMC] Starting SD/EMMC bring-up...
[EMMC] Reset controller...
[EMMC] Card init OK (SDHC/SDXC)
[EMMC] Read LBA0 OK
[EMMC] LBA0 checksum=........
  0000: <16 bytes>
  0010: <16 bytes>
[EMMC] MBR PARTITION TABLE (0x1BE..0x1FD)
  01BE: <16 bytes>
  01CE: <16 bytes>
  01DE: <16 bytes>
  01EE: <16 bytes>
[EMMC] MBR sig=55aa
[EMMC] PART0_START_LBA=8192
[EMMC] Read LBA1 OK
[EMMC] LBA1 checksum=........
[EMMC] LBA1 tail=....
[EMMC] Read PART0 LBA8192 OK
[EMMC] LBA8192 checksum=........
[EMMC] PART0 LBA8192 tail=....
[EMMC] BPB oem="........"
[EMMC] BPB bytes_per_sector=512
[EMMC] BPB sectors_per_cluster=..
[EMMC] BPB reserved_sectors=..
[EMMC] BPB num_fats=..
[EMMC] BPB root_entry_count=..
[EMMC] BPB total_sectors16=..
[EMMC] BPB total_sectors32=..
[EMMC] BPB fat_size16=..
[EMMC] BPB fat_size32=..
[EMMC] BPB root_cluster=..
[EMMC] BPB fsinfo_sector=..
[EMMC] BPB fs_label_fat32="........"
[EMMC] BPB fs_label_fat16="........"
[EMMC] BPB sig=55aa
[EMMC] FAT fat_begin_lba=....
[EMMC] FAT data_begin_lba=....
[EMMC] Multi-block read test (CMD18+AUTO_CMD12)...
[EMMC] Multi-block: LBA=8192 count=8 checksum=........
[EMMC] Multi-block: 4KB read OK (PIO throughput ~0.5-2 MiB/s est)
```

## Expected Output (Failure)

```
[EMMC] ERROR: MMIO not mapped
```

Other possible failures: `CMD8 failed`, `ACMD41 failed`, `READ_RDY timeout`, `DATA_DONE timeout`.

---

## Artifacts / Logs

- Serial console log: `phase2/logs/serial_console.log` (capture during boot)
  - Look for: `[EMMC] Starting SD/EMMC bring-up...`, `Card init OK`, `Read LBA0 OK`
  - On failure: `[EMMC] ERROR:` lines plus `cmd=... status=... irpt=... ctrl1=...`
- Kernel image: `phase2/firmware/kernel8.img`
- U-Boot script: `phase2/firmware/boot.cmd` -> `boot.scr` (loads `kernel8.img`)

Interpretation:
- If host LBA0 is non-zero but target prints all zeros with checksum=00000000, the read path is suspect.
- If host and target both show zeros, the card/sector is likely blank.

Sanity Check (Week 35 Part 2):
- `[EMMC] MBR PARTITION TABLE` should show non-zero bytes in at least one entry.
- `[EMMC] PART0_START_LBA` should match the host's partition start (often 8192).
- The PART0 LBA dump should look like a filesystem boot sector (not all zeros).
- BPB should show `bytes_per_sector=512` and `BPB sig=55aa`.

---

## Notes / Known Blockers

- Requires SD card present and EMMC2 MMIO mapping to succeed.
- If EMMC mapping is skipped due to empty slot shortage, UART continues to work but EMMC test will report MMIO not mapped.
- EMMC test is OFF by default. To enable, set `EMMC_TEST` to 1 in `phase2/src/sel4/main_arm64.c`.
- EMMC MMIO vaddr is allocated from the device mapping region (first free 4K page after UART/GPIO).

---

## Week 36 Planning (SD/EMMC Part 2)

**Remaining for Week 36 (per PHASE_2_IMPLEMENTATION_PLAN.md):**

| Objective | Status | Notes |
|-----------|--------|-------|
| Block write (CMD24) | ⏳ Planned | Single-block write |
| Multi-block write (CMD25) | ⏳ Planned | Requires careful testing |
| Throughput >10 MB/s | ⏳ Requires DMA | PIO maxes out at ~2 MiB/s |
| 10/10 driver tests | ⏳ Planned | Need test suite |

**Why PIO < 10 MB/s:**
- PIO (Programmed I/O) reads 4 bytes at a time via CPU
- Each read requires polling STATUS register
- At 115200 baud UART, debug output adds latency
- **DMA (ADMA2) is required to reach >10 MB/s**

**DMA Implementation (Optional for Week 36):**
- SDHCI ADMA2 descriptor tables
- Scatter-gather DMA support
- Interrupt-driven completion
- Estimated: +8-12 hours

**Recommendation:**
Mark Week 35 Part 1 COMPLETE. PIO multi-block read is sufficient for filesystem bring-up. DMA optimization can be deferred to Week 36 or later if >10 MB/s is truly required.

---

*Week 35 Part 1 COMPLETE - January 16, 2026*
*Week 35 kickoff updated January 13, 2026*
