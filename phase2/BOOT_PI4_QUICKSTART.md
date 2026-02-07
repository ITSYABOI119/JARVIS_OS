# JARVIS Pi 4 Boot - Quick Start Guide

**Goal:** Build JARVIS kernel and boot on Raspberry Pi 4 with UART serial console

---

## Step 1: Build JARVIS Kernel (WSL)

The kernel8.img in `firmware/` is too small (992 bytes). We need to build it properly.

### Option A: Use Existing Build (if available)

```bash
# In WSL
cd ~/sel4-workspace/rpi4_jarvis
ls -lh images/kernel8.img
# If it exists and is ~700KB, copy it:
cp images/kernel8.img /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/firmware/
```

### Option B: Build from Scratch

```bash
# In WSL
cd ~/sel4-workspace

# If jarvis-sel4 project doesn't exist, create it:
mkdir -p projects/jarvis-sel4/src
cd projects/jarvis-sel4

# Copy JARVIS sources
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/cache src/
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/ipc src/
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/drivers src/
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/ipc_phase2 src/
cp /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/sel4/main_arm64.c src/main.c

# Copy CMakeLists.txt
cp /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/jarvis-sel4-cmake/CMakeLists.txt .

# Create settings.cmake (if needed)
# Build
mkdir -p ../../rpi4_jarvis
cd ../../rpi4_jarvis
cmake -G Ninja \
    -DCROSS_COMPILER_PREFIX=aarch64-linux-gnu- \
    -DKernelPlatform=bcm2711 \
    -DKernelSel4Arch=aarch64 \
    ../projects/jarvis-sel4
ninja

# Copy kernel to firmware directory
cp images/kernel8.img /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/firmware/
```

---

## Step 2: Copy Files to SD Card

```cmd
REM In Windows PowerShell or CMD
cd C:\Users\jluca\Documents\JARVIS_OS\phase2
copy_to_sd.bat D:
```

**Replace `D:` with your SD card drive letter!**

Verify files on SD card:
- `kernel8.img` (~700KB)
- `start4.elf` (~2.3MB)
- `fixup4.dat` (~5.5KB)
- `config.txt` (~500 bytes)

---

## Step 3: Connect UART Serial Adapter

### Wiring (CRITICAL - TXD/RXD must be crossed!)

```
Pi 4 GPIO Header          USB-Serial Adapter
─────────────────         ──────────────────
Pin 6  (GND)        ───── GND
Pin 8  (GPIO14/TXD) ───── RXD  (Pi TX → Adapter RX)
Pin 10 (GPIO15/RXD) ───── TXD  (Adapter TX → Pi RX)
```

**DO NOT connect power through adapter!** Power Pi 4 via USB-C port.

### Find COM Port

1. Connect USB-serial adapter to PC
2. Open Device Manager → Ports (COM & LPT)
3. Note the COM number (e.g., COM3)

---

## Step 4: Configure serial console

1. **Open serial console**

2. **Session Tab:**
   - Connection type: ☑ **Serial**
   - Serial line: `COM3` (your adapter's port)
   - Speed: `115200`
   - Saved Sessions: Type `Pi4-UART` and click **Save**

3. **Connection → Serial:**
   - Serial line: `COM3`
   - Speed (baud): `115200`
   - Data bits: `8`
   - Stop bits: `1`
   - Parity: `None`
   - Flow control: `None` ⚠️ **MUST be None!**

4. **Session → Logging (Optional but recommended):**
   - Session logging: All session output
   - Log file: `C:\Users\jluca\Documents\JARVIS_OS\phase2\logs\serial_console.log`
   - If log exists: Always append
   - ✓ Flush log file frequently
   - ✓ Include header

5. **Click Open** (or Load saved session then Open)

---

## Step 5: Boot Pi 4

1. Insert SD card into Pi 4
2. Connect UART adapter (GPIO14/15 + GND)
3. **serial console should already be open** (see Step 4)
4. Power on Pi 4 via USB-C

### Expected Output

```
ELF-loader started on CPU 0
  paddr=[0x0000000000080000..0x00000000017f0fff]

========================================
  JARVIS AI-OS v0.2 - Phase 2 Week 32
  ARM64 Raspberry Pi 4 Port
========================================
  seL4 + PL011 UART + Decision Cache
  Build: [date] [time]
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
2. Verify baud rate is exactly 115200
3. Check config.txt has `enable_uart=1`
4. Try different USB port (prefer USB 2.0)
5. Verify adapter is 3.3V (NOT 5V!)

### Garbage Characters

1. Wrong baud rate - must be 115200
2. Wrong parity - must be 8N1 (8 data, None parity, 1 stop)
3. Flow control must be None (not XON/XOFF)

### Wrong COM Port

1. Check Device Manager for actual COM#
2. Update serial console "Serial line" to match
3. Close any other programs using the serial port

### Kernel Too Small

If kernel8.img is < 100KB, the build didn't work. Check:
- Build completed without errors
- File is ARM64 ELF format: `file kernel8.img` should show "ARM aarch64"

---

## Quick Reference

### SD Card Checklist
- [ ] SD card formatted FAT32
- [ ] kernel8.img present (~700KB)
- [ ] start4.elf present (~2.3MB)
- [ ] fixup4.dat present (~5.5KB)
- [ ] config.txt present

### serial console Checklist
- [ ] Connection type: Serial
- [ ] Serial line: COM# (correct port)
- [ ] Speed: 115200
- [ ] Data bits: 8, Stop bits: 1, Parity: None
- [ ] Flow control: None
- [ ] Session saved (optional)

### Hardware Checklist
- [ ] GND connected (Pin 6)
- [ ] TXD→RXD crossed (Pin 8 to adapter RXD)
- [ ] RXD←TXD crossed (Pin 10 to adapter TXD)
- [ ] Pi 4 powered via USB-C (NOT through adapter)

---

**See Also:**
- `SERIAL_CONSOLE_SETUP.md` - Detailed serial console configuration
- `SD_CARD_SETUP.md` - SD card preparation guide
- `TROUBLESHOOTING.md` - Common issues and solutions


