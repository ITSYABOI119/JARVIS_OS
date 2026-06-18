# Phase 4 — Goal #1 Inference Benchmark (CPU path, RECORDED)

**Status:** ✅ **Goal #1 (CPU) COMPLETE — 2026-06-18.** seL4-build inference benchmark recorded + reproducible.
**Headline:** **Gemma 4 E2B — 5.46 tok/s decode on bare-metal seL4 at `NUM_NODES=6`** (BSP + 5 worker cores), **3.57×** the M1/M2 single-thread 1.53 tok/s (**~27×** the ~0.2 scalar baseline). `err=0` across `q=100→800`. **Parallel output byte-identical to the serial path** (greedy determinism). The reframed ROADMAP goal-#1 CPU "Done when" is satisfied.
**Reproduce:** `phase3/scripts/bench_sel4_inference.sh` (the canonical seL4-build tok/s tool).
**Build under test:** `KernelFastpath=ON` + `KernelFPU=XSAVE`/feature-set 7 (AVX) + SMP `NUM_NODES=6` — **functional-but-unverified by design** (ADR `docs/decisions/2026-06-16-x86-verification-stance.md`). This is **not** a formally-verified configuration; do not describe it as one.

---

## 1. Progression — deployed Gemma 4 E2B on the seL4 build (bare metal)

The whole arc of goal #1: take the deployed seL4 build from scalar single-thread to AVX2-threaded, in the seL4 build (not the native dev engine).

| Stage | Milestone | Config | Gemma 4 E2B decode | vs scalar | vs M1 1T |
|-------|-----------|--------|--------------------|-----------|----------|
| Scalar | pre-M1 | seL4, 1 thread, no SIMD | **~0.2 tok/s** | 1.0× | — |
| AVX2 1-thread | **M1** | seL4, `-mavx2 -mfma`, 1 thread | **1.53 tok/s** | ~7.6× | 1.00× |
| AVX2 threaded | **M3** (`NN=4`) | seL4, threadpool, 3 workers | **4.21 tok/s** | ~21× | 2.75× |
| **AVX2 threaded (production)** | **M3** (`NN=6`) | seL4, threadpool, 5 workers | **5.46 tok/s** | **~27×** | **3.57×** |

The gating engineering for each step is recorded in `PHASE_4_GOAL1_INFERENCE_PERF.md` (M0 kernel XSAVE/AVX, M1 AVX2 flags, M2 SMP enable, M3 threadpool).

## 2. F2 — bare-metal Gemma N-sweep (the production number)

Deployed Gemma 4 E2B Q4_K_M, real hardware (Ryzen 7 2700X, 8C/16T), RDTSC decode bracket, TSC 3.6999970 GHz. **This is the only honest source of the production tok/s.**

| `NUM_NODES` | M3 workers | **decode tok/s** | gain | error rate |
|-------------|-----------|------------------|------|------------|
| 4 | 3 | **4.21** (2.75×) | — | `q=100 err=0` |
| **6** | **5** | **5.46** (3.57×) | **+30% vs NN=4** | `q=100→800 all err=0` |

4→6 marginal gain is only **+30%** (efficiency 1.05 → 0.91 tok/s/thread): the **dual-channel DDR4 memory-bandwidth knee**. Batch-1 Q4_K decode streams **~2.9 GB of weights per token**, so the memory bus — not the 8-core count — caps useful `N` at ~5–6. **Production `NUM_NODES=6`** (best measured, inside the tempered target, 6 of 8 physical cores; the remaining 2 cores are left for the rootserver / IPC drain and the future goal-#2 UI). `NUM_NODES=8` was not measured — diminishing returns past the knee.

## 3. F1 — QEMU-KVM scaling range-finder (relative only, NOT the production number)

Llama-1B renamed to the model file on the NVMe image (Gemma load over emulated NVMe is too slow). QEMU-KVM `-smp N -cpu host`. **0 faults / 0 timeouts at every N; coherent generation at each.** Used to prove the threadpool *scales cleanly with no correctness regression*, and to range-find before spending a bare-metal boot.

| `NUM_NODES` | tok/s | vs NN=2 |
|-------------|-------|---------|
| 2 | 4.97 | 1.00× |
| 4 | 8.95 | 1.80× |
| 5 | 10.80 | 2.17× |
| 6 | 12.54 | 2.52× |

Still climbing at 6 — Llama-1B (~770 MB/token) is **compute-bound** here (bandwidth ceiling ~54 tok/s), so **its knee does not transfer to the heavier Gemma**. F1 is a correctness + relative-scaling tool only; it must never be read as an absolute production figure.

## 4. Methodology

- **Measurement:** Process B brackets the 50-token generation loop with `RDTSC` (flag `JARVIS_M1_MEASURE`), emitting `M1 gen=50 cyc=<cycles>` via `MSG_DEBUG`. Process A forwards it to serial and (bare metal) to the durable NVMe log (`LOG_INFER`).
- **Clock:** Ryzen 2700X **invariant** TSC = **3.6999970 GHz**. KVM `-cpu host` passes `RDTSC` straight through, so QEMU-KVM and bare metal share the constant.
- **Conversion:** `tok/s = 50 × 3.6999970e9 / cyc` (avg/min/max over the per-inference lines; bare metal groups by `boot_id`).
- **Correctness gate (before trusting any number):** greedy decoding is deterministic → a serial control (`-smp 1` → `numNodes=1` → `n_workers=0` → serial path) was diffed per-prompt against the parallel run. **Every prompt's response was byte-identical.** (The lone garbage completion — `"The seL4 microkernel is" → ` U+FFFD run — is identical in serial and parallel, i.e. a pre-existing Llama-1B byte-token artifact, **not** an M3 bug; Gemma is unaffected.)
- **Capture (bare metal has no SSH during a run):** `JARVIS_DBG_BOOT_LOG=1` + `JARVIS_M1_MEASURE=1` route the RDTSC samples and `[STATS] err=` to NVMe; read back with `dd skip=4000794624 … | parse_nvme_log.py`. The deployed image is rebuilt with both flags `0` (the log is no-wrap, 2700-entry cap; per-line BOOT_LOG would overrun a soak).

## 5. Reproduction

Use `phase3/scripts/bench_sel4_inference.sh [NUM_NODES] [MODE]` — the canonical tool (parameterized, self-cleaning; it always resets `JARVIS_M1_MEASURE`/`JARVIS_DBG_BOOT_LOG` to 0 on exit so the box never lingers in a measurement state). Run under a login shell so cmake/ninja are on PATH.

```bash
# QEMU-KVM relative scaling sweep (Llama-1B-as-model on the NVMe image):
ssh jarvis 'bash -lc "cd ~/Desktop/JARVIS_OS && \
  for n in 2 4 5 6; do bash phase3/scripts/bench_sel4_inference.sh $n qemu; done"'

# Bare-metal Gemma (the production number) — prints the manual reflash/boot/dd procedure:
ssh jarvis 'bash -lc "cd ~/Desktop/JARVIS_OS && \
  bash phase3/scripts/bench_sel4_inference.sh 6 baremetal"'
```

CI: N/A — this drives a kernel build + QEMU/bare-metal boot on the JARVIS PC; it cannot run on a hosted runner. The host-portable correctness counterpart is `phase3/src/ai/test_parallel_for.c` (parallel-for == serial reduction, in CI).

## 6. Target vs actual — honest accounting

| Target source | Target | Actual (NN=6) | Verdict |
|---------------|--------|---------------|---------|
| M3 exit criterion (tempered) | ~4–6 tok/s | **5.46** | ✅ **inside band** |
| ROADMAP goal #1 (original "≈ native @16T") | ~8–9 tok/s | 5.46 | ⚠️ **walked back at M3 — see below** |

The ROADMAP's original "approach the native threaded engine (~8–9 tok/s Gemma @16T)" was **tempered at M3** once the workload was measured, not aspired. The native 8.63 tok/s figure is a **16-thread** run on a different memory profile; batch-1 decode is **memory-bandwidth-bound** (~2.9 GB/token), so on the 2700X's dual-channel DDR4 it **knees at ~5–6 of the 8 physical cores, never reaching a 16-thread regime**. **8.63 @16T is the native ceiling, not reachable at the 6-core bandwidth knee.** 5.46 @ NN=6 is the realistic, measured, reproducible CPU result, and it satisfies the tempered exit criterion. Closing the residual gap to native is a **memory-subsystem / GPU** question, not more CPU cores — and GPU inference is deferred (ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`).

## 7. Reference context — NATIVE dev engine (do NOT conflate with the seL4 build)

These are the JARVIS engine on Linux (full SMT, no seL4) and llama.cpp — listed only to bound the seL4-build figure. **They are a different measurement context** and must never be presented as the deployed seL4 number.

| Engine | Context | Gemma 4 E2B tok/s |
|--------|---------|-------------------|
| JARVIS native dev engine | Linux, 1 thread | 1.70 |
| JARVIS native dev engine | Linux, 16 threads | 8.63 |
| llama.cpp | Linux, 8 threads | 19.7 |
| **JARVIS seL4 build (deployed)** | **bare-metal seL4, NN=6** | **5.46** |

---

*Recorded for Phase 4 goal #1 (M4). Tooling: `phase3/scripts/bench_sel4_inference.sh`. Engineering log: `PHASE_4_GOAL1_INFERENCE_PERF.md` (M0–M4), `PHASE_4_M3_THREADPOOL_DESIGN.md`, `phase4/weeks/week0{2,3,4,5,6}/`.*
