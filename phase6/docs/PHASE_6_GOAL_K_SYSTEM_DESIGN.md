# Phase 6 — Goal K / M2 System Design: PB-restart (self-healing) via reuse-in-place

**Status:** 🚧 IN PROGRESS — design authored 2026-07-05 (5-agent code sweep, HEAD `4d77a7e`; citations FRESH); **K/M2a spike RUN 2026-07-05 (§4.1)** — the reuse-in-place DIRECTION is locked, the respawn MECHANIC refined (a `spawn_process_v` re-call aborts in muslc's non-re-entrant per-process init → a `pb_restart_entry` muslc-safe re-entry is required; a follow-up **K/M2a-2** spike + K/M2b implement it). The §3 hoist refactor landed (OFF-verified). No shipped behavior change (`JARVIS_ACTIONS` default-0).
**Prereqs:** the it-acts spine (K/M0 host core + K/M1 linked SHIELD gate, box-verified) — `action_allowlist` / `shield_action` / `action_audit` are compiled into PA and the `ACTION_RESTART_PB` id + `shield_assess` are exercised (probe-gated) today. B1 (self-healing PB restart, `ROADMAP.md` §Backlog) folds into this milestone.
**Mirrors:** `phase4/docs/PHASE_4_M3_THREADPOOL_DESIGN.md`, `phase5/docs/PHASE_5_GOAL6_SYSTEM_DESIGN.md`.

---

## 1. Scope / mission

K/M2 is **the first EXECUTED butler action**: Process A detects that Process B has **faulted** (crashed) or **hung**, `shield_assess`es `ACTION_RESTART_PB`, and — if the trust policy authorizes it — **respawns PB** and resumes inference, writing a durable JACT audit record and bumping `restart_count`. It folds backlog **B1** (self-healing) into the keystone: the mechanism that survives a PB crash without a reboot, which Phase 7's "0 crashes over 30 days" needs but no goal otherwise builds.

Scope (from `PHASE_6_GOAL_K_IT_ACTS.md` K/M2, §6): PA detects → `shield_assess(ACTION_RESTART_PB)` (the spine, already landed K/M0–M1) → **reuse-in-place respawn** → audit (verdict EXECUTED, outcome OK|FAIL) + `restart_count++`. Blast-radius bound (§8, verified): **PB holds NO durable state** — episodic / semantic / SCTX / decision-cache / action-audit stores are all PA-side globals; PB re-derives everything from `argv` + already-mapped frames (PA builds argv `main_x86.c:1424-1440`; PB parses `inference_server.c:419-434,467-470,676-682`). So the worst case of a respawn is what a power-cycle already does, minus the reboot.

## 2. Governing constraint — the forward-only, leaky allocator makes realloc-respawn UNSAFE

The single most important verified fact: **there is no teardown path anywhere.**

- PA's VSpace is bootstrapped with the **leaky** variant: `sel4utils_bootstrap_vspace_with_bootinfo_leaky(&vspace, …)` (`main_x86.c:984-985`) — it never reclaims bookkeeping.
- The allocman is bootstrapped **once, forward-only** from the bootinfo untypeds (`bootstrap_use_current_simple`, `main_x86.c:975-976`) over a fixed **500-page static pool** (`ALLOCATOR_STATIC_POOL_SIZE`, `main_x86.c:340`) + a **100,000-page virtual pool** (`main_x86.c:342`, ~400 MiB of bookkeeping sized for PB's ~230K frame allocs). **Neither pool is ever reset.**
- A repo-wide grep confirms **ZERO** occurrences of `sel4utils_destroy_process`, `vka_free`, `seL4_CNode_Revoke`, `seL4_CNode_Delete`, or any `seL4_Untyped_Retype`-teardown in `phase3/src/sel4/`. PB is spawned exactly once and never destroyed.

**Conclusion (locked):** a naive re-call of the spawn path (realloc + re-retype) would **leak, per restart, the ~758K Gemma model frames (§3) + PB's ~230K ELF-image frames (`main:337`) + every CSpace slot** and exhaust the untyped watermark after a handful of restarts. **Reuse-in-place is mandatory** — either reset PB's threads/state while keeping its frames/caps/mappings (Strategy A), or add a *dedicated revocable untyped* and the net-new `seL4_CNode_Revoke` primitive (Strategy B). §4 decides between them via a spike.

## 3. Resource preserve-vs-teardown boundary (each row verified + cited)

### PRESERVE — PA-owned, must survive PB death (NEVER free)
| Resource | Real location | Note |
|---|---|---|
| ~758K **model frame caps** `model_frame_caps[]` | decl `main:358` (file-scope static); alloc loop `main:2412-2456` (store `:2434`); PA map `:2460` | The durable model handle. **Re-mapping from these caps is the whole point** — reloading 2.9 GiB from NVMe per restart = seconds of downtime + re-runs the 758K cap-copy guard. `model_local` (`:2460`) is a **stack local** (CORRECTED from the sweep — not a global); PA re-derives the mapping from the caps. |
| **3 shared frames** (req ring / resp ring / SCTX pool) | alloc `main:1242-1249`; PA map `:1251-1265`; PB dup+map `:1267-1292` | ⚠️ **The frame caps are LOCALS of `spawn_inference_process()`** — not retained in any global after it returns. **K/M2b must hoist these caps to file scope** (or a persistent PA-owned struct) to re-map into the new PB. |
| **`req_notif_obj` / `resp_notif_obj`** notification objects | `main:1156-1169` (locals of `spawn_inference_process`) | ⚠️ Only the `.cptr`s escape into main()'s `req_notif`/`resp_notif` (`main:2746`); the OBJECTS have no durable owner. **Hoist to file scope** so a respawn rebinds rather than reallocs (realloc leaks the old ones — §2). |
| PA VSpace + fixed-vaddr globals: `shared_request_ring` (`:352`→`:1261`), `shared_response_ring` (`:353`→`:1262`), `g_sctx` (`:204`→`:1298`), `inference_process` (`:354`), allocator globals `vspace/vka/simple/vspace_data` (`:344-347`) | as cited | File-scope statics — PA-owned, survive by construction. `g_sctx_ready` (`:205`) gates SCTX use. |
| The Phase-5 memory stores + decision cache + `g_action_audit` (`:503`) | PA globals | All PA-side; PB never touches them. The audit-write path (`action_audit.c`, callback-driven over PA's own NVMe bounce) can log even while PB is down (§8). |

### TEARDOWN-OR-RESET — PB-side, dies/resets with PB
| Resource | Real location | Note |
|---|---|---|
| PB **TCB / CNode (`CONFIG_SEL4UTILS_CSPACE_SIZE_BITS`=22) / VSpace+PTs / stack / IPC-buffer / ELF frames** | config `main:1228-1234` (`sel4utils_configure_process_custom` — the *_custom variant; default_simple + auth only) | Owned by the `inference_process` sel4utils_process object — **would** be reaped by `sel4utils_destroy_process` (which does not exist in-tree). Strategy A resets these in place; Strategy B revokes+re-retypes them. |
| The **N-1 M3 worker TCBs / stacks / IPC-buffers** | create loop `main:1380-1416` (inside `#ifdef CONFIG_ENABLE_SMP_SUPPORT`, block `:1355-1419`); worker entry `threadpool_sel4.c:39-64` | ⚠️ **Created MANUALLY into a throwaway local `sel4utils_thread_t wt`** (`:1398`) in PB's VSpace/CSpace — **NOT owned by `inference_process`**, so a plain destroy would NOT reap them. **K/M2b must track each worker** (a persistent `g_pb_workers[JARVIS_MAX_WORKERS]`) to `sel4utils_clean_up_thread` them, else they dangle in the freed VSpace. |
| `done` / `wake[]` worker Notification objects + PB-CSpace copies | alloc `main:1373-1392`; caps via `sel4utils_copy_cap_to_process` | Also PA-allocated locals, outside the process object — free/re-copy on respawn. |
| PB globals: `g_pool` (threadpool_sel4.c static BSS) | `threadpool_sel4.c:33` | Re-zeroed by a fresh ELF load; if a reused mapping is kept (Strategy A), must re-run `jarvis_sel4_pool_init` (`:66`). |

**K/M2b prerequisite refactor (independent of the A-vs-B choice):** hoist the 3 shmem-frame caps + the req/resp notification objects to file scope, and promote the worker `wt` handles into a persistent array. These are locals today purely because spawn ran once; a respawn needs durable handles to them.

## 4. Respawn strategy: A vs B + the deciding spike

**Strategy A — reset-in-place (RECOMMENDED PRIMARY).** Keep PB's entire process object (CNode/VSpace/TCB/workers) + all mappings + all copied caps. Reset step: `seL4_TCB_Suspend` PB-main + every worker → PA re-copies the ELF **writable** segments (`.data`) and zeros `.bss` **into the existing PB ELF frames** (PA already has the CPIO ELF image mapped — it resolves `jarvis_sel4_worker_entry` from PB's `.symtab` today, `main:1345+`) → `seL4_TCB_WriteRegisters(PB, PC=ELF entry, SP=fresh stack top)`. **`seL4_TCB_Resume` is NOT part of this step** — it is deferred to §6 step 9/10, AFTER the rings are re-inited and workers re-created (resuming before that would re-enter `main()` against stale rings). **Draws ZERO new untyped** ⇒ unbounded restarts AND no 758K-cap re-copy. **Open questions (spike):** (i) whether `sel4utils` exposes a re-load-into-existing-frames primitive, or PA must hand-roll the per-segment `memcpy(.data)` / `memset(.bss)` from the mapped CPIO ELF (PA has the source; the mechanics are the risk); (ii) because PB re-executes `_start`→`main()` at the reset PC, the **argv strings must still be valid at the reset SP** — under A the caps are preserved (no regeneration needed) but argv/stack integrity at re-entry is itself a spike question (see §11 Q3).

**Strategy B — revocable dedicated untyped (FALLBACK).** Pre-carve ONE untyped backing ALL of PB's per-instance objects (CNode/VSpace/TCB/workers), **outside** the model/shared frames. On respawn: `seL4_CNode_Revoke` (net-new — absent from the tree) + re-retype it (resets that untyped's watermark) → re-run `configure_process_custom` + re-map the model + 3 shared frames + rebuild argv. Standard spawn path, but O(pages) re-map cost + re-runs the null-cap-copy guard 758K× + requires proving the revoke of PB's instance untyped **spares** the model/shared frame caps (they must sit in a *different* untyped).

**The spike (K/M2a — runs before design-lock):** a KVM experiment that repeatedly respawns PB **N×** under each strategy and measures **free untyped before/after** (`simple_get_untyped_count` / walking the untyped list). **A passes iff** untyped is FLAT across N restarts AND PB re-inits + generates coherently. **B passes iff** revoke+re-retype resets the dedicated untyped AND the model/shared caps survive the revoke. **Pick the strategy from the spike measurement, not from this prose.**

## 5. Fault vs hang detection (net-new — verified absent today)

Today a PB crash is invisible: **no fault endpoint** is configured (`process_config_default_simple` + `process_config_auth` only, `main:1228-1234`; grep for `fault_endpoint`/`fault_ep` → none), and a crash/hang manifests only as a **5,000,000-iteration poll timeout** that does `q_errors++` + `goto next_query` and **never restarts** — at **TWO** sites: the inline inference lane (`POLL_TIMEOUT` const `main:3122`, handler `:3182-3188`) AND `wait_for_response()` (`main:1464-1466`, used by the heartbeat + shield lanes). `jarvis_uptime_ms()` (`main:119`, TSC-based) exists but **nothing stamps a last-ACK age** (grep `last_hb`/`last_ack`/`heartbeat_age` → none; the only "heartbeat age" hit is the hardcoded probe literal at `main:2238`).

- **(a) FAULT (deterministic).** Add `process_config_fault_endpoint()` to PB's config (`main:1229-1231`) — one badged EP for PB-main and a distinct badge per worker so PA can identify the faulting thread. **PA is a busy-poll loop**, so poll the fault EP **non-blocking** (`seL4_Poll` / a `seL4_NBRecv` on the EP) once per workload iteration. *Decision:* poll-in-the-loop over a dedicated fault-handler thread — a handler thread would need its own priority/affinity + a lock-free flag into the single-writer PA loop, adding concurrency surface for no latency benefit (PA already iterates at high frequency). Justify + confirm in K/M2a.
- **(b) HANG (heuristic).** Stamp `last_hb_ack_ms = jarvis_uptime_ms()` on every `MSG_HEARTBEAT_ACK`. Trigger a restart when the age exceeds a threshold with **margin above the worst-case single-query latency** (~12 s Gemma @ `NUM_NODES=6`): require **N consecutive missed heartbeats spanning > one inference window** so a legitimately BUSY PB (mid-generation, counters frozen — the existing `[STATS]` behavior) is never misread as hung. Both timeout sites feed the same trigger, or a PB hung only on the hb/shield lane won't restart.

The trigger, once fired, calls `shield_assess(ACTION_RESTART_PB, ctx)` → `trust_policy` → execute. **`trust_policy()` is host-tested but never yet called live** (K/M1 is assess-only, "not executed at M1", `main:2246`) — **K/M2 introduces the first live `shield_assess→trust_policy→execute` chain.**

## 4.1 K/M2a spike result (2026-07-05, KVM) — direction LOCKED, mechanic REFINED

**What ran:** a throwaway gated spike (`JARVIS_KM2A_SPIKE`, since reset) exercised the *convenient-primitive* variant of Strategy A — after PB came up (model probed OK, forward pass OK, `[JARVIS] Process B ready type=4 seq=0`), PA suspended PB + workers, re-inited both rings, and **re-invoked `sel4utils_spawn_process_v` on the live `inference_process`** (reusing its CNode/VSpace/TCB/ELF frames/caps).

**Result — it ABORTS on the FIRST respawn cycle, in the C runtime:**
```
[SPIKE] K/M2a reuse-in-place respawn spike START
Assertion failed: ret == boot_set_tid_address
  (.../seL4_libs/libsel4muslcsys/src/vsyscall.c: init_syscall_table: 227)
seL4 root server abort()ed  →  Debug halt syscall from user thread "8"  →  halting
```

**Root cause (the finding neither this design nor the 5-agent sweep anticipated):** muslc's **per-process init** (`init_syscall_table` → `set_tid_address`) is a **ONE-TIME, non-re-entrant** setup whose guard state lives in PB's `.data`/`.bss`. Re-entering PB at `_start` **without resetting those segments** re-runs that init against dirty guards → the assertion. So the naive "re-call `spawn_process_v`" is NOT a valid reset-in-place — it re-enters through the C runtime's init, which cannot run twice.

**§11 Q3 ANSWERED:** reset-in-place CANNOT reuse the naive `spawn_process_v` re-call. It requires **either** (a) re-copying the ELF **writable** segments (`.data` reset + `.bss` zero — which *resets* muslc's init guards so `_start` may safely re-run), **or** (b) a dedicated **`pb_restart_entry`** in `inference_server.c` that re-enters PB **PAST** musl's one-time init (skipping `_start`'s C-runtime setup) and re-derives config from the fixed vaddrs (`SHMEM_VADDR_B` / `MODEL_VADDR_B` — no argv). **(b) is the cleaner path** — it avoids the ELF `.data` re-copy entirely and is the "restart-entry-skips-argv" approach.

**Measurement not obtained:** the abort halted the run before completing a single cycle, so the **zero-untyped gate (cslot-delta across N restarts) was NOT empirically confirmed.** It remains true *by construction* for a no-allocation reuse path, but is unproven end-to-end → it moves to the follow-up spike.

**DECISION:**
- **Direction LOCKED = reuse-in-place (the Strategy A family).** Strategy B is NOT chosen — it needs net-new `seL4_CNode_Revoke` + a proof that revoking PB's instance untyped spares the model/shared caps, strictly *more* work than fixing the re-entry.
- **Mechanic REFINED:** the respawn re-entry is **NOT** a `spawn_process_v` re-call — it must be a **muslc-init-safe re-entry (`pb_restart_entry` preferred)**. This also simplifies §6 (no re-run of musl's runtime init; the g_pool re-init happens inside `pb_restart_entry`, not via `_start`).
- **Follow-up:** **K/M2a-2** — a second spike implementing `pb_restart_entry` (+ the ELF-writable-reset fallback) and re-measuring the untyped delta across N≥8 cycles — precedes K/M2b.
- **KEPT from this milestone:** the §3 **hoist refactor** (durable file-scope `g_pb_*` handles — real K/M2b prep, OFF-build-verified byte-neutral). The throwaway spike block + its flag were reset (they did not reach a clean gate-passing state).

### K/M2a-2 implementation plan + a second finding (reconnaissance done 2026-07-05)

The `pb_restart_entry` re-entry (§4.1) was scoped against the live `inference_server.c`. A **second finding** shapes it: PB's query loop uses `qm`/`state`/`vocab`/`tok` as **`main()` locals** (`inference_server.c:512-546`, passed into `handle_query` at `:742-744`), and `llama_alloc_state` (`:547`) allocates the KV cache from **PB's bounded heap** (musl `malloc`, heap in `.bss`/`.data` which the restart *preserves*). Therefore a `pb_restart_entry` that **re-loads** the model would **re-alloc ~40 MiB of KV per cycle and leak it** (the old allocation is never freed — the heap is preserved, not reset), exhausting PB's heap within a few restarts. **So the mechanic must REUSE the warm model state, not reload it.**

**Required shape (K/M2a-2, an invasive PB-side change — its own careful increment):**
1. **Hoist** the warm inference state to reachable scope so `pb_restart_entry` can reuse it without re-alloc: `qm` / `vocab` / `tok` / `state` + the config (`req_notif` / `resp_notif` / `request_ring` / `response_ring` / `g_sctx_pb` / pool params `n_threads`/`done`/`wake[]`). Simplest safe form: under `#if JARVIS_KM2A_SPIKE`, stash pointers to `main()`'s locals into file-scope globals after setup + extract the query loop (`:708-765`) into a `pb_serve_loop()` both `main()` and `pb_restart_entry` call.
2. **Dedicated restart stack** (a static `g_restart_stack[]` in PB `.bss`): PA re-enters via `WriteRegisters(PC=pb_restart_entry, SP=g_restart_stack top)` so the new frame does NOT clobber `main()`'s frame (which holds the reused `qm`/`state`); the stashed pointers stay valid. Re-entering at `main()`'s original SP would overwrite that frame — do not.
3. `pb_restart_entry` re-inits ONLY volatile state (`jarvis_sel4_pool_init` from preserved params; per-query KV memset already happens in `handle_query`), re-signals `MSG_HEARTBEAT_ACK` + `Signal(resp_notif)`, and calls `pb_serve_loop()`.
4. PA side: the §6 reset sequence (suspend PB+workers → worker `WriteRegisters(PC=worker_entry)` + re-supply `ipc_buf` → `shmem_ipc_init` both rings, NOT sctx → drain notifs → `WriteRegisters(PB, PC=pb_restart_entry, SP=restart-stack)` → resume → drain-then-poll ready handshake), **with an `alloc_calls` counter around the whole path** (the definitive zero-untyped proof) + one inference per cycle for coherence.

**Status:** reconnaissance + design complete; the implementation is a delicate multi-round KVM bring-up (WriteRegisters PC/SP/arg conventions for PB-main AND workers, the restart-stack, the warm-state reuse) touching the deployed inference path — deferred to its own box-verified increment rather than rushed. `JARVIS_ACTIONS` / deploy image unaffected.

## 6. The ordered respawn sequence

1. **Suspend PB-main + ALL workers FIRST** (`seL4_TCB_Suspend`). A live worker mid-`seL4_Wait` on a since-freed wake cap faults on unmapped memory if you teardown before suspending.
2. **Apply the chosen strategy** — A: reset `.data`/zero `.bss` + set PC/SP; B: `seL4_CNode_Revoke` + re-retype the dedicated untyped.
3. **`shmem_ipc_init` BOTH rings** — allocation-free (memsets the existing mapped page + stamps `SHMEM_MAGIC 0xDEADBEEF` / version / `SHMEM_RING_SLOTS 15`, `shmem_ipc.c:47-53`). Clears torn / half-written messages + indices on the live frames. (`shmem_ipc_reset`, `:171`, zeros indices only — insufficient if slot contents are stale.)
4. **Do NOT `sctx_init`** — preserve PA working memory. PA is the single writer, so the SCTX seqlock is even/consistent across a PB crash; re-initializing would drop live context.
5. **Drain `resp_notif` + `wake[]`/`done`** — seL4 Notifications **COALESCE** (`threadpool_sel4.c:109-112` flags this exact hazard). A stale signal desyncs the M3 join (last-worker-to-zero mis-fires) or the ready handshake.
6. **Clear the PA latch + `infer_active`** — a mid-inference crash leaves a phantom `infer_active=1` (a fake in-flight inference / phantom throughput on the console); also discard any orphan `MSG_INFER_STATS` (§7).
7. **(B only) re-copy caps + re-map model/shared into the new VSpace + rebuild argv** (`main:1424-1440`).
8. **Reset/recreate workers** — re-supply each worker's **`ipc_buf` as the 3rd entry arg** (`seL4_SetIPCBuffer` is the worker's first act, `threadpool_sel4.c:45`; omitting it faults the first `seL4_Wait` at null-buffer — the M3 bring-up bug); re-apply `seL4_TCB_SetAffinity` (B); re-run `jarvis_sel4_pool_init` so `g_pool` is fresh.
9. **Ready handshake = DRAIN `resp_notif` then POLL the response ring for `MSG_HEARTBEAT_ACK 0x04` with a TIMEOUT — NEVER a bare `seL4_Wait(resp_notif)`.** The first boot uses the one legit `seL4_Wait(resp_notif)` (`main:2761`), but after a crash `resp_notif` may already be stale-signaled → the drain-then-poll pattern (`wait_for_response`, whose comment `:1459` documents the ~7% spurious-error Heisenbug) is mandatory. PB re-signals ready only AFTER model-load + M3 pool re-init (`inference_server.c:672-690`), so workers must be re-created before the ACK. A failed respawn ⇒ audit outcome **FAIL** + do NOT deadlock (bounded poll, then surface).
10. **Resume the workload** (`goto next_query`-equivalent), preserving the duty-cycle / FB-counter bookkeeping the existing `next_query` path (`main:3614`) does — a restart hook inserted before it must not skip that.

## 7. Landmines (each with its fix + citation)

- **Stale `resp_notif` signal** (`main:2761` legit-wait vs `:1459` Heisenbug) → drain-then-poll-with-timeout at the ready handshake (step 9); never bare `seL4_Wait` on the runtime path.
- **Ring-reset-YES / SCTX-reset-NO asymmetry** → re-init both rings (step 3), preserve SCTX (step 4). The rings carry cross-process in-flight IPC (torn on a crash); SCTX is PA-single-writer (consistent).
- **Worker teardown ordering** (`main:1398` local `wt`, not process-owned) → suspend-before-teardown (step 1); track workers in a persistent array; drive them to the clean `g_pool.shutting_down` exit (`threadpool_sel4.c:113-117`, quiescent-only today) before destroying TCBs.
- **Notification coalescing** (`threadpool_sel4.c:109-112`) → drain/re-create the reused wake/done caps (step 5) so a coalesced stale wake can't let a worker skip its active-counter decrement and hang the join.
- **Mid-inference orphan response + `MSG_INFER_STATS` half-latch** (`shmem_ipc.h:40-42`) — NOT a torn message; an ORDERING hazard: PB crashes after sending `MSG_INFER_STATS 0x11` but before/during the `MSG_RESPONSE` chunks → PA latches a tok/s for an inference that produced no output. Fix: the crashed query's poll hits POLL_TIMEOUT → abandon it as FAILED; the ring re-init (step 3) discards the orphan stats + partial response.
- **Orphan in-flight request** (PA sent `MSG_QUERY`, PB died before consuming) → cleared by the request-ring re-init (step 3).
- **`restart_pb` self-blocking on its own trigger text** (`shield_action.c:87-94`) — restart_pb is unblockable by risk SCORE (SELF_HEAL 10 + max learned 50 = 60 < threshold 80) **but IS blockable by the keyword blocklist**. The fault/hang `trigger_snapshot` string MUST avoid the 9 canonical keywords (`kill`, `shutdown`, `halt`, `destroy`, `delete`, `remove`, `format`, `rm -rf`, `drop table`). Use e.g. `"PB fault ep"` / `"hb age 34000 ms"` — never `"PB was killed"`.
- **`trust_policy` not yet wired live** (`shield_action.c:121-133` defined/tested; never called in `main_x86.c`) → K/M2 adds the first live call; state it's a NEW step, not a reuse.
- **argv/cap regeneration** (`main:1424-1440`) → **Strategy B** rebuilds the exact same argv (notif cptrs, worker wake/done cptrs, model/shmem vaddrs, `pb_n_threads`) after re-copying caps into the new CSpace. **Strategy A** preserves the caps (no regeneration) but re-executes `main()` at the reset PC/SP, so it must guarantee the **argv strings survive at the re-entry SP** (PB parses them at `inference_server.c:431-434,467-470,676-682`) — a spike question (§11 Q3).

## 8. Audit + telemetry

On every restart decision PA writes a JACT record (`act_audit_append(&g_action_audit, …)`, `main:2264` pattern; store `main:503`, init `:2141`): `action_id=ACTION_RESTART_PB`, `verdict=AUDIT_EXECUTED` (or `AUDIT_BLOCKED` if SHIELD ever refuses — it won't for a keyword-clean trigger), `outcome=AUDIT_OUT_OK|AUDIT_OUT_FAIL` (respawn succeeded / failed), `risk_x100` (the `shield_assess` score), `trigger_snapshot` = the fault/hang detail (keyword-clean, e.g. `"PB fault ep"` / `"hb age 34000 ms"`) — plus `restart_count++`. **The write path is entirely PA-side** (`action_audit.c` is pure + callback-driven over PA's own NVMe bounce; `evidence: no seL4/IPC/PB symbol`), so it logs even while PB is down/restarting. `restart_count` becomes the **K/M3 telemetry v7** field (with `actions_fired`/`actions_blocked`); the audit store is the 7-day criterion's evidence base ("no Level-2+ actions without approval" is answered by reading it via `parse_action_audit.py`).

## 9. Fault-injection probe (K/M2 extension of `JARVIS_ACTION_PROBE`)

TWO separate, probe-gated inducers so **both** detection paths are proven:
- **Crash** (exercises the fault EP): probe-gated code INSIDE PB deliberately faults (e.g. a null deref or `seL4_TCB_Suspend`-then-invalid-op) → PA's fault-EP poll fires.
- **Hang** (exercises the heartbeat-age path): probe-gated PA-side `seL4_TCB_Suspend(PB-main)` (or a PB busy-loop) → PA's `last_hb_ack_ms` age crosses the threshold.

Each must end in a restart + a JACT record + **resumed coherent inference**. Keep them behind `JARVIS_ACTION_PROBE` (default 0) so deploy is inert.

## 10. Milestone decomposition + done-when

- **K/M2a — the spike + strategy-lock** (KVM): implement both A and B respawn skeletons minimally, measure free-untyped across N restarts, answer the §11 open questions, **lock A or B**, and get this design reviewed. No committed behavior change.
- **K/M2b — implement the chosen respawn** behind `JARVIS_ACTIONS` (default 0): the prerequisite hoist refactor (§3) + detection (§5) + the ordered sequence (§6) + audit (§8) + the two probes (§9). Flag-OFF = deployed path byte-identical.
- **K/M2c — box smoke:** induce **crash AND hang separately** → PA restarts PB → the JACT record reads back via `parse_action_audit.py` → coherent inference resumes → `err=0` → **N repeated restarts do NOT drain untyped** (the §2 exhaustion gate). OFF-vs-ON `[INFER]` byte-identical (flag-OFF).

**Done-when (canon B1, `ROADMAP.md` §Backlog):** an induced PB crash on the box (or QEMU) auto-restarts PB, service resumes with err rate unaffected, and the event is durable + reconstructable from the NVMe log + a `restart_count` telemetry signal.

## 11. Risks + open questions (the spike answers these)

1. **Does the leaky allocman reclaim ANYTHING on a reset?** (Verified: no — so Strategy A's "draw zero untyped" is the safe path; B needs net-new `seL4_CNode_Revoke`.)
2. **Is a `process_config_fault_endpoint()` available + behaving in this libsel4utils version?** (Detection (a) depends on it.)
3. **Is Strategy A's ELF re-init tractable** — does `sel4utils` re-load into existing frames, or must PA hand-roll `.data` memcpy / `.bss` memset from the mapped CPIO ELF?
4. **Does revoking PB's instance untyped (B) SPARE the model + shared frame caps?** (Requires them in a separate untyped — must be proven, else B destroys the warm model.)
5. **The correct heartbeat-age threshold + N** — margin above the ~12 s worst-case single-query latency without false-positiving a busy PB.
6. **Worker teardown/reset mechanics** — `sel4utils_clean_up_thread` vs manual TCB/stack/IPC-buffer free; the clean `g_pool.shutting_down` drive-to-exit ordering.

## 12. Honest ceiling

> K/M2 is **bounded self-healing, not fault tolerance.** PA restarts a crashed/hung PB from the CPIO with the warm model preserved — but a **max-restart bound** applies: if PB keeps crashing (a crash-loop), PA **stops restarting, surfaces the condition** (audit + telemetry + HUD), and does NOT restart-loop. The worst case of a single restart is what a power-cycle already does (PB holds no durable state); the decision is SHIELD-scored and every attempt is durably audited. It stays **gated `JARVIS_ACTIONS` default-0 — deploy-inert until the K/M4 flip decision** (like every prior flip: real signal + box proof first). No claim of unattended reliability; the 7-day supervised run (goal 6-7) is where sustained self-healing is actually demonstrated.

---

*Companion to `phase6/docs/PHASE_6_GOAL_K_IT_ACTS.md` (K/M2). Ground truth re-verified against HEAD `4d77a7e` (2026-07-05). The A-vs-B choice is deferred to the K/M2a spike — this doc locks the constraint (reuse-in-place), the preserve/teardown boundary, the detection design, and the ordered sequence, not the strategy.*
