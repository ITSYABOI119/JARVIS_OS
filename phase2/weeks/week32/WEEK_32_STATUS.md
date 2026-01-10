# Week 32: JARVIS ARM64 Port

**Date:** December 27, 2025
**Status:** IN PROGRESS (Core + PC-Only Work Complete, Awaiting Hardware)
**Time Spent:** ~7 hours (3h ARM64 port + 4h PC-only prep)

---

## Summary

Week 32 focuses on porting JARVIS to ARM64 for Raspberry Pi 4. All core components now compile for ARM64 architecture. While awaiting hardware, additional PC-only work was completed: UART stress tests, latency simulation, AI-UART integration tests, and comprehensive documentation. Full integration with seL4 requires hardware testing.

---

## Completed Tasks

### 1. ARM64 Main Entry Point ✅
- **File:** `phase2/src/sel4/main_arm64.c` (450 lines)
- **Features:**
  - PL011 UART-based IPC (not ivshmem)
  - UART IPC protocol with CRC-16-CCITT
  - Decision cache integration
  - Message handling: QUERY, HEARTBEAT, STATS_REQUEST
  - ARM64 architecture detection
  - Response formatting for Python client

### 2. ARM64 Cross-Compilation ✅
- **Toolchain:** `aarch64-linux-gnu-gcc` (GCC 13.3.0)
- **All components compile for ARM64:**

| File | Size | Architecture |
|------|------|--------------|
| `decision_cache.o` | 7.4 KB | ARM aarch64 |
| `cache_patterns.o` | 29.7 KB | ARM aarch64 |
| `uart_pl011.o` | 10.6 KB | ARM aarch64 |
| `main_arm64.o` | 9.5 KB | ARM aarch64 |

### 3. CMakeLists.txt Reference ✅
- **File:** `phase2/src/sel4/CMakeLists_arm64.txt`
- Documents source structure for seL4 integration
- Specifies compiler flags for Cortex-A72

### 4. Build Script ✅
- **File:** `phase2/scripts/build_arm64_test.sh`
- Tests ARM64 cross-compilation without full seL4 build
- Validates toolchain and generates object files

### 5. SD Card Preparation ✅ (December 29, 2025 - Updated January 2, 2026)
- **Status:** 4/4 boot files ready ✅
- **SD Card:** D:\ formatted FAT32
- **Files Verified on D:\:**
  - `kernel8.img` (701 KB) - JARVIS seL4 kernel (MD5: 3b0d839f0b5a7d187dfc6a77f446aeaa)
  - `start4.elf` (2.3 MB) - GPU firmware
  - `fixup4.dat` (5.5 KB) - Memory configuration
  - `config.txt` (476 bytes) - Boot configuration
- **Ready for:** Immediate boot when Pi 4 arrives (5-10 min setup time)

---

## Key Differences: x86 vs ARM64

| Aspect | x86 (Week 28) | ARM64 (Week 32) |
|--------|---------------|-----------------|
| Platform | QEMU pc99 | Raspberry Pi 4 |
| IPC Method | ivshmem shared memory | UART serial |
| IPC Latency | ~54μs | ~10-20ms |
| Memory Access | Direct mmap | Capability-based |
| UART Base | N/A | 0xFE201000 (BCM2711) |
| Sync Method | Ring buffer polling | Frame-based CRC |

---

## UART IPC Protocol (ARM64)

### Frame Structure
```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  SYNC    │  TYPE    │   SEQ    │  LENGTH  │  FLAGS   │ PAYLOAD  │  CRC16   │
│ (2 bytes)│ (1 byte) │ (2 bytes)│ (2 bytes)│ (1 byte) │ (0-240)  │ (2 bytes)│
│  0xAA55  │  0x01-0E │  0-65535 │  0-240   │  0x00    │  data    │ CRC-CCITT│
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
```

### Message Types Supported
| Type | Value | Handler |
|------|-------|---------|
| QUERY | 0x01 | `handle_query()` - cache lookup |
| RESPONSE | 0x02 | (outgoing only) |
| HEARTBEAT | 0x03 | `handle_heartbeat()` - ACK |
| STATS_REQUEST | 0x05 | `handle_stats_request()` |

---

## seL4 Integration Notes

### UART Memory Mapping
The PL011 UART driver (`uart_pl011.c`) includes detailed TODO comments for seL4 capability-based memory mapping:

1. **CAmkES Dataport** (Recommended)
   - Map 0xFE201000 via ADL configuration
   - Most seL4-idiomatic approach

2. **ps_io_map()** (Alternative)
   - Use sel4platsupport `io_mapper`
   - Requires initialized `ps_io_ops_t`

3. **Direct Cast** (Current, May Fault)
   - Works if seL4 creates identity mapping
   - Will cause VM fault on standard seL4 config

### First Boot Strategy
1. Try direct cast (may work in tutorials framework)
2. If VM fault: implement `ps_io_map()` approach
3. For production: use CAmkES dataport

---

## QEMU Limitations

**QEMU does NOT support Raspberry Pi 4:**
```
$ qemu-system-aarch64 --machine help | grep -i raspi
raspi0, raspi1ap, raspi2b, raspi3ap, raspi3b
```

**Workaround options:**
1. Use `raspi3b` for basic ARM64 testing (similar PL011 UART)
2. Wait for real Pi 4 hardware
3. Use Pi 5 as host PC running Python AI

---

## Pi 5 as Host PC Option

While seL4 doesn't support Pi 5 (BCM2712), it CAN run Linux as the host:

```
Pi 5 (Linux)  ◄──UART──►  Pi 4 (seL4)
Python AI                  Decision Cache
Phi-3 Mini                 258 patterns
SHIELD                     <1ms lookup
```

This allows testing the full split architecture when Pi 4 arrives.

---

## PC-Only Work (While Awaiting Hardware)

### 5. UART Protocol Stress Tests ✅
- **File:** `phase2/src/ai/test_uart_stress.py` (850+ lines)
- **Tests:** 20 stress tests covering:
  - Rapid frame building (100 frames)
  - CRC error injection and detection
  - Timeout edge cases
  - Sequence number wraparound
  - Concurrent build/parse operations
  - All message types roundtrip
  - All flag combinations

### 6. Latency Simulation ✅
- **File:** `phase2/src/ai/uart_ipc_client.py` (updated)
- **Features:**
  - `mock_mode` parameter for forced mock mode
  - `mock_latency_ms` parameter (10-20ms realistic)
  - `mock_latency_jitter` for +/-20% variation
  - Latency tracking in statistics

### 7. AI Model UART Integration Tests ✅
- **File:** `phase2/src/ai/test_ai_uart_integration.py` (650+ lines)
- **Tests:** 15 integration tests covering:
  - AI response serialization (short, max, Unicode, truncation)
  - Query pipeline with mock processor
  - Query → UART → response roundtrip
  - Inference + serialization benchmarks
  - End-to-end mock flows with SHIELD

### 8. SD Card Setup Guide ✅
- **File:** `phase2/docs/SD_CARD_SETUP.md` (400+ lines)
- **Covers:**
  - Firmware download (start4.elf, fixup4.dat)
  - SD card formatting (FAT32)
  - config.txt configuration
  - UART wiring diagram
  - Boot verification steps

### 9. Troubleshooting Documentation ✅
- **File:** `phase2/docs/TROUBLESHOOTING.md` (500+ lines)
- **Covers:**
  - UART connection issues
  - seL4 boot failures
  - Memory mapping issues
  - Cache hit rate problems
  - Python IPC client issues
  - Build and compilation issues

---

## Files Created

| File | Location | Lines |
|------|----------|-------|
| `main_arm64.c` | `phase2/src/sel4/` | 450 |
| `CMakeLists_arm64.txt` | `phase2/src/sel4/` | 60 |
| `build_arm64_test.sh` | `phase2/scripts/` | 80 |
| `test_uart_stress.py` | `phase2/src/ai/` | 850+ |
| `test_ai_uart_integration.py` | `phase2/src/ai/` | 650+ |
| `SD_CARD_SETUP.md` | `phase2/docs/` | 400+ |
| `TROUBLESHOOTING.md` | `phase2/docs/` | 500+ |
| `WEEK_32_STATUS.md` | `phase2/weeks/week32/` | This file |

---

## Hardware Steps - ALL COMPLETE (January 2026)

### SD Card Status (Final)
1. ✅ Format SD card with FAT32 boot partition (D:\)
2. ✅ Copy firmware (`start4.elf`, `fixup4.dat`, `config.txt`)
3. ✅ Build full seL4 kernel with JARVIS integrated - **DONE** (kernel8.img 1.5MB)
4. ✅ Copy `kernel8.img` to D:\ - **DONE**
5. ✅ Connect USB-UART cable (GPIO14/15) - **DONE**
6. ✅ First boot test via serial console - **DONE** (January 7-8, 2026)

**Boot Verified:** GPU → U-Boot (80ms) → seL4 elfloader → kernel → JARVIS rootserver

### Kernel Build Options

**Option A: TII Build System (Proper Way)**
```bash
# 1. Enable Docker Desktop WSL2 integration
# 2. Install make in WSL
sudo apt install make

# 3. Clone TII manifest
cd ~/sel4-jarvis
repo init -u https://github.com/tiiuae/tii_sel4_manifest.git -b tii/development
repo sync

# 4. Build for Pi 4
make raspberrypi4-64_defconfig
make

# 5. Copy kernel
cp images/kernel.elf kernel8.img
cp kernel8.img /mnt/d/
```

**Option B: Test Boot First**
1. Download pre-built ARM64 seL4 demo kernel
2. Rename to kernel8.img → D:\
3. Verify Pi 4 boots (proves hardware + SD setup works)
4. Then build JARVIS integration properly

### Week 33: UART IPC Implementation
1. Full bidirectional UART IPC
2. Python client connected via `/dev/ttyUSB0`
3. Cache lookup validation
4. Round-trip latency measurement

### Week 34: Integration Testing
1. 100 query test through UART
2. Cache hit rate validation (target: >80%)
3. Performance benchmarking

---

## Validation Commands

```bash
# Verify ARM64 toolchain
wsl -e bash -c "aarch64-linux-gnu-gcc --version"

# Verify object files
wsl -e bash -c "file phase2/build_arm64/*.o"

# Test Python UART client (mock mode)
wsl -e bash -c "cd phase2/src/ai && python3 -c 'from uart_ipc_client import UARTIPCClient; c = UARTIPCClient(); print(c)'"
```

---

## Notes

- ARM64 compilation successful without seL4 headers
- UART driver uses ARM64 memory barriers (`dmb sy`)
- CRC-16-CCITT matches Python client implementation
- Sequence numbers ready for packet loss detection
- Timeout handling built into frame reception

---

---

## Test Summary

| Test Suite | Tests | Status |
|------------|-------|--------|
| test_uart_ipc_client.py | 22 | ✅ 100% PASS |
| test_uart_stress.py | 20 | ✅ 100% PASS |
| test_ai_uart_integration.py | 15 | ✅ 100% PASS |
| **Total** | **57** | **100% PASS** |

---

*Week 32 Status: ✅ COMPLETE - Pi 4 boots JARVIS with UART output*
*Hardware verified: January 7-8, 2026*
*Next: Week 33 (UART RX Enable) → Week 34 (Python↔seL4 IPC Testing)*
