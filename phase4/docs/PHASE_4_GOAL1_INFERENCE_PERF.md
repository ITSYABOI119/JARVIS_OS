# Phase 4 — Goal #1: Inference Performance (CPU)

**Status:** Research + plan (no engine code yet)
**Date:** 2026-06-16
**Scope:** Roadmap goal #1 reframed (GPU inference deferred, ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`). v1.0 "fast" = **AVX2/FMA + a seL4-native threadpool in the seL4 build**, up from the current scalar single-thread (~0.2 tok/s).
**Sources:** live `~/sel4-x86` build config read read-only via `ssh jarvis` (2026-06-16); seL4 14.0.0 (`ebbda2af5`); seL4 reference manual + kernel source. **Where web claims conflict with the box config read, the config read wins.**

---

## 1. Findings

### Q1 — Is AVX2 safe in a Ring-3 seL4 process? **VERIFIED: NO (not under the current kernel).**

The current kernel saves FPU state with **FXSAVE only** — it does **not** save the AVX `YMM` upper 128 bits across context switches/traps. Enabling `-mavx2` in userspace today would either `#UD` (XCR0.AVX clear) or silently corrupt `YMM[255:128]` when Process B is preempted. **VERIFIED from the box config:**

```
jbuild/kernel/gen_config/kernel/gen_config.h:
  /* disabled: CONFIG_XSAVE */
  #define CONFIG_FXSAVE  1
  #define CONFIG_XSAVE_FEATURE_SET  0
  #define CONFIG_XSAVE_SIZE  512
jbuild/CMakeCache.txt:
  KernelFPU:STRING=FXSAVE   KernelFPUFXSave=ON   KernelFPUXSave=OFF   KernelXSaveSize=512
  KernelX86MicroArch:STRING=nehalem   (Nehalem predates AVX)
QEMU sim CPU opts: -cpu Nehalem,...,-xsave,-xsaveopt,-xsavec   (xsave explicitly disabled)
```

**How AVX becomes safe (seL4 manual + `src/arch/x86/config.cmake`, `machine/fpu.c`):** the kernel saves exactly the registers in its compile-time XSAVE feature mask. To cover AVX `YMM`:
- `KernelFPU = XSAVE` (not FXSAVE).
- `KernelXSaveFeatureSet = 7` (bits 0 FPU + 1 SSE + **2 AVX**; default is `3` = FPU+SSE only).
- `KernelXSaveSize = 832` (seL4's own default when feature-set==7; **576** is the FPU+SSE-only size, **512** is FXSAVE).
- XSAVE variant the 2700X (Zen+) supports: `XSAVEOPT` or `XSAVEC` (CPUID-guarded; kernel refuses to boot on an unsupported pick). 2700X has AVX2 but **not** AVX-512 → target feature-set 7 only.
- `Arch_initFpu()` programs `XCR0 = feature_set` and **fails boot** if `CPUID(0x0d,0).EBX > CONFIG_XSAVE_SIZE` (prints `"XSAVE buffer set to N, but needs to be at least M"`). A correct AVX config boots clean with XCR0 bit 2 set.

Citations: RFC-0180 (FPU switching, lazy default + per-TCB `fpuDisabled` flag) <https://sel4.github.io/rfcs/implemented/0180-fpu-switching.html>; `config.cmake` (`KernelXSaveFeatureSet` DEFAULT 3; size 832 if set==7 else 576) <https://github.com/seL4/seL4/blob/master/src/arch/x86/config.cmake>; `machine/fpu.c` (`write_xcr0`, size check) <https://github.com/seL4/seL4/blob/master/src/arch/x86/machine/fpu.c>; configurations doc <https://docs.sel4.systems/projects/sel4/configurations.html>.

> **Testing caveat (critical):** the corruption only manifests under **preemption/context-switch**, so a single-threaded, non-preempting QEMU smoke test can pass even on a wrong kernel. Worse, the committed QEMU sim CPU is **Nehalem with xsave disabled** → AVX2 instructions `#UD` there regardless. **AVX2 correctness must be exercised on KVM `-cpu host` (exposes 2700X AVX2 + xsave) or on bare metal**, with a preempting workload — not the TCG Nehalem sim.

### Q2 — Is the build SMP? **VERIFIED: NO — uniprocessor.**

```
jbuild/kernel/gen_config/kernel/gen_config.h:
  #define CONFIG_MAX_NUM_NODES  1
  /* disabled: CONFIG_ENABLE_SMP_SUPPORT */
  /* disabled: CONFIG_KERNEL_MCS */
jbuild/CMakeCache.txt:  KernelMaxNumNodes:STRING=1   SMP:BOOL=OFF
run_jarvis.sh / jbuild simulate:  no -smp flag (1 vCPU)
```

A threadpool gives **zero** throughput on a uniprocessor — N worker TCBs just time-slice one core; for CPU-bound matmul that adds overhead, not speedup. (This confirms the PA/PB-at-MaxPrio + `seL4_Yield` cooperative design is single-core. The 19.79 tok/s 16T number in CLAUDE.md is the **native/host** bench, not seL4.)

**Cost of uniprocessor → SMP** (seL4 FAQ + manual `threads.tex` + box kernel src): mostly config + modest rootserver work, **not** a rewrite, if we stick to the big-kernel-lock + manual affinity:
- Config: `-DSMP=ON -DNUM_NODES=N` (drives `KernelMaxNumNodes`; default 4, hard cap **8** per `compile_assert(... <= 8)`). 2700X = 8C/16T.
- `seL4_TCB_SetAffinity(tcb, core)` per worker (the whole affinity API is `#ifdef ENABLE_SMP_SUPPORT` — absent today). Pin PA/dispatcher to core 0, workers to 1..N-1; keep equal priority so `seL4_Yield` works.
- **Untyped is global** (no per-core pool rework). Cross-core IPC works but takes the BKL + IPIs → prefer shared-mem atomics over per-tile IPC.
- **Main correctness risk:** the BKL protects the **kernel**, not our userspace. VKA/allocman and musl `morecore`/`malloc` are **not thread-safe** → pre-allocate all per-worker buffers up front, never `malloc` in worker hot loops; audit IPC ring cursors, decision cache, NVMe log for cross-thread races.
- **Forfeits formal verification:** the multicore (BKL) kernel is **not yet verified** ("verification in progress for static multikernel configs", FAQ). This is the headline trade-off and must be recorded in an ADR if pursued.

Citations: FAQ (BKL, "not meant to scale to many cores", multicore not yet verified) <https://sel4.systems/About/FAQ.html>; manual threads (`seL4_TCB_SetAffinity`, shared CSpace/VSpace) <https://raw.githubusercontent.com/seL4/seL4/master/manual/parts/threads.tex>; BKL = CLH queue lock `src/smp/lock.c` <https://github.com/seL4/seL4/blob/master/src/smp/lock.c>.

### Engine seams (exact files/functions)

**AVX2 — a pure build-flag flip once the kernel saves YMM** (the SIMD code already exists, `#ifdef __AVX2__`):
- `phase3/src/ai/qdot.c` — `qdot_row` + all `qdot_*_avx2` kernels (Q4_K/Q5_K/Q6_K/Q8_0/Q4_0/BF16/F16; F16 path also wants `__F16C__`, implied by `-mavx2` on Zen+). Zero source change.
- `phase3/src/ai/tensor_ops.c` — `tensor_matmul` N==1 path (`#ifdef __AVX2__`, L40-79).
- `phase3/src/ai/llama_quant.c` — `dot_f32_avx2` / `accum_axpy_f32_avx2` (L219-275) for attention.
- **Where the flag goes:** `~/sel4-x86/projects/jarvis-x86/apps/{sel4test-driver,jarvis-inference}/CMakeLists.txt` `target_compile_options(... -Wall -g -O2)` → add `-mavx2 -mfma` to **both** app targets (PB compiles the same `ai/*.c`); and/or have `phase3/scripts/build_jarvis_x86.sh` inject them. **Today: no `-mavx2/-mfma/-march` anywhere (VERIFIED).**

**Threadpool — a new seL4-native backend behind the existing ABI** (gated on SMP):
- `phase3/src/ai/llama_quant.c::qmatmul_vec` (L106-166) already calls `jarvis_parallel_for(0, M, qmatmul_qdot_row, &ctx, sizeof(ctx))` when `jarvis_threads()>1 && M>=256` under `#ifdef JARVIS_PTHREAD`. This is the row-parallel seam (ctx structs `_Static_assert`'d ≤64B).
- `phase3/src/ai/threadpool.{c,h}` — `threadpool.c` is pthread-only (not in the seL4 source list); `threadpool.h` provides a serial inline stub when `JARVIS_PTHREAD` is undefined (so `jarvis_threads()==1` today). **Write a new seL4 backend** (worker TCBs via `sel4utils_configure_thread_config` sharing PB's CSpace+VSpace, `seL4_TCB_SetAffinity`, atomic `next_idx` work-stealing + one notification/worker for wake, atomic-counter barrier to join) exposing the **same** `jarvis_parallel_for`/`jarvis_threads` ABI — no caller change.
- `phase3/src/sel4/inference_server.c` (PB main loop, L551/605) is where workers are spawned and `jarvis_parallel_for` runs during `qmodel_forward`. Revisit the PA/PB `seL4_MaxPrio` + `seL4_Yield` assumptions (`main_x86.c` L905/1042-1055) so workers don't starve PA's IPC drain.

---

## 2. Plan — M0 → M4

Each milestone: **goal · files · exit criteria · test (+ CI step) · NOW(main-PC/CI) vs JARVIS-PC.** Tests listed here MUST be added with the milestone (CLAUDE.md rule: every new `test_*.c` gets a CI step).

### M0 — Kernel XSAVE/AVX enablement + AVX2 safety spike  *(JARVIS-PC; kernel rebuild)*
The config read proves AVX is **not** saved today, so M0 is **not** a fast confirm — it requires a kernel rebuild first.
- **Goal:** rebuild the seL4 kernel so YMM is context-switched, then prove one AVX2 op in Process B returns correct results with **no fault under preemption**.
- **Files:** `~/sel4-x86` kernel config (`KernelFPU=XSAVE`, `KernelXSaveFeatureSet=7`, `KernelXSaveSize=832`, XSAVEOPT/XSAVEC); `phase3/scripts/build_jarvis_x86.sh` (pass the kernel CMake flags); the QEMU CPU model for AVX testing (KVM `-cpu host`, or a TCG model with AVX2+xsave — the committed Nehalem/`-xsave` line cannot run AVX2).
- **Exit criteria:** kernel boots clean (no `"XSAVE buffer ... at least M"` error), XCR0 bit 2 set; a single AVX2 dot-product in PB matches the scalar result bit-for-bit across a preempting workload (PA + PB + heartbeat traffic).
- **Test + CI:** `phase3/src/ai/test_avx2_correctness.c` — AVX2 `qdot_row` vs `qdot_row_scalar` for all 7 quant types, bit-exact; compiled `-mavx2 -mfma` on the CI runner (Zen/AVX2 host) → **new CI step "AVX2 qdot vs scalar"**. (Host CI proves numeric equivalence; the seL4-preemption safety is the bare-metal exit gate.)
- **NOW vs JARVIS-PC:** correctness test = NOW (CI). Kernel rebuild + preemption safety = JARVIS-PC.

### M1 — Enable `-mavx2 -mfma` in the seL4 app build (single-thread)  *(JARVIS-PC)*  — depends on M0
- **Goal:** light up the existing AVX2 kernels in the deployed build; verify boot + numeric correctness vs scalar + measure tok/s.
- **Files:** `apps/{sel4test-driver,jarvis-inference}/CMakeLists.txt` `target_compile_options` (+`build_jarvis_x86.sh`). No engine `.c` change.
- **Exit criteria:** boots on bare metal; coherent Gemma 4 E2B generation unchanged vs scalar; **tok/s approaches the native single-thread engine — target ~1.7 tok/s Gemma 4 E2B, ~3.2 tok/s Llama 1B, up from ~0.2 scalar.**
- **Test + CI:** reuse M0's correctness test in CI; record a bare-metal tok/s line via the NVMe `IPC_STATS`/bench path (no new CI step — measurement is on-box).
- **NOW vs JARVIS-PC:** JARVIS-PC (build + bench). Correctness already covered by M0 CI.

### M2 — SMP decision point  *(decision; then JARVIS-PC if branch A)*  — **uniprocessor is VERIFIED, so a decision is required**
Present both branches; do not assume:
- **Branch A — enable SMP spike:** rebuild `-DSMP=ON -DNUM_NODES=4` (up to 8); confirm boot + `bootinfo->numNodes==N` + PA/PB + NVMe model load still work; a `seL4_TCB_SetAffinity` smoke test proves a thread runs off core 0 (per-core counter). **Accept the verification-guarantee forfeit (write an ADR).**
- **Branch B — ship AVX2 single-thread for v1.0, defer threading:** if the AVX2-only tok/s (M1) is "fast enough to use daily," declare goal #1 met for v1.0 on the single-thread CPU path and defer the threadpool (+SMP) to a later phase. Records an ADR either way.
- **Exit criteria:** a recorded decision (A or B) with the M1 tok/s number as the input, plus an ADR.
- **Test + CI:** none (decision). If branch A: add a `seL4_TCB_SetAffinity` on-box smoke check (bare-metal only).
- **NOW vs JARVIS-PC:** decision = NOW; SMP spike = JARVIS-PC.

### M3 — seL4-native threadpool  *(JARVIS-PC)*  — **GATED on M2 = branch A**
- **Goal:** replace the pthread stub with a seL4 worker pool backing `jarvis_parallel_for`; parallelize `qmatmul_vec` rows; measure tok/s.
- **Files:** new `phase3/src/ai/threadpool_sel4.c` (worker TCBs via `sel4utils_configure_thread_config` sharing PB CSpace+VSpace, `SetAffinity`, atomic `next_idx` + notification wake, atomic-counter barrier); wire into the seL4 source list with the same `jarvis_parallel_for`/`jarvis_threads` ABI; `inference_server.c` (spawn workers once at startup); audit shared state (VKA/morecore/rings) for races.
- **Exit criteria:** correct generation unchanged; **tok/s scales with workers, approaching native threaded — target ~8–9 tok/s Gemma 4 E2B at up to 16T** (subject to the 8-node cap and memory bandwidth).
- **Test + CI:** `phase3/src/ai/test_parallel_for.c` — `jarvis_parallel_for` over a known reduction returns the same result as the serial path for 1..N workers (host-portable harness, pthread or stub backend) → **new CI step "parallel-for backend"**. On-box: N-worker vs 1-worker tok/s.
- **NOW vs JARVIS-PC:** ABI/correctness test = NOW (CI); the seL4 backend + scaling = JARVIS-PC.

### M4 — End-to-end bare-metal benchmark + Done-when  *(JARVIS-PC)*
- **Goal:** record the reproducible seL4-build inference benchmark and satisfy the reframed ROADMAP goal #1 "Done when" CPU item.
- **Files:** `phase4/docs/` bench result + `phase4/weeks/weekN/` status; tick the ROADMAP "Done when" box.
- **Exit criteria:** AVX2(+threaded if branch A) Gemma 4 E2B tok/s on the 2700X recorded + reproducible (target ≈ native); roadmap goal-#1 CPU "Done when" satisfied.
- **NOW vs JARVIS-PC:** JARVIS-PC.

---

## 3. Risks / open questions

- **AVX2 corruption is preemption-only + sim can't see it.** The committed QEMU sim is Nehalem with xsave disabled → AVX2 `#UD`s there. Must validate on KVM `-cpu host` or bare metal under a preempting workload, else a wrong kernel passes smoke tests and fails intermittently in the field. (VERIFIED config; see §1 Q1.)
- **SMP forfeits the formal-verification guarantee** (multicore kernel not yet verified). Branch A must weigh this; record an ADR.
- **Userspace thread-safety, not the config, is the real M3 work:** VKA/allocman + musl `malloc` are not thread-safe; pre-allocate everything per worker.
- **Memory bandwidth ceiling:** 8 nodes won't give 8× — Q4_K matmul is bandwidth-bound; the native 16T number was on a different (Zen2/5600) host. Treat ~8–9 tok/s as a target, not a guarantee.
- **`KernelX86MicroArch=nehalem`** is the kernel codegen target; user AVX2 is governed by the app `-mavx2` + the XSAVE mask, but confirm the kernel rebuild doesn't need a microarch bump for its own use.
- **Doc reconciliation:** CLAUDE.md's "AVX2+FMA" line describes the **native/CI** engine build; the **seL4** build has no `-mavx2` (VERIFIED). Keep that distinction.

## 4. Exit criteria → ROADMAP goal #1

Maps to `phase4/docs/ROADMAP.md` goal #1 "Done when":
- [ ] **seL4-build inference benchmark recorded + reproducible** — AVX2(+threaded) Gemma 4 E2B tok/s on the 2700X (target ≈ native threaded) ← **M1 (single-thread) and/or M3 (threaded), M4 records it.**
- [ ] **(deferred) GPU inference benchmark** — gated on hardware (ADR 2026-06-16), out of scope here.

**Minimum to satisfy goal #1 for v1.0:** M0 + M1 (AVX2 single-thread) if M2 = branch B; M0 + M1 + M3 if M2 = branch A.
