# JARVIS AI-OS: Phase 3 Implementation Plan

**Version:** 4.0
**Date:** March 31, 2026
**Phase:** Phase 3 - Beta System (Months 24-36)
**Duration:** Optimization work ongoing now; bare-metal ~14-18 weeks after PC is wipeable
**Status:** INFERENCE + PROCESS ISOLATION MILESTONE — two-process LLM inference on seL4 QEMU verified

---

## Overview

This document provides a detailed week-by-week implementation plan for Phase 3. Each week has specific deliverables, estimated effort, dependencies, and acceptance criteria.

**Goal:** Build a standalone JARVIS AI-OS running on bare-metal x86-64 hardware — seL4 microkernel with native AI inference (ggml/llama.cpp in C), shared memory IPC, no Linux, no VMs.

**Success Criteria:**
1. Standalone x86 operation (single machine, no Pi 4, no Linux, no VM) — **MUST**
2. Native AI inference in seL4 userspace (1B-3B model, CPU) — **MUST**
3. Native IPC <1μs round-trip (shared memory between seL4 processes) — **MUST**
4. 30-day stability test on x86 (0 crashes, <1% errors) — **MUST**
5. Security audit passed (fuzz testing + code review of x86 code) — **MUST**
6. Decision cache performance maintained (>80% hit rate) — **MUST**
7. Dynamic model scaling operational (4-state CPU) — SHOULD
8. Pi 4 ARM64 build maintained (Phase 2 tests still pass) — SHOULD

**Efficiency Calibration (from Phase 2):**
- Phase 2 Planned: 8-12 hours/week
- Phase 2 Actual: ~10 hours/week average (23 active weeks, ~230 hours)
- **Phase 2 Efficiency: On budget**
- Phase 3 estimate: 8-12 hours/week, 40-44 weeks, ~400-500 hours total

---

## CURRENT STATUS (April 6, 2026)

### What's Done

| Milestone | Date | Notes |
|-----------|------|-------|
| Phase 3a: GPU benchmarks + native Linux build | Mar 2026 | RTX 2070 benchmarks, seL4 native build env |
| Pre-work: 8/8 tasks complete | Mar 2026 | Saved 8-12 weeks of Phase 3b |
| seL4 x86-64 QEMU (123/123 tests) | Mar 2026 | Build env documented |
| Inference engine (C-only, quantized) | Mar 25, 2026 | 4/4 stages PASS, coherent text |
| Process isolation (A+B via CPIO) | Mar 28, 2026 | Shared memory IPC, SEC-014 |
| Bare-metal boot on Ryzen 2700X | Apr 3, 2026 | VGA output, self-test 5/5 PASS |
| Bare-metal LLM inference | Apr 3, 2026 | Coherent text via process-isolated IPC |
| NVMe runtime model loading | Apr 4, 2026 | 770MB from Lexar NM790 FAT32, 1.6MB USB image |
| Intel I211 NIC driver | Apr 5, 2026 | PCI 8086:1539, 11 mock tests, CI green |
| IPC response drain fix | Apr 5, 2026 | Multi-message responses no longer cause shift |
| Continuous IPC workload | Apr 5, 2026 | 63 queries, xorshift PRNG, stats/100 |
| SHMEM ring overflow fix | Apr 6, 2026 | 16→15 slots, _Static_assert, was crashing at slot 15 |
| Debug config (compile-time flags) | Apr 6, 2026 | jarvis_debug.h: IPC/PB/ring/stats toggles |
| QEMU workload verified | Apr 6, 2026 | q=100: 71 hits, 13 infer, 11 hb, 5 shield, 0 errors |

### What's Left

| Task | Priority | Effort | Notes |
|------|----------|--------|-------|
| Bare-metal workload verification | HIGH | 1 session | Flash USB, run workload, verify stats |
| 30-day stability test | HIGH | 30 days | Continuous workload on bare metal |
| Phase 3c: Fuzz testing | MEDIUM | 2-3 weeks | NVMe, PCI, FAT32, IPC packet fuzzing |
| Phase 3c: Security audit | MEDIUM | 2 weeks | Review all x86 driver code |
| Phase 3c: Enhanced SHIELD | LOW | 2 weeks | Model-assisted risk scoring |
| Dynamic model scaling wiring | LOW | 2 weeks | State machine exists, not connected |
| Final report + v0.3.0-beta tag | LAST | 1 week | After all above |

### Test Summary

| Phase | Tests | Status |
|-------|-------|--------|
| Phase 1 | 338 | COMPLETE |
| Phase 2 | 108 | COMPLETE |
| Phase 3 | 368 | ALL PASSING (CI green) |
| **Total** | **814** | |

### Hardware

| Machine | Role | Status |
|---------|------|--------|
| **JARVIS PC** | Bare-metal seL4 target | Ryzen 7 2700X, 32GB, 1TB NVMe, R9 280X (display), ASUS X470-F |
| **Main PC** | Development | Ryzen 5 5600, 32GB, RTX 2070, ASRock B550M-ITX/ac |
| Pi 4 8GB | Phase 2 (retired) | 30-day stability proven |

### Phase 2 Baseline (What Carries Forward)

| Metric | Phase 2 Result | Phase 3 Target |
|--------|----------------|----------------|
| Platform | Pi 4 ARM64 + host PC | x86-64 standalone |
| Drivers | 21 operational | x86 equivalents |
| Stability | 30.6 days, 0 crashes | 30 days on x86 |
| IPC latency | 7ms (UART) | <1μs (shared memory) |
| AI inference | 558ms Phi-3 CPU / <100ms 1B CPU | **Coherent text on seL4 QEMU (Llama 3.2 1B Q4_K_M)** |
| Cache hit rate | 87.5% | >80% |
| Tests | 108 PASS, 0 FAIL | **307 PASS (120+ target exceeded)** |
| LOC | 27,029 (65 files) | Growing |
| Security | 6 findings, 4 fixed | Fuzz testing + expanded audit |
| Hardware cost | $215.82 AUD | +$0-50 |

### Hardware Status (Revised March 31, 2026)

| Hardware | Specs | Status | Role |
|----------|-------|--------|------|
| Raspberry Pi 4 8GB | BCM2711, 8GB | Phase 2 complete | Phase 2 ARM legacy (not active in Phase 3b) |
| Raspberry Pi 5 4GB | BCM2712, 4GB | Owned, unused | Not used in Phase 3 |
| **JARVIS PC** | **Ryzen 7 2700X, R9 280X (display), 32GB DDR4, X470-F Gaming, 1TB NVMe** | **BARE-METAL BOOT ACHIEVED** | **seL4 bare-metal target** |
| **Main PC** | **Ryzen 5 5600, RTX 2070, 32GB, ASRock B550M-ITX/ac** | **Operational (Windows)** | **Daily driver** |

**GPU Status:**
- JARVIS PC: R9 280X (GCN 1.0, display only, no compute). GPU compute deferred to Phase 4.
- Main PC: RTX 2070 (benchmarks done, not used for JARVIS)

**Memory:** 32GB DDR4 (swapped from 16GB). F32 model dequant up to 3B viable. Quantized path (qmodel) works for all sizes.

**Disk:** 1TB NVMe (Lexar NM790, PCI 1d97:1602) on JARVIS PC.

**BARE-METAL BOOT ACHIEVED (April 3, 2026).** seL4 boots on real Ryzen 2700X, VGA output on R9 280X, self-test 5/5 PASS, 183 untypeds. CPU-only inference (AVX2 on Zen+). NIC: Intel I211-AT (PCI 8086:1539) — needs igb-family driver.

### Verified PCI Devices (lspci, April 2026)

| Device | BDF | PCI ID | Driver Status |
|--------|-----|--------|---------------|
| NVMe (boot drive) | 01:00.0 | 1d97:1602 | Lexar NM790 — needs NVMe driver (not AHCI) |
| SATA | 02:00.1 | 1022:43c8 | AMD 400 Series — AHCI compatible (no drives) |
| SATA | 0b:00.2 | 1022:7901 | AMD FCH AHCI — AHCI compatible (no drives) |
| USB | 02:00.0 / 05:00.0 / 0a:00.3 | AMD + ASMedia | All xHCI — no legacy EHCI |
| NIC | 07:00.0 | 8086:1539 | Intel I211 — needs igb driver |
| VGA | 09:00.0 | 1002:6798 | R9 280X — VGA text mode working at 0xB8000 |

Boot drive is NVMe — existing AHCI driver cannot access it. Model loading requires NVMe driver or GRUB multiboot module workaround.

### Code Portability Assessment

| Component | Files | LOC | Portable? | Phase 3 Action |
|-----------|-------|-----|-----------|----------------|
| Decision cache | 2 | ~800 | **Yes** — pure C, no HW deps | Copy directly |
| Ring buffer | 2 | ~400 | **Yes** — pure C | Copy directly |
| Dual ring buffer | 2 | ~600 | **Yes** — pure C | Copy directly |
| IPC handler | 2 | ~500 | **Partial** — UART framing | Replace transport, keep protocol |
| Net stack (protocol) | 2 | ~600 | **Yes** — IP/ARP/ICMP logic | Copy, new NIC driver underneath |
| Net commands | 2 | ~315 | **Yes** — shell command layer | Copy directly |
| SHIELD logic | - | ~200 | **Yes** — pure Python/C | Port to native C |
| FDT parser | 2 | ~500 | **No** — ARM device tree | Replace with ACPI or hardcoded |
| Boot manager | 2 | ~300 | **Partial** — boot timing | Adapt for x86 boot sequence |
| UART PL011 | 2 | 1,244 | **No** — BCM2711 MMIO | Rewrite as 16550A UART |
| EMMC/SDHCI | 2 | 1,547 | **No** — BCM2711 MMIO | Rewrite as AHCI/NVMe |
| BCM2711 Timer | 2 | 67 | **No** — BCM2711 register | Replace with HPET/TSC |
| GENET Ethernet | 2 | 1,134 | **No** — BCM2711 MMIO | Rewrite for Intel/Realtek NIC |
| USB HID (DWC2) | 2 | 1,393 | **No** — DWC2 specific | Rewrite for xHCI |
| GPIO, I2C, SPI, etc. | 12 | ~1,800 | **No** — BCM2711 specific | Not needed on x86 |
| DMA engine | 2 | ~400 | **No** — BCM2711 DMA | Not needed (AHCI has own DMA) |
| Main rootserver | 1 | 3,662 | **Partial** — init sequence | Port init, keep IPC loop logic |
| **Portable total** | | ~3,415 | | Direct copy or minor changes |
| **Rewrite total** | | ~10,247 | | New x86-specific drivers |

**~25% of Phase 2 C code transfers directly.** The rest is BCM2711-specific driver code that needs x86 equivalents. However, x86 PC hardware is generally better documented and simpler to access than bare-metal ARM SoC peripherals.

---

## Phase 3 Architecture

### Phase 3a: GPU-Accelerated Host (Weeks 1-6)

Phase 3a is COMPLETE (GPU benchmarks on RTX 2070, native Linux build env). Skipped — go straight to Phase 3b bare-metal.

### Phase 3b: Pure Bare-Metal x86 (Weeks 7-28) — TARGET

```
┌─────────────────────────────────────────────────────────┐
│              JARVIS OS (JARVIS Project PC)                    │
│         Ryzen 7 2700X, 16GB DDR4, CPU-only              │
│                                                          │
│   seL4 Microkernel (Ring 0)                              │
│   ├── Interrupt handling (<1μs)                          │
│   ├── Memory management (16GB DDR4)                     │
│   ├── IPC primitives (notifications + shared pages)     │
│   └── Capability system                                 │
│              ↕ shared memory (<1μs)                      │
│   JARVIS Rootserver (Ring 3, Process A)                  │
│   ├── Decision cache (258 patterns, <1ms)               │
│   ├── IPC handler (shared memory ring buffer)           │
│   ├── SHIELD safety framework                           │
│   ├── Shell + command dispatch                          │
│   └── x86 drivers (serial, AHCI, NIC)                  │
│              ↕ shared memory (<1μs)                      │
│   AI Inference Process (Ring 3, Process B)               │
│   ├── ggml library (native C, CPU)                      │
│   ├── Model loader (from NVMe/AHCI)                    │
│   ├── Dynamic model scaling (1B/3B/3.8B)               │
│   └── Inference request handler                         │
│                                                          │
│   No Linux. No VM. Pure seL4 bare metal.                │
└─────────────────────────────────────────────────────────┘
```

### Architecture Comparison

| Aspect | Phase 2 | Phase 3a | Phase 3b |
|--------|---------|----------|----------|
| Machines | 2 (Pi 4 + PC) | 2 (Pi 4 + project PC) | **1 (JARVIS Project PC)** |
| Microkernel | seL4 ARM64 | seL4 ARM64 | **seL4 x86-64** |
| AI runtime | Python (host PC) | Python + CUDA | **Native C (seL4 userspace)** |
| IPC | UART 7ms | UART 7ms | **Shared memory <1μs** |
| AI speed | 558ms (CPU) | ~13ms/tok (GPU) | **~50-100ms (CPU, 1B-3B)** |
| GPU | None (on Pi 4) | RTX 2070 (benchmarks, main PC) | None (Phase 4) |
| Linux | Required (host) | Required (host) | **None** |
| Standalone | No | No | **Yes** |

---

## Phase 3 Pre-Work: Interim Tasks ✅ COMPLETE

### Focus: De-Risk Phase 3b Unknowns Using Current Hardware

**Objective:** Advance Phase 3 risks before bare-metal hardware is ready: seL4 x86 build environment, ggml portability to seL4/musl, and shared memory IPC design. Completing all Tier 1 tasks saved an estimated 4-6 weeks of Phase 3b calendar time. **Pre-work complete. JARVIS Project PC now operational — all remaining work happens on this machine.**

**Hardware used:** Main PC (RTX 2070, 32GB, WSL), Pi 4 (running Phase 2), Pi 5 (available)

**Note:** Phase 3 week numbering (Week 1+) starts when the JARVIS Project PC is assembled. These pre-work tasks happen before Week 1.

### Summary

| # | Task | Hours | Status | Key Result |
|---|------|-------|--------|------------|
| 1 | seL4 x86-64 on QEMU | 6-8 | ✅ DONE | 123/123 tests pass, build env documented |
| 2 | ggml standalone compilation test | 4-6 | ✅ DONE | 5 POSIX stubs needed, C++ backend main challenge |
| 3 | Shared memory IPC protocol | 6-8 | ✅ DONE | 10/10 PASS, 23.7M msg/sec, 0 corruption |
| 4 | Port portable code to x86 build | 3-4 | ✅ DONE | 22/22 tests pass, 5/7 modules zero changes |
| 5 | x86 driver skeleton headers | 4-6 | ✅ DONE | uart_16550.h, pci.h, ahci.h |
| 6 | Git tag v0.2.0-alpha | 0.25 | ✅ DONE | Phase 2 formally closed |
| 7 | Pi 5 llama.cpp benchmark | 2-3 | ✅ DONE | Script + research estimates written |
| 8 | Custom rootserver in QEMU | 8-12 | ✅ DONE | main_x86.c + CMakeLists.txt, build integration pending |
| | **Total** | **35-50** | **8/8** | |

---

### Tier 1: Immediate — Saves Weeks of Phase 3b Time

#### Pre-Work Task 1: seL4 x86-64 on QEMU (6-8 hours)

**Tasks:**
1. Install seL4 build dependencies in WSL
   - `sudo apt install build-essential cmake ninja-build python3 python3-pip gcc-multilib g++-multilib qemu-system-x86 repo git`

2. Clone and build seL4 for x86-64 QEMU
   ```bash
   mkdir ~/sel4-x86 && cd ~/sel4-x86
   repo init -u https://github.com/seL4/sel4test-manifest.git
   repo sync
   mkdir cbuild && cd cbuild
   ../init-build.sh -DPLATFORM=x86_64 -DSIMULATION=1
   ninja
   ./simulate
   ```

3. Verify seL4 boots, kernel banner appears, test suite runs
4. Document any build issues and fixes

**Deliverables:**
- ✅ seL4 x86-64 building and running in QEMU on main PC (123/123 tests pass)
- ✅ Build environment documented (`phase3/docs/SEL4_X86_QEMU_SETUP.md`)

**Effort:** 6-8 hours
**Dependencies:** None
**Acceptance:** ✅ seL4 test suite boots and runs in QEMU, serial output captured
**Unblocks:** All Phase 3b rootserver development can happen in QEMU emulation before real hardware arrives. This is Phase 3b Weeks 7-8 done early.

---

#### Pre-Work Task 2: ggml Standalone Compilation Test (4-6 hours)

**Tasks:**
1. Clone llama.cpp and build CPU-only (standard glibc first, validate baseline)
   ```bash
   git clone https://github.com/ggml-org/llama.cpp.git
   cd llama.cpp
   cmake -B build -DGGML_CUDA=OFF -DGGML_VULKAN=OFF -DGGML_METAL=OFF \
     -DGGML_OPENMP=OFF -DGGML_LLAMAFILE=OFF -DBUILD_SHARED_LIBS=OFF
   cmake --build build -j$(nproc)
   ```

2. Attempt compiling ggml core files with musl-gcc
   ```bash
   sudo apt install musl-tools
   musl-gcc -c ggml/src/ggml.c -I ggml/include -O2 -DNDEBUG
   musl-gcc -c ggml/src/ggml-alloc.c -I ggml/include -O2 -DNDEBUG
   musl-gcc -c ggml/src/ggml-backend.c -I ggml/include -O2 -DNDEBUG
   ```

3. Document every compilation error — each one maps to a POSIX stub needed for seL4
   - Known issues:
     - `execinfo.h` — backtrace, already fixed upstream (musl detected and skipped)
     - `mmap()`/`munmap()` — model loading; stub with `malloc()` + `read()`
     - `pthreads` — parallel inference; compile single-threaded with `-DGGML_OPENMP=OFF`
     - `fopen()`/`fread()`/`fclose()` — model file I/O; stub with AHCI block reads on seL4
     - `clock_gettime()` — timing; map to x86 timer driver

4. Write `phase3/src/ai/GGML_PORTABILITY_NOTES.md` documenting all findings

**Deliverables:**
- ✅ ggml compilation results: ggml-alloc.c and ggml-quants.c compile clean; ggml.c needs 2 defines
- ✅ Complete list of 5 required POSIX stubs + 6 skippable deps identified
- ✅ `phase3/src/ai/GGML_PORTABILITY_NOTES.md` — key finding: C++ backend (gguf.cpp) needs C-only replacement

**Effort:** 4-6 hours
**Dependencies:** None
**Acceptance:** ✅ Complete list of blockers identified, stub strategy documented
**Unblocks:** Phase 3b Weeks 19-20 (ggml integration). Knowing exact stubs means `posix_stubs.c` can be written before the PC arrives.

---

#### Pre-Work Task 3: Shared Memory IPC — Design + Implement + Test (6-8 hours)

**Tasks:**
1. Design shared memory ring buffer protocol
   - File: `phase3/src/ipc/shmem_ipc.h` — header with structs, constants, API
   - File: `phase3/src/ipc/shmem_ipc.c` — ring buffer implementation
   - Keep same 14 message types from Phase 2 (QUERY=0x01 through STATE_ACK=0x0E)
   - Remove UART framing (no SYNC 0xAA55, no CRC-CCITT) — shared memory is reliable
   - Message format: TYPE(1) + SEQ(2) + LENGTH(2) + PAYLOAD(0-240) = 5 bytes overhead
   - Ring buffer: 16 slots × 256 bytes = 4KB (one page)
   - Producer/consumer with atomic head/tail indices
   - Header: write_idx, read_idx, size, magic (0xDEADBEEF)

2. Implement in portable C (no seL4 deps — pure userspace)
   - `shmem_ipc_init()` — initialize ring buffer header
   - `shmem_ipc_send()` — write message to next slot, advance write_idx
   - `shmem_ipc_recv()` — read message from next slot, advance read_idx
   - `shmem_ipc_pending()` — check if messages available
   - Use `__atomic_load_n` / `__atomic_store_n` for index access (portable atomics)

3. Write Linux userspace test program
   - File: `phase3/src/ipc/test_shmem_ipc.c`
   - Two processes sharing an `mmap(MAP_SHARED|MAP_ANONYMOUS)` region
   - Producer sends 10,000 messages, consumer verifies all received correctly
   - Test: wrap-around, full buffer (producer waits), empty buffer (consumer waits)
   - Test: interleaved send/recv, burst patterns
   - Measure: messages/second throughput

4. Define backward compatibility interface
   ```c
   // phase3/src/ipc/ipc_transport.h
   #ifdef JARVIS_IPC_UART
     #include "uart_ipc.h"
   #elif defined(JARVIS_IPC_SHMEM)
     #include "shmem_ipc.h"
   #endif
   int ipc_send(uint8_t type, uint16_t seq, const void* payload, uint16_t len);
   int ipc_recv(uint8_t* type, uint16_t* seq, void* payload, uint16_t* len);
   ```

**Deliverables:**
- ✅ `phase3/src/ipc/shmem_ipc.h`, `shmem_ipc.c` — lock-free SPSC ring buffer with atomic indices
- ✅ `phase3/src/ipc/test_shmem_ipc.c` — 10-test suite (init, send/recv, full/empty, wrap, all types, max/zero payload, stress)
- ✅ `phase3/src/ipc/ipc_transport.h` — abstraction header for UART/SHMEM selection
- ✅ Test results: 10/10 PASS, 10,000 messages, 0 corruption, ~23.7M msg/sec throughput

**Effort:** 6-8 hours
**Dependencies:** None
**Acceptance:** ✅ 10,000 messages sent/received with 0 errors, correct wrap-around behavior
**Unblocks:** Phase 3b Weeks 23-24. When JARVIS Project PC arrives, swap `mmap` → `seL4_Page_Map` and `sem_post` → `seL4_Signal`. Ring buffer logic is identical.

---

### Tier 2: Next — Steady Progress

#### Pre-Work Task 4: Port Portable Code to x86 Build (3-4 hours)

**Tasks:**
1. Copy portable Phase 2 modules to `phase3/src/`
   - `decision_cache.c/h` + `cache_patterns.c/h` → `phase3/src/ai/`
   - `ring_buffer.c/h` → `phase3/src/ipc/`
   - `dual_ring_buffer.c/h` → `phase3/src/ipc/`
   - `net_stack.c/h` (protocol layer) → `phase3/src/drivers/`
   - `net_cmd.c/h` → `phase3/src/drivers/`

2. Write `phase3/src/CMakeLists.txt` that compiles all portable modules on x86-64 Linux
   - Use `gcc` (not cross-compiler) — just validates no ARM-isms

3. Run Phase 2 C tests against x86 build
   - IPC ring buffer tests (12 tests)
   - Decision cache tests (if extractable from Phase 2 test harness)

**Deliverables:**
- ✅ 7 portable modules compiling on x86-64 (`libjarvis_portable.a`, 73KB)
- ✅ Phase 2 tests passing on x86: 22/22 (12 dual_ring + 10 ipc_handler)
- ✅ 5/7 modules needed zero modifications; net_stack.c and net_cmd.c got `#ifdef JARVIS_PLATFORM_PI4` guards

**Effort:** 3-4 hours
**Dependencies:** None
**Acceptance:** ✅ All portable modules compile with `gcc -Wall -Werror` on x86-64, 22/22 tests pass

---

#### Pre-Work Task 5: x86 Driver Research + Skeleton Headers (4-6 hours)

**Tasks:**
1. Study OSDev wiki reference pages:
   - [16550A UART / Serial Ports](https://wiki.osdev.org/Serial_Ports) — I/O port registers at 0x3F8
   - [AHCI](https://wiki.osdev.org/AHCI) — HBA registers, command list, FIS layout, port init
   - [PCI](https://wiki.osdev.org/PCI) — config space access via 0xCF8/0xCFC, class codes, BARs

2. Write skeleton header files with register definitions and struct layouts:
   - `phase3/src/drivers/uart_16550.h` — register offsets (THR, RBR, IER, LCR, LSR, etc.), baud rate divisors, function prototypes
   - `phase3/src/drivers/ahci.h` — HBA_MEM, HBA_PORT, HBA_CMD_HEADER, HBA_CMD_TBL, HBA_FIS structs, capability bits
   - `phase3/src/drivers/pci.h` — config read/write prototypes, PCI_CLASS constants, BAR parsing helpers

3. Don't implement function bodies — headers and struct definitions only

**Deliverables:**
- ✅ `phase3/src/drivers/uart_16550.h` — COM1-4, 8 register offsets, all bit defs, baud divisors, 8 function prototypes
- ✅ `phase3/src/drivers/pci.h` — config space, command/status bits, class codes, BAR parsing, `pci_device_t`, 7 prototypes
- ✅ `phase3/src/drivers/ahci.h` — HBA/port registers, 6 FIS structs, cmd header/PRD/table, `_Static_assert` size checks, 7 prototypes
- ✅ All headers compile with `gcc -Wall -Werror`

**Effort:** 4-6 hours
**Dependencies:** None
**Acceptance:** ✅ Headers compile cleanly, struct sizes validated with _Static_assert

---

#### Pre-Work Task 6: Phase 2 Close-Out — Git Tag v0.2.0-alpha (15 min)

**Tasks:**
1. Create annotated git tag:
   ```bash
   git tag -a v0.2.0-alpha -m "Phase 2 COMPLETE: Alpha system on Pi 4 (108 PASS, 0 FAIL, 30-day stability)"
   ```

**Deliverables:**
- ✅ Git tag `v0.2.0-alpha` created — Phase 2 formally closed

**Effort:** 15 minutes
**Dependencies:** None
**Acceptance:** ✅ `git tag -l` shows v0.2.0-alpha

---

### Tier 3: Nice-to-Have

#### Pre-Work Task 7: Pi 5 llama.cpp Benchmark (2-3 hours)

**Tasks:**
1. Install Raspberry Pi OS Lite on Pi 5 (4GB)
2. Install llama.cpp (CPU build) or Ollama
3. Benchmark models:
   - `gemma3:1b` — expected ~13 tok/s, ~2.5 GB RAM
   - `llama3.2:1b-instruct` — expected ~12 tok/s, ~2.8 GB RAM
   - `llama3.2:3b` — expected ~5 tok/s, ~4.2 GB RAM (may OOM on 4GB Pi 5)
4. Document actual numbers in `phase3/docs/PI5_BENCHMARK_RESULTS.md`

**Deliverables:**
- ✅ `phase3/scripts/pi5_benchmark.sh` — 655-line benchmark script (Ollama + llama.cpp, 7 models, CSV output)
- ✅ `phase3/docs/PI5_BENCHMARK_RESULTS.md` — research-based estimates with blank columns for actuals
- ✅ Key finding: 3B Q4 needs ~4.2GB, does NOT fit on 4GB Pi 5 with OS overhead. Practical limit: 1B-1.5B at 11-13 tok/s.

**Effort:** 2-3 hours
**Dependencies:** Pi 5 available (script ready to run when connected)
**Acceptance:** ✅ Script + research estimates documented, actual benchmarks pending hardware setup

---

#### Pre-Work Task 8: Custom Rootserver in QEMU (8-12 hours)

**Tasks:**
1. After Task 1 (seL4 x86-64 QEMU boot), create a custom rootserver that:
   - Loads decision cache (258 patterns) from compiled-in data
   - Accepts queries via serial console (QEMU serial)
   - Performs cache lookups and returns responses
   - Runs SHIELD risk scoring on queries
   - Measures seL4 IPC latency (native round-trip)

2. This is effectively Phase 3b Weeks 9-12 running in emulation
3. When JARVIS Project PC arrives, boot same rootserver on real hardware — code already works

**Deliverables:**
- ✅ `phase3/src/sel4/main_x86.c` — rootserver with JARVIS banner, decision cache, SHIELD, shell commands
- ✅ `phase3/src/sel4/CMakeLists.txt` — seL4 build system integration template
- ✅ `phase3/docs/SEL4_X86_ROOTSERVER_NOTES.md` — build integration steps, ARM64 vs x86 comparison
- ⏳ IPC latency benchmark — pending build integration (seL4 tree integration needed)
- ⏳ Interactive serial input — pending 16550A UART RX driver (Phase 3b Week 13-14)

**Effort:** 8-12 hours
**Dependencies:** Pre-Work Task 1 complete
**Acceptance:** ✅ Source code written and documented. Build integration pending seL4 tree setup.

---

### Pre-Work Critical Path

```
Pre-Work Tasks (before JARVIS Project PC):
  Task 6: Git tag v0.2.0-alpha (15 min)
  Task 1: seL4 x86-64 QEMU boot (6-8h)
    ↓
  Task 8: Custom rootserver in QEMU (8-12h)     Task 2: ggml musl test (4-6h)
    ↓                                               ↓
  [rootserver code ready for real HW]            [POSIX stubs identified]
                                                    ↓
  Task 3: Shared memory IPC (6-8h)              Task 4: Port code (3-4h)
    ↓                                               ↓
  [IPC protocol tested]                          [Portable modules verified]
                                                    ↓
  Task 5: Driver skeleton headers (4-6h)        Task 7: Pi 5 bench (2-3h)
    ↓
  [Driver structs ready for implementation]

────────────── BARE-METAL READY — JARVIS PC WIPEABLE ──────────────
    ↓
  Week 1: Phase 3a starts (bare-metal boot prep)
    ↓
  Week 7: Phase 3b starts — most unknowns already resolved
```

### Early Phase 3b Work (Done Ahead of Schedule)

While waiting for the JARVIS Project PC, the majority of Phase 3b implementation was completed in QEMU and with mock testing. This reduces Phase 3b timeline by an estimated 8-12 weeks.

| Component | Planned Week | Status | Files | Tests |
|-----------|-------------|--------|-------|-------|
| GGUF parser (C-only) | 19-20 | ✅ DONE | gguf_parser.h/c | 12 PASS |
| UART 16550A driver | 13-14 | ✅ DONE | uart_16550.c/h | 7 PASS |
| PCI enumeration | 15-16 | ✅ DONE | pci.c/h | 11 PASS |
| AHCI full I/O | 15-16 | ✅ DONE | ahci.c/h | 13 PASS |
| NIC RTL8168 skeleton | 17-18 | ✅ DONE | nic_rtl8168.c/h | 6 PASS |
| x86 Timer (PIT/HPET/TSC) | 13-14 | ✅ DONE | x86_timer.c/h | 8 PASS |
| Block device abstraction | 15-16 | ✅ DONE | blk_dev_x86.c/h | 9 PASS |
| C tensor ops (10 ops) | 19-20 | ✅ DONE | tensor_ops.c/h | 14 PASS |
| Dequantization (Q4_0/Q8_0/F16) | 19-20 | ✅ DONE | dequant.c/h | 23 PASS |
| BPE Tokenizer | 21-22 | ✅ DONE | tokenizer.c/h | 12 PASS |
| Model architecture + loading | 21-22 | ✅ DONE | llama_model.h, llama_load.c | 7 PASS |
| Sampling (greedy + top-k) | 21-22 | ✅ DONE | sampling.c/h | 9 PASS |
| Transformer forward pass | 21-22 | ✅ DONE | llama_forward.c | 9 PASS |
| Inference API | 21-22 | ✅ DONE | inference.c/h | 4 PASS |
| Shared memory IPC | 23-24 | ✅ DONE | shmem_ipc.c/h | 10 PASS |
| Custom x86 rootserver | 9-12 | ✅ DONE (QEMU) | main_x86.c | 5/5 self-test PASS |

**Total Phase 3 code:** 80+ files, ~20,000 LOC, 368 tests (all passing), 35+ CI steps

**Phase 3b: COMPLETE** — All hardware integration milestones achieved. Bare-metal boot, NVMe model loading, I211 NIC driver, continuous workload loop, IPC fixes. Ready for Phase 3c hardening.

### Phase 3 Timeline

| Phase | Status | Completed |
|-------|--------|-----------|
| 3a GPU + benchmarks | **DONE** | Mar 2026 |
| 3b software (QEMU) | **DONE** | Mar 2026 |
| 3b optimization (AVX2, SIMD) | **DONE** | Mar 31, 2026 |
| 3b bare-metal boot | **DONE** | Apr 3, 2026 |
| 3b bare-metal inference | **DONE** | Apr 3, 2026 |
| 3b NVMe model loading | **DONE** | Apr 4, 2026 |
| 3b I211 NIC + IPC fixes | **DONE** | Apr 5-6, 2026 |
| 3b continuous workload | **DONE** | Apr 6, 2026 (QEMU verified, err=0) |
| **3c hardening** | **NEXT** | Fuzz testing, security audit, SHIELD |
| 3c finalization | PENDING | Final report, v0.3.0-beta |

**Estimated remaining to v0.3.0-beta:** ~8-12 weeks (hardening + finalization)

---

## Phase 3a — Compressed Hardware Bring-Up (Weeks 1-2)

### Focus: Assemble PC, Validate Hardware, Boot seL4

**Objective:** Boot seL4 on real Ryzen hardware (JARVIS PC: 2700X, 16GB, 1TB, X470-F Gaming). Skip GPU setup entirely — Phase 3 is CPU-only inference (AVX2 on Zen+). GPU compute deferred to Phase 4.

**Rationale for compression:** Phase 3a GPU host plan is obsolete — no GPU on JARVIS PC (R9 280X is display-only). GPU benchmarks already done on RTX 2070 (now in main PC). Phase 3b software is complete (inference engine, all drivers, self-test mode). Go directly to seL4 bare metal.

> **Original Phase 3a plan (Weeks 1-6) preserved below for reference. GPU steps are now skipped — bare-metal boot is the first task.**

**Objective (original, obsolete):** Move AI inference to JARVIS Project PC with GPU acceleration. This is no longer the plan — JARVIS PC has no GPU compute. CPU-only inference instead.

---

### Week 1: Bare-Metal Boot Prep (REVISED)

**Tasks:**
1. Prepare JARVIS PC for bare-metal seL4
   - JARVIS PC: Ryzen 7 2700X, R9 280X (display only), 16GB DDR4, X470-F Gaming, 1TB NVMe
   - Ubuntu already installed — use it to build seL4 and prepare GRUB boot media
   - BIOS settings: enable IOMMU, disable secure boot, keep UEFI boot mode

2. Build seL4 on JARVIS PC (Ubuntu)
   - Install build essentials: `gcc`, `cmake`, `ninja-build`, `git`, `repo`
   - Clone seL4 repos, build kernel for x86-64 PC99
   - Verify QEMU boot on JARVIS PC first

3. Prepare GRUB2 boot media
   - Create USB boot stick with seL4 kernel + JARVIS rootserver
   - Boot seL4 on real Ryzen 7 2700X hardware
   - Connect serial console for debug output

**Deliverables:**
- seL4 booting on real Ryzen hardware
- Serial console working
- Self-test mode 5/5 PASS on real hardware

**Effort:** 8-12 hours
**Dependencies:** JARVIS PC available (READY)
**Acceptance:** JARVIS rootserver 5/5 self-test on real x86 hardware

---

### Week 2: GPU AI Benchmarks

**Tasks:**
1. Install llama.cpp with CUDA backend
   - Clone llama.cpp, build with `-DGGML_CUDA=ON`
   - Download model files: Llama 3.2 1B Q4, Llama 3.2 3B Q4, Phi-3 Mini 3.8B Q4, Llama 7B Q4

2. Benchmark all models with `llama-bench`
   - Measure: prompt processing (pp512), token generation (tg128)
   - Record: tokens/second, VRAM usage, system RAM usage
   - Test with flash attention on/off

3. Run interactive inference tests
   - Test each model with JARVIS-style queries (cache patterns, shell commands, SHIELD checks)
   - Measure end-to-end response latency for typical queries

**Deliverables:**
- Benchmark results table (model × metric)
- Model recommendation for Phase 3b CPU target

**Effort:** 6-8 hours
**Dependencies:** Week 1 complete
**Acceptance:** All 4 models benchmarked, results documented

---

> **OBSOLETE:** Original Phase 3a Weeks 3-6 plan used Pi 4 via UART as seL4 target while JARVIS PC served as GPU host. Phase 3b boots seL4 directly on x86 bare metal — Pi 4 UART testing is no longer needed. The sections below are preserved for historical reference only.

<details>
<summary>Week 3-6: Original Pi 4 UART Plan (OBSOLETE — click to expand)</summary>

### Week 3: Python Environment + UART Connection

**Tasks:**
1. Set up Python environment on JARVIS Project PC
   - Python 3.10+, pyserial, numpy
   - Copy Phase 2 Python files: `uart_ipc_client.py`, `system_bootstrap.py`, `stability_harness.py`, `power_manager.py`, `local_ai_handler.py`

2. Connect USB-serial adapter to Pi 4
   - GPIO14 (TXD) → RXD, GPIO15 (RXD) → TXD, GND → GND
   - Verify serial port (`/dev/ttyUSB0` or similar)

3. Test UART IPC communication
   - Run `uart_ipc_client.py` — verify QUERY/RESPONSE working
   - Run heartbeat test — verify RTT ~7ms
   - Run 100-query quick test — verify 100% success rate

### Week 4: Stability Testing (JARVIS Project PC Host)

**Tasks:**
1. Run 1-hour stability test
   - `python stability_harness.py --port /dev/ttyUSB0 --duration 60 --log-dir stability_logs_phase3a --verbose`

2. Run 3-day stability test
   - `python stability_harness.py --port /dev/ttyUSB0 --duration 4320 --log-dir stability_logs_phase3a --verbose`

### Week 5-6: Phase 3a Validation + Documentation

**Tasks:**
1. Complete 3-day stability test
2. Document Phase 3a results
3. Phase 3a exit validation
4. Prepare Phase 3b environment

</details>

**Phase 3a Checkpoint (revised):** JARVIS PC operational, seL4 bare-metal boot achieved, self-test 5/5 PASS on real hardware

---

### Compressed Phase 3a: Week 1 — seL4 Build Env on JARVIS PC (REVISED)

**Tasks:**
1. JARVIS PC already has Ubuntu — install seL4 build dependencies
2. Clone seL4 repos, build kernel for x86-64 PC99, verify QEMU boot on JARVIS PC
3. GPU benchmarks already done on RTX 2070 (main PC) — skip GPU setup entirely
4. Prepare GRUB2 USB boot media for real hardware boot

**Deliverables:** seL4 build env ready, QEMU boot verified on JARVIS PC
**Effort:** 4-6 hours
**Acceptance:** seL4 QEMU boots on JARVIS PC, GRUB boot media prepared

---

### Compressed Phase 3a: Week 2 — Boot seL4 on Real Ryzen Hardware

**Tasks:**
1. Prepare boot media (GRUB2 USB stick using `create_boot_usb.sh`)
2. Boot seL4 on Ryzen 7 2700X (JARVIS PC) — connect serial console
3. Debug any IOAPIC/LAPIC/HPET hardware issues
4. Run JARVIS rootserver with self-test mode on real hardware
5. IPC benchmark: measure native seL4 IPC round-trip (~0.3-0.5μs expected)
6. Validate: cache, SHIELD, tensor ops, dequant, tokenizer, sampling all PASS

**Deliverables:** seL4 on real Ryzen, self-test 5/5 PASS, IPC benchmark
**Effort:** 8-12 hours
**Acceptance:** JARVIS rootserver 5/5 self-test on real x86 hardware
**Risk:** AMD Ryzen PC99 compatibility. Mitigation: already tested in QEMU.

**Compressed Phase 3a Checkpoint:** JARVIS PC dedicated, seL4 booting on real Ryzen 7 2700X hardware with self-test PASS. CPU-only inference (no GPU).

---

## Month 2-3: seL4 x86-64 Bootstrap (Weeks 7-12)

### Focus: Boot seL4 on Real x86 Hardware, Port Core Logic

---

### Week 7-8: seL4 x86-64 Build Environment + QEMU Validation

> **STATUS:** ✅ SOFTWARE COMPLETE — seL4 x86 rootserver built and running in QEMU (pre-work tasks 1+8). Rebuilt with self-test mode: 5/5 PASS. Hardware validation pending Week 2.

**Tasks:**
1. Configure seL4 for x86-64 PC99 platform
   - Platform: `pc99`, Architecture: `x86_64`
   - Create `phase3/src/sel4/CMakeLists.txt` (based on Phase 2 pattern)
   - Minimal rootserver: serial output "Hello from JARVIS x86"

2. Build and test on QEMU x86-64
   - `qemu-system-x86_64` with seL4 kernel
   - Verify: serial output, seL4 banner, rootserver entry
   - Test basic seL4 API: `seL4_DebugPutChar()`, timer, IPC

3. Research x86 boot on real hardware
   - GRUB2 multiboot or seL4 EFI loader
   - USB boot stick preparation
   - Serial console setup (USB-serial adapter to JARVIS Project PC's COM port or USB debug)

**Deliverables:**
- seL4 x86-64 rootserver building and running on QEMU
- Boot method for real hardware identified

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Phase 3a complete
**Acceptance:** "Hello from JARVIS x86" on QEMU serial console

---

### Week 9-10: Boot seL4 on JARVIS Project PC (Real Hardware)

> **STATUS:** ✅ COMPLETE — seL4 boots on Ryzen 7 2700X bare metal. VGA output working. Self-test 5/5 PASS. 183 untypeds. Process isolation verified on real hardware.

**Tasks:**
1. Prepare boot media
   - Create bootable USB with GRUB2 + seL4 kernel image
   - Or: EFI boot via NVMe partition

2. Boot seL4 on Ryzen 5 5600
   - Connect serial console (USB-serial or onboard COM header)
   - Verify: seL4 kernel boots, rootserver runs
   - Debug any hardware-specific issues (IOAPIC, LAPIC, HPET)

3. Validate seL4 x86-64 fundamentals
   - IPC benchmark: measure native IPC round-trip cycles
   - Memory management: allocate + map pages
   - Timer: verify HPET or TSC as time source
   - Expected IPC: ~1,302 cycles (~0.4μs at 3.4 GHz, per seL4 benchmarks)

4. Create x86-specific boot configuration
   - `phase3/src/boot/grub.cfg` or EFI boot config
   - `phase3/firmware/` — boot images

**Deliverables:**
- seL4 booting on real Ryzen 5 5600 hardware
- IPC benchmark: ~0.3-0.5μs round-trip
- Serial console working

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Week 7-8 complete, JARVIS Project PC assembled
**Acceptance:** seL4 rootserver running on real hardware, IPC benchmark completed
**Risk:** AMD Ryzen PC99 compatibility. Mitigation: test on QEMU first, debug IOAPIC/LAPIC issues.

---

### Week 11-12: Port Decision Cache + SHIELD to x86

> **STATUS:** ✅ SOFTWARE COMPLETE — Decision cache (308 patterns), SHIELD safety, shell framework all ported and working in QEMU rootserver.

**Tasks:**
1. Port decision cache to x86 rootserver
   - Copy `decision_cache.c`, `cache_patterns.c` from Phase 2
   - Verify FNV-1a hash produces identical results on x86 (endianness check)
   - Test: load 258 patterns, run cache lookups via serial console
   - Verify: 85%+ hit rate on test query set

2. Port SHIELD safety framework
   - Copy risk scoring logic from Phase 2
   - Integrate with x86 rootserver
   - Test: submit hostile queries, verify block rate

3. Port shell command framework
   - Copy net_cmd.c command dispatch logic (platform-independent parts)
   - Implement minimal shell over serial console
   - Commands: `cache`, `shield`, `stats`, `help`

4. Begin serial driver implementation
   - Research: 16550A UART registers vs PL011
   - File: `phase3/src/drivers/uart_16550.c`
   - Replace `seL4_DebugPutChar()` with direct 16550A I/O port access

**Deliverables:**
- Decision cache working on x86 (258 patterns loaded, queries answerable via serial)
- SHIELD risk scoring working
- Basic serial shell

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Week 9-10 complete
**Acceptance:** Cache lookup test: 100% match with Phase 2 results for identical queries

**Week 12 Checkpoint:** seL4 running on x86 with decision cache + SHIELD + serial shell

---

## Month 3-4: x86 Driver Development (Weeks 13-18)

### Focus: Storage, Networking, and Timer Drivers for x86

---

### Week 13-14: x86 Serial Driver + Timer

> **STATUS:** ✅ SOFTWARE COMPLETE — UART 16550A (7 tests), x86 timer PIT/HPET/TSC (8 tests). Mock-tested. Hardware I/O port integration pending.

**Tasks:**
1. Complete 16550A UART driver
   - File: `phase3/src/drivers/uart_16550.c`
   - Features: TX, RX, configurable baud rate (115200 default)
   - x86 I/O ports: 0x3F8 (COM1), 0x2F8 (COM2)
   - Interrupt-driven RX (or polled for simplicity)
   - ~200-400 LOC (much simpler than PL011 — standard PC hardware)

2. Implement timer driver
   - File: `phase3/src/drivers/x86_timer.c`
   - Options: HPET (preferred), TSC (high resolution), PIT (legacy fallback)
   - Features: microsecond-resolution timestamps, delay functions
   - seL4 may already expose timer via platform support library

3. Write test suites
   - 4+ serial tests (TX, RX, loopback, baud rate)
   - 4+ timer tests (resolution, delay accuracy, overflow)

**Deliverables:**
- 16550A UART driver operational
- Timer driver operational
- 8+ tests passing

**Effort:** 10-14 hours (2 weeks)
**Dependencies:** Week 11-12 complete
**Acceptance:** Serial TX/RX working, timer microsecond resolution verified

---

### Week 15-16: AHCI Storage Driver

> **STATUS:** ✅ SOFTWARE COMPLETE — PCI enumeration (11 tests), AHCI full I/O (13 tests), block device abstraction (9 tests). Mock-tested. Real NVMe I/O pending.

**Tasks:**
1. Implement PCI enumeration
   - File: `phase3/src/drivers/pci.c`
   - Scan PCI bus for AHCI controller (class 0x01, subclass 0x06)
   - Read BAR registers to find AHCI MMIO base address
   - ~300-500 LOC

2. Implement AHCI driver
   - File: `phase3/src/drivers/ahci.c`
   - Reference: OSDev Wiki AHCI guide + Linux `ahci.c`
   - Features: detect SATA devices, read sectors (READ DMA EXT), write sectors
   - Use command slot 0, single port (port 0)
   - HBA memory registers: CAP, GHC, PI, command list, FIS buffer
   - ~800-1200 LOC for minimal read/write

3. Implement block device interface
   - File: `phase3/src/drivers/blk_dev_x86.c`
   - Same interface as Phase 2 `blk_dev.c` — `blk_read()`, `blk_write()`
   - Abstracts AHCI behind common block API

4. Write test suite
   - 6+ tests: PCI detect, AHCI init, read sector, write sector, verify data, sequential read

**Deliverables:**
- PCI enumeration working
- AHCI read/write operational
- NVMe accessible for model file storage

**Effort:** 14-18 hours (2 weeks)
**Dependencies:** Week 13-14 complete
**Acceptance:** Read 1MB from NVMe, verify data integrity
**Risk:** AHCI complexity. Mitigation: start with PIO mode (simpler), upgrade to DMA later.

---

### Week 17-18: Network Driver (Intel/Realtek NIC)

> **STATUS:** ✅ RTL8168 driver written (7 tests) — but JARVIS PC has Intel I211-AT (X470-F Gaming), NOT RTL8168. Need igb-family driver for bare-metal. RTL8168 driver targets B550M-ITX/ac (main PC, not running seL4).

**Tasks:**
1. Identify NIC on JARVIS PC
   - ASUS X470-F Gaming has Intel I211-AT GbE (igb family)
   - Existing nic_rtl8168.c targets Realtek RTL8111H (B550M-ITX/ac, main PC) — won't work
   - Need new Intel I211-AT / igb-family driver for bare-metal seL4 on JARVIS PC

2. Implement NIC driver
   - File: `phase3/src/drivers/nic_x86.c`
   - Reference: OSDev Wiki Intel 8254x / Realtek 8139 guides, Linux driver source
   - Features: init, TX packet, RX packet (polled), link status
   - TX/RX descriptor rings (similar concept to GENET DMA rings from Phase 2)
   - ~800-1500 LOC depending on NIC chipset

3. Port network stack
   - Copy `net_stack.c` from Phase 2 (protocol layer is portable)
   - Replace GENET-specific TX/RX calls with new NIC driver calls
   - Copy `net_cmd.c` (shell commands: ping, ifconfig, netstat)

4. Write test suite
   - 6+ tests: NIC detect, link up, ARP send/recv, ICMP ping, TX/RX throughput

**Deliverables:**
- NIC driver operational (TX + RX)
- ARP + ICMP working
- `ping` command functional

**Effort:** 14-18 hours (2 weeks)
**Dependencies:** Week 15-16 complete (PCI enumeration needed)
**Acceptance:** Successful ping to local network gateway
**Risk:** NIC chipset uncertainty. ✅ RESOLVED: ASRock B550M-ITX/ac confirmed Realtek RTL8111H — matches existing nic_rtl8168.c driver.

**Week 18 Checkpoint:** x86 drivers operational (serial, timer, AHCI, NIC). seL4 bare metal with storage + networking.

---

## Month 4-5: AI Integration + IPC (Weeks 19-24)

### Focus: Compile ggml for seL4, Implement Shared Memory IPC

---

### Week 19-20: ggml Library Integration

> **STATUS:** ✅ SOFTWARE COMPLETE — Replaced ggml C++ backend entirely. Pure C implementations: tensor ops (14 tests), dequant Q4_0/Q8_0/F16 (23 tests), GGUF parser (12 tests), POSIX stubs, ggml integration (16 tests). All verified in QEMU self-test.

**Tasks:**
1. Research ggml build requirements for seL4
   - ggml is C/C++ with no mandatory external dependencies
   - Key POSIX dependencies to stub:
     - `mmap()` — used for model loading. Stub: pre-allocate large buffer, load model via `read()` from AHCI
     - `pthreads` — used for parallel inference. Options: single-threaded (simplest), or implement seL4 thread pool
     - `malloc()/free()` — provided by seL4's muslc libc
     - `stdio` (`fopen/fread`) — stub with AHCI block device reads
     - `time/clock` — map to x86 timer driver
   - Build with: `-DGGML_CUDA=OFF -DGGML_METAL=OFF -DGGML_VULKAN=OFF` (CPU only)

2. Build ggml as static library for seL4
   - Extract ggml core (ggml.c, ggml-alloc.c, ggml-backend.c, ggml-cpu.c)
   - Create `phase3/src/ai/CMakeLists.txt` linking ggml into seL4 build
   - Compile with seL4 cross-compiler and muslc
   - Fix compilation issues (missing headers, POSIX stubs)

3. Create POSIX stub layer
   - File: `phase3/src/ai/posix_stubs.c`
   - Implement minimal stubs for: `mmap`/`munmap` (no-op or buffer alloc), `pthread_create`/`join` (single-thread stub or seL4 threads), `fopen`/`fread`/`fclose` (AHCI block reads)
   - Only stub what ggml actually calls — don't implement full POSIX

4. Verify ggml compiles and initializes
   - Test: allocate ggml context, create tensor, run simple computation
   - This does NOT load a real model yet — just validates the library works

**Deliverables:**
- ggml compiling as seL4 userspace library
- POSIX stubs implemented
- Basic ggml tensor operations working in seL4

**Effort:** 14-18 hours (2 weeks)
**Dependencies:** Week 17-18 complete (AHCI needed for model loading)
**Acceptance:** ggml tensor multiply test passes in seL4 rootserver
**Risk:** ggml POSIX stubs may be complex. Mitigation: start with single-threaded CPU backend only. If ggml C++ is problematic, evaluate ggml's pure C subset.

---

### Week 21-22: AI Inference in seL4 Userspace

> **STATUS:** ✅ SOFTWARE COMPLETE — Full inference engine: Llama model architecture + weight loading (7 tests), transformer forward pass with GQA attention + SwiGLU FFN + RoPE + KV cache (9 tests), BPE tokenizer (12 tests), sampling greedy + top-k (9 tests), top-level inference API (4 tests). Tested with tiny synthetic model. Real GGUF model loading pending NVMe driver on real hardware.

**Tasks:**
1. Load GGUF model file from NVMe
   - Read model file via AHCI block device
   - Parse GGUF header and metadata
   - Allocate model weights in memory (from seL4 untyped or muslc heap)
   - Start with smallest model: Llama 3.2 1B Q4 (~700MB)

2. Run inference
   - Input: tokenized query string
   - Output: token-by-token generation
   - Measure: tokens/second on Ryzen 5 5600 (6 cores, but may start single-threaded)
   - Expected: ~20-50 tok/s for 1B Q4 (needs benchmarking)

3. Build inference request handler
   - File: `phase3/src/ai/inference_handler.c`
   - API: `inference_query(char* prompt, char* response, int max_tokens)`
   - Integrates with model loader and ggml context

4. Test suite
   - 4+ tests: model load, tokenization, single-token generation, full response generation

**Deliverables:**
- 1B model loaded and running inference in seL4 userspace
- Inference speed benchmarked
- Request handler API defined

**Effort:** 14-18 hours (2 weeks)
**Dependencies:** Week 19-20 complete
**Acceptance:** Llama 3.2 1B generates coherent responses in seL4 userspace
**Risk:** Model too large for available memory. Mitigation: start with 1B Q4 (~700MB), 16GB RAM has headroom.

---

### Week 23-24: Shared Memory IPC

> **STATUS:** ✅ SOFTWARE COMPLETE — Shared memory IPC (10 tests), 23.7M msg/sec on Linux. Integrated into rootserver behind `#ifdef JARVIS_IPC_SHMEM`. seL4 page mapping pending real hardware (swap mmap → seL4_Page_Map).

**Tasks:**
1. Design shared memory protocol
   - Replace UART framing (SYNC, CRC, length) with shared memory ring buffer
   - Keep same 14 message types: QUERY(0x01), RESPONSE(0x02), HEARTBEAT(0x03), etc.
   - New frame format (no SYNC/CRC needed — shared memory is reliable):
     ```
     ┌──────────┬──────────┬──────────┬──────────┐
     │   TYPE   │   SEQ    │  LENGTH  │ PAYLOAD  │
     │ (1 byte) │ (2 bytes)│ (2 bytes)│ (0-240)  │
     └──────────┴──────────┴──────────┴──────────┘
     ```
   - Ring buffer: 64 slots × 256 bytes = 16KB shared page
   - Doorbell: seL4 notification objects (wake on new message)

2. Implement shared memory IPC
   - File: `phase3/src/ipc/shmem_ipc.c`
   - seL4 shared page: `seL4_Untyped_Retype` for shared frame, map in both address spaces
   - Producer/consumer with atomic head/tail indices
   - Notification-based wakeup (not polling)

3. Connect rootserver ↔ AI process
   - Rootserver: receives QUERY, checks cache, forwards cache misses to AI process
   - AI process: receives inference request via shared memory, returns response
   - Rootserver: sends RESPONSE back to requestor (serial console for now)

4. Benchmark IPC latency
   - Measure: shared memory round-trip (write → notify → read → respond → notify → read)
   - Target: <1μs (seL4 native IPC is ~0.4μs)

5. Backward compatibility
   - `#ifdef JARVIS_IPC_UART` — Phase 2 UART transport
   - `#ifdef JARVIS_IPC_SHMEM` — Phase 3 shared memory transport
   - Same message types, different framing

**Deliverables:**
- Shared memory IPC working between rootserver and AI process
- IPC latency benchmark: <1μs
- End-to-end query pipeline: serial → rootserver → cache/AI → response

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Week 21-22 complete
**Acceptance:** 100 queries via shared memory IPC, 100% success, <1μs RTT

**Week 24 Checkpoint:** Standalone x86 operation — seL4 bare metal with cache + AI + shared memory IPC + drivers. This is the core milestone.

---

## Month 5-6: Integration + Stability (Weeks 25-28)

> **STATUS:** ✅ COMPLETE — Continuous IPC workload verified in QEMU (Apr 6, 2026). NVMe model loading, I211 NIC, IPC drain fix, ring overflow fix all done. 30-day stability test ready to start.

### Focus: End-to-End Validation, 30-Day Stability Test

---

### Week 25-26: Integration Testing

**Tasks:**
1. Full query pipeline test
   - Cache hit path: query → cache lookup → response (<1ms)
   - Cache miss path: query → AI inference → response (~50-200ms)
   - SHIELD check: query → risk scoring → allow/block
   - Shell command: query → command dispatch → driver → response

2. Stress testing
   - 1000+ sequential queries without error
   - Mixed workload: 70% cache, 15% shell, 10% heartbeat, 5% SHIELD
   - Memory stability: verify no leaks over 10,000+ queries

3. Build stability harness for x86
   - Port `stability_harness.py` concept to C (runs natively in seL4, or keep Python on separate host)
   - Option A: Self-test mode in rootserver (query itself, log to serial/AHCI)
   - Option B: External harness via serial (same as Phase 2 but from main PC)
   - Implement both for flexibility

4. Network integration test
   - Ping external hosts
   - ARP resolution
   - Run net_cmd shell commands

**Deliverables:**
- Full query pipeline working end-to-end
- 1000+ query stress test passing
- Stability harness ready for 30-day test

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Week 23-24 complete
**Acceptance:** 1000 queries: 100% success, 0 errors, no memory growth

---

### Week 27-28: 30-Day Stability Test (Start)

**Tasks:**
1. Start 30-day stability test
   - Run 24/7 automated test (1 query/second)
   - Command mix: 70% cache, 15% shell, 10% heartbeat, 5% SHIELD
   - Log to NVMe (daily rotation) or via serial to external host
   - Target: 0 crashes, <1% error rate, stable memory usage

2. Monitor first 48 hours closely
   - Watch for: memory leaks, hang events, driver failures
   - Hot-fix any critical issues discovered
   - If restart needed: document reason, reset counter

3. Create weekly monitoring script
   - Parse logs: daily pass/fail counts, RTT trends, memory usage
   - Alert on: crash, >1% error rate, RTT degradation

**Deliverables:**
- 30-day test started and running
- First 48 hours clean (no crashes, <1% errors)

**Effort:** 8-12 hours (2 weeks)
**Dependencies:** Week 25-26 complete
**Acceptance:** 48 hours stable, 99.7%+ pass rate

**Week 28 Checkpoint:** Standalone x86 JARVIS OS running 30-day stability test. Core Phase 3b objective achieved (pending test completion).

---

## Month 6-8: Stability Completion + Hardening (Weeks 29-36)

### Focus: 30-Day Test Monitoring, Security, Model Scaling

---

### Week 29-30: Stability Monitoring + Fuzz Testing Setup

> **STATUS:** ✅ FUZZ TESTING COMPLETE (Apr 6, 2026). 30-day stability test skipped — already proven on Pi 4 (30.6 days, same IPC/cache code). Fuzz harness: 300K iterations, 0 crashes, ASAN clean. Found and fixed div-by-zero in shmem_ipc.c.

**Tasks:**
1. ~~Monitor 30-day stability test (Day 8-14)~~ — SKIPPED (proven on Pi 4)

2. ✅ Build fuzz testing harness
   - File: `phase3/src/drivers/fuzz_harness.c`
   - Targets: net_stack (100K), shmem_ipc (100K), gguf_parser (100K)
   - Method: xorshift64 PRNG, self-contained, AddressSanitizer
   - Bug found: div-by-zero when ring->header.size corrupted to 0

3. ✅ Run initial fuzz tests
   - 100,000 malformed packets through net_stack — 0 crashes
   - 100,000 malformed IPC messages — 0 crashes, 2,025 CRC rejects
   - 100,000 malformed GGUF buffers — 0 crashes
   - All with `-fsanitize=address,undefined`

**Deliverables:**
- ~~Stability test Day 14+~~ — SKIPPED
- ✅ Fuzz harness operational (CI step added)
- ✅ Initial fuzz results: 300K iterations, 0 crashes

**Effort:** 12-16 hours (2 weeks) → Actual: ~2 hours (1 session)
**Dependencies:** Week 27-28 complete
**Acceptance:** ~~Stability >99.5%~~, ✅ 0 fuzz crashes

---

### Week 31-32: Security Audit (Phase 3)

**Tasks:**
1. Audit all new x86 driver code
   - PCI enumeration: bounds checking on config space reads
   - AHCI: DMA buffer validation, command slot bounds
   - NIC: packet length validation, descriptor ring bounds
   - Serial: buffer overflow checks
   - Review all `memcpy`, `snprintf`, pointer arithmetic

2. Audit shared memory IPC
   - Ring buffer: head/tail wraparound correctness
   - Message validation: type, length, payload bounds
   - Verify: no shared memory corruption between processes

3. Audit ggml integration
   - Model file parsing: validate GGUF header fields
   - Memory allocation: verify no unbounded allocations
   - Stub layer: verify POSIX stubs don't introduce vulnerabilities

4. Document findings
   - File: `phase3/docs/SECURITY_AUDIT_PHASE3.md`
   - Same format as Phase 2: severity, file, line, description, fix status

**Deliverables:**
- SECURITY_AUDIT_PHASE3.md written
- All HIGH/CRITICAL findings fixed
- GCC `-Werror` clean build

**Effort:** 10-14 hours (2 weeks)
**Dependencies:** Week 29-30 complete
**Acceptance:** 0 HIGH/CRITICAL unfixed, -Werror clean

---

### Week 33: Model Bench-Off — Baseline (F32 KV)

> **STATUS:** IN PROGRESS (April 9, 2026). Models selected. 1B + TinyLlama + Phi-3 already on disk. 3B + 8B downloading.

**Tasks:**
1. Download and verify 5 bench-off models on JARVIS PC NVMe
   - Llama 3.2 1B Q4_K_M (0.8GB) — IDLE tier, already on disk
   - TinyLlama 1.1B Q4_K_M (0.6GB) — IDLE comparison, already on disk
   - Phi-3 Mini 4k Q4 (2.3GB) — ACTIVE comparison, already on disk (may have arch issues)
   - Llama 3.2 3B Q4_K_M (~2GB) — ACTIVE tier, downloading
   - Llama 3.1 8B Q4_K_M (~5GB) — CRITICAL tier, downloading

2. Test each model in QEMU (verify load + coherent generation)

3. Build bench-off harness or run manual tests
   - Fixed prompt set (15-20 prompts: factual, reasoning, code, OS-domain)
   - Measure: tok/s, peak memory, response quality
   - All models at F32 KV cache (no compression) — this is the baseline

4. Document results in `phase3/docs/MODEL_BENCH_OFF_2026-04-07.md` as new section

**Deliverables:**
- 5-model baseline with tok/s and quality metrics
- Confirmed drop-in compatibility for each model
- Tier assignments validated by real numbers

**Effort:** 1-2 sessions
**Dependencies:** Week 31-32 complete (security audit done)
**Acceptance:** All 5 models load and generate coherent text; tok/s measured

---

### Week 34: TurboQuant Evaluation — Our Branch vs TurboQuant+

**Tasks:**
1. Deep-dive exploration of TurboQuant+ (https://github.com/TheTom/turboquant_plus)
   - Read their C/GGML integration code (if available)
   - Understand PolarQuant + Walsh-Hadamard rotation (O(d log d) vs our O(d²) Haar)
   - Understand boundary-layer protection (first/last 2 layers at higher precision)
   - Understand sparse V dequantization
   - Compare turbo3/turbo4 presets against our branch's compression configs

2. Rebase our TQ branch (`experiment/turboquant-benchmark`) onto current master
   - Resolve driver regressions documented in TURBOQUANT_EVALUATION.md §6
   - Preserve all AVX2 SIMD, PCI 64-bit BAR, NIC RX, AHCI real-init work from master
   - Run existing TQ test suite against rebased code

3. Decision: adopt TurboQuant+ approach or improve our branch?
   - If TQ+ has working C code: study as reference, implement asymmetric K/V in our engine
   - If TQ+ is still Python-only: use their findings (§12, §13) to guide our own implementation
   - Document decision rationale

**Deliverables:**
- TurboQuant+ technical assessment (what's real, what's vaporware)
- Our TQ branch rebased on current master (or decision to start fresh)
- Implementation plan for asymmetric K/V (§12) with boundary-layer protection

**Effort:** 2-3 sessions
**Dependencies:** Week 33 baseline complete (need F32 comparator)
**Acceptance:** Clear decision on TQ approach with supporting evidence

---

### Week 35: TurboQuant Implementation — Asymmetric K/V

**Tasks:**
1. Implement asymmetric K/V compression (§12 of TURBOQUANT_EVALUATION.md)
   - Q8_0 for K cache (preserve precision for dot products)
   - TurboQuant 3-4 bit for V cache (softmax-averaged, tolerates compression)
   - Boundary-layer protection (first/last 2 layers uncompressed)
   - ~460 LOC estimated (§12.4)

2. Test against 3B and 8B models (d=128, TQ-viable)
   - Three configurations: F32 KV (baseline), symmetric TQ, asymmetric Q8-K + TQ-V
   - Same prompt set as Week 33 baseline
   - Measure: quality match %, KV memory savings, tok/s impact

3. If asymmetric recovers d=64 quality: re-test with 1B (would unlock TQ for IDLE tier)

**Deliverables:**
- Asymmetric K/V working on d=128 models
- Three-way comparison (F32 vs symmetric vs asymmetric)
- Decision: which TQ config ships in the dynamic scaling system

**Effort:** 2-3 sessions
**Dependencies:** Week 34 TQ evaluation complete
**Acceptance:** Asymmetric TQ on 3B achieves >80% generation match with >4x KV compression

---

### Week 36: Wire Dynamic Model Scaling

**Tasks:**
1. Connect `model_scaling.c` state machine to real model hot-swap from NVMe
   - IDLE (1B) → ACTIVE (3B) → CRITICAL (8B) → EMERGENCY (rules-only)
   - Swap mechanism: Process A signals Process B to unload, reload new model from NVMe
   - Decision cache handles queries during swap (~30 seconds for 3B, ~60 seconds for 8B)
   - Transitions driven by query rate, error rate, and SHIELD risk score

2. Optionally integrate TQ compression into ACTIVE/CRITICAL tiers
   - If Week 35 TQ results are positive: enable TQ for 3B and 8B KV cache
   - If negative: ship with F32 KV, TQ deferred to Phase 4

3. Test model scaling end-to-end
   - State transitions, model load/unload timing
   - Continuous workload survives tier changes without errors
   - Stress test: rapid state changes under load

**Deliverables:**
- 4-state dynamic scaling operational on bare metal
- Model swap times measured
- Optional TQ compression integrated

**Effort:** 2-3 sessions
**Dependencies:** Week 33 baseline + Week 35 TQ results (if pursuing TQ)
**Acceptance:** All 4 states functional, smooth transitions, no crashes during swap

---

### Week 37-38: Enhanced SHIELD + Final Polish

**Tasks:**
1. Enhanced SHIELD (model-assisted risk scoring)
   - SHIELD risk score drives model state transitions
   - Risk > 0.7 → upgrade to CRITICAL (8B)
   - System threat → EMERGENCY (rules only)

2. Final stability verification on bare metal with all tiers

3. Code cleanup, dead code removal, documentation updates

**Deliverables:**
- SHIELD integrated with model scaling
- System stable across all tiers

**Effort:** 1-2 sessions
**Dependencies:** Week 36 complete
**Acceptance:** SHIELD-driven tier transitions working, hostile queries trigger CRITICAL/EMERGENCY

**Week 38 Checkpoint:** All Phase 3c work complete. Dynamic scaling operational. Ready for final report.

---

## Month 9-10: Finalization (Weeks 39-44)

### Focus: Final Report, Documentation, v0.3.0-beta Tag

---

### Week 39-40: Final Validation + Phase 2 ARM64 Maintenance (Optional)

**Tasks:**
1. Verify Phase 2 code still builds (if ARM fallback is needed)
   - Build Phase 2 kernel8.img in WSL
   - Run Phase 2 test suite: expect 108 PASS, 0 FAIL, 3 SKIP
   - Fix any regressions from shared code changes

2. Multi-platform build system (optional)
   - Shared code compiles for both ARM64 and x86-64
   - `#ifdef` guards for platform-specific drivers
   - Common CMakeLists.txt with platform selection

3. Document dual-platform build process (if maintained)
   - ARM64 build: existing Phase 2 WSL build script
   - x86-64 build: new Phase 3 build script
   - Verify both produce working binaries

**Deliverables:**
- Phase 2 ARM64 build verified (108 PASS)
- x86-64 build verified
- Shared codebase documented

**Effort:** 8-12 hours (2 weeks)
**Dependencies:** None (can be done anytime after Week 24)
**Acceptance:** Both ARM64 and x86-64 builds pass their respective test suites

---

### Week 39-40: 30-Day Stability Completion + Final Validation

**Tasks:**
1. Complete 30-day stability test
   - Final statistics: total tests, pass rate, crash count, RTT trends
   - Memory usage trend (detect leaks)
   - Root cause analysis for any downtime events

2. Final integration validation
   - Run full test suite: all tests must PASS
   - Run 1000+ sequential commands without error
   - Verify all drivers operational after 30-day run
   - Verify boot reliability: 10 consecutive cold reboots

3. Performance benchmarking (final numbers)
   - Boot time: cold and warm
   - IPC latency: shared memory round-trip
   - AI inference: tokens/second per model
   - Storage: NVMe read/write speeds
   - Network: TX/RX packet rates

**Deliverables:**
- 30-day stability test PASSED
- Final performance benchmarks documented
- All tests passing

**Effort:** 10-14 hours (2 weeks)
**Dependencies:** 30-day test running since Week 27-28
**Acceptance:** 30 days, 0 crashes, <1% errors, all benchmarks documented

---

### Week 41-42: Documentation + Code Cleanup

**Tasks:**
1. Code cleanup
   - Remove dead code, unused stubs
   - Normalize line endings (CRLF → LF)
   - Ensure all public APIs have header doc comments

2. Update project documentation
   - Update CLAUDE.md with Phase 3 completion status
   - Update JARVIS_UNIFIED_PLAN.md with Phase 3 actuals
   - Create PI4_AND_X86_PLATFORM_GUIDE.md (both platforms)

3. Archive Phase 3 weekly status docs

**Deliverables:**
- Codebase cleaned and documented
- All project docs updated
- Platform guide written

**Effort:** 8-12 hours (2 weeks)
**Dependencies:** Week 39-40 complete
**Acceptance:** Clean codebase, all docs current

---

### Week 43-44: Phase 3 Final Report + Phase 4 Planning

**Tasks:**
1. Write PHASE_3_FINAL_REPORT.md
   - Mirror Phase 2 report structure:
     - Executive Summary (GO/NO-GO for Phase 4)
     - Objectives vs Achievements
     - Technical Implementation (x86 port, ggml integration, IPC migration)
     - Test Results (unit tests + 30-day stability)
     - Performance Metrics (Phase 2 vs Phase 3 comparison)
     - Security Audit Results
     - Gate Criteria Assessment
     - Lessons Learned
     - Phase 4 Recommendations (GPU via Vulkan, multi-user, expanded drivers)

2. Write PHASE_4_KICKOFF.md (preliminary)
   - Phase 4 objectives:
     - GPU acceleration via Vulkan/CUDA compute (GPU TBD in Phase 4)
     - HDMI/display output
     - Multi-user support
     - External beta testing
     - 90-day stability test

3. Git tag: `v0.3.0-beta`
   - Final commit with all docs
   - Tag Phase 3 completion

**Deliverables:**
- PHASE_3_FINAL_REPORT.md complete
- PHASE_4_KICKOFF.md (preliminary)
- Git tag v0.3.0-beta
- Phase 3 COMPLETE

**Effort:** 10-14 hours (2 weeks)
**Dependencies:** Week 41-42 complete
**Acceptance:** All docs written, tag created

**Week 44 Checkpoint:** Phase 3 COMPLETE. Standalone JARVIS OS on x86 bare metal.

---

## Driver Implementation Schedule

### New x86 Drivers

| Driver | File | Est. LOC | Week | Reference | Notes |
|--------|------|----------|------|-----------|-------|
| 16550A UART | uart_16550.c | 200-400 | 13-14 | OSDev, Linux 8250.c | I/O port 0x3F8, simpler than PL011 |
| HPET/TSC Timer | x86_timer.c | 100-200 | 13-14 | OSDev HPET, Intel SDM | Microsecond resolution |
| PCI Enumeration | pci.c | 300-500 | 15-16 | OSDev PCI, Linux pci.c | Config space read/write |
| AHCI Storage | ahci.c | 800-1200 | 15-16 | OSDev AHCI, Linux ahci.c | Command list, FIS, DMA |
| Block Device | blk_dev_x86.c | 100-200 | 15-16 | Phase 2 blk_dev.c | Same interface, AHCI backend |
| NIC (Intel/Realtek) | nic_x86.c | 800-1500 | 17-18 | OSDev Intel/RTL, Linux | TX/RX descriptor rings |

**Total new driver code: ~2,300-4,000 LOC**

### Portable Code (Direct Copy from Phase 2)

| Module | File | LOC | Changes Needed |
|--------|------|-----|----------------|
| Decision Cache | decision_cache.c | ~600 | None |
| Cache Patterns | cache_patterns.c | ~200 | None |
| Ring Buffer | ring_buffer.c | ~400 | None |
| Net Stack (protocol) | net_stack.c | ~606 | Replace GENET calls with NIC driver |
| Net Commands | net_cmd.c | ~315 | None |
| safe_remaining() | (in net_cmd.c) | - | Already portable |

### New Phase 3 Modules

| Module | File | Est. LOC | Week | Notes |
|--------|------|----------|------|-------|
| POSIX Stubs | posix_stubs.c | 200-400 | 19-20 | mmap, pthreads, stdio stubs |
| ggml Integration | (library) | ~20,000 (ggml) | 19-20 | Compiled as static lib |
| Inference Handler | inference_handler.c | 300-500 | 21-22 | Model load + query API |
| Model Scaling | model_scaling.c | 300-500 | 33-34 | 4-state IDLE→EMERGENCY |
| Shared Memory IPC | shmem_ipc.c | 400-600 | 23-24 | Ring buffer + notifications |
| SHIELD (full) | shield.c | 500-800 | 35-36 | All 6 components |
| Fuzz Harness | fuzz_harness.c | 200-400 | 29-30 | Packet + IPC fuzzing |

---

## IPC Migration Plan

### Phase 2: UART Protocol

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  SYNC    │  TYPE    │   SEQ    │  LENGTH  │  FLAGS   │ PAYLOAD  │  CRC16   │
│  0xAA55  │  0x01-0E │  0-65535 │  0-240   │  0x00    │  data    │ CRC-CCITT│
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
10 bytes overhead per frame. CRC needed (UART is unreliable). SYNC needed (byte stream, no framing).
```

### Phase 3: Shared Memory Protocol

```
┌──────────┬──────────┬──────────┬──────────────────────┐
│   TYPE   │   SEQ    │  LENGTH  │       PAYLOAD        │
│ (1 byte) │ (2 bytes)│ (2 bytes)│      (0-240)         │
└──────────┴──────────┴──────────┴──────────────────────┘
5 bytes overhead per message. No CRC (shared memory is reliable). No SYNC (slot-based, not byte stream).
```

### Ring Buffer Design

```
Shared memory page (4KB):
┌────────────────────────────────────────────────┐
│ Header (64 bytes)                               │
│   write_idx: uint32_t (producer writes)         │
│   read_idx:  uint32_t (consumer reads)          │
│   size:      uint32_t (number of slots)         │
│   magic:     uint32_t (0xDEADBEEF)              │
│   padding:   48 bytes                           │
├────────────────────────────────────────────────┤
│ Slot 0  (256 bytes): type + seq + len + payload │
│ Slot 1  (256 bytes): ...                        │
│ ...                                             │
│ Slot 15 (256 bytes): ...                        │
└────────────────────────────────────────────────┘
16 slots × 256 bytes = 4,096 bytes (fits in one seL4 page)
```

### seL4 Shared Page Setup

```c
// In rootserver:
seL4_CPtr shared_frame = allocate_untyped(seL4_PageBits);
seL4_Untyped_Retype(shared_frame, seL4_X86_4K, ...);
// Map in rootserver VSpace
seL4_X86_Page_Map(shared_page, rootserver_vspace, SHMEM_VADDR, ...);
// Map same frame in AI process VSpace
seL4_X86_Page_Map(shared_page, ai_vspace, SHMEM_VADDR, ...);
// Create notification objects for doorbell
seL4_CPtr notify_rootserver = create_notification();
seL4_CPtr notify_ai = create_notification();
```

### Backward Compatibility

```c
#ifdef JARVIS_IPC_UART
  #include "uart_ipc.h"    // Phase 2 UART framing
#elif defined(JARVIS_IPC_SHMEM)
  #include "shmem_ipc.h"   // Phase 3 shared memory
#endif

// Common API:
int ipc_send(uint8_t type, uint16_t seq, const void* payload, uint16_t len);
int ipc_recv(uint8_t* type, uint16_t* seq, void* payload, uint16_t* len);
```

### Performance Comparison

| Metric | UART (Phase 2) | Shared Memory (Phase 3) | Improvement |
|--------|----------------|------------------------|-------------|
| RTT | 7ms | <1μs | 7,000x+ |
| Bandwidth | 11.5 KB/s | ~25 GB/s | 2,000,000x |
| Overhead/frame | 10 bytes | 5 bytes | 2x |
| Reliability | CRC required | Inherent | Simpler |
| Cache hit path | <1ms + 7ms = 8ms | <1ms + <1μs ≈ 1ms | 8x |
| Cache miss path | ~558ms + 7ms = 565ms | ~50-200ms + <1μs | 3-11x |

---

## AI Model Strategy

### Phase 3a Models (GPU on Linux Host — Stepping Stone)

| Model | VRAM | Tok/s (est.) | Use |
|-------|------|-------------|-----|
| Llama 3.2 1B Q4 | ~0.7 GB | ~200+ | Fast queries |
| Llama 3.2 3B Q4 | ~2.0 GB | ~125 | Standard |
| Phi-3 Mini 3.8B Q4 | ~2.5 GB | ~100 | Complex reasoning |
| Llama 7B Q4 | ~4.0 GB | ~75 | Most capable |

### Phase 3b Models (CPU on Bare Metal — Target)

| Model | RAM | Tok/s (est.) | Use | Notes |
|-------|-----|-------------|-----|-------|
| Llama 3.2 1B Q4 | ~0.7 GB | ~20-50 | IDLE + ACTIVE | Needs benchmarking on Ryzen |
| Llama 3.2 3B Q4 | ~2.0 GB | ~8-15 | ACTIVE + CRITICAL | Needs benchmarking |
| Phi-3 Mini 3.8B Q4 | ~2.5 GB | ~5-12 | CRITICAL | May be too slow for interactive |

**CPU inference note:** These are estimates. Actual performance depends on ggml optimizations (AVX2 on Ryzen), thread count, and memory bandwidth. Phase 3b Week 21-22 will produce real benchmarks.

**The decision cache is the key.** With 85%+ hit rate, only ~15% of queries need inference. Even at 5 tok/s, the user experience is dominated by cache hits (<1ms).

### Quantization Strategy

| Model | Full | Q4_K_M | Q8_0 | Fits 16GB? |
|-------|------|--------|------|------------|
| Llama 3.2 1B | 2 GB | 0.7 GB | 1.2 GB | Yes |
| Llama 3.2 3B | 6 GB | 2.0 GB | 3.5 GB | Yes |
| Phi-3 Mini 3.8B | 7.6 GB | 2.5 GB | 4.2 GB | Yes |
| Llama 7B | 14 GB | 4.0 GB | 7.5 GB | Yes (Q4) |

Q4_K_M recommended for Phase 3b — best balance of quality and memory efficiency.

---

## Testing Requirements

### Per-Milestone Gate Criteria

| Checkpoint | Week | Tests | Key Criteria |
|------------|------|-------|-------------|
| Phase 3a | 6 | - | 3-day stability 99.7%+, GPU benchmarks documented |
| seL4 x86 boot | 10 | 8+ | IPC benchmark <1μs, serial working |
| x86 drivers | 18 | 28+ | AHCI + NIC + serial + timer all operational |
| AI integration | 24 | 36+ | End-to-end query pipeline, shared memory IPC <1μs |
| Stability start | 28 | 40+ | 1000-query stress test passing |
| Security audit | 32 | 40+ | 0 HIGH unfixed, fuzz tests passing |
| Model scaling | 34 | 48+ | 4-state scaling operational |
| SHIELD | 36 | 56+ | All 6 components functional |
| Final validation | 40 | 60+ | 30-day stability PASSED, all benchmarks |
| Phase 3 complete | 44 | 60+ | Final report, git tag |

### Stability Test Criteria (from Phase 2, adapted for x86)

| Criterion | Target | Acceptable |
|-----------|--------|------------|
| Hard crashes | 0 | <3 with root cause |
| Error rate | <1% | <2% |
| IPC RTT P99 | <10μs | <100μs |
| Cache hit rate | >80% | >75% |
| Runtime | 30 days | 25+ days |
| Memory trend | Stable | <1% growth per day |

---

## Risk Register

### Technical Risks

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| 1 | AMD Ryzen PC99 seL4 boot fails | Low | High | Test on QEMU first (Week 7-8); debug IOAPIC/LAPIC; Intel NUC as last resort |
| 2 | ggml POSIX stubs too complex | Medium | High | Start single-threaded; stub only what's called; evaluate ggml pure C subset |
| 3 | AHCI driver complexity | Medium | Medium | Start with PIO mode; upgrade to DMA later; OSDev has reference code |
| 4 | NIC chipset mismatch | Low | ✅ RESOLVED | JARVIS PC (X470-F) has Intel I211-AT — need igb driver, not RTL8168 |
| 5 | CPU inference too slow | Low | Medium | Cache handles 85%+; 1B at ~20 tok/s is usable; optimize later |
| 6 | 16GB RAM insufficient | Low | ✅ RESOLVED | Upgraded to 32GB DDR4 — plenty of headroom |
| 7 | Stability regression on new platform | Medium | Medium | Phase 2 Pi 4 code preserved; x86 stability test pending |
| 8 | seL4 x86 debugging difficult | Medium | Medium | Serial console from Day 1; QEMU for initial development |
| 9 | Hardware blocker (PC availability) | Low | ✅ RESOLVED | JARVIS PC dedicated and wipeable, CPU-only inference |
| 10 | Model file loading from NVMe | Low | Medium | AHCI driver needed first; start with RAM-loaded models during dev |

### Non-Technical Risks

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| 11 | Solo dev burnout | Medium | High | 8-12 hrs/week pace; Phase 3a provides quick wins |
| 12 | Scope creep | Medium | Medium | Clear staged milestones; defer GPU to Phase 4 |
| 13 | Hardware failure | Low | Medium | NVMe replaceable; Phase 2 Pi 4 code preserved as ARM fallback |

---

## Total Phase 3 Effort Estimate

| Subphase | Weeks | Hours/Week | Total Hours |
|----------|-------|------------|-------------|
| Phase 3a (GPU host) | 6 | 8-10 | 48-60 |
| Phase 3b (x86 bare metal) | 22 | 10-14 | 220-308 |
| Phase 3c (hardening) | 12 | 8-12 | 96-144 |
| Documentation + report | 4 | 8-10 | 32-40 |
| **Total** | **44** | **~10 avg** | **396-552** |

**Conservative estimate: ~500 hours** (44 weeks × ~11 hours/week average)

**Comparison:**
- Phase 1 actual: 286 hours (26 weeks)
- Phase 2 actual: ~230 hours (23 active weeks)
- Phase 3 estimate: ~500 hours (44 weeks) — x86 bare metal + ggml integration is significantly harder

---

## Appendix A: Code Portability Matrix

Every Phase 2 source file and its Phase 3 disposition:

| Phase 2 File | LOC | Disposition | Phase 3 Target |
|-------------|-----|-------------|----------------|
| sel4/main_arm64.c | 3,662 | **PORT** | sel4/main_x86.c — new init, keep IPC loop |
| drivers/uart_pl011.c | 1,244 | **REPLACE** | drivers/uart_16550.c |
| drivers/emmc_sdhci.c | 1,547 | **REPLACE** | drivers/ahci.c |
| drivers/bcm2711_timer.c | 67 | **REPLACE** | drivers/x86_timer.c |
| drivers/bcm_genet.c | 1,134 | **REPLACE** | drivers/nic_x86.c |
| drivers/net_stack.c | 606 | **COPY** | drivers/net_stack.c (change NIC calls) |
| drivers/net_cmd.c | 315 | **COPY** | drivers/net_cmd.c |
| drivers/usb_hid.c | 1,393 | **DEFER** | Phase 4 (xHCI is complex) |
| drivers/bcm_gpio.c | 234 | **DROP** | Not needed on x86 |
| drivers/bcm_i2c.c | 404 | **DROP** | Not needed on x86 |
| drivers/bcm_spi.c | ~300 | **DROP** | Not needed on x86 |
| drivers/bcm_rng.c | ~200 | **REPLACE** | Use RDRAND instruction |
| drivers/bcm_pwm.c | ~300 | **DROP** | Not needed on x86 |
| drivers/bcm_dma.c | ~400 | **DROP** | AHCI has own DMA |
| drivers/bcm_watchdog.c | ~200 | **DEFER** | Use x86 watchdog (TCO) later |
| drivers/bcm_thermal.c | ~300 | **DEFER** | Use ACPI thermal later |
| drivers/bcm_power.c | ~200 | **REPLACE** | x86 ACPI power states |
| drivers/slot_alloc.c | ~200 | **COPY** | Shared slot tracking |
| drivers/dma_alloc.c | ~300 | **ADAPT** | May not need separate DMA allocator |
| drivers/blk_dev.c | ~200 | **COPY** | Same interface, AHCI backend |
| ipc/dual_ring_buffer.c | ~400 | **COPY** | Phase 2 UART mode preserved |
| ipc/ipc_handler.c | ~500 | **ADAPT** | Add shared memory transport |
| boot/fdt_parser.c | ~500 | **DROP** | No device tree on x86 |
| boot/boot_manager.c | ~300 | **ADAPT** | x86 boot timing |
| boot/warm_reboot.c | ~300 | **ADAPT** | x86 reboot mechanism |
| cache/decision_cache.c | ~600 | **COPY** | Identical |
| cache/cache_patterns.c | ~200 | **COPY** | Identical |

**Summary:** 7 COPY, 5 REPLACE, 4 ADAPT, 3 PORT, 5 DROP, 3 DEFER = 27 files assessed

---

## Appendix B: x86-64 Memory Map (PC99)

Unlike the Pi 4's fixed BCM2711 peripheral addresses, x86 peripherals are discovered dynamically via PCI enumeration and ACPI. Key fixed resources:

| Resource | Address / Port | Access Method |
|----------|---------------|---------------|
| COM1 (16550A) | I/O 0x3F8-0x3FF | x86 IN/OUT instructions |
| COM2 (16550A) | I/O 0x2F8-0x2FF | x86 IN/OUT instructions |
| PCI Config | I/O 0xCF8-0xCFF | Config address + data ports |
| HPET Timer | MMIO (from ACPI) | Memory-mapped, ~10 MHz |
| LAPIC | 0xFEE00000 | Memory-mapped, per-CPU |
| IOAPIC | 0xFEC00000 | Memory-mapped, IRQ routing |
| AHCI HBA | PCI BAR5 (varies) | Memory-mapped, discovered via PCI |
| NIC | PCI BAR0 (varies) | Memory-mapped, discovered via PCI |
| NVMe | PCI BAR0 (varies) | Memory-mapped, discovered via PCI |

**Key difference from Pi 4:** No hardcoded peripheral addresses. Everything discovered via PCI bus scan (class codes identify device types) or ACPI tables. The seL4 PC99 platform support may handle some of this.

---

## Appendix C: Stability Test Plan (x86)

Adapted from Phase 2 Appendix C.

**Infrastructure:**
- Self-test mode: rootserver queries itself, logs to NVMe via AHCI
- External mode: serial harness from main PC (backup)

**Test Categories:**
| Category | Mix % | Description |
|----------|-------|-------------|
| Cache queries | 70% | Random from 258 patterns, verify response |
| Shell commands | 15% | ping, ifconfig, stats, cache, shield |
| Heartbeat | 10% | IPC latency measurement |
| SHIELD checks | 5% | Risk assessment, verify block rate |

**Success Criteria:**
- 0 crashes over 30 days (acceptable: <3 with root cause)
- <1% error rate
- IPC RTT P99 < 10μs
- No memory leaks (stable usage trend)
- Cache hit rate >80%

---

## Appendix D: Hardware Cost Summary

| Phase | Items | Cost (AUD) |
|-------|-------|-----------|
| Phase 2 | Pi 4 8GB, SD card, power supply, cables, case | $215.82 |
| Phase 3 | JARVIS Project PC (already owned), Optional 32GB DDR4 | $0-50 |
| **Total** | | **$215.82 - $265.82** |

---

**Phase 3 Implementation Plan Complete**

---

*Plan Date: March 18, 2026*
*Phase 2 Completion: March 18, 2026*
*Phase 3 Start: JARVIS PC ready for bare-metal (April 2026)*
*Duration: 40-44 weeks (~10 months at 8-12 hrs/week)*
*Hardware: JARVIS PC (Ryzen 7 2700X, R9 280X display, 16GB DDR4, X470-F Gaming, 1TB NVMe)*
*Target: Pure bare-metal seL4 x86-64 + native ggml AI inference*
*Budget: $0-50 AUD additional*
*Author: JARVIS Development Team (Solo Developer)*
