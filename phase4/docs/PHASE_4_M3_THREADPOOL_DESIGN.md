# Phase 4 — M3: seL4-native Threadpool Design

**Status:** ✅ **IMPLEMENTED (M3 done 2026-06-18)** — Gemma 4 E2B **5.46 tok/s @ `NUM_NODES=6`** bare metal (**3.57×** the 1.53 single-thread). The design below is the as-built spec; deltas discovered during implementation are recorded in **§Implemented** immediately below.
**Date:** 2026-06-17 (design) · 2026-06-18 (implemented)
**Scope:** the worker pool that backs `jarvis_parallel_for` in Process B so `qmatmul_vec` rows run across cores.
**API verified read-only against the box** (seL4 14.0.0 `~/sel4-x86`) — see **§Verified API**. Where the box differs from the design paraphrase, the box wins; corrections are flagged ⚠️.

---

## Implemented (M3 — 2026-06-18)

Built as designed (PA creates the workers in PB's VSpace/CSpace; PB drives dispatch), with these implementation facts:

- **Worker-entry resolution (as designed):** PA walks PB's **ET_EXEC, unstripped `.symtab`** (`resolve_pb_symbol` in `main_x86.c`) to get `jarvis_sel4_worker_entry`'s runtime vaddr — libelf has no by-name lookup, and PA never links the threadpool (PB-only `JARVIS_SEL4_SMP`). Confirmed: `[JARVIS] M3: started 5 workers, pb_n_threads=6`.
- **The seL4 join is NOT a verbatim port of the pthread backend.** It ports the **work-stealing CORE** (atomic `next_idx`) but replaces the join entirely: a **generation-counter publish (release/acquire)** + **per-worker one-shot wake Notifications** + an **active-counter join** (last worker signals `done`, dispatcher blocks on `seL4_Wait(done)`). This is structurally immune to the pthread cross-dispatch over-decrement race that M3 step 1 fixed (`docs`: `test_parallel_for.c`).
- **Two bugs found + fixed during bring-up (both would have been box-only failures):**
  1. **Unchecked `sel4utils_copy_cap_to_process` (returns 0 on failure)** → a null `done`/`wake` cap propagated to PB would `seL4_Signal(0)`/`seL4_Wait(0)` and **deadlock the first dispatch**. Caught by the **pre-build adversarial review**; fixed by checking both copies and degrading to serial.
  2. **Worker started outside `sel4runtime`'s per-thread init** → its libsel4 syscall-stub IPC-buffer TLS pointer (`__sel4_ipc_buffer`) was unset, so the worker's first `seL4_Wait` faulted (`vm fault @ 0x8`) and the dispatch deadlocked. Caught by the **QEMU boot smoke** (static review can't see it); fixed by `seL4_SetIPCBuffer((seL4_IPCBuffer *)ipc_buf)` (the 3rd `sel4utils_start_thread` entry arg) before any syscall.
- **Correctness proof (greedy decoding is deterministic):** a **serial control** (N=1, `n_workers=0`) vs the **parallel** run (N=2) was **byte-identical for every prompt**. Even the one garbage output (`"The seL4 microkernel is" → ï¿½ï¿½ï¿½`) is **identical serial and parallel** → a pre-existing Llama-1B byte-token artifact, **not** a parallel bug.
- **N-sweep (hybrid):**
  - **F1 — QEMU-KVM, Llama-1B (relative scaling, 0 faults/timeouts each):** NN=2 → 4.97, NN=4 → 8.95, NN=5 → 10.80, NN=6 → 12.54 tok/s. Still climbing at 6 — Llama-1B (~770 MB/token) is compute-bound here (ceiling ~54 tok/s), so its knee doesn't transfer to Gemma.
  - **F2 — bare-metal, deployed Gemma 4 E2B (RDTSC decode, TSC 3.6999970 GHz):** **NN=4 → 4.21 tok/s** (2.75×), **NN=6 → 5.46 tok/s** (3.57×); 4→6 marginal **+30%** (efficiency 1.05 → 0.91 tok/s/thread) = the Gemma bandwidth knee (~2.9 GB/token) appearing. **`err=0` across q=100→800** at NN=6.
- **Chosen production `NUM_NODES = 6`** (best measured, in the ~4–6 target, 6 of 8 physical cores). NN=8 not measured (diminishing returns). Build default + config-gate set to 6.

---

## Goal & scope

A seL4-native worker pool backing the **existing** `jarvis_parallel_for(start, end, fn, ctx, ctx_size)` / `jarvis_threads()` ABI (`phase3/src/ai/threadpool.h`) — **no caller changes**. It parallelizes the `qmatmul_vec` row loop in Process B and replaces the serial inline stub under a build define (`JARVIS_SEL4_SMP`). It **ports the native `threadpool.c` work-stealing algorithm** and swaps exactly two primitives: **thread creation** (pthreads → seL4 TCBs) and **synchronization** (pthread cond/mutex → seL4 Notifications + C11 atomics).

- **Baseline:** ~1.53 tok/s single-thread AVX2 (M1/M2).
- **Target:** **~4–6 tok/s** — the dual-channel DDR4 bandwidth knee at N≈5–6 (see §N + affinity), **not** the earlier ~8–9 stretch (that assumed compute-bound scaling to 8–16T; batch-1 decode is memory-bandwidth-bound).

---

## ⚠️ Reality correction: Process B has no allocator (the load-bearing finding)

The design as drafted said *"pool created at PB startup; all resources pre-allocated from PB's main thread."* **Verified false.** `inference_server.c` (Process B) has **no `vka_t`, `vspace_t`, `simple_t`, or allocman** — it is a minimal process that receives caps via `argv` (notification caps, shmem vaddr, model frames) and runs. It cannot allocate TCBs, stacks, IPC buffers, or notifications itself.

**Process A (the rootserver) owns all allocation:** `main_x86.c` holds `static vka_t vka; static vspace_t vspace; static simple_t simple;` **and a persistent `static sel4utils_process_t inference_process;`** — so after spawning PB, PA still holds PB's process handle, i.e. PB's **VSpace** (`inference_process.vspace`) and **CSpace** (`inference_process.cspace` / cnode). PA already uses exactly this machinery to create PB's main thread and to mint PB's notification/frame caps.

**Therefore the resource-ownership model is split:**

| Phase | Owner | Work |
|-------|-------|------|
| **Setup** (PB spawn time) | **Process A** | Allocate N−1 worker TCBs + stacks + IPC buffers **in PB's VSpace**, N−1 wake notifications + 1 done notification (minted into **PB's CSpace**); configure each worker with PB's CSpace root + VSpace, priority, and **affinity (core i)**; `sel4utils_start_thread` each at the worker-entry function (compiled into PB's binary). Pass the wake/done cptrs + worker count to PB via extended `argv`. |
| **Runtime** (inference) | **Process B** | The pool struct is a **global in PB's BSS** (same VSpace → visible to PB's main thread *and* all workers). PB's main thread = dispatcher; dispatch via atomics + `seL4_Signal(wake)` / `seL4_Wait(done)`. **No allocation at runtime.** |

This preserves the design's runtime protocol unchanged (dispatcher + workers, atomics, block-don't-spin) — only **creation moves from PB to PA**. The "pre-allocate everything, no malloc/VKA in the hot path" rule is *strengthened*: PB literally cannot allocate, so it can't violate it.

*Alternative considered & rejected:* PB bootstraps its own allocator from untypeds passed by PA (PB has no `simple_t`, would need an allocman + vspace bootstrap). More code and a second allocator to keep race-free; PA already has everything and already creates PB's threads — extending it to N more is the smaller, safer change.

---

## Research foundation (M3 research synthesis)

- **Worker recipe:** `sel4utils_configure_thread[_config]` gives each worker its own TCB + stack + IPC buffer; workers **share the parent's CSpace + VSpace** (pass PB's CNode as cspace root + PB's VSpace root). Threads start at **priority 0** → priority must be set (here: at config time via `sched_params`, by PA which holds the authority TCB). Pre-allocate everything (VKA/allocman/musl `malloc` are **not** thread-safe — and PB can't allocate anyway).
- **Sync:** C11 `_Atomic` on shared memory works **cross-core** on this kernel (the seL4 SMP test uses an `_Atomic` counter across nodes). Wake/join via **Notifications** with workers **BLOCKING** (`seL4_Wait`) — **not** `seL4_Yield`-spinning (many Yielders cause CLH-queue stalls under the BKL). The BKL adds only ~65 cyc/syscall (negligible per dispatch).
- **Bandwidth:** batch-1 decode is **memory-bandwidth-bound** — it streams ~2.9 GB of Q4_K weights per token. Dual-channel DDR4 saturates around **~5 threads**; exceeding **physical** cores hurts, and SMT siblings (sharing a core's load/store ports) don't add bandwidth. ⇒ **N ≈ 5–6, not 8**; CCX/L3 placement is second-order (the working set streams from DRAM, ≫ L3).

---

## Architecture

- The pool is created **once** — at PB spawn time by **PA** (after PA has loaded the model frames and configured PB, before PB enters its IPC loop). PB's **main thread = dispatcher + "worker 0"** on core 0; workers `1..N−1` are pinned to distinct **physical** cores `1..N−1`.
- **All seL4 resources** (TCBs, stacks, IPC buffers, notifications) are allocated by PA up front. Nothing is allocated on the inference hot path.
- The shared **pool struct lives in PB's address space** (a single BSS global in `threadpool_sel4.c`, linked into the `jarvis-inference` app). PB's main thread and every worker run in PB's VSpace, so they all see it directly. Worker bootstrap args (worker index + that worker's `wake` cptr) are passed by PA via `sel4utils_start_thread(arg0,arg1,…)`; everything else (fn/end/ctx/`done`/atomics) is read from the global after the first wake.

---

## Data structures (illustrative — adjust to the Verified API)

```c
/* Lives in PB's BSS (threadpool_sel4.c). N-1 worker threads + PB's main = N total. */
typedef struct {
    /* --- per-dispatch work descriptor (published under `gen`) --- */
    _Atomic int      next_idx;     /* work-stealing cursor: fetch_add(1, relaxed)        */
    int              end;          /* row count for this dispatch                        */
    jarvis_parallel_fn fn;         /* the row body (qmatmul_qdot_row)                    */
    char             ctx_buf[64];  /* caller ctx, memcpy'd (matches the ABI's 64B cap)   */
    _Atomic int      active;       /* workers still running this dispatch                */
    _Atomic unsigned gen;          /* release/acquire handshake publishing fn/end/ctx    */

    /* --- pre-allocated by PA, immutable after setup --- */
    sel4utils_thread_t worker[MAX_WORKERS];  /* index 1..N-1 (0 = PB main, not a TCB)    */
    seL4_CPtr        wake[MAX_WORKERS];       /* per-worker wake notification (in PB CSpace)*/
    seL4_CPtr        done;                    /* join notification (in PB CSpace)         */
    int              n_threads;               /* N (incl. PB main); jarvis_threads()      */
    volatile int     shutting_down;
} sel4_threadpool_t;
```

`MAX_WORKERS` is a small compile constant (≥ the 8-node cap). `worker[i]`/`wake[i]` for `i` in `1..N-1`; the dispatcher (PB main) is "worker 0" and owns no TCB/wake.

---

## Dispatch / join protocol — **PRIMARY = Design A (block, never spin)**

`jarvis_parallel_for(start, end, fn, ctx, ctx_size)`:
1. **if** `n_threads <= 1` **or** `(end - start) < 256` (match the native threshold in `qmatmul_vec`) → run serial inline, return.
2. `memcpy(ctx → ctx_buf)`; set `fn`, `end`; `atomic_store(next_idx, start, relaxed)`; `atomic_store(active, n_workers /*=N-1*/, relaxed)`; `atomic_store_explicit(gen, gen+1, release)` — **publish** the descriptor.
3. `seL4_Signal(wake[i])` for each worker `i` in `1..N-1` (N−1 BKL-cheap syscalls).
4. **caller runs the work-stealing loop too:** `for(;;){ int i = atomic_fetch_add(&next_idx,1,relaxed); if (i >= end) break; fn(i, ctx_buf); }`
5. `seL4_Wait(done, NULL)` — **block** until the last worker finishes (no spin).

Worker main loop (each worker `me` in `1..N-1`, created/pinned by PA):
```c
for (;;) {
    seL4_Wait(wake[me], NULL);              /* block until dispatched                    */
    if (shutting_down) seL4_TCB_Suspend(self) / return;
    atomic_load_explicit(&gen, acquire);    /* pair with the dispatcher's release        */
    for (;;) { int i = atomic_fetch_add(&next_idx,1,relaxed); if (i>=end) break; fn(i, ctx_buf); }
    if (atomic_fetch_sub(&active,1,acq_rel) == 1) seL4_Signal(done);  /* last one joins   */
}
```

- **Memory ordering:** dispatcher **release** on `gen` *after* writing `fn`/`end`/`ctx_buf`; worker **acquire** on `gen` *after* the `Wait`. x86 TSO + the kernel round-trip (Signal→Wait is a full barrier) make this safe in practice; keep the explicit release/acquire for correctness and portability of the algorithm.
- **FALLBACK — Design B (escape hatch, only if M3 bench shows per-dispatch wake overhead dominates):** keep workers "hot" across a whole forward pass — workers spin briefly on `gen` (bounded) before blocking, so back-to-back dispatches skip the wake syscall. **Default to A**; switch to B only if the N-sweep shows A's per-dispatch overhead is not in the noise. *Sanity:* ~200 `qmatmul` dispatches/token × (N−1) cross-core `Signal`s is negligible vs ~2.4e9 cyc/token — **but the IPI/reschedule cost per cross-core wake is unquantified**, so the N-sweep bench is the gate, not this estimate.

---

## Thread-safety contract

- **Parallel body = `qmatmul_qdot_row`:** each invocation writes a **disjoint** `out` row and reads the weight row `W[i]` + input `x` (both **read-only**) → safe with no locking. (Verified seam: `llama_quant.c` `qmatmul_vec` already calls `jarvis_parallel_for(0, M, qmatmul_qdot_row, &ctx, sizeof(ctx))` for `M >= 256`; ctx is `_Static_assert`'d ≤ 64 B.)
- `ctx_buf` is **read-only** for the duration of a dispatch; `next_idx`/`active`/`gen` are the only mutable shared state and are all `_Atomic`.
- **No `malloc`/VKA/vspace calls in the parallel path** — all resources pre-allocated by PA (PB cannot allocate; see the reality-correction §).
- `state->fwd_scratch` is **not** touched by the parallel body (it is used only by the *sequential* parts of `qmodel_forward`). Confirm at implementation time; **if any per-row temporary is ever needed, it must be per-worker** (sized `MAX_WORKERS`), never shared.

---

## N + affinity

- **N = min(physical cores, bandwidth knee); default target N ≈ 5–6.** The 2700X is 8C/16T; the dual-channel DDR4 knee (~5 threads) — not the core count — caps useful N. `jarvis_threads()` returns N (env `JARVIS_THREADS` override retained for sweeps).
- **Pin worker `i` → physical core `i`** (distinct cores `1..N−1`); dispatcher = core 0. No CCX special-casing — the per-token weight stream (~2.9 GB) dwarfs L3, so cross-CCX placement is second-order.
- **Affinity mapping evidence (M2 boot):** ACPI MADT enumerated `apic_id=0x0,0x1`; the SMP kernel booted `node #0 (APIC 0)` + `node #1 (APIC 1)`. With `KernelMaxNumNodes=2` only 2 nodes exist today — **M3's first build must raise `NUM_NODES` to the chosen N** (re-using the M2 `build_jarvis_x86.sh` config-verification gate, updated to assert the new N). seL4 node ↔ physical-core mapping for N>2 is taken from the firmware MADT order and must be confirmed on the box during the N-sweep.

---

## Build integration

- New `phase3/src/ai/threadpool_sel4.c` implements `jarvis_parallel_for` / `jarvis_threads` under `#ifdef JARVIS_SEL4_SMP`, **replacing** the serial inline stub in `threadpool.h` for the seL4 build. The host/CI build keeps the pthread `threadpool.c` (M3 changes nothing there).
- Wired into `build_jarvis_x86.sh`'s **Process B** source list (the `jarvis-inference` app), with `-DJARVIS_SEL4_SMP` on the PB target; raise `-DNUM_NODES=<N>` and extend the config-verification gate to assert it.
- **PA-side spawn change** (`main_x86.c` `spawn_inference_process`): after `sel4utils_configure_process_custom` + model-frame mapping, allocate + configure + start the N−1 workers in `inference_process.vspace`/`.cspace`, mint the wake/done notifications into PB's CSpace, and append their cptrs + N to PB's `argv`.
- **PB-side runtime** (`inference_server.c`): parse the new argv (wake/done cptrs, N) into the pool global before the IPC loop; the worker-entry function lives in `threadpool_sel4.c`.
- **ABI unchanged** → `qmatmul_vec` and every other caller are untouched.

---

## Verified API (box, seL4 14.0.0 `~/sel4-x86`, 2026-06-17)

Recorded verbatim from the tree; corrections vs the design paraphrase flagged ⚠️.

**Thread creation — `libsel4utils/include/sel4utils/thread.h`:**
```c
/* :73 — explicit-cspace form */
int sel4utils_configure_thread(vka_t *vka, vspace_t *parent, vspace_t *alloc, seL4_CPtr fault_endpoint,
                               seL4_CNode cspace, seL4_Word cspace_root_data, /* … */ sel4utils_thread_t *res);
/* :80 — config-struct form (preferred: carries priority + affinity via sched_params) */
int sel4utils_configure_thread_config(vka_t *vka, vspace_t *parent, vspace_t *alloc,
                                      sel4utils_thread_config_t config, sel4utils_thread_t *res);
/* :127 */
int sel4utils_start_thread(sel4utils_thread_t *thread, sel4utils_thread_entry_fn entry_point,
                           void *arg0, void *arg1, int resume);
/* :217 */
int sel4utils_set_sched_affinity(sel4utils_thread_t *thread, sched_params_t params);
```
- `parent` = the VSpace the thread **runs in** (= PB's VSpace); `alloc` = the VSpace to allocate the stack/IPC buffer from (= PB's VSpace, so the stack is reachable by the worker). `vka` = **PA's** vka (the allocator). `cspace` = PB's CNode (shared CSpace).

**`sel4utils_thread_t` struct (`thread.h:33`):**
```c
typedef struct sel4utils_thread {
    vka_object_t tcb;            vka_object_t sched_context;
    void *stack_top;            void *initial_stack_pointer;   size_t stack_size;
    seL4_CPtr ipc_buffer;       seL4_Word ipc_buffer_addr;
    bool own_sc;                bool own_reply;                vka_object_t reply;
} sel4utils_thread_t;
```

**Affinity / priority — `libsel4utils/include/sel4utils/thread_config.h`:**
```c
typedef struct sched_params { uint8_t priority; uint8_t mcp; seL4_CPtr auth; bool create_sc;
    seL4_CPtr sched_ctrl; seL4_Time period, budget; seL4_Word extra_refills, badge;
    seL4_CPtr sched_context; seL4_Word core; /* <- affinity */ } sched_params_t;   /* :21 */

static inline sched_params_t sched_params_core(sched_params_t params, seL4_Word core);  /* :86 */
```
> ⚠️ **Correction:** the design's `sched_params_set_core` does **not** exist — the real setter is **`sched_params_core(params, core)`** (sets `.core`). Apply it via `sel4utils_set_sched_affinity(thread, params)`, or set affinity directly with `seL4_TCB_SetAffinity` (below).

`sel4utils_thread_config_t` builders (fluent, `thread_config.h`): `thread_config_new(simple)`, `thread_config_cspace(cfg, cspace_root, data)`, `thread_config_auth(cfg, tcb)`, `thread_config_priority(cfg, prio)`, `thread_config_fault_endpoint(cfg, ep)`, `thread_config_stack_size(cfg, n)`, `thread_config_no_ipc_buffer(cfg)`, `thread_config_default(simple, cnode, data, fault_ep, prio)`.

**Kernel invocations — generated `jbuild/libsel4/include/interfaces/sel4_client.h`:**
```c
seL4_Error seL4_TCB_SetPriority(seL4_TCB _service, seL4_TCB authority, seL4_Word priority);  /* :3045 — 3 args */
seL4_Error seL4_TCB_SetAffinity(seL4_TCB _service, seL4_Word affinity);                      /* :3750 — 2 args */
```
> ⚠️ **Note:** `SetPriority` takes an **authority** TCB (the TCB whose MCP authorizes the new priority). Since **PA** creates the workers, the authority is PA's own TCB (`simple_get_tcb(&simple)` / `seL4_CapInitThreadTCB`). New threads start at priority 0, so this (or `thread_config_priority` at config time) is **required** or workers never run.

**Notification — `libsel4vka/include/vka/object.h:171`:**
```c
static inline int vka_alloc_notification(vka_t *vka, vka_object_t *result);  /* result->cptr = the Notification cap */
```

**Kernel config confirmed (M2 build, `jbuild/.../gen_config.h`):** `CONFIG_ENABLE_SMP_SUPPORT 1`, `CONFIG_MAX_NUM_NODES 2`, **`CONFIG_KERNEL_MCS` disabled** → **non-MCS**: no scheduling contexts (`create_sc = false`); only `priority` + `core` of `sched_params` are relevant; `seL4_TCB_SetAffinity` / `seL4_TCB_SetPriority` are the direct levers.

---

## Test plan

- **HOST / CI:** `phase3/src/ai/test_parallel_for.c` — `jarvis_parallel_for` over a known reduction must equal the serial result for `1..N` workers, using the **pthread** backend on the CI host (the seL4 backend isn't CI-buildable). **New CI step "parallel-for backend".** This validates the **ABI + work-stealing correctness** (the algorithm is shared; only the primitives differ on seL4).
- **ON-BOX (the M3-implementation milestone, not this doc):** N-sweep bench at **N = 1 / 2 / 4 / 5 / 6** → record tok/s, pick the knee; at each N confirm **coherent generation is byte-for-byte unchanged** (correctness) and `IPC_STATS err=0`. The generation-unchanged check is also the memory-ordering regression gate.

---

## Failure modes & rollback

- **Worker fault** → configured fault endpoint observed by PB (the dispatcher); log it and **degrade to serial** for the rest of the run.
- **Rollback** = rebuild **without** `JARVIS_SEL4_SMP` (and `NUM_NODES` back to a single worker / serial stub). The M1/M2 single-thread serial path (~1.53 tok/s) is the always-available fallback; the M2 SMP kernel itself stays (it imposes no 1T penalty).
- **Memory-ordering bug** → caught by the generation-unchanged correctness check in the on-box N-sweep (and by the host `test_parallel_for` for the algorithm).

---

## Open risks

- **IPI / reschedule cost per cross-core wake is unquantified** (research found no figure). The N-sweep bench is the gate: if Design A's per-dispatch overhead is not in the noise, switch to Design B (hot workers).
- **seL4 Notification memory-ordering guarantees** vs the explicit `gen` release/acquire handshake — keep the handshake regardless (don't rely on Signal/Wait as the only barrier).
- **PA-side worker creation is new code in the rootserver** (the reality-correction §): it must allocate into PB's VSpace/CSpace correctly and **before** PB's IPC loop starts; a mis-mapped stack or cspace fault would surface as a worker fault at first dispatch — covered by the fault-endpoint degrade path, but adds rootserver surface to review.
- **N>2 node↔core mapping** is firmware/MADT-dependent and only validated to N=2 today; confirm on the box during the sweep.
