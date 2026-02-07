# JARVIS Pi 4 Boot Checklist

Use this checklist to ensure everything is ready for booting JARVIS on your Pi 4.

---

## ✅ Pre-Boot Checklist

### 1. Kernel Build
- [ ] TII seL4 workspace exists at `~/sel4-workspace`
- [ ] JARVIS project exists at `~/sel4-workspace/projects/jarvis-sel4`
- [ ] ARM64 toolchain installed (`aarch64-linux-gnu-gcc`)
- [ ] Kernel built successfully
- [ ] `kernel8.img` copied to `phase2/firmware/` (~700KB)

**Build Command:**
```bash
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/scripts
./build_and_copy_kernel.sh
```

### 2. SD Card Preparation
- [ ] SD card formatted as FAT32
- [ ] `kernel8.img` present (~700KB)
- [ ] `start4.elf` present (~2.3MB)
- [ ] `fixup4.dat` present (~5.5KB)
- [ ] `config.txt` present (~500 bytes)

**Copy Command:**
```cmd
cd C:\Users\jluca\Documents\JARVIS_OS\phase2
copy_to_sd.bat D:
```
(Replace `D:` with your SD card drive letter)

### 3. UART Hardware Connection
- [ ] USB-serial adapter connected to PC
- [ ] COM port identified (Device Manager → Ports)
- [ ] GND connected (Pi 4 Pin 6 → Adapter GND)
- [ ] TXD→RXD crossed (Pi 4 Pin 8 → Adapter RXD)
- [ ] RXD←TXD crossed (Pi 4 Pin 10 → Adapter TXD)
- [ ] Pi 4 powered via USB-C (NOT through adapter)

### 4. serial console Configuration
- [ ] serial console installed
- [ ] Connection type: Serial
- [ ] Serial line: COM# (correct port)
- [ ] Speed: 115200
- [ ] Data bits: 8, Stop bits: 1, Parity: None
- [ ] Flow control: None (CRITICAL!)
- [ ] Session saved (optional: "Pi4-UART")
- [ ] Logging enabled (optional but recommended)

**serial console Settings:**
- Session → Connection type: Serial
- Connection → Serial → Flow control: **None**
- Session → Logging → Log file: `phase2/logs/serial_console.log`

---

## 🚀 Boot Sequence

1. [ ] Insert SD card into Pi 4
2. [ ] Connect UART adapter (GPIO14/15 + GND)
3. [ ] Open serial console (load saved session or configure manually)
4. [ ] Click "Open" in serial console (terminal window appears)
5. [ ] Power on Pi 4 via USB-C
6. [ ] Watch for boot output in serial console

---

## ✅ Expected Boot Output

You should see:

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

## 🔧 Troubleshooting Quick Reference

| Problem | Solution |
|---------|----------|
| No output | Check wiring, baud rate (115200), flow control (None) |
| Garbage characters | Verify 8N1 settings, check baud rate |
| Wrong COM port | Check Device Manager, update serial console |
| Kernel too small | Rebuild kernel (see BUILD_INSTRUCTIONS.md) |
| Build fails | Check TII workspace, verify toolchain |

---

## 📚 Documentation

- `BOOT_PI4_QUICKSTART.md` - Step-by-step boot guide
- `SERIAL_CONSOLE_SETUP.md` - Detailed serial console configuration
- `SD_CARD_SETUP.md` - SD card preparation
- `BUILD_INSTRUCTIONS.md` - Kernel build guide
- `TROUBLESHOOTING.md` - Common issues and solutions

---

**Status:** Ready to boot when all items checked ✅


