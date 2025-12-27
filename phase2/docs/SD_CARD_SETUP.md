# Raspberry Pi 4 SD Card Setup Guide

## Overview

This guide documents how to prepare an SD card for running seL4 + JARVIS on Raspberry Pi 4.

**Hardware:** Raspberry Pi 4 (BCM2711, Cortex-A72, 8GB RAM)
**Boot Method:** SD card with FAT32 boot partition
**OS:** seL4 microkernel (bare metal, no Linux)

---

## Requirements

### Hardware
- Raspberry Pi 4 (8GB recommended)
- microSD card (8GB+ minimum, 16GB recommended)
- USB SD card reader (for PC)
- USB-to-UART serial adapter (3.3V TTL)
- Jumper wires (for UART connection)

### Software
- SD card formatting tool (Windows: SD Card Formatter, Linux: fdisk/parted)
- File manager or terminal for copying files

### Files Needed
| File | Source | Size | Purpose |
|------|--------|------|---------|
| `start4.elf` | Raspberry Pi firmware | ~2.2 MB | GPU firmware bootloader |
| `fixup4.dat` | Raspberry Pi firmware | ~5.5 KB | Memory configuration |
| `kernel8.img` | seL4 build | ~138 KB | seL4 kernel + JARVIS |
| `config.txt` | Create manually | ~200 bytes | Boot configuration |

---

## Step 1: Download Raspberry Pi Firmware

The GPU firmware files are required to boot the Pi 4. Download them from:

**Official Repository:** https://github.com/raspberrypi/firmware/tree/master/boot

Required files:
- `start4.elf` - GPU bootloader for Pi 4
- `fixup4.dat` - Memory configuration for Pi 4

```bash
# Download via curl (Linux/WSL)
curl -L -o start4.elf https://github.com/raspberrypi/firmware/raw/master/boot/start4.elf
curl -L -o fixup4.dat https://github.com/raspberrypi/firmware/raw/master/boot/fixup4.dat

# Or clone the repo and copy:
git clone --depth 1 https://github.com/raspberrypi/firmware.git
cp firmware/boot/start4.elf firmware/boot/fixup4.dat .
```

**Note:** Files are already downloaded in `phase2/firmware/` if previously prepared.

---

## Step 2: Format SD Card

### Option A: Windows

1. Insert SD card via USB reader
2. Run **SD Card Formatter** (download from sdcard.org if needed)
3. Select your SD card drive
4. Choose **Quick Format**
5. Volume Label: `JARVIS`
6. Click **Format**

The card will be formatted as FAT32.

### Option B: Linux/WSL

```bash
# Identify the SD card (be VERY careful with device name!)
lsblk

# Assuming SD card is /dev/sdb (VERIFY THIS!)
# WARNING: Wrong device = data loss!

# Create partition table
sudo parted /dev/sdb mklabel msdos

# Create FAT32 boot partition (use all space)
sudo parted /dev/sdb mkpart primary fat32 1MiB 100%

# Format as FAT32
sudo mkfs.vfat -F 32 -n JARVIS /dev/sdb1

# Mount
sudo mkdir -p /mnt/sd
sudo mount /dev/sdb1 /mnt/sd
```

---

## Step 3: Create config.txt

Create `config.txt` with the following contents:

```ini
# Raspberry Pi 4 seL4 Boot Configuration
# JARVIS AI-OS Phase 2

# Enable 64-bit mode (required for seL4 ARM64)
arm_64bit=1

# Specify kernel filename
kernel=kernel8.img

# Enable UART for serial console
enable_uart=1

# Keep UART active during boot (debugging)
uart_2ndstage=1

# Disable Bluetooth (uses the good UART by default)
dtoverlay=disable-bt

# GPU memory (minimum - not using GPU)
gpu_mem=16

# Boot delay (optional, helps with debugging)
# boot_delay=1

# Disable rainbow splash screen
disable_splash=1
```

Save as `config.txt` in the root of the SD card.

---

## Step 4: Build seL4 Kernel with JARVIS

### Prerequisites (WSL/Ubuntu)

```bash
# Install dependencies
sudo apt update
sudo apt install -y cmake ninja-build gcc-aarch64-linux-gnu \
    python3 python3-pip python3-venv libxml2-utils \
    device-tree-compiler git curl

# Install seL4 Python tools
pip3 install sel4-deps
```

### Build Steps

```bash
# Navigate to seL4 tutorials (should already be set up)
cd ~/sel4-tutorials-manifest

# Create Pi 4 build directory
mkdir -p jarvis-pi4
cd jarvis-pi4

# Initialize for Raspberry Pi 4
../init --plat rpi4 --tut hello-world

# Copy JARVIS sources to override tutorial template
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/cache src/cache
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/sel4/* src/

# Build
ninja

# Output: images/kernel.elf (~138 KB)
```

### Copy to SD Card

```bash
# Rename kernel for Pi 4 boot
cp images/kernel.elf kernel8.img

# Copy to SD card
cp kernel8.img /mnt/sd/
# Or on Windows: copy kernel8.img to SD card root
```

---

## Step 5: Copy All Files to SD Card

Final SD card contents:

```
SD Card (FAT32, label: JARVIS)
├── start4.elf      # GPU firmware (2.2 MB)
├── fixup4.dat      # Memory config (5.5 KB)
├── kernel8.img     # seL4 + JARVIS kernel
└── config.txt      # Boot configuration
```

### Windows Copy Commands

```cmd
REM Assuming SD card is E:
copy start4.elf E:\
copy fixup4.dat E:\
copy kernel8.img E:\
copy config.txt E:\
```

### Linux Copy Commands

```bash
# Assuming mounted at /mnt/sd
sudo cp start4.elf fixup4.dat kernel8.img config.txt /mnt/sd/
sudo sync  # Ensure all writes complete
sudo umount /mnt/sd
```

---

## Step 6: Connect UART Serial Console

### Wiring

Connect USB-serial adapter to Pi 4 GPIO header:

| Pi 4 GPIO Pin | USB-Serial | Purpose |
|---------------|------------|---------|
| Pin 6 (GND) | GND | Ground |
| Pin 8 (GPIO14/TXD) | RXD | Pi transmits → Adapter receives |
| Pin 10 (GPIO15/RXD) | TXD | Pi receives ← Adapter transmits |

**CRITICAL:** Do NOT connect power through the adapter. Power the Pi 4 via its USB-C port.

### GPIO Header Reference

```
                      Pi 4 GPIO Header
                    (looking at the board)

       3V3  (1) (2)  5V
     GPIO2  (3) (4)  5V
     GPIO3  (5) (6)  GND    ← Connect to adapter GND
     GPIO4  (7) (8)  GPIO14 ← Connect to adapter RXD
       GND  (9) (10) GPIO15 ← Connect to adapter TXD
```

---

## Step 7: Connect Serial Terminal

### Windows

1. Install PuTTY or similar terminal emulator
2. Find COM port in Device Manager → Ports (COM & LPT)
3. Configure:
   - Connection type: Serial
   - Serial line: COM# (your adapter's port)
   - Speed: 115200
4. Click Open

### Linux/WSL

```bash
# Find device (usually ttyUSB0)
ls /dev/ttyUSB*

# Connect with minicom
sudo minicom -D /dev/ttyUSB0 -b 115200

# Or with screen
sudo screen /dev/ttyUSB0 115200
```

---

## Step 8: Boot and Verify

1. Insert prepared SD card into Pi 4
2. Connect UART serial adapter
3. Open serial terminal
4. Power on Pi 4 via USB-C

### Expected Boot Output

```
========================================
  JARVIS AI-OS v0.2 - Phase 2 Week 32
  ARM64 Raspberry Pi 4 Port
========================================
  seL4 + PL011 UART + Decision Cache
  Build: Dec 27 2025 18:03:37
========================================

System Information:
  Architecture: aarch64 (ARM64)
  Platform: Raspberry Pi 4 (BCM2711)
  CPU: Cortex-A72 @ 1.8 GHz
  Kernel: seL4 microkernel
  UART: PL011 @ 0xFE201000
  Baud: 115200 8N1
  Phase: 2 Week 32

Initializing decision cache...
  Cache initialized (512 entries)
  Loaded 258 patterns into cache

Cache Stats: 258 entries loaded

Starting UART IPC handler...

========================================
UART IPC Handler Running (ARM64)
Waiting for Python queries...
========================================
```

---

## Troubleshooting

### No Output on Serial

1. Check wiring (TXD↔RXD crossed correctly)
2. Verify baud rate is 115200
3. Check config.txt has `enable_uart=1`
4. Try different USB port
5. Verify adapter is 3.3V (NOT 5V!)

### Rainbow Screen Then Nothing

The GPU firmware loaded but kernel failed:

1. Check `kernel8.img` exists and is correct file
2. Verify `arm_64bit=1` in config.txt
3. Try adding `boot_delay=1` to config.txt
4. Check kernel was built for ARM64 (`file kernel8.img`)

### Kernel Panic / VM Fault

seL4 memory mapping issue:

1. UART driver uses direct cast (may fault on strict seL4 config)
2. Check seL4 kernel log for fault address
3. See TROUBLESHOOTING.md for memory mapping fixes

### Garbage Characters on Serial

1. Wrong baud rate - must be 115200
2. Wrong parity - must be 8N1
3. Electrical noise - use shorter wires

---

## Quick Reference

### SD Card Checklist

- [ ] SD card formatted FAT32
- [ ] start4.elf present
- [ ] fixup4.dat present
- [ ] kernel8.img present
- [ ] config.txt present with correct settings
- [ ] All 4 files in root directory (not in subfolder)

### Serial Console Checklist

- [ ] USB adapter connected to PC
- [ ] GND connected (Pin 6)
- [ ] TXD→RXD crossed (Pin 8 to adapter RXD)
- [ ] RXD←TXD crossed (Pin 10 to adapter TXD)
- [ ] Terminal at 115200 8N1
- [ ] NO power through adapter

### Boot Sequence

1. Power applied → GPU firmware runs (start4.elf)
2. GPU reads config.txt
3. GPU loads kernel8.img to memory
4. GPU starts ARM CPU at kernel entry
5. seL4 initializes
6. JARVIS main_arm64.c runs
7. UART output appears

---

## Files Created By This Guide

| File | Location | Description |
|------|----------|-------------|
| config.txt | SD card root | Boot configuration |
| kernel8.img | SD card root | seL4 + JARVIS kernel |

## See Also

- `UART_IPC_PROTOCOL.md` - Communication protocol
- `TROUBLESHOOTING.md` - Common issues and solutions
- `PHASE_2_HARDWARE_PIVOT.md` - Why Pi 4 was chosen
- `WEEK_32_STATUS.md` - ARM64 port status
