# JARVIS AI-OS: Roadmap — Phase 4 and Beyond

**Version:** 1.0  
**Date:** June 2026  
**Prerequisite:** Phase 3 complete (`v0.3.0-beta` tag, 30-day x86 stability test passed)

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

1. **GPU inference** — Run the inference engine on GPU (Vulkan or CUDA path). Target: ≥50 tok/s for Gemma 4 E2B on RTX 2070 class hardware.
2. **Graphical output** — Move beyond VGA text: framebuffer or HDMI with a minimal status UI (boot, model load, query/response).
3. **Input** — USB keyboard (xHCI) working on bare metal for local interaction.
4. **Installer** — One-script install: build → USB image → flash → boot checklist. Documented in a user guide.
5. **90-day stability** — Continuous workload on JARVIS PC: 0 crashes, <1% error rate, no memory growth.
6. **Documentation** — User guide, bare-metal boot guide (updated), architecture overview for contributors.
7. **Release** — Git tag `v1.0.0`, MIT open source, public repo.

### Done when

- [ ] GPU inference benchmark recorded and reproducible
- [ ] Local keyboard input works without serial console
- [ ] Fresh machine can boot JARVIS from USB following the guide alone
- [ ] 90-day stability log archived with pass criteria met
- [ ] `v1.0.0` tagged and repo public

**Estimated effort:** 6–12 months (solo, part-time)

---

## Phase 5: Memory

**Goal:** JARVIS stops forgetting. It remembers what happened, what you prefer, and what failed — without slowing down routine requests.

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
5. **Natural language primary** — Shell/commands exist but conversation is the default interface for all system interaction.
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
Phase 3 (v0.3.0-beta)
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

---

**Last updated:** June 2026  
**Status:** Draft — Phase 4 not started
