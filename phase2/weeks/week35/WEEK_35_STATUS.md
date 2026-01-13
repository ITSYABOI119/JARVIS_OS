# Week 35 Status: SD/EMMC Storage Driver - Part 1

**Status:** PENDING
**Target Start:** January 13, 2026
**Estimated Effort:** 12-16 hours
**Goal:** Bring up BCM2711 EMMC2 controller, detect SD card, and read a single block.

---

## Definition of Done

- EMMC2 controller initialization completes (clock set, interrupts masked).
- SD card responds to CMD0/CMD8/ACMD41 and CID/CSD are read.
- Read one 512-byte block (LBA 0) via PIO with correct CRC/response.

---

## First Commands / Test Hooks

```bash
# Build + copy kernel image (WSL)
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/scripts
./build_and_copy_kernel.sh
```

```cmd
# Copy to SD card (Windows)
cd C:\Users\jluca\Documents\JARVIS_OS\phase2
copy_to_sd.bat D:
```

---

## Logs / Artifacts to Capture

- Serial console log: `phase2/logs/serial_console.log`
- Build output: `phase2/firmware/kernel8.img`

---

## Notes

- Use EMMC2 base address 0xFE340000 (BCM2711).
- Start with PIO mode and minimal command set: CMD0, CMD8, ACMD41, CMD2, CMD3, CMD7.
- Document register values and responses in the Week 35 results file once tests run.

---

*Week 35 kickoff created January 13, 2026*
