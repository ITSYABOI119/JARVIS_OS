# JARVIS AI-OS: Phase 6 Final Report

**Version:** 1.0
**Date:** 2026-09-05
**Phase:** Phase 6 — Butler (Months 36+)
**Status:** CLOSED 2026-09-05 (ADR 2026-09-05-close-phase-6-defer-supervised-exit). Goal 6-7's done-when met in substance by the 2026-08 unattended soak; the supervised run deferred, owner-scheduled.
**Author:** JARVIS Development Team (Solo Developer)
**Hardware:** JARVIS PC — Ryzen 7 2700X, 16 GB DDR4 (`total_ram_mb` 16025 at boot 55), 2 TB NVMe (Lexar NM790), Intel I211-AT, ASUS X470-F

> **Prerequisite note:** Phase 5 (Memory) closed with `v1.1.0-memory` (commit `feeafd1`); Phase 4 shipped `v1.0.0` (tag `bdf0951`, 2026-06-26). This report mirrors `phase4/docs/PHASE_4_FINAL_REPORT.md` and is built from the goal docs, the 6-5 final report, the 2026-08 soak report and the CLAUDE.md rows — never from memory. Every section ends with a `Sources:` line; every number in it is read from the named file or commit, and the only fresh measurement is §8.

---

## 1. Executive Summary

Phase 6's goal, quoted from the roadmap: *"JARVIS behaves like a butler — anticipates, monitors, and acts when appropriate, not only on direct commands."* What shipped, one line per goal, all default-ON on the deployed image:

- **K — it-acts:** `JARVIS_ACTIONS` default-ON 2026-07-08 (`34a165e`), supervised boot 15.
- **6-1 — always-on monitors:** `JARVIS_MONITORS` default-ON 2026-07-09 (`161acd3`), boot 16.
- **6-2 — event-driven wake:** `JARVIS_WAKE` default-ON 2026-07-12 (`4f4487f`), boot 18.
- **6-3 — proactive INFORM behaviors:** `JARVIS_PROACTIVE` default-ON 2026-07-13 (`56647b7`), boot 19.
- **6-5 — control-IN:** `JARVIS_CONTROL_IN` default-ON 2026-07-21 (`a9c1d9a`), boot 30; cross-session recall `JARVIS_CONTROL_IN_RECALL` default-ON 2026-07-22 (`1fd505d`), boots 32–34.
- **6-6 — query routing:** `JARVIS_ROUTING` default-ON 2026-07-23 (`d4be861`), boot 38.
- **Phase C (neural rider):** `JARVIS_EMBED` default-ON 2026-08-01 (`a924044`), boot 48; `JARVIS_ROUTE_VETO` default-ON 2026-08-02 (`7c80dd6`), boot 49.
- **6-7 — the 7-day supervised exit:** its done-when is **met in substance** by the 2026-08 unattended soak; the goal's **supervised clause was not exercised as designed**; that residual is **accepted**; the supervised run is **deferred, owner-scheduled**. This is not a pass of the supervised run; no supervised week was run.

**The honest ceiling, in the words of the CLAUDE.md rows.** SEC-039 is closed for the ACTION path and for control-IN queries; the workload PA↔PB lane stays passive/ALLOW by design — never "SHIELD blocks queries". The query SHIELD refuses four DEFINED ABUSE CLASSES (key-extraction / bulk-exfil / canned-jailbreak / config-disclose) at a measured FP of 0/100 on realistic traffic; it is NOT a general injection detector — general prompt injection is contained STRUCTURALLY (inbound text can never mint an action), contained, not detected. The control-IN reply is SIGNED (HMAC-SHA256), NOT ENCRYPTED. Semantic recall reaches about half of paraphrases — 19/36 = 53 % at the 0.55 floor — and a miss degrades to EXACTLY today's no-preamble path; never "semantic recall works". The routing veto is an 81 % measured cut in ONE defect class, 32 → 6 false positives at the cost of 1 false negative; the 6 residual FPs and the 1 FN ARE the ceiling, and "routing is fixed" is never written. And the running kernel configuration — `KernelFastpath=ON` with XSAVE and SMP — is outside seL4's verified X64 set: a functional-but-unverified seL4 configuration by design.

Sources: `phase4/docs/ROADMAP.md:82` (the goal sentence); CLAUDE.md Phase 6 table row (flip dates and boots) and the rows "Query Router", "Control-IN Query SHIELD", "C/M2b — SEMANTIC RECALL WIRED", flag rows `JARVIS_ROUTE_VETO` and `JARVIS_EMBED`, the §Codebase Metrics Security bullet, and the §Architecture footnote; `git log` for the flip commits; `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md`.

---

## 2. What Shipped

### Goal K — "it-acts" (the keystone)
The self-heal action spine: a STATIC compile-time action allowlist (the LLM selects an id, never synthesizes one — the structural prompt-injection boundary), the SHIELD action gate `shield_action.c` linked into Process A, the raw-LBA JACT action-audit store at LBA 21,120,000, and PB respawn on both crashes (the fault-EP receiver) and wedges (the `km2b_miss` consecutive-miss counter) — every restart SHIELD-scored and JACT-audited. Flipped default-ON at K/M4, 2026-07-08 (`34a165e`), supervised boot 15: telemetry v7 crc_ok, `TLM_F_ACTIONS` set, restart/fired/blocked honest-0, `err=0`, NN=6. **Honest ceiling:** this closes SEC-039 FOR THE ACTION PATH; the QUERY path stays passive (Process B returns ALLOW) BY DESIGN — it does NOT block queries. A hard same-core busy loop is undetectable by the spine (the K/M4 Outcome-B experiment); the run plan names power-cycle as its remedy.

Sources: CLAUDE.md rows "Phase 6 Goal K (it-acts KEYSTONE)", "Action Allowlist", "SHIELD Action Gate", "Action-Audit Store", "Hang/Wedge Miss-Counter", flag row `JARVIS_ACTIONS`, and the §Codebase Metrics Security bullet; `git log` (`34a165e`); `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md:166–167`.

### Goal 6-1 — Always-on monitors
`monitors.c`: lightweight C watchers over REAL observable state — q_errors-delta, self-heal rate, store wrap, uptime milestones — each with debounce and a fire-once latch with re-arm-on-clear, firing a NOTIFY through the K spine into `[ANOMALY]`, JACT and its own telemetry slice (`TLM_F_MONITORS`). Flipped default-ON 2026-07-09 (`161acd3`), boot 16; the first non-zero wire values came at boot 17 (a 28.7 h unattended run crossing the 1 h and 24 h uptime marks — `monitors_fired=2`, zero false positives). Heartbeat-age was DEFERRED; CPU % is excluded as fiction (Process A busy-polls). **Honest ceiling:** `monitors_fired` is a NEUTRAL debounced event count — a mix of degradation signals and benign liveness events — never "anomalies/problems detected".

Sources: CLAUDE.md rows "Phase 6 Goal 6-1 (Always-On Monitors)", "Monitor Framework", flag row `JARVIS_MONITORS`; `git log` (`161acd3`).

### Goal 6-2 — Event-driven wake
`wake.c`: a monitor crossing with a FIXED, human-reviewed template stages one wake; ONE dispatch site gates it on a per-type 10-minute cooldown and an hourly budget (tightened 4 → 2 at the flip), then cache-lookup FIRST with PB inference on a miss; a wake timeout never bumps `q_errors`. Flipped default-ON 2026-07-12 (`4f4487f`), boot 18: 185 packets all crc_ok, `wakes_fired` honest-0 on the healthy run. **Honest ceiling:** the ceiling stays event-triggered dispatch of a PRE-TEMPLATED question — inform-only, no actuator, never "thinking" or "reasoning".

Sources: CLAUDE.md rows "Phase 6 Goal 6-2 (Event-Driven Wake)", "Wake Decision Core", flag row `JARVIS_WAKE`; `git log` (`4f4487f`).

### Goal 6-3 — Proactive actions
`behaviors.c`: the compile-time butler behavior catalog B1 anomaly-consult, B2 self-heal-consult, B3 store-roll notice, B4 status digest (integer-only system facts at the 1 h / 24 h / 7 d marks), B5 degraded-mode alert, under a global cap of 6 informs per hour; measured healthy false-positive baseline 0/1 = 0 %. Flipped default-ON 2026-07-13 (`56647b7`), boot 19: first on-wire v10, behaviors honest-0 on the sub-1 h run. **Honest ceiling:** 5 or more bounded templated INFORM behaviors — NOT autonomy, NOT interaction, NOT free-form.

Sources: CLAUDE.md rows "Phase 6 Goal 6-3 (Proactive Actions)", "Behavior Registry", flag row `JARVIS_PROACTIVE`; `git log` (`56647b7`).

### Goal 6-5 — Control-IN / natural language primary (with cross-session recall)
The two-way authenticated conversation channel — the box's first standing untrusted network inbound — shipped behind a 6-item hard checklist, all closed: a hardened, fuzzed inbound parser; HMAC-SHA256 authentication with constant-time verify and a cross-reboot NVMe-persisted replay floor; a scheduling-backed rate limit; a real QUERY SHIELD closing SEC-039 for control-IN queries; the SEC-014 least-privileged `jarvis-input` process (no NIC caps, no key, no rings); and the I211 RX bring-up. The reply is a JRPL v2 payload HMAC-signed over its CRC'd bytes, unicast to the provisioned console address only. Flipped default-ON 2026-07-21 (`a9c1d9a`), boot 30: 15/15 sustained queries answered, a 24-frame flood limited at CAP=8 then 3/3 recovery, `err=0` at q=175,600, the audit trail label-only (all 25 raw-query probes zero). Cross-session recall followed 2026-07-22 (`1fd505d`), boots 32–34, on a dedicated control-IN store at LBA 21,140,000: a fresh session recalled an unknowable marker (`recall=1`). The 6-5 arc is 41 files / 6,733 LOC with 13 CI steps, as recorded at the flip. **Honest ceiling:** a control-IN message is a QUERY, not a shell; the reply is SIGNED, NOT ENCRYPTED; the query SHIELD refuses defined abuse classes and is not a general injection detector (containment is structural); recall at 6-5 was EXACT-REPEAT only — paraphrase recall arrived with Phase C.

Sources: `phase6/docs/PHASE_6_GOAL_6-5_FINAL_REPORT.md` (header, §13); CLAUDE.md rows "Phase 6 Goal 6-5 (Control-IN)", "Control-IN MODULE INDEX", "Control-IN Reply Builder", flag rows `JARVIS_CONTROL_IN` and `JARVIS_CONTROL_IN_RECALL`; `phase4/docs/ROADMAP.md:93`; `git log` (`a9c1d9a`, `1fd505d`).

### Goal 6-6 — Query routing
`route.c`: a pure, host-testable keyword intent classifier routing each validated control-IN query to SYSTEM-FACTS (answered from a host-whitelisted set of box-state fields), INFERENCE, or an honest DECLINE for status metrics the box does not track — all three leaving through the SAME signed, audited, counted exit. The literal device/network/filesystem/user specialists were RETIRED by a documented reframe (they were Phase-1 Python only; none ever ran on the box). HELDOUT 70/73 = 95.89 % keyword-blind, DEV 64/64, 0 INFER misroutes. Flipped default-ON 2026-07-23 (`d4be861`), supervised boot 38 — after a live DECLINE false positive caught on hardware forced a retune (`5224a85`). **Honest ceiling:** a held-out score is a POINT ESTIMATE, not production accuracy; this is a KEYWORD router; its bare-word SYSFACTS false-positive class (32 FPs measured 2026-07-27) was CUT, not fixed, by the Phase C veto.

Sources: `phase4/docs/ROADMAP.md:93`; CLAUDE.md rows "Query Router", "Phase 6 Goal 6-6 (Query Routing ≥95%)", flag row `JARVIS_ROUTING`; `git log` (`d4be861`, `5224a85`).

### Phase C — the neural rider (embedder, semantic recall, routing veto)
Qwen3-Embedding-0.6B (Q8_0, 639,150,592 B) co-resident with Gemma in Process B, port proven to float32 epsilon against the host reference; embeddings travel over a dedicated 2-page region (never the response ring); vectors persist in the JVEC store at LBA 21,150,000; recall compares 128-dim mean-projected unit vectors at a 0.55 floor. `JARVIS_EMBED` flipped default-ON 2026-08-01 (`a924044`), boot 48: 4 of 6 paraphrase opportunities recalled, zero false recall observed. `JARVIS_ROUTE_VETO` flipped default-ON 2026-08-02 (`7c80dd6`), boot 49: on a SYSFACTS capture only, the embedder may reroute to the model under the parameter-free rule sim(INFER) > sim(SYSFACTS); 8/8 pre-registered legs matched. The arc is now dormant. **Honest ceiling:** about half of paraphrases recall (19/36 = 53 % at the 0.55 floor) and a miss degrades to exactly the no-preamble path — never "semantic recall works"; the veto is an 81 % measured cut in ONE defect class (32 → 6 FP at 1 FN), a genuine status question pays ~300–800 ms, and "routing is fixed" is never written.

Sources: CLAUDE.md rows "C/M2b — SEMANTIC RECALL WIRED", "Embedding Vector Store", "Embed Region staleness predicate", flag rows `JARVIS_EMBED`, `JARVIS_ROUTE_VETO`, and the Phase 6 table row; `git log` (`a924044`, `7c80dd6`).

### Goal 6-7 — the 7-day supervised exit (disposition)
Not run as designed. The 2026-08 unattended soak (telemetry `boot_id=54`) ran 7 days 18 h 18 m 42.851 s in a single boot, ended by a grid power outage; `err=0` was the only error value in every witness; its JACT boot group is exactly 42 records — 3 status digests + 39 control-IN turns — all `AUTO`/`NOTIFY`, with nothing above trust level 1 in the deployed allowlist to approve; 0 non-scheduled informs of 3 scheduled digests; 39/39 control-IN turns answered including a 12-turn adversarial battery that minted nothing. That run was not pre-declared as 6-7, exercised no 48-hour checkpoint gate, and had the operator present only for two control-IN sessions. **Disposition (ADR 2026-09-05):** the done-when is met in substance; the supervised clause was not exercised as designed; the residual is accepted; the supervised run is deferred, owner-scheduled, from the unchanged run plan. Not a pass of the supervised run; no supervised week was run.

Sources: `phase6/docs/SOAK_2026-08_FINAL_REPORT.md` §1 (:13–19), §2 (:35), §3 (:64–68), §4 (:73–92); `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` §5, §7, §8; `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md`.

---

## 3. Metrics Scorecard

| Metric | Result | Sources |
|--------|--------|---------|
| Deployed default-ON flags | 13: `JARVIS_DBG_STATS`, `JARVIS_DBG_INFER_SUMMARY`, `JARVIS_G3_RETRIEVAL`, `JARVIS_CACHE_GROWTH`, `JARVIS_ACTIONS`, `JARVIS_MONITORS`, `JARVIS_WAKE`, `JARVIS_PROACTIVE`, `JARVIS_CONTROL_IN`, `JARVIS_CONTROL_IN_RECALL`, `JARVIS_ROUTING`, `JARVIS_EMBED`, `JARVIS_ROUTE_VETO` | `grep -E '^#define JARVIS_[A-Z0-9_]+[[:space:]]+1\b' phase3/src/sel4/jarvis_debug.h`, 2026-09-05 |
| `*_PROBE` flags | 16, all 0 — the deploy induces no synthetic events | `grep -cE '#define JARVIS_[A-Z0-9_]*PROBE[[:space:]]+[0-9]'` (16) and `…[1-9]` (0) on `jarvis_debug.h`, 2026-09-05 |
| Telemetry wire | v14, 276 B, CRC-32 in the last 4 bytes (`FMT_V14`, `PKT_SIZE_V14`); the receiver decodes v10–v14 by length | `phase3/scripts/telemetry_receiver.py:117,122` |
| Unattended soak — duration | 7 days 18 h 18 m 42.851 s, single boot, ended by grid power | `SOAK_2026-08_FINAL_REPORT.md:13–14` |
| Unattended soak — load and errors | `q_total` 132,731,400 at ~198 q/s; `err=0` the only error value in every witness; zero restarts / faults / anomalies / model-bad / degraded | `SOAK_2026-08_FINAL_REPORT.md:14–19` |
| Unattended soak — audit trail | 42 JACT records = 3 status digests + 39 control-IN turns, all `AUTO`/`NOTIFY`, zero BLOCKED | `SOAK_2026-08_FINAL_REPORT.md:35,79–80` |
| Unattended soak — 7-day mark | 168 h digest within 608 ms of the exact mark; 0 non-scheduled informs of 3 scheduled | `SOAK_2026-08_FINAL_REPORT.md:64–68` |
| Unattended soak — control-IN | 39/39 answered across two sessions; 12-turn adversarial battery minted nothing | `SOAK_2026-08_FINAL_REPORT.md:73–80,91–92` |
| Routing accuracy | HELDOUT 70/73 = 95.89 % keyword-blind, 0 INFER misroutes (a point estimate) | `phase4/docs/ROADMAP.md:93`; CLAUDE.md row "Query Router" |
| Semantic-recall ceiling | about half of paraphrases: 19/36 = 53 % at the 0.55 floor | CLAUDE.md row "C/M2b — SEMANTIC RECALL WIRED" |
| Routing-veto cut | 32 → 6 FP at 1 FN — an 81 % cut in one defect class | CLAUDE.md flag row `JARVIS_ROUTE_VETO` |
| Deployed inference (recorded benchmark) | Gemma 4 E2B 5.46 tok/s @ `NUM_NODES=6` (seL4 build, bare metal); the console renders the live measured figure | CLAUDE.md §Validated Metrics; `phase4/docs/PHASE_4_GOAL1_BENCHMARK.md` |
| CI | 4 jobs. Gating on every push: `test` 135 named of 136 steps + `model-tests` 7 of 9 = **142 named steps**. Manual, non-gating (`workflow_dispatch` only): `coverage` 3 of 5, `boot-smoke` 12 of 14 (a two-leg matrix). No duplicate keys. | strict duplicate-key-aware parse of `.github/workflows/ci.yml`, 2026-09-05 |

Sources: as per row; the flag greps, the CI parse and nothing else were measured fresh for this report.

---

## 4. Key Decisions (ADRs)

- **Remove dynamic model scaling** (`2026-04-17`) — a single deployed model; the Phase 6 stack runs one Gemma 4 E2B plus, since Phase C, one co-resident embedder.
- **Defer the 30-day x86 stability soak** (`2026-06-15`) — "a risk-accepted descoping — not a pass"; the precedent this phase's closure mirrors.
- **TurboQuant/RotorQuant deferred** (`2026-06-15`) — the KV-compression experiment stays parked; the deployed model is Q4_K_M.
- **Defer GPU inference** (`2026-06-16`) — CPU-only; every Phase 6 flip validated on the bandwidth-bound 6-core seL4 build.
- **x86 verification stance** (`2026-06-16`) — the deployed build is intentionally a non-verified seL4 configuration; every "verified" claim about the running system carries the caveat.
- **Enable seL4 SMP / Branch A** (`2026-06-17`) — `SMP=ON NUM_NODES=6`, the configuration every Phase 6 boot ran.
- **Headless appliance + remote console** (`2026-06-21`) — the box emits telemetry and the rich UI lives off-box; this ADR deferred control-IN to Phase 6, where goal 6-5 landed it.
- **`--target disk` full-SSD install** (`2026-06-25`) — code + dry-run only; the box stayed dual-boot throughout Phase 6, Ubuntu first.
- **Close Phase 6; defer the supervised 7-day exit as owner-scheduled** (`2026-09-05`) — this phase's closure: 6-7's done-when met in substance by the unattended soak, the supervised clause not exercised as designed, the residual accepted, the run deferred and owner-scheduled.

Sources: the nine files in `docs/decisions/` (titles and Decision sections); `phase4/docs/PHASE_4_FINAL_REPORT.md` §4 for the Phase 4 one-liners this list extends.

---

## 5. Success-Criteria Scorecard (honest)

Against `phase4/docs/ROADMAP.md` §Phase 6 "Done when" (lines 98–101):

| # | Criterion | Status |
|---|-----------|--------|
| 1 | At least one proactive action fired correctly without user prompt (logged + correct) | ✅ *met at K/M4 (2026-07-08, boot_id=15: SHIELD-scored, JACT-audited self-heal); goals #1–#3 have since deployed the monitor → wake → ≥5 INFORM-behavior chain.* |
| 2 | 7-day test completed with SHIELD audit trail showing no Level 2+ actions taken without approval | ✅ met in substance (unattended run, 7.76 d, 42-record JACT trail all AUTO/NOTIFY, 0/3 unscheduled informs) — the supervised clause NOT exercised as designed; deferred, owner-scheduled (ADR 2026-09-05) |
| 3 | Multi-agent routing test suite ≥95 % pass | ✅ *goal #6 COMPLETE 2026-07-23: HELDOUT 70/73 = 95.89%, keyword-blind, 0 INFER misroutes; reframed to handler routing (§1).* |
| 4 | You can hold a multi-turn conversation where JARVIS references prior sessions correctly | ✅ *MET; ticked 2026-09-01 as a docs correction.* `JARVIS_CONTROL_IN_RECALL` flipped default-ON 2026-07-22 and `JARVIS_EMBED` (2026-08-01) extended recall from exact-repeat to paraphrase; demonstrated on hardware at boots 34, 47, 48, 49/52 and in the soak's two operator sessions (39 turns, `recall=12`). The one carried caveat, the `[23:00110]` recall-provenance anomaly, was CLOSED 2026-09-03 by reconstruction from the box's own stored vectors — no leakage, no confabulation. (Full caveat text: `ROADMAP.md:101`.) |

**Net:** criteria 1, 3 and 4 met as recorded in the ROADMAP; criterion 2 met in substance by the unattended run, with the supervised clause of goal 6-7 not exercised as designed and the supervised run deferred, owner-scheduled. Not a pass of the supervised run.

Sources: `phase4/docs/ROADMAP.md:98–101` (criteria and their caveat text, italics copied verbatim for rows 1 and 3; row 4 condensed with the pointer to the full text); row 2's text from `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md` and `SOAK_2026-08_FINAL_REPORT.md` §1–§4.

---

## 6. Known Limitations

- **Not formally verified.** JARVIS's x86-64 build runs `KernelFastpath=ON`, which is outside the verified X64 config ("C-level functional correctness, no fast path") — a functional-but-unverified seL4 configuration by design (IPC/AVX performance over holding the proof). The soak is an empirical stability result, not a verified-kernel one.
- **SHIELD scope.** SEC-039 is closed for the ACTION path and for control-IN queries; the workload PA↔PB query lane stays passive/ALLOW by design. Never "SHIELD blocks queries" / "blocking active".
- **The query SHIELD is a coarse abuse-refuser, not a detector.** It refuses four DEFINED ABUSE CLASSES at a measured FP of 0/100; general prompt injection is contained STRUCTURALLY (inbound text can never mint an action; tagged-untrusted so it never contaminates the cache/retrieval stores; the answer is unicast to the provisioned console address only) — contained, not detected.
- **Signed, not encrypted.** The control-IN reply is SIGNED (HMAC-SHA256), NOT ENCRYPTED. Third-host non-observation of the unicast reply was MEASURED at a wired promiscuous third host (boot 52: 0 frames on :51002 against a 690-frame broadcast positive control; re-measured at boot 55: 0 against 2,041) — for THIS LAN, in its most-favourable CAM state; it rules out a hub, a mirror port or routine flooding and says nothing about a CAM-flood or ARP-spoof adversary.
- **Semantic recall reaches about half of paraphrases** (19/36 = 53 % at the 0.55 floor) and a miss degrades to exactly the no-preamble path. Never "semantic recall works".
- **The routing veto cut one defect class, 32 → 6 FP at 1 FN.** The 6 residual quantity-question FPs and the 1 FN are the ceiling; the veto moves a question to the model without making the model right. Never "routing is fixed".
- **The non-answered control-IN exits have never fired on hardware.** The timeout / empty / fault / degraded exits — including the T6 empty-response repair — remain host/KVM-proven only; the 2026-08 soak exercised the ANSWERED exit 39 times and no other.
- **`[PANEL]` is not durable at the deployed `JARVIS_DBG_BOOT_LOG=0`.** The on-screen panel's log mirror is a plain `puts_serial` inside a `BOOT_LOG`-gated capture, so `grep -c PANEL` over a real boot's durable log returns 0; UI state is verified from `[SNAP]`, which does ride `nvme_log_write`.
- **Return-path reply loss.** In the soak's two operator sessions 2 of 39 replies never rendered on the console; the box answered both (records exist, every box-side drop counter 0), so the loss sits on the unicast return leg or the receiver and is unattributable box-side.
- **The soak's workload is synthetic** — a PRNG load generator; control-IN is the only REAL interaction, owner-driven and sparse. The claim is "7 days STABLE under sustained synthetic load + real control-IN use", not "7 days of real-world autonomy". The <5 % false-positive-interrupt bound is not statistically demonstrable in one week (0/3 is the raw tally). 7.76 days is not a 30-day result.
- **Goal 6-7's supervised clause was not exercised as designed** — the operator was present for two control-IN sessions, not through the week judging interrupts live. Deferred, owner-scheduled (ADR 2026-09-05).
- **Goal #4 (user model)** rides Phase 5's semantic store — mechanism-proven, gated off; the deterministic distill compacts observable repeated Q&A, it never "knows preferences".

Sources: CLAUDE.md §Architecture footnote, the §Codebase Metrics Security bullet, the Phase 6 table row and Current paragraph (P-RIDER), rows "C/M2b — SEMANTIC RECALL WIRED", flag rows `JARVIS_ROUTE_VETO` and `JARVIS_EMBED`, the Bare-Metal Development Rules bullet on `[PANEL]` and the row "Model-load fail-closed", the SOAK ROW, and the row "Semantic Memory (Phase 5 #4/M0)"; `SOAK_2026-08_FINAL_REPORT.md` §4 (:86–89), §6 (:122–131), §7 item 5 (:252–253); `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` §8 (:177–189); `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md`.

---

## 7. Phase 7 Handoff

Facts that carry forward, each with its source:

- **The undeployed delta is ONE image-compiled commit:** `3f676a2` — the 2026-09-04 `-Wall` cleanup of `main_x86.c`, behaviour-neutral (PA `main.c.obj` byte-identical on `.text`, `.data`, every `.rodata*` and `nm`; the 16-byte `.text.startup` delta fully accounted), KVM-gated, not on the box. Whether the next run uses the deployed image or a rebuild carrying it is the operator's call.
- **The deployed image** is `ba94eb04f4efa76ef39d752d0fec02ff` (the PROVENANCE deploy, boot 55, 2026-09-03), with rollback `2c061aecdaf08dd9bd88d7d56061f4f4` retained as `.bak-pre-provenance` on the ESP and at `~/jarvis_image.bak-pre-provenance`; kernel `d22affe86cfd6d91d3bdc9a3c559df02`, grub.cfg `475ef885e5407e1c23195036fad9acc3`. Ubuntu keeps `BootOrder[0]`; no `BootNext` remains; the box is parked on Ubuntu.
- **The hardening backlog, as the record carries it:** the coverage instrument's worth-fuzzing follow-ups (274 gaps dispositioned = 150 worth-fuzzing + 102 unreachable-by-construction + 22 defensive-guard, `gguf_parser.c` first); two legacy ring modules (`src/ipc/ring_buffer.c`, `src/ipc/dual_ring_buffer.c`) linked into Process A with zero callers; the `LOG_ERROR` redefinition in `nvme_log.h` and three `crtn.o` `.note.GNU-stack` linker warnings surviving the `-Wall` cleanup, out of scope and on the hygiene backlog.
- **ROADMAP B3's remaining half:** the five-minute QEMU quickstart, plus the promotion of `boot-smoke`'s image-generation leg to per-push gating once it has a stability distribution.
- **Phase 7 goal 1's measured gap:** its done-when asks for associative retrieval on paraphrased queries at a test-suite pass rate ≥80 %; the deployed embedder measures about 53 % (19/36 at the 0.55 floor). That gap is the first Phase 7 measurement, not a Phase 6 defect.
- **Phase 7 goal 2 (30-day autonomous operation):** the 2026-08 soak's 7.76 unattended days with zero self-inflicted events is the current envelope; the 30-day duration remains unmet.
- **The supervised 6-7 run,** if and when the operator schedules it, runs from `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md` unchanged (readiness checklist §6, including the store and SMART baselines to be re-read on the parked box) and is reported as an addendum to this document.
- **A candidate Phase 7 direction, listed not decided:** the ambient voice wearable (`phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md`, IDEA STAGE 2026-09-01; pipeline-first, commands later via control-IN; ALL training on the Main PC or cloud, never the box).

Sources: `phase6/docs/PHASE_6_GOAL_6-7_SOAK.md:28,126–128` and CLAUDE.md's Current paragraph (the pending-deploy delta; `3f676a2` by `git log`); CLAUDE.md DEPLOYED STATE block (image, rollback, kernel, grub, boot order); CLAUDE.md row "Sanitizers + the coverage instrument (A8)" (274 = 150 + 102 + 22); `docs/CLAUDE_RECORD.md` §"Quick Reference — Shared Memory IPC" (the 2026-09-04 ring-modules bracket) and §"Quick Reference — x86 Rootserver" (`LOG_ERROR`, `crtn.o`); `phase4/docs/ROADMAP.md:152` (B3), `:124` (Phase 7 goal 1 done-when), `:114,123` (goal 2 and its done-when); CLAUDE.md row "C/M2b — SEMANTIC RECALL WIRED" (19/36); `SOAK_2026-08_FINAL_REPORT.md:122–125`; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md`; `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md`.

---

## 8. Codebase Stats

Measured 2026-09-05 at `a7dc8e2` with the CLAUDE.md §Codebase Metrics method — `git ls-files <phase> | grep -E '\.(c|h|py)$'` for the file list, `wc -l` over that list for LOC, a `test_`/`fuzz_` basename match for the test column — beside the 2026-09-04 figures recorded at `8b49727`:

| Scope | 2026-09-04 (CLAUDE.md) | 2026-09-05 (measured) | Delta |
|-------|------------------------|-----------------------|-------|
| Phase 0 | 4,919 LOC / 11 files / 0 tests | 4,919 / 11 / 0 | none |
| Phase 1 | 40,837 / 102 / 41 | 40,837 / 102 / 41 | none |
| Phase 2 | 27,268 / 65 / 8 | 27,268 / 65 / 8 | none |
| Phase 3 | 84,648 / 298 / 96 | 84,901 / 299 / 96 | +253 LOC, +1 file |
| Phase 4 | 1,885 / 3 / 3 | 1,885 / 3 / 3 | none |
| Phase 5 / Phase 6 | 0 code files (docs only; code lives in `phase3/src`) | 0 / 0 | none |
| phasec | 318 / 2 / 1 | 318 / 2 / 1 | none |
| **Total (phases 0–6 + phasec)** | **159,875 / 481 / 149** | **160,128 / 482 / 149** | **+253 LOC, +1 file** |
| Tracked files repo-wide | 1,036 | 1,045 | +9 |

The delta is fully accounted by `git diff --stat 8b49727..HEAD` on `.c`/`.h`/`.py` under `phase3`: `phase3/scripts/check_claude_record.py` new at 248 lines (the +1 file; the CLAUDE.md record-pointer CI invariant), `phase3/scripts/telemetry_receiver.py` +6/−2, `phase3/src/ai/test_gemma4_forward.c` +3 — 255 insertions, 2 deletions, net +253. The nine new tracked files are `docs/CLAUDE_RECORD.md`, `phase3/scripts/check_claude_record.py`, `phase3/scripts/ci_fetch_pinned_gguf.sh`, the five files under `phase6/tools/pi/`, and `briefings/2026-09-05-tech-briefing.md`. Phase 3 by directory today: ai 155 files / 35,877 LOC / 57 tests · drivers 50 / 16,767 / 18 · ipc 15 / 2,855 / 3 · net 26 / 4,235 / 9 · crypto 8 / 1,554 / 2 · sel4 8 / 12,983 / 0 (the seL4-only tier, untestable on the host by construction); 89 `test_*`/`fuzz_*.c` under `phase3/src` and 7 `test_*.py` under `phase3/scripts`.

Sources: measured 2026-09-05 (this section is the report's only fresh measurement, as the prompt allows); the 2026-09-04 column from CLAUDE.md §Codebase Metrics (recorded at `8b49727`); `git diff --stat 8b49727..HEAD` and `git diff --name-status 8b49727..HEAD`.

---

## 9. References

- Closure ADR: `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md`
- Phase 6 plan (as authored): `phase6/docs/PHASE_6_PLAN.md`
- Goal docs: `phase6/docs/PHASE_6_GOAL_K_IT_ACTS.md`, `PHASE_6_GOAL_K_SYSTEM_DESIGN.md`, `PHASE_6_GOAL_6-1_MONITORS.md`, `PHASE_6_GOAL_6-2_EVENT_WAKE.md`, `PHASE_6_GOAL_6-3_PROACTIVE.md`, `PHASE_6_GOAL_6-5_CONTROL_IN.md`, `PHASE_6_GOAL_6-5_FINAL_REPORT.md`, `PHASE_6_GOAL_6-6_ROUTING.md`, `PHASE_6_GOAL_6-7_SOAK.md` (the runbook for the supervised run), `PHASE_6_GOAL_C_EMBEDDER.md`, `PHASE_6_GOAL_C_M1B_DESIGN.md`
- The 2026-08 unattended soak: `phase6/docs/SOAK_2026-08_FINAL_REPORT.md`; Pi tooling: `phase6/tools/pi/README.md`
- Research closed during the phase: `phase6/docs/THINKING_MODE_RESEARCH.md`, `MODEL_BENCH_2026-07.md`, `MODEL_OPTIONS_2026-07.md`, `ANSWER_QUALITY_DESIGN.md`, `A10_CI_BOOT_FEASIBILITY.md`
- Roadmap: `phase4/docs/ROADMAP.md` (§Phase 6, §Phase 7, §Cross-phase backlog B3, §Beyond Phase 7); `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md`
- Prior phase reports: `phase4/docs/PHASE_4_FINAL_REPORT.md`, `phase3/docs/PHASE_3_FINAL_REPORT.md`; Phase 5 plan: `phase5/docs/PHASE_5_PLAN.md`
- ADRs: `docs/decisions/` (nine as of 2026-09-05)
- Working guide: `CLAUDE.md`; evidence ledger: `docs/CLAUDE_RECORD.md`
- Build and CI: `.github/workflows/ci.yml`; `phase3/src/sel4/jarvis_debug.h` (the flag defaults); `phase3/scripts/telemetry_receiver.py` (the wire version's single home)
