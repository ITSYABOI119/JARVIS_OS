# Phase 6 Goal 6-1 — Always-On Monitors (PLAN-FIRST)

**Status: PLAN-FIRST — for strategist review before any M1 box code. M0 (the pure host/CI framework) may proceed.**
**Depends on:** keystone K (✅ COMPLETE 2026-07-08 — the action spine is live in deploy: static allowlist +
`shield_assess` + `trust_policy` + JACT audit + v7 telemetry, `JARVIS_ACTIONS` default-ON since `34a165e`).
**Mirrors:** `PHASE_6_GOAL_K_IT_ACTS.md` (plan-first, milestones, honest ceiling). Authored 2026-07-08.

All line numbers below verified against HEAD at authoring time (they SHIFT — re-grep before relying on any).

---

## 1. Scope + honesty

Goal 6-1 = **lightweight C watchers over REAL observable box state**. A watcher reads a signal that already
exists, compares it to a threshold with debounce/hysteresis, and on a crossing emits a **NOTIFY event through
the K action spine**: the event becomes an `ACTION_NOTIFY_ANOMALY` (`action_allowlist.c:13` —
`TRUST_AUTO / ACTION_CLASS_NOTIFY`, in the allowlist since K/M0 but **DORMANT: nothing calls it today**) →
`shield_assess` → `trust_policy` → a serial `[MONITOR]` line + a JACT audit record + (M3) a live console
surface. No new decision machinery — the spine K built is the delivery channel.

**Where watchers run:** the existing cadences only —
- the `[STATS]` q%100 block (`main_x86.c:4200`) for counter-delta sweeps, and
- the per-iteration `next_query` hook (`main_x86.c:4393`; the K/M2c hang-trip at 4403–4417 is the working
  "read signal → threshold → funnel an action" template)

**NO LLM per tick, NO new timer, NO per-`seL4_Yield` work.** A monitor tick is a handful of integer compares.

### The honest signal set (real, observable today)

| Signal | Source (verified) | Watcher |
|---|---|---|
| PB heartbeat age | `g_pb_last_ack_ms` (`main_x86.c:522`; the K/M2c `[HB-AGE]` instrument) | age > threshold while not inferring |
| q_errors delta | the `q_errors` counter (`[STATS] err=`) | errors-per-window spike (M1 — the first watcher) |
| Self-heal rate | `g_restart_count` (`main_x86.c:518`) | restarts-per-window > threshold (self-heal is firing unusually often) |
| Store growth / wrap | episodic/JACT `total_entries` (monotonic) + wrap detection; telemetry-log fullness = `nvme_log_cursor()` of 2700 (`nvme_log.h:76`) | first wrap per store; log rolling-full |
| NIC TX activity | `tx_packets` (`nic_i211.c:335/372` — REALLY incremented) | TX stalled while `g_net.ready` (delta==0 over N windows) |
| Uptime milestones | `jarvis_uptime_ms()` (TSC, boot-relative) | crossed 1h/24h/7d marks (fire-once each) |
| Inference busy-ness | `infer_duty_pct` (workload duty, v4 telemetry) | duty > threshold sustained — a busy-ness proxy, **NOT CPU%** |

### The fiction we will NEVER build (each has a concrete reason)

| Fiction | Why it's fiction |
|---|---|
| CPU% | PA busy-polls → always ~100%; any % gauge is a lie (see `system-page-honest-metrics`) |
| SMART / IOPS | no SMART/admin-log path in the NVMe driver; nothing real to read |
| `tx_errors` | declared (`nic_i211.h:222`) but **never incremented anywhere** — a constant 0 dressed as a signal |
| RX / inbound anything | I211 RX is virgin surface; control-IN is goal 6-5 (HARD-gated) — no inbound signal exists |
| Wall-clock / time-of-day | no RTC read exists; uptime is TSC boot-relative only (see Locked decision 1) |
| "% full" on circular stores | episodic/JACT/nvme_log are rolling rings — they never "fill"; a % would imply data loss that isn't happening |

---

## 2. Locked decisions

1. **RTC/schedule is DEFERRED out of 6-1** (a near-term follow-up slice): a minimal CMOS RTC read is an seL4
   IOPort cap + BCD decode — small, but it is the ONLY signal class in this goal that cannot be honestly
   faked, and nothing in 6-1's watcher set needs it (the 6-3 daily-briefing behavior does). 6-1 ships
   **boot-relative monitors only**; any "daily" phrasing is out of scope until the RTC slice lands.
2. **The first wired monitor (M1) is q_errors-delta** (error-rate spike): operationally meaningful, cheap,
   and DISTINCT from the self-heal machinery (a heartbeat-age watcher would overlap K/M2c's miss-counter and
   muddy the box gate — it comes at M2 with calibration). An induced error burst is also the easiest honest
   box-gate stimulus.
3. **NO 6-2 bundling:** 6-1 = watchers + event→NOTIFY through the spine. Event→(cache/inference) WAKE — the
   system *reacting* to a monitor event with a decision — is goal 6-2. The `monitor_event_t` struct designed
   at M0 is deliberately the 6-2/6-3 seam (a wake consumer reads the same event the NOTIFY path emits).
4. **Factor a generic `spine_run_action(action_id, trigger, trigger_len, …)` OUT of `pa_restart_pb`**
   (`main_x86.c:1764-1812`): lines ~1773–1803 are already the generic assess→trust→execute/audit→count
   sequence, hardcoded to `ACTION_RESTART_PB`. M1 extracts it so a monitor NOTIFY and the self-heal SHARE one
   spine step (one JACT shape, one counter discipline). **Behavior-neutral extract-and-delegate** — the
   K/M4-verified self-heal must re-verify IDENTICAL on the box (0 spurious restarts, the STEP-3/K/M2c/K/M4
   gates' evidence still holds).
5. **Gated `JARVIS_MONITORS` (new flag, `jarvis_debug.h`), default-0** — the K discipline: OFF is
   byte-identical to the deployed self-heal image; the default-ON flip is a deliberate decision AFTER box
   proof. NOTE a NOTIFY-only monitor still EXECUTES something real (a serial line + a JACT record +
   `g_actions_fired++`), so it gates like any action, proves on the box, and flips deliberately.
6. **Monitor snapshots are SYSTEM FACTS ONLY, keyword-clean** (the `km2b_build_trigger` discipline —
   fixed literals + decimal counters, NEVER query text or free text), or a later `shield_assess` would BLOCK
   the NOTIFY on a blocklist keyword. Actions are SELECTED from the static allowlist by id — never
   synthesized (K-b holds for monitors too).
7. **"Minimal CPU when idle" is measured in µs/sweep** (TSC-bracketed across a monitor tick at the [STATS]
   cadence), plus the structural assertion that NO monitor work runs per `seL4_Yield`. NEVER a CPU% claim
   (fiction; PA busy-polls by design).

---

## 3. Milestones

- **M0 (host/CI, no box):** the pure monitor framework — `phase3/src/ai/monitors.{c,h}` (threshold/debounce/
  hysteresis state machine + per-boot counter-delta helper + keyword-clean event snapshot builder) +
  `test_monitors.c` + a CI step. The km2b_miss/km2b_fault precedent: host-pure, NO `<sel4/sel4.h>`.
  ✅ **DONE 2026-07-08** — see §6.
- **M1 (box, gated `JARVIS_MONITORS` default-0):** factor `spine_run_action` out of `pa_restart_pb`
  (behavior-neutral; box re-verify the self-heal gates unchanged) + wire the FIRST watcher (q_errors-delta at
  the [STATS] cadence) → `ACTION_NOTIFY_ANOMALY` → the spine → `[MONITOR]` line + JACT record. Box gate: an
  induced error burst fires the NOTIFY exactly once (debounced), JACT reads back off-box, the deployed-config
  run shows 0 monitor lines (OFF) and the self-heal probes still pass. **The minimal viable 6-1.**
  ✅ **DONE 2026-07-08** — see §6 (implemented as `spine_decide`+`spine_record`, the scoped two-helper split).
- **M2 (box+host):** the honest monitor set — self-heal-rate, store-growth/wrap, uptime-milestone
  (**heartbeat-age DEFERRED** — see §6; NIC TX-activity left optional/not taken). A real-telemetry
  calibration pass sets per-signal thresholds + debounce (no guessed constants).
  ✅ **DONE 2026-07-09** — see §6.
- **M3 (telemetry/console slice — REQUIRED for done):** the NOTIFY activity on the wire — telemetry **v8**
  (232 → +N B, CRC offset shifts, the FULL K/M3-precedent lockstep: `jarvis_telemetry.h` → receiver →
  fixture → `gen_golden_pcap` → `golden.pcap` → console) + a console monitor feed (a "Monitors" surface
  rendering the real event counts). A monitor with no live surface violates UI–feature-parity.
- **Flip:** `JARVIS_MONITORS` default-ON — deliberate, box-proven, after a supervised healthy run shows no
  false-positive NOTIFY spam (the K/M4 validate-before-commit pattern).

---

## 4. Storage / state

**No new store.** Monitor NOTIFY events audit into the EXISTING JACT store (they run through the spine, so
they get JACT records for free — same `parse_action_audit.py` read-back). The v8 telemetry fields are the
live surface. Watcher state is a few dozen bytes of PA statics (`monitor_t` per signal).

## 5. Risks

- **The `pa_restart_pb` refactor touches the K/M4-verified self-heal.** Mitigation: pure
  extract-and-delegate (no logic change), then re-run the crash+hang probe gates on the box and require
  results identical to the K/M4 baseline before anything else lands on top.
- **Debounce/threshold calibration.** q_errors is IPC-timeout-dominated (not answer-quality); heartbeat-age
  must sit above the ~12 s worst-case single-inference latency; store-wrap fires once per boot at most. Each
  threshold is calibrated from real telemetry (M2), never guessed.
- **Idle cost.** The busy-poll makes % meaningless; the honest baseline is TSC-measured µs per monitor sweep
  at the [STATS] cadence, asserted small, with zero per-Yield work by construction.
- **NOTIFY spam.** Fire-once-per-crossing hysteresis is in the M0 state machine (a sustained condition fires
  exactly once until it clears and re-crosses); the box gate explicitly checks "exactly once."

## 6. Milestone log

- **M0 ✅ DONE 2026-07-08:** `phase3/src/ai/monitors.{c,h}` + `test_monitors.c` (**27 PASS**, TDD RED→GREEN) —
  `monitor_t` (GE/LE threshold, debounce = N CONSECUTIVE samples, fire-ONCE latch + re-arm-on-clear
  hysteresis; debounce 0 treated as 1; NULL-guarded), `monitor_delta_step` (per-boot cumulative-counter
  delta; first-call and boot_id-change baseline to 0 — the episodic/JACT boot_id precedent),
  `monitor_build_snapshot` (per-type fixed literals + decimals only, cap-bounded `MON_SNAP_MAX`, truncation
  NUL-safe). T7 pins the keyword-clean discipline BOTH ways: every event type's snapshot scans clean against
  the canonical blocklist AND passes the REAL `shield_assess(ACTION_NOTIFY_ANOMALY, <snapshot>, NULL, 0)`
  un-BLOCKED (the test_km2b_trigger discipline — a monitor NOTIFY can never block itself). CI step
  "Phase 6: 6-1 Monitor framework (C)" (links monitors.c + shield_action.c + action_allowlist.c +
  shield_learn.c). Links NOTHING into the deployed path (host-only; M1 is the first box wiring — deploy
  unaffected, no gating concern yet).
- **M1 ✅ DONE 2026-07-08** (two commits, each box-gated):
  - **Commit A (`7a4d7ba`) — the spine extract, behavior-neutral on the DEPLOYED self-heal.** The scoped
    two-helper split (Locked decision 4, refined): `spine_decide` = a VERBATIM move of the funnel's
    `shield_assess` → `action_lookup` → `trust_policy` sequence (pure, zero globals); `spine_record` = the v7
    counter bumps + the single JACT block. `pa_restart_pb` keeps its latch, trigger render, the ENTIRE
    execute body, serial lines, crash-loop bound, and `km2b_miss_on_pb_ack` verbatim — no callback, no
    switch; each caller keeps its own execute body at its call site. **Box re-verify (KVM `-smp 6`,
    ACTIONS=1/PROBE=1): 5/5 + 5/5 + 5/5 `recovery=COHERENT`, 0 phantoms, 0 `[FAULT-REJECT]`, 0 REFUSED;
    `[RESTART]` serial format identical; JACT byte-matched (15 new records all `action=1 NOTIFY/EXECUTED/OK
    risk=10`, zero field drift vs the boot_id=13 baseline); the HARDLOOP tail reproduces the committed
    Outcome-B signature unchanged; deploy-config healthy smoke 0 spurious `[RESTART]`; OFF (`ACTIONS=0`)
    compiles clean.**
  - **Commit B — the first watcher live.** `JARVIS_MONITORS` (default-0, compile-guarded to require
    `JARVIS_ACTIONS`) + `JARVIS_MONITOR_PROBE` (default-0, its OWN flag — `ACTION_PROBE` runs end at the
    committed HARDLOOP Outcome-B starve and never reach the workload): the q_errors-delta watcher at the
    `[STATS]` cadence (`MON_ERRRATE_THRESHOLD` 5/window, `MON_ERRRATE_DEBOUNCE` 2; M1 defaults — the
    calibration pass is M2) → `monitor_delta_step` → `monitor_sample` → on fire: `monitor_build_snapshot` →
    `spine_decide(ACTION_NOTIFY_ANOMALY, …)` (learn map passed when `JARVIS_SHIELD_LEARN`, the K/M1 probe
    pattern) → one `[ANOMALY]` line + `spine_record` (EXECUTED/OK). Deliberately NO restart latch and NO
    `g_pb_dead` gate (a NOTIFY must fire even degraded); fire-once/debounce are `monitor_t`'s job. **Box
    gate (KVM `-smp 6`, MONITORS=1/MONITOR_PROBE=1, ACTION_PROBE=0): `[MON-PROBE]` windows 1–3 → EXACTLY ONE
    `[ANOMALY] mon err-rate d=10 win=100 risk=0` at window 2 (debounce), window 3 sustained did NOT re-fire,
    the 736 later real-0 windows never fired (zero NOTIFY spam to q=73,900, err=0, 0 `[RESTART]`); JACT:
    exactly one `action=2 AUTO/EXECUTED/OK risk=0 trigger="mon err-rate d=10 win=100"` (keyword-clean, never
    BLOCKED). OFF (`JARVIS_MONITORS=0`, the deploy default) compiles the watcher out (RC=0).**
- **M2 ✅ DONE 2026-07-09 — the honest monitor set (3 added watchers on the M1 framework+spine; heartbeat-age
  DEFERRED).**
  - **The deferral:** a heartbeat-age watcher is NOT in 6-1. On the cache-heavy deployed workload it
    conflates "PB is down" with "the workload asked PB nothing for a while" (cache serves repeats; HB/shield
    sends are workload-lane-paced), and it overlaps the K/M2c consecutive-miss wedge detector, which already
    catches the real failure. `[HB-AGE]` stays INSTRUMENT-ONLY (K/M2c D1); a calibrated age backstop remains
    future work if a real gap ever shows.
  - **W1 self-heal-rate** (`MON_EV_SELF_HEAL_RATE`): `g_restart_count` window delta ≥ 2, debounce 1 — early
    warning BEFORE the crash-loop bound (5). **W2 store-wrap** (`MON_EV_STORE_WRAP`): an ABSOLUTE GE latch on
    each store's monotonic `hdr.total_entries` at capacity (episodic 8192 / JACT 4096 / semantic 4096 when
    enabled) — NOT a delta (totals persist across boots); fires ONCE at the FIRST wrap; **armed at init only
    if the store has not already wrapped** (an already-rolling store logs one `[MONITOR] … already rolling`
    info line, no event — else every boot would re-notify a historical wrap). **W3 uptime-milestone**
    (`MON_EV_UPTIME_MILESTONE`): fire-once marks at boot-relative ELAPSED 1h/24h/7d (`jarvis_uptime_ms` is
    uint32, wraps ~49.7 d → marks capped ≤ 7 d; NEVER wall-clock — no RTC, Locked decision 1). All four
    watchers share ONE notify path (`mon_notify`, factored verbatim from the M1 fire block): snapshot →
    `spine_decide` → `[ANOMALY]` line → `spine_record` — one JACT record per crossing.
  - **Host pins:** `test_monitors.c` **41/41** (+T9 wrap absolute-latch never re-arms on monotonic growth;
    +T10 multi-mark each-fires-exactly-once; +T11 heal-delta re-fires only on a NEW burst; the existing T7
    loop already pins every M2 snapshot type keyword-clean + un-BLOCKED through the real `shield_assess`).
  - **Calibration (measured, not guessed):** boot_id=15 bare-metal (durable log q=166k→301k + 147 on-wire
    packets): q_errors delta flat 0 and restart_count flat 0 → the err-rate (5/win) and heal-rate (2/win)
    thresholds have effectively unbounded margin. Episodic on both the box and the KVM image is ALREADY
    rolling (box total=8192=cap on-wire; KVM total ≈ 1.02 M) → W2's arm-only-if-unwrapped rule is what keeps
    it per-boot silent; JACT lifetime ≈ 160/4096 (years from wrapping at healthy rates); gate-length uptimes
    (KVM ~500 s to first `[STATS]`, box ~95 s boot) sit far below the 1 h first mark.
  - **Box gates (KVM `-smp 6`): OFF** (`MONITORS=0`) — **machine-code-identical, proven at the object level**:
    `.text`/`.rodata`/`.data`/`.bss` + the symbol table of `main.c.obj` byte-identical vs the committed M1
    build; the ONLY delta is 11 bytes of `.debug_line` DWARF (shifted source lines), which then avalanches
    through the image's COMPRESSED payload (405 k image bytes differ from 11 bytes of debug metadata) —
    documented so nobody md5-compares packed images again. **ON-silent** (`MONITORS=1`, no probe, 900 s):
    **0 `[ANOMALY]` over 731 windows** (q=73,100, err=0, 0 restarts); one honest `[MONITOR] episodic already
    rolling total=959853` info line; the JACT wrap watcher armed silently (159/4096). **ON-probe**
    (`+MONITOR_PROBE=1`, short marks + wrap thresholds total+1 + 2 REAL respawns): **exactly 7 `[ANOMALY]`,
    each type once** — 3 uptime marks (first window, ms=200212), err-rate d=10 (debounce window), store-wrap
    id=1 total=1022054 + id=2 total=164, heal-rate d=2 (the window after the 2 real `[RESTART] reason=hang
    EXECUTED OK` respawns — an HONEST induction: real `km2b_do_respawn` cycles, real JACT respawn records,
    truthful wire counters) — and **zero re-fires over the remaining ~720 windows** (q=72,400, err=0,
    0 `[FATAL]`). **JACT read-back:** boot 19 = exactly 7 `action=2 AUTO/EXECUTED/OK risk=0` records with
    the keyword-clean snapshots + the 2 `action=1` respawns, monotonic seq.

## 7. Done-when

- ≥3–4 REAL monitors from the honest set run at the [STATS] cadence; a threshold crossing emits a
  fire-once-per-crossing NOTIFY through the spine → serial `[MONITOR]` line + JACT record + the M3 console
  surface (live, honest, `—` until the flag is on).
- Idle cost = a measured µs/sweep figure; structurally zero per-`seL4_Yield` monitor work.
- OFF (`JARVIS_MONITORS=0`) byte-identical to the deployed image; the default-ON flip is a deliberate,
  box-proven decision (supervised run, no false-positive spam).
- The K/M4 self-heal gates still pass bit-for-bit after the spine refactor.

## 8. Honest ceiling

> 6-1 is **threshold watchers over real observable state** — not anomaly-detection ML, not a real clock, not
> CPU telemetry, not prediction. "The system anticipates" means exactly: these watchers surface unusual
> observed patterns (error spikes, stalled TX, unusually frequent self-heals, store wraps) through the same
> SHIELD-scored, JACT-audited spine the self-heal uses — nothing more. Reaction to an event (wake, briefing)
> is 6-2/6-3; a real clock is the RTC follow-up; inbound control is 6-5.

---

*Companion to `phase6/docs/PHASE_6_PLAN.md` (goal 6-1) and `PHASE_6_GOAL_K_SYSTEM_DESIGN.md` (the spine this
goal rides). Ground truth verified against HEAD at authoring (2026-07-08).*
