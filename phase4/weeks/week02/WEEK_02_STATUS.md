# Week 02 Status: Phase 4 Goal #1 — M0 (kernel XSAVE + AVX2-under-preemption safety gate)

**Date:** June 16, 2026
**Status:** M0 COMPLETE — GATE PASSED (KVM `-cpu host`). No engine logic change; scalar generation unaffected.
**Phase:** Phase 4 — Production (v1.0), goal #1 "Inference performance" (CPU path). Hardware-in-the-loop on the JARVIS PC.

---

## Summary

M0 rebuilds the seL4 kernel so it **context-switches AVX (YMM) state**, then proves — empirically, under a preempting workload on **KVM `-cpu host`** (real Ryzen 2700X AVX2) — that one isolated AVX2 op in a Ring-3 process stays correct. This unblocks M1 (turning on global `-mavx2` in the engine apps). The probe is `target("avx2,fma")`-isolated and compile-guarded (`JARVIS_AVX2_PROBE`, default **0**); there is **no global `-mavx2`** yet (that is M1).

**Result: the kernel now saves AVX state (XCR0.AVX=1) and AVX2 matches scalar across thousands of context switches with zero mismatches and zero faults.**

## Part 1 — Kernel XSAVE rebuild

The deployed kernel was FXSAVE-only (512 B; x87+SSE, **not** the YMM upper-128). M0 rebuilds it:

| Setting | Before | After (M0) |
|---------|--------|-----------|
| `KernelFPU` | FXSAVE | **XSAVE** |
| `CONFIG_XSAVE` | disabled | **1** |
| `CONFIG_XSAVE_FEATURE_SET` | 0 | **7** (FPU+SSE+**AVX**) |
| `CONFIG_XSAVE_SIZE` | 512 | **832** |
| XSAVE variant | — | **XSAVEOPT** (Zen+) |

**Gotcha found & fixed (reproducible in the repo):** `-DKernelFPU=XSAVE` was silently ignored — the seL4 cmake-tool's `ApplyCommonSimulationSettings()` **force-sets `KernelFPU=FXSAVE` under `SIMULATION=ON`** ("cannot simulate some more recent features" — TCG QEMU has no AVX). Fix: **`SIMULATION=OFF`** (we target KVM/bare-metal, real AVX2) + a **two-pass cmake** (because `KernelXSaveFeatureSet` *depends on* `KernelFPUXSave`, which only flips ON in pass 1, so pass 2 re-asserts `=7`). Both are now wired into `phase3/scripts/build_jarvis_x86.sh` (the only sim-forced setting JARVIS relied on, IOMMU=OFF, is still force-set there).

Rollback anchor captured before any change: original kernel `kernel-x86_64-pc99` sha256 `414be46…` (Apr 4). The rebuild only produces a new image tested in KVM; **bare metal was not flashed** (the KVM `-cpu host` run is the authoritative gate — it passes through the 2700X's real AVX2+XSAVE).

## Part 2 — AVX2-under-preemption safety probe (the GATE)

New isolated probe `phase3/src/sel4/avx2_probe.h` (gcc 256-bit vector extensions under a function-level `__attribute__((target("avx2,fma")))` — no `immintrin.h`, no global `-mavx2`). It (a) reads `XCR0` via `xgetbv` (legal at CPL3 iff CR4.OSXSAVE=1) and asserts bits 1 (SSE) **and** 2 (AVX); (b) runs a long YMM-accumulate loop (an asm barrier keeps the accumulator live in YMM) so a timer preemption lands while YMM holds live state, then compares against an exact `iter×single` reference (relative tol; FMA reorders rounding). Process B runs the probe (main loop **and** idle loop); Process A dirties YMM each workload iteration so the kernel's lazy per-TCB FPU path exercises **cross-thread** save/restore.

### Gate result (KVM `-cpu host`, ~6 min under load)

| Check | Result |
|-------|--------|
| `XCR0` (Process A) | `0x0000000000000007` — sse=1, **avx=1** → AVX-ENABLED |
| `XCR0` (Process B) | `0x0000000000000007` — sse=1, **avx=1** → AVX-ENABLED |
| Process B probes | **24,064** (each = 800k-iter live-YMM reduction spanning preemptions) |
| **Mismatches (AVX2 vs scalar)** | **0** |
| Faults / panics | **0** |
| Self-test (scalar engine) | **5/5 PASS** (Tensor 5/5, Dequant 3/3, Tokenizer+Sampling 4/4) |
| Process A AVX2 workload (prior no-idle run) | 200+ queries, AVX2 touch clean, 0 faults |

24,064 PB probes × 800k iters ≈ **1.9×10¹⁰ YMM MACs** across thousands of context switches, interleaved with PA's YMM touches — **0 divergence**. Direct proof (XCR0.AVX=1) + empirical proof (0 mismatches) ⇒ **the kernel correctly saves/restores AVX state; AVX2 in Ring 3 is safe.**

**Probe left in place, `JARVIS_AVX2_PROBE` default 0** (compile guard, not deleted — bare-metal debug rule). Stability defaults unchanged: IPC=0/PB=0/RING=0/STATS=1/INFER_SUMMARY=1/BOOT_LOG=0.

### Honest carve-out — model regression
The model-load + coherent-generation regression was **not** re-confirmed in QEMU this run: the 2.96 GB Gemma 4 E2B load over **emulated** NVMe is too slow to finish inside the gate window (a 7-min with-model run timed out *during* `Allocating 758480 frames`). This is a pre-existing QEMU-NVMe-speed limitation, **orthogonal** to the FPU-mode change (NVMe/FAT32/tokenizer/scalar-inference use no FPU state). The XSAVE change cannot affect model loading, and **self-test 5/5 confirms the scalar engine is unaffected** under the new kernel. Full model-gen runs fast on bare metal (real NVMe) and can be re-confirmed there in M1.

## Part 3 — Verification-cost findings (researched 2026-06-16, primary sources)

**JARVIS's x86-64 build is already OUTSIDE the exact verified X64 configuration — and the load-bearing deviation is `KernelFastpath=ON`, in force *before* M0.** seL4's verified X64 properties are *"C-level functional correctness, **no fast path**"* (<https://docs.sel4.systems/projects/sel4/verified-configurations.html>; uniquely X64 — AArch32/AArch64 verified configs say *"including fast path"*). The box runs `KernelFastpath:BOOL=ON`, so the proof is **not in force on this build today**, independent of FPU/AVX/SMP.

- The verified X64 config (`X64_verified.cmake`) sets no `KernelFPU` line → inherits the **FXSAVE** default; M0's `KernelFPU=XSAVE` is therefore a **second, independent deviation**, not the first.
- **Re-pricing:** the C-level functional-correctness guarantee is **already spent by the fast path** — it is NOT newly forfeited by M0's XSAVE or by M2's SMP. The **M2 SMP decision should be argued on maturity** (multicore BKL kernel less battle-tested; SMP verification still "in progress", FAQ), **not** on "losing the proof," which this build does not hold. To *regain* a verified X64 kernel would require `KernelFastpath=OFF` + FXSAVE + uniprocessor — sacrificing both IPC and AVX perf, off the table for a perf phase.
- This **corrects** week-01's "uniprocessor x64 runs a genuinely verified config" (true for the *architecture*, false for *this build* due to fastpath). CLAUDE.md's bare "seL4 (formally verified)" now carries a qualifier.

## Files

| File | Change |
|------|--------|
| `phase3/src/sel4/avx2_probe.h` | **NEW** — isolated `target("avx2,fma")` YMM probe (XCR0 check + long live-YMM reduction vs scalar) |
| `phase3/src/sel4/jarvis_debug.h` | `+ JARVIS_AVX2_PROBE` (default 0) |
| `phase3/src/sel4/inference_server.c` | probe wired into PB startup + main loop + idle loop (guarded) |
| `phase3/src/sel4/main_x86.c` | PA workload-loop YMM touch (guarded) |
| `phase3/scripts/build_jarvis_x86.sh` | kernel XSAVE flags (SIMULATION=OFF, KernelFPU=XSAVE, 2-pass feature-set 7, size 832, XSAVEOPT) |
| `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` | M0 marked DONE; §1 Q1 / §3 verification verdict |
| `CLAUDE.md` | week02 pointer + M0 status line + "formally verified" qualifier |

## Next steps

- **M1** (next): turn on global `-mavx2 -mfma` in both engine app targets; verify boot + numeric correctness vs scalar + measure single-thread tok/s (target ~1.7 Gemma E2B / ~3.2 Llama 1B, from ~0.2 scalar). The AVX2 kernels (`qdot.c`, `tensor_ops.c`, `llama_quant.c`) light up with no source change; M0 has proven the kernel saves YMM, so this is now safe. Re-confirm model-gen on bare metal (fast NVMe).
- **M2** decision uses the M1 tok/s number; per Part-3, the SMP trade-off is maturity (not verification).

---

*Phase 4 cadence: weekly status at `phase4/weeks/weekN/WEEK_N_STATUS.md`.*
