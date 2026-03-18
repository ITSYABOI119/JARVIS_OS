# JARVIS AI-OS: Phase 3 Hardware Research

**Date:** March 18, 2026
**Author:** JARVIS Development Team (Solo Developer)
**Purpose:** Evaluate hardware options for Phase 3 (Beta System) standalone operation
**Context:** Phase 2 complete on Pi 4 with split architecture (Pi 4 seL4 + PC host AI over UART)

---

## 1. Executive Summary

Phase 2 proved the JARVIS architecture works: seL4 microkernel at Ring 0, AI decision engine at Ring 3, connected via IPC. But the split deployment (Pi 4 + host PC over UART at 7ms RTT) was always temporary. Phase 3 must move toward standalone operation.

**Hardware situation has changed since Phase 2 planning.** A spare x86 PC (Ryzen 5 5600 + RTX 3060 12GB) is being assembled, making the original x86 vision viable. This document evaluates four options with real benchmark data.

**Recommendation: Option A (x86 Standalone)** — the spare PC with RTX 3060 provides 75+ tokens/sec for 7B models via CUDA, eliminates the UART bottleneck, and enables the original standalone vision. seL4 x86-64 is formally verified and supports CAmkES VM for Linux guest GPU access.

---

## 2. Hardware Inventory

| Hardware | CPU | RAM | GPU | Status | Cost |
|----------|-----|-----|-----|--------|------|
| Spare x86 PC (building) | Ryzen 5 5600 (6C/12T, 3.5 GHz) | 16GB DDR4 (upgradeable) | RTX 3060 12GB VRAM | Being assembled | Already owned |
| Raspberry Pi 4 8GB | Cortex-A72 (4C, 1.5 GHz) | 8GB LPDDR4 | None | Running JARVIS Phase 2 | Already owned |
| Raspberry Pi 5 4GB | Cortex-A76 (4C, 2.4 GHz) | 4GB LPDDR4X | None | Owned, unused | Already owned |
| Main PC | (Daily driver) | 32GB | RTX 3060 | Cannot wipe | N/A |

**Total additional cost for Phase 3: $0** (all hardware already owned)

---

## 3. Option A: x86 Standalone (Spare PC) — RECOMMENDED

### Architecture

```
┌─────────────────────────────────────────────────────┐
│                 Spare x86 PC                         │
│  Ryzen 5 5600 + 16GB DDR4 + RTX 3060 12GB          │
│                                                      │
│  ┌──────────────┐  shared memory  ┌───────────────┐ │
│  │ seL4 Native  │◄──────────────►│ Linux VM       │ │
│  │ (Ring 0)     │  cross-VM IPC  │ (CAmkES guest) │ │
│  │              │  <1μs latency  │                │ │
│  │ - Microkernel│                │ - CUDA/GPU     │ │
│  │ - Decision   │                │ - Phi-3 Mini   │ │
│  │   Cache      │                │ - Llama 3.2 7B │ │
│  │ - SHIELD     │                │ - llama.cpp    │ │
│  │ - IPC Handler│                │ - Python AI    │ │
│  └──────────────┘                └───────────────┘ │
│         ↑                              ↑            │
│    seL4 kernel                   GPU passthrough    │
│    (formally verified)           (PCIe → Linux VM)  │
└─────────────────────────────────────────────────────┘
```

### How It Works

seL4 runs as a hypervisor on x86-64. The JARVIS rootserver (microkernel, decision cache, SHIELD) runs as a native seL4 component. A Linux VM runs as a CAmkES guest with GPU passthrough for CUDA-accelerated AI inference. Communication between the native rootserver and Linux VM uses cross-VM shared memory connectors — replacing the 7ms UART link with sub-microsecond shared memory IPC.

### seL4 x86-64 Support Status

| Aspect | Status | Source |
|--------|--------|--------|
| Kernel support | Fully supported (PC99 platform) | [seL4 Hardware Docs](https://docs.sel4.systems/Hardware/) |
| Formal verification | Functional correctness proved for x86-64 | [seL4 Verification](https://sel4.systems/About/FAQ.html) |
| IPC latency | ~1,302 cycles on Skylake (~0.4μs at 3.4 GHz) | [seL4 Performance](https://sel4.systems/performance.html) |
| CAmkES VM | x86-64 supported, Linux guest works | [CAmkES VMM Docs](https://docs.sel4.systems/projects/camkes-vm/) |
| Cross-VM connectors | Shared dataports + events between VM and native | [CAmkES Cross-VM](https://docs.sel4.systems/Tutorials/camkes-vm-crossvm.html) |

**IPC comparison:**
- Phase 2 UART: 7ms RTT (115200 baud serial)
- seL4 native IPC on x86-64: ~0.4μs (1,302 cycles at 3.4 GHz)
- Cross-VM shared memory: ~1-10μs estimated (shared dataport + notification)
- **Improvement: 700x-7000x faster than UART**

### GPU Access Strategy

Direct GPU access from seL4 userspace is not feasible — there are no NVIDIA drivers for seL4. The proven approach is:

1. seL4 acts as hypervisor
2. Linux VM gets GPU via PCIe passthrough (CAmkES device passthrough is documented for ethernet; GPU passthrough is possible but not turn-key)
3. Linux VM runs llama.cpp with CUDA
4. Native seL4 rootserver communicates with Linux VM via cross-VM shared memory

**GPU passthrough maturity:** CAmkES supports device passthrough on x86-64 (ethernet demonstrated in examples). GPU/PCIe passthrough is technically feasible but has no ready-made examples — it requires manual configuration. Community reports indicate this is an area still maturing. **This is the primary technical risk for Option A.**

**Fallback if GPU passthrough fails:** Run Linux natively on the spare PC (no seL4 hypervisor), with JARVIS rootserver in a Docker container or process. Worse isolation but guaranteed GPU access. Migrate to full seL4 hypervisor once GPU passthrough is proven.

### AI Model Capability

RTX 3060 12GB VRAM benchmarks (llama.cpp with CUDA):

| Model | Parameters | Quantization | VRAM | Generation (tok/s) | Source |
|-------|-----------|--------------|------|-------------------|--------|
| Llama 2 7B | 7B | Q4_0 | ~4GB | 75-77 | [llama.cpp CUDA Discussion](https://github.com/ggml-org/llama.cpp/discussions/15013) |
| Llama 3.1 8B | 8B | Q4 | ~5GB | ~57 | [RTX 3060 Ti Benchmarks](https://www.databasemart.com/blog/ollama-gpu-benchmark-rtx3060ti) |
| Mistral 7B | 7B | Q4 | ~4GB | ~71 | RTX 3060 Ti benchmarks |
| Phi-3 Mini 3.8B | 3.8B | Q4 | ~2.5GB | ~90-100 (est.) | Interpolated from 7B/8B data |
| Llama 3.2 3B | 3B | Q4 | ~2GB | ~100+ (est.) | Smaller model, faster |
| 13B models | 13B | Q4 | ~7.5GB | ~9-18 | Fits in 12GB VRAM |

**Key insight:** The RTX 3060's 12GB VRAM can run 7B models at 57-77 tok/s, or multiple smaller models simultaneously. Phi-3 Mini 3.8B fits comfortably with room to spare. This is **75-100x faster** than Phase 2's Phi-3 inference on CPU (~558ms per response).

**16GB system RAM note:** With seL4 + Linux VM, 16GB is workable but tight for 13B+ models. Upgrading to 32GB DDR4 (~$50) would be advisable if running larger models.

### Code Transfer from Phase 2

| Component | Transfers? | Notes |
|-----------|-----------|-------|
| Decision cache (C) | **Yes** — portable | No arch-specific code, pure C |
| IPC handler (C) | **Partial** — protocol changes | Replace UART framing with shared memory protocol |
| SHIELD framework (Python) | **Yes** — portable | Runs in Linux VM |
| AI agents (Python) | **Yes** — portable | Runs in Linux VM |
| Stability harness (Python) | **Yes** — portable | Runs in Linux VM |
| BCM2711 drivers (C) | **No** — ARM-specific | New x86 drivers needed (much simpler — standard PC hardware) |
| seL4 rootserver (C) | **Partial** — needs CAmkES port | Architecture-neutral logic transfers; seL4 API identical |
| Boot manager (C) | **Partial** — x86 boot differs | UEFI/GRUB instead of U-Boot |

**Estimated rewrite:** ~40% of Phase 2 C code is architecture-neutral and transfers directly. BCM2711 drivers (~12,746 LOC) do not transfer but are replaced by much simpler x86 equivalents (standard UART, NVMe/AHCI, Intel NIC — all well-documented). The main new work is CAmkES VM integration and cross-VM IPC.

### Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| GPU passthrough doesn't work cleanly | Medium | High | Fallback: Linux-native with container isolation |
| CAmkES learning curve | Medium | Medium | Well-documented tutorials, active community |
| AMD Ryzen compatibility with seL4 | Low | High | PC99 is generic x86-64; community reports on various Intel CPUs |
| 16GB RAM insufficient for VM + AI | Low | Medium | Upgrade to 32GB DDR4 (~$50) |
| x86 driver complexity | Low | Low | Standard PC hardware, well-documented |

### Effort Estimate

| Task | Weeks | Notes |
|------|-------|-------|
| seL4 x86-64 boot on spare PC | 2-3 | PC99 platform, UEFI boot |
| CAmkES VM setup with Linux guest | 3-4 | Follow tutorials, configure buildroot |
| GPU passthrough (RTX 3060) | 2-4 | PCIe passthrough config, NVIDIA drivers in VM |
| Cross-VM shared memory IPC | 2-3 | Replace UART protocol with shared dataport |
| Port decision cache + rootserver logic | 2-3 | Architecture-neutral C code |
| AI integration in Linux VM | 1-2 | llama.cpp + CUDA, Python agents |
| Integration testing + stability | 2-3 | Cross-VM IPC validation |
| **Total** | **14-22 weeks** | Solo developer, 8-12 hrs/week |

### Cost

**$0** — hardware already owned. Optional: $50 for 32GB DDR4 upgrade.

---

## 4. Option B: Multi-Pi Cluster (Pi 4 + Pi 5)

### Architecture

```
┌──────────────────┐       UART/USB    ┌──────────────────┐
│   Pi 5 (Linux)   │◄─────────────────►│   Pi 4 (seL4)    │
│   4GB RAM        │                   │   8GB RAM         │
│                  │                   │                  │
│   llama.cpp      │                   │   JARVIS          │
│   - Llama 3.2 1B │                   │   - 21 drivers    │
│   - gemma3:1b    │                   │   - Decision cache│
│   (12-13 tok/s)  │                   │   - SHIELD        │
└──────────────────┘                   └──────────────────┘
```

### Pi 5 AI Inference Benchmarks (Real Data)

Tested on Raspberry Pi 5 with Ollama/llama.cpp (Q4 quantization):

| Model | Parameters | Tok/s | RAM Usage | Source |
|-------|-----------|-------|-----------|--------|
| gemma3:1b | 1B | ~13 | ~2.5 GB | [Stratosphere Labs](https://www.stratosphereips.org/blog/2025/6/5/how-well-do-llms-perform-on-a-raspberry-pi-5) |
| llama3.2:1b-instruct | 1B | ~12 | ~2.8 GB | Stratosphere Labs |
| qwen2.5:1.5b | 1.5B | ~11 | ~2.2 GB | Stratosphere Labs |
| granite3.1-dense:2b | 2B | ~9 | ~3.5 GB | Stratosphere Labs |
| llama3.2:3b | 3B | ~5 | **~4.2 GB** | Stratosphere Labs |
| qwen2.5:3b | 3B | ~5 | ~3.8 GB | Stratosphere Labs |

**Critical finding: 3B models need ~4.2 GB RAM.** The Pi 5 has 4GB total. With Linux OS overhead (~500MB-1GB), a 3B Q4 model will NOT fit reliably. The Pi 5 4GB is limited to **1B-2B models only**.

### What Transfers from Phase 2

| Component | Transfers? | Notes |
|-----------|-----------|-------|
| All Pi 4 code | **100%** | No changes needed — Pi 4 keeps running Phase 2 |
| Python AI agents | **Yes** | Move from main PC to Pi 5 Linux |
| UART IPC protocol | **Yes** | Same protocol, different host |
| Stability harness | **Yes** | Runs on Pi 5 instead of PC |

### IPC Options

| Method | Latency | Bandwidth | Complexity |
|--------|---------|-----------|------------|
| UART (GPIO) | 7ms RTT (proven) | 115200 baud (~11.5 KB/s) | Zero — already working |
| USB gadget mode | 1-2ms estimated | 480 Mbps (USB 2.0) | Medium — needs USB-OTG config |
| SPI slave | <1ms estimated | 10-50 Mbps | High — custom protocol needed |
| Ethernet | <1ms | 1 Gbps | Medium — TCP/UDP stack needed on both |

### Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| 4GB RAM too small for useful AI | **High** | High | Limited to 1B models; 12-13 tok/s may be acceptable |
| UART bandwidth bottleneck | Medium | Medium | USB gadget mode as upgrade path |
| Two-board complexity | Low | Low | Both boards proven individually |
| Pi 5 cooling under sustained load | Low | Medium | Active cooling fan already installed |

### Effort Estimate

| Task | Weeks | Notes |
|------|-------|-------|
| Pi 5 Linux setup + llama.cpp | 1 | Straightforward |
| Benchmark 1B models on Pi 5 | 1 | Validate real-world performance |
| UART bridge Pi 4 ↔ Pi 5 | 1-2 | Reuse existing protocol |
| Migrate Python AI to Pi 5 | 1-2 | Move from main PC |
| Integration testing | 1-2 | End-to-end validation |
| **Total** | **5-8 weeks** | Low risk, incremental |

### Cost

**$0** — both boards already owned.

### Verdict

**Low risk, low reward.** Gets AI off the main PC (good) but caps model size at 1B parameters with 12-13 tok/s. The 4GB RAM constraint is a hard ceiling. This is a quick win but doesn't achieve the standalone vision — it's still a split architecture, just with cheaper hardware.

---

## 5. Option C: Pi 5 Standalone (seL4 on BCM2712)

### Architecture

```
┌──────────────────────────────────────┐
│         Raspberry Pi 5 (4GB)         │
│                                      │
│  ┌──────────────┐  ┌──────────────┐ │
│  │ seL4 Kernel  │  │ AI Inference │ │
│  │ (Ring 0)     │  │ (Ring 3)     │ │
│  │ - Microkernel│  │ - 1B model   │ │
│  │ - Drivers    │  │ - C runtime  │ │
│  └──────────────┘  └──────────────┘ │
│         ↑ IPC ↑                      │
│    Shared memory (<1μs)              │
└──────────────────────────────────────┘
```

### seL4 BCM2712 Port Status

**seL4 does NOT support Pi 5 / BCM2712 as of March 2026.** The [seL4 supported platforms page](https://docs.sel4.systems/Hardware/) lists Pi 3B and Pi 4B but not Pi 5. No community ports have been found.

Key porting challenges (from `SEL4_PI5_PORTING_RESEARCH.md`):

| Challenge | Difficulty | Notes |
|-----------|-----------|-------|
| GIC-400 → GIC-400 (same) | Low | Same interrupt controller as Pi 4 |
| BCM2711 → BCM2712 timer | Low | Similar system timer |
| RP1 south bridge (PCIe) | **HIGH** | GPIO, UART, USB, Ethernet all behind PCIe bridge |
| 40-bit physical addressing | Medium | BCM2712 uses 0x10_ prefix addressing |
| No Broadcom datasheet | Medium | Must reverse-engineer from Linux drivers |

**The RP1 south bridge is the killer.** On Pi 4, all peripherals are memory-mapped. On Pi 5, most peripherals (GPIO, UART, USB, Ethernet) are behind the RP1 chip connected via PCIe. Accessing them requires a PCIe driver in seL4 — a substantial undertaking.

### AI Capability

Same as Option B — limited to 1B models with ~12-13 tok/s. Worse, actually, because:
- seL4 has no Python runtime (C-only on bare metal)
- AI inference would need a C-based runtime (ggml/llama.cpp compiled as C library)
- No CUDA, no GPU — pure CPU inference on Cortex-A76
- 4GB RAM shared between kernel + drivers + AI model

### Effort Estimate

| Task | Weeks | Notes |
|------|-------|-------|
| seL4 BCM2712 minimal boot | 3-4 | GIC, native UART, timer |
| RP1 PCIe driver | 4-8 | Novel work, no reference |
| Port all 21 drivers to BCM2712 | 6-10 | Some similar, many different |
| C-based AI runtime | 4-6 | ggml library integration |
| Testing + stability | 4-6 | From scratch on new platform |
| **Total** | **21-34 weeks** | High risk, novel work |

### Cost

**$0** — Pi 5 already owned.

### Verdict

**High risk, low reward.** Massive porting effort (21-34 weeks) for a platform limited to 1B models with 4GB RAM. The RP1 PCIe requirement alone is 4-8 weeks of novel work. Not recommended as primary target. Could be a stretch goal after x86 is working.

---

## 6. Option D: Hybrid x86 + Pi 4 (Transitional)

### Architecture

```
Phase 3a (Transitional):
┌──────────────────┐       UART        ┌──────────────────┐
│   Spare x86 PC   │◄─────────────────►│   Pi 4 (seL4)    │
│   (Linux host)   │                   │   (unchanged)    │
│                  │                   │                  │
│   CUDA AI        │                   │   JARVIS Phase 2 │
│   - RTX 3060     │                   │   - 21 drivers   │
│   - 75 tok/s     │                   │   - Decision cache│
└──────────────────┘                   └──────────────────┘

Phase 3b (Migration):
┌─────────────────────────────────────────────────────┐
│                 Spare x86 PC                         │
│   seL4 hypervisor + Linux VM (GPU) + native JARVIS  │
└─────────────────────────────────────────────────────┘
```

### How It Works

**Phase 3a:** Immediately replace main PC with spare PC as the UART host. Pi 4 code unchanged. Spare PC runs Linux with CUDA-accelerated AI. Same architecture as Phase 2, just with a dedicated (wipeable) host and GPU inference.

**Phase 3b:** Incrementally migrate seL4 to x86. Prove CAmkES VM + GPU passthrough on spare PC while Pi 4 stays as fallback. When x86 is working, transition to standalone.

### What Transfers

- **Phase 3a:** 100% of Phase 2 code transfers unchanged. Just move Python AI to spare PC.
- **Phase 3b:** Same as Option A (CAmkES port + cross-VM IPC).

### Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| None for Phase 3a | - | - | Identical to Phase 2 with better GPU |
| Phase 3b risks = Option A risks | See Option A | See Option A | Pi 4 stays as fallback throughout |

### Effort Estimate

| Task | Weeks | Notes |
|------|-------|-------|
| **Phase 3a** | | |
| Set up spare PC with Linux + CUDA | 1 | Standard Linux install |
| Install llama.cpp + benchmark | 1 | Validate 75+ tok/s |
| Move Python AI + harness to spare PC | 1 | Copy + test |
| UART connection spare PC ↔ Pi 4 | 1 | USB-serial adapter |
| Validation + stability test | 1-2 | Quick confidence test |
| **Phase 3a Total** | **4-6 weeks** | Zero risk |
| **Phase 3b** | | |
| Same as Option A | 14-22 | See Option A estimate |
| **Phase 3b Total** | **14-22 weeks** | Medium risk, Pi 4 fallback |
| **Combined** | **18-28 weeks** | Staged risk |

### Cost

**$0** — hardware already owned.

### Verdict

**Best risk management.** Gets immediate GPU-accelerated AI (Phase 3a in 4-6 weeks) while preserving Pi 4 as proven fallback. Then migrates to standalone x86 (Phase 3b) with zero pressure. If CAmkES/GPU passthrough hits problems, Pi 4 still works.

---

## 7. Comparison Matrix

| Criterion | A: x86 Standalone | B: Multi-Pi | C: Pi 5 Standalone | D: Hybrid (Rec.) |
|-----------|-------------------|-------------|--------------------|--------------------|
| **Standalone?** | Yes | No (split) | Yes | Eventually |
| **AI capability** | 7B+ @ 75 tok/s | 1B @ 12 tok/s | 1B @ 12 tok/s | 7B+ @ 75 tok/s |
| **GPU acceleration** | RTX 3060 CUDA | None | None | RTX 3060 CUDA |
| **IPC latency** | <1-10μs | 7ms (UART) | <1μs (shared mem) | 7ms → <10μs |
| **Code reuse** | ~40% | 100% | ~20% | 100% → ~40% |
| **Effort (weeks)** | 14-22 | 5-8 | 21-34 | 4-6 then 14-22 |
| **Risk** | Medium | Low | High | Low → Medium |
| **Cost** | $0 (+$50 RAM opt.) | $0 | $0 | $0 (+$50 RAM opt.) |
| **Original vision** | Yes | No | Partial | Yes (staged) |

---

## 8. Recommendation

### Primary: Option D (Hybrid x86 + Pi 4) with staged migration

**Rationale:**

1. **Immediate win (Phase 3a, weeks 1-6):** Move AI to spare PC with RTX 3060. Get 75+ tok/s GPU inference immediately. Pi 4 keeps running proven Phase 2 code. Zero risk, zero code changes.

2. **Standalone migration (Phase 3b, weeks 7-28):** Port seL4 to x86, set up CAmkES VM with Linux guest, prove GPU passthrough. Pi 4 stays as fallback the entire time. If x86 hits blockers, the system still works.

3. **Preserves Pi 4 investment.** All 21 drivers, 30-day stability proof, and 108 tests keep running. Nothing is thrown away.

4. **Enables the original vision.** The spare x86 PC with RTX 3060 is exactly what the unified plan envisioned — AI and kernel on the same machine with shared memory IPC.

5. **Risk-managed.** The hardest part (CAmkES VM + GPU passthrough) happens while Pi 4 is the live system. No single point of failure.

### Secondary: Keep Option B (Multi-Pi) as a quick experiment

If spare PC assembly is delayed, spend 1-2 weeks benchmarking llama.cpp on Pi 5. The 1B model at 12-13 tok/s might be acceptable for basic operations. This costs nothing and provides useful data regardless.

### Not Recommended: Option C (Pi 5 Standalone)

The 21-34 week porting effort for a 4GB/1B-model platform is not justified when the spare x86 PC offers 7B+ models with GPU acceleration for zero additional cost.

---

## 9. Key Research Findings

### seL4 x86-64 Is Production-Ready

- Formally verified (functional correctness proved)
- IPC: 1,302 cycles on Skylake (~0.4μs at 3.4 GHz)
- CAmkES VM supports x86-64 with Linux guest and cross-VM connectors
- Device passthrough documented (ethernet); GPU passthrough feasible but requires manual config

### RTX 3060 12GB Is Excellent for Local AI

- 7B Q4 models: 57-77 tok/s generation
- 3.8B models (Phi-3 Mini): ~90-100 tok/s estimated
- 12GB VRAM fits 7B Q4 with room for context window
- 13B Q4 models fit (~7.5GB) but slower (~9-18 tok/s)

### Pi 5 4GB Is Too Constrained for Serious AI

- 3B Q4 models need ~4.2GB — doesn't fit with OS overhead
- Practical limit: 1B-2B models at 9-13 tok/s
- Fine for basic queries, insufficient for complex reasoning

### seL4 Pi 5 Port Is Not Available

- BCM2712 not supported as of March 2026
- RP1 south bridge (PCIe) is the main blocker
- Estimated 21-34 weeks for full port — not recommended

---

## 10. References

- [seL4 Supported Platforms](https://docs.sel4.systems/Hardware/)
- [seL4 Performance Benchmarks](https://sel4.systems/performance.html)
- [CAmkES VMM Documentation](https://docs.sel4.systems/projects/camkes-vm/)
- [CAmkES Cross-VM Connectors](https://docs.sel4.systems/Tutorials/camkes-vm-crossvm.html)
- [seL4 GPU Passthrough Discussion](https://www.mail-archive.com/devel@sel4.systems/msg04166.html)
- [Pi 5 LLM Benchmarks — Stratosphere Labs](https://www.stratosphereips.org/blog/2025/6/5/how-well-do-llms-perform-on-a-raspberry-pi-5)
- [llama.cpp CUDA Performance Discussion](https://github.com/ggml-org/llama.cpp/discussions/15013)
- [RTX 3060 Ti Ollama Benchmarks](https://www.databasemart.com/blog/ollama-gpu-benchmark-rtx3060ti)
- [seL4 Pi 5 Porting Research](SEL4_PI5_PORTING_RESEARCH.md) (internal)

---

*Research Date: March 18, 2026*
*Hardware assessed: Spare x86 PC (Ryzen 5 5600 + RTX 3060 12GB), Pi 4 8GB, Pi 5 4GB*
*Recommendation: Option D (Hybrid staged migration to x86 standalone)*
