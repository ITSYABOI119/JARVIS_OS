# JARVIS AI-OS — Bare Metal Boot Guide (seL4 x86-64)

**Date:** April 2, 2026
**Platform:** x86-64 PC99 (Ryzen 7 2700X, ASUS X470-F Gaming, 16GB DDR4)
**Status:** Infrastructure ready, awaiting RTX 3060

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
| JARVIS Project PC | Ryzen 7 2700X, ASUS X470-F Gaming, 16GB DDR4, RTX 3060 |
| USB stick | 2GB+ (FAT32, will be erased) |
| Monitor | Connected to GPU or motherboard video output |
| Keyboard | USB keyboard for BIOS setup |
| USB-to-serial adapter | Optional, for COM1 serial console (see Section 8) |

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
#   jarvis-x86-image-x86_64-pc99   -- JARVIS rootserver (+ CPIO)
```

---

## 2. BIOS Settings (ASUS X470-F Gaming)

Enter BIOS by pressing **DEL** during POST (the ROG logo screen).

### Required Settings

Navigate to these settings and change them as indicated:

| Path | Setting | Value | Why |
|------|---------|-------|-----|
| Advanced > CPU Configuration | SVM Mode | **Enabled** | Hardware virtualization (KVM for QEMU testing) |
| Boot > Secure Boot | OS Type | **Other OS** | seL4 kernel is not signed; disables Secure Boot |
| Boot > CSM (Compatibility Support Module) | Launch CSM | **Enabled** | Legacy/MBR GRUB boot (multiboot1 works best here) |
| Boot > Boot Option Priorities | #1 Boot Option | **USB device** | Boot from USB stick before NVMe |
| Save & Exit | | **Save Changes and Reset** | Apply settings |

### Notes

- If "Secure Boot" does not show an OS Type option, look for a standalone "Secure Boot" enable/disable toggle under the Boot or Security tab.
- CSM must be enabled for the Legacy GRUB boot path. If you want to try UEFI boot instead, disable CSM and use the UEFI GRUB menu entry.
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
#   jarvis-x86-image-x86_64-pc99   -- JARVIS rootserver (~4 MB)
```

---

## 4. Creating Boot USB

### Automated (Recommended)

```bash
# Identify your USB device
lsblk -d -o NAME,SIZE,TYPE,TRAN,MODEL | grep disk

# Create boot USB (replace /dev/sdX with your USB device)
sudo ./phase3/scripts/create_boot_usb.sh /dev/sdX --confirm

# For BIOS-only (simpler, more reliable for first boot):
sudo ./phase3/scripts/create_boot_usb.sh /dev/sdX --confirm --bios-only
```

The script partitions the USB, copies seL4 images and GRUB config, and installs GRUB. It refuses `/dev/sda` and NVMe devices as a safety measure.

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
sudo cp ~/sel4-x86/jbuild/images/jarvis-x86-image-x86_64-pc99 /mnt/usb/boot/
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
  jarvis-x86-image-x86_64-pc99   -- JARVIS rootserver
  grub/
    grub.cfg                      -- GRUB menu configuration
    i386-pc/                      -- GRUB BIOS modules
```

---

## 5. Booting

1. Insert the boot USB into the JARVIS Project PC
2. Power on (or reboot)
3. If BIOS boot order was set correctly (Section 2), GRUB loads automatically
4. Select **"JARVIS seL4 x86-64"** from the GRUB menu
5. seL4 boots, loads the rootserver, and JARVIS self-tests run

If GRUB does not appear, press **F8** during POST (on X470-F Gaming) to open the one-time boot menu and select the USB device.

---

## 6. Expected VGA Output

With CSM enabled and VGA text mode active, the monitor displays the rootserver output via the VGA text driver:

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

Key differences on real hardware compared to QEMU:
- More untypeds reported (16GB RAM vs 4-8GB QEMU)
- Real ACPI tables (may show multiple CPUs / threads for Ryzen 7)
- Real IOAPIC/LAPIC addresses
- PCI devices visible (NVMe, RTX 3060, NIC, USB controllers)
- HPET timer available

---

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| No output at all | Secure Boot blocking GRUB | Disable Secure Boot in BIOS (Section 2) |
| No output at all | USB not booting | Press F8 for one-time boot menu; check boot order |
| GRUB menu appears, but no kernel | Module line in grub.cfg wrong | Check `ls ~/sel4-x86/jbuild/images/` and update grub.cfg image names |
| Triple fault after GRUB | IOAPIC/LAPIC issue | Try the "Serial Only" GRUB entry; disable IOMMU in BIOS |
| Hang after seL4 kernel banner | Timer source issue | Check PIT/HPET/TSC config; try rebuilding without `-DSIMULATION=1` |
| Garbled VGA text | Not in text mode | BIOS CSM must be enabled (UEFI GOP framebuffer is not VGA text) |
| Works on QEMU, fails on real HW | ACPI or memory map differences | Debug with serial console (Section 8); check IOMMU/VT-d setting |
| "No valid untypeds" | BIOS memory map issue | Enable CSM; UEFI firmware may report memory differently |
| Kernel assertion failure | IOMMU/AMD-Vi enabled | Disable AMD-Vi in BIOS for first boot |
| GRUB rescue prompt | grub.cfg not found on USB | Verify grub.cfg is at `/boot/grub/grub.cfg` on the USB partition |
| "file not found" in GRUB | Image name mismatch | Image filenames in grub.cfg must match files in `/boot/` |

---

## 8. Serial Console (Optional)

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

## 9. Note About VGA vs Serial Output

seL4 x86-64 has two independent output paths:

- **Serial (COM1 at 0x3F8):** The seL4 kernel writes boot messages, ELF loading status, and assertion failures here. The rootserver also writes here via `puts_serial()` / `seL4_DebugPutChar()`.

- **VGA text (0xB8000):** The JARVIS VGA driver (`vga_text.c`) writes directly to VGA text-mode memory. This provides rootserver output on the monitor independently of serial. CSM must be enabled for VGA text mode to work.

Both outputs run simultaneously when available. For initial bring-up, VGA alone is sufficient. Add serial later for kernel-level debugging if needed.

---

## 10. Updating After Code Changes

The development workflow is: edit source, sync, build, copy to USB, reboot.

```bash
# 1. Edit JARVIS source files in the repo

# 2. Sync and rebuild
./phase3/scripts/build_jarvis_x86.sh

# 3. Update the USB
sudo mount /dev/sdX1 /mnt/usb
sudo cp ~/sel4-x86/jbuild/images/kernel-x86_64-pc99 /mnt/usb/boot/
sudo cp ~/sel4-x86/jbuild/images/jarvis-x86-image-x86_64-pc99 /mnt/usb/boot/
sudo umount /mnt/usb

# 4. Reboot JARVIS PC from USB
```

Later (after NVMe boot is implemented), the USB step goes away and iteration becomes faster.

---

## 11. Reference

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

*Guide updated: April 2, 2026*
*Hardware: Ryzen 7 2700X, ASUS X470-F Gaming, 16GB DDR4, RTX 3060 (pending)*
*seL4 QEMU verified: 247/247 tests pass, process-isolated LLM inference on seL4*
*Boot protocol: Multiboot1 (BIOS/CSM) + Multiboot2 (UEFI) via GRUB2*
