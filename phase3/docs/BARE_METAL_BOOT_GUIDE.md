# JARVIS AI-OS — Bare Metal Boot Guide (seL4 x86-64)

**Date:** March 22, 2026
**Platform:** x86-64 PC99 (Ryzen 5 5600, B550M, 16GB DDR4)
**Status:** Infrastructure ready, awaiting spare PC assembly

---

## Overview

This guide covers booting seL4 x86-64 with the JARVIS rootserver on real hardware (the spare PC). The boot chain is:

```
Power On → BIOS/UEFI → GRUB2 (USB) → seL4 kernel (multiboot) → JARVIS rootserver
```

seL4's x86-64 kernel is multiboot-compliant. GRUB loads the kernel and rootserver image as a multiboot module, then seL4 ELF-loads the rootserver into userspace.

---

## 1. Prerequisites

### Hardware

| Item | Details |
|------|---------|
| Spare PC | Ryzen 5 5600, B550M-K, 16GB DDR4, NV3000 500GB |
| USB stick | 2GB+ (FAT32, will be erased) |
| USB-to-serial adapter | For console output (COM1 header or USB debug) |
| Serial cable | Connects adapter to PC COM1 header pins |
| Monitor + keyboard | For BIOS setup (not needed after boot) |

### Software (on WSL2 host)

```bash
# GRUB packages (for creating boot USB)
sudo apt install grub-pc-bin grub-efi-amd64-bin grub-common

# Partitioning tools
sudo apt install parted dosfstools

# Serial terminal
sudo apt install minicom
# Or use PuTTY on Windows
```

### seL4 Build (must be complete)

```bash
# Build JARVIS rootserver (in WSL)
cd ~/sel4-x86/jbuild
ninja

# Verify output images exist
ls -la images/
#   kernel-x86_64-pc99              — seL4 kernel (~9 MB)
#   jarvis-x86-image-x86_64-pc99   — JARVIS rootserver (~4 MB)
```

If images have different names (e.g., `sel4test-driver-image-x86_64-pc99` from the test build), update `phase3/firmware/grub/grub.cfg` to match.

---

## 2. BIOS/UEFI Configuration

Enter BIOS on the B550M-K by pressing **DEL** or **F2** during POST.

### Required Settings

| Setting | Value | Why |
|---------|-------|-----|
| Secure Boot | **Disabled** | seL4 kernel is not signed |
| CSM/Legacy Boot | **Enabled** (recommended) | multiboot1 works best with BIOS boot |
| USB Boot | **Enabled** | Boot from USB stick |
| Boot Order | **USB first** | Boot USB before NVMe |
| Serial Port | **Enabled** (if available) | COM1 at 0x3F8 for console output |
| IOMMU | **Disabled** (initially) | Can cause issues; enable later if needed |

### UEFI vs Legacy Boot

seL4 supports both boot modes via GRUB:

- **Legacy/CSM (recommended for first boot):** Uses `multiboot` command. Most reliable, well-tested path for seL4 on x86.
- **UEFI:** Uses `multiboot2` command. Works but has known edge cases with some firmware. Try this after Legacy boot succeeds.

The GRUB config provides menu entries for both modes. The USB script installs both BIOS and UEFI GRUB by default.

---

## 3. Create Boot USB

### Option A: Automated Script (Recommended)

```bash
# In WSL, identify your USB device
lsblk -d -o NAME,SIZE,TYPE,TRAN,MODEL | grep -E "disk"

# Create boot USB (replace /dev/sdX with your USB device)
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
sudo bash phase3/scripts/create_boot_usb.sh /dev/sdX --confirm

# For BIOS-only (simpler, more reliable for first boot):
sudo bash phase3/scripts/create_boot_usb.sh /dev/sdX --confirm --bios-only

# For UEFI-only:
sudo bash phase3/scripts/create_boot_usb.sh /dev/sdX --confirm --uefi-only
```

### Option B: Manual Setup

If the script doesn't work in your environment:

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

# Copy seL4 images
sudo cp ~/sel4-x86/jbuild/images/kernel-x86_64-pc99 /mnt/usb/boot/
sudo cp ~/sel4-x86/jbuild/images/jarvis-x86-image-x86_64-pc99 /mnt/usb/boot/

# Copy GRUB config
sudo cp /mnt/c/Users/jluca/Documents/JARVIS_OS/phase3/firmware/grub/grub.cfg \
    /mnt/usb/boot/grub/

# 3. Install GRUB (BIOS)
sudo grub-install --target=i386-pc --boot-directory=/mnt/usb/boot \
    --recheck /dev/sdX

# 4. Unmount
sudo umount /mnt/usb
```

### USB Layout After Creation

```
/boot/
  kernel-x86_64-pc99                — seL4 microkernel
  jarvis-x86-image-x86_64-pc99     — JARVIS rootserver
  grub/
    grub.cfg                        — GRUB menu configuration
    i386-pc/                        — GRUB BIOS modules
    x86_64-efi/                     — GRUB UEFI modules (if --uefi)
/EFI/BOOT/
  BOOTX64.EFI                      — UEFI bootloader (if --uefi)
```

---

## 4. Connect Serial Console

seL4 x86-64 outputs to COM1 (I/O port 0x3F8) at 115200 baud. You need a serial connection to see kernel and rootserver output.

### Option A: Onboard COM Header (B550M-K)

Many B550M boards have a COM1 header on the motherboard (9-pin or 10-pin). Connect a header-to-DB9 cable, then a USB-to-serial adapter to your host PC.

### Option B: PCIe/USB Serial Card

If no onboard header, add a PCIe serial card or use a USB-to-serial adapter connected to the spare PC itself (though this won't capture early boot output).

### Serial Terminal Setup

**Windows (PuTTY):**
- Connection type: Serial
- Serial line: COM3 (or whichever port your adapter uses)
- Speed: 115200
- Data bits: 8, Stop bits: 1, Parity: None, Flow control: None

**WSL (minicom):**
```bash
sudo minicom -b 115200 -D /dev/ttyUSB0
# Or: sudo minicom -b 115200 -D /dev/ttyS3
```

**WSL (screen):**
```bash
sudo screen /dev/ttyUSB0 115200
# Exit: Ctrl-A, K, Y
```

**Windows (built-in):**
```cmd
mode COM3 BAUD=115200 PARITY=n DATA=8 STOP=1
```

---

## 5. Boot the PC

1. **Insert USB** into the spare PC
2. **Power on** and enter BIOS (DEL or F2)
3. **Set USB as first boot device** in boot order
4. **Save and exit** BIOS
5. **Watch for GRUB menu** on the monitor (and serial console)

GRUB menu shows:
```
JARVIS seL4 x86-64
JARVIS seL4 x86-64 (UEFI)
JARVIS seL4 x86-64 (Serial Only)
```

6. **Select "JARVIS seL4 x86-64"** (for Legacy/CSM boot) or the UEFI entry
7. **Watch serial console** for seL4 boot output

---

## 6. Expected Output

Based on QEMU boot output (real hardware will be similar with different memory addresses):

```
Loading seL4 kernel...
Loading JARVIS rootserver...

ELF-loading userland images from boot modules:
  size=0x3de000 v_entry=0x403b10 ...
  ...

Booting all finished, dropped to user space

==================================================
  JARVIS AI-OS v0.3.0-dev | seL4 x86-64 | PC99
==================================================

Boot: <N> untypeds

[JARVIS] Init decision cache...
Loaded 258 total patterns into cache
[JARVIS] Loaded 308 patterns

--- Cache Queries ---
  [HIT]  check disk space -> show_disk_usage
  [MISS] unknown query test
  ...

--- SHIELD ---
  [SHIELD] delete all data -> BLOCKED
  [SHIELD] rm -rf / -> BLOCKED

--- Stats ---
  Queries: 8, Hits: 1, Rate: 12%

[JARVIS] Done. System idle.
```

**Key differences from QEMU on real hardware:**
- More untypeds (16GB RAM vs 3GB)
- Real ACPI tables (may show multiple CPUs)
- Real IOAPIC/LAPIC addresses
- HPET timer available
- PCI devices visible (NVMe, GPU, NIC, USB controllers)

---

## 7. Troubleshooting

### No GRUB menu appears

| Symptom | Cause | Fix |
|---------|-------|-----|
| Boots straight to NVMe/other OS | USB not first in boot order | Enter BIOS, move USB to top |
| Black screen, no GRUB | Secure Boot blocking GRUB | Disable Secure Boot in BIOS |
| Black screen, no GRUB | USB not bootable | Re-run create_boot_usb.sh; try --bios-only |
| GRUB rescue prompt | GRUB installed but can't find config | Check grub.cfg is at /boot/grub/grub.cfg on USB |

### GRUB shows "file not found"

The image file names in grub.cfg must match the actual files in `/boot/` on the USB.

```
# Check what the build actually produced:
ls ~/sel4-x86/jbuild/images/

# If different from expected, update grub.cfg:
# e.g., if the rootserver is named "sel4test-driver-image-x86_64-pc99"
# change the "module" line in grub.cfg accordingly
```

### GRUB says "no multiboot header found"

- The kernel image may be corrupted. Rebuild: `cd ~/sel4-x86/jbuild && ninja`
- Try switching between `multiboot` and `multiboot2` in grub.cfg
- Verify the file on USB matches the build output: `md5sum` both copies

### No serial output after GRUB

| Symptom | Cause | Fix |
|---------|-------|-----|
| GRUB works, then silence | Serial port not COM1 | Check BIOS: ensure COM1 at 0x3F8 is enabled |
| GRUB works, then silence | Wrong baud rate | Must be 115200. seL4 kernel hardcodes this. |
| GRUB works, then silence | Cable issue | Check TX/RX wiring; try swapping TX and RX |
| GRUB works, then freeze | Kernel panic (no serial) | Memory map issue. Try reducing RAM, disabling IOMMU. |

### seL4 kernel panic / assertion failure

```
seL4: ASSERTION FAILED: ...
```

Common causes on real hardware:
- **IOMMU/VT-d enabled:** Disable AMD-Vi / Intel VT-d in BIOS for first boot
- **Memory map conflict:** seL4 may not handle certain ACPI reserved regions. Check if kernel config needs adjustment.
- **UEFI multiboot2 parsing:** Known edge cases. Try Legacy/CSM boot instead.
- **Kernel built for simulation:** Rebuild without `-DSIMULATION=1`:
  ```bash
  cd ~/sel4-x86
  rm -rf jbuild && mkdir jbuild && cd jbuild
  cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake \
    -DPLATFORM=x86_64 \
    -DSEL4_CACHE_DIR=$HOME/.sel4_cache \
    -C ../projects/jarvis-x86/settings.cmake \
    ../projects/jarvis-x86
  ninja
  ```
  Note: `-DSIMULATION=1` may set kernel options that don't work on real hardware (e.g., simplified timer, no IOAPIC). Remove it for bare-metal builds.

### seL4 prints "No valid untypeds"

The kernel found no usable memory regions. This means the BIOS memory map (E820 or UEFI memory map) reported no available RAM in ranges seL4 expects.

Fix: Check BIOS memory settings. Ensure CSM is enabled. Some UEFI firmware reports memory differently than legacy BIOS.

### Rootserver doesn't start

If seL4 boots but the rootserver doesn't run:
- Check that the rootserver image was loaded as a boot module (GRUB `module` line)
- Verify the rootserver was built for x86-64: `file images/jarvis-x86-image-x86_64-pc99`
- Check the ELF entry point matches what seL4 expects

---

## 8. Updating the Kernel

After rebuilding the JARVIS rootserver, update the USB:

```bash
# Rebuild
cd ~/sel4-x86/jbuild
ninja

# Mount USB and copy new images
sudo mount /dev/sdX1 /mnt/usb  # or /dev/sdX2 if dual-mode GPT
sudo cp images/kernel-x86_64-pc99 /mnt/usb/boot/
sudo cp images/jarvis-x86-image-x86_64-pc99 /mnt/usb/boot/
sudo umount /mnt/usb

# Reboot spare PC
```

This is the development workflow: edit source, build, copy to USB, reboot. Later (Week 15+), boot from NVMe for faster iteration.

---

## 9. Boot Protocol Details

### Multiboot1 (Legacy BIOS)

seL4's `src/arch/x86/multiboot.S` contains a standard Multiboot1 header:
- Magic: `0x1BADB002`
- Flags: request memory map, page-aligned modules
- GRUB loads kernel at 0x100000, passes multiboot info struct in `%ebx`
- seL4 `boot_sys.c` parses E820 memory map from multiboot info

### Multiboot2 (UEFI)

seL4 also supports Multiboot2 (compile-time `CONFIG_MULTIBOOT2_HEADER`):
- Magic: `0xE85250D6`
- GRUB loads via `multiboot2` command
- UEFI memory map passed instead of E820
- Required for pure UEFI systems (no CSM)

### Boot Module

The rootserver image is passed as a multiboot module. seL4 kernel:
1. Finds module in multiboot info struct
2. ELF-loads it into a new address space
3. Provides all physical resources as untyped capabilities
4. Transfers control to rootserver entry point

This is identical to how QEMU boots with `-kernel` and `-initrd` — QEMU's firmware (SeaBIOS) implements multiboot internally.

---

## 10. Reference

| Resource | Location |
|----------|----------|
| GRUB config | `phase3/firmware/grub/grub.cfg` |
| USB creation script | `phase3/scripts/create_boot_usb.sh` |
| seL4 QEMU setup | `phase3/docs/SEL4_X86_QEMU_SETUP.md` |
| Rootserver notes | `phase3/docs/SEL4_X86_ROOTSERVER_NOTES.md` |
| Implementation plan | `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md` (Week 9-10) |
| x86 rootserver source | `phase3/src/sel4/main_x86.c` |
| Build config | `phase3/src/sel4/CMakeLists.txt` |
| Pi 4 boot reference | `phase2/firmware/` (different arch, similar concept) |
| seL4 PC99 docs | https://docs.sel4.systems/Hardware/IA32.html |
| seL4 multiboot source | https://github.com/seL4/seL4/blob/master/src/arch/x86/multiboot.S |

---

*Guide written: March 22, 2026*
*Hardware: Ryzen 5 5600, B550M-K, 16GB DDR4 (awaiting assembly)*
*seL4 QEMU verified: 123/123 tests pass, rootserver boots*
*Boot protocol: Multiboot1 (BIOS) + Multiboot2 (UEFI) via GRUB2*
