# ADR: --target disk — full single-OS on-SSD JARVIS install

Status: Proposed — implement code + dry-run tests ONLY; NEVER executed on the JARVIS dev PC
(Ubuntu is the build/ssh environment and must be preserved). Date: 2026-06-25.

## Context
The installer supports `--target {usb|esp|disk}`. `usb` = a removable boot stick; `esp` = reversible
dual-boot added to the existing internal ESP (DONE, verified on-box boot_id=5, Ubuntu kept default);
`disk` was reserved. `--target disk` dedicates the WHOLE internal NVMe to JARVIS — a single OS, no
Ubuntu.

HARD CONSTRAINT (user, 2026-06-25): this is NEVER run on the JARVIS dev PC, whose Ubuntu is the
build/ssh environment and must be preserved. `disk` ships as a dry-run-proven capability only, for
completeness of the target matrix and for a future dedicated box.

Two physical realities shape the design:
1. You cannot wipe the disk you are booted from → a real `disk` install runs from an installer/live
   USB that targets the internal NVMe (a different disk from the live environment).
2. A fresh dedicated disk has no Ubuntu shim to coexist with → `disk` uses the simple `--removable`
   boot (`EFI/BOOT/BOOTX64.EFI`), exactly like the USB target — not the esp target's additive
   `EFI/jarvis/` + efibootmgr machinery (which exists only to coexist with Ubuntu).

## Decision
Full clean wipe → a new GPT on the target NVMe:
- **JARVIS_DATA** (FAT32) starting at EXACTLY sector 32768 — so `main_x86.c`'s NVMe loader
  (`NVME_FAT32_PART_LBA`) finds it — ~10 GiB, holds `GEMMA2B.GUF`. (10 GiB = one model + headroom;
  FAT32's 4 GiB/file cap means a bigger partition cannot hold a larger single model anyway. The
  size is a named constant `DISK_DATA_SIZE_MIB`.)
- **ESP** (FAT32, esp/boot flags) ~512 MiB — holds the standalone GRUB via `--removable`
  (`EFI/BOOT/BOOTX64.EFI`) plus `boot/{kernel, rootserver, grub.cfg}`.
- The **telemetry-log tail** (2701 sectors at the disk end, LBA 4000794624 on the 2 TB box) is left
  UNPARTITIONED; the remaining space between the ESP and that tail is left unallocated (cleanly
  expandable).

Execution: from a live USB, targets a WHOLE DISK (e.g. `/dev/nvme0n1`). Guards refuse the
booted/root disk, any disk with a mounted partition, and any partition path (the inverse of the esp
guard, which requires a partition). Three confirms on a real run: re-type the device, then
`WIPE-AND-INSTALL-JARVIS`, then `DESTROY-ALL-DATA-ON-<dev>`. Model source: the installer media /
`--model` (there is no repo checkout after a wipe).

Reuse: `parted` + `mkfs.fat` (like `write_boot_usb`) + `stage_boot_payload "...","disk",...`
(`grub-install --removable`, already exists) + `provision_model` (`setup_nvme_partition.sh`, whose
detect-existing-JARVIS_DATA fast path copies the model into the partition this target just created) —
retargeted to the internal NVMe with stronger guards. Less net-new code than the esp target.

## Consequences
The target matrix is complete; `disk` is dry-run-proven and never run here. There is no real-hardware
test without a sacrificial box — coverage is the dry-run test plus the partition/stage/provision
sub-logic shared with the USB target.

KNOWN LIMITATION: `nvme_log.h` hardcodes the telemetry-log LBA (4000794624) for the 2 TB Lexar
NM790. A `disk` install on a different-size drive needs that LBA recomputed (and the unpartitioned
tail re-placed) — out of scope here (assume a ~2 TB target). The code carries an in-line note.

## Out of scope
The bootable installer USB/ISO that carries the images + model + installer and boots a live
environment (a separate productization). This ADR is the disk-target install LOGIC, runnable from
any live Linux, and is dry-run-tested only.
