# Week 30: QEMU ivshmem Shared Memory Integration

**Date:** December 2025
**Phase:** Phase 2 - Alpha System (Month 13)
**Status:** IN PROGRESS

---

## Overview

Week 30 implements real bidirectional IPC between Python (host/WSL) and seL4 (QEMU guest) via QEMU's ivshmem (Inter-VM Shared Memory) device. This bridges the critical gap identified in Week 28 where Python and seL4 used separate memory spaces with no actual shared communication channel.

**Note:** This replaces the original Week 30 "Suspend/Resume Integration" task. Suspend/Resume was already completed in Phase 1 Week 22 (22/22 tests passing) and can be integrated later. The ivshmem connection is prerequisite for all Phase 2 Python↔seL4 work.

---

## Problem Statement

**Week 28 Discovery:**
- Python uses `/dev/shm/jarvis_ipc` (host memory)
- seL4 uses static `dual_ring_buffer_t` (QEMU guest memory)
- These are completely separate address spaces
- No messages can flow between them

**Result:** Python↔seL4 IPC does NOT actually work despite both sides having functional ring buffers.

---

## Solution: QEMU ivshmem Device

QEMU's ivshmem device maps a host file into the guest's physical address space:

```
[Host/WSL]                      [QEMU Guest]
Python App                      seL4 Kernel
    |                               |
    v                               v
mmap("/dev/shm/jarvis_ipc")  pci_ivshmem_map_bar2()
    |                               |
    +----------- SAME ---------------+
              567KB File
         (dual_ring_buffer_t)
```

**QEMU Launch:**
```bash
qemu-system-x86_64 \
    -device ivshmem-plain,memdev=shm0 \
    -object memory-backend-file,size=567K,share=on,mem-path=/dev/shm/jarvis_ipc,id=shm0
```

---

## Tasks

### Task 1: Create Shared Memory Setup Script
**Status:** COMPLETE

**File:** `phase2/src/scripts/create_shm.sh`

Creates the 567KB shared memory file before QEMU starts:
- Zero-initialized with correct size (567,296 bytes)
- Proper permissions (666 for Python and QEMU access)
- Verifies existing file size before overwriting

### Task 2: Create QEMU Wrapper Script
**Status:** COMPLETE

**File:** `phase2/src/scripts/run_jarvis_qemu.sh`

QEMU launch script with ivshmem device:
- Locates seL4 kernel and image files
- Creates shared memory if missing
- Adds ivshmem device arguments
- Documents usage and configuration

### Task 3: Create ivshmem PCI Driver Header
**Status:** COMPLETE

**File:** `phase1/src/sel4/pci_ivshmem.h` (~160 lines)

Defines:
- PCI device identifiers (Vendor: 0x1AF4, Device: 0x1110)
- PCI configuration space offsets
- ivshmem register offsets
- `ivshmem_device_t` structure
- Driver API functions

### Task 4: Create ivshmem PCI Driver Implementation
**Status:** COMPLETE

**File:** `phase1/src/sel4/pci_ivshmem.c` (~200 lines)

Implements:
- `ivshmem_detect()` - PCI enumeration or fallback mode
- `ivshmem_map_bar2()` - Map shared memory to virtual address
- `ivshmem_get_shared_memory()` - Get pointer for dual ring buffer
- Fallback mode if PCI I/O not available (uses fixed physical address)
- Debug printing and device info functions

### Task 5: Update main_week28.c for ivshmem
**Status:** COMPLETE

**File:** `phase1/src/sel4/main_week28.c`

Changes:
- Updated banner to "v0.3 - Phase 2 Week 30"
- Changed `g_dual_ring` from static to pointer
- Added `g_ivshmem` device state and `g_using_ivshmem` flag
- Detection and mapping at startup
- Fallback to local buffer if ivshmem unavailable
- Status display shows ivshmem vs fallback mode

### Task 6: Update Python IPC Client
**Status:** COMPLETE

**File:** `phase1/src/ai/ipc_client.py`

Additions:
- Dual ring buffer constants (size, magic, version)
- `DUAL_RING_MAGIC = 0x4A415256` ("JARV")
- `wait_for_initialization()` - Wait for seL4 to init buffer
- `is_dual_ring_initialized()` - Check magic number
- `_read_uint32()` - Helper for reading from shared memory

### Task 7: Create Week 30 Status Document
**Status:** COMPLETE (this file)

### Task 8: Update PHASE_2_IMPLEMENTATION_PLAN.md
**Status:** PENDING

Update to reflect:
- Week 30: QEMU ivshmem Shared Memory (NEW)
- Original Week 30 (Suspend/Resume) deferred or integrated later

---

## Files Created/Modified

| File | Action | Lines | Purpose |
|------|--------|-------|---------|
| `phase2/src/scripts/create_shm.sh` | Create | ~40 | Shared memory setup |
| `phase2/src/scripts/run_jarvis_qemu.sh` | Create | ~115 | QEMU wrapper with ivshmem |
| `phase1/src/sel4/pci_ivshmem.h` | Create | ~160 | ivshmem driver header |
| `phase1/src/sel4/pci_ivshmem.c` | Create | ~200 | ivshmem driver implementation |
| `phase1/src/sel4/main_week28.c` | Modify | +50 | ivshmem integration |
| `phase1/src/ai/ipc_client.py` | Modify | +40 | Magic validation |
| `phase2/docs/PHASE_2_IMPLEMENTATION_PLAN.md` | Modify | ~20 | Week 30 update |

**Total New Code:** ~605 lines

---

## Testing Plan

### Unit Tests

1. **ivshmem Detection Test**
   - Verify `ivshmem_detect()` returns true with QEMU ivshmem device
   - Verify fallback mode works without device

2. **BAR2 Mapping Test**
   - Verify `ivshmem_map_bar2()` returns valid pointer
   - Verify mapped size matches expected (567KB)

3. **Magic Number Validation**
   - Python: `wait_for_initialization()` returns True after seL4 init
   - seL4: Writes magic number 0x4A415256 at offset 0

### Integration Tests

1. **End-to-End IPC Test**
   - Python sends MSG_CACHE_LOOKUP via query ring
   - seL4 receives, performs cache lookup
   - seL4 sends response via response ring
   - Python receives and validates response

2. **Cache Hit Rate Test**
   - Send 100 queries via ivshmem
   - Verify >80% cache hit rate (matching Week 19 results)

### Performance Tests

1. **IPC Latency**
   - Measure round-trip time for query→response
   - Target: <200μs (100μs each way)

2. **Throughput**
   - Send 1000 messages in sequence
   - Measure messages per second

---

## Success Criteria

| Criterion | Target | Status |
|-----------|--------|--------|
| ivshmem device detected in QEMU | Device visible | Pending |
| BAR2 mapped in seL4 | Valid pointer | Pending |
| Python connects to shared memory | Connection success | Pending |
| Magic number validation works | 0x4A415256 detected | Pending |
| Cache lookup via ivshmem | Response received | Pending |
| Cache hit rate | >= 80% | Pending |
| IPC latency | <200μs | Pending |

---

## Validation Procedure

1. **Start Shared Memory:**
   ```bash
   cd /mnt/c/Users/jluca/Documents/JARVIS_OS
   ./phase2/src/scripts/create_shm.sh
   ```

2. **Start seL4 QEMU:**
   ```bash
   ./phase2/src/scripts/run_jarvis_qemu.sh --build-dir ~/jarvis-phase1/hello-worlde_nd1ae4_build
   ```

3. **Observe seL4 Output:**
   - Should show "Using ivshmem shared memory at 0x..."
   - Should NOT show "WARNING: Running without ivshmem"

4. **Run Python IPC Client:**
   ```bash
   cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/ai
   python3 -c "from ipc_client import IPCClient; c = IPCClient(); print('Init:', c.is_dual_ring_initialized())"
   ```

5. **Test Cache Lookup:**
   ```bash
   python3 run_jarvis.py
   # Type: help
   # Should show cache hit from seL4
   ```

---

## Risks and Mitigations

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| PCI enumeration fails in seL4 | High | Medium | Fallback to fixed physical address |
| seL4 can't map BAR2 | Medium | High | Use direct mapping (phys==virt) |
| Memory alignment mismatch | Low | High | Verify sizeof() matches on both sides |
| QEMU ivshmem not working | Low | Critical | Test QEMU version, use -device syntax |

---

## Next Steps

1. Build seL4 with new ivshmem driver
2. Test ivshmem detection in QEMU
3. Validate end-to-end Python↔seL4 IPC
4. Measure cache hit rate (target: >80%)
5. Document results and update progress tracker

---

## Why Phase 1 Directory?

The ivshmem driver files are placed in `phase1/src/sel4/` because:
1. They are seL4 C code compiled into the kernel binary
2. Same location as `main_week28.c`, `dual_ring_buffer.c`
3. Must be included in CMakeLists.txt for seL4 build
4. Phase 1 contains all seL4 source; Phase 2 adds new features on top

---

**Week 30 Status:** 7/8 tasks complete (CODE COMPLETE)

---

## ⚠️ SUPERSEDED BY HARDWARE PIVOT (January 2026)

**The ivshmem E2E testing was NEVER COMPLETED and is now SUPERSEDED:**

| Item | Original Target | New Status |
|------|-----------------|------------|
| ivshmem device detection | QEMU x86 | ❌ SUPERSEDED by Pi 4 UART |
| BAR2 mapping validation | QEMU guest | ❌ SUPERSEDED - no PCI on Pi 4 |
| Python↔seL4 via ivshmem | End-to-end test | ❌ SUPERSEDED - will use UART |
| Cache hit rate via ivshmem | >80% target | ❌ Will validate on Pi 4 instead |

**Reason:** Phase 2 pivoted from x86 QEMU (ivshmem shared memory) to Raspberry Pi 4 (UART serial IPC). See `phase2/docs/PHASE_2_HARDWARE_PIVOT.md`.

**Week 30 Code Status:**
- ✅ ivshmem driver code EXISTS (pci_ivshmem.c/h)
- ✅ QEMU wrapper scripts created
- ✅ Python magic validation implemented
- ⚠️ **NEVER END-TO-END TESTED** - project pivoted before validation
- ✅ Pi 4 uses UART IPC instead (no PCI, no shared memory)

**Validation moved to Week 34:** Python↔seL4 IPC will be validated via UART on real Pi 4 hardware.

---

*Week 30 Code Complete: December 2025*
*E2E Testing: SUPERSEDED by UART (January 2026)*
