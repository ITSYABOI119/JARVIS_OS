# JARVIS AI-OS: Phase 3 Implementation Plan

**Version:** 1.0
**Date:** March 18, 2026
**Phase:** Phase 3 - Beta System (Months 24-36)
**Duration:** 40-44 weeks (~10 months at 8-12 hours/week)
**Status:** PRE-WORK COMPLETE, awaiting spare PC for Phase 3a

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

## CURRENT STATUS (March 19, 2026)

**Phase 2 COMPLETE** — GO recommendation for Phase 3
**Pre-Work:** ✅ COMPLETE — all 8 interim tasks done (see Pre-Work section below)

### Phase 2 Baseline (What Carries Forward)

| Metric | Phase 2 Result | Phase 3 Target |
|--------|----------------|----------------|
| Platform | Pi 4 ARM64 + host PC | x86-64 standalone |
| Drivers | 21 operational | x86 equivalents |
| Stability | 30.6 days, 0 crashes | 30 days on x86 |
| IPC latency | 7ms (UART) | <1μs (shared memory) |
| AI inference | 558ms Phi-3 CPU / <100ms 1B CPU | Native C in seL4 userspace |
| Cache hit rate | 87.5% | >80% |
| Tests | 108 PASS, 0 FAIL | 120+ PASS |
| LOC | 27,029 (65 files) | Growing |
| Security | 6 findings, 4 fixed | Fuzz testing + expanded audit |
| Hardware cost | $215.82 AUD | +$0-50 |

### Hardware Status

| Hardware | RAM | Status | Phase 3 Role |
|----------|-----|--------|--------------|
| Raspberry Pi 4 8GB | 8GB | Running Phase 2 | Fallback through Phase 3a |
| Raspberry Pi 5 4GB | 4GB | Owned, unused | Available for experiments |
| Spare x86 PC | 16GB DDR4 | **PARTS ORDERING IN PROGRESS** | Phase 3a host → Phase 3b target |
| Main PC (RTX 2070) | 32GB | Daily driver, cannot wipe | Development + cross-compilation |

**Spare PC Build Progress (ordering incrementally):**

| Part | Model | Price (AUD) | Status |
|------|-------|-------------|--------|
| RAM | G.Skill Ripjaws V 16GB DDR4-3200 | $179 | **BOUGHT** (Mwave) |
| SSD | Netac NV3000 500GB NVMe | $110 | Pending (Amazon AU) |
| PSU | Corsair RM650e 650W | $125 | Pending (Amazon AU) |
| Mobo | Gigabyte B550M K | $109 | Pending (MSY/Centre Com) |
| CPU | AMD Ryzen 5 5600 | $176 | Pending (Centre Com) |
| GPU | MSI RTX 3060 12GB | $429 | Pending (Centre Com) |
| Case | Antec NX200M | $49 | Pending (MSY) |
| **Total** | | **~$1,177** | **1/7 bought** |

**Note:** DDR4 prices inflated 150-200% due to global DRAM shortage (AI demand). RAM bought early to avoid further increases. Recovery not expected until late 2027.

**Phase 3 week numbering starts when the spare PC is assembled.** Pre-work tasks happen before Week 1 using current hardware (main PC WSL, Pi 4, Pi 5).

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

```
┌──────────────────┐       UART        ┌──────────────────┐
│   Spare x86 PC   │◄─────────────────►│   Pi 4 (seL4)    │
│   (Linux + CUDA) │   7ms RTT         │   (Unchanged)    │
│                  │                   │                  │
│   RTX 3060 AI    │                   │   Decision Cache  │
│   75+ tok/s      │                   │   21 drivers      │
└──────────────────┘                   └──────────────────┘
     Dedicated, wipeable                    Proven stable
```

Same as Phase 2 architecture but with dedicated GPU host. Pi 4 code untouched.

### Phase 3b: Pure Bare-Metal x86 (Weeks 7-28) — TARGET

```
┌─────────────────────────────────────────────────────────┐
│              JARVIS OS (Spare x86 PC)                    │
│            Ryzen 5 5600 + RTX 3060 12GB                 │
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
| Machines | 2 (Pi 4 + PC) | 2 (Pi 4 + spare PC) | **1 (spare PC)** |
| Microkernel | seL4 ARM64 | seL4 ARM64 | **seL4 x86-64** |
| AI runtime | Python (host PC) | Python + CUDA | **Native C (seL4 userspace)** |
| IPC | UART 7ms | UART 7ms | **Shared memory <1μs** |
| AI speed | 558ms (CPU) | ~13ms/tok (GPU) | **~50-100ms (CPU, 1B-3B)** |
| GPU | None (on Pi 4) | RTX 3060 CUDA | None (Phase 4) |
| Linux | Required (host) | Required (host) | **None** |
| Standalone | No | No | **Yes** |

---

## Phase 3 Pre-Work: Interim Tasks ✅ COMPLETE

### Focus: De-Risk Phase 3b Unknowns Using Current Hardware

**Objective:** While the spare x86 PC is not yet assembled, advance Phase 3 using the main PC (WSL), Pi 4, and Pi 5. These tasks target the three biggest Phase 3b risks: seL4 x86 build environment, ggml portability to seL4/musl, and shared memory IPC design. Completing all Tier 1 tasks saves an estimated 4-6 weeks of Phase 3b calendar time.

**Hardware used:** Main PC (RTX 2070, 32GB, WSL), Pi 4 (running Phase 2), Pi 5 (available)

**Note:** Phase 3 week numbering (Week 1+) starts when the spare PC is assembled. These pre-work tasks happen before Week 1.

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
**Unblocks:** Phase 3b Weeks 23-24. When spare PC arrives, swap `mmap` → `seL4_Page_Map` and `sem_post` → `seL4_Signal`. Ring buffer logic is identical.

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
3. When spare PC arrives, boot same rootserver on real hardware — code already works

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
Pre-Work Tasks (before spare PC):
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

────────────── SPARE PC ASSEMBLED ──────────────
    ↓
  Week 1: Phase 3a starts (GPU host + Pi 4 UART)
    ↓
  Week 7: Phase 3b starts — most unknowns already resolved
```

### Early Phase 3b Work (Done Ahead of Schedule)

While waiting for the spare PC, the majority of Phase 3b implementation was completed in QEMU and with mock testing. This reduces Phase 3b timeline by an estimated 8-12 weeks.

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
| Custom x86 rootserver | 9-12 | ✅ DONE (QEMU) | main_x86.c | Boots |

**Total Phase 3 code:** 69 files, 18,344 LOC, 159 tests (all passing), 29 CI steps

**What remains for Phase 3b on real hardware:**
- Boot seL4 on actual Ryzen hardware (vs QEMU)
- AHCI full read/write against real NVMe (mock-tested, real I/O pending)
- NIC driver TX/RX against real NIC (skeleton done, real I/O pending)
- Load real GGUF model from NVMe and run inference end-to-end
- Extract tokenizer vocab from GGUF metadata (currently manual init)
- QEMU rootserver rebuild with latest code
- 30-day stability test on x86

---

## Month 1-2: Phase 3a — GPU-Accelerated Host (Weeks 1-6)

### Focus: Replace Main PC with Dedicated Spare PC

**Objective:** Move AI inference from the main PC (cannot wipe) to the spare PC (dedicated, wipeable) with RTX 3060 GPU acceleration. Pi 4 continues running Phase 2 code unchanged. Zero code changes, zero risk.

---

### Week 1: Spare PC Assembly + Linux Setup

**Tasks:**
1. Assemble spare PC hardware
   - Install Ryzen 5 5600, 16GB DDR4, RTX 3060, NVMe SSD
   - Verify POST, BIOS settings (enable IOMMU, disable secure boot for later seL4 use)
   - Note: Keep UEFI boot mode (not legacy BIOS) — seL4 x86-64 uses EFI

2. Install Linux
   - Ubuntu 22.04 LTS (well-tested NVIDIA driver support)
   - Partition: 100GB Linux, rest unformatted (reserved for seL4 bare metal later)
   - Install build essentials: `gcc`, `cmake`, `ninja-build`, `git`

3. Install NVIDIA drivers + CUDA
   - NVIDIA driver 535+ (proprietary, not nouveau)
   - CUDA toolkit 12.x
   - Verify: `nvidia-smi` shows RTX 3060, `nvcc --version` shows CUDA

**Deliverables:**
- Spare PC assembled and booting Linux
- NVIDIA driver + CUDA working
- `nvidia-smi` output documented

**Effort:** 6-8 hours
**Dependencies:** Spare PC parts available
**Acceptance:** `nvidia-smi` shows RTX 3060 12GB, CUDA 12.x installed

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

### Week 3: Python Environment + UART Connection

**Tasks:**
1. Set up Python environment on spare PC
   - Python 3.10+, pyserial, numpy
   - Copy Phase 2 Python files: `uart_ipc_client.py`, `system_bootstrap.py`, `stability_harness.py`, `power_manager.py`, `local_ai_handler.py`

2. Connect USB-serial adapter to Pi 4
   - GPIO14 (TXD) → RXD, GPIO15 (RXD) → TXD, GND → GND
   - Verify serial port (`/dev/ttyUSB0` or similar)

3. Test UART IPC communication
   - Run `uart_ipc_client.py` — verify QUERY/RESPONSE working
   - Run heartbeat test — verify RTT ~7ms
   - Run 100-query quick test — verify 100% success rate

**Deliverables:**
- Python environment working on spare PC
- UART IPC to Pi 4 verified

**Effort:** 4-6 hours
**Dependencies:** Week 1 complete, Pi 4 running
**Acceptance:** 100-query test: 100% success, RTT ~7ms

---

### Week 4: Stability Testing (Spare PC Host)

**Tasks:**
1. Run 1-hour stability test
   - `python stability_harness.py --port /dev/ttyUSB0 --duration 60 --log-dir stability_logs_phase3a --verbose`
   - Target: 99.7%+ pass rate (matching Phase 2)

2. Run 3-day stability test
   - `python stability_harness.py --port /dev/ttyUSB0 --duration 4320 --log-dir stability_logs_phase3a --verbose`
   - Monitor: pass rate, RTT, cache hit rate, hang events

3. Benchmark GPU-accelerated cache miss path
   - Measure: query → cache miss → GPU inference → response time
   - Compare with Phase 2 CPU-only inference time

**Deliverables:**
- 1-hour test results
- 3-day test started (completes in Week 5)

**Effort:** 4-6 hours
**Dependencies:** Week 3 complete
**Acceptance:** 1-hour test: 99.7%+ pass, 0 failures

---

### Week 5-6: Phase 3a Validation + Documentation

**Tasks:**
1. Complete 3-day stability test (Week 5)
   - Analyze results: pass rate, RTT trends, hang events
   - Compare with Phase 2 3-day test results

2. Document Phase 3a results (Week 5)
   - Create `phase3/weeks/week5/WEEK_5_STATUS.md`
   - Benchmark comparison table: Phase 2 vs Phase 3a
   - GPU inference latency measurements

3. Phase 3a exit validation (Week 6)
   - Verify all Phase 3a exit criteria met
   - Go/no-go decision for Phase 3b
   - Create `phase3/docs/PHASE_3A_RESULTS.md`

4. Prepare Phase 3b environment (Week 6)
   - Install seL4 build dependencies on spare PC (or main PC for cross-compilation)
   - Clone seL4 repositories: kernel, seL4_tools, muslc, sel4runtime, sel4_libs
   - Verify x86-64 PC99 platform builds (on QEMU first)

**Deliverables:**
- 3-day stability test PASSED
- PHASE_3A_RESULTS.md written
- Phase 3b build environment ready

**Effort:** 10-14 hours (2 weeks)
**Dependencies:** Week 4 complete
**Acceptance:** 3-day stability 99.7%+, seL4 x86-64 builds for QEMU

**Phase 3a Checkpoint:** Spare PC operational, GPU AI validated, UART IPC stable, seL4 build env ready

---

## Month 2-3: seL4 x86-64 Bootstrap (Weeks 7-12)

### Focus: Boot seL4 on Real x86 Hardware, Port Core Logic

---

### Week 7-8: seL4 x86-64 Build Environment + QEMU Validation

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
   - Serial console setup (USB-serial adapter to spare PC's COM port or USB debug)

**Deliverables:**
- seL4 x86-64 rootserver building and running on QEMU
- Boot method for real hardware identified

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Phase 3a complete
**Acceptance:** "Hello from JARVIS x86" on QEMU serial console

---

### Week 9-10: Boot seL4 on Spare PC (Real Hardware)

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
**Dependencies:** Week 7-8 complete, spare PC assembled
**Acceptance:** seL4 rootserver running on real hardware, IPC benchmark completed
**Risk:** AMD Ryzen PC99 compatibility. Mitigation: test on QEMU first, debug IOAPIC/LAPIC issues.

---

### Week 11-12: Port Decision Cache + SHIELD to x86

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

**Tasks:**
1. Identify NIC on spare PC
   - PCI scan for Ethernet controller
   - Likely: Intel I225-V (2.5GbE) or Realtek RTL8111/8125 (common on B550 boards)
   - Check MSI B550M specifications for exact NIC chipset

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
**Risk:** NIC chipset uncertainty. Mitigation: check B550M specs before ordering, or use USB Ethernet as fallback.

**Week 18 Checkpoint:** x86 drivers operational (serial, timer, AHCI, NIC). seL4 bare metal with storage + networking.

---

## Month 4-5: AI Integration + IPC (Weeks 19-24)

### Focus: Compile ggml for seL4, Implement Shared Memory IPC

---

### Week 19-20: ggml Library Integration

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

**Tasks:**
1. Monitor 30-day stability test (Day 8-14)
   - Daily log review: error rate, RTT trends, memory
   - Weekly summary report
   - Fix any bugs discovered (rebuild, redeploy, restart test if needed)

2. Build fuzz testing harness
   - File: `phase3/src/drivers/fuzz_harness.c`
   - Targets: network packet parsing (IP, ARP, ICMP), IPC message parsing
   - Method: generate random/malformed inputs, verify no crashes
   - Use: AFL-style mutation or manual malformed packet generator

3. Run initial fuzz tests
   - 10,000+ malformed packets through net_stack
   - 10,000+ malformed IPC messages through ipc_handler
   - Verify: no crashes, no buffer overflows, no undefined behavior

**Deliverables:**
- Stability test Day 14+ (no critical issues)
- Fuzz harness operational
- Initial fuzz results documented

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Week 27-28 complete
**Acceptance:** Stability >99.5%, 0 fuzz crashes

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

### Week 33-34: Dynamic Model Scaling

**Tasks:**
1. Implement 4-state model scaling
   - File: `phase3/src/ai/model_scaling.c`
   - States: IDLE → ACTIVE → CRITICAL → EMERGENCY
   - IDLE: 1B model loaded (fastest, ~20-50 tok/s)
   - ACTIVE: 3B model loaded (~8-15 tok/s)
   - CRITICAL: 3.8B model + validation (~5-12 tok/s, double-check responses)
   - EMERGENCY: rules-only, no AI inference (<1ms)
   - Transitions: based on SHIELD risk score (0-1.0)

2. Implement model hot-swap
   - Unload current model, load new model from NVMe
   - Swap time: 1-3 seconds (load ~700MB-2.5GB from NVMe to RAM)
   - Decision cache handles queries during swap (85%+ coverage)

3. Test model scaling
   - 4+ tests: state transitions, model load/unload, response quality per state
   - Stress test: rapid state changes under load

**Deliverables:**
- 4-state model scaling operational
- Model swap time measured and documented

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Week 21-22 complete (AI inference working)
**Acceptance:** All 4 states functional, smooth transitions, no crashes during swap

---

### Week 35-36: Enhanced SHIELD Safety Framework

**Tasks:**
1. Implement full SHIELD components (from ARCHITECTURE_ENHANCEMENTS.md)
   - **S**taged execution: sandbox new/risky operations before committing
   - **H**euristic impact analysis: estimate impact of AI decisions
   - **I**nterrupt capability: cancel dangerous operations mid-execution
   - **E**nsemble validation: secondary model validates CRITICAL decisions
   - **L**earning from failures: log failed/blocked actions for pattern recognition
   - **D**eterministic rollback: undo operations that fail validation

2. Integrate with model scaling
   - SHIELD risk score drives model state transitions
   - Risk > 0.7 → upgrade to CRITICAL (larger model + validator)
   - System threat → EMERGENCY (rules only)

3. Test suite
   - 8+ tests: staged execution, impact analysis, interrupt, ensemble, learning, rollback

**Deliverables:**
- Full 6-component SHIELD operational
- Integrated with model scaling
- 8+ tests passing

**Effort:** 12-16 hours (2 weeks)
**Dependencies:** Week 33-34 complete
**Acceptance:** All 6 SHIELD components functional, hostile queries blocked, 0% false positives

**Week 36 Checkpoint:** 30-day stability test completing (or complete), security audit done, model scaling + SHIELD operational.

---

## Month 8-10: Polish + Finalization (Weeks 37-44)

### Focus: Multi-Platform Maintenance, Documentation, Final Report

---

### Week 37-38: Pi 4 ARM64 Build Maintenance

**Tasks:**
1. Verify Phase 2 code still builds
   - Build Phase 2 kernel8.img in WSL
   - Run Phase 2 test suite: expect 108 PASS, 0 FAIL, 3 SKIP
   - Fix any regressions from shared code changes

2. Multi-platform build system
   - Shared code compiles for both ARM64 and x86-64
   - `#ifdef` guards for platform-specific drivers
   - Common CMakeLists.txt with platform selection

3. Document dual-platform build process
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
     - GPU acceleration via Vulkan compute (RTX 3060)
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
| 4 | NIC chipset unknown until board inspected | Low | Medium | Check B550M specs; USB Ethernet as fallback |
| 5 | CPU inference too slow | Low | Medium | Cache handles 85%+; 1B at ~20 tok/s is usable; optimize later |
| 6 | 16GB RAM insufficient | Low | Medium | 1B Q4 needs 0.7GB + seL4 <1GB; plenty of headroom; upgrade to 32GB (~$50) |
| 7 | Stability regression on new platform | Medium | Medium | Pi 4 stays as fallback; staged migration |
| 8 | seL4 x86 debugging difficult | Medium | Medium | Serial console from Day 1; QEMU for initial development |
| 9 | Spare PC not assembled (blocks Phase 3) | Medium | High | Phase 3a can temporarily use main PC; Pi 4 keeps running |
| 10 | Model file loading from NVMe | Low | Medium | AHCI driver needed first; start with RAM-loaded models during dev |

### Non-Technical Risks

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| 11 | Solo dev burnout | Medium | High | 8-12 hrs/week pace; Phase 3a provides quick wins |
| 12 | Scope creep | Medium | Medium | Clear staged milestones; defer GPU to Phase 4 |
| 13 | Hardware failure | Low | Medium | Pi 4 fallback; NVMe replaceable |

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
| Phase 3 | Spare PC (already owned), Optional 32GB DDR4 | $0-50 |
| **Total** | | **$215.82 - $265.82** |

---

**Phase 3 Implementation Plan Complete**

---

*Plan Date: March 18, 2026*
*Phase 2 Completion: March 18, 2026*
*Phase 3 Start: Pending spare PC assembly*
*Duration: 40-44 weeks (~10 months at 8-12 hrs/week)*
*Hardware: Spare x86 PC (Ryzen 5 5600, RTX 3060 12GB, 16GB DDR4)*
*Target: Pure bare-metal seL4 x86-64 + native ggml AI inference*
*Budget: $0-50 AUD additional*
*Author: JARVIS Development Team (Solo Developer)*
