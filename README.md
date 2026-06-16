# JARVIS AI-OS

**An AI-controlled operating system on the seL4 microkernel — the LLM makes the decisions, a formally verified kernel keeps it safe.**

> **Status:** Phase 3 (Beta) complete — `v0.2.1-beta` · solo, bootstrapped research project · last updated 2026-06-16

JARVIS runs a large language model as the primary control layer of an operating system. It boots standalone on bare-metal **seL4 x86-64** (no Linux, no VM, no hypervisor), loads a quantized LLM from NVMe at runtime, and serves it from a **process-isolated** inference engine over lock-free shared-memory IPC.

This is a one-person project, built and validated on existing hardware. There is no team, no budget, and no funding round — just the kernel, the engine, and the boxes on the desk.

---

## What it is

An LLM is far too slow to handle hardware interrupts: AI inference takes **50–500 ms**, while interrupts must be serviced in **<1 ms** — three orders of magnitude apart. So JARVIS uses **Model B**: the seL4 microkernel owns the time-critical path at **Ring 0**, and the AI decision engine runs at **Ring 3** on dedicated cores, making high-level decisions and talking to the kernel over lock-free IPC. (seL4 is a formally verified microkernel; JARVIS's x86-64 build deliberately runs a *performance* configuration — fast path + AVX — **outside** seL4's verified X64 set, so the running system is not itself proof-carrying. See [docs/decisions/2026-06-16-x86-verification-stance.md](docs/decisions/2026-06-16-x86-verification-stance.md).)

Today that is real, not a plan. On a Ryzen 7 2700X, JARVIS boots seL4 to a self-test, brings up its own PCI/NVMe/FAT32/VGA drivers, loads **Gemma 4 E2B** (Q4_K_M, ~2.9 GiB) from an NVMe partition at runtime, spawns a second isolated seL4 process for inference, and generates coherent text — all in C, with no operating system underneath it.

---

## Project status

| Phase | Stage | Platform | Tag | Result |
|-------|-------|----------|-----|--------|
| **0** | ✅ Validation | simulation + HW prototypes | — | 80% validation success → GO |
| **1** | ✅ Proof of concept | seL4 x86 (QEMU) | — | 26/26 weeks; cache + IPC + AI proven |
| **2** | ✅ Alpha | Raspberry Pi 4 bare metal | `v0.2.0-alpha` | 21 drivers; **30.6-day** stability run (0 crashes) |
| **3** | ✅ **Beta** | **x86-64 bare metal** | **`v0.2.1-beta`** | process-isolated LLM inference on real hardware |
| **4** | ⏳ Next | x86-64 + GPU | `v1.0.0` (planned) | Production: speed, UI, installer, MIT release |

---

## Architecture (Model B)

```
                    JARVIS PC — bare-metal seL4 x86-64 (no Linux, no VM)
┌──────────────────────────────────────────────────────────────────────────────┐
│  seL4 microkernel — Ring 0 (formally verified)                                 │
│  interrupts <1 ms · memory management · IPC primitives · capabilities          │
└──────────────────────────────────────────────────────────────────────────────┘
        │ spawns Process B from a CPIO archive (process isolation)
        ▼
┌────────────────────────────────┐   lock-free shmem    ┌────────────────────────────────┐
│ Process A — rootserver          │   2 rings, 15×256B,  │ Process B — inference engine    │
│ • decision cache (85.7% hit)    │   CRC-32 per message │ • custom quantized GGUF engine  │
│ • SHIELD safety check           │ ───── request ─────► │ • Gemma 4 E2B (Q4_K_M)          │
│ • PCI / NVMe / FAT32 / VGA      │ ◄──── response ───── │ • dequant-on-the-fly, ~50 MB    │
│ • NVMe runtime model loading    │                      │   heap, CPU-only (AVX2 target)  │
│ • continuous IPC workload loop  │                      │                                 │
└────────────────────────────────┘                      └────────────────────────────────┘
        both processes run at seL4_MaxPrio; IPC is single-producer/single-consumer
```

**Process A** (the rootserver) owns the decision cache, the inlined SHIELD safety check, all device drivers, NVMe model loading, and the workload loop. It spawns **Process B** (the inference engine) from an embedded CPIO archive into a separate address space. They communicate only through two shared-memory ring buffers (15 × 256 B slots, CRC-32 checksummed) — Process B never touches hardware directly.

---

## How it works now

- **Custom inference engine.** A from-scratch C11 quantized GGUF engine — **zero-copy** (weights stay quantized in place, dequantized on the fly: Q4_0/Q4_K/Q5_K/Q6_K/Q8_0/BF16/F16), ~50 MB working heap. AVX2 fused dequant-dot (`qdot`) kernels on the dev engine. **11 models across 6 families** load and generate (Llama, Gemma 4 with PLE + sliding-window attention + KV-sharing, Phi-3 fused-QKV, Mistral Q8_0, Qwen3 QK-norm, Qwen3.5 Gated-DeltaNet SSM hybrid). **Gemma 4 E2B is the deployed single model.**
- **Runtime model loading.** No model is baked into the boot image. The NVMe driver (PCI → BAR0 → admin/IO queues → IDENTIFY) reads the GGUF from a FAT32 partition (`JARVIS_DATA`, LBA 32768) into mapped frames, then hands those frames to Process B.
- **SHIELD safety.** Risk screening on the decision/inference path (keyword screening in Process A today; model-assisted scoring is future work).
- **Persistent telemetry.** Raw-sector NVMe log captures serial output and per-query stats across boots (constant `boot_id`).

---

## What's proven in Phase 3

- **Bare-metal boot on Ryzen 7 2700X** — seL4 self-test 5/5 PASS, boot to ready in ~2 s, no Linux/VM.
- **Process isolation + IPC** — Process A spawns Process B from CPIO; shared-memory rings with CRC-32; the heartbeat/shield IPC race fixed (spurious error rate **7% → 0**).
- **End-to-end inference on real hardware** — NVMe model load → coherent Gemma 4 E2B generation via process-isolated IPC.
- **Bare-metal burn-in** — **err = 0 over 400 queries** (q = 100/200/300/400), `boot_id` constant. *(A burn-in, not a 30-day soak — see limitations.)*
- **Model bench-off** — 11 models scored on speed (llama-bench), perplexity (WikiText-2), and quality (10 prompts, blind **7-judge** consensus) → **Gemma 4 E2B winner, 8.40/10**.
- **Security** — two adversarial audits (**51 findings, 0 HIGH/MEDIUM open**) plus a fuzz harness at **300K iterations**, ASAN-clean.

### Selected metrics

| Metric | Result |
|--------|--------|
| Boot time | ~2 s (target <60 s) |
| Shared-memory IPC | ~23.7M msg/sec throughput (pre-work test); under the <1 µs RTT target (seL4 native IPC ~0.4 µs) |
| Decision cache hit rate | 85.7% |
| Models supported (engine) | 11/11 load, 6 families |
| SHIELD | 100% harmful blocked, 0% false positive |
| Engine speed *(native dev bench, AVX2 + threads)* | Llama 1B **3.22 tok/s (1T) → 19.79 tok/s (16T)**; Gemma 4 E2B 1.70 / 8.63 |
| Engine speed *(deployed seL4 build, scalar single-thread)* | ~0.2 tok/s (≈3k queries/day) — **perf is a Phase 4 focus** |

> **Speed, stated honestly:** the headline 3.22 / 19.79 tok/s figures are the **native development engine** (AVX2 + pthread). The **bare-metal seL4 build is currently scalar and single-threaded** (~0.2 tok/s). AVX2/threading-on-seL4 and a GPU path are Phase 4 work — these numbers are not the deployed kernel speed.

---

## Current limitations (honest carve-outs)

- **30-day x86 stability soak: DEFERRED — not run, not passed.** Risk-accepted and owner-scheduled (ADR `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`). The beta's stability evidence is the ~400-query burn-in + 300K-iteration fuzz + two security audits, with Phase 2's 30.6-day Pi 4 run as supporting context (different IPC stack, not equivalent).
- **CPU-only, scalar seL4 build.** Inference is CPU-only (the JARVIS PC's GPU is display-only); the deployed kernel build is single-threaded. Performance (AVX2/threading on seL4, then GPU) is the headline Phase 4 goal.
- **Single model by design.** Dynamic model scaling was **removed** (ADR `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`); JARVIS ships one model (Gemma 4 E2B). KV-cache compression (TurboQuant/RotorQuant) was evaluated and deferred to Phase 4 (ADR `docs/decisions/2026-06-15-defer-turboquant-rotorquant-to-phase4.md`).

---

## Repository structure

```
JARVIS_OS/
├── phase0/          # ✅ Validation experiments (GO decision)
├── phase1/          # ✅ Proof of concept — seL4 + AI in QEMU
├── phase2/          # ✅ Alpha — Pi 4 bare metal (drivers, 30.6-day stability)
├── phase3/          # ✅ Beta — x86-64 bare metal (engine, drivers, seL4 rootserver)
│   ├── src/         #    ai/  drivers/  ipc/  sel4/
│   ├── docs/        #    final report, kickoff, plan, bench-off, security audits, boot guide
│   └── scripts/     #    build_jarvis_x86.sh, qemu_test.sh, parse_nvme_log.py, bench_*.sh
├── phase4/          # ⏳ Roadmap (Production → Memory → Butler → Autonomy)
│   └── docs/ROADMAP.md
├── docs/decisions/  # Architecture Decision Records (ADRs)
├── models/          # bench_results/, quality_results/, wikitext-2-raw/
├── archive/         # Historical research & original plans
├── CLAUDE.md        # Working guide: architecture, status, build/test commands
├── JARVIS_UNIFIED_PLAN.md   # Original aspirational 36-month plan (historical context)
└── README.md        # This file
```

---

## Build & run

The toolchain (WSL/Linux + the TII seL4 build system, CMake/Ninja, QEMU) and the full command set live in **[CLAUDE.md](CLAUDE.md)** (build/test commands per phase). For bare metal:

- **Bare-metal boot:** `phase3/docs/BARE_METAL_BOOT_GUIDE.md` (BIOS checklist, USB image, GRUB).
- **Build / test in QEMU first:** `phase3/scripts/build_jarvis_x86.sh`, then `phase3/scripts/qemu_test.sh <model.gguf>` — always validate in QEMU before flashing.
- **Run notes:** `RUN_JARVIS.md`.

Tests run as a flat compile-and-run suite in CI (GitHub Actions, "JARVIS AI-OS Test Suite"); each `test_*.c` compiles its real dependencies and prints PASS/FAIL.

---

## Roadmap (Phase 4 and beyond)

From `phase4/docs/ROADMAP.md` — sequential, each with explicit "done when" criteria:

- **Phase 4 — Production (`v1.0.0`):** GPU inference (target ≥50 tok/s for Gemma 4 E2B on RTX-2070-class), framebuffer/HDMI status UI, USB keyboard (xHCI) on bare metal, one-script installer, **90-day** stability, and an **MIT open-source release**.
- **Phase 5 — Memory:** episodic store on NVMe, shared context pool in C, retrieval-before-inference, persisted SHIELD learning, cache that grows from use.
- **Phase 6 — Butler:** always-on monitors, event-driven proactive actions, a structured user model, conversation as the primary interface, multi-agent routing >95%.
- **Phase 7 — Autonomy (`v2.0.0`):** associative ("Instinct") memory, 30-day unsupervised operation, staged self-modification with rollback (immutable core), larger GPU models for hard tasks, external security audit.

---

## Tech stack

- **Microkernel:** [seL4](https://sel4.systems/) — formally verified, x86-64 (and ARM64 for Phase 2).
- **Inference:** custom C11 quantized **zero-copy GGUF engine** with AVX2 fused dequant-dot kernels — no external runtime.
- **Models:** GGUF, Q4_K_M (deployed: Gemma 4 E2B); 6 architecture families supported.
- **Build:** TII seL4 build system + CMake/Ninja; gcc 13.
- **Test/emulate:** QEMU (KVM-accelerated) for pre-flash validation.
- **Hardware:** JARVIS PC — Ryzen 7 2700X, 32 GB DDR4, 2 TB NVMe (Lexar NM790), ASUS X470-F (CPU-only inference; GPU compute is Phase 4).

---

## License

No license is applied yet. **MIT** is planned for the public **`v1.0.0`** release in Phase 4. Until then this is a personal research repository.

---

## More detail

- **[CLAUDE.md](CLAUDE.md)** — the living working reference: full architecture, current status, file map, and build/test commands.
- **[phase3/docs/PHASE_3_FINAL_REPORT.md](phase3/docs/PHASE_3_FINAL_REPORT.md)** — the Phase 3 (beta) summary, metrics scorecard, and honest success-criteria table.
- **[docs/decisions/](docs/decisions/)** — ADRs (dynamic-scaling removal; 30-day soak deferral; TurboQuant/RotorQuant deferral).
- **[JARVIS_UNIFIED_PLAN.md](JARVIS_UNIFIED_PLAN.md)** — the original aspirational 36-month corporate-scale plan, kept for historical context (its budget/team/timeline framing does not reflect this solo project).
