# Phase 5 — Week 03 Status (Memory)

**Period:** 2026-07-03 → 2026-07-04
**Phase:** 5 (Memory) — Arc 2: **#5 SHIELD failure-learning (MONITOR-ONLY)** + **#4 semantic memory kickoff**
**Branch:** `master` · **This week's HEAD:** the #5/M2 telemetry-v5 commit (after `bb0f902`)
**Author:** JARVIS Development

---

## Summary

**#5 SHIELD failure-learning is functionally COMPLETE as a monitor (M0–M4) and stays GATED-OFF in
deploy.** The Phase-1 `FailureLearningSystem` port learns per-key failure risk from the episodic log
(+0.1/failure, cap +0.5, monotonic-raise-only), surfaces it through telemetry **v5** and a monitor-only
console section, and proves criterion 2 (risk RISES on a repeat failure) via the synthetic probe. It
never blocks — SEC-039 unchanged; enforcement is Phase 6. **Deliberate non-flip:** the benign deployed
workload runs err=0, so a default-ON monitor would honestly read 0 forever — the capability is proven
via transient probe smokes and activates in Phase 6. `JARVIS_SHIELD_LEARN`/`JARVIS_SHIELD_PROBE` remain
default-0; the deployed image is unchanged by #5.

## Done this week

### #5/M0 — host risk-map (2026-07-03, `e47c718`)
- `phase3/src/ai/shield_learn.c/h`: pure open-addressed `{key, risk_adj, fail_count}` risk map, exact
  Phase-1 parity arithmetic; keys = episodic `query_key` (FNV-1a parity with #1/#3/#6).
- `test_shield_learn.c` 28/28 (parity ladder incl. the criterion-2 rise, monotonic, collision,
  saturation) + CI step "Phase 5: SHIELD-learning risk-map (C)".

### #5/M1 — box wiring (2026-07-03, `bb0f902`)
- Gated `JARVIS_SHIELD_LEARN` Process-A wiring: boot recall-scan seeds the map from persisted
  `outcome != EPI_OUT_OK` records (D-a derive-from-episodic — no second NVMe store); `[STATS]`-cadence
  batch fold BEFORE `epi_commit`; `[SHIELD-LEARN] keys=/maxrisk_x100=/fails=` honest summary.
- Gated `JARVIS_SHIELD_PROBE` (D-d): injects the same failing marker action twice → `[SHIELD-PROBE]`
  shows the learned risk RISING on the repeat (criterion-2 gate).

### #5/M2 — telemetry v5 + monitor-only console (2026-07-04, this commit)
- **Wire:** `telemetry_packet_t` v4→**v5**: appends `uint16 shield_learn_keys` +
  `uint16 shield_learn_max_risk_x100` → **222 B, CRC@218, version 5**, +`TLM_F_SHIELD_LEARN` 0x200.
  Fill is `#if JARVIS_SHIELD_LEARN` (same table walk as the `[SHIELD-LEARN]` summary; flag only when
  keys>0) → the flag-OFF deploy emits 0s + flag clear (honest).
- **Lockstep (the v2/v3/v4 precedent):** receiver decode/record/FLAG_NAMES; fixture `_DEFAULTS` +
  `FLAG_BITS`; golden json (infer frame carries keys=1/max=20 — the probe shape) + `golden.pcap`
  regenerated; `gen_golden_pcap` guards 222.
- **Console:** SHIELD screen gains the flag-gated **"Failure-learning"** section — worded
  *"learns which actions fail and raises their risk — monitor-only, not a live blocker; enforcement is
  Phase 6"* — showing "Actions with learned risk" (`shield_learn_keys`) + "Max learned risk"
  (`shield_learn_max_risk_x100`/100), `—` until the flag is live; Capabilities auto-row
  "SHIELD failure-learning (monitor-only)". NEVER a blocked-count.
- **Teeth:** honesty gate adds "SHIELD blocks"/"blocking active" to BANNED + asserts the monitor-only
  wording (53/53); e2e value-pins rendered == live `shield_learn_keys` + requires "not a live blocker"
  on-screen (24/24); receiver 106/106; telemetry C 47/47; logic 14/14.
- **Box smoke (transient, then restored):** `JARVIS_SHIELD_LEARN=1` + `JARVIS_SHIELD_PROBE=1` sed-flip,
  seL4 build + QEMU/KVM against the persistent `nvme_test.img` — the v5 build compiles, the boot
  recall-scan seeds prior probe failures, and the probe raises the learned risk further across attempts
  (monotonic from the seeded level), err=0. Flags restored to 0 — **NOT deployed-ON, no ESP write**.

### #4/M0 — semantic memory kickoff: plan doc + store + deterministic distill (2026-07-04)
- Plan doc `phase5/docs/PHASE_5_GOAL4_SEMANTIC_MEMORY.md`: honest ceiling ("compacts what it has
  repeatedly seen" — observable patterns, never "knows preferences"/"understands"; NO LLM/embeddings,
  PLAN §7.2/§7.6), D-a..D-f locked (separate store @ LBA 21,110,000; deterministic distill = #7's
  compact-core; distinct-from-#6 boundary; retrieval hook is a future G3 slice; gated `JARVIS_SEMANTIC`
  default-0 at M1), and the explicit note that **#4/#7 are completeness/Phase-6-readiness goals —
  Phase 5's canonical done-when is already met**.
- `semantic_store.c/h`: raw-LBA circular fact store (episodic_store clone; 4096 × 512 B
  `semantic_fact_t`, magic "JSEM") + `sem_store_upsert` — insert-or-raise-support, support monotonic
  via max (idempotent across boot re-distills), a repeated subject updates its fact, never a dup.
  `test_semantic_store.c` 6/6.
- `semantic_distill.c/h`: `sd_distill` — group by `query_key`, support counted over usable records
  only (`sd_record_usable` = INFER+OK+resp>0), newest answer wins, `confidence_x100` = share of
  byte-identical same-key answers, emit at support ≥ `SEM_MIN_SUPPORT`(3); text by `resp_len`, never
  strlen. `test_semantic_distill.c` 8/8 (incl. FNV-1a key parity via the real `episodic_fill`).
- Two new CI steps: "Phase 5: Semantic store (C)" + "Phase 5: Semantic distill (C)".

## Tests / verification
- Host suites green locally: shield_learn 28, telemetry C 47, receiver 106, honesty 53, logic 14,
  e2e 24; golden-drift gate regenerated (2264-byte pcap). CI green after push.
- **Honesty note:** live `[SHIELD-LEARN]`/telemetry counters would read 0 on the deployed workload
  (err=0) — that IS the correct display; the console row shows `—` until `TLM_F_SHIELD_LEARN` is live.

## Next
1. **#4/M1** — gated `JARVIS_SEMANTIC` box wiring (boot-scan + batch distill → `sem_store_upsert`,
   `[SEM]` proof line), then M2 telemetry slice / M3 reboot-survival / M4 flag decision.
2. **#7 consolidation** — the remaining scope (prune + low-prio job scheduling; its compact-core
   already landed in `sd_distill`) — plan doc first.
3. Backlog (ROADMAP): B1 self-healing PB restart, B2 "it-acts" keystone (the real SEC-039 closure
   path), B3 QEMU quickstart + CI generation smoke.
4. User: the proposed `memory` tag (Arc 1) remains open — user names/creates tags.

## Notes / risks
- #5's honest ceiling stands: it learns and surfaces failure risk, never blocks, never lowers a score
  (see `PHASE_5_GOAL5_SHIELD_LEARNING.md` — the authored ceiling). Wording drift toward "SHIELD blocks"
  is now a CI-failing regression (honesty gate).
- The episodic store on `nvme_test.img` persists probe failure records across QEMU runs — future probe
  smokes start from the seeded risk level (still rises monotonically; cap +0.50).
