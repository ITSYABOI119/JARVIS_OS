# Week 03 Status: Phase 4 Goal #1 — M1 (AVX2 in the seL4 build) — DONE, bare-metal verified

**Date:** June 17, 2026
**Status:** M1 COMPLETE — Gemma 4 E2B **~1.53 tok/s** single-thread AVX2 on bare metal (from ~0.2 scalar, **~7.6×**). Both M0 carve-outs CLOSED.
**Phase:** Phase 4 — Production (v1.0), goal #1 "Inference performance" (CPU path). Hardware-in-the-loop on the JARVIS PC (Ryzen 7 2700X).

---

## Summary

M1 turns on global `-mavx2 -mfma -mf16c` in **both** engine app targets (kernel unchanged from M0's XSAVE). This was the **first bare-metal flash** of the XSAVE + `SIMULATION=OFF` + AVX2 config — KVM-gated before flash, with a captured rollback. The AVX2 kernels (`qdot.c`, `tensor_ops.c`, `llama_quant.c`) light up with **no engine source change**.

## Result — bare metal (NVMe log, boot_id=1, 2026-06-17)

| Check | Result |
|-------|--------|
| Boot (SIMULATION=OFF, IOMMU-off, XSAVE, AVX2) | **clean** |
| Self-test | **5/5 PASS** |
| Model load (Gemma 4 E2B from real NVMe) | `MODEL_LOAD` "GGUF loaded OK" |
| Coherent generation | **15 inferences, coherent text** (`LOG_INFER` snippets) |
| Stability | `IPC_STATS q=100 hit=71 err=0` |
| **Decode tok/s** | gen=50, cyc≈**120.7e9**, TSC=**3.6999970 GHz** → **~1.53 tok/s** |
| vs scalar baseline (~0.2 tok/s) | **~7.6×**; ~90% of the native single-thread engine (1.70 tok/s) |

tok/s = gen × TSC_HZ / cyc = 50 × 3.6999970e9 / 120.7e9 ≈ **1.53 tok/s** (decode, single-thread, AVX2).

## AVX2 enablement (reproducible)

- `-mavx2 -mfma -mf16c` added to `target_compile_options` of **both** apps (`sel4test-driver` = PA, `jarvis-inference` = PB) via `build_jarvis_x86.sh`.
- **`-isystem <gcc include>` fix:** `-mavx2` makes the engine `#include <immintrin.h>`, but the seL4 app build excludes gcc's intrinsics-header dir → `immintrin.h: No such file`. Fixed by adding gcc's include dir (`gcc -print-file-name=include`, computed) via `-isystem` so the intrinsics resolve while musl still supplies libc.
- No engine `.c/.h` change — the `#ifdef __AVX2__` kernels activate automatically.

## The SIMULATION=OFF fallout — IOMMU (root cause + fix)

The first bare-metal boot of `SIMULATION=OFF` **would have failed** without this fix:
- `projects/jarvis-x86/settings.cmake` force-sets **`KernelIOMMU ON`** for non-simulation x86 builds (inherited from sel4test, whose IOMMU tests want VT-d). Under `SIMULATION=ON` the sim block force-set it OFF for us; with `SIMULATION=OFF` (required for AVX/XSAVE) that force is gone → IOMMU ON.
- **IOMMU ON ⇒ the NVMe driver's direct-physical DMA is blocked ⇒ model load fails.** `-DKernelIOMMU=OFF` on the cmake line cannot override the project settings force.
- **Fix:** patch `jarvis-x86/settings.cmake` `KernelIOMMU ON → OFF` (idempotent, in `build_jarvis_x86.sh`). Verified: `gen_config` `/* disabled: CONFIG_IOMMU */`.

**Config delta vs the burn-in kernel (answers the open M0 item "what did SIMULATION=OFF change beyond XSAVE"):** the *only* functional change is **FXSAVE→XSAVE** (M0). `KernelSupportPCID=OFF`, `KernelFSGSBase=msr`, `KernelIOMMU=OFF` (re-forced), `KernelMaxNumNodes=1`, `KernelFastpath=ON` all end up **identical** to the known-good burn-in config. So `SIMULATION=OFF` changed nothing functional beyond enabling XSAVE, once IOMMU is re-forced off.

## M0 carve-outs — both CLOSED

1. **Coherent model-gen on bare metal** — CLOSED: 15 coherent inferences (text in `LOG_INFER` snippets) under the AVX2 build.
2. **SIMULATION=OFF bare-metal boot** — CLOSED: boots clean on the real 2700X (self-test 5/5, model load, workload), with the IOMMU fix.

## Measurement method (flag-guarded, bench-only)

`JARVIS_M1_MEASURE` (default **0**): Process B brackets the generation loop with RDTSC and reports `M1 gen=<n> cyc=<cycles> | <response snippet>` via `MSG_DEBUG`; Process A writes it to the NVMe log as `LOG_INFER`. Offline: tok/s = n × TSC_HZ / cyc, TSC_HZ = 3.6999970 GHz (refined TSC calibration from the box). Reading the snippet from the same record also verifies coherence — no serial capture needed.

**Deployment note:** the instrument writes one `LOG_INFER` per inference; over a long/soak run that would overrun the no-wrap 2700-entry log (silent `-2`). So it is **bench-only** — the **deployed USB image was rebuilt with `M1_MEASURE=0`** (all stability flags at committed defaults; instrument source stays in-tree, guarded off).

## NVMe log handling

- Pre-M1 burn-in telemetry **archived** before clearing: `~/nvme_log_archive_pre_m1.{bin,txt}` (boot_id=1, 9 entries, final `IPC_STATS q=400 hit=292 err=0` — the 2026-06-15 burn-in).
- Cleared via the new **`phase3/scripts/clear_nvme_log.sh`** (derives LBA/count from `nvme_log.h`, bounds-checks against the device size, requires `--yes`). M1's boot then wrote a fresh `boot_id=1` log.

## Files

| File | Change |
|------|--------|
| `phase3/scripts/build_jarvis_x86.sh` | AVX2 flags (`-mavx2 -mfma -mf16c -isystem <gcc inc>`) on both apps + `KernelIOMMU ON→OFF` settings.cmake patch |
| `phase3/scripts/clear_nvme_log.sh` | **NEW** — NVMe telemetry-log reset utility |
| `phase3/src/sel4/jarvis_debug.h` | `+ JARVIS_M1_MEASURE` (default 0) |
| `phase3/src/sel4/inference_server.c`, `main_x86.c` | RDTSC decode timing + `LOG_INFER` reporting (guarded off) |
| `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` | M1 DONE + tok/s; M0 carve-outs CLOSED; config delta |
| `CLAUDE.md` | Validated-Metrics seL4-build AVX2 row + status + pointers |

## Next

- **M2 — SMP decision point** (goal-#1 plan §2). Build is uniprocessor; the threadpool is gated on SMP. Per ADR `docs/decisions/2026-06-16-x86-verification-stance.md`, the M2 SMP call is a **maturity** decision, not a verification forfeit (this build already runs `KernelFastpath=ON`, outside the verified X64 config). Branch B (ship AVX2 single-thread for v1.0) is now viable given the ~1.53 tok/s result; Branch A (enable SMP) targets the ~8–9 tok/s threaded number.

---

*Phase 4 cadence: weekly status at `phase4/weeks/weekN/WEEK_N_STATUS.md`.*
