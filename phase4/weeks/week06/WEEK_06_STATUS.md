# Week 06 Status: Phase 4 Goal #1 — M4 (record benchmark) — DONE; goal #1 (CPU) COMPLETE

**Date:** June 18, 2026
**Status:** M4 COMPLETE — the seL4-build inference benchmark is **recorded + reproducible**. Phase 4 **goal #1 (inference performance, CPU path) is COMPLETE**. Headline: **Gemma 4 E2B 5.46 tok/s @ `NUM_NODES=6`** bare-metal seL4 (3.57× the 1.53 single-thread, ~27× the ~0.2 scalar), `err=0` over 800 queries, parallel output byte-identical to serial.
**Phase:** Phase 4 — Production (v1.0). Goals #2–7 (graphical output, USB input, installer, 90-day soak, docs, `v1.0.0`) remain open.

---

## Summary

M4 is a **recording / closure** milestone — no new engine code, no box boot. The numbers were already captured and independently verified during M1 (1.53 tok/s 1-thread AVX2) and M3 (the F1/F2 N-sweep). M4 distills the throwaway sweep logic into a permanent reproduction tool, writes the recorded benchmark, and ticks the ROADMAP goal-#1 CPU "Done when" box. The box stays on the clean `NUM_NODES=6` deploy image (no measurement state).

## Deliverables

| Artifact | What |
|----------|------|
| `phase3/scripts/bench_sel4_inference.sh` | **NEW** — canonical seL4-build tok/s tool. Args `NUM_NODES` (default 6) + `MODE` (`qemu`\|`baremetal`); sets `JARVIS_M1_MEASURE=1`, builds at NN (config gate runs in `build_jarvis_x86.sh`), measures via the RDTSC `M1 gen=50 cyc=` lines, and an EXIT trap **always** resets `JARVIS_M1_MEASURE=0` + `JARVIS_DBG_BOOT_LOG=0` so the box never lingers in a measurement config. CI: N/A (drives the box). |
| `phase4/docs/PHASE_4_GOAL1_BENCHMARK.md` | **NEW** — the recorded benchmark: progression (scalar → M1 → M3), F2 bare-metal Gemma table (NN=4 4.21 / NN=6 5.46), F1 QEMU scaling table, methodology, reproduction, and an honest target-vs-actual. |
| `phase4/docs/ROADMAP.md` | goal-#1 CPU "Done when" box ticked `[x]`; stale `Status: Draft — Phase 4 not started` footer corrected. |
| `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` | M4 marked DONE (status line + M4 section); §4 goal-#1 Done-when box ticked. |
| `CLAUDE.md` | Phase-4 status: M4 DONE / goal-#1 CPU satisfied; Quick Reference updated. |

## The recorded result

- **Production:** Gemma 4 E2B **5.46 tok/s** @ `NUM_NODES=6` (5 worker cores), bare-metal seL4. **3.57×** the M1/M2 single-thread 1.53; **~27×** the ~0.2 scalar baseline.
- **N-sweep (F2, bare metal):** NN=4 → 4.21, NN=6 → 5.46. The +30% 4→6 gain is the dual-channel DDR4 bandwidth knee (~2.9 GB/token). `NUM_NODES=6` chosen — best measured, inside the tempered ~4–6 target, leaving 2 cores for the rootserver/IPC and the goal-#2 UI.
- **Correctness:** parallel == serial, byte-identical (greedy determinism); `err=0` across `q=100→800`.
- **Honesty:** the running build (`KernelFastpath=ON` + XSAVE/AVX + SMP `NUM_NODES=6`) is **functional-but-unverified by design** (ADR `docs/decisions/2026-06-16-x86-verification-stance.md`) — **not** a formally-verified configuration. The original "≈ native ~8–9 @16T" ROADMAP aspiration was walked back to the tempered ~4–6 once the workload was measured as memory-bandwidth-bound; 8.63 @16T is the native ceiling, not reachable at the 6-core knee.

## Next

- **Phase 4 goal #2 — graphical output:** move beyond VGA text to a framebuffer/HDMI status UI (boot, model load, query/response). UI mockups have been staged under `phase4/docs/ui_mockups/`.
- Remaining Phase 4 goals (#3 USB keyboard input, #4 one-script installer, #5 90-day stability soak, #6 docs, #7 `v1.0.0` MIT release) stay open.

---

*Phase 4 cadence: weekly status at `phase4/weeks/weekN/WEEK_N_STATUS.md`.*
