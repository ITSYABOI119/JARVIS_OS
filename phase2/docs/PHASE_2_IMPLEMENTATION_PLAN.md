# JARVIS AI-OS: Phase 2 Implementation Plan

**Version:** 1.0
**Date:** December 2025
**Phase:** Phase 2 - Alpha System (Months 12-24)
**Duration:** 12 months (52 weeks)
**Status:** IN PROGRESS

---

## Overview

This document provides a detailed week-by-week implementation plan for Phase 2. Each week has specific deliverables, estimated effort (calibrated using Phase 1 actual efficiency), and dependencies clearly defined.

**Goal:** Build an alpha JARVIS AI-OS running on real hardware with 15+ operational Tier 1 drivers and 20 active alpha testers.

**Success Criteria:** 7/7 Phase 2 gate criteria met (Pi 4 bare-metal boot, Python↔seL4 UART IPC, 15+ drivers, 30-day stability, alpha release, security audit, performance on real hardware).

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

**Week 46 Checkpoint:** Pi 4 fully integrated, 6 drivers working, boot optimized

---

## Month 21-22: Alpha Testing + Security Audit (Weeks 47-50)

### Focus: User Onboarding + External Security Review

**Objective:** Onboard 20 alpha testers and pass external security audit.

---

### Week 47: Alpha Tester Onboarding

**Tasks:**
1. Distribute JARVIS to alpha testers
   - Send USB installer images (or download links)
   - Provide installation support (email, Discord)
   - Track installation success rate (target: >80%)

2. Collect initial feedback
   - Survey: First impressions, installation experience
   - Bug reports: Critical issues (P0), high-priority issues (P1)
   - Feature requests: What's missing, what's confusing

3. Fix critical bugs (P0 issues)
   - Triage bug reports by priority
   - Fix crashes, data corruption, boot failures
   - Release patch: v0.2.1-alpha (bug fixes)

4. Monitor alpha tester activity
   - Track: Daily active users (target: 15+/20)
   - Track: Commands executed per user
   - Track: Crash reports, error logs

**Deliverables:**
- ⏳ 20 testers using JARVIS
- ⏳ Initial feedback collected (50+ bug reports expected)
- ⏳ P0 bugs resolved (5-10 critical issues)
- ⏳ Patch released (v0.2.1-alpha)

**Estimated Effort:** 16-20 hours (bug fixing and support)
**Blockers:** Alpha tester availability (mitigate: recruit 30, aim for 20 active)

---

### Week 48: VESA Framebuffer Driver (Tier 1 #10)

**Tasks:**
1. Study VESA BIOS Extensions (VBE)
   - Read VBE 3.0 specification
   - Focus on: Framebuffer mode, linear memory access
   - Understand: Mode setting, resolution/depth configuration

2. Implement VESA driver
   - Files: `phase2/src/drivers/vesa_fb.{c,h}`
   - Query available modes (VBE function 0x4F01)
   - Set graphics mode (VBE function 0x4F02)
   - Map framebuffer to memory

3. Implement basic graphics primitives
   - Draw pixel (x, y, color)
   - Fill rectangle (for future GUI)
   - Draw text (for console output)

4. Test VESA driver
   - Set mode: 1920×1080, 32-bit color
   - Draw test pattern (gradient, checkerboard)
   - Validate: Framebuffer accessible, no corruption

**Deliverables:**
- ✅ VESA framebuffer driver operational
- ✅ Basic graphics primitives working
- ✅ 10/20 Tier 1 drivers working (50%)

**Estimated Effort:** 10-14 hours
**Blockers:** VBE compatibility (mitigate: fallback to 1024×768 if needed)

---

### Week 49: External Security Audit - Part 1

**Tasks:**
1. Hire 3rd-party security firm
   - Research firms: Trail of Bits, NCC Group, Cure53
   - Request proposals (RFP): scope, timeline, cost
   - Budget: $75K (realistic for comprehensive audit)

2. Provide audit scope
   - Components: seL4 kernel, IPC layer, SHIELD framework, drivers
   - Focus areas: Memory safety, privilege escalation, input validation
   - Exclusions: AI model vulnerabilities (out of scope)

3. Prepare codebase for audit
   - Code cleanup (remove debug code, unused functions)
   - Documentation (architecture diagrams, threat model)
   - Test coverage report (demonstrate testing rigor)

4. Audit kickoff
   - Initial meeting with auditors
   - Provide access (GitHub repo, documentation)
   - Establish communication channels (Slack, email)

**Deliverables:**
- ✅ Security firm hired
- ✅ Audit scope defined
- ✅ Codebase prepared
- ✅ Audit started

**Estimated Effort:** 10-14 hours (coordination and prep)
**Blockers:** Budget constraint (mitigate: use open-source tools if needed)

---

### Week 50: External Security Audit - Part 2

**Tasks:**
1. Support audit activities
   - Answer auditor questions
   - Provide additional documentation as requested
   - Reproduce reported issues

2. Review audit findings
   - Receive preliminary report (mid-audit)
   - Triage findings by severity (Critical, High, Medium, Low)
   - Prioritize fixes: Critical + High first

3. Fix critical vulnerabilities
   - Address memory safety issues (buffer overflows, use-after-free)
   - Fix privilege escalation bugs
   - Patch input validation flaws

4. Prepare for re-audit
   - Document all fixes (commit messages, pull requests)
   - Request re-audit of critical findings
   - Target: No critical vulnerabilities remaining

**Deliverables:**
- ⏳ Security audit report received
- ⏳ Critical vulnerabilities fixed (target: 0 critical)
- ⏳ Audit passed (no critical issues remaining)

**Estimated Effort:** 20-30 hours (fixing vulnerabilities)
**Blockers:** Audit findings severity (mitigate: thorough testing in Phase 1 reduces risk)

**Week 50 Checkpoint:** Security audit passed, 30-day stability test started

---

## Month 23-24: Stability + Phase 2 Completion (Weeks 51-52)

### Focus: 30-Day Stability Test + Phase 2 Final Report

**Objective:** Validate production-grade stability and complete Phase 2 documentation.

---

### Week 51: 30-Day Stability Test

**Tasks:**
1. Set up 30-day stability test infrastructure
   - Hardware: Raspberry Pi 4 8GB (primary)
   - Monitoring: Crash logs, error logs, memory leaks
   - Automation: Script to execute commands 24/7

2. Start 30-day stability test
   - Target: 0 crashes, <1% error rate
   - Commands: Random mix (80% safe, 15% validated, 5% blocked)
   - Logging: Every command executed, every error encountered

3. Monitor test progress
   - Daily checks: Crash count, error rate, memory growth
   - Weekly analysis: Identify patterns, failure modes
   - Fix issues: Hot-patches for critical bugs

4. Implement Intel i915 graphics driver (Tier 1 #11)
   - Files: `phase2/src/drivers/i915_core.{c,h}`, `i915_driver.{c,h}`
   - Intel-specific GPU driver (more complex than VESA)
   - Accelerated graphics (vs VESA framebuffer)
   - Test: Boot with i915, validate graphics output

**Deliverables:**
- ✅ 30-day stability test started
- ✅ Monitoring infrastructure operational
- ✅ Intel i915 driver operational (bonus)
- ✅ 11/20 Tier 1 drivers working (55%)

**Estimated Effort:** 12-16 hours (mostly monitoring, some driver work)
**Blockers:** Stability issues (mitigate: extensive Phase 1 testing reduces risk)

---

### Week 52: Phase 2 Final Report

**Tasks:**
1. Complete 30-day stability test
   - Collect final statistics: Crash count, error rate, commands executed
   - Validate: 0 crashes (or <3 crashes), <1% error rate
   - Document: Failure analysis for any crashes

2. Write PHASE_2_FINAL_REPORT.md
   - Structure (mirror PHASE_1_FINAL_REPORT.md):
     - Executive Summary (GO/NO-GO for Phase 3)
     - Phase 2 Objectives vs Achievements
     - Performance Metrics (real hardware validation)
     - Gate Criteria Status (7/7 MET)
     - Driver Implementation (15/20 operational = 75%)
     - Lessons Learned
     - Technical Debt for Phase 3
     - Phase 3 Recommendations
   - Length: 15,000+ lines (comprehensive)

3. Collect alpha tester feedback
   - Survey: Overall satisfaction (target: >70%)
   - Interviews: 5-10 detailed feedback sessions
   - Metrics: Daily active users, feature usage, bug reports

4. Prepare Phase 3 planning
   - Create `PHASE_3_KICKOFF.md` (preview)
   - Define Phase 3 objectives (beta system, 10+ hardware configs)
   - Estimate Phase 3 timeline (12-18 months)

**Deliverables:**
- ⏳ 30-day stability test passed (0 crashes target)
- ⏳ PHASE_2_FINAL_REPORT.md complete (~15,000 lines)
- ⏳ Alpha tester satisfaction >70%
- ⏳ Phase 3 kickoff ready

**Estimated Effort:** 10-14 hours (primarily documentation)
**Blockers:** None (final week, wrap-up tasks)

**Week 52 Checkpoint:** Phase 2 complete, all 7 gate criteria met

---

## Driver Implementation Schedule (Raspberry Pi 4)

**Platform:** Raspberry Pi 4 8GB (BCM2711, Cortex-A72)

### Core Drivers (Required)

| Driver | Week | Status | Priority | Notes |
|--------|------|--------|----------|-------|
| 1. PL011 UART | 32-33 | ✅ DONE | Critical | Serial console + IPC (TX+RX working) |
| 2. SD/EMMC | 35-36 | ⏳ Planned | Critical | Boot + primary storage |
| 3. Broadcom GENET | 37-38 | ⏳ Planned | Critical | Gigabit Ethernet |
| 4. USB HID | 39-40 | ⏳ Planned | Critical | Keyboard/mouse input |
| 5. GPIO | 43 | ⏳ Planned | High | General I/O, LED status |
| 6. Watchdog | 44 | ⏳ Planned | High | System recovery |

### System Drivers

| Driver | Week | Status | Priority | Notes |
|--------|------|--------|----------|-------|
| 7. Device Tree | 45-46 | ⏳ Planned | High | Hardware description |
| 8. Temperature | 44 | ⏳ Planned | Medium | Thermal monitoring |
| 9. I2C (BSC) | 43 | ⏳ Optional | Medium | Peripheral support |
| 10. SPI | Phase 3 | ⏳ Deferred | Low | Peripheral support |

### Graphics (Future)

| Driver | Week | Status | Priority | Notes |
|--------|------|--------|----------|-------|
| 11. VideoCore VI | Phase 3 | ⏳ Deferred | Medium | GPU framebuffer |
| 12. HDMI | Phase 3 | ⏳ Deferred | Medium | Display output |

### Audio (Future)

| Driver | Week | Status | Priority | Notes |
|--------|------|--------|----------|-------|
| 13. PWM Audio | Phase 3 | ⏳ Deferred | Low | Basic audio output |
| 14. I2S | Phase 3 | ⏳ Deferred | Low | Digital audio |

### Comparison: x86 vs ARM64

| x86 Driver (Removed) | ARM64 Replacement | Notes |
|---------------------|-------------------|-------|
| NVMe | SD/EMMC | No PCIe on Pi 4 |
| Intel e1000e | Broadcom GENET | Pi 4 built-in NIC |
| Intel i915 | VideoCore VI | Pi 4 GPU |
| ACPI S3 | Device Tree + Watchdog | ARM power model |
| PS/2 | USB HID only | No legacy ports |
| PCI/PCIe | N/A | No PCI bus on Pi 4 |

**Phase 2 Target: 8/10 core drivers (80%)**

**Drivers Planned for Phase 2:**
1. ✅ PL011 UART (Week 32-33) - TX via seL4_DebugPutChar, RX via device frame mapping
2. ⏳ SD/EMMC (Weeks 35-36)
3. ⏳ Broadcom GENET (Weeks 37-38)
4. ⏳ USB HID (Weeks 39-40)
5. ⏳ GPIO (Week 43)
6. ⏳ Watchdog (Week 44)
7. ⏳ Device Tree (Weeks 45-46)
8. ⏳ Temperature (Week 44)
9. ⏳ I2C (Week 43, optional)

**Drivers Deferred to Phase 3:**
- VideoCore VI, HDMI, PWM Audio, I2S, SPI

---

## Testing Requirements per Milestone

### Every 4 Weeks (Gate Criteria Checkpoints)

**Week 30 Checkpoint:**
- ✅ IPC integration working
- ✅ Python shell cache >80% (via seL4)
- ✅ All managers initialized
- **Test Suite:** 30+ IPC integration tests, 10+ manager tests

**Week 34 Checkpoint:**
- ✅ Raspberry Pi 4 boots JARVIS
- ✅ UART IPC working (Python↔seL4)
- ✅ Cache hit rate >80% validated
- **Test Suite:** 20+ UART IPC tests, 10+ cache tests

**Week 38 Checkpoint:**
- ✅ SD/EMMC + GENET drivers operational
- ✅ 4/8 core drivers working (50%)
- ✅ Network ping working on Pi 4
- **Test Suite:** 15+ storage tests, 15+ network tests

**Week 42 Checkpoint:**
- ✅ USB HID driver working
- ✅ Physical keyboard input functional
- ✅ Alpha testers recruited (20-30 people)
- **Test Suite:** 20+ USB HID tests, alpha tester onboarding

**Week 46 Checkpoint:**
- ✅ GPIO + Device Tree integrated
- ✅ Boot optimized (<10s cold, <5s warm)
- ✅ 8/8 core drivers working (100%)
- **Test Suite:** 25+ integration tests, 10+ boot tests

**Week 50 Checkpoint:**
- ✅ Security audit passed (no critical issues)
- ✅ 30-day stability test started
- ✅ 15/20 Tier 1 drivers working (75%) [stretch]
- **Test Suite:** Security test suite (100+ security tests), alpha tester feedback

**Week 52 Checkpoint:**
- ✅ 30-day stability test passed (0 crashes)
- ✅ Phase 2 complete (all 7 gate criteria met)
- ✅ Phase 3 planning approved
- **Test Suite:** Final validation suite (200+ comprehensive tests)

---

## Risk Register (Informed by Phase 1 Discoveries)

### Technical Risks

| Risk | Probability | Impact | Phase 1 Learning | Mitigation |
|------|-------------|--------|------------------|------------|
| **Real-time IPC integration fails** | Medium | Critical | Phase 1 split demo; IPC 54μs validated | Incremental integration (Weeks 27-30); fallback to mock mode if needed; extensive testing |
| **Driver complexity underestimated** | Medium | High | VirtIO took 2 weeks; Pi 4 drivers simpler | Allocate 2-3 weeks per driver; start with UART/SD (highest priority); reference Linux drivers |
| **Hardware compatibility issues** | Low | Medium | Phase 1 QEMU-only; Pi 4 seL4 verified | Single Pi 4 platform; well-documented BCM2711; reference Linux drivers |
| **Cache capacity overflow** | Low | Medium | Phase 1 overflow at 256→512 (Week 25) | Start with 1024 capacity; implement dynamic resizing; monitor pattern growth |
| **Documentation lag** | Medium | Low | Week 17 scope change caused lag | Update PHASE_2_PROGRESS_TRACKER.md within 24h; bi-weekly status docs |
| **Security vulnerabilities** | Medium | Critical | Phase 1 focused on functionality | External audit (Month 22); fix critical issues before alpha release; community bug bounty |
| **Alpha tester recruitment** | Medium | Medium | No Phase 1 external testing | Start recruiting Month 16; aim for 30 (buffer for 20 target); incentivize with early access |
| **Stability on real hardware** | Medium | High | Phase 1 QEMU only; 12h test passed | Start with 7-day test (Week 40); scale to 30-day (Week 51); continuous monitoring |
| **ACPI S3 complexity** | Medium | Medium | Phase 1 suspend/resume instant | Phase 1 validated framework; Week 45-46 focus; extensive debugging via serial console |
| **AI inference on different GPUs** | Low | Medium | Phase 1 RTX 2070 only | Test on Arc A380 (Intel) and Radeon 780M (AMD); fallback to CPU if GPU fails |

### Non-Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Hardware acquisition delays** | Low | Low | Pi 4 already owned; accessories ($65) ship in 1-2 days |
| **No funding secured** | Low | Low | Pi 4 pivot eliminates hardware budget need | Self-fund Phase 2 ($65-175 total); Pi 4 already owned |
| **Timeline slips (solo dev)** | High | Medium | Adjust scope (defer 20→15 drivers acceptable); focus on 7 gate criteria; 24% buffer from Phase 1 |
| **Security audit cost** | Medium | Low | Budget $75K; consider open-source tools (KLEE, AFL, Valgrind); defer to Month 23 if needed |
| **Alpha tester drop-off** | Medium | Medium | Recruit 30 (aim for 20 active); provide responsive support; release patches quickly (P0 within 48h) |
| **Scope creep** | Medium | Medium | Maintain discipline (Phase 1 lesson); defer non-critical features; focus on 7 gate criteria only |

---

## Total Phase 2 Effort Estimate

**Based on Phase 1 Efficiency (24% under budget):**

**Week-by-Week Breakdown (Raspberry Pi 4):**
- Weeks 27-30 (IPC integration): 38-48 hours (4 weeks × 9.5-12h/week)
- Weeks 31-34 (Pi 4 setup + UART IPC): 42-58 hours (4 weeks × 10.5-14.5h/week)
- Weeks 35-38 (SD/EMMC + GENET drivers): 48-64 hours (4 weeks × 12-16h/week)
- Weeks 39-42 (USB HID + alpha prep): 42-56 hours (4 weeks × 10.5-14h/week)
- Weeks 43-46 (GPIO + Device Tree): 38-52 hours (4 weeks × 9.5-13h/week)
- Weeks 47-50 (Alpha + security): 56-74 hours (4 weeks × 14-18.5h/week, bug fixing)
- Weeks 51-52 (Stability + report): 22-30 hours (2 weeks × 11-15h/week)

**Total Estimated: 286-382 hours**

**Conservative Estimate: ~400 hours** (reduced from 500h due to simpler Pi 4 vs x86)

**Comparison to Phase 1:**
- Phase 1 actual: 286 hours (26 weeks)
- Phase 2 estimate: 400 hours (52 weeks)
- Scaling factor: 1.4× (reduced complexity with single Pi 4 target)

**Hardware Cost Savings:**
- Original (x86): $5,000 (Intel NUC + Framework + Dell)
- Revised (Pi 4): $65-175
- Savings: $4,825-4,935 (97-99%)

**Confidence Level: High** (based on Phase 1 efficiency, proven seL4 Pi 4 support)

---

## Path Back to Standalone Operation (Phase 3+)

The Phase 2 split architecture is **temporary**. The original vision and end goal remains a fully self-contained system where AI runs on the same hardware as the microkernel.

### Phase 3 Options (2027+)

**Option A: Multi-Pi Cluster**
- Pi 4: seL4 microkernel (bare metal)
- Pi 5: Linux + TinyLlama (AI inference)
- UART connection between boards
- Cost: ~$200 additional
- Benefit: Keeps ARM64 architecture, incremental upgrade

**Option B: x86 Return (Original Vision)**
- Intel NUC or equivalent x86-64 hardware
- All AI runs on-device (Phi-3 Mini + TinyLlama)
- ivshmem shared memory IPC (<100μs, not UART)
- Cost: ~$1,200+
- Benefit: Full performance, original architecture restored

### Hardware Requirements for Standalone
- **CPU:** 8-core minimum (2 kernel, 6 AI)
- **RAM:** 16-32GB (dynamic scaling support)
- **GPU:** Discrete GPU (RTX 3060+ / Arc A380) for <500ms inference
- **Storage:** NVMe SSD (fast model loading)

### Why This Matters
The split architecture adds latency (1-10ms UART vs 54μs shared memory) and requires a host PC. Phase 3+ should eliminate this dependency for:
- **Portable deployment** (laptop, embedded systems)
- **Air-gapped operation** (no network/host required)
- **Production reliability** (single device = single point of failure eliminated)

### Migration Path
1. **Phase 2 (Now):** Validate architecture on Pi 4 + Host PC
2. **Phase 3 Option A:** Add Pi 5 for local AI (if budget limited)
3. **Phase 3 Option B:** Return to x86 (if budget available)
4. **Phase 4:** Production standalone deployment

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

**Optional (Future):**
- Raspberry Pi 5 for AI inference (~$120 AUD)
- Larger SD card 64GB+ (~$30 AUD)
- Active cooling case (~$35 AUD)

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

## Appendix C: Alpha Tester Recruitment Plan

**Target: 30 recruited, 20 active**

**Channels:**
- Reddit: r/linux, r/programming, r/selfhosted, r/homelab
- Hacker News: Show HN post
- Twitter/X: Announce alpha release
- Discord: JARVIS community server
- GitHub: Star/watch repository

**Requirements:**
- Technical background (comfortable with Linux, command line)
- Willing to provide feedback (bug reports, feature requests)
- Hardware compatible (Intel/AMD x86-64, 8GB+ RAM, GPU optional)

**Incentives:**
- Early access to JARVIS AI-OS
- Acknowledgment in credits (CREDITS.md)
- Community recognition (alpha tester badge)
- Potential co-authorship on future papers/publications

**Timeline:**
- Week 40: Start recruitment (Reddit, Hacker News posts)
- Week 41: Onboarding guide created
- Week 42: Alpha release distributed
- Week 47: Active user count validated (target: 15-20)

---

**Phase 2 Implementation Plan Complete**

---

*Implementation Plan Date: December 2025*
*Updated: January 10, 2026 (Week 33 COMPLETE - UART RX Enabled)*
*Author: JARVIS Development Team (Solo Developer)*
*Phase 1 Complete: 26/26 weeks (100%), 286 hours*
*Phase 2 Estimate: 52 weeks, ~400 hours*
*Hardware: Raspberry Pi 4 8GB (BCM2711, Cortex-A72)*
*Hardware Cost: $65-175 (savings: $4,825-4,935 vs original x86 plan)*
*Current Status: Week 33 COMPLETE - UART RX enabled + IPC handler waiting for Python queries*
*Next Milestone: Week 34 - Python↔seL4 bidirectional IPC validation*

**See Also:** `PHASE_2_HARDWARE_PIVOT.md` for full pivot rationale
