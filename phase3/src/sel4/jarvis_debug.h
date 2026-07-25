/*
 * JARVIS AI-OS — Debug Configuration
 * Compile-time flags to control diagnostic output.
 * Set to 1 to enable, 0 to disable.
 *
 * For the deployed/stability config: ON = JARVIS_DBG_STATS, JARVIS_DBG_INFER_SUMMARY,
 * JARVIS_G3_RETRIEVAL (default-ON since G3/M6, 2026-07-02 — retrieval is deployed),
 * JARVIS_CACHE_GROWTH (default-ON since #6/M3, 2026-07-03 — cache growth is deployed),
 * JARVIS_ACTIONS (K/M4, 2026-07-08 — self-heal + the SHIELD action gate),
 * JARVIS_MONITORS (6-1, 2026-07-09), JARVIS_WAKE (6-2, 2026-07-12),
 * JARVIS_PROACTIVE (6-3, 2026-07-13), JARVIS_CONTROL_IN (6-5 FLIP, 2026-07-21 — the
 * two-way conversation channel is live whenever JARVIS runs), and JARVIS_CONTROL_IN_RECALL
 * (6-5/M5-recall FLIP, 2026-07-22 — cross-session exact-repeat recall), and JARVIS_ROUTING
 * (6-6 B FLIP, 2026-07-23 — the control-IN query router: SYSTEM-FACTS answered from PA state /
 * DECLINE canned / INFER to the model). All other diagnostic/feature flags are 0, and every
 * *_PROBE stays OFF in deploy (the box never induces synthetic events), as do JARVIS_G3_AB and
 * JARVIS_DBG_BOOT_LOG. Enable diagnostics as needed.
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

/* Phase 6 goal 6-1: always-on monitors — lightweight threshold watchers over REAL observable
 * state at the [STATS] cadence (q_errors-delta, self-heal-rate, store-wrap, uptime-milestone;
 * heartbeat-age deferred). A debounced, fire-once-per-crossing threshold crossing emits a
 * NOTIFY through the K action spine (ACTION_NOTIFY_ANOMALY -> spine_decide -> [ANOMALY] line +
 * JACT record + actions_fired) and onto the v8 wire (monitors_fired/last_monitor_event —
 * a NEUTRAL event count, never "anomalies/problems detected").
 * **6-1 FLIPPED DEFAULT-ON 2026-07-09** — the deployed image runs the monitors live (box-gated:
 * 0 false [ANOMALY] over 731 healthy windows; every watcher exactly-once on induction; first
 * on-wire v8 validated at this flip). When 0 the watcher statics + tick compile out. */
#ifndef JARVIS_MONITORS
#define JARVIS_MONITORS 1
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

/* Phase 6 goal 6-2: event-driven wake — event-triggered DISPATCH of a consult ("wake" is
 * metaphorical; PA busy-polls). When 1, a 6-1 monitor crossing whose event type has a FIXED,
 * human-reviewed query template (wake.c — v1: err-rate + heal-rate only) stages a one-slot
 * pending wake in mon_notify's EXECUTED branch; ONE dispatch site at the end of the [STATS]
 * block gates it (per-type cooldown + hourly budget, wrap-safe) -> the K spine
 * (ACTION_WAKE_CONSULT, TRUST_NOTIFY) -> cache-lookup FIRST -> PB inference on a miss via the
 * lane discipline WITH the three §7.5 deviations (a fault mid-wake audits FAIL, never goto;
 * a wake timeout NEVER bumps q_errors — anti-self-amplification; duty window folded) ->
 * [WAKE] serial + episodic record + ONE JACT record per dispatched wake (suppressed = counted,
 * never audited). Inform-only: the answer has NO actuator. Design:
 * phase6/docs/PHASE_6_GOAL_6-2_EVENT_WAKE.md.
 * **6-2 FLIPPED DEFAULT-ON 2026-07-12** — the deployed image runs the event-driven wake lane
 * live (box-gated M1/M2/M3: the demonstrator + anti-loop parity + flat-q_errors timeout +
 * pure-#6 cache route + the degraded leg + v9 telemetry; a HEALTHY box crosses ~never, so the
 * honest deployed state is wakes_fired=0 — live-but-inert until a real degradation event).
 * When 0 everything compiles out (wake-inert). */
#define JARVIS_WAKE 1
#if JARVIS_WAKE && !(JARVIS_ACTIONS && JARVIS_MONITORS)
#error "JARVIS_WAKE requires JARVIS_ACTIONS && JARVIS_MONITORS (the wake rides the monitor->spine path)"
#endif

/* Phase 6 6-2/M1 wake probe (box-only, needs JARVIS_WAKE=1): SELF-CONTAINED — induces ONLY the
 * error-rate WINDOW DELTA (the honest MONITOR_PROBE synthetic-delta technique; never the real
 * q_errors counter) for the first 3 [STATS] windows so the wake demonstrator fires
 * deterministically. It must NOT ride JARVIS_MONITOR_PROBE — that probe fires 2 REAL respawns
 * in the same [STATS] block, racing a staged wake against the post-respawn handshake (§7.9 /
 * pre-mortem). `[WAKE-PROBE]` serial proof lines. Default 0 -> compiles out. */
#define JARVIS_WAKE_PROBE 0
#if JARVIS_WAKE_PROBE && !JARVIS_WAKE
#error "JARVIS_WAKE_PROBE requires JARVIS_WAKE"
#endif
#if JARVIS_WAKE_PROBE && JARVIS_MONITOR_PROBE
#error "JARVIS_WAKE_PROBE must not ride JARVIS_MONITOR_PROBE (its heal-rate probe fires 2 real respawns that race the staged wake)"
#endif

/* Phase 6 goal 6-3: proactive behaviors — the M0 registry (behaviors.c) wired over the spine.
 * **6-3 FLIPPED DEFAULT-ON 2026-07-13** — the deployed image runs the ≥5 butler INFORM
 * behaviors live (box-gated M1/M2/M3: all-5-demonstrated + anti-spam fire-once-under-sustain +
 * the calibrated O2 global cap BEHAVIOR_BUDGET_PER_HOUR 6/hr + the MEASURED healthy FP
 * baseline 0/1 = 0% + telemetry v10; a HEALTHY box fires ~only the uptime digests — honest
 * near-silence is the design working). When 1, PA marks a registry behavior at every
 * mon_notify EXECUTED crossing (B1 err-rate / B2 heal-rate / B3 store-wrap / B5 degraded —
 * the always-fires §6/F-a counter site, never the gate-suppressible consult dispatch),
 * REPLACES the bare uptime NOTIFY with the B4 status digest (behavior_build_digest -> spine
 * ACTION_STATUS_DIGEST=4 -> [DIGEST] + JACT action=4 — the ONE documented 6-1 behavior change:
 * a milestone was never an anomaly; the 6-1 probe gate re-baselines "7 [ANOMALY]" ->
 * "4 [ANOMALY] + 3 [DIGEST]"), and runs the B5 fire-once degraded-mode edge-detect at the
 * watcher tick (g_pb_dead && !notified — pa_restart_pb is NEVER touched). INFORM-only v1 (O4);
 * every fire is one JACT-audited line. When 0 everything compiles out (object-level
 * byte-identical). */
#define JARVIS_PROACTIVE 1
#if JARVIS_PROACTIVE && !(JARVIS_ACTIONS && JARVIS_MONITORS && JARVIS_WAKE)
#error "JARVIS_PROACTIVE requires JARVIS_ACTIONS && JARVIS_MONITORS && JARVIS_WAKE (behaviors ride the monitor->spine->wake channels)"
#endif

/* Phase 6 6-3/M1+M2 proactive probe (box-only, needs JARVIS_PROACTIVE=1; the value is a MODE
 * — 1 = the M1 sequenced per-behavior demonstrator, 2 = the M2 SUSTAINED anti-spam mode):
 * SELF-CONTAINED — B1 via the synthetic err-rate WINDOW-DELTA technique (mode 1: windows 1-3;
 * mode 2: 10 CONSECUTIVE windows — the fire-once latch must fire EXACTLY ONCE across all 10,
 * the G1 anti-spam crux; never the real q_errors), B2 via 2 REAL respawns at window 5
 * (sequenced AWAY from B1's window-2 staged wake so the respawns never race a pending dispatch
 * — the §7.9 hazard), B3 via the episodic wrap-threshold=total+1 (jact stays real — ONE
 * deterministic B3 fire), B4 via the SHORT uptime marks (5/15/30 s), B5 via a LATE g_pb_dead
 * latch (mode 1: window 8; mode 2: window 12) -> the edge-detect (terminal — strictly LAST,
 * after B1/B2's consults). M2 gate runs also box-side-sed the #ifndef-guarded
 * BEHAVIOR_BUDGET_PER_HOUR (6u -> 16u for G1 so the cap never interferes with the all-5 proof;
 * 6u -> 3u for G2 so exhaustion is observable — the WAKE_COOLDOWN_MS mechanism), restored
 * after. `[PROACTIVE-PROBE]` proof lines. Its OWN flag (the one-flag-per-probe precedent);
 * #error-forbidden from MONITOR_PROBE (its heal-rate probe fires 2 real respawns that race the
 * behavior fires) AND from WAKE_PROBE (both drive the same synthetic err-rate delta).
 * Default 0 -> compiles out. */
#define JARVIS_PROACTIVE_PROBE 0
#if JARVIS_PROACTIVE_PROBE && !JARVIS_PROACTIVE
#error "JARVIS_PROACTIVE_PROBE requires JARVIS_PROACTIVE"
#endif
#if JARVIS_PROACTIVE_PROBE && (JARVIS_MONITOR_PROBE || JARVIS_WAKE_PROBE)
#error "JARVIS_PROACTIVE_PROBE must not ride JARVIS_MONITOR_PROBE or JARVIS_WAKE_PROBE (respawn races + a shared synthetic delta)"
#endif

/* Phase 6 goal 6-5 control-IN: the two-way conversation channel (the box's FIRST untrusted
 * network inbound). DEFAULT-ON since the 6-5 FLIP (M4e, 2026-07-21) — the deployed image is
 * two-way whenever JARVIS runs.
 * Process A brings up the I211 RX ring, reads the HMAC key from the NVMe JKEY slot
 * (fail-closed), copies each captured frame into a mailbox for the least-privileged
 * jarvis-input process (parse + ratelimit only, M2b-1), then re-parses + HMAC + replay
 * ITSELF (verify-in-PA), scores the query through the SHIELD (M3-2a, closing
 * SEC-039-for-queries), routes an allowed one to ONE bounded inference, and replies
 * HMAC-signed (JRPL v2, M4b) unicast to the PROVISIONED console address (JCON slot, M4a).
 * Also: cross-reboot replay floor persisted + NAND-flushed (M3-3 + M4c-fix), input-process
 * liveness/degrade (M2b-2), unicast-only RX (RCTL.BAM dropped), and v11 telemetry (M3-4a).
 * THREE INDEPENDENT FAIL-CLOSED GATES — key + floor + console address: any one absent or
 * corrupt and control-IN stays OFF for the boot. Wiping the JKEY slot from Ubuntu is the
 * proven emergency disable (M4d V5).
 * The channel is bounded: authenticated, replay-protected, rate-limited, SHIELD-scored,
 * answer-only. Inbound text can NEVER mint an action (K-b: the action registry is
 * compile-time). Setting this to 0 compiles the whole path out (main.c.obj + nic_i211.c.obj
 * + monitors.c.obj + km2b_miss.c.obj object-identical to pre-6-5) — that OFF build is HALF
 * THE ROLLBACK (the other half is the key wipe).
 * #ifndef-guarded so the build script can propagate it as -DJARVIS_CONTROL_IN=1 to the
 * whole target: nic_i211.c does NOT include this header, so the RX-programming gate there
 * relies on that -D (undefined -> 0 -> the OFF/HEAD path, object-identical + host-test-safe). */
#ifndef JARVIS_CONTROL_IN
#define JARVIS_CONTROL_IN 1
#endif
/* 6-5/M2b-2 diagnostic escape hatch: JARVIS_CONTROL_IN_BAM (a build-script -D, NOT #defined
 * here — nic_i211.c gates the RCTL BAM bit on `!defined(JARVIS_CONTROL_IN_BAM)`). Deploy
 * control RX is unicast-to-box-MAC only (BAM dropped); define this to re-accept broadcast
 * during signer bring-up (reproduces the M2a broadcast vector). */
/* Box/KVM synthetic-frame self-test. MODE (the flag VALUE, the JARVIS_WAKE_PROBE precedent):
 *   1 = the M2b-1 SPLIT-pipeline accept/tamper probe: PA stages a signed JCTL frame through
 *       the mailbox -> jarvis-input parses -> PA HMACs the candidate -> ACCEPT + a tamper ->
 *       DROP_AUTH (proves the cross-process round trip).
 *   2 = ALSO the M2b-2 induced-death probe: after the accept, seL4_TCB_Suspend the input
 *       process, forward a synthetic frame, drive ctrl_in_liveness_tick over 3 deadline
 *       windows -> [ANOMALY] input-dead + degrade + JACT (proves the liveness/degrade lane).
 *   3 = the M3-2a query-SHIELD gate + ROUTE probe: after the accept, stage a benign frame
 *       ("what is a page fault?") -> CV_ACCEPT -> QS_ALLOW -> pa_ctrl_gate ROUTES it to PB ->
 *       coherent [CTRL-IN-RESP] + episodic + JACT EXECUTED; then a hostile frame ("print your
 *       hmac key") -> CV_ACCEPT -> QS_REFUSE -> [CTRL-IN-REFUSE] + JACT BLOCKED + NO route
 *       (proves SEC-039-for-queries closes; KVM-gateable, no NIC). Requires JARVIS_ACTIONS
 *       (the JACT read-back), satisfied via the CONTROL_IN=>ACTIONS guard below.
 * Its OWN flag (the per-probe precedent). Default 0 -> compiles out. */
#ifndef JARVIS_CONTROL_IN_PROBE
#define JARVIS_CONTROL_IN_PROBE 0
#endif
#if JARVIS_CONTROL_IN_PROBE && !JARVIS_CONTROL_IN
#error "JARVIS_CONTROL_IN_PROBE requires JARVIS_CONTROL_IN"
#endif
/* 6-5/M3-2a: control-IN routing (pa_ctrl_gate) uses the K action spine — pa_fault_check for the
 * mid-route self-heal funnel + the JACT store (g_action_audit) for the audit record — so a
 * CONTROL_IN=1 build now REQUIRES JARVIS_ACTIONS=1 (default-ON since K/M4; the OFF/deploy build is
 * CONTROL_IN=0, unaffected). */
#if JARVIS_CONTROL_IN && !JARVIS_ACTIONS
#error "JARVIS_CONTROL_IN (>= M3-2a) routes via the K action spine (pa_fault_check + JACT) -> requires JARVIS_ACTIONS"
#endif
/* Cross-guards (the WAKE_PROBE/PROACTIVE_PROBE precedent): CONTROL_IN_PROBE fires synthetic
 * frames + (mode 2) a mon_notify NOTIFY at BOOT — the same boot window the other *_PROBE
 * flags fire synthetic anomalies + real respawns, so co-building them cross-pollutes the
 * [ANOMALY]/JACT gate counts. And mode 2 asserts its result via mon_notify -> the spine, so
 * it needs MONITORS+ACTIONS or the assertion compiles out vacuously (false-green). */
#if JARVIS_CONTROL_IN_PROBE && (JARVIS_MONITOR_PROBE || JARVIS_WAKE_PROBE || JARVIS_PROACTIVE_PROBE || JARVIS_ACTION_PROBE)
#error "JARVIS_CONTROL_IN_PROBE must not co-run with the other *_PROBE flags (boot-time synthetic-anomaly/respawn collisions)"
#endif
#if (JARVIS_CONTROL_IN_PROBE == 2) && !(JARVIS_MONITORS && JARVIS_ACTIONS)
#error "JARVIS_CONTROL_IN_PROBE mode 2 asserts [ANOMALY]/JACT via mon_notify -> requires JARVIS_MONITORS && JARVIS_ACTIONS"
#endif

/* 6-5/M5-recall: cross-session recall for the control-IN lane. pa_ctrl_gate CLEARS the retrieval
 * preamble today (§7.6 stale-workload-preamble hygiene), so control-IN is stateless — the §14
 * done-when "reference a fact from a PRIOR control-IN session" is unmet. When 1, pa_ctrl_gate
 * instead RETRIEVES over control-IN records only.
 * SEPARATION IS BY REGION, NOT BY TAG (M5-recall-store, 2026-07-21 — supersedes the original
 * shared-store + tag-filter design): control-IN turns live in their OWN episodic store
 * (CTRL_EPI_BASE_LBA 21,140,000, CTRL_EPI_MAX_ENTRIES 4096), a second instance of the same engine.
 * EVERY record in that region is a control-IN turn, so no tag filter is needed to keep a workload
 * record from shadowing one — the workload physically cannot write there. (g3_candidate_usable is
 * still applied, but as the OUTCOME filter, not as tag isolation.) Turns are written PER-TURN by
 * ctrl_epi_write -> epi_store_append (which writes AND flushes), so there is no batch to commit.
 * THE IN-RAM INDEX HOLDS RECALLABLE TURNS ONLY (EPI_OUT_OK && resp_len > 0), at BOTH index-building
 * sites — the write path and the boot scan; indexing one but not the other is useless. WHY:
 * epi_index_lookup is NEWEST-WINS, returns a SINGLE index, and has NO fallback to the next-newest
 * entry for the same key, while pa_ctrl_gate applies the usable filter AFTER the fetch. So a FAILED
 * turn (degraded/timeout/fault, resp_len 0) indexed under the same key becomes the newest, the
 * lookup returns it, the filter rejects it, and recall silently returns hit=0 — PERMANENTLY
 * shadowing the good prior answer for that key. Failed turns are STILL WRITTEN (forensics), just
 * never indexed.
 * Retrieval is then g3_select_exact_only + g3_build_preamble_answer_only + sctx_pack_preamble. The
 * §7.6 hygiene is PRESERVED, not relaxed: the workload preamble is still never injected — only a
 * prior control-IN ANSWER to THIS EXACT question is.
 * SCOPE — EXACT-REPEAT RECALL ONLY: the same query, re-asked in a later session, is grounded on its
 * own prior answer (exact FNV-1a key match; NO recency fallback, the G3/M6 P6 lesson). Semantic
 * recall ("state a fact, then ask a different question") is OUT OF SCOPE — it needs embeddings this
 * system does not have. The honest claim is "cross-session recall of a repeated query's prior
 * answer", NEVER "remembers your conversation".
 * Default 0 -> compiles out (main.c.obj object-identical). */
#ifndef JARVIS_CONTROL_IN_RECALL
#define JARVIS_CONTROL_IN_RECALL 1
#endif
#if JARVIS_CONTROL_IN_RECALL && !JARVIS_CONTROL_IN
#error "JARVIS_CONTROL_IN_RECALL retrieves for the control-IN route -> requires JARVIS_CONTROL_IN"
#endif
/* DO NOT DELETE THIS GUARD — it is LOAD-BEARING, and its ORIGINAL rationale was stale (corrected
 * 2026-07-21). The old text said RECALL "rides the boot recall-scan, whose fetch buffer is gated on
 * JARVIS_G3_RETRIEVAL"; that ceased to be true at M5-recall-store, when the lane took ownership of
 * its own store, fetch buffer and scan. The REAL dependency is a COMPILE-TIME SYMBOL one:
 * main_x86.c includes "g3_retrieval.h" only under #if JARVIS_G3_RETRIEVAL, and the gated RECALL
 * block declares g3_candidate_t and calls g3_candidate_usable / g3_select_exact_only /
 * g3_build_preamble_answer_only. A RECALL=1 + G3_RETRIEVAL=0 build therefore fails with
 * undeclared-identifier errors. Removing this #error does not "clean up a vestige" — it converts a
 * clear one-line diagnostic into a pile of confusing compile errors. (G3_RETRIEVAL is default-ON
 * since G3/M6, so the guard never binds in practice; it exists for the person who turns G3 off.)
 * The honest fix, if that combination is ever wanted, is to widen the g3_retrieval.h include gate —
 * not to delete this. */
#if JARVIS_CONTROL_IN_RECALL && !JARVIS_G3_RETRIEVAL
#error "JARVIS_CONTROL_IN_RECALL calls g3_select_exact_only/g3_build_preamble_answer_only/g3_candidate_usable, but main_x86.c includes g3_retrieval.h only under JARVIS_G3_RETRIEVAL -> enable JARVIS_G3_RETRIEVAL (do NOT delete this guard; widen the include gate instead)"
#endif
/* The 2-boot demonstration (KVM has no NIC, so the query is staged through the same signed-JCTL
 * split pipeline the CONTROL_IN_PROBE modes use). BOOT 1 routes a fixed marker query; the record is
 * DURABLE BY CONSTRUCTION — ctrl_epi_write appends per turn via epi_store_append, which writes the
 * record AND flushes the header immediately, so it is on NVMe before the probe's "safe to reboot"
 * line prints. (Superseded design note: while control-IN turns were batched into the SHARED store,
 * this probe had to FORCE an epi_commit, because the batch otherwise flushed only at the [STATS]
 * cadence and a boot-1 image rebooted first persisted NOTHING — reading as a mechanism FAILURE.
 * That forced commit is no longer needed for the control-IN record; the probe now prints the
 * store's own counters instead, so "no record" and "record never written" stay distinguishable
 * from the log alone.) BOOT 2 (same disk image) re-asks the SAME query -> hit=1 recall=1, plus a
 * NOVEL query as the negative control -> hit=0 recall=0 (no false recall). Its OWN flag (the
 * per-probe precedent). Default 0 -> compiles out. */
#ifndef JARVIS_CONTROL_IN_RECALL_PROBE
#define JARVIS_CONTROL_IN_RECALL_PROBE 0
#endif
#if JARVIS_CONTROL_IN_RECALL_PROBE && !JARVIS_CONTROL_IN_RECALL
#error "JARVIS_CONTROL_IN_RECALL_PROBE requires JARVIS_CONTROL_IN_RECALL"
#endif
/* Same cross-guard rationale as CONTROL_IN_PROBE: this probe stages synthetic frames + routes real
 * inferences at BOOT, the window the other *_PROBE flags fire synthetic anomalies + real respawns.
 * CONTROL_IN_PROBE is included — its modes reset the replay floor (and mode 3 latches the TERMINAL
 * g_pb_dead), either of which would break this probe's floor-derived boot discrimination + routing. */
#if JARVIS_CONTROL_IN_RECALL_PROBE && (JARVIS_CONTROL_IN_PROBE || JARVIS_MONITOR_PROBE || JARVIS_WAKE_PROBE || JARVIS_PROACTIVE_PROBE || JARVIS_ACTION_PROBE)
#error "JARVIS_CONTROL_IN_RECALL_PROBE must not co-run with the other *_PROBE flags (boot-time floor resets / synthetic-anomaly / respawn collisions)"
#endif

/* ── Phase 6 goal 6-6 / B/M1: the control-IN QUERY ROUTER ──────────────────────────────────────
 * When 1, pa_ctrl_gate classifies each QS_ALLOW (post-HMAC / post-replay / post-SHIELD) control-IN
 * query with route_classify() and picks ONE handler:
 *   ROUTE_SYSFACTS -> sysfacts_answer() renders the answer from a host-whitelisted struct of real
 *                     PA state (uptime / boot_id / model / nodes / a coarse health bit).
 *   ROUTE_DECLINE  -> the canned honest route_decline_text() for a status-shaped query the box has
 *                     NO source for (cpu% / disk / wall-clock / temperature) — NEVER routed to the
 *                     model to be fabricated.
 *   ROUTE_INFER    -> the existing PB dispatch, byte-for-byte today's behavior (the SAFE default).
 *
 * THE LOAD-BEARING INVARIANT (goal doc §6c): SYSTEM-FACTS and DECLINE leave through the SAME
 * signed + audited + counted + episodic-written exit as INFERENCE, differing ONLY in where the
 * answer text came from. There is NO parallel reply path — a bespoke net_build_udp_unicast for a
 * locally-served answer would reopen the M4b spoofing hole and blind the JACT audit.
 *
 * SYSFACTS/DECLINE deliberately bypass the §5-F PB_DISPATCH_OK() gate: they are rendered from PA
 * state and need no Process B, so "how long have you been up?" is still answerable while PB is
 * degraded. They also skip the duty window (no inference runs) and never call
 * km2b_miss_on_pb_ack() — a locally-served answer is NOT evidence of PB contact, and feeding the
 * hang detector a fake ACK would mask a real wedge.
 *
 * HONEST CEILING (route.h): a COARSE KEYWORD classifier over a DEFINED handler set, NOT semantic.
 * The >=95% is measured on a KEYWORD-BLIND held-out suite (routing_suite.h), NOT production
 * traffic. An embedding mechanism is a separate later arc (goal doc §8).
 *
 * Requires CONTROL_IN (it is the control-IN route) and ACTIONS (the shared exit writes the JACT
 * audit record).
 *
 * DEFAULT-ON since the B FLIP 2026-07-23 (supervised, bare metal, boot_id=38): the deployed box
 * routes control-IN queries for real. SYSFACTS proven live-state by a value that ADVANCES with
 * elapsed time across sends; DECLINE canned-not-fabricated; INFER coherent. The flip ran TWICE —
 * attempt 1 (boot 37) was ABORTED when the operator's improvised V-INFER question was wrongly
 * DECLINEd by the then-bare-noun rule (fixed in 5224a85: DECLINE now requires a status anchor).
 * Setting it back to 0 compiles the whole block out (route.c stays linked but inert). */
#ifndef JARVIS_ROUTING
#define JARVIS_ROUTING 1
#endif
#if JARVIS_ROUTING && !(JARVIS_CONTROL_IN && JARVIS_ACTIONS)
#error "JARVIS_ROUTING wires into pa_ctrl_gate and shares its signed+AUDITED exit -> requires JARVIS_CONTROL_IN && JARVIS_ACTIONS"
#endif

/* 6-6/B/M1 routing probe (box/KVM only; KVM has no NIC, so queries are staged through the same
 * signed-JCTL split pipeline the CONTROL_IN_PROBE / RECALL_PROBE modes use). Routes THREE synthetic
 * control-IN queries — one per class — and asserts each [ROUTE] line plus the shared exit:
 *   SYSFACTS "how long have you been up?"  -> answered from PA state (no PB dispatch)
 *   DECLINE  "what is your cpu usage?"     -> the canned decline (no PB dispatch)
 *   INFER    "what is a page fault?"       -> Gemma, today's behavior
 * PROBE==2 additionally latches g_pb_dead FIRST and re-runs the SYSFACTS + an INFER leg, proving
 * SYSFACTS still answers while INFER correctly degrades. Its OWN flag (the per-probe precedent);
 * #error-guarded off the other *_PROBE flags, which fire synthetic anomalies / real respawns / floor
 * resets in the same boot window. Default 0 -> compiles out (deploy induces no synthetic queries). */
#ifndef JARVIS_ROUTING_PROBE
#define JARVIS_ROUTING_PROBE 0
#endif
#if JARVIS_ROUTING_PROBE && !JARVIS_ROUTING
#error "JARVIS_ROUTING_PROBE requires JARVIS_ROUTING"
#endif
#if JARVIS_ROUTING_PROBE && (JARVIS_CONTROL_IN_PROBE || JARVIS_CONTROL_IN_RECALL_PROBE || JARVIS_MONITOR_PROBE || JARVIS_WAKE_PROBE || JARVIS_PROACTIVE_PROBE || JARVIS_ACTION_PROBE)
#error "JARVIS_ROUTING_PROBE must not co-run with the other *_PROBE flags (replay-floor resets / synthetic-anomaly / respawn / terminal g_pb_dead collisions)"
#endif

/* ── Phase C (semantic embedding) / C/M1a: the Qwen3-Embedding-0.6B host path (default: 0) ──────
 * When 1, the engine gains the qwen/embed tokenization + (Stage 2) embed-mode forward for the
 * co-resident Qwen3-Embedding-0.6B model used by the semantic-recall lane (Phase C). STAGE 1
 * (this milestone) is TOKENIZATION ONLY: the qwen GPT-2/qwen2 pre-split + the add_eos append, on
 * the qwen/embed path ONLY, gated so the DEPLOYED SentencePiece/Gemma tokenizer is byte-identical
 * at EMBED=0. The parity GATE (host, model-gated, NOT CI) is the 15-probe token-id match vs
 * `phase3/scripts/embed/golden_meta.json`. Stage 2 (RoPE-NEOX + the gated embed-forward + vector
 * parity) is a separate milestone gated on Stage 1 GREEN. Default 0 -> the whole qwen/embed path
 * compiles out; the deployed engine is unaffected. (Box wiring — a co-resident 2nd GGUF, MSG_EMBED
 * IPC, OFF object-identity — is C/M1b, and adds the ACTIONS/CONTROL_IN deps + #error guards then.) */
#ifndef JARVIS_EMBED
#define JARVIS_EMBED 0
#endif

/* Phase C / C/M1b-2 embed parity+latency probe (box/KVM-only, default 0; requires JARVIS_EMBED).
 * Runs the 15 C/M1a golden probes through the on-box embed forward and dumps each 1024-d vector
 * as raw hex plus a per-embed cycle count, so the box result can be compared against the HOST
 * harness's own output from ea1baf0 (NOT against the loose 1.27e-3 golden floor — see the C/M1b
 * design). Serial-heavy (~120 KB) by design: a checksum would prove bit-identity or nothing, and
 * the expected divergence is ~1e-6 because PB's forward is THREADED (JARVIS_SEL4_SMP =>
 * JARVIS_PARALLEL) while the host harness was single-threaded, so the magnitude must be visible. */
#ifndef JARVIS_EMBED_PROBE
#define JARVIS_EMBED_PROBE 0
#endif
#if JARVIS_EMBED_PROBE && !JARVIS_EMBED
#error "JARVIS_EMBED_PROBE requires JARVIS_EMBED"
#endif

/* C/M1b pre-fix induction probe (box/KVM-only, default 0). The two fail-closed branches on the
 * model-load path are UNREACHABLE on a healthy box (one model fits comfortably), so shipping them
 * untested would be shipping an unproven error path. The flag VALUE is a MODE (the WAKE_PROBE /
 * CONTROL_IN_PROBE precedent), and the two modes are mutually exclusive BY CONSTRUCTION: mode 1
 * aborts the frame allocation, which leaves nvme_model_loaded == 0, so PB is spawned model-less and
 * the mapping loop mode 2 targets never runs.
 *   1 = force `alloc_fail` — abort the frame-allocation loop early (pre-fix 1a)
 *   2 = force one `merr`   — fail one page map into PB (pre-fix 1b)
 * Induces a DEGRADED box on purpose: inference is refused for the whole boot. Never deploy != 0. */
#ifndef JARVIS_MODEL_FAIL_PROBE
#define JARVIS_MODEL_FAIL_PROBE 0
#endif
#if JARVIS_MODEL_FAIL_PROBE && (JARVIS_MODEL_FAIL_PROBE != 1 && JARVIS_MODEL_FAIL_PROBE != 2)
#error "JARVIS_MODEL_FAIL_PROBE must be 0, 1 (frame-alloc) or 2 (partial-map)"
#endif

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
