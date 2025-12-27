# Week 32: JARVIS ARM64 Port

**Date:** December 27, 2025
**Status:** IN PROGRESS (Core Work Complete, Awaiting Hardware)
**Time Spent:** ~3 hours

---

## Summary

Week 32 focuses on porting JARVIS to ARM64 for Raspberry Pi 4. All core components now compile for ARM64 architecture. Full integration with seL4 requires hardware testing.

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

## Files Created

| File | Location | Lines |
|------|----------|-------|
| `main_arm64.c` | `phase2/src/sel4/` | 450 |
| `CMakeLists_arm64.txt` | `phase2/src/sel4/` | 60 |
| `build_arm64_test.sh` | `phase2/scripts/` | 80 |
| `WEEK_32_STATUS.md` | `phase2/weeks/week32/` | This file |

---

## Next Steps (When Hardware Arrives)

### Immediate (Pi 4 Arrival)
1. ⏳ Format SD card with FAT32 boot partition
2. ⏳ Copy firmware (`start4.elf`, `fixup4.dat`)
3. ⏳ Build full seL4 kernel with JARVIS integrated
4. ⏳ Create `config.txt` for Pi 4 boot
5. ⏳ Connect USB-UART cable (GPIO14/15)
6. ⏳ First boot test via serial console

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

*Week 32 Status: Core ARM64 port complete, awaiting Pi 4 hardware*
*Next: Week 33 (UART IPC Implementation) when hardware arrives*
