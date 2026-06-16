# Week 01 Status: Phase 4 Kickoff — Goal #1 Inference-Perf Research + Plan

**Date:** June 16, 2026
**Status:** RESEARCH + PLAN COMPLETE (no engine code)
**Phase:** Phase 4 — Production (v1.0). Phase 3 CLOSED (`v0.2.1-beta` tagged @ `06de75c`).

---

## Summary

Phase 4 begins. GPU inference was deferred (ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`) and roadmap **goal #1 was reframed from "GPU inference" to "Inference performance"** — v1.0 "fast" = **AVX2/FMA + a seL4-native threadpool in the seL4 build**, up from the current scalar single-thread (~0.2 tok/s).

This week answered the two gating questions authoritatively by reading the **live `~/sel4-x86` build config** on the JARVIS PC (`ssh jarvis`, read-only) and the seL4 manual/source, then wrote the M0–M4 implementation plan: `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md`.

## The two verdicts (VERIFIED from the box config — config read wins over web)

| # | Question | Verdict | Evidence |
|---|----------|---------|----------|
| **Q1** | AVX2 safe in a Ring-3 process? | **NO — not under the current kernel** | `CONFIG_XSAVE` disabled, `CONFIG_FXSAVE 1`, `CONFIG_XSAVE_SIZE 512`, `KernelFPU=FXSAVE`, `KernelX86MicroArch=nehalem`. FXSAVE saves x87+SSE only, **not** AVX `YMM` upper 128 bits. |
| **Q2** | Build SMP (multi-core)? | **NO — uniprocessor** | `CONFIG_MAX_NUM_NODES 1`, `ENABLE_SMP_SUPPORT` disabled, `KernelMaxNumNodes=1`, no `-smp` in the QEMU scripts. |

**Consequence:** *both* halves of goal #1 need a **kernel config change**, not just app CFLAGS:
- AVX2 requires rebuilding the kernel with `KernelFPU=XSAVE`, `KernelXSaveFeatureSet=7`, `KernelXSaveSize=832` (so YMM is saved) — *then* `-mavx2 -mfma` on both app targets lights up the already-written `#ifdef __AVX2__` kernels (qdot.c, tensor_ops.c, llama_quant.c) with zero source change.
- The threadpool gives **zero** speedup on a uniprocessor; it is **gated on enabling SMP** (`-DSMP=ON -DNUM_NODES=N`, cap 8 on the 2700X's 8 cores), which forfeits the formal-verification guarantee (multicore kernel not yet verified).

seL4 on the box: **14.0.0** (`ebbda2af5`), x86_64/pc99, MCS off, IOMMU off.

## Engine seams identified

- **AVX2** = build-flag flip in `apps/{sel4test-driver,jarvis-inference}/CMakeLists.txt` (+ `build_jarvis_x86.sh`); the SIMD code already exists behind `#ifdef __AVX2__`. Today: **no `-mavx2/-mfma/-march` anywhere** (verified).
- **Threadpool** = new seL4-native backend behind the existing `jarvis_parallel_for`/`jarvis_threads` ABI; the parallel seam is `llama_quant.c::qmatmul_vec`. `threadpool.c` is pthread-only (serial stub on seL4 today).

## New Files

| File | Description |
|------|-------------|
| `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` | Findings (Q1/Q2 + engine seams) + M0–M4 plan + risks + exit criteria mapped to ROADMAP goal #1 |
| `phase4/weeks/week01/WEEK_01_STATUS.md` | This file (Phase 4 weekly-status cadence resumes, like Phase 2) |

## Modified Files

| File | Changes |
|------|---------|
| `CLAUDE.md` | Quick-Reference pointers (plan + Phase 4 weeks); Development-Documentation-Pattern note (Phase 4 resumes weekly status); File-Structure tree adds `phase4/` |

## Plan at a glance (see the plan doc for full detail)

- **M0** — kernel XSAVE/AVX enablement + AVX2 safety spike *(JARVIS-PC; kernel rebuild — not a fast confirm, since AVX is verified-unsaved today)*. Test: `test_avx2_correctness.c` (AVX2 qdot vs scalar, all 7 quant types) → **new CI step**.
- **M1** — enable `-mavx2 -mfma` in the seL4 app build, single-thread; target ~1.7 tok/s Gemma E2B / ~3.2 Llama 1B (from ~0.2 scalar). *(JARVIS-PC; depends on M0.)*
- **M2** — SMP decision point (uniprocessor verified): **Branch A** enable SMP spike, or **Branch B** ship AVX2-single-thread for v1.0 and defer threading. ADR either way.
- **M3** — seL4-native threadpool (worker TCBs + affinity + atomic work-stealing), parallelize `qmatmul_vec`; target ~8–9 tok/s Gemma E2B @ up-to-16T. Test: `test_parallel_for.c` → **new CI step**. *GATED on M2 branch A.*
- **M4** — end-to-end bare-metal benchmark recorded; ROADMAP goal-#1 CPU "Done when" satisfied.

## Testing caveat carried into M0/M1

AVX2 state corruption is **preemption-only**, and the committed QEMU sim is **Nehalem with xsave disabled** (AVX2 would `#UD`). AVX2 correctness must be validated on **KVM `-cpu host` or bare metal** under a preempting workload — a single-threaded QEMU smoke test can pass on a wrong kernel.

## Next steps

1. **M0** on the JARVIS PC: rebuild the kernel with `KernelFPU=XSAVE` / `XSaveFeatureSet=7` / `XSaveSize=832`; confirm clean boot (no XSAVE-size error, XCR0 bit 2 set).
2. Add `phase3/src/ai/test_avx2_correctness.c` + its CI step (AVX2 qdot vs scalar, bit-exact) — main-PC/CI, doable now.
3. Then M1: `-mavx2 -mfma` on both app targets; bench single-thread tok/s on bare metal.
4. M2 decision uses the M1 number.

---

*Phase 4 cadence: weekly status files at `phase4/weeks/weekN/WEEK_N_STATUS.md`, committed with the week number (resumes the Phase 2 pattern; Phase 3 used topic docs).*
