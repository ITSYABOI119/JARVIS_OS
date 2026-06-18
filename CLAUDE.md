# CLAUDE.md

Guidance for Claude Code when working with this repository.

---

## Project Overview

**JARVIS AI-OS** - An AI-controlled operating system using seL4 microkernel with autonomous decision-making. AI runs at Ring 3 (50-500ms decisions), microkernel runs at Ring 0 (<1ms interrupts). 36-month timeline.

| Phase | Status | Period | Summary |
|-------|--------|--------|---------|
| Phase 0 | COMPLETE | Months 1-6 | Validation (80% success, GO decision) |
| Phase 1 | COMPLETE | Months 6-12 | PoC on x86 QEMU (26/26 weeks, Dec 2025) |
| Phase 2 | COMPLETE | Months 12-24 | Alpha on Pi 4 bare metal (21 drivers, 30-day stability) |
| **Phase 3** | **COMPLETE (beta)** | Months 24-36 | Beta on x86-64 bare metal (**LLM inference on seL4 VERIFIED**) — v0.2.1-beta tagged (30-day soak deferred) |
| Phase 4 | IN PROGRESS | Months 36+ | Production v1.0 |

**Current:** Phase 4 started (2026-06-16). Phase 3 CLOSED — v0.2.1-beta tagged @ 06de75c (2026-06-16). **MILESTONE: JARVIS engine bench-off COMPLETE — 11/11 models load and generate across 6 model families (Llama, Gemma 4, Phi-3, Mistral, Qwen3, Qwen3.5 DeltaNet SSM).** Gemma 4 E2B (#1 quality, 8.40/10) validated on seL4 QEMU. Fused QKV/gate-up support unlocked Phi-3. Gated DeltaNet SSM (~1200 LOC) unlocked Qwen3.5 hybrid architecture. JARVIS engine speed: 3.22 tok/s (1T), 19.79 tok/s (16T) — 2x gap to llama.cpp (down from 40x). Fused qdot + SIMD attention + RoPE tables + pthread. Phase 3b complete (bare-metal boot, NVMe model loading, IPC workload, I211 NIC). Phase 3c hardening done (fuzz testing, security audit 25 findings). NVMe persistent logging operational — auto-captures all serial output with [VGA]/[SER]/[PB] tags. Bare-metal **burn-in passed** (2026-06-15 — hours, ~400 queries, err=0; NOT a 30-day soak): x86 boot/model-load/coherent gen, heartbeat/shield IPC fix err=0 over 400 real-hardware queries, durable NVMe log (boot_id constant). Formal 30-day x86 soak **DEFERRED — descoped from v0.2.1-beta gating (risk-accepted, ADR `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`)** — next: Phase 4 perf (AVX2/threading); TurboQuant/RotorQuant evaluated → deferred to Phase 4 (ADR `docs/decisions/2026-06-15-defer-turboquant-rotorquant-to-phase4.md`). **Phase 3 final report written (`phase3/docs/PHASE_3_FINAL_REPORT.md`); v0.2.1-beta TAGGED @ 06de75c (2026-06-16).** Two-PC setup: Main PC (5600/2070/32GB) for dev, JARVIS PC (2700X/280X/32GB/2TB NVMe) running bare-metal seL4. Phase 4 goal #1 reframed to **Inference performance** — v1.0 = CPU AVX2+threading in the seL4 build (~8–9 tok/s Gemma E2B @16T target); GPU inference DEFERRED (no usable GPU — ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`); GPU display/UI (goal #2) unaffected. **M0 DONE (2026-06-16):** seL4 kernel rebuilt to XSAVE (feature-set 7) so it context-switches AVX YMM — AVX2-under-preemption safety gate PASSED on KVM (XCR0.avx=1 in PA+PB, 0 mismatches / 24k probes, self-test 5/5); M1 DONE (2026-06-17): AVX2 enabled in the seL4 build → **Gemma 4 E2B ~1.53 tok/s bare metal, single-thread** (vs ~0.2 scalar, ~7.6×; both M0 carve-outs closed); **M2 DONE (2026-06-17): seL4 SMP enabled (`-DSMP=ON -DNUM_NODES=2`, BSP + 1 AP) — multicore foundation validated KVM + bare metal: numNodes==2, self-test 5/5, Gemma load/gen + shmem IPC intact under SMP, `q=100 err=0`, ~1.53 tok/s 1T (no SMP penalty vs M1). E1: seL4 does NOT auto-distribute → M3 workers need explicit `seL4_TCB_SetAffinity`; no inference speedup yet (threadpool is M3). Branch A (ADR `docs/decisions/2026-06-17-enable-smp-branch-a.md`); a per-build config-verification gate in `build_jarvis_x86.sh` asserts SMP-on + M0/M1 invariants. **M3 DONE (2026-06-18): seL4-native threadpool — PA creates N−1 worker TCBs in PB's VSpace/CSpace (PB has no allocator), resolving the worker entry from PB's ET_EXEC `.symtab`; workers pin to distinct cores (`seL4_TCB_SetAffinity`), block on per-worker wake notifications, run the corrected work-stealing core over `qmatmul_vec` (generation publish + active-counter join). Verified BYTE-IDENTICAL to the serial path (greedy determinism, all prompts). Bare-metal N-sweep → Gemma 4 E2B `5.46 tok/s @ NUM_NODES=6` (3.57× the 1.53 1T), `err=0` over 800 queries; production `NUM_NODES=6`. Two bring-up bugs fixed (unchecked `sel4utils_copy_cap_to_process` → null-cap deadlock, caught by adversarial review; worker started outside sel4runtime → `seL4_SetIPCBuffer`, caught by QEMU smoke). M4 (record benchmark) next.** This build runs `KernelFastpath=ON` **+ SMP (`NUM_NODES=6`)** → outside seL4's verified X64 config; functional-but-unverified by design (see `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §3).

---

## Architecture

### Model B: Microkernel with AI Control

```
Hardware Layer
    ↓
Microkernel (Ring 0, cores 0-1)
• Interrupt handling (<1ms)
• Memory management, IPC primitives
• ~12K LOC, seL4 (formally verified*)
    ↓ ← Lock-free IPC →
AI Decision Engine (Ring 3, cores 2-N)
• Specialist agents (4-6) — domain-expert agents (device, network, filesystem, user), NOT model-size tiers
• Decision cache (50ms→<1ms for 85% ops)
• SHIELD safety framework
    ↓
User Space Services (Ring 3)
• Device drivers, File systems, Applications
```

> *\*seL4 "(formally verified)" refers to its verified configurations. JARVIS's x86-64 build runs `KernelFastpath=ON`, which is **outside** the verified X64 config ("C-level functional correctness, **no fast path**") — so JARVIS runs a functional-but-unverified seL4 configuration by design (IPC/AVX perf > holding the proof). See `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §3.*

**Why:** AI inference (50-500ms) is 3 orders of magnitude too slow for Ring 0 interrupt handling (<1ms). Microkernel isolates time-critical ops from AI.

### Phase 2 Split Deployment (COMPLETE)

```
┌──────────────────┐       UART        ┌──────────────────┐
│   PC (Host)      │◄─────────────────►│   Pi 4 (seL4)    │
│                  │   115200 baud     │                  │
│  Python AI       │                   │  Decision Cache  │
│  - Phi-3 Mini    │   7ms median RTT  │  - 258 patterns  │
│  - Llama 3.2 1B  │                   │  - 85.7% hit     │
│  - SHIELD        │                   │  - <1ms lookup   │
└──────────────────┘                   └──────────────────┘
```

- **Cache hits (85%):** seL4 decision cache answers in <1ms
- **Cache misses (15%):** Forwarded to PC via UART (7ms RTT)
- **No Python on Pi 4:** seL4 userspace is C-only
- Phase 3+ returns to standalone (x86 or Multi-Pi cluster)

### Architecture Enhancements

- **Decision Cache:** Pre-compiled query→bytecode patterns. 135x speedup (50ms→<1ms), 85.7% hit rate.
- **SHIELD Safety:** Staged execution sandbox, continuous risk scoring (0-1.0), shadow execution, deterministic rollback.
- **Shared Context Pool:** Lock-free shared state for agent coordination. 220x faster than serialization.

### Key Constraints

**AI cannot:** handle interrupts (<1ms), generate drivers real-time, run at Ring 0
**AI can:** make high-level decisions (50-500ms), optimize configs, coordinate agents, learn over time

---

## Build & Test Commands

### Phase 2: seL4 Pi 4 Build

```bash
# In WSL - build JARVIS kernel for Pi 4
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/scripts
tr -d '\r' < build_and_copy_kernel.sh | bash

# Manual build steps
cd ~/sel4-workspace/rpi4_jarvis
cmake -G Ninja \
    -DCROSS_COMPILER_PREFIX=aarch64-linux-gnu- \
    -DKernelPlatform=bcm2711 \
    -DKernelSel4Arch=aarch64 \
    -DKernelArmPlatform=bcm2711 \
    ../projects/jarvis-sel4
ninja

# Copy to firmware directory
cp images/kernel8.img /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/firmware/

# Deploy to SD card
copy phase2\firmware\kernel8.img D:\
```

### Phase 2: Test Commands

```bash
# Python tests
wsl python3 phase2/src/ai/test_uart_ipc_client.py      # 22 tests - UART protocol
wsl python3 phase2/src/ai/test_system_bootstrap.py     # 25 tests - Bootstrap
wsl python3 phase2/src/ai/test_integration.py          # 10 tests - Integration

# C tests
wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/ipc && \
  gcc -O2 -I../../../phase1/src/cache -I../../../phase1/src/ipc \
  dual_ring_buffer.c test_dual_ring.c ../../../phase1/src/ipc/ring_buffer.c \
  -o test_dual_ring && ./test_dual_ring"                # 12 tests

wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/ipc && \
  gcc -O2 -I../../../phase1/src/cache -I../../../phase1/src/ipc \
  ipc_handler.c dual_ring_buffer.c ../../../phase1/src/ipc/ring_buffer.c \
  ../../../phase1/src/cache/decision_cache.c ../../../phase1/src/cache/cache_patterns.c \
  test_ipc_handler.c -o test_ipc_handler && ./test_ipc_handler"  # 10 tests

wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/drivers && \
  gcc -O2 test_uart_logic.c -o test_uart_logic && ./test_uart_logic"  # 8 tests
```

### Phase 1: Comprehensive Test (All 27 tests)

```bash
wsl
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
./setup_wsl_dependencies.sh  # First time only
./run_all_tests_wsl.sh       # 25-35 min
```

---

## File Structure

```
JARVIS_OS/
├── phase0/                     # COMPLETE - validation experiments
├── phase1/                     # COMPLETE (26/26 weeks)
│   └── src/                    # cache/, ipc/, sel4/, ai/, shell/
├── phase2/                     # COMPLETE
│   ├── docs/                   # PHASE_2_KICKOFF.md, UART_IPC_PROTOCOL.md, USER_GUIDE.md, ALPHA_TESTER_GUIDE.md, PI4_PLATFORM_GUIDE.md
│   ├── firmware/               # kernel8.img, u-boot.bin, boot files
│   ├── src/
│   │   ├── ipc/               # dual_ring_buffer, ipc_handler + tests
│   │   ├── drivers/           # uart_pl011, emmc_sdhci, bcm2711_timer,
│   │   │                      # bcm_genet, net_stack, net_cmd, usb_hid, bcm_gpio, bcm_i2c,
│   │   │                      # bcm_spi, bcm_rng, bcm_pwm, bcm_dma, slot_alloc, dma_alloc, blk_dev
│   │   ├── ai/                # uart_ipc_client.py, system_bootstrap.py + tests
│   │   ├── boot/              # jarvis.dts, jarvis.dtb, jarvis_dtb_data.h, fdt_parser.h/c
│   │   ├── sel4/              # main_arm64.c, CMakeLists.txt
│   │   └── jarvis-sel4-cmake/ # CMakeLists.txt for TII build system
│   ├── weeks/                  # week27-week47 status docs
│   └── scripts/               # build_and_copy_kernel.sh, build_installer_image.sh, flash_sd.sh, install_jarvis.sh
├── phase3/                     # IN PROGRESS (JARVIS Project PC operational)
│   ├── docs/                   # PHASE_3_KICKOFF.md, HARDWARE_RESEARCH.md, IMPLEMENTATION_PLAN.md
│   ├── src/
│   │   ├── ai/                # ggml integration, model loading, inference
│   │   ├── boot/              # x86 boot config (GRUB/EFI)
│   │   ├── drivers/           # x86 drivers (16550A UART, AHCI, NIC)
│   │   ├── ipc/              # Shared memory IPC
│   │   └── sel4/             # seL4 x86-64 rootserver
│   ├── scripts/               # Build scripts
│   ├── firmware/              # Boot images, GRUB configs
│   └── weeks/ (empty/unused)  # Phase 3 used phase3/docs/ topic docs + docs/decisions/ ADRs + PHASE_3_FINAL_REPORT.md, not weekly status files
├── phase4/                     # IN PROGRESS (Production v1.0)
│   ├── docs/                   # ROADMAP.md (Phases 4-7), PHASE_4_GOAL1_INFERENCE_PERF.md (goal #1 research+plan)
│   └── weeks/                  # weekN/WEEK_N_STATUS.md (weekly status resumes, like Phase 2)
├── JARVIS_UNIFIED_PLAN.md     # 36-month master plan
├── ARCHITECTURE_ENHANCEMENTS.md
├── archive/                    # Historical research
└── temp_sd_backup/            # SD card backups
```

---

## Coding Conventions & Patterns

### C/Python IPC Struct Alignment

Python ctypes structs MUST exactly match C struct layout:

```python
class RingMessage(ctypes.Structure):
    _pack_ = 1  # REQUIRED: No padding
    _fields_ = [
        ("type", ctypes.c_uint32),
        ("id", ctypes.c_uint32),
        ("payload_size", ctypes.c_uint32),
        ("payload", ctypes.c_char * MAX_MESSAGE_SIZE),
        ("timestamp", ctypes.c_uint64)
    ]
```

### Query Normalization

Both C and Python must normalize identically: lowercase → collapse spaces → trim → FNV-1a 64-bit hash.
- C: `cache_normalize_query()` in `decision_cache.c`
- Python: `QueryProcessor.normalize_query()` in `query_processor.py`

### seL4 Tutorials CMakeLists.txt Pattern

```cmake
include(${SEL4_TUTORIALS_DIR}/settings.cmake)
sel4_tutorials_regenerate_tutorial(${CMAKE_CURRENT_SOURCE_DIR})
include(rootserver)
include(simulation)
add_executable(hello-world src/main.c ...)
target_link_libraries(hello-world sel4 muslc utils sel4muslcsys sel4platsupport sel4utils sel4debug)
DeclareRootserver(hello-world)
```

Note: `DeclareTutorialApp()` does NOT exist. Use `add_executable()` + `DeclareRootserver()`.

### Testing Philosophy

- Every component has standalone tests printing PASS/FAIL
- C tests: compile and run directly (no deps)
- Python tests: `python test_*.py` (mock mode if seL4 unavailable)
- Always aim for 100% pass rate

### Development Documentation Pattern

- **Phase 2** used weekly status files: `phase2/weeks/weekN/WEEK_N_STATUS.md` (weeks 27-49), committed weekly with the week number in the message.
- **Phase 3** was documented via topic/milestone docs in `phase3/docs/` (KICKOFF, HARDWARE_RESEARCH, IMPLEMENTATION_PLAN, the QEMU/native test-results docs, MODEL_BENCH_OFF, the security audits), the ADRs in `docs/decisions/`, and `phase3/docs/PHASE_3_FINAL_REPORT.md` — **not** weekly status files (there is no `phase3/weeks/`).
- **Phase 4** resumes weekly status files at `phase4/weeks/weekN/WEEK_N_STATUS.md` (like Phase 2); Phase 3 used topic docs.
- Implement, test, update the relevant doc; commit with a descriptive subject referencing the milestone/ADR where applicable.

### Bare-Metal Development Rules

- **Always test in QEMU before flashing USB** — run `phase3/scripts/qemu_test.sh` after every build. If it crashes in QEMU, don't waste time reflashing.
- **Build without embedded model for fast iteration** — use `-DJARVIS_EMBED_MODEL=""` for quick boot tests. Model loading is tested separately via the NVMe FAT32 runtime path (the GRUB-module approach was abandoned — seL4 overwrites module memory during rootserver relocation).
- **Verify GRUB menu entry works** — wrong image names cause silent boot failures (keyboard dies, no output). Check grub.cfg image names match `ls ~/sel4-x86/jbuild/images/`.

---

## Current Status (Phase 3 — Process-Isolated LLM Inference on seL4)

**NVMe RUNTIME MODEL LOADING MILESTONE** (April 4, 2026) — **LLM inference with runtime NVMe model loading on bare-metal seL4 on Ryzen 7 2700X.** 1.6MB USB boot image loads the model GGUF from the NVMe FAT32 partition (Lexar NM790, PCI bus 1) at runtime — model-agnostic, loading whatever `JARVIS_MODEL_FILE` names (current default `"GEMMA2B GUF"` = Gemma 4 E2B Q4_K_M, ~2.89 GiB). NVMe driver: PCI detection → BAR0 MMIO → admin queue → IDENTIFY → I/O queue → FAT32 read → frames → Process B. (Original Apr 4 milestone: 770MB Llama 3.2 1B Q4_K_M → 197K frames.) Coherent text generation via process-isolated IPC. No embedded model binary. Self-test 5/5 PASS, 181 untypeds, 32410MB RAM.

| Milestone | Status |
|-----------|--------|
| **Phase 2 Pi 4 (ALL 87 milestones DONE)** | **DONE** |
| U-Boot boot, UART IPC, SD/EMMC, GENET networking, USB HID keyboard | DONE |
| 21 BCM2711 drivers (GPIO, I2C, SPI, RNG, PWM, DMA, watchdog, thermal) | DONE |
| 30-day stability test (30.6 days, 0 crashes, 99.8% pass rate) | DONE |
| Phase 2 security audit: 6 C findings (4 fixed) + Snyk dependency audit | DONE |
| Cross-codebase adversarial audit (Mar 2026, phase2+phase3 C ~35K LOC): 26 findings fixed, 211/211 tests | DONE |
| **Phase 3a: GPU benchmarks (RTX 2070, 1B: 273 tok/s GPU, 44 CPU)** | **DONE** |
| **Phase 3a: seL4 native Linux build env (123/123 PASS, KVM)** | **DONE** |
| **Phase 3a: 247/247 Phase 3 tests validated on native x86-64** | **DONE** |
| **GGUF memory API (gguf_open_memory via fmemopen)** | **DONE** |
| **Q4_K/Q6_K dequantization (verified bit-exact vs llama.cpp)** | **DONE** |
| **Quantized zero-copy model loading (llama_quant.c, ~50MB heap)** | **DONE** |
| **GGUF vocab extraction (128,256 BPE tokens from raw binary)** | **DONE** |
| **Weight tying (output.weight → token_embd.weight fallback)** | **DONE** |
| **RoPE freq factors (rope_freqs.weight loaded, applied as divisors)** | **DONE** |
| **4/4 inference stages PASS on seL4 QEMU (parse→load→tokenize→generate)** | **DONE** |
| **Coherent text: "a microkernel implementation of the L4 architecture..."** | **DONE** |
| **Logits verified vs llama.cpp reference (top-5 match exactly)** | **DONE** |
| **Process isolation: Process A spawns Process B from CPIO (SEC-014)** | **DONE** |
| **Shared memory IPC between two seL4 processes (direct page mapping)** | **DONE** |
| **End-to-end IPC: Process A query → Process B inference → response** | **DONE** |
| **CNode=22, morecore=128MB, allocator pools scaled for 230K frames** | **DONE** |
| Hardware recon (lspci verified: NVMe, I211, R9 280X, xHCI) | DONE |
| VGA text mode output (80x25, 0xB8000) via vka_alloc_frame_at | DONE |
| GRUB2 boot config + USB creator script | DONE |
| x86 source sync/build script | DONE |
| Bare-metal boot on Ryzen 7 2700X | DONE |
| Self-test 5/5 PASS on real hardware | DONE |
| Process isolation on real hardware | DONE |
| Bare-metal boot guide + BIOS checklist | DONE |
| **Llama 3.2 1B inference on bare-metal Ryzen 2700X** | **DONE (Apr 15)** |
| **Coherent text generation via process-isolated IPC on real hardware** | **DONE (Apr 15)** |
| NVMe driver (PCI detect, BAR0 map, admin queue, IDENTIFY, I/O read) | DONE |
| FAT32 read-only parser (BPB, root dir scan, cluster chain) | DONE |
| NVMe IDENTIFY succeeds in QEMU ("QEMU NVMe Ctrl") | DONE |
| NVMe sector read verified in QEMU | DONE |
| NVMe → FAT32 → GGUF magic verified in QEMU | DONE |
| **NVMe full model loading: 770MB from FAT32 → 197K frames → Process B** | **DONE** |
| **Runtime inference from NVMe (no embedded model)** | **DONE** |
| NVMe on bare metal (Lexar NM790 — no HMB needed) | DONE |
| FAT32 partition LBA = 32768 (JARVIS_DATA partition); whole-disk (LBA 0) fallback for QEMU | DONE |
| PCI multi-bus scan (buses 0-15) for NVMe on bus 1 | DONE |
| Continuous IPC workload loop (63 queries, xorshift PRNG, stats, error tracking) | DONE |
| Intel I211 NIC driver (PCI 8086:1539, TX+RX polled) | DONE |
| IPC response drain fix (multi-message responses) | DONE |
| Debug config (compile-time IPC/PB/ring/stats flags) | DONE |
| SHMEM ring overflow fix (16→15 slots, _Static_assert) | DONE |
| Fuzz testing harness (net_stack, shmem_ipc, gguf_parser, 300K iterations) | DONE |
| Phase 3c security audit: 14 findings (4 HIGH, 3 MED, 4 LOW, 3 INFO) | DONE |
| All 8 HIGH/MED findings fixed (fat32, nvme, nic_i211, vga, tokenizer) | DONE |
| Phase 3c audit gap fill: 11 new findings (1 HIGH, 2 MED, 4 LOW, 4 INFO) — pci, ahci, uart_16550, posix_stubs | DONE |
| All 4 HIGH/MED gap-fill findings fixed (pci BAR5, ahci buf_len overflow, uart timeout, posix clock_gettime honest failure) | DONE |
| Model bench-off: open discovery (20 models surveyed) | DONE |
| Model bench-off: compatibility verification (GGUF byte-verified) | DONE |
| Model bench-off: speed bench (11 models, Ryzen 2700X + Ryzen 5600 + RTX 2070) | DONE |
| Model bench-off: perplexity bench (WikiText-2 full corpus, 10 models, RTX 2070) | DONE |
| Model bench-off: quality bench (10 prompts, 11 models, Claude-judged blind) | DONE |
| **Bench-off winner: Gemma 4 E2B (8.40/10 avg, 7 judges, 19.7 tok/s) — single-model deployment** | **DECISION** |
| **Llama 3.1 8B disqualified** — 5.06/10 (#8 of 11), training data contamination | **DROPPED** |
| Bench-off deferred contenders (Gemma 4, Qwen3, Qwen3.5 SSM) — ALL since IMPLEMENTED (rows below); RotorQuant/TurboQuant evaluated → deferred to Phase 4 (ADR 2026-06-15) | DONE |
| **Gemma 4 E2B engine: ~4,400 LOC across 27 files — 17 fixes on `feature/gemma4-arch`** | **DONE** |
| **Gemma 4 E2B validated: main PC native test — coherent English** | **DONE** |
| **Gemma 4 E2B validated: JARVIS PC seL4 QEMU Process B (4/4 queries coherent)** | **DONE** |
| **JARVIS engine bench harness (bench_engine.c, llama-bench table format)** | **DONE** |
| **JARVIS engine bench: 11/11 models load, 6 model families supported** | **DONE** |
| Fused QKV + fused gate-up tensor support (Phi-3) | DONE |
| Phi-3 + Qwen3 chat template wrapping in bench_engine | DONE |
| Partial RoPE support (Qwen3.5: 64 of 256 dims) | DONE |
| **Gated DeltaNet SSM implementation (ssm.c/h, ~1200 LOC, 7 unit tests)** | **DONE** |
| **Qwen3.5 4B + 9B hybrid SSM — loads and generates** | **DONE** |
| Zero-embedding forward for invalid tokens (SSM temporal continuity) | DONE |
| NVMe write logging (raw sector telemetry) | DONE |
| NVMe log auto-capture (puts_serial intercept, [VGA]/[SER] tags) | DONE |
| MSG_DEBUG IPC for Process B -> NVMe log ([PB] tag) | DONE |
| NVMe log bare-metal verified (boot 5, full boot timeline captured) | DONE |
| **Bare-metal IPC inference VERIFIED: 7 queries, 0 crashes (stack overflow fixed)** | **DONE** |
| Bare-metal debug: equal prio, poll timeout, ring overflow, QEMU NVMe, probe, fwd_scratch | DONE |
| Fused qdot (7 types, AVX2) + SIMD attn + RoPE tables + pthread threadpool | DONE |
| **Performance: Llama 1B 0.99 -> 3.22 (1T) -> 19.79 tok/s (16T)** | **DONE** |

**Next:** 30-day x86 soak **DEFERRED — descoped from v0.2.1-beta gating (risk-accepted, ADR `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`)**; bare-metal **burn-in** passed (~400 queries, err=0, boot_id constant — not a soak). Active: Phase 4 perf (AVX2 + threading in the seL4 build). TurboQuant/RotorQuant: evaluated, deferred to Phase 4 (ADR `docs/decisions/2026-06-15-defer-turboquant-rotorquant-to-phase4.md`). Phase 4 started — goal #1 reframed to **Inference performance**: v1.0 = CPU AVX2+threading in the seL4 build (~8–9 tok/s Gemma E2B @16T target); GPU inference DEFERRED (no usable GPU — ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`). GPU display/UI (goal #2) unaffected.

### Pre-Work Tasks (Before JARVIS Project PC)

| # | Task | Status |
|---|------|--------|
| 1 | seL4 x86-64 on QEMU (main PC WSL) | DONE — 123/123 tests pass, build env documented |
| 2 | ggml standalone musl compilation test | DONE — 5 stubs needed, C++ backend is main challenge |
| 3 | Shared memory IPC protocol design + test | DONE — 10/10 PASS, 23.7M msg/sec |
| 4 | Port portable code to x86 build | DONE — 22/22 tests pass, 5/7 modules zero changes |
| 5 | x86 driver skeleton headers | DONE — uart_16550.h, pci.h, ahci.h |
| 6 | Git tag v0.2.0-alpha | DONE |
| 7 | Pi 5 llama.cpp benchmark | DONE — script + research estimates written |
| 8 | Custom rootserver in QEMU | DONE — builds and runs in QEMU, cache loads 308 patterns, SHIELD works |

### Phase 3 Early Work (Done in QEMU — ahead of schedule)

| Component | Planned Week | Status | Files | Tests |
|-----------|-------------|--------|-------|-------|
| GGUF parser (C-only) | 19-20 | DONE | gguf_parser.h/c | 12 PASS |
| UART 16550A driver | 13-14 | DONE | uart_16550.c/h | 7 PASS |
| PCI enumeration | 15-16 | DONE | pci.c/h | 11 PASS |
| AHCI full I/O | 15-16 | DONE | ahci.c/h | 13 PASS |
| NIC RTL8168 (TX + RX delivery) | 17-18 | DONE | nic_rtl8168.c/h | 7 PASS |
| VGA text mode driver | 13-14 | DONE | vga_text.c/h | 7 PASS |
| x86 Timer (PIT/HPET/TSC) | 13-14 | DONE | x86_timer.c/h | 8 PASS |
| Block device abstraction | 15-16 | DONE | blk_dev_x86.c/h | 9 PASS |
| C tensor ops (9 ops, AVX2+FMA) | 19-20 | DONE | tensor_ops.c/h | 12 PASS |
| Dequantization (Q4_0/Q8_0/F16/Q4_K/Q6_K) | 19-20 | DONE | dequant.c/h | 36 PASS |
| BPE Tokenizer | 21-22 | DONE | tokenizer.c/h | 12 PASS |
| Model architecture + loading | 21-22 | DONE | llama_model.h, llama_load.c | 7 PASS |
| Sampling (greedy + top-k) | 21-22 | DONE | sampling.c/h | 9 PASS |
| Transformer forward pass | 21-22 | DONE | llama_forward.c | 9 PASS |
| Inference API | 21-22 | DONE | inference.c/h | 4 PASS |
| Shared memory IPC | 23-24 | DONE | shmem_ipc.c/h | 10 PASS |
| Quantized zero-copy inference | 21-22 | DONE | llama_quant.c/h | 10 PASS |
| GGUF vocab extraction | 21-22 | DONE | gguf_vocab.c/h | 10 PASS |
| GGUF memory API | 19-20 | DONE | gguf_parser.c/h (extended) | 7 PASS |
| F32 vs quantized comparison | — | DONE | test_forward_compare.c | SKIP on CI |
| Custom x86 rootserver | 9-12 | DONE (QEMU) | main_x86.c | 5/5 self-test + 4/4 inference PASS |
| SHIELD safety module | — | DONE | shield.c/h | 8 PASS |
| Generation quality tests | — | DONE | test_generation.c | 6 PASS (model needed) |
| IPC CRC-32 (SEC-020) | — | DONE | shmem_ipc.c/h | 3 PASS (13 total) |
| GPT-2 full byte mapping | — | DONE | tokenizer.c | 12 PASS |
| Tiled matmul + unroll | — | DONE | tensor_ops.c, llama_quant.c | 14+10 PASS |
| RDTSC timing (Stage 5) | — | DONE | main_x86.c | 5/5 stages PASS |
| NVMe driver (polled I/O) | 15-16 | DONE | nvme.c/h | 10 PASS |
| FAT32 read-only parser | 15-16 | DONE | fat32.c/h | 8 PASS |
| NIC Intel I211 (TX + RX polled) | 17-18 | DONE | nic_i211.c/h | 11 PASS |
| Fused QKV + gate-up tensor split | — | DONE | llama_quant.c | (load-time, no new tests) |
| SSM / Gated DeltaNet (Qwen3.5) | — | DONE | ssm.c/h | 7 PASS |
| Engine bench harness | — | DONE | bench_engine.c | (compile-only CI) |
| AVX2 qdot correctness (-mavx2) | — | DONE | test_avx2_correctness.c | 24 PASS |
| NVMe write (opcode 0x01) | — | DONE | nvme.c (extended) | 10 PASS |
| NVMe log module | — | DONE | nvme_log.c/h | (bare-metal only) |
| NVMe log parser | — | DONE | parse_nvme_log.py | (Python) |
| Threadpool join race fix + parallel_for ABI test | — | DONE | threadpool.c, test_parallel_for.c | ~6 PASS |
| seL4-native threadpool (M3) | — | DONE | threadpool_sel4.c, main_x86.c, inference_server.c | bare-metal (Gemma 5.46 tok/s @ NN=6) |
| **Total** | | | **123 code files (41 test)** | **383+ test asserts, ~35,700 LOC** |

**Phase 3b on real hardware — bare-metal burn-in passed (2026-06-15, NOT a 30-day soak):** clean boot, NVMe model load (LBA-32768), coherent Gemma 4 E2B, heartbeat/shield IPC fix holding at err=0 over 400 queries (q=100/200/300/400), durable NVMe logging with constant boot_id. Formal 30-day x86 soak **DEFERRED — descoped from v0.2.1-beta gating (risk-accepted)**: the new x86 surfaces (shmem_ipc, in-process inference, NVMe load) are covered by this ~400-query burn-in + 300K-iter fuzz + 2 audits + the IPC fix; Phase 2's 30.6-day Pi 4 run (different IPC/ARM stack) is supporting context, not equivalent coverage. See `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`.

### Phase 3 Weeks

| Weeks | Task |
|-------|------|
| 1-6 | Phase 3a: GPU benchmarks + native Linux build env (COMPLETE) |
| 7-28 | Phase 3b: Pure bare-metal seL4 x86-64 — BARE-METAL BOOT ACHIEVED |
| 29-40 | Phase 3c: Hardening, fuzz testing, SHIELD |
| 41-44 | Final report + git tag v0.2.1-beta |

### Validated Metrics

| Metric | Target | Actual |
|--------|--------|--------|
| IPC latency | <100us | 54us (Phase 1), 7ms UART (Phase 2) |
| Cache hit rate | >80% | 85.7% |
| AI inference | <500ms | 558ms CPU Phi-3, <100ms Llama 3.2 1B (host PC benchmarks, no log artifact) |
| JARVIS engine (quantized, 1T) | — | Llama 1B: 3.22, Gemma 4 E2B: 1.70, Mistral 7B: 0.56 tok/s |
| JARVIS engine (quantized, 16T) | — | Llama 1B: 19.79, Gemma 4 E2B: 8.63, Mistral 7B: 4.16 tok/s |
| llama.cpp ref (8T) | — | Llama 1B: 40.4, Gemma 4 E2B: 19.7 tok/s |
| **seL4 build, bare metal** (M1 1T → M3 threaded) | — | M1 (1T AVX2): **Gemma 4 E2B ~1.53 tok/s** (vs ~0.2 scalar, ~7.6×). **M3 (2026-06-18): Gemma 4 E2B 5.46 tok/s @ `NUM_NODES=6` threaded (3.57× the 1.53 1T; bare-metal N-sweep, err=0 over 800 q; NN=4 = 4.21).** Held under M2 SMP with no 1T penalty. NOTE: the three rows above are the NATIVE dev engine — do not conflate with this deployed seL4-build figure. |
| Models supported | — | 11/11 load, 6 model families (Llama, Gemma 4, Phi-3, Mistral, Qwen3, Qwen3.5 DeltaNet SSM) |
| Boot time | <60s | ~2s |
| SHIELD block rate | >90% | 100% harmful blocked, 0% FP (keyword + model-assisted) |
| Multi-agent routing | >90% | 100% |

---

## Key Technical Notes

### UART IPC Protocol

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  SYNC    │  TYPE    │   SEQ    │  LENGTH  │  FLAGS   │ PAYLOAD  │  CRC16   │
│ (2 bytes)│ (1 byte) │ (2 bytes)│ (2 bytes)│ (1 byte) │ (0-240)  │ (2 bytes)│
│  0xAA55  │  0x01-0E │  0-65535 │  0-240   │  0x00    │  data    │ CRC-CCITT│
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
```

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| QUERY | 0x01 | Py→seL4 | Cache lookup |
| RESPONSE | 0x02 | seL4→Py | Cache result |
| HEARTBEAT | 0x03 | Both | Keep-alive |
| HEARTBEAT_ACK | 0x04 | Both | Keep-alive ack |
| STATS_REQUEST | 0x05 | Py→seL4 | Cache stats req |
| STATS_RESPONSE | 0x06 | seL4→Py | Cache stats |
| COMMAND | 0x07 | Py→seL4 | Shell command |
| COMMAND_RESULT | 0x08 | seL4→Py | Command output |
| SHIELD_CHECK | 0x09 | Py→seL4 | Risk assessment |
| SHIELD_RESULT | 0x0A | seL4→Py | Risk decision |
| ERROR | 0x0B | Both | Error |
| RESET | 0x0C | Both | Protocol reset |
| STATE_CHANGE | 0x0D | Py→seL4 | State change |
| STATE_ACK | 0x0E | seL4→Py | State ack |

Baud: 115200. RTT: 7ms measured. Heartbeat: 5s. Timeout: 30s.
Full spec: `phase2/docs/UART_IPC_PROTOCOL.md`

### BCM2711 Hardware

```
GENET Ethernet:  0xFD580000  (separate device untyped!)
Peripheral Base: 0xFE000000
System Timer:    0xFE003000  (1 MHz free-running counter)
GPIO:            0xFE200000
UART0 (PL011):   0xFE201000
EMMC/SDHCI:      0xFE340000
BSC1 I2C:        0xFE804000  (I2C master, 100/400 kHz)
DWC2 USB:        0xFE980000  (USB OTG host controller)
```

USB-Serial wiring: GPIO14(TXD)→RXD, GPIO15(RXD)←TXD, GND─GND

### SD Card Boot (U-Boot)

```
Boot Partition (FAT32):
├── start4.elf      # GPU firmware
├── fixup4.dat      # Memory config
├── u-boot.bin      # U-Boot 2026.01 bootloader
├── boot.scr        # U-Boot boot script
├── kernel8.img     # JARVIS seL4 boot image
├── bcm2711-rpi-4-b.dtb  # Device tree
└── config.txt      # arm_64bit=1, kernel=u-boot.bin, enable_uart=1

Boot flow: GPU firmware → U-Boot → boot.scr → kernel8.img → seL4
```

U-Boot: Press key during 3s countdown for interactive shell. Auto-boot loads kernel8.img at 0x00080000.
Backup: `temp_sd_backup/uboot_working/`

### seL4 Device Mapping Rules

- **Forward-only cursor:** seL4 Untyped_Retype watermark only moves forward. Map devices in ascending paddr order.
- **VSpace range:** vaddr must be within mapped VSpace (0x400000-0x5b9fff). Error 6 = seL4_FailedLookup means vaddr not in VSpace.
- **Init order:** systimer(0xFE003000) → DMA(0xFE007000) → mailbox(0xFE00B000) → watchdog(0xFE100000) → RNG(0xFE104000) → UART/GPIO(0xFE200000-0xFE201000) → SPI(0xFE204000) → PWM(0xFE20C000) → EMMC(0xFE340000) → I2C(0xFE804000) → USB(0xFE980000)
- **Binary buddy skip:** When timer is mapped before UART, use power-of-2 Untyped retypes to advance watermark from 0xFE004000→0xFE200000 (7 retypes: 16KB→32KB→64KB→128KB→256KB→512KB→1MB) instead of 2MB LargePage skip (which would consume GPIO's frame).
- **Device cursor after mapping:** GPIO_BASE + 2*4KB = 0xFE202000
- **DMA = uncacheable:** seL4 does NOT set `SCTLR_EL1.UCI`, so ALL cache maintenance instructions (`DC IVAC/CIVAC/CVAC`) trap from EL0. Map DMA buffers with `vm_attributes = 0` (uncacheable) instead.

### Virtual Address Layout

```
0x5c0000 - UART PL011 (hardcoded)
0x5c1000 - GPIO (hardcoded)
0x5c2000 - System Timer (auto-assigned)
0x5c3000 - EMMC (auto-assigned)
0x5c4000-0x603FFF - DMA pool (256KB)
0x604000-0x609FFF - GENET MMIO (6 pages, own device untyped)
0x610000 - VideoCore Mailbox (explicit vaddr, maps 0xFE00B000)
0x611000 - PM Watchdog (explicit vaddr, maps 0xFE100000)
0x612000 - DMA Engine (explicit vaddr, maps 0xFE007000)
RNG, SPI, PWM - auto-assigned (0xFE104000, 0xFE204000, 0xFE20C000)
0x60A000-0x60CFFF - DWC2 USB (3 pages, paddr 0xFE980000)
```

### Phase 1 IPC Limitation

Phase 1 used "mock IPC" - Python and seL4 did NOT communicate in real-time. Separate memory spaces. Both proven independently, connected in Phase 2 via UART.

### NVMe Write Logging

- Log region: LBA 4000794624 (past p4, the 1.7G vfat tail partition). Drive confirmed via lsblk: **Lexar NM790 2TB**, 4,000,797,360 sectors (1907 GiB). The log's 2700 sectors fit the ~2,736-sector tail with only ~36 sectors (~18 KB) of headroom — TIGHT. NOTE: nvme_log.h's comments ("1TB", "~172K-sector tail") are BOTH stale/wrong; fix them.
- Max entries: 2700 (1.3 MB, fits in unpartitioned tail of disk) — log does NOT wrap; cursor persists across boots, so `nvme_log_write` returns -2 once full
- Periodic stats cadence: serial `[STATS]` prints every 100 queries (free); the NVMe `LOG_IPC_STATS` entry is written every `JARVIS_STATS_NVME_INTERVAL` = **100** queries (`jarvis_debug.h`). Justified by the **measured** bare-metal rate ~3k queries/day (single core, scalar) → ~870 entries over 30 days, well under the 2700-entry cap. (Task 3 originally used 1000 on a 16k/day estimate the bare-metal run disproved; q=400 err=0 verified on real hardware 2026-06-15. If a future build speeds the query rate up ~5x+, raise this back toward 1000.)
- Format: 512-byte records (header + entries), XOR checksum
- Auto-capture: `puts_serial()` intercept writes every line to NVMe log
- Tags: `[VGA]` = visible on monitor, `[SER]` = serial only, `[PB]` = Process B via MSG_DEBUG IPC
- Recovery: `sudo dd if=/dev/nvme0n1 bs=512 skip=4000794624 count=2700 | python3 phase3/scripts/parse_nvme_log.py`
- `seL4_Yield` only yields to equal-priority — PA and PB both at MaxPrio

### Bare-Metal Debugging Rules

- Use `#if EMMC_DEBUG` preprocessor guards for diagnostics, NOT code deletion (deleting debug code caused mysterious boot failures)
- `seL4_DebugPutChar()` works for TX without device frame mapping
- Device frame mapping required for UART RX
- Serial console: 115200 baud, 8N1
- `put_dec()` and `put_hex()` go through `putc_serial()` -> NVMe log. To suppress NVMe logging temporarily (e.g. verbose dumps), save/clear `g_nvme_ptr`.
- Process A and Process B run at `seL4_MaxPrio` (equal priority). `seL4_Yield` only works between equal-priority threads — MaxPrio-1 gets starved.
- Response ring has 15 slots. PB must reserve 3 for MSG_RESPONSE — use `pb_can_log()` before debug writes.
- `qmodel_forward` stack budget: keep below ~8KB. Large temporaries (>4KB) MUST use `state->fwd_scratch` heap buffer. seL4 Process B has limited stack.
- **PA response consumption: POLL the ring, do NOT `seL4_Wait(resp_notif)` in the workload loop.** The inference path polls and never consumes `resp_notif`, leaving it signaled; heartbeat/shield used to `seL4_Wait(resp_notif)` + a single recv, which returned immediately on that stale signal and read before PB responded (~7% spurious errors, all in hb/shield). Fixed via `wait_for_response()` which polls the ring for the expected type and drains stale messages (race-free, like inference). The only legit `seL4_Wait(resp_notif)` is the pre-loop ready handshake. NOTE: the race is **Heisenbug** — enabling `JARVIS_DBG_IPC` serial prints inserts a scheduling window that masks it, so verify by err-rate ([STATS]) at `JARVIS_DBG_IPC=0`, not per-event logs.

---

## For Claude Code

### Reading Order (New Session)
1. This file (CLAUDE.md) → architecture + current status
2. `phase3/docs/PHASE_3_FINAL_REPORT.md` → current phase (Phase 3) summary + ADR pointers
3. `phase2/weeks/week47/WEEK_47_STATUS.md` → Phase 2 latest-week detail (history)
4. `phase2/docs/PHASE_2_KICKOFF.md` → Phase 2 goals
5. Source files as needed

### Quick Reference
- **Build:** `wsl -e bash -lc "cd .../phase2/scripts && tr -d '\r' < build_and_copy_kernel.sh | bash"`
- **Deploy:** `copy phase2\firmware\kernel8.img D:\`
- **Serial:** 115200 baud, Pi 4 GPIO14/15
- **Main source:** `phase2/src/sel4/main_arm64.c`
- **UART driver:** `phase2/src/drivers/uart_pl011.c`
- **EMMC driver:** `phase2/src/drivers/emmc_sdhci.c`
- **Timer driver:** `phase2/src/drivers/bcm2711_timer.c`
- **Slot allocator:** `phase2/src/drivers/slot_alloc.c`
- **DMA allocator:** `phase2/src/drivers/dma_alloc.c`
- **Block device:** `phase2/src/drivers/blk_dev.c`
- **GENET Ethernet:** `phase2/src/drivers/bcm_genet.c`
- **Net Stack:** `phase2/src/drivers/net_stack.c`
- **Net Commands:** `phase2/src/drivers/net_cmd.c`
- **USB HID Keyboard:** `phase2/src/drivers/usb_hid.c`
- **GPIO Driver:** `phase2/src/drivers/bcm_gpio.c`
- **I2C Driver:** `phase2/src/drivers/bcm_i2c.c`
- **Watchdog driver:** `phase2/src/drivers/bcm_watchdog.c`
- **Thermal driver:** `phase2/src/drivers/bcm_thermal.c`
- **Power manager:** `phase2/src/ai/power_manager.py`
- **FDT parser:** `phase2/src/boot/fdt_parser.c`
- **Device tree source:** `phase2/src/boot/jarvis.dts`
- **Embedded DTB:** `phase2/src/boot/jarvis_dtb_data.h`
- **Boot manager:** `phase2/src/boot/boot_manager.c`
- **Warm reboot:** `phase2/src/boot/warm_reboot.c`
- **Power manager:** `phase2/src/drivers/bcm_power.c`
- **SPI driver:** `phase2/src/drivers/bcm_spi.c`
- **RNG driver:** `phase2/src/drivers/bcm_rng.c`
- **PWM driver:** `phase2/src/drivers/bcm_pwm.c`
- **DMA engine:** `phase2/src/drivers/bcm_dma.c`
- **Stability harness:** `phase2/src/ai/stability_harness.py`
- **Build config:** `phase2/src/jarvis-sel4-cmake/CMakeLists.txt`
- **SD Image Builder:** `phase2/scripts/build_installer_image.sh`
- **SD Flasher:** `phase2/scripts/flash_sd.sh`
- **Installer (Linux):** `phase2/scripts/install_jarvis.sh`
- **User Guide:** `phase2/docs/USER_GUIDE.md`
- **Tester Guide:** `phase2/docs/ALPHA_TESTER_GUIDE.md`
- **Platform Guide:** `phase2/docs/PI4_PLATFORM_GUIDE.md`
- **Security Self-Audit:** `phase2/docs/SECURITY_SELF_AUDIT.md`
- **Adversarial Security Audit:** `phase3/docs/SECURITY_AUDIT_2026-03-22.md`
- **Phase 2 Final Report:** `phase2/docs/PHASE_2_FINAL_REPORT.md`
- **Phase 3 Hardware Research:** `phase3/docs/PHASE_3_HARDWARE_RESEARCH.md`
- **Phase 3 Kickoff:** `phase3/docs/PHASE_3_KICKOFF.md`
- **Phase 3 Implementation Plan:** `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md`
- **Phase 3 Final Report:** `phase3/docs/PHASE_3_FINAL_REPORT.md` — beta summary (soak/scaling/TQ-RQ deferred; v0.2.1-beta tagged 06de75c)
- **Decision records (ADRs):** `docs/decisions/` — dynamic-scaling removal (2026-04-17), 30-day soak deferral + TurboQuant/RotorQuant deferral (2026-06-15), GPU-inference deferral (2026-06-16), x86 verification stance (2026-06-16), **enable SMP / Branch A (2026-06-17)**
- **Phase 4 Goal #1 (Inference Perf) Plan:** `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` — AVX2/XSAVE + SMP findings (VERIFIED) + M0–M4 plan
- **Phase 4 M3 Threadpool Design:** `phase4/docs/PHASE_4_M3_THREADPOOL_DESIGN.md` — seL4-native worker pool design (verified API; PA allocates workers since PB has no allocator; N≈5–6 bandwidth knee; design only, no code yet)
- **Phase 4 Weeks:** `phase4/weeks/weekN/WEEK_N_STATUS.md` (latest: **week05** — M3 seL4 threadpool, Gemma 5.46 tok/s @ NN=6; week04 — M2 SMP; week03 — M1 AVX2)
- **seL4 x86 QEMU Setup:** `phase3/docs/SEL4_X86_QEMU_SETUP.md`
- **x86 Rootserver Notes:** `phase3/docs/SEL4_X86_ROOTSERVER_NOTES.md`
- **GGML Portability Notes:** `phase3/src/ai/GGML_PORTABILITY_NOTES.md`
- **GGUF Parser:** `phase3/src/ai/gguf_parser.c/h`
- **x86 Rootserver:** `phase3/src/sel4/main_x86.c`
- **Shared Memory IPC:** `phase3/src/ipc/shmem_ipc.c/h`
- **USB Boot Creator:** `phase3/scripts/create_boot_usb.sh`
- **UART 16550A Driver:** `phase3/src/drivers/uart_16550.c`
- **PCI Enumeration:** `phase3/src/drivers/pci.c`
- **AHCI Discovery:** `phase3/src/drivers/ahci.c`
- **Bare-Metal Boot Guide:** `phase3/docs/BARE_METAL_BOOT_GUIDE.md`
- **Block Device (x86):** `phase3/src/drivers/blk_dev_x86.c/h`
- **x86 Timer:** `phase3/src/drivers/x86_timer.c/h`
- **NIC RTL8168:** `phase3/src/drivers/nic_rtl8168.c/h`
- **NIC Intel I211:** `phase3/src/drivers/nic_i211.c/h`
- **Tensor Ops:** `phase3/src/ai/tensor_ops.c/h`
- **Dequantization:** `phase3/src/ai/dequant.c/h`
- **BPE Tokenizer:** `phase3/src/ai/tokenizer.c/h`
- **Llama Model Header:** `phase3/src/ai/llama_model.h`
- **Llama Weight Loading:** `phase3/src/ai/llama_load.c`
- **Llama Forward Pass:** `phase3/src/ai/llama_forward.c`
- **Sampling:** `phase3/src/ai/sampling.c/h`
- **Inference API:** `phase3/src/ai/inference.c/h`
- **Quantized Inference:** `phase3/src/ai/llama_quant.c/h`
- **GGUF Vocab Extraction:** `phase3/src/ai/gguf_vocab.c/h`
- **SHIELD Safety Module:** `phase3/src/ai/shield.c/h`
- **SSM / Gated DeltaNet:** `phase3/src/ai/ssm.c/h` — Qwen3.5 hybrid recurrent layers
- **Inference Benchmark (stale F32):** `phase3/src/ai/bench_inference.c`
- **Engine Bench (quantized, all models):** `phase3/src/ai/bench_engine.c`
- **AVX2 Correctness Test:** `phase3/src/ai/test_avx2_correctness.c`
- **Parallel-for ABI Test:** `phase3/src/ai/test_parallel_for.c` — jarvis_parallel_for work-stealing correctness (reduction==serial, ctx-copy, 100x multi-dispatch, swept threads)
- **Threadpool join (note):** `phase3/src/ai/threadpool.c` uses a per-dispatch generation counter — cross-dispatch over-decrement race fixed 2026-06-17 (caught by `test_parallel_for.c`); the seL4 M3 backend ports this corrected work-stealing core
- **seL4 Threadpool (M3):** `phase3/src/ai/threadpool_sel4.c` — PB worker pool (generation publish + active-counter join + per-worker wake notifications). PA creates the N−1 workers in PB's VSpace/CSpace (PB has no allocator) and resolves `jarvis_sel4_worker_entry` from PB's ET_EXEC `.symtab` (PA never links it); PB-only `JARVIS_SEL4_SMP`. Production `NUM_NODES=6` → Gemma 4 E2B 5.46 tok/s (3.57×). Worker MUST `seL4_SetIPCBuffer` (started outside sel4runtime).
- **AVX2 Probe (M0, seL4 on-box):** `phase3/src/sel4/avx2_probe.h` — `target("avx2,fma")`-isolated YMM save/restore probe (`JARVIS_AVX2_PROBE`, default off)
- **SMP Probe (M2, seL4 on-box):** `phase3/src/sel4/smp_probe.h` — `smp_apic_id()` (CPUID leaf 1) for the numNodes gate + PA/PB core placement (`JARVIS_SMP_PROBE`, default off)
- **RotorQuant Reference:** `phase3/docs/ROTORQUANT_REFERENCE.md` — KV cache compression alternative (on `experiment/turboquant-benchmark` branch)
- **Perf Optimization Spec:** `docs/superpowers/specs/2026-04-13-perf-fused-dequant-dot-design.md` — Week 35 plan, fused dequant-dot + SIMD attention
- **GRUB Config:** `phase3/firmware/grub/grub.cfg`
- **GPU Benchmarks:** `phase3/docs/GPU_BENCHMARK_RTX2070.md`
- **Inference Benchmark Results:** `phase3/docs/INFERENCE_BENCHMARK.md`
- **Native Linux Setup:** `phase3/docs/SEL4_NATIVE_LINUX_SETUP.md`
- **Native Test Results:** `phase3/docs/NATIVE_TEST_RESULTS.md`
- **CI Workflow:** `.github/workflows/ci.yml`
- **VGA Text Driver:** `phase3/src/drivers/vga_text.c/h`
- **Debug Config:** `phase3/src/sel4/jarvis_debug.h`
  - `JARVIS_DBG_IPC` — Per-query IPC tracing in Process A (`[DBG] q=N slot=N`)
  - `JARVIS_DBG_PB` — Process B inference tracing (`[PB] handle_query, generating, decoded`)
  - `JARVIS_DBG_RING` — Ring health checks before send (`[PB] ring @... magic=... w= r=`)
  - `JARVIS_DBG_STATS` — Periodic stats every 100 queries (default: ON)
  - `JARVIS_DBG_INFER_SUMMARY` — Per-inference summary line (default: ON)
  - `JARVIS_DBG_BOOT_LOG` — NVMe log at every boot stage + auto-capture serial output (default: OFF)
  - Committed defaults ARE the stability config: STATS + INFER_SUMMARY on, IPC/PB/RING/BOOT_LOG off (BOOT_LOG off avoids NVMe write wear over 30 days)
- **NVMe Driver:** `phase3/src/drivers/nvme.c/h`
- **NVMe Log:** `phase3/src/drivers/nvme_log.c/h` — raw sector logging for bare-metal telemetry
- **NVMe Log Parser:** `phase3/scripts/parse_nvme_log.py` — extract + format log from raw device
- **NVMe Log Reset:** `phase3/scripts/clear_nvme_log.sh` — zero the telemetry-log region (derives LBA/count from `nvme_log.h`; archive first, `--yes` to write)
- **FAT32 Parser:** `phase3/src/drivers/fat32.c/h`
- **Fuzz Harness:** `phase3/src/drivers/fuzz_harness.c`
- **Security Audit (Apr 2026):** `phase3/docs/SECURITY_AUDIT_2026-04-06.md`
- **Model Bench-Off:** `phase3/docs/MODEL_BENCH_OFF_2026-04-07.md` — tiers, compatibility, deferred contenders (Gemma 4 §7, Qwen3.5 §7b, Qwen3 §9)
- **Bench Scripts:**
  - `phase3/scripts/bench_models.sh` — unified bench (speed/perplexity/quality) for JARVIS PC
  - `phase3/scripts/bench_engine_models.sh` — JARVIS engine bench (all 11 models, quantized path)
  - `phase3/scripts/bench_speed_windows.ps1` — Windows speed bench (CPU + GPU modes)
  - `phase3/scripts/bench_perplexity_windows.ps1` — Windows perplexity bench (GPU, full WikiText-2)
- **Bench Results:** `models/bench_results/bench_results.txt` (JARVIS PC speed), `models/bench_results/bench_results_mainpc.txt` (5600 CPU), `models/bench_results/bench_results_mainpc_gpu.txt` (5600 + RTX 2070)
- **Engine Bench Results:** `models/bench_results/jarvis_engine_bench.txt` (JARVIS engine, 11/11 models, quantized path, 6 model families)
- **Perplexity Results:** `models/bench_results/perplexity_results.txt`
- **Quality Results:** `models/quality_results/ALL_RESPONSES.txt` (11 models × 10 prompts, Claude-judged)
- **Judge Consensus:** `models/quality_results/JUDGE_CONSENSUS.txt` (5-agent blind consensus)
- **Final Scores:** `models/quality_results/FINAL_SCORES.txt` (7-judge combined: quality + speed + PPL + tier decisions)
- **x86 Build Script:** `phase3/scripts/build_jarvis_x86.sh`
- **QEMU NVMe Test:** `phase3/scripts/qemu_test.sh` (pass model path as arg)
- **QEMU NVMe Disk:** `~/nvme_test.img` FAT32 with the model file named to match `JARVIS_MODEL_FILE` (currently `GEMMA2B GUF`); `-drive file=...,if=none,id=nvme0,format=raw -device nvme,serial=QEMU_NVME,drive=nvme0`
- **Inference Server (PB):** `phase3/src/sel4/inference_server.c`
- **NVMe Partition Setup:** `phase3/scripts/setup_nvme_partition.sh`
- **Check CI:** `gh run list --limit 1` then `gh run view <id> --log-failed` if failed

### Rules
- Always update CLAUDE.md and week status files after completing work
- COMMIT WEEKLY with week number in message
- Update `phase2/docs/PHASE_2_PROGRESS_TRACKER.md` when completing work
- Test everything: aim for 100% pass rate
- When creating new test files (test_*.c or test_*.py), ALWAYS add a corresponding step to `.github/workflows/ci.yml` with the exact compile+run command. Verify the new CI step compiles locally before committing.
- After every `git push`, check the GitHub Actions workflow status with `gh run list --limit 1` and `gh run view` to verify CI passed. If any step failed, investigate with `gh run view --log-failed` and fix before continuing.
- Phase 2 is C-only on Pi 4 (no Python runtime on seL4)

### Codebase Metrics

Tracked `.c/.h/.py` files and their `wc -l` LOC via `git ls-files` (measured 2026-06-14):

- **Phase 0:** 4,919 LOC, 11 code files (COMPLETE)
- **Phase 1:** 40,837 LOC, 102 code files, 41 test files (COMPLETE)
- **Phase 2:** 27,236 LOC, 65 code files, 8 test files (COMPLETE)
- **Phase 3:** 35,705 LOC, 123 code files, 41 test files (IN PROGRESS — **11/11 models, 6 families, qdot + seL4 threadpool M3**)
- **Total (phases):** ~108,700 LOC, 299 code files, 89 test files (648 tracked files repo-wide)
- Per-assertion PASS counts run higher than file counts (self-reported ~383+ Phase 3 / ~635+ total).
- **Security:** March 2026 adversarial audit — 26 findings, all resolved (phase2+phase3 C). April 2026 Phase 3c audit — 25 findings (all 5 HIGH + 5 MEDIUM fixed, 7 LOW/INFO accepted). Phase 2's own audit: 6 C findings (4 fixed) + Snyk. SHIELD: keyword + model-assisted risk scoring.
- **Inference:** 11 GGUF models across 6 model families on JARVIS engine. Gemma 4 E2B (8.40/10 quality) is the single deployed model. Qwen3.5 DeltaNet SSM hybrid working. 3.22 tok/s (1T), 19.79 tok/s (16T).

### Hardware Setup

**Two-PC setup (April 2026):**
- **Main PC:** Ryzen 5 5600, RTX 2070, 32GB, ASRock B550M-ITX/ac — daily driver
- **JARVIS PC:** Ryzen 7 2700X, R9 280X (display only), 32GB DDR4 (swapped from 16GB), ASUS X470-F, 2TB NVMe, Ubuntu — dedicated, wipeable for bare-metal seL4
- **Pi 4 8GB:** Phase 2 complete, ARM fallback
- CPU-only AI inference on JARVIS PC (no GPU compute). GPU deferred to Phase 4.
- X470-F Gaming NIC: Intel I211-AT — needs igb-family driver (nic_rtl8168.c targets B550M's RTL8111H)
- Phase 2 hardware pivot doc: `phase2/docs/PHASE_2_HARDWARE_PIVOT.md`

### Technology Stack

- **Microkernel:** seL4 (formally verified*) on ARM64 + x86-64 — *\*the x86-64 build runs `KernelFastpath=ON`, outside seL4's verified X64 config ("no fast path"); functional-but-unverified by design (perf > proof) — see `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §3*
- **AI Models:** 11 GGUF models across 6 model families — Llama, Gemma 4 (PLE/SWA/KV-sharing), Phi-3 (fused QKV), Mistral (Q8_0), Qwen3 (QK norms), Qwen3.5 (DeltaNet SSM hybrid)
- **Inference:** Quantized zero-copy (Q4_0/Q4_K/Q5_K/Q6_K/Q8_0/BF16 dequant on-the-fly, ~50MB heap). Gated DeltaNet SSM for Qwen3.5 hybrid layers.
- **Build:** TII seL4 build system + CMake/Ninja
- **Cross-compiler:** aarch64-linux-gnu-gcc 13.3.0 (ARM64), gcc 13.3.0 (x86-64)
- **SIMD:** AVX2+FMA for tensor_matmul/rms_norm/qmatmul_vec/qdot/attention (`-mavx2 -mfma`); `#ifdef __AVX2__` fallback to scalar
- **Bootloader:** U-Boot 2026.01 (Pi 4), GRUB2/multiboot (x86 QEMU)
- **Hardware:** Raspberry Pi 4 (BCM2711, 8GB), JARVIS PC (Ryzen 7 2700X, R9 280X display, 32GB, 2TB NVMe, ASUS X470-F, Ubuntu), Main PC (Ryzen 5 5600, RTX 2070, 32GB, ASRock B550M-ITX/ac)
- **QEMU:** KVM-accelerated x86-64, 4GB RAM, CNode 19 (524K slots)
