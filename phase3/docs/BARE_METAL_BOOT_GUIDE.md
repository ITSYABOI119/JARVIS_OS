# JARVIS AI-OS — Bare Metal Boot Guide (seL4 x86-64)

**Date:** April 2, 2026
**Platform:** x86-64 PC99 (Ryzen 7 2700X, ASUS X470-F Gaming, 32GB DDR4, R9 280X display)
**Status:** UEFI boot VERIFIED bare-metal (Phase 4 goal #2 Step 1, 2026-06-18) — the full stack survives UEFI (self-test 5/5, NVMe Gemma load, M3 NUM_NODES=6, coherent inference, err=0, ~5.5 tok/s). Framebuffer status UI = goal #2 Step 2.

---

## Overview

This guide covers booting seL4 x86-64 with the JARVIS rootserver on real hardware (the JARVIS Project PC). The boot chain is:

```
Power On -> BIOS/UEFI -> GRUB2 (USB) -> seL4 kernel (multiboot) -> JARVIS rootserver
```

seL4's x86-64 kernel is multiboot-compliant. GRUB loads the kernel and rootserver image as a multiboot module, then seL4 ELF-loads the rootserver into userspace.

---

## 1. Prerequisites

### Hardware

| Item | Details |
|------|---------|
| JARVIS Project PC | Ryzen 7 2700X, ASUS X470-F Gaming, 32GB DDR4, R9 280X (display only) |
| USB stick | 2GB+ (FAT32, will be erased) |
| Monitor | Connected to GPU or motherboard video output |
| Keyboard | USB keyboard for BIOS setup |
| USB-to-serial adapter | Optional, for COM1 serial console (see Section 9) |

### Software (on JARVIS PC running Ubuntu)

The JARVIS PC should have Ubuntu installed with the seL4 build toolchain. See `phase3/docs/SEL4_X86_QEMU_SETUP.md` for initial toolchain setup.

```bash
# GRUB packages (for creating boot USB)
sudo apt install grub-pc-bin grub-efi-amd64-bin grub-common

# Partitioning tools
sudo apt install parted dosfstools

# Serial terminal (optional, for COM1 console)
sudo apt install minicom screen
```

### seL4 Build Tree

The seL4 build tree must be set up and the JARVIS rootserver must build successfully before creating a boot USB:

```bash
# Verify build tree exists
ls ~/sel4-x86/jbuild/images/
#   kernel-x86_64-pc99              -- seL4 kernel
#   sel4test-driver-image-x86_64-pc99   -- JARVIS rootserver (+ CPIO)
```

---

## 2. BIOS Settings (ASUS X470-F Gaming)

Enter BIOS by pressing **DEL** during POST (the ROG logo screen).

### Required Settings

Navigate to these settings and change them as indicated:

| Path | Setting | Value | Why |
|------|---------|-------|-----|
| Advanced > CPU Configuration | SVM Mode | **Enabled** | Hardware virtualization (KVM for QEMU testing) |
| Boot > CSM (Compatibility Support Module) | Launch CSM | **Disabled** | Pure UEFI boot — the v1.0 standard (CSM is sunset; installer/release need UEFI; GOP is the goal-#2 framebuffer path) |
| Boot > Secure Boot | OS Type | **Other OS** | Non-enforcing — the GRUB/shim isn't enrolled in Microsoft keys; "Windows UEFI mode" would block the boot |
| Boot > Boot Option Priorities | #1 Boot Option | **UEFI: \<USB stick\>** | Boot the UEFI USB entry (not a plain/legacy entry) before NVMe |
| Save & Exit | | **Save Changes and Reset** | Apply settings |

### Notes

- If "Secure Boot" does not show an OS Type option, look for a standalone "Secure Boot" enable/disable toggle under the Boot or Security tab.
- Boot in **UEFI mode** (CSM disabled): the grub default entry (`default=1`) is the multiboot2/efi_gop UEFI entry. At the boot picker choose the **`UEFI: <stick>`** entry. The **legacy BIOS path is the fallback** — flash with `reflash_usb.sh --bios` / `create_boot_usb.sh --bios-only`, then re-enable CSM.
- SVM Mode is not required for bare-metal seL4, but it is useful for QEMU/KVM testing on the same machine.

---

## 3. Building JARVIS seL4

Build the JARVIS rootserver using the sync-and-build script:

```bash
# First time: clone seL4 and set up build tree
# (see phase3/docs/SEL4_X86_QEMU_SETUP.md for initial setup)

# Sync JARVIS sources from repo into seL4 build tree, then build
cd ~/Desktop/JARVIS_OS
./phase3/scripts/build_jarvis_x86.sh
```

The script copies all JARVIS source files (rootserver, AI modules, IPC, drivers) into the seL4 project tree and runs `ninja` to rebuild. Use `--dry-run` to see what would be copied without actually copying:

```bash
./phase3/scripts/build_jarvis_x86.sh --dry-run
```

If your JARVIS repo is in a different location, pass it as an argument:

```bash
./phase3/scripts/build_jarvis_x86.sh /path/to/JARVIS_OS
```

After a successful build, verify the output images exist:

```bash
ls -lh ~/sel4-x86/jbuild/images/
#   kernel-x86_64-pc99              -- seL4 kernel (~9 MB)
#   sel4test-driver-image-x86_64-pc99   -- JARVIS rootserver (~4 MB)
```

---

## 4. Creating Boot USB

### Automated (Recommended)

```bash
# Identify your USB device
lsblk -d -o NAME,SIZE,TYPE,TRAN,MODEL | grep disk

# Create boot USB — DEFAULT IS UEFI (GPT+ESP, x86_64-efi). Replace /dev/sdX.
sudo ./phase3/scripts/create_boot_usb.sh /dev/sdX --confirm

# Legacy BIOS/CSM fallback:
sudo ./phase3/scripts/create_boot_usb.sh /dev/sdX --confirm --bios-only

# Quick reflash of an existing build (defaults to UEFI; --bios for legacy):
sudo bash ./phase3/scripts/reflash_usb.sh /dev/sdX
```

The script partitions the USB, copies the seL4 images + GRUB config, and installs GRUB (UEFI by default → `/EFI/BOOT/BOOTX64.EFI`). It refuses NVMe and the root-filesystem disk as a safety measure.

### Manual

If the script does not work in your environment:

```bash
# 1. Partition USB (MBR + FAT32)
sudo parted /dev/sdX mklabel msdos
sudo parted /dev/sdX mkpart primary fat32 1MiB 100%
sudo parted /dev/sdX set 1 boot on
sudo mkfs.fat -F 32 -n JARVIS_BOOT /dev/sdX1

# 2. Mount and copy files
sudo mkdir -p /mnt/usb
sudo mount /dev/sdX1 /mnt/usb
sudo mkdir -p /mnt/usb/boot/grub

sudo cp ~/sel4-x86/jbuild/images/kernel-x86_64-pc99 /mnt/usb/boot/
sudo cp ~/sel4-x86/jbuild/images/sel4test-driver-image-x86_64-pc99 /mnt/usb/boot/
sudo cp ~/Desktop/JARVIS_OS/phase3/firmware/grub/grub.cfg /mnt/usb/boot/grub/

# 3. Install GRUB (BIOS)
sudo grub-install --target=i386-pc --boot-directory=/mnt/usb/boot \
    --recheck /dev/sdX

# 4. Unmount
sudo umount /mnt/usb
```

### USB Layout After Creation

```
/boot/
  kernel-x86_64-pc99              -- seL4 microkernel
  sel4test-driver-image-x86_64-pc99   -- JARVIS rootserver
  grub/
    grub.cfg                      -- GRUB menu configuration
    i386-pc/                      -- GRUB BIOS modules
```

---

## 5. NVMe Model Provisioning

Before first boot, the deployed model must be present on the NVMe drive (`/dev/nvme0n1`) in a FAT32 partition the rootserver can find. The rootserver (`phase3/src/sel4/main_x86.c`) **hardcodes** both the partition location and the model filename:

- **Partition start LBA:** `NVME_FAT32_PART_LBA = 32768`. The JARVIS_DATA FAT32 partition **MUST** start at sector **32768**. If it starts anywhere else, the rootserver silently finds no model and skips inference — there is no error message.
- **Model file:** `JARVIS_MODEL_FILE = "GEMMA2B GUF"` — the FAT 8.3 short name for `GEMMA2B.GUF`. The deployed model is **Gemma 4 E2B Q4_K_M GGUF (~2.89 GiB)**.

The installer (`phase3/scripts/install_jarvis_x86.sh`) and `phase3/scripts/setup_nvme_partition.sh` create the partition with the correct 32768-sector start and place the model file there, so normally you do not partition by hand.

Verify the partition starts at the expected LBA:

```bash
sudo parted /dev/nvme0n1 unit s print
#   the JARVIS_DATA partition's Start must read 32768s
```

If the Start is not `32768s`, re-run `phase3/scripts/setup_nvme_partition.sh` (or the installer) — do not attempt to "fix" the LBA in `main_x86.c`.

---

## 6. Booting (UEFI)

1. Insert the boot USB into the JARVIS Project PC
2. Power on (or reboot); press **F8** during POST (X470-F) for the one-time boot menu and select the **`UEFI: <stick>`** entry
3. The GRUB menu appears on the monitor (~10s) and auto-selects **"JARVIS seL4 x86-64 (UEFI)"** (the multiboot2 entry, grub `default=1`)
4. seL4 boots via multiboot2 → loads the rootserver → self-test → loads Gemma from NVMe → starts inference
5. **The monitor shows the JARVIS GOP framebuffer HUD at 1024×768×32** once seL4 takes over (~2s) — status panel, STATE badge, the live model-load progress bar, and the scrolling event log (goal #2; box-day confirmed 2026-06-24). Legacy VGA text (`0xB8000`) is unavailable under UEFI — that map fails gracefully (non-fatal); the on-screen UI is the GOP framebuffer, not VGA text. For headless/scripted validation use the **network telemetry console** (Section 9) or the NVMe telemetry log (Section 7). (1080p is not available bare-metal — the firmware GOP only offers 1024×768.)

---

## 7. Expected Output (serial / NVMe log — headless validation)

On-screen the monitor shows the **GOP framebuffer HUD at 1024×768×32** (status panel + STATE badge + live model-load progress bar + scrolling event log; goal #2, box-day confirmed). Legacy VGA text (`0xB8000`) is unavailable under UEFI and maps non-fatally. For headless/scripted validation, the rootserver output below is what appears on the **COM1 serial console** and, with `JARVIS_DBG_BOOT_LOG=1`, in the **NVMe telemetry log** (read back via `sudo dd if=/dev/nvme0n1 bs=512 skip=4000794624 count=2700 | python3 phase3/scripts/parse_nvme_log.py`):

```
==================================================
  JARVIS AI-OS v0.2.1-dev | seL4 x86-64 | PC99
==================================================

[JARVIS] Init decision cache...
[JARVIS] Loaded 308 patterns

=== JARVIS Self-Test Mode ===

[1/5] Decision Cache ... PASS
[2/5] SHIELD Safety ... PASS
[3/5] Tensor Operations ... 5/5 PASS
[4/5] Dequantization ... 3/3 PASS
[5/5] Tokenizer + Sampling ... 4/4 PASS

=== Self-Test: 5/5 PASS ===

[JARVIS] Bootstrapping system...
[JARVIS] System bootstrapped
[JARVIS] Spawning inference process...
[JARVIS] Process B started
[JARVIS] Waiting for Process B ready signal...
[JARVIS] Sending test query to Process B...
[JARVIS] System idle.
```

### Validation channels

Two independent off-box validation channels confirm the boot succeeded without needing on-screen output:

- **NVMe telemetry log (durable):** read back the durable boot log with
  `sudo dd if=/dev/nvme0n1 bs=512 skip=4000794624 count=2700 | python3 phase3/scripts/parse_nvme_log.py`.
- **Network telemetry (live, UDP):** run `python3 phase3/scripts/telemetry_receiver.py` on another machine to receive the box's UDP telemetry on port 51000 over the Intel I211 NIC. Pass `--sse` (`python3 phase3/scripts/telemetry_receiver.py --sse`) to also serve the browser console at `phase4/console/`.

Key differences on real hardware compared to QEMU:
- More untypeds reported (32GB RAM vs 4-8GB QEMU)
- Real ACPI tables (may show multiple CPUs / threads for Ryzen 7)
- Real IOAPIC/LAPIC addresses
- PCI devices visible (NVMe, R9 280X, NIC, USB controllers)
- HPET timer available

---

## 8. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| No output at all | Secure Boot blocking GRUB | Disable Secure Boot in BIOS (Section 2) |
| No output at all | USB not booting | Press F8 for one-time boot menu; check boot order |
| GRUB menu appears, but no kernel | Module line in grub.cfg wrong | Check `ls ~/sel4-x86/jbuild/images/` and update grub.cfg image names |
| Triple fault after GRUB | IOAPIC/LAPIC issue | Try the "Serial Only" GRUB entry; disable IOMMU in BIOS |
| Hang after seL4 kernel banner | Timer source issue | Check PIT/HPET/TSC config; try rebuilding without `-DSIMULATION=1` |
| Monitor dark after GRUB | No GOP framebuffer mapped (or a pre-goal-#2-Step-2 build) | The GOP HUD renders at 1024×768 since goal #2 Step 2 (box-day confirmed). If still dark: check the monitor is on the active video output and the firmware GOP is active (not legacy/CSM); validate the box is alive via the network telemetry console / NVMe log. |
| Works on QEMU, fails on real HW | ACPI or memory map differences | Debug with serial console (Section 9); check IOMMU/VT-d setting |
| "No valid untypeds" | Memory map / IOMMU | UEFI memory map validated in Step 1; if it recurs, confirm IOMMU/AMD-Vi is Disabled. |
| Kernel assertion failure | IOMMU/AMD-Vi enabled | Disable AMD-Vi in BIOS for first boot |
| GRUB rescue prompt | grub.cfg not found on USB | Verify grub.cfg is at `/boot/grub/grub.cfg` on the USB partition |
| "file not found" in GRUB | Image name mismatch | Image filenames in grub.cfg must match files in `/boot/` |

---

## 9. Serial Console (Optional)

The seL4 kernel outputs to COM1 (I/O port 0x3F8) at 115200 baud. Serial provides kernel-level output that the VGA driver cannot display (the VGA driver only shows rootserver output).

### Connecting

The ASUS X470-F Gaming has a COM1 header on the motherboard (10-pin). Connect a motherboard COM header cable to a DB9-to-USB adapter, then plug the USB end into another machine (or the JARVIS PC itself, pre-boot).

### Terminal Setup

```bash
# Linux (on another machine, or the JARVIS PC before reboot)
screen /dev/ttyUSB0 115200
# Exit: Ctrl-A, K, Y

# Or minicom
minicom -b 115200 -D /dev/ttyUSB0
```

On Windows (PuTTY): Connection type Serial, COM port matching the adapter, 115200 baud, 8N1, no flow control.

---

## 10. Note About VGA vs Serial Output

seL4 x86-64 has two independent output paths:

- **Serial (COM1 at 0x3F8):** The seL4 kernel writes boot messages, ELF loading status, and assertion failures here. The rootserver also writes here via `puts_serial()` / `seL4_DebugPutChar()`.

- **VGA text (0xB8000):** The JARVIS VGA driver (`vga_text.c`) writes to VGA text-mode memory, available **only under legacy BIOS/CSM**. Under UEFI there is no VGA text window — the `0xB8000` map fails gracefully (non-fatal, `vga_ready=0`); the on-screen UI is instead the **GOP framebuffer HUD** (goal #2 Step 2, 1024×768×32, box-day confirmed). Use the network telemetry console / serial / NVMe log for headless validation.

Both outputs run simultaneously when available. For initial bring-up, VGA alone is sufficient. Add serial later for kernel-level debugging if needed.

---

## 11. Updating After Code Changes

The development workflow is: edit source, sync, build, copy to USB, reboot.

```bash
# 1. Edit JARVIS source files in the repo

# 2. Sync and rebuild
./phase3/scripts/build_jarvis_x86.sh

# 3. Update the USB
sudo mount /dev/sdX1 /mnt/usb
sudo cp ~/sel4-x86/jbuild/images/kernel-x86_64-pc99 /mnt/usb/boot/
sudo cp ~/sel4-x86/jbuild/images/sel4test-driver-image-x86_64-pc99 /mnt/usb/boot/
sudo umount /mnt/usb

# 4. Reboot JARVIS PC from USB
```

Later (after NVMe boot is implemented), the USB step goes away and iteration becomes faster.

---

## 12. Reference

| Resource | Location |
|----------|----------|
| Build script | `phase3/scripts/build_jarvis_x86.sh` |
| USB creation script | `phase3/scripts/create_boot_usb.sh` |
| GRUB config | `phase3/firmware/grub/grub.cfg` |
| x86 rootserver source | `phase3/src/sel4/main_x86.c` |
| VGA text driver | `phase3/src/drivers/vga_text.c` |
| seL4 QEMU setup | `phase3/docs/SEL4_X86_QEMU_SETUP.md` |
| Rootserver notes | `phase3/docs/SEL4_X86_ROOTSERVER_NOTES.md` |
| Implementation plan | `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md` |
| seL4 PC99 docs | https://docs.sel4.systems/Hardware/IA32.html |

---

*Guide updated: June 18, 2026 (Phase 4 goal #2 Step 1 — UEFI boot migration)*
*Hardware: Ryzen 7 2700X, ASUS X470-F Gaming, 32GB DDR4, R9 280X (display only)*
*Boot protocol: **UEFI / Multiboot2** via GRUB2 (default) — verified bare metal (self-test 5/5, NVMe Gemma load, M3 NUM_NODES=6, err=0, ~5.5 tok/s). Legacy BIOS/Multiboot1 kept as the `--bios` fallback.*
