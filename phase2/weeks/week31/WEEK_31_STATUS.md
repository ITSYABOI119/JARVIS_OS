# Week 31 Pre-Hardware Prep Status

**Date:** December 26, 2025
**Status:** COMPLETE (7/7 tasks done)
**Time Spent:** ~2 hours

---

## Summary

All pre-hardware preparation tasks are complete. When the Raspberry Pi 4 arrives, we can immediately proceed with hardware setup.

---

## Completed Tasks

### 1. ARM64 Cross-Compilation Toolchain ✅
- **Installed:** `aarch64-linux-gnu-gcc` (GCC 13.3.0)
- **Location:** WSL Ubuntu
- **Verified:** Working

### 2. seL4 Pi 4 Build System ✅
- **Repo:** seL4 tutorials manifest synced to `~/sel4-pi4`
- **Also available:** TII build system at `~/tii_sel4_build`
- **Python deps:** `pyfdt`, `jinja2`, `plyfile` installed

### 3. seL4 Kernel for Pi 4 ✅
- **Build successful:** 44/44 ninja targets completed
- **Output:** `~/sel4-pi4/build/kernel.elf`
- **Architecture:** ARM64 (aarch64)
- **Size:** 138KB (116KB text, 2.5KB data)
- **Platform:** BCM2711 (Raspberry Pi 4)

### 4. Pi 4 GPU Firmware ✅
- **Location:** `~/pi4-firmware/`
- **Files:**
  - `start4.elf` (2.2 MB) - GPU firmware
  - `fixup4.dat` (5.5 KB) - Memory fixup

### 5. PL011 UART Driver ✅
- **Header:** `phase2/src/drivers/uart_pl011.h` (195 lines)
- **Implementation:** `phase2/src/drivers/uart_pl011.c` (301 lines)
- **Features:**
  - BCM2711 PL011 at 0xFE201000
  - 115200 8N1 default
  - TX/RX FIFO support
  - Statistics tracking
  - ARM64 memory barriers

### 6. UART IPC Protocol Spec ✅
- **Document:** `phase2/docs/UART_IPC_PROTOCOL.md` (450+ lines)
- **Format:** Binary frames with CRC-16-CCITT
- **Message types:** 14 defined (QUERY, RESPONSE, HEARTBEAT, etc.)
- **Performance:** ~10-20ms round-trip (vs 54μs shared memory)

### 7. Python UART Client ✅
- **File:** `phase2/src/ai/uart_ipc_client.py` (550+ lines)
- **Features:**
  - pyserial-based (with mock fallback)
  - Full protocol implementation
  - Threading for async RX
  - CRC-16-CCITT validation
  - cache_lookup(), shield_check(), send_command()

---

## Files Created

| File | Location | Lines |
|------|----------|-------|
| `uart_pl011.h` | `phase2/src/drivers/` | 195 |
| `uart_pl011.c` | `phase2/src/drivers/` | 301 |
| `UART_IPC_PROTOCOL.md` | `phase2/docs/` | 450+ |
| `uart_ipc_client.py` | `phase2/src/ai/` | 550+ |
| `setup_sel4_pi4.sh` | `phase2/scripts/` | 50 |

---

## WSL Resources

| Resource | Location |
|----------|----------|
| seL4 source | `~/sel4-pi4/` |
| seL4 kernel.elf | `~/sel4-pi4/build/kernel.elf` |
| TII build system | `~/tii_sel4_build/` |
| Pi 4 firmware | `~/pi4-firmware/` |
| ARM64 toolchain | `/usr/bin/aarch64-linux-gnu-gcc` |

---

## Validation Commands

```bash
# Verify toolchain
wsl -e bash -c "aarch64-linux-gnu-gcc --version"

# Verify kernel
wsl -e bash -c "file ~/sel4-pi4/build/kernel.elf"

# Verify firmware
wsl -e bash -c "ls -la ~/pi4-firmware/"

# Test UART client (mock mode)
wsl -e bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/ai && python3 uart_ipc_client.py --test-crc"
```

---

## When Hardware Arrives

1. **Format SD card** with FAT32 boot partition
2. **Copy to SD card:**
   - `start4.elf`, `fixup4.dat` from `~/pi4-firmware/`
   - `kernel.elf` from `~/sel4-pi4/build/`
   - Create `config.txt` for Pi 4 boot config
3. **Connect USB-serial adapter:**
   - GPIO14 (TXD) → RXD
   - GPIO15 (RXD) → TXD
   - GND → GND
4. **Power on Pi 4**
5. **Open terminal:** `screen /dev/ttyUSB0 115200`
6. **Verify seL4 boot messages**

---

## Next Steps (Week 31 Proper) - NOW COMPLETE

**Hardware arrived December 27, 2025. All tasks completed in Weeks 32-33:**

1. ✅ Create bootable SD card - **DONE Week 32** (7 boot files on FAT32)
2. ✅ First seL4 boot test - **DONE Week 32** (GPU → U-Boot → seL4 → rootserver)
3. ✅ UART console verification - **DONE Week 32-33** (TX via seL4_DebugPutChar)
4. ✅ Integrate JARVIS code into seL4 build - **DONE Week 32** (kernel8.img 1.5MB)
5. ⏳ Test UART IPC end-to-end - **Week 34** (RX enabled, awaiting Python client test)

---

## Notes

- The seL4 tutorials framework doesn't expose Pi 4 as a platform option in `init`, but the kernel itself fully supports BCM2711
- Direct kernel build works: `cmake -DKernelPlatform=bcm2711 -DKernelSel4Arch=aarch64 ...`
- UART latency (~10-20ms) is acceptable since decision cache handles the fast path
- TinyLlama 1.1B can run on Pi 4 with 5-15 tokens/sec (sufficient for AI queries)
