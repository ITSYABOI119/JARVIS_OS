# ADR: Defer GPU inference — ship CPU-only v1.0; GPU gated on a hardware change

**Date:** 2026-06-16
**Status:** Accepted
**Deciders:** JARVIS Development

## Context

- Phase 4 roadmap goal #1 (`phase4/docs/ROADMAP.md`) targeted **≥50 tok/s GPU inference** for Gemma 4 E2B on RTX-2070-class hardware (Vulkan or CUDA path).
- Deep research (2026-06-16) established that GPU *compute* is not executable on our hardware/stack without a Linux driver layer:
  - (a) GPU compute requires **Vulkan**, which requires a Linux driver stack — Mesa **RADV** (AMD) or the NVIDIA driver. <https://github.com/ggml-org/llama.cpp/issues/19615>
  - (b) No from-scratch (bare-metal) GPU driver exists for our chips — **tinygrad AM is RDNA3/4 only**, not GCN1 Tahiti (R9 280X) or Turing (RTX 2070). <https://docs.tinygrad.org/developer/am/>
  - (c) A bare-metal-seL4 GPU driver + compute stack is **research-grade / year-class** effort.
  - (d) The **R9 280X is a dead end**: 3 GB VRAM < ~2.9 GiB model + KV cache + framebuffer; `DeviceLost` instability on RADV; 2013-era GCN1.
  - (e) The **RTX 2070 hits the ≥50 tok/s target — but only natively on Linux**, not bare-metal seL4. <https://github.com/ggml-org/llama.cpp/discussions/10879>
- **Owner hardware decision (2026-06-16):** the main-PC RTX 2070 is unusable for this (daily-driver GPU, wrong box, Linux-only), and **no new GPU will be acquired for the JARVIS PC for the foreseeable future** ⇒ there is no hardware to *develop on* or *run* GPU inference on.

## Decision

- **Defer the GPU half of goal #1 entirely.** v1.0 ships **CPU-only**.
- **"Fast" for v1.0 = CPU AVX2 + threading in the seL4 build.** The AVX2 `qdot`/attention kernels already exist but compile out of the seL4 build, and the threadpool is a serial stub, so the deployed seL4 build runs **scalar single-thread (~0.2 tok/s)**. Target: approach the native threaded engine — **~8–9 tok/s Gemma 4 E2B (16T)** and **~20 tok/s Llama 1B**.
- **Revisit GPU inference only on a usable-GPU hardware change.** If pursued: backend = **Vulkan compute** (cross-vendor; CUDA is NVIDIA-only); reference = the ggml-vulkan batch=1 Q4_K mat-vec shader. <https://github.com/ggml-org/llama.cpp/pull/10296>
- **Scope note:** this defers GPU *compute* only. GPU *display* / the graphical-UI goal (roadmap goal #2) is **unaffected** — a linear framebuffer needs no Vulkan/Mesa.

## Alternatives rejected

- **A — Native Vulkan on the RTX 2070.** Meets the perf target, but there is no usable GPU available to the project (wrong box, daily driver, Linux-only) — rejected on the owner hardware decision.
- **B — Bare-metal-seL4 GPU now.** No from-scratch driver exists for GCN1/Turing (tinygrad AM is RDNA3/4 only); a bare-metal GPU compute stack is year-class research — rejected as non-executable in the v1.0 timeframe.

## Consequences

- v1.0 inference is **CPU-bounded**; the headline number becomes the AVX2+threaded seL4 build (~8–9 tok/s Gemma 4 E2B @16T target), not GPU.
- **≥50 tok/s becomes a future, hardware-gated goal**, not a v1.0 deliverable.
- Keeps the **no-Linux / bare-metal-seL4 thesis intact** — pulling in Mesa/NVIDIA for Vulkan would require a Linux driver stack, contradicting the project's core constraint.
- Roadmap goal #1 is reframed to **Inference performance** (CPU path); its GPU "Done when" item is marked deferred (see `phase4/docs/ROADMAP.md`).

## References

- Vulkan needs a Linux driver stack (Mesa RADV / NVIDIA): <https://github.com/ggml-org/llama.cpp/issues/19615>
- tinygrad AM driver is RDNA3/4 only: <https://docs.tinygrad.org/developer/am/>
- RTX 2070 meets the target on native Linux: <https://github.com/ggml-org/llama.cpp/discussions/10879>
- ggml-vulkan batch=1 Q4_K mat-vec shader (future reference): <https://github.com/ggml-org/llama.cpp/pull/10296>
- Reframed roadmap goal #1: `phase4/docs/ROADMAP.md`
- Related deferrals: `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`, `docs/decisions/2026-06-15-defer-turboquant-rotorquant-to-phase4.md`
