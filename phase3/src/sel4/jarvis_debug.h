/*
 * JARVIS AI-OS — Debug Configuration
 * Compile-time flags to control diagnostic output.
 * Set to 1 to enable, 0 to disable.
 *
 * For the deployed/stability config: ON = JARVIS_DBG_STATS, JARVIS_DBG_INFER_SUMMARY,
 * JARVIS_G3_RETRIEVAL (default-ON since G3/M6, 2026-07-02 — retrieval is deployed), and
 * JARVIS_CACHE_GROWTH (default-ON since #6/M3, 2026-07-03 — cache growth is deployed); all other
 * diagnostic/feature flags are 0 (JARVIS_G3_PROBE, JARVIS_G3_AB, JARVIS_DBG_BOOT_LOG stay OFF).
 * Enable diagnostics as needed.
 */

#ifndef JARVIS_DEBUG_H
#define JARVIS_DEBUG_H

/* Per-query IPC tracing in Process A ([DBG] q=N slot=N, send/signal/wait) */
#define JARVIS_DBG_IPC     0

/* Process B inference tracing ([PB] handle_query, generating, decoded) */
#define JARVIS_DBG_PB      0

/* Ring health checks before send ([PB] ring @... magic=... w= r=) */
#define JARVIS_DBG_RING    0

/* Periodic stats every 100 queries ([STATS] q= hits= infer= ...) — keep on for stability */
#define JARVIS_DBG_STATS   1

/* Per-inference summary line (query + response snippet) */
#define JARVIS_DBG_INFER_SUMMARY  1

/* NVMe log writes at every boot stage (FAT32 init, model progress, spawn).
 * Useful for diagnosing stalls. Turn OFF for 30-day stability test (write wear). */
#define JARVIS_DBG_BOOT_LOG  0

/* Phase 4 goal #1 / M0: AVX2-under-preemption safety probe (avx2_probe.h).
 * When 1, Process B runs a target("avx2,fma")-isolated YMM reduction vs a scalar
 * reference interleaved with the live PA<->PB workload, and confirms XCR0.AVX is
 * set (kernel saves AVX state). Default 0 — the M0 gate run builds with it =1.
 * This is a compile guard ONLY; it does NOT enable global -mavx2 (that is M1). */
#define JARVIS_AVX2_PROBE  0

/* Phase 4 goal #1 / M1: decode tok/s measurement. When 1, Process B brackets the
 * generation loop with RDTSC and reports "M1 gen=<n> cyc=<cycles>" via MSG_DEBUG;
 * Process A writes it to the NVMe log (LOG_INFER) + serial. Convert offline:
 * tok/s = n * TSC_HZ / cyc (Ryzen 2700X invariant TSC = 3.7 GHz). Default 0 — set 1
 * only for the M1 bare-metal bench (one LOG_INFER entry per inference). */
#define JARVIS_M1_MEASURE  0

/* Phase 4 goal #1 / M2: SMP viability probe (smp_probe.h). When 1, Process A
 * reads bootinfo->numNodes and logs "SMP numNodes=N nodeID=M apic=K" to the NVMe
 * log + serial (the primary M2 gate — numNodes==2 proves BSP + 1 AP both booted),
 * and Process B prints its local-APIC id to serial at startup (E1: do PA and PB
 * land on different cores by default?). Compile guard ONLY; does NOT enable SMP
 * (that is the kernel build: -DSMP=ON -DNUM_NODES=2). Default 0 — the M2 spike
 * build sets it = 1; the deployed image rebuilds with it = 0. */
#define JARVIS_SMP_PROBE  0

/* Phase 5 G3 (Retrieval Before Inference): master switch for the retrieval path.
 * DEFAULT ON since G3/M6 (2026-07-02) — retrieval is DEPLOYED. When 1, Process A — on the
 * inference route, BEFORE MSG_QUERY — scores the recent episodic batch, assembles a preamble, and
 * PACKS it into the shared context pool (sctx_pack_preamble), logging one [RETR] line per
 * inference; Process B then INJECTS that preamble into the Gemma prompt between the user turn's
 * `\n` and the question (bounded by g3_prompt_budget, prompt_ids[256], KV stays 512).
 * Injection is EXACT-KEY-ONLY (g3_select_exact_only) + fenced answer-only preamble
 * (g3_build_preamble_answer_only) + clean-boundary truncation (g3_clean_answer_len) — the
 * recency fallback was DROPPED for injection (caused P6 cross-topic contamination; do NOT
 * re-add) and prior-question text is NOT embedded (caused echo).
 * Ship basis (M6): 3× offline OFF-vs-ON A/B on the box — A/B1 exposed P6 (fixed 70ca236),
 * A/B2 blind panel net-positive + exposed P7 self-echo (fixed 2bb537b), A/B3 clean (### W = 0,
 * zero contamination, coherent, err=0; blind panel net-neutral-to-slightly-positive; the
 * residual lead-sentence overlap on repeats is the model's natural greedy answer — the OFF
 * baseline produces the identical opening with no injection).
 * Honesty: the metrics are retrieval HITS and LATENCY (telemetry v3 + console rows); "memory
 * helped" is NOT a claim the system makes — that stays an offline-A/B question.
 * When 0 (opt-out), the whole PA block + PB injection + their g3_retrieval.h include compile
 * out — OFF==baseline behavior, no new dependency. PA links g3_retrieval.c
 * (build_jarvis_x86.sh); PB uses only the header's static-inline g3_prompt_budget. */
#define JARVIS_G3_RETRIEVAL  1

/* Phase 5 G3/M4c: synthetic-fact PROBE harness (box-only). When 1 (alongside JARVIS_G3_RETRIEVAL=1),
 * Process A seeds ONE known INFER fact (marker SYNTHPROBEZX9Q7) whose query matches an inference
 * query, then self-checks (case-insensitive contains) whether the model echoes the marker after
 * retrieval injects it — proving the injected memory is PRESENT-AND-USABLE (NOT "memory helped",
 * which stays an offline A/B question). Default 0 → the seed + self-check compile out, deploy
 * byte-identical. The operator confirms `[PROBE] … hit=1` (+ `[RETR] lat_us=` < 50 ms) in a
 * flag-ON QEMU smoke. */
#define JARVIS_G3_PROBE  0

/* Phase 5 G3/M6: OFF-vs-ON retrieval A/B harness (box-only). When 1, Process A dumps the FULL query +
 * FULL response for every inference as one `[AB] q=<n> query="..." resp="..."` serial line (control
 * chars -> space so the log stays line-oriented), so an offline judge can compare a flag-OFF (baseline)
 * run against a JARVIS_G3_RETRIEVAL=1 run over the IDENTICAL, deterministic (rng_state=42) query
 * sequence — the M6 re-flip gate ("does retrieval HELP", not just "work"). Independent of
 * JARVIS_G3_PROBE (leave PROBE=0 for the A/B so the real inference prompts are used). Default 0 ->
 * compiles out, deploy byte-identical. */
#define JARVIS_G3_AB  0

/* Phase 5 #6 cache-growth — canon = promote repeated query→action patterns from the EPISODIC LOG
 * into the decision cache (see phase5/docs/PHASE_5_GOAL6_CACHE_GROWTH.md). The route-through-cache
 * impl (commit 7e8c30f) was REVERTED (wrong design vs canon); this flag owns the canon pass:
 * the [STATS]-cadence promotion (M1: Option-B rolling freq aggregate seeded at the boot scan +
 * CG_PROMOTE_HWM=409 cap) + the READ-only cache_lookup-before-infer serve path (M3a — serves
 * already-promoted answers; NEVER inserts on the inference path).
 * DEFAULT ON since #6/M3 (2026-07-03) — cache growth is DEPLOYED. Ship basis: S1-snapshot
 * ON/OFF/REF box proof — ON: grow 6→9, used max 261 < hwm 409 (SEC-024 LRU never fires; EMPTY
 * slots survive, the <1 ms miss path preserved), served=42,404 promoted-pattern hits, infer
 * FROZEN at 17 while q reached 283,400 err=0 (vs q≈220 OFF in the same 1800 s); served text =
 * coherent stored answer heads ([CACHE-SERVE] verbatim); OFF = byte-identical to the pre-M3a
 * baseline. Honesty: the cache learns FREQUENTLY-ASKED queries and serves them fast
 * (frequency-based, deterministic) — it never "understands"; the throughput multiple is a
 * property of the repeat-heavy workload. When 0 (opt-out), all #6 blocks compile out. */
#define JARVIS_CACHE_GROWTH 1

/* Phase 5 #5 SHIELD failure-learning (MONITOR-ONLY — learns + surfaces a per-key risk signal,
 * NEVER blocks; SEC-039 unchanged, live enforcement is Phase 6). When 1, Process A derives the
 * risk map from the episodic log's ERROR/BLOCKED records (D-a): the boot recall-scan seeds
 * shield_learn_record_failure per persisted failure, and the [STATS]-cadence pass folds this
 * batch's failures the same way; a [SHIELD-LEARN] keys=/maxrisk_x100=/fails= summary prints at
 * the [STATS] cadence. Phase-1 parity arithmetic (+0.1/failure, cap 0.5, monotonic-raise-only).
 * Default 0 -> the include + table + seed + fold compile out; deploy behavior-identical.
 * The benign deployed workload runs err=0, so with no probe the live counters honestly read 0. */
#define JARVIS_SHIELD_LEARN 0

/* Phase 5 #5/M3 synthetic-failure PROBE (box-only, needs JARVIS_SHIELD_LEARN=1): injects the SAME
 * failing action twice (one synthetic EPI_OUT_ERROR record per [STATS] tick, marker query, no
 * resp — so retrieval/cache-growth ignore it by their OK+resp filters) and prints
 * `[SHIELD-PROBE] attempt=N risk_x100=` after each fold. The criterion-2 signal is the risk
 * RISING on the repeat (10 -> 20), honestly scoped: the learning signal, NOT live blocking.
 * Default 0 -> compiles out. */
#define JARVIS_SHIELD_PROBE 0

/* Phase 5 #4 semantic memory (WRITE-ONLY at M1): at boot, Process A distills a bounded window of
 * the persisted episodic store into the SEPARATE semantic fact store (sd_distill ->
 * sem_store_upsert @ LBA 21,110,000) — DETERMINISTIC (support >= SEM_MIN_SUPPORT + consistency;
 * no LLM, no embeddings; honest ceiling = observable repeated Q&A patterns compacted into durable
 * facts, never "knows preferences"/"understands"). The store is populated but NEVER read into
 * inference/routing at M1 (retrieval from it is a future G3 slice with its own hygiene review),
 * so ON changes no generation; when 0 the includes + store + distill compile out
 * (behavior-identical). Default 0 — a flip is a deliberate M4 call. */
#define JARVIS_SEMANTIC 0

/* Phase 6 K: the action-execution spine (allowlist + linked SHIELD gate + JACT action-audit
 * store @ LBA 21,120,000). When 1, Process A inits the audit store, links the it-acts spine
 * (shield_action.c), and runs the fault-EP receiver + miss-counter self-heal: a PB crash/wedge is
 * detected -> shield_assess (SHIELD-scored) -> reuse-in-place respawn -> JACT audit.
 * **K/M4 FLIPPED DEFAULT-ON 2026-07-08** — the deployed x86 image now runs the SHIELD-scored,
 * JACT-audited self-heal ACTION gate LIVE, which closes SEC-039 FOR THE ACTION PATH (boot_id=15
 * supervised on-wire: v7 crc_ok, TLM_F_ACTIONS set, restart/fired/blocked honest-0, err=0, NN=6).
 * The QUERY path stays PASSIVE (Process B returns ALLOW) BY DESIGN — this does NOT block queries.
 * When 0 the includes + store + probe compile out (action-inert, byte-identical to pre-K). */
#define JARVIS_ACTIONS 1

/* Phase 6 K/M1 induced-BLOCK probe (box-only, needs JARVIS_ACTIONS=1): one-shot at boot,
 * three cases through the REAL linked gate, NOTHING executes — (A) benign allowlisted action
 * -> EXECUTE risk=10 (assessed only; proves not-a-blanket-block), (B) the blocklisted poison
 * id -> BLOCKED risk=100 + an AUDIT_BLOCKED record (the SEC-039 closure-mechanism teeth),
 * (C, needs JARVIS_SHIELD_LEARN=1) PROBE_HIGH 75 EXECUTE -> one recorded failure -> 85
 * BLOCKED on the 2nd attempt (K-e — Phase-5 criterion-2's live half) + audited.
 * `[ACTION-PROBE]` serial proof lines. Default 0 -> compiles out. */
#define JARVIS_ACTION_PROBE 0

/* Phase 6 goal 6-1 M1: always-on monitors — lightweight threshold watchers over REAL observable
 * state at the [STATS] cadence (first watcher: the q_errors window delta). A debounced,
 * fire-once-per-crossing threshold crossing emits a NOTIFY through the K action spine
 * (ACTION_NOTIFY_ANOMALY -> spine_decide -> [ANOMALY] line + JACT record + actions_fired) —
 * NOTE a NOTIFY-only monitor still EXECUTES something real (line + audit + counter), so it
 * gates like any action: prove on the box, flip deliberately (the K discipline). When 0 the
 * watcher statics + tick compile out (deploy byte-identical). Default 0. */
#ifndef JARVIS_MONITORS
#define JARVIS_MONITORS 0
#endif
#if JARVIS_MONITORS && !JARVIS_ACTIONS
#error "JARVIS_MONITORS requires JARVIS_ACTIONS (the monitor rides the action spine: spine_decide / g_action_audit / g_actions_fired)"
#endif

/* Phase 6 6-1/M1 monitor probe (box-only, needs JARVIS_MONITORS=1): feeds a synthetic
 * over-threshold delta into the q_errors watcher's WINDOW DELTA (never the real counter) for
 * the first 3 [STATS] windows — fires at window MON_ERRRATE_DEBOUNCE, window 3 sustained must
 * NOT re-fire (the M0 fire-once latch), later real-0 windows re-arm. Its OWN flag (the
 * G3_PROBE/SHIELD_PROBE one-flag-per-probe precedent) because JARVIS_ACTION_PROBE runs end at
 * the committed HARDLOOP experiment (Outcome-B starve) and never reach the workload.
 * `[MON-PROBE]` serial proof lines. Default 0 -> compiles out. */
#define JARVIS_MONITOR_PROBE 0

/* Phase 6 K/M2a-2 reuse-in-place respawn spike (box-only KVM measurement; SYSTEM_DESIGN
 * §4.1/§4.2). When 1, Process B gains a muslc-init-safe `pb_restart_entry` (re-enters PAST
 * musl's one-time init on a dedicated ABI-aligned restart stack, REUSING the warm model
 * state — no re-alloc) and Process A runs an N-cycle spike after the ready handshake:
 * suspend PB-main (quiescent) → WriteRegisters(rip=pb_restart_entry, fresh SP, fs_base
 * preserved) → Resume → drain-then-poll the ready ACK → one inference → measure the 3
 * zero-RESOURCE axes (PB musl-heap pointers flat + PA cslot-delta==0 + coherent gen).
 * Independent of JARVIS_ACTIONS (which stays 0 — the deploy image is action-inert and this
 * flag is OFF in it). THROWAWAY: reset after the measurement locks Strategy A. Default 0 ->
 * pb_restart_entry + the spike driver + the restart stack compile out (deploy byte-identical;
 * pb_serve_loop is extracted unconditionally but is a behavior-neutral refactor). */
#define JARVIS_KM2A_SPIKE 0

/* Phase 6 K/M2b-2: the reuse-in-place respawn PRIMITIVES (pb_restart_entry + the dedicated
 * restart stack + the stashed warm-context handles + km2b_reset_workers) are needed by BOTH the
 * K/M2a-2/b-1 measurement spike (JARVIS_KM2A_SPIKE) AND the live self-healing path
 * (JARVIS_ACTIONS). This macro gates the shared primitives; the spike-measurement-only bits (the
 * N-cycle driver, the cooperative-flag/TOKSPIN markers) stay under JARVIS_KM2A_SPIKE, and the
 * live detect→assess→execute→audit path stays under JARVIS_ACTIONS. */
#define JARVIS_RESPAWN (JARVIS_KM2A_SPIKE || JARVIS_ACTIONS)

/* Serial [STATS] prints every 100 queries; NVMe LOG_IPC_STATS is written every
 * JARVIS_STATS_NVME_INTERVAL. Measured bare-metal rate is ~3k queries/day (single
 * core, scalar) -> interval=100 gives ~870 entries over 30 days, well under the
 * 2700-entry saturating log. (Task 3 used 1000 on a 16k/day estimate the bare-metal
 * run disproved; q=400 err=0 verified on real hardware 2026-06-15.) */
#define JARVIS_STATS_NVME_INTERVAL  100

/* Per-forward-pass tracing in llama_quant.c ([L00@N], [FWD], [TOP5@N]).
 * NOTE: llama_quant.c does NOT include this header (it is built in both seL4
 * and standalone native-test contexts). It carries its own #ifndef fallback
 * that defaults to 0. To enable, compile llama_quant.c with -DJARVIS_DBG_FORWARD=1.
 * Documented here for discoverability. */

#endif /* JARVIS_DEBUG_H */
