# ADR: Remove Dynamic Model Scaling

**Date:** 2026-04-17
**Status:** Accepted
**Deciders:** JARVIS Development, strategic review

## Context

The `model_scaling.c` subsystem implemented a 4-state scaler (IDLE / ACTIVE / CRITICAL / EMERGENCY) that swapped GGUF models at runtime based on rolling cache-miss rate. The original design (documented in `ARCHITECTURE_ENHANCEMENTS.md` and `JARVIS_UNIFIED_PLAN.md`) called for CRITICAL to be a safety-ensemble of primary + validator models. That ensemble was never built. What shipped was a miss-rate-driven swap — implementation drift from the plan.

Phase 0 measured memory savings from tiering in isolation but never measured whether tier transitions actually improved decision quality on real workloads. Phase 2 Pi 4 never ran the scaler on hardware at all. The "60% memory savings" figure is unproven in production.

A deep-research synthesis in April 2026 recommended deletion with 80% confidence. The user and strategic guide endorsed the recommendation.

## Decision

Delete `model_scaling.{c,h}`, `test_model_scaling.c`, the Process A swap orchestration in `main_x86.c`, the Process B `MSG_MODEL_SWAP` handler in `inference_server.c`, and the `MSG_MODEL_SWAP` / `SWAP_*` constants in `shmem_ipc.h`. Ship single-model Gemma 4 E2B Q4_K_M.

## Rationale

1. **Drift, not feature.** What existed was not the designed system — it was a miss-rate-driven swap with no safety-ensemble. The design rationale (ensemble validation for CRITICAL ops) was not realized.
2. **Unmeasured utility.** Memory savings were measured; quality/latency improvements from tiering were not. The cost of maintaining dead code exceeds the unproven benefit.
3. **No blocking dependency.** Five adjacent systems (SHIELD, decision cache, trust level, specialist agents, shared context pool) are tier-agnostic and continue unchanged.
4. **Bench-off data supports a single winner.** Gemma 4 E2B scored 8.40/10 across 7 judges at 8.63 tok/s @16T — sufficient for the Phase 3 stability target.

## What was kept

- **Rolling cache-miss window.** Useful observability metric. Renamed `miss_win_*` → `cache_miss_window_*` with an explicit `Metrics only — no longer feeds any scaler` comment to signal intent.
- **SHIELD, decision cache, trust level, specialist agents, shared context pool.** Untouched.
- **Both models on disk.** Llama 1B and Gemma 4 E2B GGUF files remain on NVMe. Only the runtime swap is removed.

## What was removed

- `phase3/src/ai/model_scaling.{c,h}` + `test_model_scaling.c`
- Process A scaler orchestration in `main_x86.c` (~146 LOC)
- Process B `MSG_MODEL_SWAP` handler in `inference_server.c` (~42 LOC)
- `MSG_MODEL_SWAP` (0x10) and `SWAP_*` protocol constants
- `test_model_scaling` CI step
- `docs/superpowers/specs/2026-04-16-dynamic-model-scaling-design.md`
- `docs/superpowers/plans/2026-04-16-dynamic-model-scaling.md`

Protocol slot `0x10` is reserved with a comment to prevent future collision.

## What would trigger resurrection

- **Pi 5 port commits.** If the project ports to Pi 5 (smaller memory budget) and measurements show IDLE-tier savings are materially valuable under that constraint.
- **Real safety-ensemble feature work.** If a deliberate plan to build CRITICAL = primary + validator is prioritized (not drift, not observation, a funded feature).

Not triggers: generic "might be useful later," future model family support, or "we can always turn it back on."

## References

- Synthesis: internal research thread 2026-04-17 "Dynamic Model Scaling — Keep or Delete?"
- Previous design: `ARCHITECTURE_ENHANCEMENTS.md` (section marked Superseded)
- Previous plan: `JARVIS_UNIFIED_PLAN.md` §2 (section marked Superseded)
- Bench-off: `phase3/docs/MODEL_BENCH_OFF_2026-04-07.md`
