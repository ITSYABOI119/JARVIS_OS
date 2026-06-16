# JARVIS AI-OS: Phase 3 Kickoff — Beta System

**Version:** 1.0
**Date:** March 18, 2026
**Phase:** Phase 3 - Beta System (Months 24-36)
**Duration:** 12 months estimated (flexible — solo developer, part-time)
**Author:** JARVIS Development Team (Solo Developer)
**Prerequisite:** Phase 2 COMPLETE (GO recommendation — see PHASE_2_FINAL_REPORT.md)

---

## 1. Executive Summary

Phase 2 delivered a working AI-controlled operating system on Raspberry Pi 4: 21 drivers, 30-day stability (0 crashes), security audit passed, 108 tests green. The architecture is proven. But the split deployment (Pi 4 seL4 + host PC AI over 7ms UART) was always temporary.

**Phase 3 eliminates the split.** A spare x86 PC (Ryzen 5 5600 + RTX 3060 12GB) enables the original vision: seL4 microkernel and AI inference on the same machine. Pure bare metal — no Linux, no VMs. AI runs as a native seL4 userspace process via ggml/llama.cpp compiled as C. CPU inference on the Ryzen 5 5600 with sub-microsecond shared memory IPC.

**Approach: Staged migration (Option D from hardware research).** Phase 3a moves AI to the spare PC running Linux with GPU (4-6 weeks, stepping stone). Phase 3b ports seL4 to bare-metal x86 with native AI inference (14-22 weeks). Pi 4 stays as fallback throughout.

**What "done" looks like:** JARVIS running standalone on one x86 machine — pure seL4 bare metal, AI inference in native C userspace, shared memory IPC (<1μs), decision cache + CPU inference handling all queries, 30-day stability on new platform. GPU acceleration deferred to Phase 4 (Vulkan compute).

**Note:** The spare x86 PC is not yet assembled. Phase 3 starts when hardware is ready.

---

## 2. Phase 2 Results (Baseline)

| Metric | Phase 2 Result | Phase 3 Target |
|--------|----------------|----------------|
| Platform | Pi 4 (ARM64) + host PC | x86-64 standalone |
| Drivers | 21 operational | Maintain + x86 equivalents |
| Stability | 30.6 days, 0 crashes | 30 days on x86 |
| IPC latency | 7ms (UART) | <10μs (shared memory) |
| AI inference | 558ms (Phi-3 CPU) / <100ms (Llama 1B CPU) | <20ms (7B GPU) |
| Cache hit rate | 87.5% | >85% |
| Test suite | 108 PASS, 0 FAIL | 120+ PASS |
| Security | 6 findings, 4 fixed | Fuzz testing + expanded audit |
| Architecture | Split (2 machines) | Standalone (1 machine) |

---

## 3. Phase 3 Objectives

### Primary Objectives

**P1: GPU-Accelerated AI Inference (Phase 3a)**
Move AI inference from main PC CPU to spare PC RTX 3060 GPU. Achieve 75+ tok/s for 7B models with CUDA. Validate Phi-3 Mini 3.8B, Llama 3.2 3B, and Llama 7B on the RTX 3060.

**P2: Standalone x86 Operation (Phase 3b)**
Port seL4 to x86-64 spare PC. Run JARVIS rootserver as native seL4 component with Linux VM guest for GPU access. Replace UART IPC with cross-VM shared memory (<10μs latency). Achieve single-machine deployment.

**P3: 30-Day Stability on x86**
Prove the new platform is as stable as Pi 4. Run the stability harness for 30 days on x86 with 0 crashes, <1% error rate, matching Phase 2 criteria.

### Secondary Objectives

**S1: Enhanced AI Models**
Leverage RTX 3060 12GB VRAM to run larger/better models. Evaluate 7B instruction-tuned models for improved decision quality vs 1B/3B models used in Phase 2.

**S2: Security Hardening**
Fuzz testing for network packet parsing (deferred from Phase 2). Expand self-audit to cover CAmkES VM attack surface. Validate cross-VM IPC isolation.

**S3: Dynamic Model Scaling (Production) — REMOVED 2026-04-17**
The 4-state scaler was never fully built; what existed was implementation drift (miss-rate-driven swap, not the designed safety-ensemble). System now ships single-model (Gemma 4 E2B). Success criterion changed to: single-model (Gemma 4 E2B) stable for 30 days. See `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`.

### Stretch Objectives

**X1: Multi-Platform Support**
Maintain Pi 4 ARM64 build alongside x86-64. Same codebase, architecture-conditional compilation. Proves portability.

**X2: HDMI/Display Output**
VideoCore VI driver for Pi 4 or framebuffer for x86. Visual status display instead of serial-only output.

---

## 4. Architecture Changes

### Phase 2 Architecture (Current)

```
┌──────────────────┐       UART        ┌──────────────────┐
│   Main PC        │◄─────────────────►│   Pi 4 (seL4)    │
│   (Host, Linux)  │   7ms RTT         │   (Bare metal)   │
│                  │   115200 baud     │                  │
│   Python AI      │                   │   Decision Cache  │
│   (CPU only)     │                   │   21 drivers      │
└──────────────────┘                   └──────────────────┘
     Cannot wipe                            Proven stable
```

### Phase 3a Architecture (Immediate — Weeks 1-6)

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

**Changes:** Main PC replaced by spare PC. GPU inference. Same UART protocol. Pi 4 code untouched.

### Phase 3b Architecture (Target — Weeks 7-28)

```
┌─────────────────────────────────────────────────────────┐
│              JARVIS OS (Spare x86 PC)                    │
│            Ryzen 5 5600 + RTX 3060 12GB                 │
│                                                          │
│   seL4 Microkernel (Ring 0)                              │
│   ├── Interrupt handling (<1μs)                          │
│   ├── Memory management                                 │
│   └── IPC primitives                                    │
│              ↕ shared memory (<1μs)                      │
│   AI Decision Engine (Ring 3)                            │
│   ├── ggml/llama.cpp (native C, CPU inference)          │
│   ├── Decision cache (258 patterns, <1ms)               │
│   ├── SHIELD safety framework                           │
│   └── Dynamic model scaling                             │
│              ↕                                           │
│   Drivers (Ring 3)                                       │
│   ├── AHCI/NVMe storage                                │
│   ├── Intel/Realtek NIC                                 │
│   ├── USB HID                                           │
│   └── Serial console (debug)                            │
│                                                          │
│   No Linux. No VM. Pure seL4 bare metal.                │
└─────────────────────────────────────────────────────────┘
```

**Changes:** Everything on one machine. Pure bare-metal seL4 — no Linux, no VMs. AI inference via ggml/llama.cpp compiled as native C seL4 userspace. CPU-only inference initially (Ryzen 5 5600, ~5-15 tok/s for 7B, ~20-50 tok/s for 1B-3B). GPU access (RTX 3060) deferred to Phase 4 (Vulkan compute or native driver). Decision cache handles 85%+ of queries in <1ms regardless.

**Note:** The spare x86 PC is not yet assembled. Phase 3 starts when hardware is ready.

---

## 5. Migration Plan

### Phase 3a: GPU-Accelerated Host (Weeks 1-6)

**Goal:** Replace main PC with dedicated spare PC for AI inference. Zero code changes to Pi 4.

| Week | Task | Deliverable |
|------|------|-------------|
| 1 | Assemble spare PC, install Linux (Ubuntu/Fedora) | Bootable system |
| 1 | Install NVIDIA drivers + CUDA toolkit | `nvidia-smi` working |
| 2 | Install llama.cpp with CUDA, benchmark models | Performance numbers for Phi-3, Llama 3B, 7B |
| 2 | Install Python environment, copy AI agents | `uart_ipc_client.py`, `stability_harness.py` working |
| 3 | Connect USB-serial adapter, UART to Pi 4 | IPC communication verified |
| 3-4 | Run 1-hour stability test (spare PC ↔ Pi 4) | 99.7%+ pass rate |
| 4-5 | Benchmark GPU inference latency end-to-end | Measure query→cache miss→GPU→response time |
| 5-6 | Run 3-day stability test | Confidence in new host |

**Phase 3a Exit Criteria:**
- GPU inference working: 7B model at 50+ tok/s
- UART IPC stable: 99.7%+ pass rate over 3 days
- End-to-end latency for cache misses measured and documented

### Phase 3b: seL4 x86 Bare Metal (Weeks 7-28)

**Goal:** Port seL4 to x86-64 spare PC. Run JARVIS as a pure bare-metal OS — no Linux, no VMs.

| Week | Task | Deliverable |
|------|------|-------------|
| 7-8 | seL4 x86-64 build environment setup | PC99 platform config, GRUB/EFI boot |
| 8-9 | Boot seL4 on spare PC (minimal — serial output) | `Hello World` from seL4 rootserver on x86 |
| 10-11 | Port decision cache + SHIELD to x86 rootserver | Cache queries working via serial console |
| 12-13 | x86 driver: serial UART (16550/PCH) | Debug console + IPC fallback |
| 14-15 | x86 driver: AHCI/NVMe storage | Basic read/write for persistence |
| 16-17 | x86 driver: Intel/Realtek NIC | Basic networking (ARP, ICMP) |
| 18-19 | Integrate ggml library as seL4 userspace | llama.cpp core compiled as C library |
| 20-21 | AI inference in seL4 userspace (CPU) | 1B-3B model running, benchmark tok/s |
| 22-23 | Shared memory IPC: rootserver ↔ AI process | Replace UART framing with shared memory ring buffer |
| 24-25 | Integration testing | End-to-end: query → cache/AI → response, all native |
| 26-28 | Stability testing (30-day on x86) | Match Phase 2 criteria |

**Phase 3b Exit Criteria:**
- Standalone x86 operation (single machine, no Pi 4, no Linux, no VM)
- Native seL4 IPC latency <1μs (shared memory between processes)
- AI inference: 1B-3B model running natively in seL4 userspace
- 30-day stability: 0 crashes, <1% errors

### Phase 3c: Hardening & Polish (Weeks 29-40)

| Week | Task | Deliverable |
|------|------|-------------|
| 29-30 | Fuzz testing: network packet parsing | Fuzz harness for ICMP/ARP/IPv4 |
| 31-32 | Security audit: x86 drivers + IPC | SECURITY_AUDIT_PHASE3.md |
| 33-34 | Dynamic model scaling (4-state CPU) | IDLE(1B)/ACTIVE(3B)/CRITICAL(3B+validator)/EMERGENCY(rules) |
| 35-36 | Enhanced SHIELD (full 6-component) | Shadow execution + learning from failures |
| 37-38 | Pi 4 ARM64 build maintenance | Verify Phase 2 code still builds + tests pass |
| 39-40 | Documentation + final report | PHASE_3_FINAL_REPORT.md |

---

## 6. IPC Migration

### Current: UART (Phase 2)

```
Python → UART frame (10 bytes header + payload) → 115200 baud → Pi 4 UART RX → parse → cache lookup → response frame → UART TX → Python
RTT: 7ms median, 8.19ms p95
Bandwidth: ~11.5 KB/s
```

### Target: Native Shared Memory IPC (Phase 3b)

```
AI Process (seL4 userspace) → write to shared memory page → seL4 notification → Rootserver reads shared memory → cache lookup → write response → notification → AI Process reads
RTT: <1μs (seL4 native IPC is 0.4μs on x86-64 Skylake, ~0.3μs on Ryzen)
Bandwidth: Memory speed (~25 GB/s DDR4-3200)
```

### Migration Steps

1. **Define shared memory protocol** — replace UART frame format with shared memory ring buffer. Keep the same message types (QUERY, RESPONSE, HEARTBEAT, etc.) but remove SYNC bytes, CRC, and length framing (shared memory is reliable, no corruption).

2. **Implement seL4 shared page** — map a shared memory region between rootserver and AI process. Use seL4 notification objects for doorbell signaling (wake on new message).

3. **Port AI client to C** — the AI inference process is native C (ggml library), so the IPC client is also C. Replace Python `serial.Serial` with direct shared memory reads/writes.

4. **Backward compatibility** — keep UART protocol code for Pi 4 ARM64 builds. Use compile-time flag (`#ifdef JARVIS_IPC_UART` / `#ifdef JARVIS_IPC_SHMEM`) to select transport.

### Expected Performance Improvement

| Metric | Phase 2 (UART) | Phase 3b (Shared Mem) | Improvement |
|--------|----------------|----------------------|-------------|
| IPC RTT | 7ms | <1μs | 7,000x+ |
| Bandwidth | 11.5 KB/s | ~25 GB/s (DDR4) | 2,000,000x |
| Cache miss → AI response | 558ms + 7ms | 50-200ms (CPU) + <1μs | 3-11x |
| Cache hit → response | <1ms + 7ms | <1ms + <1μs | ~8x |

---

## 7. AI Model Strategy

### Phase 2 Baseline

| Model | Platform | Inference | Use Case |
|-------|----------|-----------|----------|
| Phi-3 Mini 3.8B | Host PC CPU | 558ms | Complex queries (cache misses) |
| Llama 3.2 1B | Host PC CPU | <100ms | Fast queries |
| Decision cache | Pi 4 (seL4) | <1ms | 85.7% of all queries |

### Phase 3 Target (Bare-Metal CPU Inference)

| Model | Platform | Inference | Use Case |
|-------|----------|-----------|----------|
| Llama 3.2 1B (Q4) | Ryzen 5 5600 CPU (native seL4) | ~20-50 tok/s est. | Fast queries |
| Llama 3.2 3B (Q4) | Ryzen 5 5600 CPU (native seL4) | ~8-15 tok/s est. | Standard queries |
| Phi-3 Mini 3.8B (Q4) | Ryzen 5 5600 CPU (native seL4) | ~5-12 tok/s est. | Complex reasoning |
| Decision cache | seL4 native | <1ms | 85%+ of queries |

**Note:** These are CPU-only estimates. The Ryzen 5 5600 (6C/12T, 3.5-4.4 GHz) with AVX2 should outperform the Cortex-A72 significantly. Exact numbers need benchmarking once hardware is assembled. The decision cache handling 85%+ of queries means only ~15% of operations actually hit the inference path.

### Phase 3a Target (GPU on Linux Host — Stepping Stone)

During Phase 3a, the spare PC runs Linux with RTX 3060 CUDA:

| Model | Platform | Inference | Use Case |
|-------|----------|-----------|----------|
| Llama 7B (Q4) | RTX 3060 CUDA | ~75 tok/s | Complex reasoning |
| Phi-3 Mini 3.8B (Q4) | RTX 3060 CUDA | ~100 tok/s est. | Standard queries |
| Llama 3.2 3B (Q4) | RTX 3060 CUDA | ~125 tok/s est. | Fast queries |

These GPU speeds are available in Phase 3a while the bare-metal port is in progress.

### Dynamic Model Scaling (Production)

From ARCHITECTURE_ENHANCEMENTS.md — implemented with CPU inference on bare metal:

```
State Transitions:
  IDLE ──(query)──► ACTIVE ──(risk>0.7)──► CRITICAL ──(system threat)──► EMERGENCY
   │                  │                       │                            │
   │ 1B CPU          │ 3B CPU               │ 3.8B CPU + validator       │ Rules only
   │ ~20ms/tok       │ ~80ms/tok            │ ~100ms/tok                 │ <1ms
   │                  │                       │                            │
   ◄──(idle 30s)─────◄──(risk<0.3)──────────◄──(threat cleared)──────────┘
```

**Memory strategy:** 16GB system RAM holds the active model + seL4 + drivers. Model swapping takes 1-3 seconds (load from NVMe to RAM). Cache handles queries during model transitions. Upgrade to 32GB DDR4 (~$50) recommended if running multiple models simultaneously.

### Quantization Strategy

| Model | Full Size | Q4_K_M | Q8_0 | Fits 16GB RAM? |
|-------|-----------|--------|------|----------------|
| Llama 3.2 1B | 2GB | ~0.7GB | ~1.2GB | Yes (with room) |
| Llama 3.2 3B | 6GB | ~2.0GB | ~3.5GB | Yes (with room) |
| Phi-3 Mini 3.8B | 7.6GB | ~2.5GB | ~4.2GB | Yes (with room) |
| Llama 7B | 14GB | ~4.0GB | ~7.5GB | Yes (Q4) / Yes (Q8) |
| Llama 13B | 26GB | ~7.5GB | ~14GB | Tight (Q4) / No (Q8) |

### GPU Access — Phase 4 Goal

GPU acceleration (RTX 3060 CUDA) is deferred to Phase 4. Options to explore:
- **Vulkan compute** — open-source, no proprietary drivers, llama.cpp has Vulkan backend
- **Native GPU driver** — write minimal NVIDIA register-level driver for compute shaders (very hard)
- **Hybrid approach** — seL4 bare metal for kernel/AI, minimal Linux shim for GPU only (last resort)

For Phase 3, CPU inference on the Ryzen 5 5600 is the target. The 85% cache hit rate means most queries never touch inference anyway.

---

## 8. Risk Register

### Technical Risks

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| 1 | AMD Ryzen incompatibility with seL4 PC99 | Low | High | PC99 is generic x86-64; test early (Week 7-8) |
| 2 | ggml/llama.cpp won't compile for seL4 userspace | Medium | High | ggml is portable C; may need muslc stubs for missing POSIX calls |
| 3 | x86 driver complexity (AHCI/NIC) | Medium | Medium | Start with serial-only, add drivers incrementally; Linux references |
| 4 | CPU inference too slow for usability | Low | Medium | Cache handles 85%+ queries; 1B model at ~20+ tok/s is acceptable |
| 5 | 16GB RAM insufficient for model + seL4 | Low | Medium | Upgrade to 32GB DDR4 (~$50); or use smaller models |
| 6 | Stability regression on new platform | Medium | Medium | Pi 4 stays as fallback; staged migration reduces blast radius |
| 7 | Spare PC not assembled (blocks Phase 3) | Medium | High | Phase 3a can use main PC temporarily if needed; Pi 4 keeps running |

### Non-Technical Risks

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| 8 | Solo dev burnout / time constraints | Medium | High | 8-12 hrs/week pace; Phase 3a provides quick wins for motivation |
| 9 | Hardware failure (spare PC) | Low | Medium | Pi 4 stays operational as fallback |
| 10 | Scope creep (too many features) | Medium | Medium | Clear staged milestones; Phase 3a/3b/3c structure |

---

## 9. Hardware Budget

| Item | Cost | Status |
|------|------|--------|
| Spare x86 PC (Ryzen 5 5600, RTX 3060, 16GB) | $0 | Already owned / being assembled |
| Raspberry Pi 4 8GB | $0 | Already running Phase 2 |
| Raspberry Pi 5 4GB | $0 | Already owned (available for experiments) |
| USB-serial adapter | $0 | Already owned from Phase 2 |
| **Optional:** 32GB DDR4 upgrade | ~$50 AUD | Recommended if running 13B models |
| **Total** | **$0-50 AUD** | |

**Phase 2 total hardware cost:** $215.82 AUD
**Phase 3 additional cost:** $0-50 AUD
**Project total hardware:** ~$266 AUD

---

## 10. Success Criteria / Gate

### Must-Pass (Phase 3 → Phase 4 Gate)

| # | Criterion | Target | How to Validate |
|---|-----------|--------|-----------------|
| 1 | Standalone operation | Single x86 machine, no Pi 4, no Linux, no VM | Boot and run queries without external hardware |
| 2 | Native AI inference | 1B-3B model in seL4 userspace (CPU) | ggml benchmark on Ryzen 5 5600 |
| 3 | Native IPC | <1μs round-trip (shared memory) | Benchmark seL4 shared page IPC |
| 4 | 30-day stability (x86) | 0 crashes, <1% errors | Run stability harness 30 days |
| 5 | Security audit (Phase 3) | All HIGH/CRITICAL fixed | Self-audit + fuzz testing of network/IPC |
| 6 | Cache performance maintained | >80% hit rate | Stability harness metrics |

### Should-Pass

| # | Criterion | Target |
|---|-----------|--------|
| 7 | Dynamic model scaling | 4-state CPU scaling operational |
| 8 | Pi 4 build maintained | Phase 2 tests still pass on ARM64 |
| 9 | Enhanced SHIELD | All 6 components implemented |

### Stretch

| # | Criterion | Target |
|---|-----------|--------|
| 10 | Multi-platform build | Same codebase builds for ARM64 + x86-64 |
| 11 | Display output | Framebuffer or HDMI on at least one platform |
| 12 | 2+ hardware configs validated | x86 + Pi 4 both operational |

---

## 11. Technical Debt from Phase 2

Items flagged in PHASE_2_FINAL_REPORT.md and SECURITY_SELF_AUDIT.md:

| Item | Severity | Phase 3 Action |
|------|----------|----------------|
| F4: uint64 timeout wraparound | LOW | No action (584,942-year overflow) |
| F5: Cache action field in snprintf | LOW | Validate if cache becomes dynamic |
| diskcache Snyk vulnerability (no patch) | MEDIUM | Pin when llama-cpp-python updates |
| CRLF mixed line endings | LOW | Normalize all files in Phase 3 |
| No fuzz testing for network parsing | MEDIUM | Phase 3c: build fuzz harness |
| UART p99 RTT spike (513ms) | MEDIUM | Eliminated by shared memory IPC |
| Split architecture | HIGH | Primary Phase 3 objective |
| Single hardware config | MEDIUM | x86 adds second platform |
| No alpha/beta testers | LOW | Defer to Phase 4; focus on stability |

---

## 12. Preliminary Timeline

### Month-by-Month Overview

| Month | Weeks | Phase | Focus | Key Deliverable |
|-------|-------|-------|-------|-----------------|
| **Month 1** | 1-4 | 3a | Spare PC setup + GPU AI | RTX 3060 benchmarks, UART IPC from new host |
| **Month 2** | 5-8 | 3a→3b | Stability validation, seL4 x86 boot | 3-day stability test, seL4 `Hello World` on x86 |
| **Month 3** | 9-12 | 3b | Decision cache port + x86 drivers | Cache working on x86, serial + basic storage |
| **Month 4** | 13-16 | 3b | NIC driver + ggml integration | Networking + AI inference compiling for seL4 |
| **Month 5** | 17-20 | 3b | Native AI inference + shared memory IPC | Full pipeline: query → cache/CPU AI → response |
| **Month 6** | 21-24 | 3b | Integration testing | End-to-end standalone operation |
| **Month 7-8** | 25-32 | 3b→3c | 30-day stability + fuzz testing | Stability PASSED on x86, fuzz harness |
| **Month 9-10** | 33-36 | 3c | Security audit + model scaling | 4-state CPU scaling, SHIELD expansion |
| **Month 11** | 37-40 | 3c | Polish + Pi 4 maintenance | Multi-platform build, documentation |
| **Month 12** | 41-44 | Report | Final report + Phase 4 planning | PHASE_3_FINAL_REPORT.md, git tag v0.2.1-beta |

**Total estimated: 40-44 weeks** at 8-12 hours/week (~400-500 hours)

### Critical Path

```
Week 1: Spare PC assembled + Linux
  ↓
Week 2-3: GPU benchmarks + UART IPC to Pi 4
  ↓
Week 5-6: 3-day stability (Phase 3a DONE)
  ↓ ← Can stop here if x86 bare-metal port is blocked
Week 8-9: seL4 boots on x86 bare metal
  ↓
Week 14-15: x86 drivers (serial, AHCI, NIC)
  ↓
Week 18-21: ggml compiled for seL4 userspace
  ↓ ← HIGHEST RISK POINT (ggml portability to seL4)
Week 22-25: Shared memory IPC + integration
  ↓
Week 26-28: 30-day stability on x86 (Phase 3b DONE)
  ↓
Week 40: Phase 3c complete, final report
```

**Key decision points:**
- **Week 6:** Phase 3a complete. GPU AI working with Pi 4 over UART from spare PC. Go/no-go for bare-metal port.
- **Week 9:** seL4 boots on Ryzen or not. If AMD incompatibility, investigate early.
- **Week 21:** ggml running in seL4 userspace or not. If portability issues, evaluate what POSIX stubs are needed.
- **Week 28:** 30-day stability on x86. If passed, Phase 3b complete. If not, debug and re-run.

---

## 13. Development Environment

### Phase 3a (Linux host for AI)

| Component | Version/Details |
|-----------|----------------|
| OS | Ubuntu 22.04 or 24.04 LTS |
| GPU Driver | NVIDIA 535+ |
| CUDA | 12.x |
| llama.cpp | Latest (CUDA backend) |
| Python | 3.10+ |
| Serial | pyserial + USB-serial adapter (CP2102) |

### Phase 3b (seL4 x86 development)

| Component | Version/Details |
|-----------|----------------|
| seL4 | Latest stable (x86-64 PC99) |
| CAmkES | Latest stable |
| Cross-compiler | gcc (native x86-64) |
| Build system | CMake + Ninja |
| Bootloader | GRUB2 or seL4 EFI loader |
| VM guest | Buildroot Linux (minimal) |

### Continued from Phase 2

| Component | Version/Details |
|-----------|----------------|
| Pi 4 firmware | Phase 2 kernel8.img (unchanged) |
| UART protocol | Same 14-type protocol (QUERY, RESPONSE, etc.) |
| Decision cache | 258 patterns, 85.7% hit rate |
| Test harness | stability_harness.py (30-day capable) |

---

## 14. Relationship to Unified Plan

The JARVIS_UNIFIED_PLAN.md defines Phase 3 with a $720K budget, 6-person team, 100 beta users, and 10+ hardware configurations. None of that applies to the actual project (solo developer, $0 budget, no beta users).

**What we take from the unified plan:**
- The vision: standalone AI OS with full autonomous operation
- The architecture: microkernel (Ring 0) + AI (Ring 3) with shared memory IPC
- The safety framework: SHIELD with staged execution and risk scoring
- The model scaling: IDLE → ACTIVE → CRITICAL → EMERGENCY states

**What we scope down:**
- 10+ hardware configs → 2 (x86 + Pi 4)
- 100 beta users → 0 (private project, stability harness replaces testers)
- External security audit → self-audit + fuzz testing
- Full autonomous operation → supervised autonomy with proven safety
- $720K budget → $0-50

**What we defer to Phase 4:**
- Self-modification framework
- Learning system (user preferences)
- Beta release to external users
- 7-day autonomous operation
- Advanced multi-agent coordination

---

## 15. Next Steps (Immediate)

### Pre-Hardware Interim Work — ✅ ALL DONE

All 8 pre-work tasks completed. Additionally, significant Phase 3b code written early:
- **All 8 pre-work tasks:** DONE (seL4 QEMU, ggml test, IPC, portable code, headers, tag, Pi 5, rootserver)
- **GGUF parser:** C-only replacement for C++ gguf.cpp — 9/9 tests pass
- **x86 drivers:** UART 16550A (7/7), PCI enumeration (11/11), AHCI discovery (5/5)
- **Total:** 32 files, 8,636 LOC, 64 tests in phase3/src/

### Once Spare PC is Assembled (Week 1+)

1. **Install Linux + NVIDIA drivers + CUDA** on spare PC
2. **Benchmark RTX 3060** — run `llama-bench` with Phi-3, Llama 3B, Llama 7B
3. **Connect UART to Pi 4** — verify IPC from new host
4. **Run 1-hour stability test** — confirm spare PC ↔ Pi 4 works
5. **Document Phase 3a results** — create `phase3/weeks/week1/` status doc
6. **Begin weekly commits** — maintain Phase 2's discipline

---

**Phase 3 Kickoff Complete**

---

*Kickoff Date: March 18, 2026*
*Phase 2 Completion: March 18, 2026 (Week 49, 30-day stability PASSED)*
*Phase 3 Start: Upon spare PC assembly (estimated April 2026)*
*Hardware: Spare x86 PC (Ryzen 5 5600, RTX 3060 12GB, 16GB DDR4) + Pi 4 8GB*
*Budget: $0-50 AUD additional*
*Approach: Staged migration (Option D — see PHASE_3_HARDWARE_RESEARCH.md)*
*Target: Standalone x86 operation with GPU-accelerated AI inference*

**See Also:**
- `PHASE_2_FINAL_REPORT.md` — Phase 2 results and GO recommendation
- `PHASE_3_HARDWARE_RESEARCH.md` — Hardware option analysis and benchmarks
- `ARCHITECTURE_ENHANCEMENTS.md` — Decision Cache, SHIELD, Dynamic Scaling designs
- `JARVIS_UNIFIED_PLAN.md` — Original 36-month master plan (aspirational scope)
- `SEL4_PI5_PORTING_RESEARCH.md` — Pi 5 feasibility (deferred)
