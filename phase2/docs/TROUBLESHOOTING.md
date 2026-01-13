# JARVIS Phase 2 Troubleshooting Guide

## Overview

This document covers common issues when running JARVIS on Raspberry Pi 4 with seL4 and UART IPC.

---

## Table of Contents

1. [UART Connection Issues](#1-uart-connection-issues)
2. [seL4 Boot Failures](#2-sel4-boot-failures)
3. [Memory Mapping Issues](#3-memory-mapping-issues)
4. [Cache Hit Rate Problems](#4-cache-hit-rate-problems)
5. [Python IPC Client Issues](#5-python-ipc-client-issues)
6. [Build and Compilation Issues](#6-build-and-compilation-issues)

---

## 1. UART Connection Issues

### Problem: No output on serial console

**Symptoms:**
- Terminal shows nothing when Pi 4 powers on
- No characters at all, not even garbage

**Solutions:**

1. **Check wiring (most common issue)**
   ```
   Pi 4 Pin 8 (TXD) → Adapter RXD
   Pi 4 Pin 10 (RXD) → Adapter TXD
   Pi 4 Pin 6 (GND) → Adapter GND
   ```
   The TX/RX lines must be crossed!

2. **Verify adapter voltage**
   - Must be 3.3V TTL (NOT 5V or RS-232)
   - 5V signals can damage Pi 4 GPIO

3. **Check config.txt settings**
   ```ini
   enable_uart=1
   uart_2ndstage=1
   dtoverlay=disable-bt
   ```

4. **Try different USB port**
   - Some USB 3.0 ports have issues with serial adapters
   - Try USB 2.0 port

5. **Verify adapter is detected**
   ```bash
   # Linux/WSL
   ls /dev/ttyUSB*
   dmesg | tail -20  # Check for USB serial detection

   # Windows
   # Check Device Manager → Ports (COM & LPT)
   ```

### Problem: Garbage characters on serial

**Symptoms:**
- Output appears but is unreadable
- Random symbols instead of text

**Solutions:**

1. **Verify baud rate**
   - Must be exactly 115200
   - Both Pi 4 and terminal must match

2. **Check terminal settings**
   ```
   Speed: 115200
   Data bits: 8
   Parity: None
   Stop bits: 1
   Flow control: None
   ```

3. **Reduce wire length**
   - Long wires can cause signal degradation
   - Use wires under 30cm if possible

4. **Check ground connection**
   - Poor ground can cause noise
   - Ensure solid GND connection

### Problem: Partial output then stops

**Symptoms:**
- Banner prints but system hangs
- Output stops at specific point

**Solutions:**

1. **Check for crashes in seL4**
   - Look for "Kernel panic" or "VM fault" messages
   - See [Memory Mapping Issues](#3-memory-mapping-issues)

2. **Increase serial buffer**
   - Some terminals drop characters
   - Try different terminal (minicom vs screen vs serial console)

---

## 2. seL4 Boot Failures

### Problem: Rainbow screen then black

**Symptoms:**
- Rainbow splash appears
- Screen goes black
- No serial output

**Solutions:**

1. **Verify kernel8.img exists**
   ```bash
   ls -la /mnt/sd/
   # Should show kernel8.img
   ```

2. **Check file is ARM64**
   ```bash
   file kernel8.img
   # Should say "ARM aarch64" or "ELF 64-bit LSB"
   ```

3. **Verify config.txt**
   ```ini
   arm_64bit=1
   kernel=kernel8.img
   ```

4. **Try boot delay**
   Add to config.txt:
   ```ini
   boot_delay=2
   ```

### Problem: seL4 assertion failure

**Symptoms:**
- Output shows "Assertion failed"
- Kernel prints debug info then stops

**Solutions:**

1. **Check kernel configuration**
   - Verify built for rpi4 platform
   - Check CMake options

2. **Memory configuration**
   - Pi 4 has different memory map than Pi 3
   - Ensure using Pi 4 specific addresses

### Problem: "No bootloader found"

**Symptoms:**
- Error message about bootloader
- GPU firmware didn't load

**Solutions:**

1. **Check firmware files**
   - `start4.elf` must exist (not start.elf)
   - `fixup4.dat` must exist (not fixup.dat)

2. **Verify SD card format**
   - Must be FAT32
   - MBR partition table (not GPT)

---

## 3. Memory Mapping Issues

### Problem: VM fault at 0xFE201000

**Symptoms:**
```
VM Fault at address 0xFE201000
Fault type: Data abort
```

**Cause:** Direct cast to UART address without proper memory mapping.

**Solutions:**

1. **Check UART driver implementation**
   The PL011 driver in `uart_pl011.c` attempts direct access:
   ```c
   volatile pl011_regs_t *uart = (pl011_regs_t *)UART0_BASE;
   ```

2. **Use seL4 memory mapping**
   Replace with proper capability-based access:
   ```c
   // Option 1: ps_io_map (requires io_ops)
   void *uart = ps_io_map(&io_ops, UART0_BASE, 0x1000, ...);

   // Option 2: CAmkES dataport (production)
   // Configure in ADL file
   ```

3. **Enable identity mapping (tutorials framework)**
   If using seL4 tutorials framework, it may create identity mappings.
   Check tutorial kernel configuration.

### Problem: Memory allocation failure

**Symptoms:**
- "Failed to allocate memory"
- Crash during cache initialization

**Solutions:**

1. **Check available memory**
   - Decision cache needs ~200KB
   - Ensure heap is configured

2. **Reduce cache size**
   In `decision_cache.h`:
   ```c
   #define CACHE_SIZE 256  // Reduce if memory constrained
   ```

---

## 4. Cache Hit Rate Problems

### Problem: 0% cache hit rate

**Symptoms:**
- All queries return MISS
- Cache statistics show 0 hits

**Solutions:**

1. **Check patterns loaded**
   ```
   Cache Stats: 258 entries loaded
   ```
   If 0 entries, pattern loading failed.

2. **Verify query normalization**
   - Queries must be lowercase
   - Extra whitespace removed
   - Check `cache_normalize_query()` is called

3. **Check cache lookup**
   - Verify hash function matches between loader and lookup
   - FNV-1a 64-bit must be consistent

### Problem: Low cache hit rate (<80%)

**Symptoms:**
- Many queries return MISS
- Hit rate below 80% target

**Solutions:**

1. **Analyze miss patterns**
   Log queries that miss and check:
   - Are they close to cached patterns?
   - Is normalization working?

2. **Add more patterns**
   In `cache_patterns.c`:
   ```c
   // Add common variations
   {"show files", "exec_ls", TRUST_AUTOMATIC},
   {"display files", "exec_ls", TRUST_AUTOMATIC},
   ```

3. **Implement fuzzy matching** (future enhancement)
   - Levenshtein distance
   - Keyword extraction

### Problem: Cache overflow

**Symptoms:**
- "Cache full" warning
- New patterns not loaded

**Solutions:**

1. **Increase cache size**
   In `decision_cache.h`:
   ```c
   #define CACHE_SIZE 512  // Increase from 256
   ```

2. **Remove unused patterns**
   - Audit `cache_patterns.c`
   - Remove rarely-used entries

---

## 5. Python IPC Client Issues

### Problem: Mock mode instead of real UART

**Symptoms:**
- "[UART] Warning: pyserial not installed"
- Mock responses returned

**Solutions:**

1. **Install pyserial**
   ```bash
   pip3 install pyserial
   ```

2. **Verify installation**
   ```python
   import serial
   print(serial.VERSION)
   ```

### Problem: Serial port not found

**Symptoms:**
- "No such file or directory: /dev/ttyUSB0"

**Solutions:**

1. **Check device exists**
   ```bash
   ls /dev/ttyUSB*
   ```

2. **Use correct port name**
   - Linux: `/dev/ttyUSB0` or `/dev/ttyACM0`
   - WSL: May need USB passthrough
   - Windows: `COM3`, `COM4`, etc.

3. **Check permissions**
   ```bash
   sudo chmod 666 /dev/ttyUSB0
   # Or add user to dialout group:
   sudo usermod -a -G dialout $USER
   ```

### Problem: CRC errors on receive

**Symptoms:**
- "[UART] CRC error: expected 0xXXXX, got 0xYYYY"
- Frames rejected

**Solutions:**

1. **Check for noise**
   - Use shorter wires
   - Add ground wire
   - Move away from EMI sources

2. **Verify CRC algorithm**
   Both sides must use same CRC-16-CCITT:
   - Polynomial: 0x1021
   - Initial value: 0xFFFF

3. **Check byte order**
   - CRC is little-endian in frame
   - Verify struct packing matches

### Problem: Timeout waiting for response

**Symptoms:**
- "Timeout waiting for response"
- Heartbeat failures

**Solutions:**

1. **Check Pi 4 is responding**
   - Look at serial console for activity
   - Verify UART handler is running

2. **Increase timeout**
   In `uart_ipc_client.py`:
   ```python
   RESPONSE_TIMEOUT = 1.0  # Increase from 0.5
   ```

3. **Check for missed bytes**
   - Enable debug logging
   - Look for frame sync issues

---

## 6. Build and Compilation Issues

### Problem: aarch64-linux-gnu-gcc not found

**Symptoms:**
- "aarch64-linux-gnu-gcc: command not found"

**Solutions:**

1. **Install ARM64 toolchain**
   ```bash
   sudo apt install gcc-aarch64-linux-gnu
   ```

2. **Verify installation**
   ```bash
   aarch64-linux-gnu-gcc --version
   ```

### Problem: Missing seL4 headers

**Symptoms:**
- "sel4/sel4.h: No such file or directory"

**Solutions:**

1. **Building standalone (testing)**
   - Use `-D__sel4__=0` to disable seL4 includes
   - Or create stub headers

2. **Building with seL4**
   - Use seL4 tutorials framework
   - Ensure kernel is built first

### Problem: Linker errors

**Symptoms:**
- "undefined reference to `seL4_*`"

**Solutions:**

1. **Check library linking order**
   ```cmake
   target_link_libraries(jarvis
       sel4 muslc utils sel4muslcsys
       sel4platsupport sel4utils sel4debug
   )
   ```

2. **Verify object file architecture**
   ```bash
   file *.o
   # All should say "ARM aarch64"
   ```

---

## Diagnostic Commands

### Check SD Card Contents
```bash
ls -la /mnt/sd/
file /mnt/sd/kernel8.img
cat /mnt/sd/config.txt
```

### Check Serial Connection
```bash
# List USB serial devices
ls /dev/ttyUSB* /dev/ttyACM*

# Test connection (Ctrl+A, X to exit)
sudo screen /dev/ttyUSB0 115200

# Or with minicom
sudo minicom -D /dev/ttyUSB0 -b 115200
```

### Check Python Environment
```bash
python3 -c "import serial; print(serial.VERSION)"
python3 -c "from uart_ipc_client import UARTIPCClient; print('OK')"
```

### Check ARM64 Build
```bash
file phase2/build_arm64/*.o
aarch64-linux-gnu-objdump -d phase2/build_arm64/main_arm64.o | head -50
```

---

## Getting Help

If issues persist:

1. **Check documentation**
   - `UART_IPC_PROTOCOL.md`
   - `SD_CARD_SETUP.md`
   - `WEEK_32_STATUS.md`

2. **Review test results**
   ```bash
   python3 test_uart_ipc_client.py      # 22 tests
   python3 test_uart_stress.py          # 20 tests
   python3 test_ai_uart_integration.py  # 15 tests
   ```

3. **Enable debug output**
   In `uart_ipc_client.py`:
   ```python
   import logging
   logging.basicConfig(level=logging.DEBUG)
   ```

4. **Check seL4 kernel logs**
   Enable verbose output in seL4 config:
   ```cmake
   set(KernelPrinting ON CACHE BOOL "")
   ```
