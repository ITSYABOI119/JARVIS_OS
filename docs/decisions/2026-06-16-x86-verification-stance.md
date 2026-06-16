# ADR: JARVIS x86-64 runs a performance-first, non-verified seL4 configuration

**Date:** 2026-06-16
**Status:** Accepted
**Deciders:** JARVIS Development

## Context

- JARVIS's architecture (Model B) leans on **seL4's formal verification** as a core part of the safety story: a tiny, capability-secure, formally verified microkernel at Ring 0 isolating the AI decision engine at Ring 3.
- **M0 finding (2026-06-16).** seL4's *verified* X64 configuration is **"C-level functional correctness, no fast path"** — uniquely X64; the AArch32/AArch64 verified configs say *"including fast path"* (<https://docs.sel4.systems/projects/sel4/verified-configurations.html>). JARVIS's deployed x86-64 build runs **`KernelFastpath=ON`**, so it is **already outside the verified X64 config — before M0, independent of XSAVE/AVX/SMP** (`phase4/weeks/week02/WEEK_02_STATUS.md` §3, `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §3).
- M0 added a **second, independent** deviation: `KernelFPU=XSAVE` (feature-set 7 / AVX) so the kernel context-switches YMM, enabling AVX2 in Ring 3 (the engine perf path). M2 may add a **third**: SMP (the multikernel BKL kernel is also unverified; SMP verification is only *"in progress"* per the seL4 FAQ <https://sel4.systems/About/FAQ.html>).
- Phase 4 goal #1 ("Inference performance") **needs** the fast path (IPC latency) and AVX (CPU inference throughput). A verified-config kernel would forfeit both.

## Decision

- **JARVIS's x86-64 deployment INTENTIONALLY runs a high-performance, NON-VERIFIED seL4 configuration:** `KernelFastpath=ON` + `KernelFPU=XSAVE`/AVX (M0), with SMP a candidate at M2. We prioritize usable performance over holding seL4's C-level functional-correctness proof *on the deployed config*. This is a deliberate, recorded stance — not an oversight.
- **A verified-config variant remains RECOVERABLE** by toggling, in the kernel build: `KernelFastpath=OFF` **+** `KernelFPU=FXSAVE` (no AVX) **+** `KernelMaxNumNodes=1` (uniprocessor) — i.e. the verified X64/PC99 config. It is **NOT the default** and is **not maintained as a build target** unless a future need arises (external audit, a verified-variant comparison, a high-assurance deployment).
- **Project docs must not claim the RUNNING x86-64 system is "formally verified."** Accurate phrasing: *JARVIS runs seL4 — a microkernel formally verified in its canonical configurations (ARM/RISC-V, and X64 without the fast path) — in a performance configuration outside that verified set.*

## Consequences

- **Unverified ≠ unsafe.** JARVIS runs the same seL4 capability/isolation architecture and the same battle-tested kernel code; what is *not* in force is the machine-checked proof for **this exact configuration** — not the security model or the isolation guarantees the design relies on day to day.
- **The M2 SMP decision is a MATURITY/risk call, not a verification forfeit.** Because this build already holds no proof (fast path), enabling SMP cannot "lose" one; the real SMP cost is that the multikernel BKL kernel is less battle-tested (SMP verification "in progress"). Argue M2 on maturity, not on verification.
- **The "formally verified" claim is reconciled project-wide** (this pass): `README.md` and `PROJECT_OVERVIEW.md` qualified at their running-system claims; `CLAUDE.md` already qualified at M0 (architecture diagram + Technology Stack) and gains this ADR in its decision-records pointer. General, true statements ("seL4 *is* a formally verified microkernel") and historical Phase 1/2 + ARM64 docs (AArch64's verified config *does* include the fast path) are intentionally left unchanged. Two further running-x86-64 implications are **flagged but not edited** in this bounded pass: `phase3/docs/QEMU_INFERENCE_MILESTONE.md` (a point-in-time milestone record) and `JARVIS_UNIFIED_PLAN.md:503` (a general base-tech bullet in the aspirational master plan) — qualify later if desired.

## Alternatives rejected

- **Ship the verified config (fast path off, FXSAVE, uniprocessor).** Kills the Phase 4 perf goal — no AVX (CPU inference stays ~0.2 tok/s scalar) and slower IPC — for a research OS whose whole point is to be *usable*. Rejected; kept recoverable instead.
- **Keep claiming "formally verified" unqualified.** Inaccurate for the deployed x86-64 config and therefore an honesty violation. Rejected.

## References

- M0 findings: `phase4/weeks/week02/WEEK_02_STATUS.md` (§3), `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` (§1 Q1, §2 M0/M2, §3)
- seL4 verified configurations: <https://docs.sel4.systems/projects/sel4/verified-configurations.html> ("X64 … C-level functional correctness, no fast path")
- seL4 FAQ (per-arch proofs; multicore verification "in progress"): <https://sel4.systems/About/FAQ.html>
- Related ADRs: `docs/decisions/2026-06-16-defer-gpu-inference.md`, `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`, `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`
