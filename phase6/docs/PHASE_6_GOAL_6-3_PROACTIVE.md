# Phase 6 Goal 6-3 — Proactive Actions: ≥5 Butler Behaviors (PLAN-FIRST)

**Status: APPROVED (strategist review 2026-07-12 — O1–O5 resolved, §12) — M0 DONE 2026-07-12
(host-only: the behavior registry + integer-only digest + the two append-only edits; NO box wiring,
NO flags, NO wire changes — M1 is next).**
**Depends on:** keystone K (✅ the live action spine: allowlist + `shield_assess` + `trust_policy` +
JACT, `JARVIS_ACTIONS` default-ON since 2026-07-08), goal 6-1 (✅ the live always-on monitors,
`JARVIS_MONITORS` default-ON since 2026-07-09), and goal 6-2 (✅ COMPLETE 2026-07-12 — the event-driven
wake lane live, `JARVIS_WAKE` default-ON, first on-wire v9 boot_id=18).
**Mirrors:** `PHASE_6_GOAL_6-2_EVENT_WAKE.md` / `PHASE_6_GOAL_6-1_MONITORS.md` /
`PHASE_6_GOAL_K_IT_ACTS.md` (plan-first, milestones, honest ceiling). Authored 2026-07-12.
**Pre-mortem-hardened 2026-07-12** (3-agent adversarial review, the K/6-2 precedent): the DIRECTION
held; the folded findings are the §7 counter-site pin + FP-denominator definition (one crossing =
2 JACT records = ONE behavior fire), B5 relocated to a watcher-tick edge-detect (never a hook inside
`pa_restart_pb` — the proven funnel stays untouched), B4 REPLACING the bare uptime NOTIFY (a
milestone was never an anomaly — a deliberate, documented 6-1 behavior change), the M2 ordering
constraint (B5's terminal latch strictly LAST — it starves B1/B2), `behaviors_mask` widened to u16,
and the integer-only digest-builder signature + forbidden-input list (the "no free text" property
made compile-enforced).

All line numbers below verified against HEAD (`4f4487f`) at authoring time (they SHIFT — re-grep
before relying on any).

---

## 1. Canon + honest reading

ROADMAP canon (`phase4/docs/ROADMAP.md` §Phase 6, goal 3, verbatim):

> **Proactive actions** — At least 5 automated butler behaviors (e.g. low-disk warning, daily
> briefing, anomaly alert). Trust Level 0–1 only; higher risk asks or notifies.

And the two phase-exit criteria that **start binding at 6-3** (goal 7, verbatim): "*7-day supervised
autonomy — JARVIS runs 7 days with you present: proactive actions logged, zero unapproved high-risk
actions, <5% false-positive interrupts*" — plus the phase done-when "*At least one proactive action
fired correctly without user prompt (logged + correct)*", met at K/M4 and **deliberately re-proven
here at ≥5-behavior scale** (K-d's recorded promise).

Honesty corrections, up front:

1. **A 6-3 behavior is INFORM-only (notify / consult / propose-log).** The box TELLS you things —
   serial, telemetry, console — without being asked. The user cannot yet RESPOND (control-IN = 6-5),
   so "proactive" here means event-triggered inform, NOT interaction and NOT free-form action. The
   trust mechanics are exact (`shield_action.h:93-105`): TRUST_AUTO (L0) and TRUST_NOTIFY (L1)
   execute (an inform's "execute" = its serial line + JACT record + surface); **TRUST_REQUEST (L2)
   degrades to `ACT_PROPOSE_LOG` pre-control-IN — logged, NEVER executed before 6-5**; TRUST_REQUIRE
   never auto-executes. Every v1 behavior is an L0/L1 inform; nothing state-changing ships in 6-3.
2. **The canon's "low-disk warning" is honestly reframed.** The stores are circular rings — they
   never "fill", and a "% full" gauge on them is the exact fiction 6-1's honesty table banned. The
   honest analog is the **store-first-roll notice**: the moment a store starts overwriting its oldest
   records (the 6-1 W2 wrap event) is a real, useful, one-time fact — "your episodic history now
   rolls" — not a fabricated disk-space alarm.
3. **The canon's "daily briefing" needs wall-clock time the box cannot read** (no RTC — deferred
   since 6-1 Locked decision 1). The honest pre-RTC analog is the **boot-relative status digest** at
   the existing uptime marks (1h/24h/7d, `main_x86.c:528`). A true time-of-day daily briefing is
   EXPLICITLY out of 6-3: the **CMOS-RTC bring-up is a flagged follow-up slice** (seL4 IOPort cap +
   BCD decode + its own honesty review) — recorded, not smuggled in.
4. **A healthy box fires ~nothing.** Boot_id=17 ran 28.7 h with zero degradation crossings; the
   deployed honest state for the degradation behaviors is 0 fires. The digest marks are the ONE
   deliberate benign cadence (≤3 per boot). "Proactive" must never mean chatty.

## 2. Scope boundary — and what is GENUINELY NEW vs 6-1/6-2

**6-3 IS:** a compile-time, human-reviewed **behavior registry** — ≥5 distinct
`{ trigger → spine action → fixed template/builder → user surface }` bindings — populating the
channels K/6-1/6-2 built; two net-new inform behaviors (the status digest + the degraded-mode
alert); per-behavior anti-spam budgets (the #7 <5%-FP criterion made operational); a per-behavior
console surface (M3); one JACT record per fire.

**6-3 IS NOT:** interaction or approval (control-IN = 6-5); free-form/LLM-synthesized behaviors
(K-b holds — the registry is compile-time; the LLM only ever fills 6-2's fixed templates); a real
clock (the RTC slice is a follow-up); new inbound surface (none — I211 RX stays virgin); routing
specialists (6-6); the 7-day run itself (6-7 — 6-3 builds what it measures).

**Why this is not "6-1/6-2 relabeled"** (the objection this section exists to answer):
- 6-1 proved watchers can NOTIFY; 6-2 proved an event can trigger ONE templated consult. Neither
  defines *what the butler actually does*: today a monitor event is an `[ANOMALY]` line + a JACT
  record + (for the two degradation events) a consult — there is no behavior CATALOG, no
  per-behavior surface, no digest, no degraded-mode alert, and nothing fires on the benign events.
- **New artifact 1 — the registry:** one reviewed table binding every trigger to its action,
  template, trust, budget, and surface. It is the seam 6-5 (approvals attach to behavior ids) and
  6-6 (routing targets behavior ids) ride, exactly as `monitor_event_t` was 6-1's deliberate seam
  for 6-2.
- **New artifact 2 — two net-new behaviors** (B4 digest, B5 degraded alert) with one new allowlisted
  action id and one new monitor-event snapshot type — real new capability, both inform-only.
- **New artifact 3 — the user-facing butler map:** the console section that shows WHICH behaviors
  exist, which have fired this boot, and the live totals (M3) — turning "the box printed a line"
  into "the butler has these ≥5 behaviors and here is what they did".
- **New discipline — the FP budget:** #7's "<5% false-positive interrupts" becomes a per-behavior
  cooldown/fire-once budget with a measured healthy-box baseline, not an aspiration.

## 3. Ground truth (verified against live code)

- **The K spine:** `spine_decide` (`main_x86.c:1840`) → `shield_assess` → `action_lookup` →
  `trust_policy`; `spine_record` (`:1853`) = counters + exactly ONE JACT record. Callers keep their
  own execute bodies (the 6-1 scoped-extract discipline — do NOT unify).
- **The allowlist** (`action_allowlist.h:25-29`): `ACTION_RESTART_PB=1` (TRUST_NOTIFY, SELF_HEAL),
  `ACTION_NOTIFY_ANOMALY=2` (TRUST_AUTO, NOTIFY), `ACTION_WAKE_CONSULT=3` (TRUST_NOTIFY, CONSULT
  base 10 — `shield_action.h:49`). Ids are append-only (audit currency); additions are a
  human-reviewed PR (`action_allowlist.c:4-5`).
- **The monitor events** (`monitors.h:31-39`): ERROR_RATE, SELF_HEAL_RATE, STORE_WRAP,
  HEARTBEAT_AGE (unwired — the 6-1 deferral), UPTIME_MILESTONE; **"extend by appending (never
  renumber — JACT snapshots cite these)" (`monitors.h:29-30`)**. Live watchers + `mon_notify` call
  sites: err-rate/heal-rate/store-wrap/uptime at `main_x86.c:4482/:4489/:4513-4522/:4529`;
  `mon_notify` itself `:1871` (EXECUTED branch = `[ANOMALY]` + JACT + the v8 counters + the 6-2 wake
  staging). Uptime marks 1h/24h/7d (`main_x86.c:528`, uint32-ms wrap-bounded ≤7d).
- **The wake mechanism** (6-2, live): `wake.c:15-22` — the template allowlist v1 maps EXACTLY the
  two degradation events (err-rate, heal-rate); benign events deliberately don't wake. The gate
  (`wake.h`): 10-min per-type cooldown + **2/hour global budget** (tightened at the flip);
  suppressed = counted, never audited. The ONE dispatch site `main_x86.c:4739` (take→try, cache-first,
  the three §7.5 deviations); `g_wakes_fired++` at `:4990` (dispatched consults only).
- **The degraded latch:** `g_pb_dead = 1` at `main_x86.c:1947` (the crash-loop bound,
  `KM2B_CRASHLOOP_BOUND` 5 `:577`) — today it prints `[FATAL]` and stops PB dispatch (§5-F), but the
  TRANSITION itself produces **no JACT record and no NOTIFY** (each respawn is audited; the
  giving-up moment is not). (`:4470` is the WAKE_PROBE mode-2 induction, not the real latch.)
- **Telemetry:** v9 = 240 B, CRC@236; `monitors_fired`/`last_monitor_event` (v8) +
  `wakes_fired`/`last_wake_event` (v9, `jarvis_telemetry.h:99-101`); flags through `TLM_F_WAKE`
  0x2000 (`:39`). Next slot: **v10, 0x4000**. The JACT store (LBA 21,120,000 × 4096) already audits
  every spine decision — 6-3's per-behavior history rides it for free.
- **NIC TX counters:** `tx_packets` really increments (`nic_i211.c:335/372`); a TX-stall watcher was
  6-1's "left optional / not taken" candidate (`PHASE_6_GOAL_6-1_MONITORS.md` §3 M2).

## 4. Architecture — the behavior registry over the existing spine

```
   trigger (an EXISTING monitor crossing / the uptime marks / the g_pb_dead latch)
        │
        ▼
   the BEHAVIOR REGISTRY (compile-time, human-reviewed — behaviors.c, M0):
   behavior_def_t { id; name; trigger; action_id; trust (compile-time);
                    builder (fixed template / system-facts digest); budget; surface }
        │   K-b for behaviors: the registry SELECTS everything; the LLM only ever
        │   fills 6-2's fixed consult templates; nothing synthesizes a behavior.
        ▼
   the EXISTING channels, ADDITIVELY (no rewrite of the proven paths):
     · NOTIFY behaviors  → mon_notify / a registry notify hook → spine → [ANOMALY]/[NOTICE] + JACT
     · CONSULT behaviors → the 6-2 stage→take→try→dispatch lane (unchanged)
     · DIGEST behavior   → a new allowlisted ACTION_STATUS_DIGEST → spine → [DIGEST] + JACT
     · (optional) PROPOSE_LOG behavior → TRUST_REQUEST → ACT_PROPOSE_LOG (logged, never executed)
        ▼
   surfaces: serial (log-mirrored) + JACT (one record per fire) + v10 telemetry
             (behaviors_fired / last_behavior / per-behavior fired-mask) + the console
             "Proactive behaviors" section (M3)
```

**Additive by design (with ONE documented exception):** the proven 6-1 NOTIFY path and the 6-2
consult lane are NOT restructured — the registry formalizes what already fires (B1-B3) and adds two
separately-hooked behaviors (B4 at the uptime-mark sample site — REPLACING the bare uptime NOTIFY,
the one deliberate 6-1 behavior change, §5; B5 as a fire-once edge-detect at the watcher tick —
`pa_restart_pb` is never touched). **Every registry evaluation runs at the existing [STATS]/watcher
cadence — zero per-query hot-path work, structurally** (the F-f pin). The registry table is the
single reviewed source the tests, the console manifest, and the JACT interpretation all pin
against.

## 5. The ≥5 behavior set (v1 candidate — the strategist refines at review)

| # | Behavior | Trigger (exists?) | Action → trust | Surface | New code |
|---|----------|-------------------|----------------|---------|----------|
| B1 | **Anomaly consult** (canon "anomaly alert") | `MON_EV_ERROR_RATE` (live 6-1 watcher) | `ACTION_WAKE_CONSULT` → L1 NOTIFY (the live 6-2 lane) | `[ANOMALY]`+`[WAKE]`+consult text, JACT ×2, wakes_fired, console | none — FORMALIZED into the registry |
| B2 | **Self-heal consult** | `MON_EV_SELF_HEAL_RATE` (live) | `ACTION_WAKE_CONSULT` → L1 (the heal-rate template, `wake.c:18-20`) | same shape as B1 | none — formalized |
| B3 | **Store-roll notice** (honest "low-disk") | `MON_EV_STORE_WRAP` (live, fire-once per store, armed-if-unwrapped) | `ACTION_NOTIFY_ANOMALY` → L0 | `[ANOMALY]` + JACT + the M3 per-behavior row ("episodic history now rolls — oldest records overwritten") | surface/framing only |
| B4 | **Status digest** (honest pre-RTC "briefing") | `MON_EV_UPTIME_MILESTONE` (live, 1h/24h/7d fire-once each) | **NEW `ACTION_STATUS_DIGEST=4`** → L0 AUTO, class NOTIFY (base risk 0) — **REPLACES the bare uptime `[ANOMALY]` NOTIFY** (pre-mortem: a milestone was never an anomaly; mon_notify hardcodes `ACTION_NOTIFY_ANOMALY` so B4 needs its own emit — stacking both would be 2 records/mark of deliberate chat). A DOCUMENTED 6-1 behavior change: the uptime-mark JACT `action_id` shifts 2→4, `monitors_fired` no longer bumps on marks, and 6-1's probe-gate expectation becomes "4 `[ANOMALY]` + 3 `[DIGEST]`" | `[DIGEST] up=…h q=… err=… heal=… mon=… wake=… tok=…` (integer system facts ONLY, keyword-clean) + JACT + console "last digest" | new allowlist entry + a host-pure INTEGER-ONLY digest builder |
| B5 | **Degraded-mode alert** | the `g_pb_dead` state (`main_x86.c:1947` sets it — exists; the transition is currently un-audited) | `ACTION_NOTIFY_ANOMALY` → L0 | `[ANOMALY] pb degraded cache-only …` + JACT + console | append `MON_EV_DEGRADED` (monitors.h, append-only, immediately before `MON_EV__COUNT`) + one snapshot case + **a fire-once EDGE-DETECT at the watcher tick** (`if (g_pb_dead && !notified)` — pre-mortem: NEVER a hook inside `pa_restart_pb`, which holds `g_restart_in_progress` mid-funnel and is the K/M4-verified path that must stay byte-identical; next-[STATS] latency is fine for a terminal one-shot notice) |
| B6 | *(optional 6th)* **TX-stall notice** | **NEW watcher**: `tx_packets` delta==0 while `g_net.ready`, N windows (the 6-1 optional candidate) | `ACTION_NOTIFY_ANOMALY` → L0 | `[ANOMALY] telemetry tx stalled …` + console | one watcher + snapshot type — **blurs into 6-1; justified as the watcher 6-1 explicitly left optional** |

Distinctness: five distinct triggers (two degradation, one liveness transition, one benign cadence,
one terminal latch), three distinct action shapes (consult / notify / digest), five distinct surface
framings. B1/B2 ride 100%-existing machinery — 6-3 makes them NAMED, budgeted, surfaced behaviors
rather than anonymous plumbing; B4/B5 are net-new capability; B3 is net-new framing/surface.

**Flagged additions requiring review:** ONE new allowlisted action id (`ACTION_STATUS_DIGEST=4`,
TRUST_AUTO, class NOTIFY — inform-only, human-reviewed PR per the K-b discipline); ONE appended
monitor-event type (`MON_EV_DEGRADED` — append-only per `monitors.h:29-30`; its snapshot gets the
full T7 keyword-clean treatment); B6's new watcher if taken. **NO new L1-executing action** — and if
the strategist wants the PROPOSE_LOG lane demonstrated pre-6-5, that is Open question O4 (a
TRUST_REQUEST behavior that only ever logs).

## 6. Security + the false-positive budget (the #7 criterion starts binding here)

- **Select-never-synthesize, extended to behaviors (K-b):** the registry is a compile-time table;
  triggers are existing observable events; templates/builders are fixed literals + decimal counters
  (the `km2b_build_trigger`/`monitor_build_snapshot` discipline, `monitors.h:7-10`); the LLM's only
  role remains filling 6-2's fixed consult templates, and its answers still have NO actuator. Every
  new string (the digest, the degraded snapshot, B6's snapshot) is host-pinned keyword-clean BOTH
  ways — substring traps included ("skill"⊃"kill", "information"⊃"format") — and must pass the REAL
  `shield_assess` un-BLOCKED (the T7 discipline).
- **The counter site + the FP denominator (pre-mortem F-a, pinned):** ONE B1/B2 crossing produces
  TWO JACT records by design (the `[ANOMALY]` NOTIFY `action=2` + the consult `action=3` — traced:
  `mon_notify` fires `spine_record` at `main_x86.c:1888` then stages the wake; the dispatch site
  records again at `:4986`) — that is PLUMBING. It is **ONE behavior fire and ONE user interrupt**.
  `behaviors_fired++`/`last_behavior`/`behaviors_mask` are bumped at the **mon_notify record step**
  (the always-fires site) for the NOTIFY-staged-consult behaviors — NOT at the consult dispatch
  (which is gate-suppressible: a suppressed consult must still count the crossing's behavior fire,
  or the user sees an `[ANOMALY]` that counted 0). **The #7 FP fraction's denominator = behavior
  fires** (user interrupts), never JACT records.
- **The digest carries NO text — compile-enforced (pre-mortem F1):** the digest builder's signature
  takes ONLY integer arguments (no `char*` parameter exists to misuse); its output is fixed literals
  + decimals. **Forbidden digest/snapshot inputs, permanently:** model answers (`wresp`), the
  response head (`g_fb_last_resp`/`last_text`), episodic `resp` text, cache action strings, any
  GGUF-sourced string. `model_name` is excluded too (it happens to be a compile-time literal today,
  `main_x86.c:2229` — but it stays out so nobody later "enriches" it from file metadata). The
  keyword-clean requirement binds the TRIGGER/SNAPSHOT builder outputs (what `shield_assess` scans);
  console-row prose (e.g. "oldest records overwritten") is governed by the console honesty gate.
- **Per-behavior anti-spam budget** (inherited bounds first, then per-behavior):
  B1/B2 — debounce + fire-once + re-arm (6-1) THEN the 6-2 gate (10-min type cooldown, 2/hr global);
  B3 — fire-once per store per boot, armed only if not already rolled (the 6-1 W2 rule);
  B4 — fire-once per mark, ≤3 per boot by construction (marks capped ≤7d);
  B5 — fire-once per boot (the latch is terminal until power-cycle);
  B6 (if taken) — N-window debounce + fire-once + re-arm.
  **Healthy-box expectation: B4's ≤3 digests are the ONLY fires** — everything else honest-0
  (boot_id=17: zero degradation crossings in 28.7 h). That IS the design target, not a limitation.
- **The <5% FP definition (operational):** a false positive = a fired inform whose stated condition
  did not hold (e.g., a threshold crossing from miscalibration, a wrap notice for a store that
  didn't roll). Denominator = all fired informs over the supervised window. The digest is
  structurally FP-free (unconditional facts); the degradation behaviors inherit 6-1's measured
  calibration (thresholds with unbounded margin on a healthy box). M2 measures the healthy-box FP
  rate directly (expected: 0/digests-only); the 7-day run (6-7) measures it for real.
- **No new inbound surface; no wall-clock claims.** Triggers derive from PA-observable state only.
  The RTC follow-up is its own flagged slice with its own review (risk: a wrong clock makes every
  "daily" claim dishonest — worse than boot-relative honesty).

## 7. Telemetry + console (UI-parity, REQUIRED at M3)

**Proposed v10 (PIN at M3, the v9 lockstep precedent):** append `uint16 behaviors_fired` (total
behavior fires — bumped once per fire at the always-fires record step, §6) + `uint16
behaviors_mask` (bit b set once behavior b has fired this boot — the live per-row source for the
console; **u16 per pre-mortem F-d**: a u8 would overflow at behavior #9 exactly when the registry
does its job as the 6-5/6-6 seam) + `uint8 last_behavior` (behavior id, 0=none) + `uint8 pad` →
**246 B, CRC@242, version 10**, +`TLM_F_PROACTIVE` 0x4000 (set on registry-init, capability-live;
exact offsets pinned by offsetof at M3). Gated fill → a PROACTIVE=0 deploy emits 0s + flag clear
(the v5…v9 honest pattern).

**Console:** a "Proactive behaviors" section (System screen or its own card): one row per registry
behavior — static NAME from the compile-time manifest, live "fired this boot" from
`behaviors_mask`, plus the live total + last-behavior label; `—` until `TLM_F_PROACTIVE` is live.
Wording = "informs you when …" / "event-triggered" — the honesty gate BANS "the AI decided",
"thinking", "reasoning" (already banned), and never claims interaction ("respond" is 6-5). The
existing consult/monitor rows stay; B1/B2 fires remain visible in `wakes_fired` too (double-counting
is avoided in the DOC's accounting: `behaviors_fired` counts registry fires; a consult bump appears
in both `wakes_fired` and `behaviors_fired` BY DESIGN — two honest views of one event, documented).

**Open alternative (O3):** pure reuse — no v10; the console derives behavior activity from
`monitors_fired`/`wakes_fired` + JACT offline. Rejected as the default because the console then
cannot show per-behavior liveness honestly (no live per-row source) — but the strategist may prefer
wire minimalism.

## 8. Locked decisions (candidates — the strategist confirms)

1. **Behaviors are a compile-time registry** (`behaviors.c/h`, host-pure) — trigger, action id,
   trust, builder, budget, surface tag per behavior; additions are a human-reviewed PR (the
   allowlist discipline). The LLM never selects, parameterizes, or synthesizes a behavior.
2. **Additive wiring, one documented exception:** the proven mon_notify/wake paths are not
   restructured; B1-B3 are REGISTERED (named, budgeted, surfaced) over their existing fire sites;
   B4 REPLACES the bare uptime NOTIFY at the mark-sample site (a milestone was never an anomaly —
   the 6-1 gate expectations re-baseline, §5/§11); B5 is a fire-once edge-detect at the watcher
   tick — **`pa_restart_pb` is NEVER modified** (the K/M4-verified funnel stays byte-identical).
3. **Counter/FP discipline:** `behaviors_fired`/`last_behavior`/`behaviors_mask` bump at the
   always-fires record step (one bump per behavior fire — a gate-suppressed consult still counts
   its crossing); the #7 FP denominator = behavior fires, never JACT records (§6).
4. **All v1 behaviors are L0/L1 INFORMS.** Nothing state-changing; TRUST_REQUEST (if O4 is taken)
   only ever PROPOSE_LOGs pre-6-5; TRUST_REQUIRE stays never-auto.
5. **The digest is system facts, never model text** — fixed literals + decimal counters, ≤ the JACT
   trigger cap, keyword-clean host-pinned. A model-written briefing is NOT 6-3 (it would need the
   consult lane + fresh hygiene review — deferred with the RTC slice).
6. **One JACT record per behavior EMIT** (via `spine_record` — the §5-E one-record discipline;
   B1/B2's consult adds its own record, the documented two-views accounting §6);
   suppressed/budgeted fires are counted, never audited (the 6-2 rule).
7. **Gating:** new `JARVIS_PROACTIVE` (default 0), `#error`-guarded to require
   `JARVIS_ACTIONS && JARVIS_MONITORS && JARVIS_WAKE`; box-only `JARVIS_PROACTIVE_PROBE` (own flag,
   the one-flag-per-probe precedent) for deterministic per-behavior demonstrators. OFF =
   object-level byte-identical (`main.c.obj` sections + `nm`; never md5 the packed image). The flip
   is a deliberate post-M3 decision.
8. **Honest naming:** "behavior" = a named, bounded inform; console/docs say "informs/notifies/
   consults", never autonomy language. The canon's "daily briefing"/"low-disk warning" appear ONLY
   with their honest reframings (§1.2/§1.3).

## 9. Milestones

- **M0 (host/CI, pure) — ✅ DONE 2026-07-12 (TDD RED→GREEN):** `phase3/src/ai/behaviors.c/h` — the
  registry table + accessors + the INTEGER-ONLY digest builder (+ the appended `MON_EV_DEGRADED`
  snapshot case in `monitors.c`, T7-covered — the case MUST land with the enum value or
  `monitor_build_snapshot`'s `default: return -1` makes B5 SILENTLY never fire, an audit hole; the
  RED run proved exactly this: enum-without-case failed T7a with `type 6: n=-1`) + `test_behaviors.c`
  (7 test groups, ~45 asserts) (+ extended `test_monitors.c` 44/44 (T12 exact-format pin) /
  `test_wake.c` 9/9 (DEGRADED → NULL template; gate arrays 6→7 recompile-safe) /
  `test_action_allowlist.c` 6/6 (count==4, id-4 = {TRUST_AUTO, CLASS_NOTIFY}) /
  `test_shield_action.c` 8/8 (T8 digest gate: base 0, teeth)). CI step
  "Phase 6: 6-3 Behavior registry (C)". Console `MON_EVENT_LABELS` gained 'degraded' (the
  else-unlabeled-event pin); honesty 92/92 + logic 14/14 + e2e 34/34 re-green.
  **M0 pin list (pre-mortem F3/F4):** `MON_EV_DEGRADED` appended IMMEDIATELY before `MON_EV__COUNT`
  with every existing value unchanged (JACT snapshot-type stability, `monitors.h:29-30`; the wake
  gate's `last_fire_ms`/`fired[MON_EV__COUNT]` arrays resize recompile-safe);
  `action_allowlist_count()==4`; the id-4 def == {TRUST_AUTO, ACTION_CLASS_NOTIFY}; the digest
  builder's output un-BLOCKED through the REAL `shield_assess` (T7) AND an injected blocklist
  keyword DOES block (teeth); `ACTION_ID_BLOCK_PROBE` 0xFFFE still refused; the receiver/console
  `last_monitor_event` label table gains the new type (else B5's telemetry renders an unlabeled
  event).
- **M1 (box, gated `JARVIS_PROACTIVE` default-0) — ✅ DONE 2026-07-12 (KVM `-smp 6`, no deploy):**
  the registry wired over the existing sites — `proactive_mark` (the §6/F-a ALWAYS-FIRES bump:
  `g_behaviors_fired`/`g_last_behavior`/`g_behaviors_mask`, called at mon_notify's EXECUTED record
  step, never at the gate-suppressible consult dispatch) + the B4 `proactive_digest_emit`
  (REPLACING the bare uptime NOTIFY at the mark-sample site; digest → spine `ACTION_STATUS_DIGEST`
  → `[DIGEST]` + ONE JACT `action=4` + the mark) + the B5 fire-once edge-detect at the watcher tick
  (`g_pb_dead && !g_b5_notified` — `pa_restart_pb` untouched) + `build_jarvis_x86.sh`
  sync/injection of behaviors.c. **Gate 1 (PROACTIVE=1 + PROACTIVE_PROBE=1):** all 5 behaviors
  fired in the designed sequence — B4×3 `[DIGEST] digest up=0h q=100 … tok=593 risk=0` (one per
  short mark) → B1 → B3 → B2 → B5, `mask=31`, `fired=7`; B1/B2 rode the live 6-2 lane
  (`[WAKE] route=infer ms≈13.5s outcome=OK` ×2 — the hourly budget exactly consumed); B2's
  induction = 2 REAL respawns (EXECUTED/OK); **the sequencing constraint held live** (B2's
  respawns at window 5, AWAY from B1's window-2 staged wake — the §7.9 hazard; B5's terminal
  latch LAST at window 8); after B5 the box served cache-only with **err=0 to q=91,900**,
  0 `[FATAL]`, 0 phantoms. **JACT read-back** (KVM store boot 31, monotonic, keyword-clean):
  3× `action=4` digest (the first action=4 records) + the B1/B2 `action=2`+`action=3` pairs +
  2× `action=1` respawn + B3/B5 `action=2`. **OFF-identity:** pre-M1 (`22c1ef4`) vs
  M1-at-PROACTIVE=0 — `main.c.obj` `.text`/`.rodata`/`.data` + `nm` all IDENTICAL (the packed
  image grows only by the linked-but-unused behaviors.o — the wake.c pre-flip pattern).
  Probe inductions all honest (synthetic deltas into the WINDOW delta only; real respawns; real
  wrap thresholds; short marks; a labeled probe latch).
- **M2 (box) — ✅ DONE 2026-07-13 (KVM `-smp 6`, no deploy; + ONE host-pure addition):** the O2
  GLOBAL inform cap landed host-first (`behavior_budget_t`/`behavior_budget_try` in behaviors.c/h —
  hourly bucket-INDEX semantics, wrap-safe, suppressed = counted non-event; `test_behaviors.c` test H,
  TDD RED→GREEN, rides the existing CI step → **8 PASS groups**) and wired at the TWO surfacing
  sites (mon_notify top, mapped types only + the digest emit) — a capped inform is NOT surfaced,
  NOT audited, NOT marked (the FP denominator stays "interrupts the user saw"); **the terminal B5
  notice RE-ARMS on suppression** (retries each window, lands in the next budget bucket — every
  other capped one-shot drops by design: a backstop, not a scheduler); the cap NEVER gates
  `pa_restart_pb`. **G1 anti-spam (probe mode 2, cap sed-raised to 16):** B1's synthetic delta held
  for 10 CONSECUTIVE windows → **exactly ONE `[ANOMALY] mon err-rate`** (fire-once held, not
  per-sample); all 5 fired once each in one run, B5 STRICTLY LAST (`mask=31`, fired=7), consults
  bounded by the 6-2 gate (2× route=infer OK), 0 suppressions, err=0 to q=25,500. **G2 the cap
  (sed-shrunk to 3):** 3 digests ALLOWed then EVERY notice suppressed — B1/B3/B2 exactly once each
  (`budget total=1/2/3`), 0 `[ANOMALY]`, 0 `[WAKE]`, B5 re-arm retrying every window (192 counted),
  the 2 induction respawns EXECUTED un-capped; **JACT boot 36 = exactly 5 records (3× action=4 +
  2× action=1) — ZERO records for the 195 suppressed informs** (counted, never audited). **G3 the
  MEASURED healthy-config FP baseline (PROACTIVE=1, PROBE=0, committed defaults, real marks):** a
  60.5-min run (q=938,500, err=0) fired **exactly ONE inform — the 1h digest**
  (`digest up=1h q=930600 err=0 heal=0 mon=0 wake=0 tok=558`, JACT `action=4`, every counter ==
  live truth, the uptime condition held) and honest-0 everywhere else (0 `[ANOMALY]`/suppress/wake/
  restart) → **FP = 0 / 1 fired informs = 0% — the measured number the #7 <5% criterion rests on.**
  **G4 deployed regression (PROACTIVE=0, all M2 code in-tree):** `main.c.obj` 3 sections + `nm`
  byte-identical to the pre-M1 baseline; KVM smoke = zero `[BEHAVIOR]`/`[DIGEST]`/`[PROACTIVE*]`
  lines, err=0. **O2 CALIBRATED: `BEHAVIOR_BUDGET_PER_HOUR 6u` stands** — 6× the measured healthy
  hour (1 inform) and ≥ the worst-case degraded hour (2 consults + ≤3 wraps + 1 digest, with the
  B5 re-arm covering the 7th-inform edge). (Box-driving note: detached KVM runs must escape the ssh
  session CGROUP — `sudo systemd-run` a transient unit; setsid/nohup dies with the session scope.)
- **M3 (telemetry v10 + console — REQUIRED) — ✅ DONE 2026-07-13 (full v9-precedent lockstep):**
  **v10 = 246 B, CRC@242, version 10** — appends `behaviors_fired` u16@236 (USER INTERRUPTS; a
  cap-suppressed inform never counts) + `behaviors_mask` u16@238 (bit id−1 latches per fired
  behavior — the console's live per-row source) + `last_behavior` u8@240 + `beh_pad` u8@241,
  +`TLM_F_PROACTIVE` 0x4000 set on `g_proactive_inited` (`jarvis_telemetry.c` untouched —
  offsetof-based finalize, verified); gated fill in `jarvis_telemetry_emit` → the PROACTIVE=0
  deploy emits 0s + flag clear (the v5..v9 honest pattern); UDP frame buffer 288→304 (headroom —
  the v10 frame is exactly 288). Lockstep: receiver FMT `…HBBHBBHHBBI` + `FLAG_NAMES[0x4000]` +
  decode+record BOTH; fixture `_DEFAULTS`/`FLAG_BITS`; `gen_golden_pcap` guards 246;
  `golden_telemetry.json` meta + the infer frame (`behaviors_fired=3`, `behaviors_mask=21` =
  0b10101 B1+B3+B5, `last_behavior=5`) + `golden.pcap` regenerated (2456 B, idempotent). Console:
  the **"Proactive behaviors" System-screen card (O5)** — a static 5-row manifest (KEEP IN SYNC
  with behaviors.c) with per-row fired/quiet from the mask bit, the live total + last-behavior
  label, all `—` until `TLM_F_PROACTIVE`; + the Capabilities "Proactive behaviors (registry
  informs)" row (flag-parity); sim = 0s + no flag (previews `—`). Honesty: "informs you"/
  "event-triggered" REQUIRED; "the AI decided" banned globally; "autonomous" banned SCOPED to the
  card (the K ACTIONS row keeps it); the card never sums wakes+behaviors (the documented
  double-count). Host green: telemetry C **71/71**, receiver **145/145**, honesty **111/111**,
  logic 14/14, e2e **35/35** (the new pin: total==3 AND B1-fired/B2-quiet derived from mask 21).
  Box: OFF = zero v10/behavior lines + **[INFER] overlap 16/16 byte-identical** vs the pre-v10 OFF
  baseline, err=0 (obj-identity is NOT the M3 gate — the wire bump legitimately changes the
  telemetry emit code); transient ON (probe mode 1) = **`[TLM-V10] behaviors_fired=6
  behaviors_mask=15 last_behavior=2 proactive_inited=1`** climbing + the committed cap-6
  suppressing the probe's 7th inform (B5 re-arm ×281 — the M2 semantics in compressed probe-land,
  correct); on-wire I211 v10 validates at the flip (no NIC in QEMU — the v8/v9 precedent).
- **Flip:** `JARVIS_PROACTIVE` default-ON — deliberate, after a supervised healthy run (expected
  shape: the uptime digests fire, everything else honest-0) + first on-wire v10.

## 10. Storage / state

**No new store.** Behavior fires audit into JACT (`action_id` + the behavior's keyword-clean
trigger snapshot distinguish them; `parse_action_audit.py` reads the per-behavior history);
registry + budgets = PA statics; v10 fields are the live surface.

## 11. Risks

- **Chattiness / FP-budget failure** — a miscalibrated threshold makes an "inform" a nag; the #7
  <5% criterion fails at 6-7. Mitigation: per-behavior fire-once/budgets (§6), 6-1's measured
  thresholds, the M2 healthy-run gate (digests-only), and the flip standard (no spam on a
  supervised run).
- **Behavior sprawl / registry rot** — behaviors added casually erode the reviewed-table guarantee.
  Mitigation: additions are human-reviewed PRs touching ONE table; the console manifest + tests pin
  the registry so an unregistered fire or an unfired registration is visible.
- **Blur into 6-1/6-2** — §2 is the boundary: 6-3 adds NO new watcher class except the flagged
  optional B6; anything needing a genuinely new signal goes back through the 6-1 honesty table.
- **The B4 replace re-baselines proven 6-1 gates** — the uptime-mark JACT `action_id` shifts 2→4
  and `monitors_fired` no longer counts marks; the 6-1 probe expectation ("exactly 7 `[ANOMALY]`")
  becomes "4 `[ANOMALY]` + 3 `[DIGEST]`". M1 re-runs the 6-1 probe gate against the NEW baseline and
  records it in both docs — a deliberate, documented change, never a silent drift.
  **✅ RE-BASELINE CONFIRMED ON THE BOX 2026-07-12 (M1 run 2, `JARVIS_MONITOR_PROBE=1` +
  `JARVIS_PROACTIVE=1`):** exactly **4 `[ANOMALY]`** (err-rate, store-wrap ×2 epi+jact, heal-rate)
  + **3 `[DIGEST]`**, `[TLM-V8] monitors_fired=4` (was 7 — marks no longer bump it;
  `last_monitor_event=2` = the heal-rate, the last true monitor event), behavior marks
  B4×3/B1/B3×2/B2 → `mask=15` (B5 correctly absent — MONITOR_PROBE induces no degraded latch),
  both consults `outcome=OK` alongside the probe's 2 real respawns, err=0 at q=27,000.
  Recorded in `PHASE_6_GOAL_6-1_MONITORS.md` the same day.
- **The digest drifting into fiction** — it must render ONLY live counters (the [SNAP]/telemetry
  fields); no "health verdict", no CPU%, no wall-clock. The honesty gate + T7 pin it.
- **Double-counting confusion** — a consult behavior bumps both `wakes_fired` and (v10)
  `behaviors_fired`; documented as two views of one event (§7), value-pinned in e2e so the console
  never adds them.
- **The RTC follow-up** — flagged, out of scope; any "daily" wording before a verified RTC is
  dishonest and banned.

## 12. Open questions — ALL RESOLVED (strategist review 2026-07-12)

- **O1 — the final set: RESOLVED = B1–B5 in, B6 DEFERRED.** The TX-stall watcher stays a 6-1-class
  candidate (a genuinely new watcher belongs behind the 6-1 honesty table, not smuggled into 6-3);
  the ≥5 canon bar is met without it.
- **O2 — FP-budget defaults: RESOLVED = per-behavior budgets as §6 PLUS a global aggregate cap
  (≤N informs/hour across all behaviors, the 6-2 budget precedent).** The CONCRETE N is calibrated
  and lands with the box wiring at M1/M2 (measured, not guessed — the 6-1/6-2 discipline); M0
  deliberately ships no budget code (the gate state is PA-side, not registry-side).
  **✅ CALIBRATED AT M2 (2026-07-13): `BEHAVIOR_BUDGET_PER_HOUR = 6u`** — the measured healthy hour
  fires exactly 1 inform (the G3 baseline: 1 digest / 60.5 min, FP 0/1 = 0%), so 6/hr = 6× headroom
  while still bounding a runaway watcher; the worst-case degraded hour (2 consults + ≤3 store-roll
  notices + 1 digest ≈ 6, + the terminal B5 as a possible 7th) is covered by the B5 RE-ARM rule
  (a suppressed terminal notice retries and lands in the next hour bucket — never lost). The cap
  gates INFORMS only (mon_notify-mapped + the digest), never the self-heal funnel.
- **O3 — telemetry: RESOLVED = v10 at M3** (`behaviors_fired` u16 + `behaviors_mask` u16 +
  `last_behavior` u8 + pad → 246 B, CRC@242, `TLM_F_PROACTIVE` 0x4000). The console needs a live
  per-row source; the u16 mask leaves 6-5/6-6 headroom (behaviors.h pins ids ≤ 16 for exactly this).
- **O4 — the PROPOSE_LOG demonstrator: RESOLVED = DEFERRED to 6-5; v1 is INFORM-PURE.** Every v1
  behavior is L0/L1 (test_behaviors.c test C pins it: allowlisted + AUTO/NOTIFY only, a clean
  verdict always executes as an inform). The F6 `spine_record` mis-count (`actions_blocked` bump on
  a PROPOSED) stands as the recorded blocker; the `AUDIT_PROPOSED`-aware count ships with 6-5.
- **O5 — the console shape: RESOLVED = a System-screen card** (the registry manifest's static names
  + `behaviors_mask` live per-row — the Capabilities-rows-driven-by-live-flags pattern satisfies
  the honesty bar; no new screen).

## 13. Done-when (6-3's own)

- ≥5 registered behaviors fire correctly on the box — each demonstrated once (probe-induced,
  honestly labeled), each producing its serial line + exactly one JACT record + its console row;
  the canon done-when thereby re-proven at ≥5-behavior scale.
- The anti-spam/FP discipline demonstrated: sustained conditions fire once; budgets suppress
  (counted, not audited); a healthy-config run shows digests-only/honest-0.
- The M3 surface live: v10 (or the O3 alternative) + the per-behavior console rows, honesty-gated.
- OFF (`JARVIS_PROACTIVE=0`) object-level byte-identical; the flip is a deliberate, recorded
  decision after a supervised healthy run.
- The 6-5/6-6 seam documented: behavior ids are the attachment point for approvals and routing.

## 14. Honest ceiling

> 6-3 is **≥5 bounded, templated, SHIELD-scored, JACT-audited, rate-limited INFORM behaviors** —
> the box surfaces genuinely useful observations (an anomaly consult, a self-heal summary, a
> store-roll notice, a status digest, a degraded-mode alert) without being asked. It is NOT
> autonomy, NOT interaction (you cannot answer until control-IN, 6-5), NOT free-form generation
> (the registry and every template are compile-time and human-reviewed), and NOT a real clock (the
> digest is boot-relative until the RTC slice ships). A healthy box stays almost silent — that
> silence is the design working, and every fire that does happen is one auditable line in JACT.

---

*Companion to `phase6/docs/PHASE_6_PLAN.md` (goal 6-3); rides `PHASE_6_GOAL_K_IT_ACTS.md`'s spine,
`PHASE_6_GOAL_6-1_MONITORS.md`'s watchers, and `PHASE_6_GOAL_6-2_EVENT_WAKE.md`'s consult lane.
Ground truth verified against HEAD (`4f4487f`) at authoring (2026-07-12).*
