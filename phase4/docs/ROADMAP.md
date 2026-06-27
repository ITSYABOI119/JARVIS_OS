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
| **5** | Memory | JARVIS remembers interactions and learns your preferences across reboots |
| **6** | Butler | JARVIS acts proactively, not only when asked |
| **7** | Autonomy | JARVIS runs unsupervised for extended periods and improves over time |

---

## Phase 4: Production

**Goal:** Turn the proven bare-metal beta into something you can actually use — faster inference, a real interface, and a release others could install.

### Goals

1. **Inference performance** — Make Gemma 4 E2B fast enough to use daily on the seL4 box. **v1.0 path = CPU: enable AVX2/FMA + a seL4-native threadpool in the seL4 build** (the AVX2 qdot/attention kernels exist but compile out today — the seL4 build is scalar single-thread, ~0.2 tok/s). Target: approach the native threaded engine (~8–9 tok/s Gemma 4 E2B @16T; ~20 tok/s Llama 1B). **GPU inference (≥50 tok/s, Vulkan) is DEFERRED — no usable GPU — see docs/decisions/2026-06-16-defer-gpu-inference.md; revisit on a hardware change.**
2. **Graphical output** — Move beyond VGA text: framebuffer or HDMI with a minimal status UI (boot, model load, query/response). **DONE (v1-complete)** — bare-metal GOP framebuffer HUD (status panel + STATE badge + route/counters + scrolling event log), capped at bitmap fidelity (no GPU / font engine / compositor on the box).
   - **Goal #2b — Remote Telemetry Console (v1.0):** Adopt the **headless-appliance** model — the box is a network inference appliance that **emits** its existing `[SNAP]`/`[STATS]`/`[INFER]` telemetry over the (currently dormant) **Intel I211 NIC**; a **browser console on a separate machine** renders the rich, honesty-corrected "Jarvis OS" design (UI seed = the local, git-ignored `phase4/docs/ui_mockups/design-system/`). The box's own monitor keeps the thin HUD as a local "alive" readout. **MVP = telemetry-OUT only** (low-risk); **control-IN is deferred to ~Phase 6** (see goal #3 + the ADR). Absorbs part of goal #2's "status UI" intent — the rich UI lives off-box because the bare-metal box can't render it. **Done when:** the console shows live, honest box state over the network. **Progress (telemetry-OUT, 3 steps):** ✅ **N-a — I211 NIC TX first-light** (DONE 2026-06-21: the dormant Intel I211 brought up on bare metal — MAC `0c:9d:92:0e:39:9a` AV=1, link 1000 Mbps, 5× DD=1, fully non-fatal; boot_id-log verified) → ✅ **N-b — minimal Eth/IPv4/UDP emit** (DONE 2026-06-21: valid UDP limited-broadcast over the I211, Wireshark-decoded with a VALID IP checksum, 192.168.100.143→255.255.255.255:51000, non-fatal) → ✅ **N-c (N-c-1) — continuous ~1 Hz binary telemetry over the I211** (DONE 2026-06-22: a 200-byte CRC'd `telemetry_packet_t` emitted at the `[STATS]` site + a 1 Hz keepalive that also ticks from inside the inference poll → true ~1 Hz even across a ~12 s inference; **Wireshark-verified 914/914 packets CRC-valid, mean 1.000 s cadence**, mid-inference heartbeat, num_nodes=6, err=0; host `test_jarvis_telemetry` 22/22). **Box-side telemetry-OUT (N-a→N-b→N-c-1) complete.** → ✅ **N-c-2 — Python UDP receiver** (DONE 2026-06-22: `telemetry_receiver.py` binds :51000, decodes the 200-byte packet + validates the zlib CRC-32 over [:196], honest pretty-print + seq-gap drop detection, `--once/--follow/--json`; C↔Python wire-compat host-tested — canonical 0xCBF43926 vector, 20/20). Remaining to close "Done when": **N-c-3** browser console — ✅ **N-c-3a SSE bridge** (DONE 2026-06-22: `telemetry_receiver.py --sse` serves a `/events` SSE stream of decoded telemetry records + static `phase4/console`; `--replay <pcap>` for box-free dev; host-tested 31/31) → ✅ **N-c-3b/c** the honest browser console landed at `phase4/console/` (DONE 2026-06-22: 5 screens — CommandCenter/Routing/LastResponse/Models/SHIELD — reusing the design system, served by `telemetry_receiver.py --web-dir`, consuming `/events`; honesty CI-gated by `test_console_honesty.py`). **goal #2b "Done when" (console shows live, honest box state over the network) — SATISFIED; box-side telemetry-OUT + console complete.** **Standing — UI-feature parity** (CLAUDE.md rule): the console must reflect every real feature; an **auto-populated Capabilities/Features** section driven from the telemetry `flags_list` (new features surface automatically — honest, never hardcoded) landed as ✅ **N-c-3d** (DONE 2026-06-22): a `Capabilities` console view + rail entry rendering one row per live `flags_list` flag (unknown flags surfaced as a "new capability", never dropped) plus derived-from-real-field rows (cores / NVMe MB / cache hits / telemetry-OUT); honesty-gated by `test_console_honesty.py` (40/40). **Hardening — frontend-test foundation (2026-06-22):** console runtime libs vendored + pinned under `phase4/console/vendor/` (hermetic CI; replaces the `lucide@latest` supply-chain risk → 1.21.0); a golden fixture (`fixtures/golden_telemetry.json` + generated `golden.pcap`) + a shared packer (`phase3/scripts/telemetry_fixture.py`) drive a **key-contract** test (every frame round-trips, `set(record.keys())` locked to the fixture meta, 81/81) + a CI golden-drift gate. The logic + e2e (Playwright-**Python**) layers landed 2026-06-22 — **the full frontend test stack (honesty 40 + key-contract 81 + logic 13 + e2e 11, incl. a flag-parity invariant) is COMPLETE & CI-gated** (headless Chromium, `playwright==1.60.0`, no Node). ADR: `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md`.
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

**Status:** IN PROGRESS (started 2026-06-26) — detailed plan: `phase5/docs/PHASE_5_PLAN.md`; keystone goal: `phase5/docs/PHASE_5_GOAL1_EPISODIC_STORE.md`. Shipping keystone-first as an "it-remembers" MVP arc (#1, #2, #3, #6) then #4, #5, #7. Storage = a carved raw-LBA region in the ~1.66 TiB free gap after JARVIS_DATA (verified on-box 2026-06-26).

### Goals

1. **Episodic store** — Structured interaction log on NVMe: timestamp, query, action, outcome, optional feedback. Survives reboot.
2. **Shared context pool (C)** — Port working-memory layer to bare metal: system state, event stream, recent decisions. All agents/processes read without serialization.
3. **Retrieval before inference** — Process A retrieves relevant episodic + semantic entries and injects them into Process B's context before generation.
4. **Semantic memory** — Distilled facts and preferences (e.g. "prefers briefings at 7am", "do not interrupt during deep work"). Stored separately from raw episodic log.
5. **SHIELD learning on bare metal** — Port failure-learning from Phase 1 Python: failed actions increase risk score, persisted on NVMe.
6. **Cache growth** — Promote repeated query→action patterns from episodic log into decision cache automatically.
7. **Consolidation job** — Low-priority offline process: compact episodic → semantic, prune stale entries, promote hot patterns to cache.

### Done when

- [ ] Reboot → JARVIS recalls at least 10 stored preferences without re-prompting
- [ ] Repeated harmful action is blocked faster on second attempt (SHIELD learning verified)
- [ ] Cache hit rate improves measurably after 1 week of use (target: >90%)
- [ ] Episodic log readable and parseable (`parse_*` tool or equivalent)
- [ ] Inference latency for cache hits still <1 ms; retrieval adds <50 ms to cache misses

**Estimated effort:** 4–8 months

---

## Phase 6: Butler

**Goal:** JARVIS behaves like a butler — anticipates, monitors, and acts when appropriate, not only on direct commands.

### Goals

1. **Always-on monitors** — Lightweight background watchers (CPU, disk, network, schedule). Minimal CPU when idle.
2. **Event-driven wake** — Monitors trigger Process A → cache lookup or inference when thresholds crossed. No constant polling of the LLM.
3. **Proactive actions** — At least 5 automated butler behaviors (e.g. low-disk warning, daily briefing, anomaly alert). Trust Level 0–1 only; higher risk asks or notifies.
4. **User model** — Semantic memory includes a structured profile: schedule patterns, communication style, priority topics. Updated from consolidation, not manual config files.
5. **Natural language primary** — Shell/commands exist but conversation is the default interface for all system interaction. **This is where the Remote Telemetry Console's control-IN channel lands** — turning the console from read-only telemetry (shipped in Phase 4 goal #2b) into a two-way interface — gated on the full security checklist: **auth + HMAC, real SHIELD (close SEC-039), rate-limiting, a hardened/fuzzed inbound parser, and ideally a less-privileged input process (SEC-014)**. See `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md`.
6. **Multi-agent routing** — Device, network, filesystem, and user specialists route queries correctly (>95% accuracy on test suite).
7. **7-day supervised autonomy** — JARVIS runs 7 days with you present: proactive actions logged, zero unapproved high-risk actions, <5% false-positive interrupts.

### Done when

- [ ] At least one proactive action fired correctly without user prompt (logged + correct)
- [ ] 7-day test completed with SHIELD audit trail showing no Level 2+ actions taken without approval
- [ ] Multi-agent routing test suite ≥95% pass
- [ ] You can hold a multi-turn conversation where JARVIS references prior sessions correctly

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

## Beyond Phase 7 (vision, no fixed timeline)

These are direction, not commitments. Start only after Phase 7 exit criteria are met.

| Direction | What it means |
|-----------|---------------|
| **Distributed JARVIS** | Multiple devices share decision cache and semantic memory |
| **Mobile / edge** | ARM phone/tablet port, <5 W idle |
| **Federated learning** | Improve models across devices without sending raw data to cloud |
| **Custom hardware** | NPU/ASIC for inference or decision-cache acceleration |
| **True agency research** | Explore bounded autonomy within formally verified capability sets — the safe habitat goal |

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

**Last updated:** June 2026  
**Status:** Phase 4 engineering COMPLETE — v1.0 scope FROZEN 2026-06-26. Goals #1 (inference perf, CPU) / #2 (graphical output) / #2b (Remote Telemetry Console) / #4 (installer) / #6 (docs) DONE; **#3 (USB keyboard) CUT** (interactive input is Phase 6 console control-IN, not a local keyboard); #5 (90-day soak) NOT run — owner-scheduled; **#7 (v1.0.0 MIT release) ✅ DONE** (tagged bdf0951, 2026-06-26; LICENSE + doc-honesty pass + final report done). See `phase4/docs/PHASE_4_FINAL_REPORT.md`.
