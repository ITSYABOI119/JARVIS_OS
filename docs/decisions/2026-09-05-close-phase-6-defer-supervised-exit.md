# ADR: Close Phase 6; defer the supervised 7-day exit (goal 6-7) as owner-scheduled

**Date:** 2026-09-05
**Status:** Accepted
**Deciders:** JARVIS Development

## Context

Phase 6 (Butler) set out with the goal *"JARVIS behaves like a butler — anticipates, monitors, and acts when appropriate, not only on direct commands."* (`phase4/docs/ROADMAP.md:82`). Every goal but the phase exit is complete, deployed default-ON, and carries bare-metal evidence:

- **K — "it-acts" keystone:** `JARVIS_ACTIONS` default-ON 2026-07-08 (`34a165e`), supervised boot 15 — the SHIELD action gate linked live, self-heal respawn SHIELD-scored and JACT-audited.
- **6-1 — always-on monitors:** `JARVIS_MONITORS` default-ON 2026-07-09 (`161acd3`), boot 16.
- **6-2 — event-driven wake:** `JARVIS_WAKE` default-ON 2026-07-12 (`4f4487f`), boot 18.
- **6-3 — proactive INFORM behaviors:** `JARVIS_PROACTIVE` default-ON 2026-07-13 (`56647b7`), boot 19.
- **6-5 — control-IN, natural language primary:** `JARVIS_CONTROL_IN` default-ON 2026-07-21 (`a9c1d9a`), boot 30; cross-session recall `JARVIS_CONTROL_IN_RECALL` default-ON 2026-07-22 (`1fd505d`), boots 32–34.
- **6-6 — query routing:** `JARVIS_ROUTING` default-ON 2026-07-23 (`d4be861`), boot 38; HELDOUT 70/73 = 95.89 % keyword-blind.
- **Phase C (the neural rider):** `JARVIS_EMBED` default-ON 2026-08-01 (`a924044`), boot 48; `JARVIS_ROUTE_VETO` default-ON 2026-08-02 (`7c80dd6`), boot 49.
- **Goal #4 (user model)** rides Phase 5's semantic store, mechanism-proven and gated off.

Goal 6-7 is the phase exit. Its canon (`ROADMAP.md:94`): *"7-day supervised autonomy — JARVIS runs 7 days with you present: proactive actions logged, zero unapproved high-risk actions, <5% false-positive interrupts."* Its done-when (`ROADMAP.md:99`): *"7-day test completed with SHIELD audit trail showing no Level 2+ actions taken without approval."*

The 2026-08 unattended soak (telemetry `boot_id=54`, JACT boot group 40, image `2c061aec…`) ran longer than the goal requires and produced the audit trail the done-when names: **7 days 18 h 18 m 42.851 s in a single boot, ended by a grid power outage, not by the box**; `q_total` 132,731,400 at a sustained ~198 q/s; `err=0` the only error value in every witness; zero restarts, faults, anomalies, model-bad or degraded states. Its JACT boot group is **exactly 42 records = 3 status digests + 39 control-IN turns, nothing else**, every record `AUTO` or `NOTIFY` (`action=4 AUTO/EXECUTED/OK` ×3, `action=5 NOTIFY/EXECUTED/OK` ×39). No Level 2+ action exists in the deployed allowlist to approve: `TRUST_REQUEST` and `TRUST_REQUIRE` have no entry, which the run plan calls structural. The 168-hour digest fired within 608 ms of the exact 7-day mark; the false-positive-interrupt tally is **0 non-scheduled informs out of 3 scheduled digests** (`monitors_fired=0` throughout). Control-IN saw real use: two operator sessions at uptime ≈3.10 d and ≈4.05 d (17 + 22 turns), **39/39 answered**, including a **12-turn adversarial battery** (delete-memory, disable-SHIELD, restart-PB, unrestricted access) that minted nothing.

What that run did **not** do: it was not pre-declared as 6-7; the run plan's 48-hour checkpoint gate was not exercised; and the operator was present only for the two control-IN sessions, so the goal's "with you present" clause was not exercised as designed. The run plan itself records that supervision-for-approval "has no object — there is nothing to approve" (§5, §8), that the <5 % bound "is NOT statistically demonstrable in one supervised week — report K/N raw", that the workload is synthetic with control-IN the only real interaction, and that the whole stack runs outside seL4's verified X64 configuration (§8).

The operator cannot commit the box to another week-long run at present and has chosen to close the phase and proceed.

Sources: `phase4/docs/ROADMAP.md:82,94,99`; CLAUDE.md Phase 6 table row (flip dates and boots); `git log` for the flip commits `34a165e`, `161acd3`, `4f4487f`, `56647b7`, `a9c1d9a`, `1fd505d`, `d4be861`, `a924044`, `7c80dd6`; `phase6/docs/SOAK_2026-08_FINAL_REPORT.md` §1 (:13–19), §2 (:35), §3 (:64–68), §4 (:73–80, :91–92); `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` §5 (:83–91, :96–100), §7 (:157–159), §8 (:177–189).

## Decision

**Close Phase 6.** Record goal 6-7's done-when as **met in substance** by the 2026-08 unattended run: a run of more than 7 days whose SHIELD audit trail — the 42-record JACT boot group — shows no Level 2+ action taken without approval, because none exists to approve and none escaped. Record the goal's **supervised clause as not exercised as designed**: the operator was present for two control-IN sessions, not through the week judging interrupts live. That residual is **accepted**. The supervised run is **deferred, owner-scheduled**: `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` remains the runbook, unchanged, and nothing in this record proposes when it runs.

**This is not a pass of the supervised run. No supervised week was run.**

Sources: `phase6/docs/SOAK_2026-08_FINAL_REPORT.md` §1–§4; `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` §5, §7, §8; `phase4/docs/ROADMAP.md:94,99`.

## Rationale

1. **The done-when is about the audit trail, and the trail exists.** 42 records, all `AUTO`/`NOTIFY`, nothing above trust level 1 to approve — structural, as the run plan §5 states (`action_allowlist.c` holds only L0–1 entries), and confirmed by the week.
2. **Duration exceeds the requirement.** 7.76 days > 7 days, in a single boot.
3. **The false-positive-interrupt bound was already declared not demonstrable in one week**; the measured tally is 0 non-scheduled informs of 3 scheduled digests.
4. **The supervised clause's remaining value** is the operator judging interrupts live and using control-IN through a week. Two sessions of 39 turns, including an adversarial battery, are a partial exercise of it, not the full one.
5. **Precedent.** Phase 3 closed with its 30-day x86 soak deferred as "a risk-accepted descoping — not a pass" (ADR 2026-06-15), and Phase 4 shipped `v1.0.0` with its 90-day soak recorded "❌ NOT DONE — owner-scheduled, not run" in its scoreboard.

Sources: `phase6/docs/SOAK_2026-08_FINAL_REPORT.md` §1 (:27), §2 (:35), §3 (:66–68), §4 (:73–92); `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` §5 (:83–91, :96–103), §8 (:183–184); `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md` (Decision, :15); `phase4/docs/PHASE_4_FINAL_REPORT.md` §5 (:89).

## Consequence

- **Phase 6 is CLOSED 2026-09-05.** The ROADMAP done-when for the 7-day test is ticked with caveat text stating the disposition; CLAUDE.md's phase table says CLOSED with the deferral; the Phase 6 final report is `phase6/docs/PHASE_6_FINAL_REPORT.md`.
- **Phase 7 planning may begin.**
- **The supervised run, if and when the operator schedules it,** is run from `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` unchanged and reported as an addendum to the Phase 6 final report.

Sources: this ADR; `phase4/docs/ROADMAP.md` (Phase 6 status and done-when as edited alongside this ADR); `phase6/docs/PHASE_6_FINAL_REPORT.md`.

## What is preserved

- The run plan `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` — the runbook for the supervised run, with its readiness checklist (§6).
- The Pi capture and snapshot tooling under `phase6/tools/pi/` (the capture unit that has run since 2026-08-13, committed verbatim; the daily dated-snapshot timer).
- The SMART baseline (Lexar NM790 2TB, fw `18950`: `percentage_used` 1 %, `media_errors` 0, `unsafe_shutdowns` 160) and the store lifetime baselines, both to be re-read on the parked box immediately before any run (run plan §6).
- The rollback images: the deployed `ba94eb04…` (boot 55) with `2c061aec…` retained as `.bak-pre-provenance` on the ESP and at `~/jarvis_image.bak-pre-provenance`; Ubuntu keeps `BootOrder[0]`.

Sources: `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` §4 (:74–77), §6 (:115–148); CLAUDE.md DEPLOYED STATE block (the Current paragraph) and the Pi soak tooling row.

## What would trigger the supervised run

Only the operator's decision. Not a proposal from any session: the standing rule "never propose soak timing" is unchanged by this ADR.

Sources: CLAUDE.md Phase 6 table row ("never propose soak timing"); `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` §6 (:126–130, the owner's presence model).

## References

- `phase6/docs/SOAK_2026-08_FINAL_REPORT.md` — the 2026-08 unattended soak forensics (written 2026-09-01).
- `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` — the 6-7 run plan; stays the runbook.
- CLAUDE.md, the SOAK ROW ("SOAK ROW — STARTED 2026-08-13, boot_id=54") and the Phase 6 table row.
- `phase4/docs/PHASE_4_FINAL_REPORT.md` §5 — the scoreboard row for Phase 4's owner-scheduled 90-day soak.
- `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md` — the precedent: a risk-accepted descoping, not a pass.
- `phase4/docs/ROADMAP.md` §Phase 6 — goal 7's canon (:94) and done-when (:99).
- `phase6/docs/PHASE_6_FINAL_REPORT.md` — the Phase 6 final report written with this ADR.
