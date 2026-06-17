# Week 04 Status: Phase 4 Goal #1 — M2 (enable seL4 SMP, Branch A) — DONE, bare-metal validated

**Date:** June 17, 2026
**Status:** M2 COMPLETE — seL4 SMP enabled (`NUM_NODES=2`); multicore foundation validated on KVM **and** real bare metal. Branch A decided (`docs/decisions/2026-06-17-enable-smp-branch-a.md`). **No inference speedup yet — the threadpool is M3.**
**Phase:** Phase 4 — Production (v1.0), goal #1 "Inference performance" (CPU path). Hardware-in-the-loop on the JARVIS PC (Ryzen 7 2700X).

---

## Summary

M2 is a **validation spike**, not a build milestone: enable SMP **minimally** (`-DSMP=ON -DNUM_NODES=2` = BSP + 1 AP), prove the multicore kernel boots with the existing two-process system intact, and **observe how SMP behaves** to feed the M3 threadpool design. It is the first kernel-config change since M0, so it was driven defensively: KVM-gated before flash, with the M1 single-core image as a captured rollback.

**Result: the SMP kernel boots and the deployed system is unaffected** (everything still runs on node 0 — see E1), at **zero single-thread cost**. The second core is a *dormant, regression-free foundation* the M3 threadpool will light up via explicit affinity.

## Result — M2 spike (verified)

| Check | KVM (`-smp 2 -cpu host`) | Bare metal (real 2700X, NVMe log boot_id=1) |
|-------|--------------------------|---------------------------------------------|
| Both nodes boot | ✅ `node #0 APIC 0` + **`node #1 APIC 1`** → "dropped to user space" | ✅ (kernel boot + `SMP numNodes=2` log) |
| `bootinfo->numNodes` | **2** (`[SMP] numNodes=2 nodeID=0 apic=0`) | **2** (durable NVMe `SMP numNodes=2 nodeID=0 apic=0`) |
| Self-test | **5/5 PASS** | **5/5 PASS** |
| Process B + IPC under SMP | ✅ spawn + shmem mapped both sides + ring **ready-handshake** | ✅ `Process B ready` + full workload |
| Model load (Gemma 4 E2B) | n/a (model-less KVM) | `MODEL_LOAD "GGUF loaded OK"` |
| Coherent generation | n/a | **14 coherent inferences** (`LOG_INFER` snippets) |
| Stability | no fault/panic/deadlock; model-less PB timeouts are expected | `IPC_STATS q=100 err=0` |
| Decode tok/s | — | **~1.53 tok/s** (gen=50, cyc≈121.0e9, TSC 3.6999970 GHz) |

**No SMP penalty on the 1T path:** ~1.53 tok/s under SMP is within **~0.2%** of M1's single-core ~1.53 — the big-kernel-lock overhead is negligible because PA and PB still run on one core (E1).

**`boot_id=1`, not `2` — intentional:** the operator cleared the telemetry log (`clear_nvme_log.sh`, archived first to `~/nvme_log_archive_boot1.{bin,txt}`) before this boot, so the run wrote a fresh `boot_id=1`. This is **expected, not a durability regression** — the 2026-06-15 burn-in already proved the cursor/boot_id persist across reboots; clearing simply resets them by design.

## SMP enablement (reproducible by construction)

- `-DSMP=ON -DNUM_NODES=2` added to the pass-1 kernel cmake in `build_jarvis_x86.sh`. **Stickiness handled:** `jarvis-x86/settings.cmake` reads the `SMP`/`NUM_NODES` cache vars and FORCE-sets `KernelMaxNumNodes` from them, so `-DKernelMaxNumNodes` alone would be overridden — the supported lever is `SMP`/`NUM_NODES` (the same class of trap as the IOMMU force at M1).
- **Config-verification gate** (new, in `build_jarvis_x86.sh`): after `ninja`, the build greps the kernel `gen_config.h` and **aborts** unless all hold — `CONFIG_MAX_NUM_NODES=2`, `CONFIG_ENABLE_SMP_SUPPORT=1`, `CONFIG_XSAVE=1`, XSAVE feature-set 7, FXSAVE disabled, IOMMU disabled, `FASTPATH=1`. This makes the M2 SMP config + the preserved M0/M1 invariants **self-checking on every build** — a silent wrong-config build (the exact IOMMU/XSAVE trap) now fails loudly.
- Verified this week: the gate printed **all-OK** on both the instrumented spike build and the clean deployment build.

## Probe (compile-guarded, default off)

- `phase3/src/sel4/smp_probe.h` (NEW) — `smp_apic_id()` (CPUID leaf 1 EBX[31:24], unprivileged/Ring-3-legal). Included only under `#if JARVIS_SMP_PROBE`.
- `JARVIS_SMP_PROBE` (NEW in `jarvis_debug.h`, default **0**): Process A logs `SMP numNodes=N nodeID=M apic=K` to serial (NVMe-independent, so it shows on a model-less KVM boot) **and** durably to the NVMe log when present; Process B prints its APIC id at startup. The spike build set it **=1**; the **deployed image was rebuilt with it =0** (all stability flags at committed defaults). Source stays in-tree, guarded — not deleted (bare-metal debug rule).

## EXPLORE — SMP behavior recon (M3 threadpool design inputs)

These were captured while SMP was up; they do **not** gate M2 but are the inputs that make the M3 design concrete.

- **E1 — seL4 does NOT auto-distribute (FIRST design constraint).** Under SMP with no affinity set, **PA `apic=0` AND PB `apic=0`** — both on core 0; the AP is idle (QEMU ≈100%, one core). A spawned process inherits its creator's (node 0) affinity. ⇒ **M3 workers MUST call `seL4_TCB_SetAffinity(tcb, core)`** to run off core 0; the scheduler will not place them for us.
- **E2 — `seL4_Yield` busy-spins when alone on a core.** With PA the only runnable thread on its core, its end-of-loop `seL4_Yield` is a near-noop and PA spins the core at 100%. ⇒ **M3 workers must BLOCK on a notification when idle (`seL4_Wait`), not busy-spin** — else N idle workers pin N cores and starve nothing useful.
- **E3 — cross-core IPC not yet exercised (consequence of E1).** Because both PA and PB sit on node 0, the PA↔PB notification+ring path at M2 is **intra-core**, identical to M1 (hence the 0% SMP penalty). Genuine cross-core IPC latency (BKL + IPI) only appears once M3 places workers on core 1 — to be measured then. Honest status: **M2 proves SMP is regression-free, not that cross-core IPC is fast.**
- **E4 — BKL = CLH queue lock; every syscall serializes.** Confirmed in `kernel/src/arch/x86/kernel/boot_sys.c` (`clh_lock_init()` + `NODE_LOCK_SYS` before APs boot) and `src/smp/lock.c`. Every kernel entry on every core takes the one big lock. ⇒ **M3's parallel-for hot path must minimize syscalls** — prefer shared-memory atomics (`next_idx` work-stealing) + **one** wake notification per worker, **not** per-tile IPC. Syscall-heavy fan-out would serialize on the BKL and erase the SMP win.
- **E5 — core enumeration.** ACPI MADT reports `apic_id=0x0` and `0x1`; the kernel boots `node #0` (APIC 0) and `node #1` (APIC 1). With `KernelMaxNumNodes=2` the kernel takes the first 2 firmware-enumerated APIC ids. **For M3's production N:** target **physical** cores (the 2700X is 8C/16T) — Q4_K matmul is memory-bandwidth-bound, so SMT siblings sharing a core's load/store ports are unlikely to add throughput. Production `NUM_NODES` is an M3 decision after bandwidth testing (spike used 2; cap is 8).
- **E6 — worker-creation recipe.** PB is spawned as a full process via `sel4utils_configure_process` (own CSpace/VSpace). **M3 workers should be threads that SHARE PB's address space** — `sel4utils_configure_thread` (lighter: shares PB's CSpace + VSpace, so they see the model weights and `state` directly), each needing a pre-allocated stack + IPC buffer + priority, then `seL4_TCB_SetAffinity(tcb, core)` and a per-worker notification for wake. `seL4_TCB_SetAffinity` is generated only under `ENABLE_SMP_SUPPORT` — now present.
- **E7 — thread-safety boundary in `qmodel_forward`/`qmatmul_vec`.** The row-parallel seam already exists: `qmatmul_vec` (`llama_quant.c` L106-166) calls `jarvis_parallel_for(0, M, qmatmul_qdot_row, &ctx, sizeof(ctx))` for `M>=256` (ctx `_Static_assert`'d ≤64B, memcpy'd per call). The seL4 build links the **serial stub** of `threadpool.h` today. What becomes concurrent when workers split rows, and the isolation each needs:
  - **`out` (output vector):** each worker writes **disjoint rows** → safe, no locking.
  - **weights (`W`) + input (`x`):** **read-only** in the row fn → safe to share.
  - **`state->fwd_scratch`:** shared scratch used by the **sequential** parts of `qmodel_forward` (conv_k, dn_out, PLE, norm bufs) — *not* inside the parallel row loop today, so single-threaded and safe **as written**. M3 must keep it out of the parallel path, or **partition it per-worker** if any future parallel stage touches it.
  - **no malloc in the hot path:** `qmatmul_vec` uses a stack/pre-set ctx — keep it that way. **VKA/allocman and musl `malloc`/`morecore` are NOT thread-safe** → pre-allocate every per-worker buffer (stack, IPC buffer, scratch) up front; never allocate in a worker hot loop. Audit IPC-ring cursors, the decision cache, and the NVMe-log cursor for cross-thread races before workers touch them.
  - **M3's job is therefore narrow:** provide a thread-safe seL4 backend for the existing `jarvis_parallel_for`/`jarvis_threads` ABI; the callers and the disjoint-row math need no change.

## Files

| File | Change |
|------|--------|
| `phase3/scripts/build_jarvis_x86.sh` | `-DSMP=ON -DNUM_NODES=2` (pass-1 cmake) + post-build **config-verification gate** (SMP on + M0/M1 invariants) + sync `smp_probe.h` to both apps |
| `phase3/src/sel4/smp_probe.h` | **NEW** — `smp_apic_id()` (compile-guarded probe header) |
| `phase3/src/sel4/jarvis_debug.h` | `+ JARVIS_SMP_PROBE` (default 0) |
| `phase3/src/sel4/main_x86.c` | guarded numNodes gate (early serial + durable NVMe `SMP numNodes=…`) |
| `phase3/src/sel4/inference_server.c` | guarded PB APIC-id print (E1) |
| `docs/decisions/2026-06-17-enable-smp-branch-a.md` | **NEW** — Branch A ADR (SMP for M3; maturity, not forfeit; spike result) |
| `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` | M2 DONE (Branch A); E1–E7 folded into the M3 section |
| `CLAUDE.md` | status (M2 done, SMP `NUM_NODES=2`); SMP/verification note; week04 + smp_probe.h pointers |

## Next

- **M3 — seL4-native threadpool** (goal-#1 plan §2, gated on M2 = Branch A, now satisfied). Implement worker TCBs behind the existing `jarvis_parallel_for` ABI per the E1–E7 recipe above; parallelize `qmatmul_vec` rows across cores; target **~5–8 tok/s** Gemma 4 E2B (stretch ~8–9 at higher N, bandwidth permitting). Add `phase3/src/ai/test_parallel_for.c` + a CI step. Production `NUM_NODES` decided here after bandwidth testing.

---

*Phase 4 cadence: weekly status at `phase4/weeks/weekN/WEEK_N_STATUS.md`.*
