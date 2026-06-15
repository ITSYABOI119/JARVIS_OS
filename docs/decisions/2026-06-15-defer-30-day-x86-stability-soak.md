# ADR: Defer the formal 30-day x86 stability soak

**Date:** 2026-06-15
**Status:** Accepted
**Deciders:** JARVIS Development

## Context

Phase 3's success criteria (`PHASE_3_IMPLEMENTATION_PLAN.md`) list a "30-day stability test on x86 (0 crashes, <1% errors)" as a **MUST** item gating the `v0.3.0-beta` tag. The docs were internally inconsistent: the Week 29-30 section treated the x86 soak as skipped (citing the Pi 4 run), while the status header, the What's-Left table, and the acceptance criteria still read as pending. Neither framing was accurate — no x86 soak was run, and the Pi 4 run is *not* equivalent coverage of the new x86 surfaces. This ADR sets the record straight as a deliberate, risk-accepted deferral.

The bare-metal seL4 x86 system is now built and burned-in on the JARVIS PC (Ryzen 7 2700X). Before committing the only bare-metal box to an uninterrupted month-long run, we assessed whether the formal 30-day x86 soak is worth its cost given the burn-in / fuzz / audit coverage already in place.

## Decision

Do **not** run the formal 30-day x86 stability soak as a gate for Phase 3 / `v0.3.0-beta`. This is a **risk-accepted descoping — not a pass.** No 30-day x86 soak was run. The residual *duration* risk is judged acceptable because:

- the load-bearing **new** x86 surfaces — `shmem_ipc` (SPSC rings), in-process `qmodel` inference (Process B), and NVMe model loading — have **burn-in + fuzz + audit** coverage (see Rationale), not 30-day duration, and
- Phase 2's 30.6-day Pi 4 run is **supporting context** for the *shared* concepts (decision cache + ring-buffer patterns) — it used a different IPC (UART dual-ring) and the ARM stack, so it is **not equivalent coverage** of the x86 surfaces.

A long-duration soak can still be started at any time — the flashed USB boots straight into the continuous workload loop and NVMe logging is operational — it is simply no longer a blocking milestone.

## Rationale

1. **Partial precedent, not equivalence.** The decision-cache and ring-buffer *concepts* that recur in the long-run surface were exercised by Phase 2's 30.6-day Pi 4 run (**30.6 days / 0 crashes / 99.8% pass**). But that run used a **different IPC** (UART dual-ring) and the ARM driver stack — it did **not** exercise the new x86 surfaces (`shmem_ipc` SPSC rings, in-process `qmodel` inference, NVMe model load). It is supporting context for the shared design, not proof of the x86 build.
2. **x86 bare-metal burn-in passed (2026-06-15) — hours, not 30 days.** Clean boot, NVMe model load (LBA-32768 partition), coherent Gemma 4 E2B generation, the Task 5 heartbeat/shield IPC fix holding at **err=0 across 400 real-hardware queries** (`IPC_STATS q=100/200/300/400`, `boot_id` constant), durable NVMe logging. A QEMU+KVM soak independently reached **err=0 at q=200** on the same build. This is a burn-in, not a long-duration soak.
3. **The failure modes a soak hunts for are independently covered.** Fuzz harness (300K iterations, ASAN/UBSan clean), two security audits (51 findings, 0 HIGH/MED open), and the Task 5 IPC race fix (`commit aa9dbe8`) address the crash / memory-corruption / IPC-desync / log-saturation modes.
4. **Poor cost/value at the current speed.** The deployed build is single-core, single-thread, scalar (the seL4 build compiles **without** `-mavx2`/`-mfma` and uses the serial threadpool stub) → **~3k queries/day**. A 30-day soak would tie up the only bare-metal box for a month to accumulate ~90k queries — marginal additional assurance over what is already shown. Throughput is a Phase 4 concern (GPU/threading), not a Phase 3 stability concern.

## Consequence

- `v0.3.0-beta` ships **without** a 30-day x86 soak; its stability posture rests on the x86 burn-in + fuzz + audit, with Phase 2's Pi 4 30-day run as supporting context — the **duration gap is the accepted risk.**
- Success criterion #4 in `PHASE_3_IMPLEMENTATION_PLAN.md` remains a **MUST but is DEFERRED** — descoped from `v0.3.0-beta` gating (risk-accepted). It is **not** marked met or passed; no 30-day x86 soak was run.
- CLAUDE.md "Next:" no longer lists the 30-day test; the next active work is TurboQuant/RotorQuant evaluation and Phase 4 performance (AVX2 + threading in the seL4 build).

## What is preserved

- The stability harness, the continuous IPC workload loop, and NVMe logging (`JARVIS_STATS_NVME_INTERVAL=100`, ~870 entries over 30 days) remain in place and working.
- The flashed USB boots directly into the workload loop, so a soak of any duration can be started on demand without a rebuild.

## What would trigger a real long-duration soak

- **Before a v1.0 production claim.** Phase 4 (`phase4/docs/ROADMAP.md`) already specifies a 90-day soak — run it there, ideally **after** the Phase 4 perf work (GPU/threading) raises the query rate enough to accumulate meaningful volume.
- **An x86-specific stability signal.** If an x86-only defect (driver, timer, NVMe write wear, memory) is suspected that the Pi 4 run could not have surfaced.

Not triggers: generic "more is better," or re-running a Pi 4-style soak of the *shared* cache/ring concepts (those have Pi 4 precedent; the x86 surfaces are covered by burn-in + fuzz + audit instead).

## References

- Bare-metal burn-in log (2026-06-15): NVMe `IPC_STATS q=100..400 err=0`, `boot_id=1` constant (hours of runtime, not a 30-day soak).
- IPC fix: commit `aa9dbe8` (heartbeat/shield poll-the-ring fix), Task 5; verified in QEMU at err=0/q=200.
- Phase 2 stability: 30.6 days / 0 crashes (`PHASE_3_IMPLEMENTATION_PLAN.md` §Phase 2 Baseline; CLAUDE.md Phase 2 milestones).
- Plan note already partially reflecting this: `PHASE_3_IMPLEMENTATION_PLAN.md` Week 29-30.
- NVMe stats interval: ADR-adjacent commit `6796986` (`JARVIS_STATS_NVME_INTERVAL` 1000→100, measured ~3k q/day).
