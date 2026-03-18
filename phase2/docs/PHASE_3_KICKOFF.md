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

**Phase 3 eliminates the split.** A spare x86 PC (Ryzen 5 5600 + RTX 3060 12GB) enables the original vision: seL4 microkernel and AI inference on the same machine. GPU-accelerated inference delivers 75+ tokens/second for 7B models — replacing 558ms CPU inference and 7ms UART round-trips with sub-microsecond shared memory IPC.

**Approach: Staged migration (Option D from hardware research).** Phase 3a moves AI to the spare PC immediately (4-6 weeks, zero risk). Phase 3b migrates seL4 to x86 with CAmkES VM for GPU access (14-22 weeks). Pi 4 stays as fallback throughout.

**What "done" looks like:** JARVIS running standalone on one x86 machine — seL4 kernel with Linux VM guest for GPU AI, cross-VM shared memory IPC, 7B+ model inference at 75+ tok/s, 30-day stability on new platform.

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

**S3: Dynamic Model Scaling (Production)**
Implement the full 4-state scaling design from ARCHITECTURE_ENHANCEMENTS.md with GPU acceleration:
- IDLE: 1B model (CPU, instant)
- ACTIVE: 3.8B model (GPU, ~13ms)
- CRITICAL: 7B model + validator (GPU, ~26ms)
- EMERGENCY: Deterministic rules only (<1ms)

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
│                   Spare x86 PC                           │
│            Ryzen 5 5600 + RTX 3060 12GB                 │
│                                                          │
│   ┌──────────────────┐     shared    ┌────────────────┐ │
│   │  seL4 Native     │◄────memory───►│  Linux VM      │ │
│   │  (CAmkES)        │    <10μs IPC  │  (CAmkES guest)│ │
│   │                  │              │                │ │
│   │  JARVIS Kernel   │              │  RTX 3060 GPU  │ │
│   │  - Decision Cache│              │  - llama.cpp   │ │
│   │  - IPC Handler   │              │  - CUDA 7B+    │ │
│   │  - SHIELD Core   │              │  - Python AI   │ │
│   │  - x86 drivers   │              │  - Agents      │ │
│   └──────────────────┘              └────────────────┘ │
│              ↑                              ↑           │
│         seL4 kernel                   GPU passthrough   │
│     (formally verified)              (PCIe → Linux VM)  │
└─────────────────────────────────────────────────────────┘
```

**Changes:** Everything on one machine. seL4 hypervisor with Linux VM for GPU. Cross-VM shared memory IPC replaces UART. 700x+ latency improvement.

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

### Phase 3b: seL4 x86 Migration (Weeks 7-28)

**Goal:** Move seL4 to x86, establish CAmkES VM, achieve standalone operation.

| Week | Task | Deliverable |
|------|------|-------------|
| 7-8 | seL4 x86-64 build environment setup | Cross-compilation toolchain, PC99 platform config |
| 8-9 | Boot seL4 on spare PC (minimal — serial output) | `Hello World` from seL4 rootserver on x86 |
| 10-11 | Port decision cache + IPC handler to x86 rootserver | Cache queries working via serial console |
| 12-14 | CAmkES setup: Linux VM guest on x86 | Linux booting inside seL4 VM, serial access |
| 15-17 | GPU passthrough: RTX 3060 → Linux VM | `nvidia-smi` working inside VM, CUDA inference |
| 18-19 | Cross-VM shared memory IPC | Dataport connector: rootserver ↔ Linux VM |
| 20-21 | Port IPC protocol to shared memory | Replace UART framing with shared memory ring buffer |
| 22-23 | Integrate AI pipeline in Linux VM | llama.cpp + Python agents in VM, connected to rootserver |
| 24-25 | Integration testing | End-to-end: query → cache miss → VM → GPU → response |
| 26-28 | Stability testing (30-day on x86) | Match Phase 2 criteria |

**Phase 3b Exit Criteria:**
- Standalone x86 operation (single machine, no Pi 4 required)
- Cross-VM IPC latency <10μs
- GPU inference in Linux VM: 50+ tok/s for 7B
- 30-day stability: 0 crashes, <1% errors

### Phase 3c: Hardening & Polish (Weeks 29-40)

| Week | Task | Deliverable |
|------|------|-------------|
| 29-30 | Fuzz testing: network packet parsing | Fuzz harness for ICMP/ARP/IPv4 |
| 31-32 | Security audit: CAmkES VM + cross-VM IPC | SECURITY_AUDIT_PHASE3.md |
| 33-34 | Dynamic model scaling (4-state GPU) | IDLE/ACTIVE/CRITICAL/EMERGENCY on GPU |
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

### Target: Cross-VM Shared Memory (Phase 3b)

```
Linux VM → write to shared dataport → notification → seL4 rootserver reads dataport → cache lookup → write response to dataport → notification → Linux VM reads
RTT: <10μs estimated (seL4 native IPC is 0.4μs; VM exit/entry adds overhead)
Bandwidth: Memory speed (~10 GB/s)
```

### Migration Steps

1. **Define shared memory protocol** — replace UART frame format with shared memory ring buffer. Keep the same message types (QUERY, RESPONSE, HEARTBEAT, etc.) but remove SYNC bytes, CRC, and length framing (shared memory is reliable).

2. **Implement CAmkES cross-VM connector** — use seL4's dataport mechanism for shared memory between native rootserver and Linux VM. Use notification objects for doorbell signaling.

3. **Port Python client** — replace `serial.Serial` with `mmap` or shared memory access in the Linux VM. Same protocol semantics, different transport.

4. **Backward compatibility** — keep UART protocol code for Pi 4 builds. Use compile-time flag (`#ifdef JARVIS_IPC_UART` / `#ifdef JARVIS_IPC_SHMEM`) to select transport.

### Expected Performance Improvement

| Metric | Phase 2 (UART) | Phase 3b (Shared Mem) | Improvement |
|--------|----------------|----------------------|-------------|
| IPC RTT | 7ms | <10μs | 700x+ |
| Bandwidth | 11.5 KB/s | ~10 GB/s | 870,000x |
| Cache miss → AI response | 558ms + 7ms | <20ms (GPU) + <10μs | 28x+ |
| Cache hit → response | <1ms + 7ms | <1ms + <10μs | ~8x |

---

## 7. AI Model Strategy

### Phase 2 Baseline

| Model | Platform | Inference | Use Case |
|-------|----------|-----------|----------|
| Phi-3 Mini 3.8B | Host PC CPU | 558ms | Complex queries (cache misses) |
| Llama 3.2 1B | Host PC CPU | <100ms | Fast queries |
| Decision cache | Pi 4 (seL4) | <1ms | 85.7% of all queries |

### Phase 3 Target

| Model | Platform | Inference | Use Case |
|-------|----------|-----------|----------|
| Llama 7B (Q4) | RTX 3060 CUDA | ~13ms/tok, 75 tok/s | Complex reasoning |
| Phi-3 Mini 3.8B (Q4) | RTX 3060 CUDA | ~10ms/tok, ~100 tok/s | Standard queries |
| Llama 3.2 3B (Q4) | RTX 3060 CUDA | ~8ms/tok, ~125 tok/s | Fast queries |
| Llama 3.2 1B (Q4) | CPU (in VM) | <50ms | Ultra-fast fallback |
| Decision cache | seL4 native | <1ms | 85%+ of queries |

### Dynamic Model Scaling (Production)

From ARCHITECTURE_ENHANCEMENTS.md — implemented with GPU acceleration:

```
State Transitions:
  IDLE ──(query)──► ACTIVE ──(risk>0.7)──► CRITICAL ──(system threat)──► EMERGENCY
   │                  │                       │                            │
   │ 1B CPU          │ 3.8B GPU             │ 7B GPU + validator         │ Rules only
   │ <50ms           │ ~10ms/tok            │ ~26ms/tok                  │ <1ms
   │                  │                       │                            │
   ◄──(idle 30s)─────◄──(risk<0.3)──────────◄──(threat cleared)──────────┘
```

**Memory strategy:** 12GB VRAM holds the active model. Model swapping takes 2-5 seconds (load from NVMe to VRAM). Cache handles queries during model transitions.

### Quantization Strategy

| Model | Full Size | Q4_K_M | Q8_0 | Fits 12GB VRAM? |
|-------|-----------|--------|------|-----------------|
| Llama 3.2 1B | 2GB | ~0.7GB | ~1.2GB | Yes (with room) |
| Llama 3.2 3B | 6GB | ~2.0GB | ~3.5GB | Yes (with room) |
| Phi-3 Mini 3.8B | 7.6GB | ~2.5GB | ~4.2GB | Yes (with room) |
| Llama 7B | 14GB | ~4.0GB | ~7.5GB | Yes (Q4) / Yes (Q8) |
| Llama 13B | 26GB | ~7.5GB | ~14GB | Yes (Q4) / No (Q8) |

---

## 8. Risk Register

### Technical Risks

| # | Risk | Probability | Impact | Mitigation |
|---|------|-------------|--------|------------|
| 1 | GPU passthrough in CAmkES fails | Medium | High | Fallback: Linux-native with container isolation; or keep UART to Pi 4 |
| 2 | CAmkES learning curve longer than estimated | Medium | Medium | Well-documented tutorials; start with examples, iterate |
| 3 | AMD Ryzen incompatibility with seL4 PC99 | Low | High | PC99 is generic x86-64; test early (Week 7-8); Intel NUC as backup |
| 4 | Cross-VM IPC latency higher than expected | Low | Medium | Even 100μs is 70x better than UART; acceptable degradation |
| 5 | 16GB RAM insufficient for VM + AI | Low | Medium | Upgrade to 32GB DDR4 (~$50); or use smaller models |
| 6 | Stability regression on new platform | Medium | Medium | Pi 4 stays as fallback; staged migration reduces blast radius |
| 7 | NVMe/AHCI driver complexity in seL4 | Low | Low | Not needed for Phase 3 — Linux VM handles storage |

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
| 1 | Standalone operation | Single x86 machine, no Pi 4 required | Boot and run queries without external hardware |
| 2 | GPU-accelerated inference | 7B model at 50+ tok/s | llama-bench on RTX 3060 in VM |
| 3 | Cross-VM IPC | <100μs round-trip | Benchmark shared memory dataport |
| 4 | 30-day stability (x86) | 0 crashes, <1% errors | Run stability harness 30 days |
| 5 | Security audit (Phase 3) | All HIGH/CRITICAL fixed | Self-audit + fuzz testing of network/IPC |
| 6 | Cache performance maintained | >80% hit rate | Stability harness metrics |

### Should-Pass

| # | Criterion | Target |
|---|-----------|--------|
| 7 | Dynamic model scaling | 4-state GPU scaling operational |
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
| **Month 3** | 9-12 | 3b | Decision cache port, CAmkES setup | Cache working on x86, Linux VM booting |
| **Month 4** | 13-16 | 3b | GPU passthrough + cross-VM IPC | `nvidia-smi` in VM, shared memory working |
| **Month 5** | 17-20 | 3b | IPC migration + AI integration | Full pipeline: query → cache/GPU → response |
| **Month 6** | 21-24 | 3b | Integration testing | End-to-end standalone operation |
| **Month 7-8** | 25-32 | 3b→3c | 30-day stability + fuzz testing | Stability PASSED on x86, fuzz harness |
| **Month 9-10** | 33-36 | 3c | Security audit + model scaling | 4-state GPU scaling, SHIELD expansion |
| **Month 11** | 37-40 | 3c | Polish + Pi 4 maintenance | Multi-platform build, documentation |
| **Month 12** | 41-44 | Report | Final report + Phase 4 planning | PHASE_3_FINAL_REPORT.md, git tag v0.3.0-beta |

**Total estimated: 40-44 weeks** at 8-12 hours/week (~400-500 hours)

### Critical Path

```
Week 1: Spare PC assembled + Linux
  ↓
Week 2-3: GPU benchmarks + UART IPC
  ↓
Week 5-6: 3-day stability (Phase 3a DONE)
  ↓ ← Can stop here if x86 migration is blocked
Week 8-9: seL4 boots on x86
  ↓
Week 15-17: GPU passthrough in CAmkES VM
  ↓ ← HIGHEST RISK POINT
Week 18-21: Cross-VM IPC + AI integration
  ↓
Week 26-28: 30-day stability on x86 (Phase 3b DONE)
  ↓
Week 40: Phase 3c complete, final report
```

**Key decision points:**
- **Week 6:** Phase 3a complete. GPU AI working with Pi 4 over UART. Go/no-go for Phase 3b.
- **Week 17:** GPU passthrough working or not. If blocked, evaluate fallback (Linux-native, no seL4 hypervisor).
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

1. **Finish assembling spare PC** — install Linux, NVIDIA drivers, CUDA
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
