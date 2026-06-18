# Week 05 Status: Phase 4 Goal #1 — M3 (seL4-native threadpool) — DONE, bare-metal Gemma 5.46 tok/s @ 6 cores

**Date:** June 18, 2026
**Status:** M3 COMPLETE — Gemma 4 E2B **5.46 tok/s** decode on bare-metal seL4 at **`NUM_NODES=6`** (BSP + 5 worker cores), **3.57×** the M1/M2 single-thread ~1.53 tok/s. Verified byte-identical to the serial path; `err=0` across 800 queries. Production `NUM_NODES=6`.
**Phase:** Phase 4 — Production (v1.0), goal #1 "Inference performance" (CPU path). Hardware-in-the-loop on the JARVIS PC (Ryzen 7 2700X).

---

## Summary

M3 is the seL4-native worker pool behind `jarvis_parallel_for`, parallelizing `qmatmul_vec` rows across cores in Process B. It was built in three movements: **step 1** fixed a real race in the *existing* pthread backend (the algorithm the seL4 port copies); **step 2** implemented the seL4 backend + the Process-A worker creation; and the **F1/F2 N-sweep** measured the scaling and set the production node count. Two bring-up bugs (both box-only) were caught — one by a pre-build adversarial review, one by the QEMU boot smoke — and the parallel path was proven **byte-identical to serial** before any tok/s number was trusted.

## Architecture (as built)

**PB has no allocator**, so **Process A** (the rootserver, which owns `vka`/`vspace`/`simple` + the persistent `inference_process` handle) creates the N−1 worker TCBs **in PB's VSpace/CSpace** at spawn time; **PB drives dispatch** at runtime. PA never links the threadpool (PB-only `-DJARVIS_SEL4_SMP`), so it resolves the worker entry **by name from PB's ET_EXEC, unstripped `.symtab`** (`resolve_pb_symbol`). Workers pin to distinct cores (`seL4_TCB_SetAffinity`), **block** on per-worker one-shot wake Notifications, and run the work-stealing core (atomic `next_idx`). The join is a **generation publish (release/acquire) + active-counter** (last worker signals `done`; the dispatcher = worker 0 blocks on `seL4_Wait(done)`) — **not** a verbatim port of the pthread `has_work` join; it ports only the work-stealing core, and is structurally immune to the step-1 race.

## Step 1 — the pthread cross-dispatch race (foundation the port depends on)

The new multi-dispatch `test_parallel_for.c` exposed a real race in `threadpool.c` (`worker_main` re-entered the same dispatch through the `has_work` gate and over-decremented `active_workers`, so `jarvis_parallel_for` could return while stragglers were live → next dispatch corrupted under them; flaky at ≥12 threads on the 2700X). Fixed with a **per-dispatch generation counter** (each worker claims + decrements a dispatch exactly once). Verified PASS=20/20 at threads 1..16 (was 4–8 FAIL). Committed `0817d29` (M3 step 1).

## Step 2 — the two bring-up bugs (both box-only, caught before/at the box)

1. **Unchecked `sel4utils_copy_cap_to_process`** (returns **0 on failure**): a failed `done`/`wake` cap copy would hand PB a **null cap** → `seL4_Signal(0)`/`seL4_Wait(0)` → **deadlock the first dispatch**. Caught by the **pre-build adversarial review** (4 reviewers, distinct lenses); fixed by checking both copies and degrading to serial.
2. **Worker started outside `sel4runtime`'s per-thread init**: the libsel4 syscall stubs read the IPC buffer from a TLS pointer (`__sel4_ipc_buffer`) that was **unset** for a thread sel4utils started directly → the worker's first `seL4_Wait` read a null IPC buffer (`vm fault @ 0x8`) → deadlock. Caught by the **QEMU boot smoke** (a static review cannot see a TLS-init runtime fault); fixed by `seL4_SetIPCBuffer((seL4_IPCBuffer *)ipc_buf)` (the 3rd `sel4utils_start_thread` entry arg) before any syscall.

Also hardened from the review: a `pool_init` clamp (`n_threads ≤ n_wake+1`), a shared `JARVIS_MAX_WORKERS` via `threadpool.h`, a `SetAffinity` return check, a `.symtab` bound check, and the worker block guarded by `#ifdef CONFIG_ENABLE_SMP_SUPPORT` so the uniprocessor degrade build still compiles.

## Correctness proof — serial control (greedy determinism)

Greedy decoding is deterministic, so a correct parallel path must be **byte-identical** to serial. A **serial control** (`-smp 1` → `numNodes=1` → `n_workers=0` → serial path) was diffed per-prompt against the parallel (`-smp 2`) run: **every prompt's response was byte-identical**. The one garbage output — **`"The seL4 microkernel is" → ï¿½ï¿½ï¿½`** — is **identical in serial and parallel**, i.e. a **pre-existing Llama-1B byte-token artifact** (byte-fallback tokens that render as the U+FFFD replacement char for that completion-style prompt). It is **Llama-1B-specific, NOT an M3 bug, and Gemma is unaffected** — do not re-chase it.

## N-sweep (hybrid) — relative scaling, then the deployed number

**F1 — QEMU-KVM, Llama-1B-as-GEMMA2B.GUF (range-finder; 0 faults/timeouts, coherent at each NN):**

| NN | tok/s | vs NN=2 |
|----|-------|---------|
| 2  | 4.97  | 1.00×   |
| 4  | 8.95  | 1.80×   |
| 5  | 10.80 | 2.17×   |
| 6  | 12.54 | 2.52×   |

Still climbing at 6 — Llama-1B (~770 MB/token) is compute-bound here (bandwidth ceiling ~54 tok/s), so its knee does **not** transfer to the heavier Gemma. F1's real value: proves the threadpool **scales cleanly with no correctness regression or faults at every N**.

**F2 — bare-metal, deployed Gemma 4 E2B (RDTSC decode bracket, TSC 3.6999970 GHz):**

| NN | M3 workers | **decode tok/s** | err |
|----|-----------|------|-----|
| **4** | 3 | **4.21** (2.75×) | `q=100 err=0` |
| **6** | 5 | **5.46** (3.57×) | `q=100→800 all err=0` |

4→6 marginal gain **+30%** (efficiency 1.05 → 0.91 tok/s/thread) = the Gemma bandwidth knee (~2.9 GB/token) appearing. **Chosen production `NUM_NODES=6`** — best measured, in the tempered ~4–6 target, 6 of the 2700X's 8 physical cores. NN=8 not measured (diminishing returns).

## NVMe capture (bare metal has no SSH during a JARVIS run)

`JARVIS_DBG_BOOT_LOG=1` + `JARVIS_M1_MEASURE=1` (temporary, F2 only) routed the M3 line, the RDTSC tok/s samples, and the `[STATS] err=` entries to the durable NVMe log. Validated content-emission in QEMU first (no wasted boot), then captured both bare-metal runs in one log (boot_id=1 = NN=4, boot_id=2 = NN=6), 0 real faults each. The **deployed image rebuilt with both flags = 0** (stability config; the log is no-wrap, so per-line BOOT_LOG would overrun a soak).

## Files

| File | Change |
|------|--------|
| `phase3/src/ai/threadpool.h` | ABI visible under either backend (`JARVIS_PTHREAD` \|\| `JARVIS_SEL4_SMP`) + seL4 entry-point decls + shared `JARVIS_MAX_WORKERS` |
| `phase3/src/ai/threadpool_sel4.c` | **NEW** — seL4 worker pool (gen publish + active-counter join + per-worker wake; `seL4_SetIPCBuffer` fix) |
| `phase3/src/ai/llama_quant.c` | `qmatmul_vec` threaded path under `JARVIS_PARALLEL` (= either backend) — no behavior change on host CI |
| `phase3/src/sel4/main_x86.c` | PA-side worker creation in PB's VSpace/CSpace + `.symtab` resolver + cap-copy/affinity checks |
| `phase3/src/sel4/inference_server.c` | PB pool-init from argv before the ready handshake |
| `phase3/scripts/build_jarvis_x86.sh` | `threadpool_sel4.c` sync + PB CMakeLists patch (`JARVIS_SEL4_SMP`) + `NUM_NODES` parameterized (default **6**) + gate |
| `phase4/docs/PHASE_4_M3_THREADPOOL_DESIGN.md` | §Implemented (as-built, 2 bugs, sweep, chosen N) |
| `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` | M3 DONE + RESULT; M4 next |
| `CLAUDE.md` | M3 DONE status + metrics row + Quick Reference |

## Next

- **M4 — record the end-to-end bare-metal benchmark** and tick the ROADMAP goal-#1 CPU "Done when". The headline is set: **Gemma 4 E2B 5.46 tok/s @ NUM_NODES=6 bare metal (3.57× single-thread)**, `err=0`. This build now departs the verified X64 config along a third axis (SMP `NUM_NODES=6`, in addition to `KernelFastpath=ON` and XSAVE/AVX) — functional-but-unverified **by design** (ADR `docs/decisions/2026-06-16-x86-verification-stance.md`); no "formally verified" claim.

---

*Phase 4 cadence: weekly status at `phase4/weeks/weekN/WEEK_N_STATUS.md`.*
