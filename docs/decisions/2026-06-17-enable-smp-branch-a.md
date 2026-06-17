# ADR: Enable seL4 SMP (Branch A) for the M3 threadpool

**Date:** 2026-06-17
**Status:** Accepted
**Deciders:** JARVIS Development

## Context

- Phase 4 goal #1 ("Inference performance", CPU path) targets a **seL4-native threadpool** (M3) to scale Gemma 4 E2B decode beyond the single-thread AVX2 number. M1 delivered **~1.53 tok/s** single-thread AVX2 on bare metal (`phase4/weeks/week03/WEEK_03_STATUS.md`).
- **A threadpool gives zero speedup on a uniprocessor kernel** — the deployed build was `CONFIG_MAX_NUM_NODES=1`, `ENABLE_SMP_SUPPORT` disabled, so N worker TCBs would just time-slice one core. SMP is the prerequisite for every threading gain (`phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §1 Q2).
- The M2 plan (goal-#1 plan §2) framed a **decision point**: **Branch A** (enable SMP, pursue the threadpool, target the threaded number) vs **Branch B** (ship AVX2 single-thread for v1.0, defer threading).
- **The verification objection to SMP is already spent.** Per ADR `docs/decisions/2026-06-16-x86-verification-stance.md`, JARVIS's x86-64 build runs `KernelFastpath=ON` — **outside** seL4's verified X64 config ("C-level functional correctness, *no fast path*") — *before* M0/M2, independent of XSAVE/AVX/SMP. So enabling SMP cannot "forfeit a proof we hold"; the real cost is **maturity** (the multikernel BKL kernel is less battle-tested; SMP verification is only "in progress" per the seL4 FAQ).

## Decision

**Pursue Branch A: enable seL4 SMP and build the M3 threadpool.**

- The seL4 build is rebuilt with `-DSMP=ON -DNUM_NODES=2` (the minimal config that exercises cross-core concurrency: BSP + 1 AP). The production node count is chosen at M3 after bandwidth testing — **NUM_NODES=2 is a spike value, not the final N** (cap is 8; the 2700X is 8C/16T).
- The SMP config is **repo-reproducible by construction**: `phase3/scripts/build_jarvis_x86.sh` passes the flags and a **config-verification gate** asserts `CONFIG_ENABLE_SMP_SUPPORT=1` + `CONFIG_MAX_NUM_NODES=2` + XSAVE/feature-set-7 + FXSAVE-off + IOMMU-off on every build, aborting the build if any invariant regresses.
- **This is a maturity/risk call, decided on perf grounds — explicitly NOT a verification forfeit** (the proof was already not in force; see the verification-stance ADR). The build now departs the verified config along a third axis (SMP), in addition to the fast path and XSAVE/AVX.
- Branch B (ship 1T for v1.0) remains a viable fallback if M3 threading does not clear a useful throughput bar; M2 deliberately keeps that option open by proving the 1T path is **unharmed** by SMP (below).

## Spike result (M2 — verified, KVM + bare metal, 2026-06-17)

The SMP foundation was validated before committing to the threadpool work:

- **KVM (`-smp 2 -cpu host`):** both nodes boot (`Starting node #0` + `Starting node #1` → "dropped to user space"), `bootinfo->numNodes==2`, self-test **5/5**, Process B spawns and completes the IPC ready-handshake **under SMP**, **no fault/panic/deadlock**.
- **Bare metal (real Ryzen 7 2700X, NVMe-log boot_id=1):** `SMP numNodes=2`, self-test **5/5**, `MODEL_LOAD "GGUF loaded OK"`, **14 coherent Gemma 4 E2B inferences**, `IPC_STATS q=100 err=0`.
- **No single-thread penalty from SMP:** decode held at **~1.53 tok/s** (gen=50, cyc≈121.0e9, TSC 3.6999970 GHz) — within ~0.2% of M1's single-core ~1.53 tok/s. The big-kernel-lock adds negligible overhead on the 1T path (PA and PB still run on node 0; see E1).
- **E1 (the load-bearing M3 input):** under SMP with no `SetAffinity`, **PA `apic=0` AND PB `apic=0`** — both processes run on core 0; the AP (core 1) is **idle** (host QEMU ≈100%, one core). **seL4 does not auto-distribute threads** — a spawned process inherits its creator's node. ⇒ **M3 workers MUST call `seL4_TCB_SetAffinity` to use the second core.** Corollary: at M2 (no SetAffinity) the existing system runs entirely on node 0 exactly as on M1 — the multicore kernel is a *dormant, regression-free foundation*, which is the safe outcome for this milestone.

(The bare-metal run is `boot_id=1` rather than `2` because the operator intentionally cleared the telemetry log with `clear_nvme_log.sh` before booting — expected; the burn-in already proved cursor/boot_id persist across boots, so this is not a durability regression.)

## Consequences

- **M3 (seL4-native threadpool) is unblocked** and is the next milestone. The E1–E7 recon captured during this spike (`phase4/weeks/week04/WEEK_04_STATUS.md`) is its design input: workers need explicit `SetAffinity` (E1), must block (not busy-spin) when idle (E2), must minimize syscalls in the parallel-for hot path because the BKL serializes them (E4), and must isolate `state->fwd_scratch` per worker while output rows stay disjoint and weights read-only with no hot-path malloc (E7).
- **Maturity risk accepted:** the multikernel BKL kernel is less battle-tested than the uniprocessor kernel. Mitigations: keep `NUM_NODES` minimal until M3 bandwidth testing; the per-build config gate prevents silent config drift; the M1 single-core image remains a captured rollback.
- **Verification stance unchanged in substance** — the running x86-64 system was already non-verified (fast path); SMP is a third deviation, not a new category. Project docs continue to avoid claiming the running system is "formally verified" (per ADR 2026-06-16).
- **v1.0 fallback preserved:** if M3 threading underdelivers, Branch B (AVX2 single-thread, ~1.53 tok/s) can still satisfy goal #1 for v1.0, now demonstrably without SMP-induced regression.

## Alternatives rejected

- **Branch B now (ship 1T, defer SMP/threadpool).** Caps goal #1 at ~1.53 tok/s and leaves the 8C/16T machine's parallelism unused. Rejected as the *primary* path (kept as a fallback) — the threaded target (~5–8 tok/s, stretch ~8–9) is the reason goal #1 reframed to "inference performance."
- **Max out NUM_NODES now (e.g. 8).** Over-commits before bandwidth testing and enlarges the cross-core surface for a foundation spike. Rejected — NUM_NODES=2 is the minimal cross-core test; production N is an M3 decision.
- **Block SMP on regaining verification.** Would require `KernelFastpath=OFF` + FXSAVE + uniprocessor — killing IPC and AVX perf for a proof this build already doesn't hold. Rejected (see verification-stance ADR).

## References

- M2 spike detail + E1–E7 recon: `phase4/weeks/week04/WEEK_04_STATUS.md`
- Goal-#1 plan (M0–M4, SMP findings): `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` (§1 Q2, §2 M2/M3, §3)
- M1 single-thread result: `phase4/weeks/week03/WEEK_03_STATUS.md`
- Verification stance (why SMP is maturity, not forfeit): `docs/decisions/2026-06-16-x86-verification-stance.md`
- seL4 FAQ (BKL; multicore verification "in progress"): <https://sel4.systems/About/FAQ.html>
- Related ADRs: `docs/decisions/2026-06-16-defer-gpu-inference.md`, `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`
