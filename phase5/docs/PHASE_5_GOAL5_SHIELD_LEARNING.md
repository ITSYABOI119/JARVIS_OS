# Phase 5 — Goal #5: SHIELD Learning (persisted failure-learning — MONITOR-ONLY, not a live blocker)

**Status:** 🚧 IN PROGRESS — **M0 ✅ host/CI (2026-07-03: `shield_learn.c/h` pure risk-map, Phase-1 parity, host-tested 28/28)** · **M1 ✅ LANDED (2026-07-03: gated `JARVIS_SHIELD_LEARN` PA wiring — boot-scan seed + [STATS]-cadence fold + `[SHIELD-LEARN]` summary + the gated `JARVIS_SHIELD_PROBE` D-d probe; box smoke = the gate)**; M2 (telemetry/console slice) next. **Arc 2 opener.**
**Date:** 2026-07-03
**Prereqs:** #1 episodic store ✅ box-verified (the failure SOURCE — `outcome ∈ {EPI_OUT_ERROR, EPI_OUT_BLOCKED}` records) + the #6-proven **boot-scan-seeded + batch-folded per-key aggregate** pattern (`cg_freq_bump/get` precedent) + decision-cache FNV-1a key parity (an episodic `query_key` is the shared key currency).
**Sources:** `phase1/src/ai/shield_framework.py:525-559` (`FailureLearningSystem` — THE port source); `phase5/docs/PHASE_5_PLAN.md` §7 locked decision 3 (learn + persist + MONITOR only; enforcement = Phase 6), §8 A2-2 (host+CI now; box = criterion-2 proof), §3 criterion 2; `phase4/docs/ROADMAP.md:64` (canon) + `:71` (done-when checkbox); SEC-039 (the live SHIELD stub — unchanged by this goal).

> **Goal (canon, `ROADMAP.md:64`):** SHIELD learning on bare metal — port failure-learning from the
> Phase 1 Python: failed actions increase risk score, persisted on NVMe.

---

## 1. Scope + done-when

- **#5 is Arc 2's opener** and a sibling of #6: it depends on the **episodic store only** (#1), reusing the exact promote-from-log shape #6 proved (boot-scan seed + `[STATS]`-cadence batch fold → an in-RAM per-key aggregate).
- **Closes Phase-5 criterion 2** (`PLAN §3` / `ROADMAP:71`): *"Repeated harmful action is blocked faster on second attempt (SHIELD learning verified)."* **Honest reading (PLAN §7.3):** Phase 5 proves the **learning signal** — the persisted risk score **RISES on the second attempt** — not live faster-blocking. The deployed system does not block (SEC-039: Process B always ALLOWs; the live PA check is a 6-term inline keyword list). Live enforcement is **Phase 6 #5**, behind the full inbound-control security checklist.
- **The deployed workload cannot prove it:** the benign canned workload runs at `q_errors=0` (874K-query soak, zero ERROR/BLOCKED records) — so the criterion-2 proof is a **synthetic-failure probe** (D-d), exactly the `JARVIS_G3_PROBE` precedent: inject a failing action twice, assert the learned risk rose between attempts.

| Concern | Owner |
|---|---|
| The durable failure record (ERROR/BLOCKED outcomes survive reboot) | **#1 (DONE)** — THE source |
| Learn: per-key risk aggregate raised by failures, monotonic-only | **#5 (this goal)** |
| Surface: `shield_learn_count`/max-risk telemetry + console row (monitor-only wording) | **#5** (M2, one deliberate slice) |
| Live blocking / enforcement / closing SEC-039 | **explicitly NOT #5** — Phase 6 #5 (security checklist) or the B2 "it-acts" keystone |

## 2. The port source (ground truth — Phase-1 parity)

`phase1/src/ai/shield_framework.py:525-559`, `FailureLearningSystem`:
- `record_failure(action, error, risk_score)` → `risk_adjustments[action_type] += 0.1`, then `min(0.5, …)` — **+10% per failure, capped at +50%, monotonic-raise-only** (nothing ever lowers it).
- `get_learned_risk_adjustment(action_type)` → the adjustment, `0.0` default.
- `get_failure_count(action_type)` → failures recorded for that type.

The C port keeps this arithmetic EXACTLY (+0.1/failure, cap 0.5, monotonic). One deliberate mapping change: Phase 1 keyed on `action_type` (the deployed episodic routes have only 2 type codes — too coarse to learn anything); the port keys on the **episodic `query_key`** (D-b) so "the same failing thing" is per-pattern, matching how #1/#3/#6 already key memory.

## 3. Locked decisions

- **D-a — Source = DERIVE from the episodic log's ERROR/BLOCKED records** (boot-scan-seeded + batch-folded per-key risk aggregate; the #6 pattern). *Alternative considered:* a dedicated SHIELD-state sub-region in the reserved ~8 GiB memory region (`PLAN §5` explicitly reserves one, and `episodic_store.c` is callback-driven so a second `epi_store_t` instance is cheap). *Why derive wins:* the failures are ALREADY durably persisted in #1 (outcome codes survive reboot — "persisted on NVMe" is satisfied by the source of truth); a second store would duplicate the same records behind a second write path (more wear, more code, a consistency question), while derive-from-#1 is zero new NVMe I/O and reuses the boot scan #6 already widened. The sub-region stays reserved for a future need (e.g. Phase-6 audit trail), not consumed now.
- **D-b — Learning = Phase-1 parity:** +0.1/failure, cap 0.5, **monotonic-raise-only**; key = episodic `query_key`.
- **D-c — MONITOR-ONLY:** no live block; the learned score is surfaced, never enforced. SEC-039 unchanged; enforcement = Phase 6 #5 (or the B2 keystone, deliberately).
- **D-d — Criterion-2 proof = a synthetic-failure probe** (`JARVIS_SHIELD_PROBE`-style, box-only, default 0): inject the same failing action twice; assert `[SHIELD-LEARN]` shows risk 0.1 → 0.2 between attempts (risk RISES on the 2nd attempt — the signal, honestly scoped).
- **D-e — Telemetry = one deliberate slice** (M2): a `shield_learn_count` (keys with learned risk) + max-risk field, fixture-synced (golden + key-contract + honesty + e2e together, the v2/v3/v4 precedent); console row worded monitor-only ("learns which actions fail" — NEVER "blocks").
- **D-f — Gated `JARVIS_SHIELD_LEARN`** (jarvis_debug.h, **default 0**, introduced at M1 with the box wiring): the whole derive/fold path compiles out when OFF; flag-OFF deploy behavior-identical.

## 4. Mechanism (M1 shape — mirrors #6/M1)

- **Boot seed:** the existing recall-scan (outer gate widens to include `JARVIS_SHIELD_LEARN`) feeds every persisted record with `outcome != EPI_OUT_OK` into `shield_learn_record_failure(g_shield_risk, …, rec.query_key)`.
- **Batch fold:** at the `[STATS]` cadence (before `epi_commit` clears the batch), fold this batch's ERROR/BLOCKED records the same way.
- **Proof line:** `[SHIELD-LEARN] keys=<n> max_adj=<x100> fails=<total>` at the [STATS] cadence — log-mirrored, honest counters only.
- **Never on the hot path:** no per-query work beyond what #6 already does; the aggregate is in-RAM; persistence is the episodic log itself (D-a).

## 5. The verification model — host-test first

- **Layer A (HOST/CI, M0 — DONE):** the pure risk-map (`shield_learn.{c,h}`) — parity ladder (+0.1 steps, cap 0.5 exact, monotonic), independence, collision probing, saturation (-1.0f), absent-key defaults — `test_shield_learn.c`, CI step "Phase 5: SHIELD-learning risk-map (C)".
- **Layer B (BOX, M1/M3):** derive-from-episodic wiring (gated) + the D-d synthetic-failure probe = the criterion-2 proof. Generation never runs in CI; the box smoke is the gate.
- **Honesty:** the deployed workload has no failures, so live [SHIELD-LEARN] counters will honestly read 0 — the flag-gated console row shows `—`/0 until a failure ever occurs; that IS the correct display.

## 6. Milestones

- **M0 (HOST/CI)** ✅ **DONE 2026-07-03** — `shield_learn.{c,h}` pure risk-map (Phase-1 parity arithmetic, open-addressed per-key slots, `fail_count==0` empty sentinel, full-table `-1.0f`) + `test_shield_learn.c` + the new CI step.
- **M1 (BOX)** — gated `JARVIS_SHIELD_LEARN` wiring in Process A: boot-scan seed + batch fold of ERROR/BLOCKED records; `[SHIELD-LEARN]` proof line; OFF = behavior-identical smoke.
- **M2 (CI + BOX)** — the deliberate telemetry/console slice (D-e), monitor-only wording, fixture-synced.
- **M3 (BOX)** — the synthetic-failure probe: same failing action twice → risk 0.1 → 0.2 across attempts (**criterion 2 closed, honestly scoped**).
- **M4** — docs + week status; flag default decision (flip is harmless — the fold is idle at err=0 — but stays a deliberate call like G3/G6).

## 7. Risks & landmines

- **Wording drift → overclaiming.** The one non-negotiable: nothing may read as "SHIELD blocks". The honesty gate already bans SHIELD-blocking language in the console; keep M2's row inside that fence.
- **Key granularity:** query_key-based learning means a failing *pattern* is learned, not an action *category* (Phase-1's type). Right for the current system (patterns are the unit of memory); revisit if Phase 6 actions get real type codes.
- **Wrap under-count:** deriving from the circular episodic store means failures older than the 8,192-record window are forgotten on re-seed (same benign wrap semantics as #6's frequency counts; monotonic within a boot).
- **Float parity:** the C port computes the adjustment as `min(cap, step × fail_count)` — behaviorally identical to Python's accumulate-then-clamp, and exact at every ladder step in IEEE single (tested).

## Honest ceiling (authored)

> **#5 learns which actions FAIL and how often, persists that signal across reboots (via the episodic
> log it derives from), and surfaces it — monitor-only.** It never blocks, never lowers a score, and
> never claims to protect the running system (SEC-039 stands until Phase 6). Criterion 2 is satisfied
> by the demonstrated signal — risk RISES on the second attempt of a failing action — proven by a
> synthetic probe, because the benign deployed workload (err=0 at 874K queries) has no failures to
> learn from. That honesty is the feature.

*Mirrors `PHASE_5_GOAL6_CACHE_GROWTH.md`; the plan it serves is `PHASE_5_PLAN.md` (§7 locked decision 3, §8 A2-2).*
