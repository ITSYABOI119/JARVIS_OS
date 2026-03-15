# JARVIS AI-OS: Phase 2 Implementation Plan

**Version:** 1.0
**Date:** December 2025
**Phase:** Phase 2 - Alpha System (Months 12-24)
**Duration:** 12 months (52 weeks)
**Status:** IN PROGRESS

---

## Overview

This document provides a detailed week-by-week implementation plan for Phase 2. Each week has specific deliverables, estimated effort (calibrated using Phase 1 actual efficiency), and dependencies clearly defined.

**Goal:** Build an alpha JARVIS AI-OS running on Raspberry Pi 4 bare metal with 15+ operational drivers/modules and proven 30-day stability.

**Success Criteria (Updated for Pi 4 Pivot):**
1. Pi 4 bare-metal boot (seL4 + JARVIS rootserver) - ✅ MET
2. Python↔seL4 UART IPC (bidirectional, <10ms RTT) - ✅ MET
3. 15+ drivers/modules operational - ✅ MET (17 done, 4 planned)
4. 30-day stability test passed (0 crashes, <1% errors) - ⏳ Week 49-51
5. Self-audit passed (static analysis + code review) - ⏳ Week 49
6. Performance validated on real hardware - ✅ MET
7. Phase 2 final report + Phase 3 plan - ⏳ Week 52

**Efficiency Calibration (from Phase 1):**
- Phase 1 Planned: 12-16 hours/week
- Phase 1 Actual: 6-8 hours/week
- **Phase 1 Efficiency: 24% under budget**

**Phase 2 Adjusted Estimates:**
- Weekly baseline: 8-12 hours/week (accounting for driver complexity)
- Total Phase 2: ~500 hours estimated (52 weeks × 9.6h/week avg)
- Scaling factor: 1.75× Phase 1 (real hardware, driver framework complexity)

---

## CURRENT STATUS (January 13, 2026)

**Phase 2 Progress:** Week 34 COMPLETE - UART IPC validated + benchmarked

**Update (January 13, 2026):**
- **UART IPC BENCH COMPLETE:** 500-query run via UART
  - Success: 100% (timeouts: 0)
  - Hit rate: 100%
  - RTT median 7.09 ms, p95 8.19 ms, p99 513.82 ms (9 retries)
  - CRC mismatches: 7; invalid-length frames: 2 (handled; no cascading failures)

**Update (January 10, 2026):**
- **UART RX WORKING:** Device frame mapped at vaddr 0x5c0000 via seL4 capabilities
  - TX: seL4_DebugPutChar() kernel syscall
  - RX: Direct MMIO via mapped device frame
  - Key fix: vaddr must be within VSpace range (0x400000-0x5b9fff), not arbitrary address
- JARVIS fully booting on Pi 4 with bidirectional UART capability
- Decision cache loaded (258 patterns, 252 entries)
- IPC handler running, waiting for Python queries
- **Next:** Week 35 - SD/EMMC storage driver (start Week 35)

**Update (January 8, 2026):**
- **UART OUTPUT FIX:** Added PL011 UART initialization to elfloader `platform_init.c` for bcm2711.
- Pi 4 boot reaches seL4 user space; elfloader loads DTB from CPIO and jumps to kernel.
- JARVIS rootserver runs; banner and system info visible on UART.
- Cache loads confirmed (258 patterns).

**IMPORTANT NOTE ON CHECKMARKS:**
Some deliverables marked ✅ below were completed in **simulation/QEMU** or as **design/code artifacts**,
not yet validated on real hardware with actual Python↔seL4 communication. True end-to-end IPC testing
begins in Week 34 now that UART RX is enabled.

### What's Ready to Test

**SD Card Status: OK - JARVIS BOOTABLE + VERIFIED**

Your SD card has the required boot files in `phase2/firmware/`:
- `start4.elf` (2.2 MB) - GPU firmware ✅
- `fixup4.dat` (5.5 KB) - Memory configuration ✅
- `config.txt` (476 bytes) - Boot settings ✅
- `kernel8.img` (~1.6 MB) - **JARVIS seL4 rootserver** OK

**MD5 Checksum:** build-dependent; check `phase2/firmware/kernel8.img`

**Observed on Pi 4 (January 7, 2026):**
1. GPU loads start4.elf → Works ✓
2. GPU reads config.txt → Works ✓
3. GPU loads kernel8.img (JARVIS) → Works ✓
4. GPU starts ARM CPU at kernel entry → Works ✓
5. ELF-loader starts, prints banner → Works ✓
6. seL4 kernel initializes → Works ✓
7. JARVIS rootserver runs `main()` -> Works (OK)
8. UART serial output visible -> Works (banner + IPC handler)

**This IS JARVIS** - the complete JARVIS AI-OS rootserver with:
- Decision cache (258 patterns, 85.7% hit rate)
- PL011 UART driver for serial IPC
- UART IPC protocol (14 message types, CRC-16)
- Ready for Python↔seL4 communication

### What's NOT Ready

**Hardware Test: COMPLETE** (Pi 4 boot verified)

- ✅ JARVIS ARM64 build complete (275KB rootserver)
- ✅ Boot image ready (~1.6 MB kernel8.img)
- ✅ SD card files prepared in `phase2/firmware/`
- Hardware test complete (UART banner + IPC loop)

### Build Completed (January 1, 2026)

**What Was Built:**
- TII seL4 build system integration (sel4test pattern)
- CMakeLists.txt for JARVIS rootserver (93 lines)
- GCC 13 compatibility fixes applied
- JARVIS ARM64 rootserver: 275KB
- Boot image: ~1.6 MB (includes elfloader + kernel + rootserver)

**GCC 13 Fixes Applied:**
1. musllibc weak_alias visibility fix (`src/internal/libc.h`)
2. elfloader array bounds fix (`src/arch-arm/sys_boot.c`)

### Next Steps

**Next Steps (Week 33):**
1. Keep UART RX disabled until a safe receive path is mapped (Week 33 task).
2. Implement safe UART RX mapping or ring buffer for host->Pi.
3. Re-enable UART RX and verify host->Pi queries.
4. Validate UART IPC with Python host (query/response).
5. Remove duplicate IPC header output (optional cleanup).
6. Confirm stable idle with periodic status (no faults).

**Observed Serial Output (January 7, 2026):**
```
========================================
  JARVIS AI-OS v0.2 - Phase 2 Week 32
  ARM64 Raspberry Pi 4 Port
========================================
  seL4 + PL011 UART + Decision Cache
  Build: Jan  7 2026 15:36:25
========================================
```

---

## Phase 2 Architecture: Split Deployment (CRITICAL)

**IMPORTANT:** Phase 2 uses a split architecture due to Pi 4 hardware constraints.

```
┌─────────────────────┐         UART          ┌─────────────────────┐
│   Host PC           │◄───────────────────►│   Raspberry Pi 4     │
│                     │   115200 baud         │                      │
│  Python AI Runtime  │   1-10ms RTT          │  seL4 Microkernel   │
│  ├── Phi-3 Mini 3.8B│                       │  ├── Decision Cache  │
│  ├── TinyLlama 1.1B │                       │  ├── IPC Handler     │
│  ├── SHIELD         │                       │  └── PL011 UART      │
│  └── Multi-Agent    │                       │                      │
└─────────────────────┘                       └─────────────────────┘
```

**Why Split?**
- Pi 4 CPU too slow for Phi-3 Mini (5-30s/inference vs 558ms on PC GPU)
- seL4 is C-only microkernel (no Python runtime)
- Decision cache handles 85% of queries locally (<1ms)
- Remaining 15% forwarded to Host PC via UART (10-20ms RTT)

**This is TEMPORARY** - see "Path to Standalone" section at the end of this document.

---

## Month 12-13: Integration Foundation (Weeks 27-30)

### Focus: Python↔seL4 IPC Integration + Manager Initialization

**Objective:** Integrate Phase 1's separate components (Python shell + seL4 kernel) with real-time bidirectional IPC and initialize all system managers.

---

### Week 27: Bidirectional IPC Design

**Tasks:**
1. Analyze Phase 1 IPC architecture
   - Review `phase1/src/ipc/ring_buffer.{c,h}` (54μs validated)
   - Review `phase1/src/ipc/ipc_client.py` (mmap-based Python client)
   - Identify gaps in current mock IPC design

2. Design bidirectional IPC protocol
   - Define message format: Query (Python→seL4), Response (seL4→Python)
   - Add message types: CACHE_LOOKUP, CACHE_RESPONSE, CACHE_INSERT
   - Design cache coordination protocol

3. Update IPC message structures
   - Extend `ring_message_t` in C
   - Extend `RingMessage` in Python (ctypes struct)
   - Add cache metadata fields (hit/miss, lookup time)

4. Create IPC integration test plan
   - Test cases: cache lookup, cache miss, cache insert
   - Performance validation: <100μs latency maintained
   - Correctness validation: cache hit rate >80%

**Deliverables:**
- ✅ IPC design document (architecture, message format, protocol)
- ✅ Updated message structures (C and Python)
- ✅ Integration test plan (30+ test cases)

**Estimated Effort:** 8-10 hours
**Blockers:** None (design phase, no coding yet)

---

### Week 28: IPC Implementation

**Tasks:**
1. Implement Python→seL4 cache lookup
   - Modify `ipc_client.py` to send CACHE_LOOKUP messages
   - Add cache query serialization (query → hash → message)
   - Implement timeout handling (10s max wait)

2. Implement seL4→Python cache response
   - Modify `phase1/src/sel4/main.c` to handle CACHE_LOOKUP
   - Add decision cache integration (cache_lookup() call)
   - Send CACHE_RESPONSE with hit/miss indicator + bytecode

3. Integrate with Python shell
   - Modify `phase1/src/shell/shell.py` to use real IPC
   - Remove mock mode fallback (require seL4)
   - Update statistics tracking (cache hits via IPC)

4. Test end-to-end IPC
   - Query: "show cpu" → Python → seL4 cache → response → Python → display
   - Measure latency: target <100μs per IPC call
   - Measure cache hit rate: target 85.7% (match Week 19 seL4 QEMU)

**Deliverables:**
- ✅ Bidirectional IPC code written (dual_ring_buffer.c, ipc_handler.c)
- ⏳ Python shell shows cache hits from seL4 (requires Week 34 UART testing)
- ⏳ IPC latency measurement (requires Week 34 hardware testing)

**Estimated Effort:** 10-12 hours
**Blockers:** seL4 shared memory API (mitigate: reuse Week 4 implementation)

**Week 28 Milestone:** IPC code complete, hardware testing deferred to Week 34

---

### Week 29: Manager Initialization Framework

**Tasks:**
1. Create SystemBootstrap class
   - New file: `phase2/src/ai/system_bootstrap.py`
   - Responsibilities: Initialize all managers, coordinate startup
   - Load order: IPC → agents → managers → shell

2. Initialize health monitoring
   - Import `phase1/src/ai/agent_health.py`
   - Create AgentHealthMonitor instance on startup
   - Start heartbeat checks (10-second interval)
   - Validate: `health` command shows real agent statistics

3. Initialize dynamic scaling
   - Import `phase1/src/ai/system_state_manager.py`
   - Create SystemStateManager instance with ACTIVE state
   - Start state monitoring (100ms updates)
   - Validate: `scaling state` command shows current model + memory

4. Test manager interconnections
   - Health monitor tracks state manager
   - State manager triggers scaling transitions
   - All components report to SystemBootstrap

**Deliverables:**
- ✅ SystemBootstrap class created
- ✅ Health monitoring initialized
- ✅ Dynamic scaling initialized
- ✅ Integration tests passing

**Estimated Effort:** 8-10 hours
**Blockers:** None (reusing Phase 1 components)

---

### Week 30: QEMU ivshmem Shared Memory Integration

**Problem Statement:**
Week 28 discovered Python and seL4 use separate memory spaces:
- Python uses `/dev/shm/jarvis_ipc` (host memory)
- seL4 uses static `dual_ring_buffer_t` (QEMU guest memory)
- These are NOT connected - Python↔seL4 IPC doesn't actually work!

**Solution:** QEMU ivshmem device maps host file into guest physical memory.

**Tasks:**
1. Create shared memory setup infrastructure
   - `phase2/src/scripts/create_shm.sh` - Create 567KB shared memory file
   - `phase2/src/scripts/run_jarvis_qemu.sh` - QEMU wrapper with ivshmem device
   - QEMU args: `-device ivshmem-plain,memdev=shm0 -object memory-backend-file,...`

2. Implement ivshmem PCI driver (seL4)
   - `phase1/src/sel4/pci_ivshmem.{c,h}` - PCI device detection + BAR2 mapping
   - Vendor: 0x1AF4, Device: 0x1110
   - Fallback to fixed physical address if PCI not available

3. Integrate ivshmem with main_week28.c
   - Detect ivshmem at startup
   - Map BAR2 as dual_ring_buffer_t pointer
   - Fallback to local buffer if ivshmem unavailable

4. Update Python IPC client
   - Add magic number validation (0x4A415256 = "JARV")
   - Add `wait_for_initialization()` for seL4 sync
   - Add dual ring buffer constants

5. Validate end-to-end IPC
   - Python sends MSG_CACHE_LOOKUP via query ring
   - seL4 performs cache lookup, sends response
   - Python receives response via response ring
   - Target: Cache hit rate >80%

**Deliverables:**
- ✅ Shared memory scripts created (create_shm.sh, run_jarvis_qemu.sh)
- ✅ ivshmem PCI driver written (~360 lines) - code exists, never validated
- ✅ main_week28.c ivshmem support added - code exists, never tested
- ✅ Python IPC client magic validation added - code exists, never tested
- ⏳ End-to-end IPC validated (deferred to Week 34 with UART)

**Note:** ivshmem approach was superseded by UART IPC for Pi 4 hardware. Code exists but was never end-to-end tested.

**Estimated Effort:** 10-14 hours
**Blockers:** seL4 PCI access (mitigate: fallback to fixed address)

**Week 30 Checkpoint:** ivshmem code complete (QEMU simulation). Hardware IPC via UART in Week 33-34.

**Note:** ivshmem approach was for QEMU testing. Real hardware (Pi 4) uses UART IPC instead.
Actual Python↔seL4 communication testing begins Week 34 with UART RX now enabled.

---

## Month 13-14: First Hardware Target (Weeks 31-34)

### Focus: Raspberry Pi 4 Setup + UART IPC

**Objective:** Boot JARVIS on bare metal Raspberry Pi 4 and implement UART-based Python↔seL4 IPC.

**Hardware Pivot:** See `PHASE_2_HARDWARE_PIVOT.md` for full rationale (Intel NUC → Pi 4, $1,065 savings).

---

### Week 31: seL4 Pi 4 Environment Setup

**STATUS: COMPLETE (PC-only prep, hardware not yet arrived)** ✅

**What Was Actually Done (December 26-29, 2025):**

1. ✅ **ARM64 cross-compilation toolchain setup**
   - Installed `aarch64-linux-gnu-gcc 13.3.0` on WSL Ubuntu
   - Verified with version check and basic compilation test
   - Toolchain ready for Pi 4 cross-compilation

2. ✅ **TII seL4 build system setup** (DIFFERENT from original plan)
   - Cloned TII seL4 manifest repository (28 repos, ~53 directories)
   - Synced all dependencies using Google `repo` tool
   - Fixed SSH→HTTPS authentication for GitHub repos
   - Built `vm_minimal` target for Raspberry Pi 4:
     - Command: `make raspberrypi4-64_defconfig && make vm_minimal`
     - Build completed successfully (411 ninja targets)
     - Output: Complete CAmkES VM system (NOT bare-metal JARVIS)

3. ✅ **Pi 4 boot media prepared**
   - Downloaded GPU firmware from official Raspberry Pi repo:
     - `start4.elf` (2.3 MB) - GPU bootloader
     - `fixup4.dat` (5.5 KB) - Memory configuration
   - Created `config.txt` with Pi 4 boot settings:
     - 64-bit mode enabled (`arm_64bit=1`)
     - UART enabled (`enable_uart=1`)
     - Kernel file specified (`kernel=kernel8.img`)
   - **kernel8.img on SD card:** TII `capdl-loader-image-arm-bcm2711` (36 MB)
     - This is a COMPLETE bootable seL4 CAmkES VM system
     - Contains: elfloader + kernel + capdl-loader + VM components + Linux guest
     - **WILL BOOT** and run Linux VM on seL4
     - **NOT JARVIS** - but proves hardware works

4. ⏳ **Hardware not yet arrived**
   - Pi 4 ordered December 2025
   - SD card fully prepared and ready
   - UART serial adapter ready
   - Cannot test boot until hardware arrives

**Deliverables:**
- ✅ Cross-compilation toolchain configured (aarch64-linux-gnu-gcc 13.3.0)
- ✅ seL4 boots on Pi 4 (verified January 7-8, 2026)
- ✅ UART serial console working (serial logs captured)
- ✅ Boot process documented (SD_CARD_SETUP.md)

**Actual Effort:** ~8 hours (Week 31 PC-only prep)
**Blockers Encountered:** Pi 4 hardware not yet arrived (expected delivery TBD)

**Critical Discovery:**
TII `vm_minimal` builds a CAmkES VM hypervisor (runs Linux VMs on seL4), NOT a bare-metal JARVIS rootserver. The kernel.elf we initially copied (1.6 MB) was just the seL4 kernel without any rootserver, which would panic on boot. The correct file is `capdl-loader-image-arm-bcm2711` (36 MB) which is now on the SD card and WILL boot successfully.

**Hardware Status:**
- Raspberry Pi 4 8GB: Ordered, awaiting delivery
- MicroSD card 16GB: Formatted FAT32, firmware + kernel ready
- USB-serial adapter: Ready for UART connection
- USB-C power supply: Ready

---

### Week 32: JARVIS ARM64 Port + seL4 Build Framework

**STATUS: COMPLETE** (January 7, 2026)

**What Was Actually Done:**

1. ✅ **TII seL4 build system integration**
   - Created JARVIS project at `~/sel4-workspace/projects/jarvis-sel4/`
   - Followed sel4test pattern for simple rootserver
   - Created CMakeLists.txt (93 lines)
   - Created settings.cmake + easy-settings.cmake

2. ✅ **JARVIS sources integrated**
   - `src/main.c` → main_arm64.c entry point
   - `src/cache/` → Decision cache (258 patterns)
   - `src/drivers/` → PL011 UART driver
   - `src/ipc/` → Ring buffer IPC
   - `src/ipc_phase2/` → Bidirectional IPC handler

3. ✅ **GCC 13 compatibility fixes**
   - **musllibc weak_alias:** Fixed protected symbol visibility
     ```c
     // ~/sel4-workspace/projects/musllibc/src/internal/libc.h
     #define weak_alias(old, new) \
         extern __typeof(old) new __attribute__((weak, alias(#old), visibility("default")))
     ```
   - **elfloader array bounds:** Fixed GCC 13 warning
     ```c
     // ~/sel4-workspace/tools/seL4/elfloader-tool/src/arch-arm/sys_boot.c
     printf("  paddr=[%p..%p]\n", _text, (void*)((uintptr_t)_end - 1));
     ```

4. ✅ **Build completed successfully**
   - JARVIS rootserver: 275KB ARM64 ELF
   - Boot image: ~1.6 MB (includes elfloader + kernel + rootserver)
   - All ninja targets passed
   - MD5: build-dependent; check `phase2/firmware/kernel8.img`

5. ✅ **SD card files prepared**
   - All 4 boot files in `phase2/firmware/`
   - `copy_to_sd.bat` script updated
   - Ready for hardware test

**Update (January 7, 2026):**
- Pi 4 hardware boot reaches seL4 user space (elfloader + kernel + rootserver all running).
- Rootserver cache initialization confirmed in UART output (50 initial + 258 total patterns).
- UART MMIO fault fixed by switching to libsel4platsupport serial mapping.
- Raw driver image build enabled (ElfloaderImage=binary); `kernel8.img` now raw (~1.6 MB).
- UART banner visible on GPIO14/15; IPC loop running; RX disabled to avoid cap faults.

**Deliverables:**
- ✅ ARM64 JARVIS code integrated (main_arm64.c, uart_pl011.c, decision_cache)
- ✅ seL4 build framework working (TII build system, sel4test pattern)
- ✅ JARVIS built successfully (275KB rootserver)
- ✅ Boot image created (~1.6 MB kernel8.img)
- ✅ SD card files ready in `phase2/firmware/`
- ✅ copy_to_sd.bat script updated
- Hardware test complete (UART banner + IPC loop)

**Actual Effort:** ~8 hours (build + GCC fixes + verification)

**Key Files Created/Modified:**
- `~/sel4-workspace/projects/jarvis-sel4/CMakeLists.txt` (93 lines)
- `~/sel4-workspace/projects/jarvis-sel4/settings.cmake`
- `~/sel4-workspace/projects/jarvis-sel4/easy-settings.cmake`
- `phase2/firmware/kernel8.img` (~1.6 MB JARVIS boot image)
- `phase2/copy_to_sd.bat` (updated for JARVIS)

**JARVIS Symbols Verified:**
- `main` @ 0x4001c0
- `uart_init` @ 0x403200
- `cache_init` @ 0x400d90

**Banner Embedded:**
```
JARVIS AI-OS v0.2 - Phase 2 Week 32
ARM64 Raspberry Pi 4 Port
```

### Week 32 Build Summary

| Component | Size | Status |
|-----------|------|--------|
| jarvis-sel4 (rootserver) | 275 KB | ✅ Built |
| kernel8.img (boot image) | ~1.6 MB | ✅ Created |
| kernel.elf (seL4 kernel) | 172 KB | ✅ Built |
| kernel.dtb (device tree) | 26 KB | ✅ Generated |

**Build Command:**
```bash
cd ~/sel4-workspace/rpi4_jarvis
ninja
```

**Copy to SD Card:**
```cmd
cd C:\Users\jluca\Documents\JARVIS_OS\phase2
copy_to_sd.bat D:
```

---

### Week 33: UART RX Enable via Device Frame Mapping ✅

**STATUS: COMPLETE (January 10, 2026)**

**Problem Solved:**
- seL4 rootserver runs unprivileged, cannot directly access UART MMIO at 0xFE201000
- Initial vaddr 0x0ffff000 failed with seL4_FailedLookup (error 6) - no page tables there
- Solution: Map device frame within existing VSpace range (0x5c0000)

**What Was Done:**

1. ✅ **UART TX via seL4_DebugPutChar()**
   - Kernel syscall works from user space in debug builds
   - Added sel4_puts() helper for string output

2. ✅ **UART RX via Device Frame Mapping**
   - Find device untyped covering 0xFE201000 from bootinfo
   - Retype to ARM SmallPage (4KB)
   - Map at vaddr 0x5c0000 (within rootserver VSpace)
   - Direct MMIO access via uart_mmio_base pointer

3. ✅ **Debug output for troubleshooting**
   - Added debug_puts()/debug_hex() functions
   - Prints bootinfo untypeds, retype status, map results
   - Essential for diagnosing vaddr issue

**Deliverables:**
- ✅ UART TX working (seL4_DebugPutChar)
- ✅ UART RX enabled (device frame mapped at 0x5c0000)
- ✅ Debug output added to uart_pl011.c
- ✅ IPC handler running, waiting for queries
- ⏳ End-to-end Python↔seL4 IPC testing (Week 34)

**Actual Effort:** ~4 hours (3 kernel iterations)

**Week 33 Evidence:** `phase2/weeks/week33/WEEK_33_STATUS.md`, `phase2/weeks/week33/WEEK_33_RESULTS.md`

**Key Learning:**
seL4 ARM64 VSpace only has page tables for the address range where code is loaded.
Mapping at arbitrary addresses requires creating intermediate page tables (PUD/PMD).
Solution: Map device frames within the existing VSpace range.

---

### Week 34: Python↔seL4 IPC Testing

**STATUS: COMPLETE (January 13, 2026)**

**Prerequisites:** Week 33 UART RX enabled ✅

**Update (January 13, 2026):**
- 500-query UART IPC bench: success 100%, hit rate 100%
- RTT median 7.06 ms, p95 8.12 ms (target <25 ms)
- CRC mismatches observed (7) and two invalid-length frames; no cascading failures


**Tasks:**
1. Test UART RX with serial console character input
   - Type characters, verify seL4 receives them
   - Confirm uart_rx_ready() and uart_getc() work

2. Full Python↔seL4 IPC via UART
   - Connect uart_ipc_client.py via USB-UART
   - Python sends CACHE_LOOKUP message (0xAA55 sync + payload + CRC)
   - seL4 performs cache lookup, sends CACHE_RESPONSE
   - Python displays result with timing

3. Validate cache hit rate via IPC
   - Run 100 test queries through UART IPC
   - Measure hit rate (target: >80%)
   - Compare to Phase 1 QEMU results (85.7%)

4. Performance benchmarking
   - UART IPC latency: 10-20ms expected (115200 baud)
   - Cache lookup time: <1ms
   - Round-trip time: <25ms total
   - Document any bottlenecks

**Deliverables:**
- ⏳ UART RX validated with character input
- ⏳ Full Python↔seL4 IPC working via UART
- ⏳ Cache hit rate >80% validated
- ⏳ Performance documented (latency, throughput)

**Estimated Effort:** 8-12 hours
**Blockers:** None (UART RX now enabled)

**Week 34 Checkpoint:** Pi 4 boots JARVIS, UART IPC working, cache >80%

---

## Month 15-16: Driver Framework Expansion (Weeks 35-38)

### Focus: Storage + Network Drivers (Pi 4)

**Objective:** Implement SD/EMMC storage driver and Broadcom GENET network driver for Raspberry Pi 4.

---

### Week 35: SD/EMMC Storage Driver - Part 1

**Tasks:**
1. Study BCM2711 EMMC2 controller
   - Read BCM2711 ARM Peripherals datasheet (EMMC2 section)
   - Focus on: SD card protocol, command structure, data transfer
   - Understand: Controller registers at 0xFE340000

2. Design SD/EMMC driver architecture
   - Controller initialization sequence
   - Command/response handling (CMD0, CMD8, ACMD41, etc.)
   - Data transfer modes: PIO vs DMA
   - Block read/write operations

3. Implement controller initialization
   - Files: `phase2/src/drivers/bcm_emmc.{c,h}`
   - Initialize EMMC2 controller
   - Set clock frequency (400kHz init, 50MHz data)
   - Card detection and identification

4. Implement card initialization
   - Send CMD0 (GO_IDLE)
   - Send CMD8 (SEND_IF_COND)
   - Send ACMD41 (SD_SEND_OP_COND)
   - Read CID/CSD registers

**Deliverables:**
- ✅ BCM2711 EMMC2 spec reviewed
- ✅ Driver architecture designed
- ✅ Controller initialization working
- ✅ SD card detected and identified

**Estimated Effort:** 12-16 hours
**Blockers:** SD protocol complexity (mitigate: use proven initialization sequence)

---

### Week 36: SD/EMMC Storage Driver - Part 2

**Tasks:**
1. Implement block read operations
   - CMD17: READ_SINGLE_BLOCK
   - CMD18: READ_MULTIPLE_BLOCK
   - Buffer management

2. Implement block write operations
   - CMD24: WRITE_BLOCK
   - CMD25: WRITE_MULTIPLE_BLOCK
   - Write verification

3. Integrate with file system layer
   - Block device interface
   - Read/write 512-byte sectors
   - Error handling and retry logic

4. Test SD driver
   - Read boot sector (LBA 0)
   - Write test pattern, read back, verify
   - Measure throughput: target >10 MB/s read
   - Test with different SD cards

**Deliverables:**
- ✅ SD/EMMC driver operational (~800 LOC)
- ✅ Read/write operations working
- ✅ Throughput >10 MB/s validated
- ✅ 10/10 driver tests passing

**Estimated Effort:** 12-16 hours
**Blockers:** None (straightforward block device interface)

**Success Criteria:** SD card read/write working, >10 MB/s throughput ✅

---

### Week 37: Broadcom GENET Network Driver - Part 1

**Tasks:**
1. Study Broadcom GENET specification
   - Read BCM2711 GENET documentation
   - Focus on: TX/RX DMA rings, MAC configuration, PHY interface
   - Understand: Controller registers at 0xFD580000

2. Design GENET driver architecture
   - TX ring buffer: 256 descriptors
   - RX ring buffer: 256 descriptors
   - DMA descriptor format
   - Interrupt handling

3. Implement controller initialization
   - Files: `phase2/src/drivers/bcm_genet.{c,h}`
   - Reset GENET controller
   - Configure MAC address (from OTP or Device Tree)
   - Initialize PHY (RGMII interface)

4. Implement TX ring buffer
   - Allocate TX descriptor ring
   - Implement packet transmission: buffer → DMA descriptor → doorbell
   - Test: Send raw Ethernet frame

**Deliverables:**
- ✅ GENET spec reviewed
- ✅ Driver architecture designed
- ✅ Controller initialization working
- ✅ TX ring buffer operational

**Estimated Effort:** 12-16 hours
**Blockers:** DMA configuration (mitigate: reference Linux driver)

---

### Week 38: Broadcom GENET Network Driver - Part 2

**Tasks:**
1. Implement RX ring buffer
   - Allocate RX descriptor ring
   - Implement packet reception: DMA → buffer → interrupt
   - Test: Receive raw Ethernet frame

2. Implement basic networking stack
   - ARP: Address Resolution Protocol (map IP → MAC)
   - ICMP: Internet Control Message Protocol (ping)
   - IP: Internet Protocol (basic routing)

3. Integrate with shell commands
   - `ping <host>` → ICMP Echo Request → wait for Reply
   - `ifconfig` → display MAC address, IP address, link status
   - `netstat` → display network statistics

4. Test GENET driver
   - Ping gateway (192.168.1.1): verify local network
   - Ping external host (8.8.8.8): verify internet connectivity
   - Measure RTT: target <5ms on local network
   - Test link up/down handling

**Deliverables:**
- ✅ GENET driver operational (~1,200 LOC)
- ✅ Basic networking stack (ARP, ICMP, IP)
- ✅ Network commands working (`ping`, `ifconfig`, `netstat`)
- ✅ 10/10 driver tests passing

**Estimated Effort:** 12-16 hours
**Blockers:** PHY initialization (mitigate: use standard RGMII config)

**Week 38 Checkpoint:** SD/EMMC + Network drivers operational, 4 Pi 4 drivers working

---

## Month 17-18: USB HID + Alpha Prep (Weeks 39-42)

### Focus: USB Input + Alpha Release Infrastructure

**Objective:** Implement USB HID driver for keyboard/mouse input and prepare for alpha tester onboarding.

---

### Week 39: USB HID Driver - Part 1

**Tasks:**
1. Study USB HID specification
   - Read USB HID Class Specification 1.11
   - Focus on: Endpoint descriptors, interrupt transfers, HID reports
   - Understand: Boot protocol (keyboard/mouse), report descriptors

2. Study BCM2711 USB host controller
   - Pi 4 uses DesignWare USB 2.0 (DWC2) controller
   - xHCI for USB 3.0 ports
   - Controller registers and DMA

3. Implement USB enumeration
   - Files: `phase2/src/drivers/usb_hid.{c,h}`
   - Detect USB keyboard/mouse
   - Read device descriptor
   - Configure device (Set Configuration)

4. Implement keyboard input
   - Read HID report (8 bytes: modifier keys + 6 scan codes)
   - Parse scan codes → ASCII characters
   - Test: Keyboard input via UART passthrough

**Deliverables:**
- ✅ USB HID spec reviewed
- ✅ USB enumeration working
- ✅ Keyboard input partially working
- ✅ Basic HID parsing operational

**Estimated Effort:** 12-16 hours
**Blockers:** DWC2 complexity (mitigate: use simplified boot protocol)

---

### Week 40: USB HID Driver - Part 2

**Tasks:**
1. Complete keyboard driver
   - All printable characters working
   - Modifier keys (Shift, Ctrl, Alt)
   - Special keys (Enter, Backspace, arrows)

2. Implement mouse input (optional)
   - Read HID report (4 bytes: buttons + X/Y movement)
   - Parse mouse movement
   - Log events (no GUI yet)

3. Integrate with shell interface
   - Keyboard input → shell prompt
   - Replace UART input with USB keyboard
   - Test: Type commands using physical keyboard

4. Test USB HID driver
   - Keyboard: Type all printable characters
   - Modifier keys: Shift+letter for uppercase
   - Error handling: Device disconnection

**Deliverables:**
- ✅ USB HID driver operational (~600 LOC)
- ✅ Keyboard fully working
- ✅ Shell accepts physical keyboard input
- ✅ 10/10 driver tests passing

**Estimated Effort:** 10-14 hours
**Blockers:** None (keyboard focus, mouse optional)

---

### Week 41: Alpha Release Infrastructure - Part 1

**Tasks:**
1. Create automated installation script
   - File: `phase2/scripts/install_jarvis.sh`
   - Detect hardware (Intel vs AMD, network controller, storage)
   - Install appropriate drivers
   - Configure bootloader (GRUB)
   - Copy AI models to /opt/jarvis/models/

2. Create user documentation
   - File: `phase2/docs/USER_GUIDE.md`
   - Sections:
     - Installation guide (step-by-step with screenshots)
     - Getting started (first boot, basic commands)
     - Command reference (all 27 commands documented)
     - Troubleshooting (common issues + solutions)
   - Length: 50-100 pages (comprehensive)

3. Test installation on Pi 4
   - Raspberry Pi 4: Run install script, verify automated setup
   - Test with different SD cards (Samsung, SanDisk)

4. Create USB installer image
   - Bootable USB with installation script
   - Include: seL4 kernel, AI models, drivers, documentation
   - Size: ~10GB (models are large)

**Deliverables:**
- ✅ Automated installation script working
- ✅ User documentation complete (50+ pages)
- ✅ Installation tested on 2 platforms
- ✅ USB installer image created

**Estimated Effort:** 10-14 hours
**Blockers:** None (primarily documentation and scripting)

---

### Week 42: Alpha Release Infrastructure - Part 2

**Tasks:**
1. Set up feedback collection system
   - Create GitHub repository (public or private)
   - Issue tracker: Bug reports, feature requests
   - Discussion forum: User questions, community support
   - Telemetry (optional): Anonymous usage statistics

2. Recruit alpha testers
   - Target: 30 testers (aim for 20 active)
   - Channels: Reddit (r/linux, r/programming), Hacker News, Twitter
   - Requirements: Technical users, willing to provide feedback
   - Incentive: Early access, acknowledgment in credits

3. Create alpha tester onboarding guide
   - File: `phase2/docs/ALPHA_TESTER_GUIDE.md`
   - Sections:
     - What to expect (alpha software, bugs expected)
     - How to report bugs (template, required info)
     - How to provide feedback (surveys, interviews)
     - Communication channels (Discord, Slack, email)

4. Prepare alpha release package
   - USB installer image (v0.2.0-alpha)
   - User guide (PDF)
   - Alpha tester guide (PDF)
   - Release notes (what's new, known issues)

**Deliverables:**
- ⏳ Feedback system operational (GitHub + forum)
- ⏳ 20-30 alpha testers recruited
- ⏳ Alpha tester guide created
- ⏳ Alpha release package ready

**Estimated Effort:** 8-12 hours
**Blockers:** Tester recruitment (mitigate: start outreach in Week 40)

**Week 42 Checkpoint:** USB HID working, alpha testers recruited

---

## Month 19-20: System Integration + Power Management (Weeks 43-46)

### Focus: GPIO + Device Tree + Boot Optimization

**Objective:** Complete Pi 4 system integration with GPIO, power management, and boot optimization.

---

### Week 43: GPIO Driver + System Integration

**Tasks:**
1. Implement GPIO driver
   - Files: `phase2/src/drivers/bcm_gpio.{c,h}`
   - BCM2711 GPIO controller at 0xFE200000
   - Pin mode configuration (input/output/alt)
   - Read/write pin values

2. Implement I2C driver (optional)
   - BCM2711 BSC controller
   - Support for I2C peripherals (sensors, displays)
   - Basic read/write operations

3. System integration testing
   - All drivers working together
   - UART IPC + SD storage + Network + USB HID
   - Stress test: continuous operation for 24h

4. Document Pi 4 hardware platform
   - Driver compatibility matrix (Pi 4 specific)
   - Known limitations
   - Performance benchmarks

**Deliverables:**
- ✅ GPIO driver operational (~200 LOC)
- ✅ I2C driver operational (optional, ~300 LOC)
- ✅ 24-hour integration test passed
- ✅ Pi 4 platform documentation complete

**Estimated Effort:** 8-12 hours
**Blockers:** None (GPIO is straightforward)

---

### Week 44: Watchdog + Power Management

**Tasks:**
1. Implement BCM2711 watchdog driver
   - Files: `phase2/src/drivers/bcm_watchdog.{c,h}`
   - Hardware watchdog at 0xFE100000
   - Timeout configuration (1-15 seconds)
   - Heartbeat/feed mechanism

2. Implement basic power management
   - CPU frequency scaling (optional)
   - Temperature monitoring (VideoCore mailbox)
   - Thermal throttling awareness

3. Integrate with SuspendManager
   - Reuse Phase 1 `suspend_manager.py`
   - State save to SD card (vs NVMe)
   - Agent state persistence

4. Test power management
   - Watchdog triggers on hang
   - Temperature reported correctly
   - State save/restore cycle works

**Deliverables:**
- ✅ Watchdog driver operational (~150 LOC)
- ✅ Temperature monitoring working
- ✅ SuspendManager integration complete
- ✅ 5/5 power tests passing

**Estimated Effort:** 8-10 hours
**Blockers:** None (Pi 4 watchdog is simple)

---

### Week 45: Device Tree + Boot Optimization - Part 1

**Tasks:**
1. Study Device Tree specification
   - Read Devicetree Specification v0.4
   - Focus on: Node structure, properties, overlays
   - Understand: Pi 4 DTB structure

2. Create JARVIS Device Tree overlay
   - Files: `phase2/src/boot/jarvis.dts`
   - Define JARVIS-specific device nodes
   - Configure reserved memory regions
   - Enable required peripherals

3. Implement DT parsing in seL4
   - Parse DTB blob at boot
   - Extract memory regions
   - Configure drivers from DT properties

4. Test Device Tree integration
   - Boot with custom overlay
   - Verify peripherals configured correctly
   - Compare boot time with/without DT

**Deliverables:**
- ✅ Device Tree spec reviewed
- ✅ JARVIS overlay created (~100 lines)
- ✅ DT parsing operational
- ✅ Boot configuration automated

**Estimated Effort:** 10-14 hours
**Blockers:** DT parsing complexity (mitigate: use libfdt)

---

### Week 46: Device Tree + Boot Optimization - Part 2

**Tasks:**
1. Optimize boot sequence
   - Parallel driver initialization
   - Lazy loading of optional components
   - Reduce kernel initialization time

2. Implement warm reboot
   - Preserve state across reboots
   - Fast resume without full re-initialization
   - Target: <5s warm boot

3. Power state management
   - Idle power reduction
   - CPU core parking
   - Dynamic frequency scaling

4. Validate boot performance
   - Cold boot: target <10s
   - Warm boot: target <5s
   - Measure power consumption at idle

**Deliverables:**
- ✅ Boot time <10s (cold), <5s (warm)
- ✅ Power optimization working
- ✅ Device Tree fully integrated
- ✅ Boot process stable

**Estimated Effort:** 12-16 hours
**Blockers:** None

**Week 46 Checkpoint:** ✅ MET — 17 drivers/modules, boot 1.8s, power management, 89 PASS / 0 FAIL / 3 SKIP

---

## Month 21-22: Additional Drivers + Stability Harness (Weeks 47-50)

### Focus: Complete Driver Suite + Automated Stability Testing

**Objective:** Reach 15+ operational drivers/modules, build automated Python stability harness, begin 30-day stability test, and perform self-audit using open-source tools.

---

### Week 47: SPI Driver + Hardware RNG

**Tasks:**
1. Implement BCM2835 SPI0 driver
   - Files: `phase2/src/drivers/bcm_spi.{c,h}`
   - SPI0 at 0xFE204000 (shares main peripheral untyped, use `uart_device_map_page()`)
   - Clock divider configuration (125 MHz base / divider)
   - Polled mode transfers (FIFO TX/RX)
   - Chip select control (CE0/CE1 via GPIO)
   - Shell command: `spi` (bus scan, loopback test)

2. Implement hardware RNG driver
   - Files: `phase2/src/drivers/bcm_rng.{c,h}`
   - RNG200 at 0xFE104000 (shares main peripheral untyped)
   - Map MMIO page, enable RNG, read entropy words
   - Provide `rng_read(buf, len)` API for SHIELD entropy needs
   - Shell command: `rng` (read random bytes, health check)

3. Implement PWM driver
   - Files: `phase2/src/drivers/bcm_pwm.{c,h}`
   - PWM at 0xFE20C000 (2 channels)
   - Clock source configuration via mailbox (clock ID 6 = PWM)
   - Duty cycle control for fan/LED dimming
   - Shell command: `pwm` (set channel, duty, frequency)

4. Test suite for Week 47 drivers
   - 5+ SPI tests (init, loopback, clock config, transfer, error handling)
   - 3+ RNG tests (init, entropy quality, throughput)
   - 3+ PWM tests (init, duty cycle, frequency)

**Deliverables:**
- ⏳ BCM2835 SPI0 driver operational
- ⏳ Hardware RNG driver operational (critical for security)
- ⏳ PWM driver operational
- ⏳ 14+ drivers/modules operational

**Estimated Effort:** 12-16 hours
**Blockers:** SPI device mapping (mitigate: follows same pattern as I2C, uses shared untyped)

---

### Week 48: DMA Engine + Stability Harness Setup

**Tasks:**
1. Implement BCM2711 DMA engine driver
   - Files: `phase2/src/drivers/bcm_dma.{c,h}`
   - DMA controller at 0xFE007000 (channels 0-14)
   - Control block (CB) chain for scatter-gather transfers
   - Memory-to-memory copy for validation
   - Future: integrate with EMMC ADMA2 and GENET for improved throughput

2. Build automated Python stability harness
   - File: `phase2/src/ai/stability_harness.py`
   - Connects via UART IPC protocol (reuse `uart_ipc_client.py` transport)
   - Test categories:
     - Cache queries (random mix from 258 patterns, verify responses)
     - Shell commands via COMMAND/COMMAND_RESULT (0x07/0x08)
     - Heartbeat monitoring (detect hangs, measure RTT drift)
     - SHIELD checks (verify block rate maintained)
     - Network operations (ping, ARP, ifconfig via commands)
   - Logging: timestamped CSV (command, response, latency, pass/fail)
   - Crash detection: heartbeat timeout → log crash event, attempt protocol reset
   - Configurable: duration, command mix ratio, interval between commands

3. Validate stability harness on Pi 4
   - Run 1-hour smoke test
   - Verify all test categories execute correctly
   - Confirm logging and crash detection work
   - Fix any protocol edge cases discovered

4. Test suite for DMA + harness
   - 4+ DMA tests (init, mem-to-mem copy, channel allocation, error handling)
   - 3+ harness self-tests (protocol connection, command execution, log output)

**Deliverables:**
- ✅ BCM2711 DMA engine driver operational
- ✅ 15+ drivers/modules operational (Phase 2 target met)
- ✅ Automated stability harness ready (`stability_harness.py`)
- ✅ 1-hour smoke test passed (3,562/3,570 PASS, 99.8%, 0 FAIL)

**Estimated Effort:** 14-18 hours
**Blockers:** DMA CB alignment requirements (mitigate: use dma_alloc uncacheable buffers)

**Week 48 Checkpoint:** 15+ drivers operational, stability harness validated

---

### Week 49: 30-Day Stability Test Start + Self-Audit

**Tasks:**
1. Start 30-day automated stability test
   - Hardware: Raspberry Pi 4 8GB running continuously
   - Harness: `stability_harness.py` on host PC via UART
   - Schedule: 24/7 automated (commands every 1-5 seconds)
   - Command mix: 70% cache queries, 15% shell commands, 10% heartbeat, 5% SHIELD
   - Target metrics: 0 crashes, <1% error rate, <20ms RTT P99
   - Daily log rotation with summary statistics

2. Perform self-audit: static analysis
   - Run `cppcheck` on all C driver sources (detect memory issues, UB, leaks)
   - Run `gcc -Wall -Wextra -Werror -Wconversion` (stricter warnings pass)
   - Run `flawfinder` / `rats` for security-focused C analysis
   - Review all `memcpy`/`memmove` calls for bounds correctness
   - Document findings in `phase2/docs/SECURITY_SELF_AUDIT.md`
   
   ## NOTE THIS IS 4 DAYS BEFORE 30 DAY TEST IS DONE, I RAN SNYK AUDIT FIND IN WEEK 49 FOLDER and the SECURITY_SELF_AUDIT.md file

3. Perform self-audit: code review
   - Review all network-facing code paths (GENET RX → net_stack → protocol handlers)
   - Review all user-input paths (UART RX → IPC handler → command dispatch)
   - Review DMA buffer handling (bounds, alignment, cache coherency)
   - Verify all index masking (ring buffers, DMA descriptor indices)
   - Check for integer overflow in length/size calculations

   ##NOTE I AM USING CURSOR AUTO MODE FOR THIS PART WILL HAVE TO DOUBLE CHECK WIHT OPUS AFTER 30 DAY TEST

4. Fix audit findings
   - Triage by severity (Critical → High → Medium)
   - Fix all Critical and High issues immediately
   - Document Medium/Low issues as technical debt for Phase 3

   ##NOTE FIXED BY CURSOR AUOT MODE BUT STILL GOTTA DOUBLE CHECK EVERYTHING WITH OPUS
   
   ## hmm i havnt touched this in a while but i think im actually upto the  week 50 but unsure, the pi has been running the 30 day stability test with
      "python phase2/src/ai/stability_harness.py --port COM5 --duration 43200 --log-dir stability_logs --verbose" command
**Deliverables:**
- ⏳ 30-day stability test running (Day 1-7)
- ⏳ Static analysis completed (cppcheck, flawfinder)
- ⏳ Code review completed (network, input, DMA paths)
- ⏳ SECURITY_SELF_AUDIT.md written
- ⏳ Critical/High issues fixed

**Estimated Effort:** 14-18 hours (audit + monitoring + fixes)
**Blockers:** Stability issues discovered (mitigate: fix critical bugs, restart test if needed)

---

### Week 50: Stability Monitoring + Phase 3 Research

**Tasks:**
1. Monitor 30-day stability test (Day 8-14)
   - Daily log review: crash count, error rate, latency trends
   - Weekly analysis report: patterns, failure modes, memory growth
   - Fix any bugs discovered during testing (hot-patch, rebuild, redeploy)
   - If test restarted due to fix: document reason, reset counter

2. Harden drivers based on stability findings
   - Add defensive checks for any crash-causing patterns
   - Improve error recovery paths (timeout → reset → retry)
   - Validate watchdog is feeding correctly during long tests
   - Ensure thermal monitoring prevents overheating during 24/7 operation

3. Phase 3 research and prototyping
   - Evaluate Phase 3 hardware options (keep options open):
     - **Option A:** Multi-Pi cluster (Pi 4 seL4 + Pi 5 Linux AI) — both boards already owned
     - **Option B:** x86 (requires spare PC — no spare currently available)
     - **Option C:** Pi 5 standalone (pending seL4 BCM2712 support)
   - Prototype on Pi 5 (4GB, active cooling, already owned):
     - Install lightweight Linux, benchmark Llama 3.2 1B inference time
     - Test UART bridge between Pi 4 and Pi 5
     - Evaluate 4GB RAM constraint for AI models
   - Document findings in `phase2/docs/PHASE_3_HARDWARE_RESEARCH.md`

4. Python host-side improvements
   - Enhance `power_manager.py` with stability harness integration
   - Add harness dashboard: live RTT graph, error rate, uptime counter
   - Improve `system_bootstrap.py` with auto-reconnect on UART disconnect

**Deliverables:**
- ⏳ Stability test Day 14+ (no critical issues)
- ⏳ Driver hardening patches applied
- ⏳ PHASE_3_HARDWARE_RESEARCH.md written
- ⏳ Host-side Python improvements deployed

**Estimated Effort:** 12-16 hours (monitoring + research + improvements)
**Blockers:** Stability failures (mitigate: prioritize fixes over new features)

**Week 50 Checkpoint:** Stability test 50%+ complete, self-audit passed, Phase 3 options documented

---

## Month 23-24: Stability Completion + Phase 2 Report (Weeks 51-52)

### Focus: Complete 30-Day Stability Test + Phase 2 Final Report + Phase 3 Planning

**Objective:** Pass 30-day stability test, write comprehensive Phase 2 final report, and prepare Phase 3 planning document.

---

### Week 51: Stability Test Completion + Final Validation

**Tasks:**
1. Complete 30-day stability test (Day 15-30)
   - Continue monitoring: daily log review, crash tracking
   - Final statistics collection:
     - Total uptime hours
     - Total commands executed
     - Crash count (target: 0, acceptable: <3)
     - Error rate (target: <1%)
     - RTT P50/P95/P99 over 30 days
     - Memory usage trend (detect leaks)
   - Document any downtime events with root cause analysis

2. Final integration validation
   - Run full test suite: all 89+ tests must PASS
   - Run extended stress test: 1000+ sequential commands without error
   - Validate all drivers operational after 30-day run (no degradation)
   - Verify boot reliability: 10 consecutive cold reboots without failure

3. Performance benchmarking (final numbers)
   - Boot time: cold and warm (target: <5s cold, <2s warm)
   - IPC latency: cache hit and miss paths
   - EMMC throughput: ADMA2 read/write speeds
   - Network: GENET TX/RX packet rates
   - Document in `phase2/docs/PERFORMANCE_BENCHMARKS.md`

4. Code cleanup and documentation
   - Remove dead code, unused #ifdef branches
   - Ensure all public APIs have header doc comments
   - Update PI4_PLATFORM_GUIDE.md with final driver matrix
   - Archive stability test logs

**Deliverables:**
- ⏳ 30-day stability test PASSED
- ⏳ All 89+ tests passing (final validation)
- ⏳ PERFORMANCE_BENCHMARKS.md written
- ⏳ Codebase cleaned and documented

**Estimated Effort:** 12-16 hours (monitoring + validation + docs)
**Blockers:** Late-breaking stability issues (mitigate: 2 weeks of monitoring already completed)

---

### Week 52: Phase 2 Final Report + Phase 3 Planning

**Tasks:**
1. Write PHASE_2_FINAL_REPORT.md
   - Structure (mirror PHASE_1_FINAL_REPORT.md):
     - Executive Summary (GO/NO-GO recommendation for Phase 3)
     - Phase 2 Objectives vs Achievements
     - Performance Metrics (real hardware validation, all benchmarks)
     - Gate Criteria Status (updated for Pi 4 pivot)
     - Driver Implementation Summary (15+ operational)
     - 30-Day Stability Test Results
     - Self-Audit Findings and Resolutions
     - Architecture Lessons Learned (seL4 on BCM2711)
     - Technical Debt Inventory for Phase 3
     - Phase 3 Recommendations (hardware direction, priorities)
   - Include: boot logs, test output, performance charts, memory maps

2. Write PHASE_3_KICKOFF.md
   - Phase 3 objectives (options kept open):
     - Standalone AI inference (on-device, no host PC dependency)
     - Hardware platform decision (Pi 5 / x86 / multi-Pi)
     - Enhanced driver suite (VideoCore VI, HDMI, audio)
     - Security hardening (formal verification of critical paths)
     - Beta stability (multi-month continuous operation)
   - Preliminary timeline estimate (6-12 months)
   - Hardware budget and procurement plan
   - Migration path from split architecture to standalone

3. Update all project documentation
   - Update CLAUDE.md with Phase 2 completion status
   - Update JARVIS_UNIFIED_PLAN.md with Phase 2 actuals
   - Update PHASE_2_PROGRESS_TRACKER.md (all weeks complete)
   - Archive Phase 2 weekly status docs

4. Final commit and tag
   - Git tag: `v0.2.0-alpha` (Phase 2 complete)
   - Commit: "Phase 2 COMPLETE: Alpha system on Pi 4 (X PASS, 0 FAIL)"
   - Ensure all source, docs, and test artifacts committed

**Deliverables:**
- ⏳ PHASE_2_FINAL_REPORT.md complete (comprehensive)
- ⏳ PHASE_3_KICKOFF.md complete
- ⏳ All documentation updated
- ⏳ Git tag v0.2.0-alpha created
- ⏳ Phase 2 COMPLETE

**Estimated Effort:** 10-14 hours (primarily documentation)
**Blockers:** None (final week, wrap-up tasks)

**Week 52 Checkpoint:** Phase 2 complete, all gate criteria met, Phase 3 planned

---

## Driver Implementation Schedule (Raspberry Pi 4)

**Platform:** Raspberry Pi 4 8GB (BCM2711, Cortex-A72)

### Core Drivers (Required)

| # | Driver | Week | Status | Priority | Notes |
|---|--------|------|--------|----------|-------|
| 1 | PL011 UART | 32-33 | ✅ DONE | Critical | Serial console + IPC (TX+RX) |
| 2 | SD/EMMC (SDHCI) | 35-36 | ✅ DONE | Critical | Boot + storage (ADMA2, 11.9 MB/s) |
| 3 | Broadcom GENET | 37-38 | ✅ DONE | Critical | Gigabit Ethernet (TX+RX, ARP, ICMP) |
| 4 | USB HID (DWC2) | 39-41 | ✅ DONE | Critical | Keyboard input (boot protocol) |
| 5 | GPIO | 43 | ✅ DONE | High | Pin control, LED, pull-up/down |
| 6 | PM Watchdog | 44 | ✅ DONE | High | 10s timeout, system recovery |

### System Drivers

| # | Driver | Week | Status | Priority | Notes |
|---|--------|------|--------|----------|-------|
| 7 | Device Tree (FDT) | 45 | ✅ DONE | High | Embedded DTB parser (~500 LOC) |
| 8 | Thermal (Mailbox) | 44 | ✅ DONE | High | GPU temp, generic property API |
| 9 | I2C (BSC1) | 43 | ✅ DONE | Medium | 100/400 kHz, bus scan |
| 10 | System Timer | 35 | ✅ DONE | Critical | 1 MHz free-running counter |
| 11 | Boot Manager | 46 | ✅ DONE | High | Per-stage timing, lazy init |
| 12 | Warm Reboot | 46 | ✅ DONE | Medium | SD persistence, warm/cold detect |
| 13 | Power Management | 46 | ✅ DONE | High | WFI idle, ARM clock scaling |
| 14 | Network Stack | 38 | ✅ DONE | Critical | ARP, ICMP, IP (net_stack.c) |

### Infrastructure Modules

| # | Module | Week | Status | Notes |
|---|--------|------|--------|-------|
| 15 | Slot Allocator | 35 | ✅ DONE | seL4 CNode slot management |
| 16 | DMA Allocator | 35 | ✅ DONE | Uncacheable DMA buffer pool |
| 17 | Block Device | 36 | ✅ DONE | Sector read/write abstraction |

### Week 47-48 Drivers (Planned)

| # | Driver | Week | Status | Priority | Notes |
|---|--------|------|--------|----------|-------|
| 18 | SPI (BCM2835) | 47 | ⏳ Planned | Medium | SPI0 polled transfers |
| 19 | Hardware RNG | 47 | ⏳ Planned | High | RNG200, entropy for SHIELD |
| 20 | PWM | 47 | ⏳ Planned | Medium | 2-channel, fan/LED control |
| 21 | DMA Engine | 48 | ⏳ Planned | Medium | BCM2711 DMA, mem-to-mem |

### Deferred to Phase 3

| Driver | Priority | Notes |
|--------|----------|-------|
| VideoCore VI | Medium | GPU framebuffer (requires VC firmware interface) |
| HDMI | Medium | Display output (depends on VideoCore) |
| I2S | Low | Digital audio |
| PCIe (Pi 5) | Low | Only if Phase 3 targets BCM2712 |

### Phase 2 Driver Count

| Category | Count | Status |
|----------|-------|--------|
| Core drivers (UART, EMMC, GENET, USB, GPIO, Watchdog) | 6 | ✅ All DONE |
| System drivers (FDT, Thermal, I2C, Timer, Boot, Reboot, Power, NetStack) | 8 | ✅ All DONE |
| Infrastructure (Slot, DMA, BlkDev) | 3 | ✅ All DONE |
| Week 47-48 drivers (SPI, RNG, PWM, DMA Engine) | 4 | ⏳ Planned |
| **Total Phase 2** | **21** | **17 DONE + 4 planned** |

**Phase 2 Target: 15+ operational drivers/modules — currently at 17, targeting 21 by Week 48**

---

## Testing Requirements per Milestone

### Every 4 Weeks (Gate Criteria Checkpoints)

**Week 30 Checkpoint:** ✅ MET
- ✅ IPC integration working (Phase 1 validated, dual ring buffer)
- ✅ Decision cache operational (258 patterns, 85.7% hit rate)
- **Test Suite:** 57 tests (22 UART IPC + 25 bootstrap + 10 integration)

**Week 34 Checkpoint:** ✅ MET
- ✅ Raspberry Pi 4 boots JARVIS via U-Boot
- ✅ UART IPC working (Python↔seL4, 7ms RTT, 500-query bench 100%)
- ✅ Cache hit rate >80% validated on hardware
- **Test Suite:** 57 Python tests + 30 C tests

**Week 38 Checkpoint:** ✅ MET
- ✅ SD/EMMC driver operational (ADMA2, 11.9 MB/s, 12 tests)
- ✅ GENET Ethernet operational (TX+RX, ARP, ICMP)
- ✅ Network stack: ping, ifconfig, netstat working
- **Test Suite:** 12 EMMC + 6 GENET + 5 RX/net + 10 shell tests

**Week 42 Checkpoint:** ✅ MET
- ✅ USB HID keyboard operational (DWC2 host, boot protocol)
- ✅ Interactive shell via USB keyboard
- ✅ Alpha infrastructure ready (installer, docs, SD imaging)
- **Test Suite:** 6 USB + 10 keyboard + install scripts tested

**Week 46 Checkpoint:** ✅ MET (Hardware Verified Feb 9, 2026)
- ✅ GPIO + I2C + Device Tree integrated
- ✅ Boot optimized (1.8s total, <2s warm)
- ✅ Power management (WFI idle, ARM clock scaling)
- ✅ 17 drivers/modules operational
- **Test Suite:** 89 PASS, 0 FAIL, 3 SKIP (cumulative)

**Week 48 Checkpoint:** ⏳ PLANNED
- ⏳ SPI + RNG + PWM + DMA drivers operational
- ⏳ 15+ drivers target met (21 planned)
- ⏳ Automated stability harness validated
- **Test Suite:** Target 100+ tests total

**Week 50 Checkpoint:** ⏳ PLANNED
- ⏳ 30-day stability test 50%+ complete
- ⏳ Self-audit completed (static analysis + code review)
- ⏳ Critical/High security issues resolved
- ⏳ Phase 3 hardware research documented

**Week 52 Checkpoint:** ⏳ PLANNED
- ⏳ 30-day stability test PASSED (0 crashes target)
- ⏳ Phase 2 final report complete
- ⏳ Phase 3 planning document ready
- ⏳ Git tag v0.2.0-alpha
- **Test Suite:** Final validation (all tests passing, stability proven)

---

## Risk Register (Updated for Pi 4 Reality)

### Technical Risks

| Risk | Probability | Impact | Status | Mitigation |
|------|-------------|--------|--------|------------|
| **Real-time IPC integration fails** | Low | Critical | ✅ RESOLVED | UART IPC working: 7ms RTT, 500-query 100% success |
| **Driver complexity underestimated** | Low | High | ✅ MITIGATED | 17 drivers done; BCM2711 well-documented; Linux driver references |
| **Hardware compatibility issues** | Low | Low | ✅ RESOLVED | Single Pi 4 target, all peripherals mapped and tested |
| **Cache capacity overflow** | Low | Medium | ✅ RESOLVED | 258 patterns at 85.7% hit rate, stable |
| **Stability on real hardware** | Medium | High | ⏳ IN PROGRESS | 30-day automated test planned (Week 49-51); Python harness |
| **seL4 untyped watermark bugs** | Low | Critical | ✅ RESOLVED | Forward-only mapping, buddy skip, shared untyped tracking |
| **DMA cache coherency** | Low | High | ✅ RESOLVED | All DMA buffers mapped uncacheable (vm_attributes=0) |
| **Security vulnerabilities** | Medium | High | ⏳ PLANNED | Self-audit (Week 49): cppcheck, flawfinder, manual code review |
| **Network-facing code exploits** | Medium | High | ⏳ PLANNED | Code review of GENET RX → net_stack paths (Week 49) |

### Non-Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Hardware acquisition delays** | Low | Low | Pi 4 already owned and operational |
| **Timeline slips (solo dev)** | Medium | Medium | 6 weeks remaining; scope flexible (defer PWM/DMA if needed) |
| **Scope creep** | Low | Medium | Clear gate criteria; focus on stability over new features |
| **Phase 3 hardware decision** | Low | Low | Keeping options open; research in Week 50, decide in Phase 3 |

---

## Total Phase 2 Effort Estimate

**Based on Phase 1 Efficiency (24% under budget):**

**Week-by-Week Breakdown (Raspberry Pi 4):**
- Weeks 27-30 (IPC integration): 38-48 hours (4 weeks × 9.5-12h/week)
- Weeks 31-34 (Pi 4 setup + UART IPC): 42-58 hours (4 weeks × 10.5-14.5h/week)
- Weeks 35-38 (SD/EMMC + GENET drivers): 48-64 hours (4 weeks × 12-16h/week)
- Weeks 39-42 (USB HID + alpha prep): 42-56 hours (4 weeks × 10.5-14h/week)
- Weeks 43-46 (GPIO + Device Tree + Power): 38-52 hours (4 weeks × 9.5-13h/week)
- Weeks 47-48 (SPI/RNG/PWM/DMA + stability harness): 26-34 hours (2 weeks × 13-17h/week)
- Weeks 49-50 (Stability test + self-audit + research): 26-34 hours (2 weeks × 13-17h/week)
- Weeks 51-52 (Stability completion + report): 22-30 hours (2 weeks × 11-15h/week)

**Total Estimated: 282-376 hours**

**Conservative Estimate: ~380 hours** (simpler Pi 4 target, no external audit coordination)

**Comparison to Phase 1:**
- Phase 1 actual: 286 hours (26 weeks)
- Phase 2 estimate: 380 hours (52 weeks)
- Scaling factor: 1.33× (single Pi 4 target, reuse of Phase 1 patterns)

**Hardware Cost:**
- Original plan (x86): $5,000 (Intel NUC + Framework + Dell)
- Actual (Pi 4): $215.82 AUD
- Savings: ~$4,800+ (96%)

**Confidence Level: High** (based on Phase 1 efficiency, proven seL4 Pi 4 support)

---

## Path Back to Standalone Operation (Phase 3+)

The Phase 2 split architecture is **temporary**. The original vision and end goal remains a fully self-contained system where AI runs on the same hardware as the microkernel.

**Why the split exists:** No spare PC available for bare-metal OS testing (risk of bricking primary development machine). Pi 4 provides safe bare-metal target; host PC runs AI in userspace (no risk).

### Phase 3 Options (2027+)

**Option A: Multi-Pi Cluster (Lowest Risk — Hardware Already Owned)**
- Pi 4 (8GB): seL4 microkernel (bare metal) — already running JARVIS
- Pi 5 (4GB, active cooling): Linux + TinyLlama/Llama 3.2 1B (AI inference)
- UART or USB connection between boards
- Cost: $0 additional (both boards already owned)
- Benefit: Keeps ARM64, incremental upgrade, no risk to main PC
- Constraint: Pi 5 has 4GB RAM — limits model size (1B-3B parameter models)

**Option B: x86 Return (Original Vision — Requires Spare PC)**
- Dedicated x86-64 machine (NUC, mini-PC, or second-hand desktop)
- All AI runs on-device (Phi-3 Mini 3.8B + larger models)
- ivshmem shared memory IPC (<100μs, not UART)
- Cost: ~$500-1,200 (used mini-PC or NUC)
- Benefit: Full performance, original architecture, 16-32GB RAM for larger models
- Blocker: Need a spare machine that can be safely wiped

**Option C: Pi 5 Standalone (Pending seL4 Support)**
- seL4 running directly on Pi 5 (BCM2712)
- AI inference on-device (4GB constraint)
- Requires seL4 BCM2712 port (4-8 weeks, see `SEL4_PI5_PORTING_RESEARCH.md`)
- Cost: $0 additional
- Risk: seL4 doesn't support BCM2712 yet; RP1 south bridge adds complexity

### Hardware Already Available
| Hardware | RAM | Status | Role |
|----------|-----|--------|------|
| Raspberry Pi 4 Model B | 8GB | ✅ Running JARVIS | seL4 microkernel |
| Raspberry Pi 5 | 4GB | ✅ Owned (active cooling) | Available for Phase 3 AI |
| Host PC | — | ✅ Development machine | Python AI (current), builds |

### Why This Matters
The split architecture adds latency (7ms UART vs 54μs shared memory) and requires a host PC. Phase 3+ should eliminate this dependency for:
- **Portable deployment** (self-contained embedded system)
- **Air-gapped operation** (no network/host required)
- **Simpler setup** (one device instead of two + cable)

### Migration Path
1. **Phase 2 (Now):** Validate architecture on Pi 4 + Host PC
2. **Phase 3 Step 1:** Prototype AI on Pi 5 Linux (test inference speed, 4GB limit)
3. **Phase 3 Step 2:** Bridge Pi 4 ↔ Pi 5 (UART or USB IPC)
4. **Phase 3 Step 3:** Evaluate x86 if Pi 5 RAM proves insufficient
5. **Phase 4:** Production standalone deployment (final platform TBD)

---

## Appendix A: Phase 1 Code Dependencies

**Critical files to reuse in Phase 2:**

**Decision Cache:**
- `phase1/src/cache/decision_cache.{c,h}` (512 entries, FNV-1a hash)
- `phase1/src/cache/cache_patterns.c` (258 patterns)
- Action: Expand to 1024 entries, add driver patterns

**Lock-Free IPC:**
- `phase1/src/ipc/ring_buffer.{c,h}` (SPSC, 1024 slots, 54μs)
- `phase1/src/ipc/ipc_sel4.{c,h}` (seL4 integration)
- Action: Add bidirectional Python↔seL4 communication

**AI Agents:**
- `phase1/src/ai/agent.py` (Phi-3 Mini wrapper, 558ms)
- `phase1/src/ai/agent_router.py` (4 specialist agents, 100% routing)
- `phase1/src/ai/device_agent.py`, `network_agent.py`, `filesystem_agent.py`, `user_agent.py`
- Action: Initialize all agents on startup

**SHIELD:**
- `phase1/src/ai/shield_framework.py` (100 action types, 100% block)
- `phase1/src/ai/shield_action_db.py` (action database)
- Action: Refine risky scenario accuracy (46.7% → >90%)

**Managers:**
- `phase1/src/ai/system_state_manager.py` (4 states, dynamic scaling)
- `phase1/src/ai/suspend_manager.py` (state persistence, 22/22 tests)
- `phase1/src/ai/agent_health.py` (health monitoring, failover)
- Action: Initialize on startup via SystemBootstrap

**Drivers:**
- `phase1/src/drivers/virtio_core.{c,h}` (ring buffer pattern)
- `phase1/src/drivers/virtio_blk.{c,h}` (block storage pattern)
- Action: Reuse ring buffer pattern for SD/EMMC, GENET, USB HID

**Shell:**
- `phase1/src/shell/shell.py` (27 commands, 43/43 tests)
- Action: Integrate with real-time IPC, test on real hardware

---

## Appendix B: Hardware Configuration (Raspberry Pi 4)

**Hardware Ordered (December 2025):**
- Raspberry Pi 4 Model B 8GB - $145.00 AUD
- Official USB-C Power Supply 5.1V 15.3W - $11.90 AUD
- MicroSD 16GB Class 10 - $17.25 AUD
- Large Heatsink - $2.40 AUD
- USB to TTL Serial Cable (Adafruit) - $20.75 AUD
- Shipping - $18.52 AUD
- GST - $19.62 AUD

**Total Hardware Cost: $215.82 AUD** (vs $5,000+ USD original x86 plan)
**Savings: ~$4,800+ (96%)**

**Also Available (Already Owned):**
- Raspberry Pi 5 4GB (with active cooling fan + case) — for Phase 3 AI prototyping
- Larger SD card 64GB+ (~$30 AUD, if needed)

### Pi 4 Specifications (BCM2711)

| Component | Specification |
|-----------|---------------|
| CPU | Cortex-A72 quad-core @ 1.8 GHz |
| RAM | 8GB LPDDR4-3200 |
| Storage | MicroSD (SDIO) |
| Network | Broadcom GENET Gigabit |
| USB | 2× USB 3.0, 2× USB 2.0 |
| UART | PL011 @ 0xFE201000 |
| GPIO | 40-pin header |
| Power | 5V/3A USB-C |

---

## Appendix C: Automated Stability Test Plan

**Approach: Python-based automated harness (no external testers — private project)**

**Infrastructure:**
- Host PC running `stability_harness.py` connected via UART to Pi 4
- Pi 4 running JARVIS seL4 continuously (24/7 for 30 days)
- Timestamped CSV logging on host PC
- Crash detection via heartbeat timeout

**Test Categories:**
| Category | Mix % | Description |
|----------|-------|-------------|
| Cache queries | 70% | Random selection from 258 patterns, verify response |
| Shell commands | 15% | ping, ifconfig, temp, boot, dt, gpio, i2c, usb |
| Heartbeat | 10% | RTT measurement, hang detection |
| SHIELD checks | 5% | Risk assessment requests, verify block rate |

**Success Criteria:**
- 0 crashes over 30 days (acceptable: <3 with root cause analysis)
- <1% error rate across all command categories
- RTT P99 < 20ms (no latency degradation over time)
- No memory leaks (stable memory usage trend)
- Watchdog never triggers (all heartbeats fed)

**Timeline:**
- Week 48: Harness built and smoke-tested (1-hour run)
- Week 49: 30-day test starts (Day 1)
- Week 50: Midpoint check (Day 14), fix any issues
- Week 51: Test completion (Day 30), final statistics

---

**Phase 2 Implementation Plan Complete**

---

*Implementation Plan Date: December 2025*
*Updated: February 10, 2026 (Week 46 HARDWARE VERIFIED — Weeks 47-52 rewritten for Pi 4)*
*Author: JARVIS Development Team (Solo Developer)*
*Phase 1 Complete: 26/26 weeks (100%), 286 hours*
*Phase 2 Estimate: 52 weeks, ~380 hours*
*Hardware: Raspberry Pi 4 8GB (BCM2711, Cortex-A72)*
*Hardware Cost: $215.82 AUD (savings: ~$4,800+ vs original x86 plan)*
*Current Status: Week 46 HARDWARE VERIFIED — 89 PASS, 0 FAIL, 3 SKIP*
*Next Milestone: Week 47 — SPI + Hardware RNG + PWM drivers*

**See Also:**
- `PHASE_2_HARDWARE_PIVOT.md` — Full pivot rationale (NUC → Pi 4)
- `SEL4_PI5_PORTING_RESEARCH.md` — Pi 5 porting research for Phase 3+
- `PI4_PLATFORM_GUIDE.md` — Driver matrix, memory maps, benchmarks
