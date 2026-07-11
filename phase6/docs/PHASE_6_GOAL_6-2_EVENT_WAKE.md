# Phase 6 Goal 6-2 — Event-Driven Wake (PLAN-FIRST)

**Status: APPROVED (strategist verdict 2026-07-10; O1–O5 resolved — see §11) — M0 ✅ DONE 2026-07-10
(the host-pure wake decision core, see §8); M1 (box wiring, gated `JARVIS_WAKE` default-0) is next.**
**Depends on:** keystone K (✅ COMPLETE 2026-07-08 — the action spine is live in deploy: static allowlist +
`shield_assess` + `trust_policy` + JACT audit, `JARVIS_ACTIONS` default-ON since `34a165e`) and goal 6-1
(✅ COMPLETE 2026-07-09 — the always-on monitors are live in deploy, `JARVIS_MONITORS` default-ON since
`161acd3`; `monitor_event_t` was designed at 6-1/M0 as the deliberate 6-2/6-3 seam).
**Mirrors:** `PHASE_6_GOAL_6-1_MONITORS.md` + `PHASE_6_GOAL_K_IT_ACTS.md` (plan-first, milestones, honest
ceiling). Authored 2026-07-10.
**Pre-mortem-hardened 2026-07-10** (the K/M2a-2 precedent): a 3-agent adversarial review (starvation/
mechanics, security boundary, cite+convention verification) before this doc went to review. The DIRECTION
held; two HIGH mechanics landmines and six security/convention disciplines were folded in — the material
ones are the three deliberate DEVIATIONS from the workload dispatch discipline (Locked decision 5: a
fault mid-wake must audit FAIL, never `goto next_query`; a wake timeout must NEVER bump `q_errors` — it
would feed the very watcher that fired the wake, a self-amplifying loop; the duty window must be folded,
not clobbered), the stage-time template filter (Locked decision 3), `TRUST_NOTIFY` + a real `query_key`
(§5), and a self-contained `JARVIS_WAKE_PROBE` (Locked decision 9).

All line numbers below verified against HEAD (`2123c35`) at authoring time (they SHIFT — re-grep before
relying on any).

---

## 1. Canon + honest reading

ROADMAP canon (`phase4/docs/ROADMAP.md` §Phase 6, goal 2, verbatim):

> **Event-driven wake** — Monitors trigger Process A → cache lookup or inference when thresholds crossed.
> No constant polling of the LLM.

Three honesty corrections this doc commits to, up front:

1. **"Wake" is metaphorical.** PA busy-polls by design (`seL4_Yield` loop; CPU% is fiction — see
   `system-page-honest-metrics`); nothing sleeps and nothing is woken. 6-2 is **event-triggered
   DISPATCH**: a debounced monitor crossing triggers a consult (cache lookup, else one bounded inference)
   that would otherwise never happen. "Monitors trigger Process A" reads honestly as: the monitors —
   which already run *inside* Process A — trigger a consult lane inside the same workload loop.
2. **"No constant polling of the LLM" is a claim about the WAKE lane, not the box.** The deployed box
   runs a synthetic self-driven workload that exercises the cache and PB constantly (the load generator /
   soak instrument — 70% cache lane, 15% inference lane, `main_x86.c:3700-3709`). 6-2's honest claim is
   scoped: **the monitor→consult lane invokes the cache/LLM ONLY on a fire-once monitor crossing, never
   per tick** — a handful of integer compares per [STATS] window when no event fires, zero LLM contact.
3. **The canon phase done-when — "at least one proactive action fired correctly without user prompt
   (logged + correct)" — was already met at K/M4** (the PB-restart, per `PHASE_6_GOAL_K_IT_ACTS.md` §7)
   and is deliberately re-proven at 6-3 scale with real butler behaviors (K-d). **6-2's own done-when is
   the MECHANISM proof**: event → gate → cache-first consult → result → audit, end-to-end, exactly once
   per crossing — with exactly ONE demonstrator. 6-2 does not claim a butler behavior.

## 2. Scope boundary — 6-2 vs 6-3

**6-2 IS:** the wake mechanism — a static, human-reviewed **query-template allowlist** keyed by
`monitor_event_type_t`; a **cooldown/budget gate** (the anti-loop state machine); ONE dispatch site in
Process A that routes a fired event through the K spine to **cache-lookup-first → inference-on-miss**;
result → episodic record + JACT audit + serial proof + (M3) telemetry/console. Plus exactly **ONE
demonstrator wake** proven end-to-end.

**6-2 IS NOT:** the ≥5 butler behaviors (daily briefing, low-disk warning, anomaly alert … — **goal
6-3**, which CONSUMES this mechanism); acting on the model's answer (any action derived from a consult
result must be SELECTED from the action allowlist by id — 6-3+, never free-form); the RTC/wall-clock
slice (6-3's briefing needs it; deferred per 6-1 Locked decision 1); anything inbound (control-IN = 6-5;
6-2 adds NO network surface); a new monitor (the watcher set is 6-1's).

## 3. Ground truth (verified against live code)

What exists TODAY — the seam 6-2 extends:

- **A monitor event today terminates at NOTIFY.** `mon_notify` (`main_x86.c:1852-1879`):
  `monitor_build_snapshot` → `spine_decide(ACTION_NOTIFY_ANOMALY, …)` → EXECUTED branch = one
  `[ANOMALY]` serial line (`:1867`) + one JACT record via `spine_record` (`:1869`) +
  `g_monitors_fired++`/`g_last_monitor_event` (`:1871-1872`). **Nothing downstream** — no consult, no
  cache lookup, no inference. The EXECUTED branch is 6-2's hook point.
- **The K spine** (shared assess+audit funnel): `spine_decide` (`main_x86.c:1821-1830` — pure:
  `shield_assess` → `action_lookup` → `trust_policy(verdict, trust, false)`) + `spine_record`
  (`:1834-1845` — the counter bumps + exactly ONE JACT record per resolved event). Callers keep their own
  execute bodies at their call sites (the 6-1/M1 scoped extract — do NOT unify).
- **The watcher tick** runs inside the `[STATS]` q%100 block (`main_x86.c:4334-4476`; `mon_notify` call
  sites `:4409/:4416/:4440/:4443/:4447/:4456`), immediately after `jarvis_telemetry_emit` (`:4332`).
  ALL staging a wake could ever do happens inside this block — a pending wake is staged and consumed in
  the SAME iteration, never carried across iterations.
- **The static-id boundary:** `action_allowlist.h:25-28` — ids `ACTION_RESTART_PB=1`,
  `ACTION_NOTIFY_ANOMALY=2` (append-only, never renumber); `action_lookup` (`:51`) refuses unknown ids
  before SHIELD. `shield_action.h`: `shield_assess` (`:80`) scans `action_ctx_t.trigger` length-bounded
  against the ONE canonical immutable keyword blocklist (9 words, `:12-23`) and consumes
  `ctx.query_key` ONLY as the learned-risk lookup key; `SHIELD_BLOCK_THRESHOLD_X100=80`, base risks
  SELF_HEAL=10/NOTIFY=0/PROBE_HIGH=75 (`:45-48`); `trust_policy` (`:105` — AUTO→EXECUTE and
  NOTIFY→ACT_NOTIFY both auto-execute; REQUEST/REQUIRE degrade to PROPOSE_LOG/REFUSE pre-control-IN).
- **The cache:** `cache_lookup(&g_cache, normalized, action, sizeof action, &trust)`
  (`decision_cache.h:99-103`; workload use `main_x86.c:3734`); keying =
  `cache_normalize_query` (`decision_cache.h:81`, `MAX_QUERY_LEN` 128) — **the full normalized query
  text IS the key** (FNV-1a, `cache_hash` `:88`). `MAX_ACTION_LEN` 256; `CACHE_SIZE` 512. The #6/M3a
  read-only cache-serve-before-infer precedent: `main_x86.c:3777-3794` (a HIT serves <1 ms, recorded
  `EPI_ACT_CACHE`; a MISS falls through to inference unchanged). Cache "action" strings and the action
  ALLOWLIST are DISJOINT namespaces — cache text is only ever ECHOED (logged/sent as response text),
  never executed; every spine dispatch uses a numeric literal action id (`:1864`, `:1897`).
- **The inference dispatch discipline** (the pattern a wake adapts — with the §7.5 deviations): the
  `PB_DISPATCH_OK()` degraded-state guard (`main_x86.c:580`, applied `:3801` — never dispatch to a dead
  PB); `shmem_ipc_send(shared_request_ring, MSG_QUERY, seq++, query, len)` (`:3930`, `shmem_ipc.h:24`,
  payload ≤ `SHMEM_MAX_PAYLOAD` 240; `seq` is a workload-loop local, `uint16_t`, in scope at the
  [STATS] site) + `seL4_Signal(req_notif)` (`:3935`); the inline poll spin (`:3956-4017`): drains
  `MSG_INFER_STATS`/`MSG_DEBUG`/`MSG_RESPONSE`, ticks `jarvis_telemetry_tick()` (`:4003`) and
  `pa_fault_check()` (`:4008` — which the LANE answers with `goto next_query`), `POLL_TIMEOUT`
  5,000,000 polls ≈ 60-120 s (`:3953`); on timeout the LANE does `q_errors++` +
  `km2b_miss_on_pb_timeout(…, KM2B_LANE_INFERENCE)` (`:4019-4030`); on a genuine response
  `km2b_miss_on_pb_ack` + `g_pb_last_ack_ms` (`:4035-4036`). The lane's duty accounting opens
  `g_infer_active`/`g_infer_t0` at `:3928-3929` and closes ONLY at `next_query:` (`:4662-4665`). An
  inference occupies PA for ~9-12 s on the box (5.46 tok/s, 50-token cap) up to ~55 s in KVM.
- **The preamble staging hazard:** the G3 retrieval pack (`main_x86.c:3818-3899`) writes the shared
  staging buffer via `sctx_pack_preamble` (`:3880`) before EVERY workload inference; PB's `handle_query`
  injects whatever is staged at generation time (`inference_server.c` — `pre_len==0` ⇒ no inject).
  `sctx_pack_preamble(c, NULL, 0)` clears it (`shared_context.h:116-118`). **A wake dispatch that does
  not explicitly manage the staging buffer would inject the PREVIOUS workload query's preamble into the
  wake inference** (wrong-key contamination — the exact P6 class the G3/M6 fixes killed).
- **Episodic recording:** `epi_batch_add(query, action, outcome, resp)` (`main_x86.c:294`,
  `EPI_BATCH_MAX` 128 `:189`); action codes `EPI_ACT_CACHE=1`/`EPI_ACT_INFER=2`
  (`episodic_store.h:60-61`). Usable-filters downstream (`g3_candidate_usable`, the #6 promotion pass)
  key on `EPI_ACT_INFER + EPI_OUT_OK + resp_len>0`.
- **JACT:** `action_audit.h` — verdicts `:54-56`, outcomes `:59-61`, `ACT_TRIGGER_MAX` 456 `:66`, store
  @ LBA 21,120,000 × 4096 records (`:31-32`).
- **Telemetry:** v8 = 236 B, CRC@232, `JARVIS_TLM_VERSION 8` (`jarvis_telemetry.h:23`), flags used
  through `TLM_F_MONITORS` 0x1000 (`:38`); v8 fields @228-231 (`:90-92`). Next slot: **v9, 0x2000**.
- **Flags:** `jarvis_debug.h` — `JARVIS_ACTIONS 1` (`:151`), `JARVIS_MONITORS 1` (`:172`),
  `JARVIS_MONITOR_PROBE 0` (`:185`).

## 4. Architecture — the reactive extension of the spine

```
   monitor crossing (6-1: debounced, fire-once — monitor_t, host-proven)
        │
        ▼
   mon_notify EXECUTED branch (main_x86.c:1865-1872 — [ANOMALY] + JACT, as today)
        │  + NEW: stage a pending wake — STAGE GUARD: only if
        │    wake_template_lookup(type) != NULL (an unmapped/benign event can
        │    NEVER occupy the slot); one-slot latch carrying {event_type} ONLY
        │    (never the snapshot); a second TEMPLATED event in the same window
        │    is DROPPED + counted (wake_dropped), never queued
        ▼
   the ONE wake dispatch site (end of the same [STATS] block, after epi_commit —
   zero cost on the other 99 iterations by construction; PA is single-threaded,
   so at most ONE wake is ever in flight; staged + consumed in the SAME iteration)
        │
        ▼
   wake gate (host-pure M0 module): per-type COOLDOWN + global BUDGET checked
        │  BEFORE the spine (a suppressed wake is a non-event: counted, never
        │  audited); wrap-safe arithmetic (§6)
        ▼
   spine_decide(ACTION_WAKE_CONSULT,
                ctx = { trigger = the TEMPLATE TEXT, trigger_len,
                        query_key = cache_hash(normalize(template)) },
                learn map when JARVIS_SHIELD_LEARN)
        │  — the payload about to leave for the cache/LLM is exactly what
        │    SHIELD scans; templates are host-pinned keyword-clean (§5);
        │    a real query_key makes the learned-risk feed non-vacuous (§5)
        ▼
   route:  cache_normalize_query(template) → cache_lookup   ── HIT ──▶ result (<1 ms)
        │                                                                │
        └── MISS ──▶ PB_DISPATCH_OK()? ── no ──▶ outcome FAIL (route=none)
                          │ yes                                          │
                          ▼                                              │
                    fold any OPEN duty window (§7.5c), then open the wake's
                    sctx_pack_preamble(g_sctx, NULL, 0)   /* clear — LAST
                          staging write before the send (§7.6) */
                    MSG_QUERY → the poll-spin discipline WITH the §7.5
                    deviations: a fault mid-wake sets wake_faulted + breaks
                    (NEVER goto next_query — the wake must still audit);
                    a timeout NEVER bumps q_errors (anti-self-amplification);
                    km2b_miss_on_pb_timeout uses a new KM2B_LANE_WAKE           │
                          │                                              │
                          ▼                                              ▼
   [WAKE] serial line (+ first-2 verbatim [WAKE-RESP] heads) + epi_batch_add
        ▼
   spine_record(ACTION_WAKE_CONSULT, EXECUTED, OK/FAIL,
                trigger = "wake <event-literal> route=<cache|infer|none> ms=<n>")
        │       — reached on EVERY dispatched wake, incl. fault/timeout/degraded
        ▼
   (M3) telemetry v9: wakes_fired / last_wake_event + TLM_F_WAKE → console
```

The consult result is **INFORM-ONLY text**: it goes to serial, the episodic store, and (as a count) the
wire. It has **no actuator** — nothing parses it, nothing executes from it (verified: cache/episodic
text is only ever echoed; every spine dispatch uses a numeric literal id — §3). Acting on a consult is
6-3+ and must arrive as an allowlisted action id through the same spine (K-b).

## 5. Security boundary (this phase's character)

- **The wake query comes from a STATIC, human-reviewed template allowlist** keyed by
  `monitor_event_type_t` — the same structural prompt-injection boundary as K's action allowlist
  (K-b): the system SELECTS a fixed template; **nothing synthesizes query text from free/untrusted
  input**. Not the model, not the event snapshot, not retrieved memory, not (ever) inbound bytes.
  **The load-bearing invariant:** `wake_template_lookup(monitor_event_type_t)` takes the event TYPE
  only — never a snapshot pointer — and the pending latch carries `{event_type}` only, so no free-text
  path into template selection can even be plumbed. M0 pins it: the built wake query for a type is
  BYTE-IDENTICAL regardless of the event's `v1`/`v2` values.
- v1 proposal: templates for the two degradation events only (`MON_EV_ERROR_RATE`,
  `MON_EV_SELF_HEAL_RATE`); the benign liveness events (`MON_EV_STORE_WRAP`,
  `MON_EV_UPTIME_MILESTONE`) deliberately map to **no wake** — waking the LLM to observe an uptime
  milestone would be noise dressed as intelligence. `MON_EV_HEARTBEAT_AGE` (enum 4) is unwired in 6-1
  (deferred watcher) ⇒ NULL until it ever becomes live. The M0 truth-table pins every enum value.
- **The template text is FIXED — no interpolation, not even the event's decimals** (Locked decision 2
  explains why: cache-key stability). The event's numbers already travel in the `[ANOMALY]` snapshot and
  its JACT record (6-1); the wake JACT record carries route + latency.
- **Keyword-clean, both ways, host-pinned** (the `monitor_build_snapshot`/`km2b_build_trigger`
  discipline): every template must scan clean against the canonical 9-keyword blocklist AND pass the
  REAL `shield_assess(ACTION_WAKE_CONSULT, {template, len, key}, NULL, 0)` un-BLOCKED — a wake must
  never block itself. **Template-authoring hazard: the scan is a case-insensitive SUBSTRING match**
  (`shield_action.c` `contains_keyword`) — innocent diagnostic words self-block: "**skill**" contains
  "kill", "**information**"/"informative" contains "format". A human review for blocklist WORDS misses
  these; the both-ways host pin is the load-bearing catch, not the reviewer. (The shield workload lane's
  queries ARE blocklist keywords — `shield_queries[]` text must never reach a wake trigger, which the
  fixed-template rule guarantees by construction. Separately verified: a wake can never block the
  self-heal — `pa_restart_pb` passes `learn=NULL` and builds its own trigger, no shared path.)
- **The model's answer NEVER enters a trigger/snapshot.** Model output is untrusted text (it can contain
  blocklist keywords); it goes ONLY to serial, `epi_batch_add`'s `resp`, and nowhere near
  `action_ctx_t.trigger` or a JACT `trigger_snapshot`.
- **Trust:** the wake is an INFORM/CONSULT action — new allowlisted id **`ACTION_WAKE_CONSULT = 3`**
  (append-only, `action_allowlist.h:25-28` precedent), **`TRUST_NOTIFY` (L1)** — functionally identical
  to AUTO (both auto-execute, and the `[WAKE]` line IS the notify), but honest and monotonic in impact:
  a consult burns 10-120 s of the box and writes episodic/cache state, so it must not sit BELOW the
  self-heal restart (also `TRUST_NOTIFY`) in ceremony. New base-risk enum **`ACTION_CLASS_CONSULT`
  (=3), mapped to base risk 10** in `shield_action.c` (SELF_HEAL parity; 10 + the learned cap 50 = 60 <
  80, so learning alone never blocks a consult — but see next bullet for why the learned feed still
  matters).
- **The learned-risk feed must be real, not ceremonial:** `shield_assess` consumes `ctx.query_key` ONLY
  to look up the learned adjustment, and an absent key adjusts 0 — so a wake passing `query_key=0`
  would make "learned risk feeds the gate" fiction. The wake passes
  `ctx.query_key = cache_hash(cache_normalize_query(template))` (the #1/#3/#5/#6 key currency) and the
  `g_shield_learn` map under `JARVIS_SHIELD_LEARN`: a repeatedly-FAILING consult visibly raises its
  key's risk in `[SHIELD-LEARN]`/JACT even though it can never self-block. (Noted in passing: today's
  `mon_notify` passes `query_key=0` — a pre-existing latent gap in the NOTIFY path's learned feed,
  harmless at base 0 but worth a later cleanup; NOT changed by this goal.)
- **Cross-lane key hygiene:** wake answers are deliberately ordinary episodic INFER records (§7.7), so
  they are live retrieval/#6-promotion candidates. G3 injection is exact-key only — contamination into a
  WORKLOAD prompt would require a wake template normalizing to the SAME key as a workload query. M0 pins
  key disjointness: every wake template key differs from every `inference_queries[]`/`shield_queries[]`
  key (and cache-pattern keys stay distinct by text). Residual exposure even on a future collision =
  text-only-in-prompt (no execution path — §3/§4).
- **NO new inbound surface.** Monitor events derive from PA-observable state only; I211 RX stays virgin
  (control-IN = 6-5).

## 6. Anti-loop / rate-limit (critical — an inference is ~seconds)

An un-bounded wake loop could occupy PA with back-to-back ~10-60 s inferences and starve the workload.
Layered bounds, outermost first:

1. **Inherited fire-once-per-crossing** (6-1): `monitor_t` debounce + fire-once latch + re-arm-on-clear
   hysteresis — a sustained condition produces ONE event, hence at most one wake, until it clears and
   re-crosses (host-proven, `test_monitors.c` 41/41).
2. **No self-amplification, structurally (pre-mortem HIGH):** the error-rate watcher is fed from
   `q_errors` (`main_x86.c:4394`), and the WORKLOAD lane's timeout bumps `q_errors` (`:4023`) — so a
   wake timeout that copied that bump would inflate the next window's delta, re-cross the watcher, and
   stage another wake: a feedback loop rate-limited only by cooldown. **A wake failure is therefore
   NEVER a workload error: the wake's timeout path records outcome FAIL + feeds the miss-counter and
   nothing else** (Locked decision 5b). The wake lane cannot feed the watcher that fires it.
3. **One-slot pending latch:** at most ONE wake staged per [STATS] window; only TEMPLATED events touch
   the latch (stage guard — a benign event can neither wake nor shadow a real one); a second templated
   same-window event is dropped + counted (`wake_dropped`, serial-visible). PA is single-threaded — a
   wake dispatch is synchronous inside the loop, so concurrency is structurally impossible.
4. **Per-type cooldown:** after a wake fires for event type T, further T-wakes are suppressed for
   `WAKE_COOLDOWN_MS` (proposal **600,000 ms = 10 min**; M2 calibrates from real telemetry, the 6-1
   threshold discipline — never guessed constants shipped as final). Wrap-safe by construction:
   `uint32` elapsed via subtraction (`(uint32_t)(now - last) < WAKE_COOLDOWN_MS`), never absolute marks
   (`jarvis_uptime_ms` wraps ~49.7 d).
5. **Global budget:** at most `WAKE_BUDGET_PER_HOUR` (proposal **4**) wake dispatches per uptime-hour
   bucket, all types combined — implemented as a bucket INDEX (`now_ms / 3,600,000`) whose change
   resets the count (wrap-safe), never a monotonic threshold. Exhausted budget = suppressed + counted.
   The budget is NOT dead code: the degradation watchers re-arm on clear (error-rate/heal-rate can
   legitimately re-cross within an hour); only the benign fire-once types would make it moot, and those
   never reach the latch (bound 3).
6. **The dispatch itself is the existing bounded machinery** (with the §7.5 deviations):
   `PB_DISPATCH_OK()` (never consult a dead PB), `POLL_TIMEOUT` (a wedged PB costs one bounded timeout,
   feeding the K/M2c miss-counter under a new `KM2B_LANE_WAKE` — correct: a wake timeout IS a genuine PB
   contact miss, and it may legitimately contribute to the `next_query` hang-trip the same iteration),
   `pa_fault_check` in the spin (a PB crash mid-wake self-heals exactly like a workload inference — but
   the wake still audits FAIL, Locked decision 5a).

**Worst case, healthy box:** 6 event types × fire-once, gated to ≤4/hour — the wake lane adds at most 4
inference-lengths (~40-240 s) per hour to a loop that already runs ~15% inference lanes; the workload is
delayed, never starved. **Cost when idle:** structurally ZERO outside the [STATS] cadence (the gate lives
inside the q%100 block); the honest metric is **added µs/event** for the decision path (template select +
cache_lookup, sub-ms) — inference seconds are paid ONLY on a real event with a cache miss. NEVER a CPU%
claim (fiction; PA busy-polls).

## 7. Locked decisions

1. **The wake rides the K spine unchanged** — `spine_decide`/`spine_record`, a new allowlisted id
   `ACTION_WAKE_CONSULT=3` @ **`TRUST_NOTIFY`** (pre-mortem: monotonic with the restart's NOTIFY —
   functionally identical to AUTO pre-control-IN, but honest for a compute-burning action), new enum
   `ACTION_CLASS_CONSULT` (=3) mapped to base risk 10. **One JACT record per DISPATCHED wake on EVERY
   exit path** — cache hit, coherent inference, timeout, fault-mid-wake, degraded (outcome
   FAIL/route=none); a cooldown/budget-suppressed wake is a non-event (counted, not audited — no JACT
   spam).
2. **The template text is FIXED per event type — the cache key IS the template.** Interpolating the
   event's numbers into the query would change the normalized key every time → the cache could NEVER
   hit → every wake becomes an inference → the canon "cache lookup or inference" collapses to
   "inference always." Fixed text ⇒ a stable FNV-1a key ⇒ repeat wakes of a type become cache-served
   (via #6's existing promotion of repeated usable INFER answers — see Open question O3 and M2's
   timeline). Numbers stay in the `[ANOMALY]`/JACT records where they already live. M0 pins
   byte-identity of the built query across arbitrary event values.
3. **The template filter applies at STAGE time, in `mon_notify`'s EXECUTED branch** (pre-mortem):
   `wake_template_lookup(type) != NULL` guards the latch, so a benign/unmapped event can neither wake
   nor occupy the slot ahead of a real degradation event firing later in the same window (the watcher
   block fires in code order — err-rate, heal-rate, wraps, uptime — and uptime/wrap events must not
   shadow an err-rate wake). `wake_dropped` counts only templated drops. The latch carries
   `{event_type}` ONLY (§5). v1 maps only the degradation events; benign liveness events map to NULL.
4. **`shield_assess` scans the actual dispatch payload, with a REAL key:** at assess time `ctx.trigger`
   = the template text (what's leaving for the cache/LLM) and `ctx.query_key` =
   `cache_hash(cache_normalize_query(template))`, learn map passed under `JARVIS_SHIELD_LEARN` (§5 —
   else the learned-risk feed is vacuous). The JACT `trigger_snapshot` = the system-facts wake trigger
   (`"wake <event-literal> route=… ms=…"`, `km2b_build_trigger` discipline, host-pinned). The assessed
   bytes remain reconstructable from the record: `action_id=3` + the snapshot's event literal map 1:1
   (compile-time) to the exact template — M0 pins the reconstruction (every event literal ↔ its
   template). The model's answer never enters either (§5).
5. **The wake reuses the inference dispatch DISCIPLINE with exactly THREE deliberate deviations**
   (pre-mortem HIGHs — a verbatim copy of the lane imports two bugs):
   **(a)** a fault mid-wake does NOT `goto next_query` (the lane's `:4008` answer): the wake spin sets
   a local `wake_faulted`, breaks, and STILL reaches `spine_record(…, AUDIT_OUT_FAIL)` — a dispatched
   wake is audited on every exit path (decision 1), while `pa_fault_check` inside the spin still
   funnels the self-heal exactly as the lane does.
   **(b)** a wake timeout NEVER bumps `q_errors` — outcome FAIL + `km2b_miss_on_pb_timeout` with a new
   `KM2B_LANE_WAKE` lane constant (honest hang-lane attribution), nothing else. §6 bound 2 is the why.
   **(c)** duty-window fold: the [STATS] block can run with the workload lane's `g_infer_active` still
   open (the lane closes only at `next_query:`, `:4662-4665`) — the wake must FOLD any open window
   (`g_infer_cycles += now - g_infer_t0`) before opening its own, never clobber `g_infer_t0`.
   Everything else is the lane's machinery unchanged: `PB_DISPATCH_OK()`, `MSG_QUERY` +
   `seL4_Signal`, the drain/tick/fault-check spin, `POLL_TIMEOUT`, `km2b_miss_on_pb_ack` on a genuine
   response. One hygiene pin (pre-mortem, LOW): the wake DRAINS the response ring before its
   `MSG_QUERY` (the lane pattern), so a stale `MSG_RESPONSE`/`MSG_INFER_STATS` can never be misread as
   the wake's reply — and the property that keeps the ring clean at the wake site today is
   load-bearing and fragile: a FAULTING inference lane `goto next_query` skips the ENTIRE [STATS]
   block, so the wake never dispatches on an iteration whose lane left the IPC mid-drain. A refactor
   that moves the wake site, or makes a lane fault fall through instead of `goto`, must re-check both.
6. **The preamble staging buffer is explicitly CLEARED (`sctx_pack_preamble(g_sctx, NULL, 0)`) as the
   LAST staging write before a wake `MSG_QUERY`** — a stale workload preamble must never inject into a
   wake inference (§3 hazard; adequacy verified: PA is single-threaded and the spin never re-packs).
   Retrieval-on-wake (exact-key build-on from the same template's prior answer) is deliberately
   DEFERRED (Open question O4) — v1 keeps the wake prompt fully deterministic.
7. **The wake result is recorded `EPI_ACT_INFER` / `EPI_ACT_CACHE`** (by route) via the existing
   `epi_batch_add` — reusing the memory pipeline: exact-key retrieval and #6 cache-growth promotion then
   apply to wake answers with zero new machinery (this is HOW "cache lookup or inference" becomes true
   over time). A dedicated `EPI_ACT_WAKE` code would exclude wake answers from every existing usable
   filter — rejected for v1 (revisit only if wake records ever need segregating). Cross-lane key
   hygiene is pinned at M0 (§5).
8. **One dispatch site, decoupled from `mon_notify` by the one-slot latch,** placed at the END of the
   same [STATS] block (after `epi_commit` — the existing [STATS] passes run undisturbed; the wake's
   episodic record commits at the next cadence; staged + consumed in the same iteration, never carried).
   `mon_notify` stays cheap; the NOTIFY path is unchanged (an event still `[ANOMALY]`s + audits even
   when its wake is suppressed or unmapped). A wake timeout may feed the `next_query` hang-trip in the
   same iteration — acceptable and correct (a genuine consecutive miss), documented.
9. **Gated `JARVIS_WAKE` (new flag, `jarvis_debug.h`), default 0**, compile-guarded to require
   `JARVIS_ACTIONS && JARVIS_MONITORS` (it rides the monitor→spine path). Box-only `JARVIS_WAKE_PROBE`
   (default 0, its OWN flag — the MONITOR_PROBE precedent) is **self-contained**: it induces ONLY the
   error-rate WINDOW DELTA (the honest MONITOR_PROBE synthetic-delta technique — never the real
   counter) and shrinks `WAKE_COOLDOWN_MS` for gate runs; the M1/M2 gates must NOT ride
   `JARVIS_MONITOR_PROBE` (pre-mortem: its heal-rate probe fires 2 REAL respawns in the same [STATS]
   block, racing a staged wake against the post-respawn handshake). OFF = object-level byte-identical
   (`main.c.obj` `.text`/`.rodata`/`.data` + symbols — the 6-1/M2 lesson; NEVER md5-compare packed
   images). The default-ON flip is a deliberate decision AFTER box proof.
10. **Honest naming everywhere:** "event-triggered consult" / "wake dispatch" — never "thinking",
    "reasoning", "JARVIS decided", "autonomous". The M3 console honesty gate bans the fictions. One
    known seam, stated not hidden: a wake inference latches `MSG_INFER_STATS` like any inference, so
    the console's "live · last inference" throughput tile may reflect a wake's speed — honest (it IS
    the last inference), noted for the M3 console text.

## 8. Milestones

- **M0 (host/CI, pure — no box, no flags):** `phase3/src/ai/wake.c/h` + `test_wake.c` + CI step
  **"Phase 6: 6-2 Wake decision core (C)"** (the `monitors.c`/`km2b_miss.c` precedent: host-pure, NO
  `<sel4/sel4.h>`): the template table + `wake_template_lookup(monitor_event_type_t)` (NULL = no wake;
  takes the TYPE only — §5); the gate state machine (`wake_gate_t`: one-slot pending latch, per-type
  cooldown, hourly budget — pure, caller passes `now_ms`; wrap-safe semantics per §6.4/6.5); the
  system-facts wake-trigger builder. Allowlist/class additions land here too (`ACTION_WAKE_CONSULT=3`
  in `action_allowlist.c`, the `ACTION_CLASS_CONSULT` (=3)→10 mapping in `shield_action.c`) with their
  existing host suites extended. Host pins: template lookup truth table over EVERY enum value (mapped /
  unmapped / HEARTBEAT_AGE / out-of-range); EVERY template keyword-clean both ways (blocklist scan +
  un-BLOCKED through the REAL `shield_assess(ACTION_WAKE_CONSULT, …)` — the T7 discipline, and the
  SUBSTRING hazard is exactly what this catches); built-query BYTE-IDENTITY across arbitrary event
  values; template keys DISJOINT from every `inference_queries[]`/`shield_queries[]` key; the 1:1
  event-literal↔template reconstruction; templates fit `MAX_QUERY_LEN` after normalization AND
  `SHMEM_MAX_PAYLOAD`; cooldown/budget truth tables (suppress inside window, re-allow after, budget
  exhaustion + bucket-change reset, wrap-crossing cases, drop-on-pending); trigger builder
  keyword-clean + cap/canary.
  ✅ **DONE 2026-07-10 (TDD RED→GREEN):** `phase3/src/ai/wake.{c,h}` + `test_wake.c` (**9 PASS**,
  groups A–I exactly as pinned above — incl. the 23-seed key-disjointness mirror and the wrap cases) +
  `ACTION_WAKE_CONSULT=3` @ `TRUST_NOTIFY` / `ACTION_CLASS_CONSULT` (=3) → base 10 landed in
  `action_allowlist.c` / `shield_action.c` (their suites extended: allowlist **5 PASS** incl. the T5
  entry pin + count 3, shield gate **7 PASS** incl. the T7 CONSULT-class pin); sibling suites
  re-verified (km2b_trigger 11/11, monitors 41/41). CI step **"Phase 6: 6-2 Wake decision core (C)"**
  added. Host-only — nothing links into the deployed path (M1 is the first box wiring).
- **M1 (box, gated `JARVIS_WAKE` default-0):** wire the staging into `mon_notify`'s EXECUTED branch
  (stage guard, §7.3) + the ONE dispatch site (§7.8) with the full route (§4) including the three
  deviations (§7.5); `wake.c` added to `build_jarvis_x86.sh`'s `AI_FILES` sync + the CMakeLists
  PA-source injection (the `monitors.c` precedent). Box gate (KVM `-smp 6`, WAKE=1 + **WAKE_PROBE=1**
  — self-contained, MONITOR_PROBE stays 0): exactly ONE `[WAKE] … route=infer` with a coherent result
  head, the wake JACT record (`action=3 NOTIFY/EXECUTED/OK`) alongside the event's `action=2` NOTIFY
  record, workload advancing err=0 (and the wake dispatch itself provably NOT bumping `q_errors`), 0
  spurious restarts; OFF (deploy default) = object-level byte-identical + zero `[WAKE]` lines. **The
  minimal viable 6-2.**
- **M2 (box): anti-loop + cost proof.** Sustained/repeated induced crossings → exactly one wake per
  crossing; a re-crossing INSIDE the cooldown is suppressed + counted (serial-visible); the hourly
  budget caps a burst; an induced wake TIMEOUT leaves `q_errors` flat (the §6.2 anti-amplification
  proof) and audits FAIL; a degraded run (induced `g_pb_dead`) shows route=none + outcome FAIL, no
  dispatch, no timeout burn; duty accounting stays sane when q%100 lands on an inference iteration
  (§7.5c); the cache route proven end-to-end under the probe-shrunk cooldown — the promotion timeline
  is 3 crossings (1st wake infers; 2nd infers and its fold reaches freq≥2 → #6 promotes; 3rd serves
  `route=cache` <1 ms), which the 10-min production cooldown would stretch past any gate, hence the
  probe shrink (§7.9); measured added decision-path µs/event; deployed-config (WAKE=0) regression run
  clean. Cooldown/budget values re-calibrated from the measured runs.
- **M3 (telemetry v9 + console — REQUIRED for done, the UI-parity slice):** append
  `uint16 wakes_fired` + `uint8 last_wake_event` + `uint8 wake_pad` → **240 B, CRC@236, version 9**,
  +`TLM_F_WAKE` **0x2000** (PROPOSED pin — the v8 pattern verbatim: gated `#if JARVIS_WAKE` fill, a
  WAKE=0 deploy emits 0s + flag clear; `wakes_fired` bumped ONLY at the dispatch site, counting
  DISPATCHED consults — not suppressed, not refused). Full lockstep in ONE slice: `jarvis_telemetry.h`
  → `telemetry_receiver.py` (decode + `packet_to_record` BOTH — the KeyError lesson) →
  `telemetry_fixture.py` → `gen_golden_pcap.py` → `golden.pcap` regen → console (Capabilities
  "Event-driven wake (templated consults)" row + System "Event consults" / "Last wake event" stats,
  `—` until the flag is live; the throughput-tile seam noted per §7.10) → all test layers. Honesty gate
  bans "thinking"/"reasoning"/"decided on its own"; wording = "event-triggered consults — a fixed,
  human-reviewed question per monitor event; cache-served or one bounded inference".
- **Flip:** `JARVIS_WAKE` default-ON — deliberate, box-proven, after a supervised healthy run shows
  honest-0 wakes (no event ⇒ no wake ⇒ no false-positive LLM burn — exactly the point; the 6-1 flip
  precedent, incl. the on-wire v9 validation at the flip since QEMU has no NIC).

## 9. Storage / state

**No new store.** Wake decisions audit into the EXISTING JACT store (they ride the spine — same
`parse_action_audit.py` read-back, distinguishable by `action_id=3`); wake results persist in the
EXISTING episodic store (route-coded, §7.7). Gate state = a few dozen bytes of PA statics
(`wake_gate_t` + the one-slot latch). The v9 fields are the live surface.

## 10. Risks

- **Wake-loop starvation / self-amplification** — the layered bounds of §6, with the amplification path
  structurally severed (§6.2: wake failures never touch `q_errors`); the M2 gate proves suppression,
  budget, and the flat-`q_errors` timeout on the box, not just in host tests.
- **Prompt injection via the wake lane** — structurally closed by fixed templates selected on the event
  TYPE only (§5); the residual surface is the G3 preamble (workload-side, already P6/P7-hardened) which
  v1 wake dispatches explicitly CLEAR (§7.6), and the text-only cross-lane echo bounded by the M0 key-
  disjointness pin (§5). The pre-flip review re-checks that no free text can reach a wake query.
- **Stale cached consults** — a promoted wake answer serves from the cache indefinitely (until LRU/
  eviction); the "diagnosis" for THIS error spike may be last month's cached text. Honest framing: the
  cache route is canon behavior and the JACT record carries `route=cache`; the consult is a templated
  reference answer, not a live investigation (§13 ceiling). If this proves misleading in practice, 6-3
  can add per-behavior freshness policy — NOT solved here.
- **A wake mid-instability** — a self-heal-rate event fires precisely when PB is flaky; the wake then
  burns a `POLL_TIMEOUT` (~60-120 s) or dispatches to a just-restarted PB. Mitigations: `PB_DISPATCH_OK`
  + the timeout feeding the K/M2c miss-counter (`KM2B_LANE_WAKE`) is CORRECT (a genuine miss, honestly
  attributed) and may rightly contribute to a hang-trip; outcome FAIL is honest audit; `q_errors` stays
  untouched (§6.2); the budget caps repeat burns. M2's degraded-run gate covers this.
- **Template rot / key drift** — editing a template changes its cache key and orphans its episodic/cache
  lineage. Templates are compile-time consts, human-reviewed at PR; a changed template is a NEW consult
  by design (documented, not prevented; the M0 disjointness + reconstruction pins re-run at PR).
- **Telemetry honesty** — `wakes_fired` counts dispatched consults only (not suppressed, not refused);
  the single bump site is pinned (the `monitors_fired` discipline). The throughput tile may show a
  wake's tok/s (§7.10 — honest, noted).

## 11. Open questions — ALL RESOLVED (strategist verdict 2026-07-10)

- **O1 ✅ CONFIRMED — the demonstrator event:** `MON_EV_ERROR_RATE`, induced by the self-contained
  `JARVIS_WAKE_PROBE` (synthetic window-delta, the MONITOR_PROBE technique — its own flag; the
  pre-mortem showed the M1 gate must not ride MONITOR_PROBE's real-respawn probe).
- **O2 ✅ RESOLVED — cooldown/budget defaults:** 10 min per-type + 4/hour global stand as
  M2-calibration proposals (M0 defines `WAKE_BUDGET_PER_HOUR` 4), but **the FLIP starts the budget
  CONSERVATIVE (2/hour)** and relaxes only after observation — the flip commit may tighten the
  constant.
- **O3 ✅ RESOLVED (the one with risk) — cache seeding:** pure #6-promotion for v1 (zero new
  cache-write machinery), **BUT M2 makes "does #6 actually promote the wake key within the gate" an
  explicit PASS/FAIL checkpoint**, with a direct `cache_insert`-after-first-inference as the READY
  FALLBACK if promotion proves gate-infeasible. M0/M1 must not assume the cache route comes free.
- **O4 ✅ RESOLVED — retrieval-on-wake:** DEFERRED to v2/6-3 (v1 keeps the wake prompt fully
  deterministic).
- **O5 ✅ RESOLVED — consult result surfacing:** DEFERRED to v2/6-3 (v9 carries counts only; the
  result text lives in serial + episodic; a console consult-head surface is 6-3's briefing territory).

## 12. Done-when (6-2's own — the mechanism, not the canon phase claim)

- A REAL (probe-induced, honestly-labeled) monitor crossing drives the FULL path on the box: event →
  stage guard → gate → spine (`ACTION_WAKE_CONSULT`) → cache-lookup-first → inference-on-miss →
  coherent result → episodic record + JACT audit + `[WAKE]` serial proof + live v9/console surface —
  **exactly once per crossing**, with cooldown, budget, and drop-on-pending each demonstrated
  suppressing on the box.
- The failure paths are audited, never miscounted: a dispatched wake reaches `spine_record` on EVERY
  exit (fault/timeout/degraded ⇒ FAIL), and an induced wake timeout leaves `q_errors` flat.
- The cache route is demonstrated end-to-end at least once (`route=cache`, <1 ms serve of a promoted
  wake answer, under the probe-shrunk cooldown).
- OFF (`JARVIS_WAKE=0`) is object-level byte-identical; the default-ON flip is a deliberate, recorded
  decision after a supervised healthy run (honest-0 wakes on a healthy box).
- Exactly ONE demonstrator — the ≥5 behaviors remain 6-3's, and no 6-2 text claims the canon phase
  done-when (already K/M4's, re-proven at 6-3).

## 13. Honest ceiling

> 6-2 is **event-triggered dispatch of a PRE-TEMPLATED question** — the system consults its cache or
> its LLM only when a real, debounced, observed event fires, and what it asks is a fixed, human-reviewed
> string selected by event type. It is NOT autonomy, NOT free-form reasoning, NOT the system "deciding
> to think": the templates are static, the answer has no actuator, the whole lane is SHIELD-scored,
> JACT-audited, rate-limited, and honestly counted on the wire. The value proven here is the MECHANISM —
> monitors can now cause a consult instead of only a NOTIFY line — so that 6-3's real behaviors have a
> safe, bounded, audited channel to ride.

---

*Companion to `phase6/docs/PHASE_6_PLAN.md` (goal 6-2); rides `PHASE_6_GOAL_K_IT_ACTS.md`'s spine and
consumes `PHASE_6_GOAL_6-1_MONITORS.md`'s `monitor_event_t` seam. Ground truth verified against HEAD
(`2123c35`) at authoring; pre-mortem-hardened by a 3-agent adversarial review the same day (2026-07-10).*
