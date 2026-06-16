# ADR: TurboQuant/RotorQuant evaluated — deferred to Phase 4

**Date:** 2026-06-15
**Status:** Accepted
**Deciders:** JARVIS Development

## Context

Phase 3c carried a "TurboQuant/RotorQuant eval" item (KV-cache compression). The evaluation is **complete**, not pending: TurboQuant was fully implemented, unit-tested, benchmarked, and IPC-integrated on the `experiment/turboquant-benchmark` branch (~3,500 LOC), and RotorQuant was assessed as a reference (Python/CUDA) alternative. That branch's `phase3/docs/TURBOQUANT_EVALUATION.md`, `BENCHMARK_RESULTS.md`, and `ROTORQUANT_REFERENCE.md` hold the full record. This ADR records the verdict and the disposition; the branch stays parked (unmerged) as the evaluation artifact.

## Decision

Do **not** integrate TurboQuant or RotorQuant in Phase 3. **Defer to Phase 4.** Keep `experiment/turboquant-benchmark` parked (unmerged) as the evaluation record — do **not** merge or cherry-pick it into master.

## Rationale

1. **TurboQuant works mathematically but fails multi-step generation at small `head_dim`.** Compression matches the paper within 1-2% and single-step quality is perfect — but multi-step generation breaks down at small `head_dim`: Llama 1B (d=64) drops to ~8.9% token match with repetition loops. This is a fundamental SNR limit, **not a bug** — *more* bits make it worse. It is viable at d=128 (Llama 3B: 45-100% match); the real payoff is 7B-70B (d≥128) on memory-constrained systems.
2. **Low value for the deployed model.** Gemma 4 E2B is a 2B model with KV-sharing + sliding-window attention that fits comfortably in 32 GB — it is not KV-cache-bound, so KV compression buys little. (Caveat: the eval tested Llama 1B/3B, not Gemma; the conclusion holds because a 2B KV-sharing model is not KV-bound regardless.)
3. **RotorQuant is the preferred approach if KV compression is ever pursued** — O(d) vs O(d²) block-diagonal rotations, ~28% faster decode, 5.3x faster prefill, simpler rotation math — but it is **reference-only (Python/CUDA)** and needs a C port. Not worth porting for a use case that is not needed yet.
4. **The branch is a merge hazard.** It regressed master driver code (PCI 64-bit BAR, AHCI init, NIC RX, tensor_ops AVX2) and predates the dynamic-scaling removal, the hb/shield IPC fix, the perf work, and the model-filename / NVMe-log changes. **DO NOT MERGE.**

## Consequences

- The Phase 3c "TurboQuant/RotorQuant eval" item is **DONE (evaluated)**; the outcome is **deferred to Phase 4** — not skipped, not integrated.
- Phase 4 KV-compression work, if pursued, should: prefer **RotorQuant** (IsoQuant quaternion, ~20 LOC of rotation math) ported to C; target **7B+ / d≥128** models; and adopt the **"deferred K quantization"** trick (K stays FP16 during prefill) regardless of which rotation is chosen.
- Optional future cherry-pick: the clean `qmatmul.h` refactor from the branch — **only if** it preserves the AVX2 path in `llama_quant.c`. (Noted as a future option; not done now.)
- `v0.2.1-beta` remaining work no longer includes TQ/RQ — only the final report + tag (the stability soak is owner-scheduled/decoupled, ADR `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`).

## What would trigger resurrection

- Phase 4 with **7B+ models on memory-constrained hardware** (especially GPU KV-cache pressure), where d≥128 makes TurboQuant viable and KV compression actually pays off.

Not a trigger: the current single-model Gemma 4 E2B deployment (2B, KV-sharing, fits in RAM).

## References

- Evaluation branch (parked, unmerged): `experiment/turboquant-benchmark` — `phase3/docs/TURBOQUANT_EVALUATION.md`, `BENCHMARK_RESULTS.md`, `ROTORQUANT_REFERENCE.md`.
- Quick-reference pointer on master: the `RotorQuant Reference` entry in CLAUDE.md.
- Related ADRs: `docs/decisions/2026-04-17-remove-dynamic-model-scaling.md`, `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`.
