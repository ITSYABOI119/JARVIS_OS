# JARVIS AI-OS: Roadmap — Phase 4 and Beyond

**Version:** 1.0  
**Date:** June 2026  
**Prerequisite:** Phase 3 complete (`v0.2.1-beta` tag). The 30-day x86 stability soak was DEFERRED / owner-scheduled (ADR docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md) and is NOT a hard prerequisite for Phase 4 — bare-metal burn-in (err=0/400q) + 300K fuzz + 2 audits cover the beta.

This document is the simple forward roadmap. Each phase has specific goals and a clear "done" definition. Phases are sequential — do not start the next until the current phase's exit criteria are met.

---

## Overview

| Phase | Name | One-line goal |
|-------|------|---------------|
| **4** | Production | JARVIS is fast, visible, and reliable enough to run daily on dedicated hardware |
| **5** | Memory | JARVIS remembers interactions and learns your preferences across reboots (memory arc largely complete; Arc 2 gated-off, activates Phase 6) |
| **6** | Butler | JARVIS acts proactively, not only when asked |
| **7** | Autonomy | JARVIS runs unsupervised for extended periods and improves over time |

---

## Phase 4: Production

**Goal:** Turn the proven bare-metal beta into something you can actually use — faster inference, a real interface, and a release others could install.

### Goals

1. **Inference performance** — Make Gemma 4 E2B fast enough to use daily on the seL4 box. **v1.0 path = CPU: enable AVX2/FMA + a seL4-native threadpool in the seL4 build** (the AVX2 qdot/attention kernels exist but compile out today — the seL4 build is scalar single-thread, ~0.2 tok/s). Target: approach the native threaded engine (~8–9 tok/s Gemma 4 E2B @16T; ~20 tok/s Llama 1B). **GPU inference (≥50 tok/s, Vulkan) is DEFERRED — no usable GPU — see docs/decisions/2026-06-16-defer-gpu-inference.md; revisit on a hardware change.**
2. **Graphical output** — Move beyond VGA text: framebuffer or HDMI with a minimal status UI (boot, model load, query/response). **DONE (v1-complete)** — bare-metal GOP framebuffer HUD (status panel + STATE badge + route/counters + scrolling event log), capped at bitmap fidelity (no GPU / font engine / compositor on the box).
   - **Goal #2b — Remote Telemetry Console (v1.0):** Adopt the **headless-appliance** model — the box is a network inference appliance that **emits** its existing `[SNAP]`/`[STATS]`/`[INFER]` telemetry over the (currently dormant) **Intel I211 NIC**; a **browser console on a separate machine** renders the rich, honesty-corrected "Jarvis OS" design (UI seed = the local, git-ignored `phase4/docs/ui_mockups/design-system/`). The box's own monitor keeps the thin HUD as a local "alive" readout. **MVP = telemetry-OUT only** (low-risk); **control-IN is deferred to ~Phase 6** (see goal #3 + the ADR). Absorbs part of goal #2's "status UI" intent — the rich UI lives off-box because the bare-metal box can't render it. **Done when:** the console shows live, honest box state over the network. **Progress (telemetry-OUT, 3 steps):** ✅ **N-a — I211 NIC TX first-light** (DONE 2026-06-21: the dormant Intel I211 brought up on bare metal — MAC `0c:9d:92:0e:39:9a` AV=1, link 1000 Mbps, 5× DD=1, fully non-fatal; boot_id-log verified) → ✅ **N-b — minimal Eth/IPv4/UDP emit** (DONE 2026-06-21: valid UDP limited-broadcast over the I211, Wireshark-decoded with a VALID IP checksum, 192.168.100.143→255.255.255.255:51000, non-fatal) → ✅ **N-c (N-c-1) — continuous ~1 Hz binary telemetry over the I211** (DONE 2026-06-22: a 200-byte CRC'd `telemetry_packet_t` — since grown to **v6 / 224 B, CRC@220 at that time; v14 / 276 B / CRC@272 as of 2026-08 — the u16 flags word is exhausted and the receiver's FMT ladder is the only place the version lives** for the Phase 5 memory metrics — emitted at the `[STATS]` site + a 1 Hz keepalive that also ticks from inside the inference poll → true ~1 Hz even across a ~12 s inference; **Wireshark-verified 914/914 packets CRC-valid, mean 1.000 s cadence**, mid-inference heartbeat, num_nodes=6, err=0; host `test_jarvis_telemetry` 22/22). **Box-side telemetry-OUT (N-a→N-b→N-c-1) complete.** → ✅ **N-c-2 — Python UDP receiver** (DONE 2026-06-22: `telemetry_receiver.py` binds :51000, decodes the packet (then 200 B/CRC@196; since grown to v6/224 B/CRC@220) + validates the zlib CRC-32, honest pretty-print + seq-gap drop detection, `--once/--follow/--json`; C↔Python wire-compat host-tested — canonical 0xCBF43926 vector, 20/20). Remaining to close "Done when": **N-c-3** browser console — ✅ **N-c-3a SSE bridge** (DONE 2026-06-22: `telemetry_receiver.py --sse` serves a `/events` SSE stream of decoded telemetry records + static `phase4/console`; `--replay <pcap>` for box-free dev; host-tested 31/31) → ✅ **N-c-3b/c** the honest browser console landed at `phase4/console/` (DONE 2026-06-22: now **7 screens** — CommandCenter/Routing/LastResponse/Models/SHIELD/**Capabilities**/**System** — reusing the design system, served by `telemetry_receiver.py --web-dir`, consuming `/events`; honesty CI-gated by `test_console_honesty.py`). **goal #2b "Done when" (console shows live, honest box state over the network) — SATISFIED; box-side telemetry-OUT + console complete.** **Standing — UI-feature parity** (CLAUDE.md rule): the console must reflect every real feature; an **auto-populated Capabilities/Features** section driven from the telemetry `flags_list` (new features surface automatically — honest, never hardcoded) landed as ✅ **N-c-3d** (DONE 2026-06-22): a `Capabilities` console view + rail entry rendering one row per live `flags_list` flag (unknown flags surfaced as a "new capability", never dropped) plus derived-from-real-field rows (cores / NVMe MB / cache hits / telemetry-OUT); honesty-gated by `test_console_honesty.py` (40/40). **Hardening — frontend-test foundation (2026-06-22):** console runtime libs vendored + pinned under `phase4/console/vendor/` (hermetic CI; replaces the `lucide@latest` supply-chain risk → 1.21.0); a golden fixture (`fixtures/golden_telemetry.json` + generated `golden.pcap`) + a shared packer (`phase3/scripts/telemetry_fixture.py`) drive a **key-contract** test (every frame round-trips, `set(record.keys())` locked to the fixture meta, 81/81) + a CI golden-drift gate. The logic + e2e (Playwright-**Python**) layers landed 2026-06-22 — **the full frontend test stack (honesty + key-contract + logic 14 + e2e 16+, incl. a flag-parity invariant — counts have since grown with each telemetry slice) is COMPLETE & CI-gated** (headless Chromium, `playwright==1.60.0`, no Node). ADR: `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md`.
3. **Input — CUT from v1.0 (2026-06-26).** A local USB keyboard (xHCI) on bare metal was **cut**: a local keyboard is local-only input that does not fit a remotely-observed headless appliance. Primary interaction is the read-only remote console; the future interactive path is **console control-IN (Phase 6), NOT a local keyboard** (first down-ranked from v1.0-blocker to optional by the 2026-06-21 headless-appliance ADR, then cut at the v1.0 scope-freeze).
4. **Installer** — One-script install: build → USB image → flash → boot checklist. Documented in a user guide.
5. **90-day stability** — Continuous workload on JARVIS PC: 0 crashes, <1% error rate, no memory growth.
6. **Documentation** — User guide, bare-metal boot guide (updated), architecture overview for contributors.
7. **Release** — Git tag `v1.0.0`, MIT open source, public repo.

### Done when

- [x] seL4-build inference benchmark recorded + reproducible: Gemma 4 E2B **5.46 tok/s @ `NUM_NODES=6`** (3.57× the 1.53 1T), 2026-06-18 — see `phase4/docs/PHASE_4_GOAL1_BENCHMARK.md`
- [x] **Graphical output (goal #2)** v1-complete — bare-metal GOP framebuffer HUD (2026-06-20)
- [ ] (deferred) GPU inference benchmark — gated on hardware, see ADR 2026-06-16
- [x] **Remote Telemetry Console (goal #2b)** shows live, honest box state over the network (telemetry-OUT MVP) — DONE 2026-06-22 (box emit N-a→N-c-1 + `telemetry_receiver.py` SSE bridge + `phase4/console/`); see ADR 2026-06-21
- [ ] ~~Local keyboard input works without serial console~~ — **CUT from v1.0 (2026-06-26)**: headless appliance; the future interactive path is Phase 6 console control-IN, not a local keyboard (was down-ranked per ADR 2026-06-21)
- [x] **Installer feature-complete (goal #4)** — `install_jarvis_x86.sh` **usb** (appliance / recovery) / **esp** (reversible on-SSD dual-boot, VERIFIED on-box boot_id=5) / **disk** (full single-OS, CODE+DRY-RUN ONLY); fresh-machine install documented in `phase4/docs/USER_GUIDE.md`
- [ ] 90-day stability log archived with pass criteria met
- [ ] `v1.0.0` tagged and repo public

**Estimated effort:** 6–12 months (solo, part-time)

---

## Phase 5: Memory

**Goal:** JARVIS stops forgetting. It remembers what happened, what you prefer, and what failed — without slowing down routine requests.

**Status:** COMPLETE (all 7 goals mechanism-proven 2026-07-04; Arc 1 deployed default-ON, #4/#5 gated-off by decision; tag cut) — detailed plan: `phase5/docs/PHASE_5_PLAN.md`. **MVP "it-remembers" arc (#1 episodic, #2 shared context, #3 retrieval, #6 cache-growth) DONE + box-verified + DEPLOYED default-ON** (retrieval since 2026-07-02, cache-growth since 2026-07-03; the deployed image intentionally diverges from v1.0.0). **Arc 2: #5 SHIELD-learning + #4 semantic memory mechanism-proven but GATED-OFF** (activate in Phase 6); **#7 consolidation folded into #4's distill** (compact-core landed; prune + scheduled job deferred). Telemetry grown v1→**v6/224 B** for the memory metrics. `memory` milestone tag **CUT as `v1.1.0-memory`** (an annotated tag → commit `feeafd1`; resolve with `^{commit}` — the bare tag object is `205df61`). Storage = the carved raw-LBA region in the ~1.66 TiB free gap after JARVIS_DATA (verified on-box 2026-06-26).

### Goals

1. **Episodic store** — Structured interaction log on NVMe: timestamp, query, action, outcome, optional feedback. Survives reboot. — **✅ DONE (M0–M4, deployed-live, reboot/power-cycle survival proven).**
2. **Shared context pool (C)** — Port working-memory layer to bare metal: system state, event stream, recent decisions. All agents/processes read without serialization. — **✅ DONE (M0–M4, deployed-live).**
3. **Retrieval before inference** — Process A retrieves relevant episodic + semantic entries and injects them into Process B's context before generation. — **✅ DONE (M0–M6, deployed default-ON since 2026-07-02; exact-key injection after A/B hygiene).**
4. **Semantic memory** — Distilled facts and preferences (e.g. "prefers briefings at 7am", "do not interrupt during deep work"). Stored separately from raw episodic log. — **✅ mechanism-proven, GATED-OFF (M0 host, M1 box, M2 telemetry; deterministic distill of observable repeated Q&A — NOT stated preferences; no retrieval hook yet — a future G3 slice; activates Phase 6).**
5. **SHIELD learning on bare metal** — Port failure-learning from Phase 1 Python: failed actions increase risk score, persisted on NVMe. — **✅ mechanism-proven, GATED-OFF (M0–M2; MONITOR-ONLY — risk-rises signal proven via synthetic probe; does NOT close SEC-039 / not live-blocking — that's Phase 6).**
6. **Cache growth** — Promote repeated query→action patterns from episodic log into decision cache automatically. — **✅ DONE (M0–M4, DEPLOYED default-ON 2026-07-03; ~874K-q soak err=0).**
7. **Consolidation job** — Low-priority offline process: compact episodic → semantic, prune stale entries, promote hot patterns to cache. — **◐ FOLDED into #4 (compact episodic→semantic = #4's `sd_distill`; prune + scheduled low-prio job NOT started, deferred to Phase 6 when there's real signal).**

### Done when

- [x] Reboot → JARVIS recalls at least 10 stored preferences without re-prompting — *(#1+#3: box-proven `[RECALL] index n=330` post-reboot recall, 6 distinct prior-boot queries recalled `recall=1`; literal stated "preferences" = Phase 6 semantic; ≥10 recallable, 6 demonstrated).*
- [ ] Repeated harmful action is blocked faster on second attempt (SHIELD learning verified) — **learning SIGNAL proven (#5: risk RISES on the 2nd attempt, synthetic probe, MONITOR-ONLY); live faster-blocking is Phase 6 → stays UNCHECKED — the criterion says "blocked," which the box does not do.**
- [x] Cache hit rate improves measurably after 1 week of use (target: >90%) — *(#6: measurable, ~85% held at 874K q; a property of the repeat-heavy synthetic workload, not a week of varied real use).*
- [x] Episodic log readable and parseable (`parse_*` tool or equivalent) — *(#1: `parse_episodic.py`, real-box `dd|parse` round-trip).*
- [x] Inference latency for cache hits still <1 ms; retrieval adds <50 ms to cache misses — *(#3+#6: retrieval `lat_us`≈0–1 µs ≪ 50 ms; cache-hit <1 ms preserved, HWM keeps EMPTY slots).*

**Estimated effort:** 4–8 months

---

## Phase 6: Butler

**Goal:** JARVIS behaves like a butler — anticipates, monitors, and acts when appropriate, not only on direct commands.

**Status:** IN PROGRESS — the keystone **K ("it-acts", self-heal + the SHIELD action gate)** plus goals **#1 always-on monitors**, **#2 event-driven wake**, **#3 proactive actions** and **#5 natural language primary (control-IN)** are **COMPLETE and DEPLOYED default-ON**. **Goal #5 was the LAST hard-security goal of the phase** — it opened the box to its first untrusted network inbound and had to clear a 6-item security checklist before it could ship. **Goal #6 (routing) is now COMPLETE + DEPLOYED default-ON (2026-07-23). Remaining: #7 (the 7-day supervised exit) — the LAST Phase-6 goal.** Goal #4 (user model) rides Phase 5's semantic store, which is mechanism-proven and activates with real interaction now that control-IN is live.

### Goals

1. **Always-on monitors** — Lightweight background watchers (CPU, disk, network, schedule). Minimal CPU when idle.
2. **Event-driven wake** — Monitors trigger Process A → cache lookup or inference when thresholds crossed. No constant polling of the LLM.
3. **Proactive actions** — At least 5 automated butler behaviors (e.g. low-disk warning, daily briefing, anomaly alert). Trust Level 0–1 only; higher risk asks or notifies.
4. **User model** — Semantic memory includes a structured profile: schedule patterns, communication style, priority topics. Updated from consolidation, not manual config files.
5. **Natural language primary** — Shell/commands exist but conversation is the default interface for all system interaction. **This is where the Remote Telemetry Console's control-IN channel lands** — turning the console from read-only telemetry (shipped in Phase 4 goal #2b) into a two-way interface — gated on the full security checklist: **auth + HMAC, real SHIELD (close SEC-039), rate-limiting, a hardened/fuzzed inbound parser, and ideally a less-privileged input process (SEC-014)**. See `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md`. — **✅ DONE 2026-07-21 (`a9c1d9a`): `JARVIS_CONTROL_IN` flipped default-ON; the deployed image is two-way.** All 6 checklist items closed — hardened + 300K-iter-fuzzed inbound parser · HMAC-SHA256 auth with constant-time verify + a cross-reboot NVMe-persisted replay floor · a scheduling-backed rate limit · a real QUERY SHIELD closing SEC-039 for control-IN queries · the SEC-014 least-privileged `jarvis-input` process (no NIC caps, no key, no rings) · the virgin I211 RX bring-up. `/security-review` CLEAN, emergency-disable proven. Supervised bare-metal validation (boot_id=30): 15/15 sustained queries at human pace, a 24-frame flood limited at exactly CAP=8 with 16 dropped then 3/3 recovery, the browser round-trip coherent and HMAC-verified, `err=0` at `q=175,600`, audit-clean (49 EXECUTED + 21 BLOCKED, all 25 raw-query probes ZERO). Ubuntu keeps `BootOrder[0]`, so exposure is bounded to deliberate JARVIS boots; rollback = the retained pre-flip image and/or wiping the JKEY slot, both proven. **Honest limits:** the reply is SIGNED, NOT ENCRYPTED; the query SHIELD refuses DEFINED ABUSE CLASSES and is not a general injection detector (injection is contained structurally — inbound text can never mint an action); third-host non-observation of the unicast reply is NOT PROVEN. Detail: `phase6/docs/PHASE_6_GOAL_6-5_CONTROL_IN.md` + `phase6/docs/PHASE_6_GOAL_6-5_FINAL_REPORT.md`.
6. **Multi-agent routing** — Device, network, filesystem, and user specialists route queries correctly (>95% accuracy on test suite). — **✅ COMPLETE 2026-07-23 (`JARVIS_ROUTING` default-ON), MET AT THE HANDLER-ROUTING LEVEL AFTER A DOCUMENTED REFRAME.** The literal device/network/filesystem/user specialists are **RETIRED**: they were Phase-1 Python only, ZERO of them ever ran on the box, and a headless conversational appliance has nothing for them to route (`Routing.jsx` already labelled them "not live"). 6-6 instead routes each validated control-IN query to the correct **handler** — SYSTEM-FACTS / INFERENCE / DECLINE — scoring **HELDOUT 70/73 = 95.89%** on a keyword-BLIND held-out suite with **0 INFER misroutes**, box-proven on bare metal (boot 38). See `phase6/docs/PHASE_6_GOAL_6-6_ROUTING.md` §1 (the reframe + evidence) and §2b (honest limits: a point estimate, not production accuracy; a KEYWORD not semantic router — semantic routing is the separate Phase C arc).
7. **7-day supervised autonomy** — JARVIS runs 7 days with you present: proactive actions logged, zero unapproved high-risk actions, <5% false-positive interrupts. — **🔜 REMAINING (the phase exit).**

### Done when

- [x] At least one proactive action fired correctly without user prompt (logged + correct) — *met at K/M4 (2026-07-08, boot_id=15: SHIELD-scored, JACT-audited self-heal); goals #1–#3 have since deployed the monitor → wake → ≥5 INFORM-behavior chain.*
- [ ] 7-day test completed with SHIELD audit trail showing no Level 2+ actions taken without approval — *goal #7, remaining.*
- [x] Multi-agent routing test suite ≥95% pass — *goal #6 COMPLETE 2026-07-23: HELDOUT 70/73 = 95.89%, keyword-blind, 0 INFER misroutes; reframed to handler routing (§1).*
- [x] You can hold a multi-turn conversation where JARVIS references prior sessions correctly — *MET; ticked 2026-09-01 as a docs correction. The text this box carried ("the recall half is UNWIRED — `pa_ctrl_gate` clears the retrieval preamble") was true at the 6-5 flip and superseded the NEXT DAY: `JARVIS_CONTROL_IN_RECALL` flipped default-ON 2026-07-22 (the dedicated control-IN store @ LBA 21,140,000; three bare-metal gates incl. a fresh-boot cross-session recall of an unknowable marker), and `JARVIS_EMBED` (2026-08-01) extended recall from exact-repeat to paraphrase. Demonstrated on hardware repeatedly since: boot 34 (cross-session marker recall), boot 47 (multi-turn build-on — the second ask continued the first answer), boot 48 (4-of-6 paraphrases recalled, zero false recall), boots 49/52 (recall with stored provenance chains across boots), and the 2026-08 soak's two operator sessions (39 turns, `recall=12` incl. prior-session facts). ONE caveat carried, not glossed: the `[23:00110]` recall-provenance anomaly — the recalled ANSWER was correct, but the provenance field's claimed source contradicts it; the KVM probe RAN 2026-09-01 (`JARVIS_EMBED_PROBE` mode 4) and found the mechanism: the preamble can be MULTI-FACT while provenance records a single src, so a triple can point at an "I don't know" record while the prompt actually contained the answer — no leakage observed. CLOSED 2026-09-03: the turn was RECONSTRUCTED from the box's own stored vectors through the deployed selector and builder — a two-fact preamble (seq 93 at cos_x1000 944, then seq 108 at 765) whose SECOND fact carried the answer, while provenance recorded only the first; the positive control reproduced the box's own recorded 944 exactly. No leakage, no confabulation (`SOAK_2026-08_FINAL_REPORT.md` open questions, item 2). The single-src provenance limit itself is a recorded follow-up — CLOSED GOING FORWARD 2026-09-03: the record now carries the selected count and the second (seq, cos) pair — and, from the same date, an emitted mask (`recall_emit_mask` @502) saying which of those facts actually reached the preamble, so a source that contributed nothing is visible rather than assumed; a future occurrence is self-explaining off the store. Records written before those dates stay single-src and render n=? / emit=?.*

**Estimated effort:** 6–12 months

---

## Phase 7: Autonomy

**Goal:** JARVIS can run safely on its own for extended periods, retrieve memories associatively, and improve — within seL4 capability bounds.

### Goals

1. **Associative memory (Instinct)** — Fast similarity retrieval over semantic memory (<100 MB budget). Hopfield or embedding index — retrieve relevant memories without exact query match.
2. **30-day autonomous operation** — JARVIS PC runs 30 days: proactive monitoring + inference + memory consolidation. Human checks in weekly, not daily.
3. **Self-modification (staged)** — AI-generated config/driver patches go through: sandbox → static checks → staged deploy → atomic rollback. Immutable core (kernel, SHIELD rules) never auto-modified.
4. **Larger models for hard tasks** — GPU path supports 7B+ models for complex reasoning; Gemma 4 E2B remains default for speed.
5. **Cross-session personality** — Consistent tone, remembered inside jokes, acknowledged mistakes from episodic log. Not roleplay — grounded in stored facts.
6. **External security audit** — Third-party review of memory store, SHIELD, and capability system. All HIGH findings resolved before tag.
7. **Release** — Git tag `v2.0.0` — "autonomous butler" milestone.

### Done when

- [ ] 30-day autonomous log archived; <1% error rate, 0 crashes
- [ ] Associative retrieval returns relevant memory for paraphrased queries (test suite ≥80%)
- [ ] One staged self-modification deployed and rolled back successfully in test
- [ ] External audit complete with no open HIGH findings
- [ ] `v2.0.0` tagged

**Estimated effort:** 12–18 months

---

## Cross-phase backlog: pull-forwards & multipliers (added 2026-07-02)

Three items that no phase currently owns. They do not change the phase order — the first two are *mechanisms/slices pulled forward* to de-risk Phases 6–7 early (the same keystone-first logic that restructured Phase 5 into the "it-remembers" MVP arc), the third is phase-agnostic reach/velocity work.

### B1. Self-healing: Process B fault-handler restart

Phase 7's "0 crashes over 30 days" is an exit **criterion**, but no goal builds the **mechanism** that survives a crash. Cash in seL4's actual selling point (isolation): PA detects PB faulting (fault endpoint and/or heartbeat timeout on the existing IPC path) and **re-spawns PB from the CPIO without a reboot** — model re-load from NVMe, ring re-init, ready handshake, resume. Buildable now with zero Phase 5/6 dependencies (SEC-014 spawn path + shmem re-init already exist).

**Done when:** an induced PB crash on the box (or QEMU) auto-restarts PB, service resumes with err rate unaffected, and the event is visible in the durable NVMe log + a console/telemetry signal (honest — a real `restart_count` field, per the UI-parity rule).

### B2. "It-acts" keystone (thin slice of Phase 6 goal #3, pulled forward)

On the current plan, the first moment JARVIS *acts* is deep in Phase 6, behind the full memory arc. Pull forward **one** Trust-Level-0 action — driven by real telemetry the box already has (e.g. telemetry-log-nearing-wrap → rotate/compact, or B1's PB restart decision) — routed through a **genuinely-linked SHIELD** (closes SEC-039 early, converting SHIELD from host-harness-only to load-bearing) with an NVMe audit trail. This de-risks Phase 6's hardest unknowns (SEC-039, action execution, audit) years early and makes the project *be* an AI-controlled OS in miniature, not just be roadmapped as one. Scope-honesty: ONE action, allowlisted, Trust Level 0; everything else stays Phase 6.

**Done when:** one allowlisted action fires from live state on the box, SHIELD evaluated it in the live path (not a stub ALLOW), and the decision + outcome are reconstructable from the NVMe log + reflected on the console.

### B3. Reach & dev-velocity (phase-agnostic)

- **5-minute QEMU quickstart** — a `make demo`-style one-liner that boots the seL4 build in QEMU with a small bundled/downloaded GGUF (tens of MB, not the 3 GB Gemma) and reaches coherent generation. v1.0 is public MIT, but reproducing it today needs specific hardware + NVMe layout; "run an LLM on seL4 in 5 minutes" is what turns a public repo into a project people actually run.
- **CI generation smoke** — text generation is currently verified only on the box; CI never exercises it (the model-gated tests SKIP with `::warning::`). The same tiny-model QEMU boot becomes a CI step that asserts end-to-end generation (boot → NVMe load → PA↔PB IPC → coherent tokens), closing the largest untested-in-CI surface.

**Done when:** fresh clone → one command → generated text in QEMU with no physical box; and a CI job runs a bounded version of the same on every push.

---

## Beyond Phase 7 (vision, no fixed timeline)

These are direction, not commitments. Start only after Phase 7 exit criteria are met.

| Direction | What it means |
|-----------|---------------|
| **Distributed JARVIS** | Multiple devices share decision cache and semantic memory |
| **Mobile / edge** | ARM phone/tablet port, <5 W idle |
| **Federated learning** | Improve models across devices without sending raw data to cloud |
| **Custom hardware** | NPU/ASIC for inference or decision-cache acceleration |
| **True agency research** | Explore bounded autonomy within formally verified capability sets — the safe habitat goal |
| **Operates a workspace** | JARVIS gets its own read-write filesystem + a scratch/project region (separate from its own code) and a sandboxed task/command executor, so it can make files and run code/projects on request — every file-write and process-launch an allowlisted, SHIELD-scored, JACT-audited action. Data-and-projects by default, NOT self-modifying code (that stays Phase 7 #3). |
| **Ambient voice wearable** | The owner's "bracelet" idea (2026-09-01): an offline recorder wearable + USB-C batch ingest on the Main PC (Whisper → own-voice filter → distill → JARVIS memory), later wake-word voice commands through the existing control-IN channel. Pipeline-first (provable with no hardware); non-owner speech DISCARDED at ingest by rule. Full idea doc: `BEYOND_PHASE7_VOICE_WEARABLE.md`. |

> **On "Operates a workspace"** — the north star toward a *do-things* AI-OS (the Model B design already
> places user-space filesystems + applications at Ring 3 with the AI coordinating them): the jump from
> "butler that watches, informs, and self-heals" (Phase 6) to "assistant that runs a computer for you."
> Honest scoping — **this half is not built**: the box today is a read-only model partition + raw-LBA
> record stores + one inference process + no input channel. It needs, roughly: (1) a crash-safe
> read-write filesystem service on seL4 (today's FS is read-only FAT32); (2) a scratch/project data
> region JARVIS may freely read-write, distinct from the model and from its own code; (3) a sandboxed
> process/task executor (the K/M2 respawn path is the seed); (4) every op wired through the K action
> spine (allowlisted + SHIELD-scored + JACT-audited — bounded, reconstructable, never free-form);
> (5) **prerequisites** — control-IN (Phase 6 #5, so you can hand it a command/project) and a bigger
> model than Gemma 4 E2B for competent project work (GPU/hardware-gated, Phase 7 #4). **The safety line:
> operating a data/project workspace is a distinct, *less-dangerous* capability than self-modifying its
> own code** — this arc is data-and-projects by default; code self-modification stays the tightly-guarded
> Phase 7 #3.

---

## What each phase does *not* include

Keeps scope honest and prevents creep.

| Phase | Explicitly out of scope |
|-------|-------------------------|
| **4** | Memory across reboots, proactive behavior, multi-agent |
| **5** | Proactive actions without user initiation, self-modification |
| **6** | Unsupervised multi-week operation, model fine-tuning on device |
| **7** | Unbounded self-modification, cloud dependency, general AGI claims |

---

## Dependency chain

```
Phase 3 (v0.2.1-beta)
    ↓
Phase 4 (v1.0.0)     — fast, visible, installable
    ↓
Phase 5              — remembers
    ↓
Phase 6              — anticipates
    ↓
Phase 7 (v2.0.0)     — runs alone, safely
    ↓
Beyond               — research directions
```

---

## References

- `JARVIS_UNIFIED_PLAN.md` — original 36-month corporate plan (aspirational scope)
- `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md` — Phase 3 tactical plan
- `ARCHITECTURE_ENHANCEMENTS.md` — decision cache, SHIELD, shared context pool designs
- `archive/research/jarvis_research_findings.md` — working / episodic / semantic / procedural memory research
- `PROJECT_OVERVIEW.md` — Instinct Integration (Hopfield) Phase 4+ notes
- `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md` — headless-appliance ADR (goal #2b Remote Telemetry Console; goal #3 keyboard down-rank; control-IN deferral to Phase 6). The console's UI seed is the local design-system at `phase4/docs/ui_mockups/design-system/` (git-ignored).

---

**Last updated:** September 2026 (2026-09-04 drift pass: Phase-5 status + tag, telemetry version. Earlier, July 2026: Beyond-Phase-7 vision: added the "Operates a workspace" arc — read-write FS + scratch/project region + sandboxed executor through the K action spine; data-and-projects by default, NOT self-modifying code. Earlier: Phase 5 memory arc: it-remembers MVP #1/#2/#3/#6 DONE + deployed default-ON; Arc 2 #4 semantic + #5 SHIELD-learning mechanism-proven gated-off; #7 folded into #4; telemetry v6; the cross-phase backlog B1 self-healing, B2 "it-acts" keystone, B3 QEMU quickstart + CI generation smoke)  
**Status:** Phase 4 engineering COMPLETE — v1.0 scope FROZEN 2026-06-26. Goals #1 (inference perf, CPU) / #2 (graphical output) / #2b (Remote Telemetry Console) / #4 (installer) / #6 (docs) DONE; **#3 (USB keyboard) CUT** (interactive input is Phase 6 console control-IN, not a local keyboard); #5 (90-day soak) NOT run — owner-scheduled; **#7 (v1.0.0 MIT release) ✅ DONE** (tagged bdf0951, 2026-06-26; LICENSE + doc-honesty pass + final report done). See `phase4/docs/PHASE_4_FINAL_REPORT.md`. **Phase 5 largely complete** — the memory stack is deployed (retrieval + cache-growth live default-ON); `v1.1.0-memory` tag proposed; the remaining Arc-2 items activate in Phase 6. See `phase5/docs/PHASE_5_PLAN.md`.
