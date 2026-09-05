# Phase 6: Butler — Plan

**CLOSED 2026-09-05 — see `PHASE_6_FINAL_REPORT.md` and ADR `docs/decisions/2026-09-05-close-phase-6-defer-supervised-exit.md`; the text below is the plan as authored.** **Status:** IN PLANNING (authored 2026-07-04) — a PROPOSAL for strategist/user review; no Phase 6 implementation starts from this doc until it is approved. Phase 5 (Memory) is SUBSTANTIALLY COMPLETE (`phase5/docs/PHASE_5_PLAN.md` — Arc 1 deployed default-ON; #4 semantic + #5 SHIELD-learning mechanism-proven but GATED-OFF, **waiting on exactly the real interaction Phase 6 creates**).
**Prerequisite:** Phase 5 memory arc (deployed: episodic + shared context + retrieval + cache-growth; gated: semantic distill + SHIELD-learning). The proposed `v1.1.0-memory` tag is the clean baseline to cut before Phase 6 code lands.
**Estimated effort:** 6–12 months (canon).
**Sources:** `phase4/docs/ROADMAP.md` §Phase 6 (goals + done-when, canon — quoted verbatim below) + §Cross-phase backlog B1/B2; `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md` (the control-IN security checklist — load-bearing here); `phase3/docs/SECURITY_AUDIT_2026-04-06.md` (SEC-039 live-SHIELD stub, SEC-014 process isolation) + the project's SHIELD-reality analysis (divergent SHIELD surfaces: PA's inline 6-word list, `shield.c`'s own list, PB's always-ALLOW stub with no list); `phase5/docs/PHASE_5_GOAL4_SEMANTIC_MEMORY.md` §8 + `PHASE_5_GOAL5_SHIELD_LEARNING.md` (the gated capabilities this phase activates). **Where an ADR/audit/box fact conflicts with aspiration, the ADR/audit/box fact wins.**

> **Mission (canon, `ROADMAP.md`):** JARVIS behaves like a butler — anticipates, monitors, and acts
> when appropriate, not only on direct commands.

---

## 1. Strategy — keystone-first, security as the through-line

Phases 1–5 were **read-only / monitor-only / gated**: the box observes, remembers, and answers, but has never *acted*, and its live SHIELD is a stub (SEC-039 — Process B returns ALLOW; Process A runs a 6-word inline keyword check; `shield.c` is not linked). **Phase 6 is the first time JARVIS does something on its own initiative — so the safe-execution spine is the keystone, and security moves from "accepted risk" to "must close."**

**KEYSTONE = "it-acts" (backlog B2, a thin slice of goal #3): the first live action through a genuinely-linked SHIELD.** Exactly as the episodic store was Phase 5's keystone (everything read from it), the action spine is Phase 6's: every proactive behavior, every control-IN command, and the 7-day exit criterion route through it. It is: ONE allowlisted, Trust-Level-0/1 action, driven by real observable box state, evaluated by **`shield.c` linked into the live path (closes SEC-039)**, with an **NVMe audit record** reconstructable from the durable log and reflected on the console.

**Recommended first action = fold backlog B1 (self-healing PB restart) into the keystone.** B2's canon example ("telemetry-log-nearing-wrap → rotate") is stale — the log has been circular/never-fills since 2026-06-24. The PB-restart is the honest best first action: PA detects PB faulting (fault endpoint and/or heartbeat timeout on the existing IPC path) → SHIELD assesses → re-spawn PB from the CPIO (model re-load from NVMe, ring re-init, ready handshake) → audit + `restart_count` telemetry. It merges two backlog items, builds the exact mechanism Phase 7's "0 crashes over 30 days" needs, and its blast radius is bounded (worst case = what a power-cycle already does; PB holds no durable state). Fallback de-risk action if PB-restart bring-up drags: a notify-only L0 action (anomaly line to console/HUD) — the spine is the deliverable, the first action just proves it.

**The keystone-first arc** (each stage depends on the previous):

1. **K — "it-acts" + live SHIELD** (B2+B1): the safe action-execution spine + audit. Closes SEC-039.
2. **Monitors + event-driven wake** (goals #1+#2): lightweight watchers → threshold events → the spine. Without the spine, monitors can only log; with it, they can act.
3. **Proactive actions** (goal #3): ≥5 butler behaviors = (trigger, allowlisted action) pairs on the spine.
4. **User model** (goal #4): ACTIVATE Phase-5 `JARVIS_SEMANTIC` — real action/outcome + interaction signal makes the distill worth running; consolidation produces a structured profile (typed semantic facts).
5. **Control-IN / natural-language primary** (goal #5): the read-only console goes two-way — **the highest-security pillar, HARD-gated on the full ADR checklist** (auth + HMAC, live SHIELD, rate-limit, hardened/fuzzed inbound parser, SEC-014 less-privileged input process). Multi-turn conversation referencing prior sessions lands here (the memory stack already deployed in Phase 5 provides the recall).
6. **Multi-agent routing** (goal #6): specialists + the ≥95% routing suite — meaningful only once real, varied queries arrive via control-IN (the synthetic loop already routes 100%).
7. **7-day supervised autonomy** (goal #7): the exit — runs on everything above.

**Phase-5 activation dependency (explicit):** #4 semantic memory and #5 SHIELD-learning yield ~nothing today because the deployed workload is a benign synthetic loop (err=0 at 874K queries; #6 cache-growth serves the recurring inference, starving the distill — `PHASE_5_GOAL4_SEMANTIC_MEMORY.md` §8). **Phase 6's real interaction is what gives them signal:** failed real actions feed `shield_learn` (its learned `risk_adj` plugs into the live `shield_assess` — so *"repeated harmful action is blocked faster on the second attempt"*, the Phase-5 criterion whose live half was honestly deferred, becomes TRUE here); real repeated queries/actions feed `sd_distill` → the user model. Both flags flip ON only per the established bar: real signal + transient-smoke box proof first (the G3/M6 and #6/M3 precedent).

---

## 2. Goals (canon, `ROADMAP.md` §Phase 6, verbatim)

1. **Always-on monitors** — Lightweight background watchers (CPU, disk, network, schedule). Minimal CPU when idle.
2. **Event-driven wake** — Monitors trigger Process A → cache lookup or inference when thresholds crossed. No constant polling of the LLM.
3. **Proactive actions** — At least 5 automated butler behaviors (e.g. low-disk warning, daily briefing, anomaly alert). Trust Level 0–1 only; higher risk asks or notifies.
4. **User model** — Semantic memory includes a structured profile: schedule patterns, communication style, priority topics. Updated from consolidation, not manual config files.
5. **Natural language primary** — Shell/commands exist but conversation is the default interface for all system interaction. This is where the Remote Telemetry Console's control-IN channel lands — turning the console from read-only telemetry into a two-way interface — gated on the full security checklist: auth + HMAC, real SHIELD (close SEC-039), rate-limiting, a hardened/fuzzed inbound parser, and ideally a less-privileged input process (SEC-014).
6. **Multi-agent routing** — Device, network, filesystem, and user specialists route queries correctly (>95% accuracy on test suite).
7. **7-day supervised autonomy** — JARVIS runs 7 days with you present: proactive actions logged, zero unapproved high-risk actions, <5% false-positive interrupts.

**Monitor honesty note (goal #1):** the canon's "CPU, disk, network, schedule" is calibrated to what the box can REALLY observe. There is no honest CPU% (PA busy-polls — the System-page lesson); "disk" = the NVMe counters/regions we own (store fullness, wrap counts), not SMART/IOPS; "network" = I211 TX stats (RX doesn't exist yet); "schedule" needs wall-clock time the box does not currently read (no RTC driver — a small CMOS/RTC read is net-new work, flagged in §6). Monitors watch: PB heartbeat age, q_errors delta, store wrap/fullness counters, NIC TX failures, uptime milestones, (post-RTC) time-of-day.

---

## 3. Done when (canon, verbatim) + which goal satisfies each

- [ ] At least one proactive action fired correctly without user prompt (logged + correct) — **keystone K** (earliest possible), re-proven at goal #3 scale.
- [ ] 7-day test completed with SHIELD audit trail showing no Level 2+ actions taken without approval — **goal #7**, on the keystone's audit store + live SHIELD.
- [ ] Multi-agent routing test suite ≥95% pass — **goal #6**.
- [ ] You can hold a multi-turn conversation where JARVIS references prior sessions correctly — **goal #5** (control-IN) + the Phase-5 memory stack already deployed (retrieval provides the prior-session recall; control-IN provides the conversation surface).

---

## 4. Architecture — the it-acts spine (keystone design sketch)

```
   monitors (cheap C checks, PA loop/[STATS] cadence — no LLM per tick)
        │  threshold crossed → event
        ▼
   decision (cache lookup → else inference; the LLM SELECTS from the
             action allowlist — it never composes/synthesizes an action)
        ▼
   ACTION ALLOWLIST (static, C-implemented, compile-time trust level each)
        ▼
   shield_assess()  ←  shield.c LINKED INTO PROCESS A (closes SEC-039)
        │              risk = base(action class) + learned adj (#5 shield_learn)
        │              + the ONE canonical immutable blocklist (consolidates the
        │                divergent SHIELD surfaces; never learned-down)
        ▼
   trust policy: L0 AUTO    → execute + audit
                 L1 NOTIFY  → execute + audit + console/HUD notification
                 L2 REQUEST → propose + wait for approval (NEEDS control-IN;
                              before goal #5 exists: LOG-ONLY, never executes)
                 L3 REQUIRE → never auto (explicit approval only)
        ▼
   execute (in PA — the actor; PB only generates text)
        ▼
   AUDIT: raw-LBA action-audit record (durable, wrap-order parseable)
          + telemetry/console (real fields — e.g. restart_count, actions_fired)
```

- **Trust levels are the REAL enum (0..3: AUTO/NOTIFY/REQUEST/REQUIRE)** — the April audit's 0..5 scale does not exist in code. Per-action trust is a compile-time constant in the allowlist, never assigned by the LLM.
- **SEC-039 closure is on the ACTION path in Process A.** PB's SHIELD_CHECK stub can be wired to real scoring too, but the load-bearing gate is where actions execute. Closure teeth: an induced-BLOCK box probe (the `JARVIS_SHIELD_PROBE` precedent) — a blocklisted/high-risk action must be REFUSED in the live path, not just scored.
- **Why this is safe to build first:** the spine has no inbound network surface (control-IN comes 4 stages later); its first trigger is internal state; its first action is service-restoring.

---

## 5. Storage / state (the Phase-5 raw-LBA memory region has room)

Reserved region: base 21,100,000, ~8 GiB to ≈37,877,215 (`PHASE_5_PLAN.md` §5). Current tenants: episodic @ 21,100,000 (+8193 → ends 21,108,192), semantic @ 21,110,000 (+4097 → ends 21,114,096). Phase 6 adds:

- **Action-audit store** — proposed base **21,120,000**, 4096 × 512 B records + header (4097 sectors → ends ≈21,124,096; 8-sector-aligned; ~5,900 sectors clear of semantic). The proven `episodic_store` clone (callback-driven, circular, boot_id, XOR header checksum, magic e.g. "JACT"). Record: {boot_id, seq, t_ms, action_id, trust_level, risk_x100 (base+learned), verdict (EXECUTED/BLOCKED/PROPOSED), outcome, trigger snapshot}. Host-testable like every prior store.
- **User model — NO new store:** the profile is **typed facts in the existing semantic store** (the `fact_type` field exists for exactly this: extend SEM_FACT_QA with e.g. SEM_FACT_SCHEDULE / SEM_FACT_STYLE / SEM_FACT_PRIORITY), written by consolidation — "updated from consolidation, not manual config" (canon). This also un-defers #7's periodic job with real signal behind it.
- Total new footprint ≈ 2 MiB of the reserved 8 GiB. The region map stays documented + reserved (installer/repartition must not overlap).

---

## 6. Build order (keystone-first; canon goal #s in parens)

| Order | Goal | Depends on | Host/CI-now vs box vs SECURITY-GATED |
|-------|------|------------|---------------------------------------|
| K | **"It-acts" spine + live SHIELD** ⭐ KEYSTONE (B2, thin #3; folds B1 PB-restart) | Phase 5 stores (done) | allowlist/trust policy/`shield_assess` scoring/audit-record pack → **host+CI now**; shield.c link + PB fault-detect/respawn + induced-BLOCK probe → **box (JARVIS PC)**. Flips `JARVIS_SHIELD_LEARN` ON (with box proof). |
| 6-1 | **Monitors** (#1) | K (else log-only) | threshold/watcher logic (pure C) → **host+CI**; wiring + idle-cost check → **box**. RTC/CMOS read for "schedule" = small net-new driver → **box**. |
| 6-2 | **Event-driven wake** (#2) | K, #1 | event→cache/inference routing decisions → **host**; end-to-end → **box**. |
| 6-3 | **Proactive actions ≥5** (#3) | K, #1, #2 | each behavior's decision logic + tests → **host**; firing + audit → **box**. Trust L0–1 only (canon). |
| 6-4 | **User model** (#4) | #3 (real signal) | profile schema + consolidation rules + distill extensions → **host+CI**; `JARVIS_SEMANTIC` flip → **box proof** (also revisit GOAL4 §8's post-#6 filter nuance). |
| 6-5 | **Control-IN / NL primary** (#5) | K…#4 mature | **SECURITY-GATED (hard — §7b):** inbound parser + HMAC/auth + rate-limit + replay protection → **host-fuzzable/CI** (the biggest CI win of the phase); I211 RX bring-up (never run on hardware — TX-only today) + SEC-014 less-privileged input process → **box**; ship only when EVERY checklist item is met. |
| 6-6 | **Multi-agent routing** (#6) | #5 (real queries) | routing suite ≥95% → **host+CI**; live → **box**. |
| 6-7 | **7-day supervised autonomy** (#7) — EXIT | all | **box, owner present** (like the 90-day soak, owner-scheduled). |

---

## 7. Locked technical decisions (candidates — the implementing session settles the details, the DIRECTION is locked)

a. **SHIELD activation = `shield.c` linked into Process A's action path.** One canonical gate + ONE immutable blocklist (consolidating the divergent SHIELD surfaces — PA's inline 6-word list, `shield.c`'s own list, PB's listless always-ALLOW stub; the project's SHIELD-reality analysis under SEC-039, not an audit finding); Trust-Level enforcement per §4; #5's learned `risk_adj` feeds the score (monotonic-raise-only stands — nothing ever learns a risk DOWN). SEC-039 is closed when a live induced-BLOCK is proven on the box, not when the code merely links.
b. **The control-IN security checklist is a HARD gate.** No control-IN ships until **every** item is met: auth + HMAC (with replay protection; key provisioned at install, not over the network), live SHIELD (from (a)), rate-limiting, a hardened+fuzzed inbound parser (fuzz in CI, the `fuzz_harness` precedent), and the SEC-014 less-privileged input process (the PB-spawn path is the template). The ADR's physically-gated USB-keyboard fallback stays the recoverable no-network-attack-surface alternative.
c. **Actions = a static C allowlist; the LLM selects, never synthesizes.** Every action is C-implemented with a compile-time trust level; >L1 asks (post-control-IN) or is log-only (before it). No generic shell, no LLM-composed commands — this is also the prompt-injection defense once inbound text exists (retrieved memory or inbound queries can never mint a new action).
d. **Monitors are lightweight + event-driven, not polling the LLM.** Cheap C checks at the existing loop/[STATS] cadence over REAL observable state (§2 honesty note); the LLM/cache is invoked only on threshold events. "Minimal CPU when idle" is measured against the existing busy-poll baseline, honestly.
e. **The user model lives in the semantic store** as consolidation-updated typed facts (no manual config files, no new store); the Phase-5 #7 consolidation job un-defers here with real signal.
f. **Phase-5 flag flips are earned, not assumed.** `JARVIS_SHIELD_LEARN` flips at keystone K; `JARVIS_SEMANTIC` flips at goal #4 — each gated on real signal + transient-smoke box proof, exactly like G3/M6 and #6/M3. `JARVIS_SHIELD_PROBE`/`JARVIS_G3_PROBE`/`JARVIS_G3_AB` stay box-only diagnostics.
g. **Honesty:** proactive actions are Trust-Level-scoped + audited; SHIELD becomes load-bearing for the first time and is described that way ONLY after the induced-BLOCK proof; the console/telemetry grow real fields only (`restart_count`, `actions_fired`, `actions_blocked` — a REAL blocked count finally becomes honest, but only post-(a)); no autonomy overclaiming — this phase is supervised.
h. **Login / auth = an install-provisioned key + HMAC-signed commands, NOT a web login form.** The "login" for control-IN (goal #5) is: a symmetric key provisioned at **INSTALL time** (installer / an NVMe key slot), never sent over the network; **every inbound command is HMAC-signed with a nonce + monotonic sequence** (replay protection), and an unsigned / replayed / stale command is dropped **before** it reaches the action allowlist. A username/password web form is explicitly **REJECTED** — headless appliance, no TLS stack, no keyboard, and a login form is a *larger* attack surface than a pre-shared key. Rotation = reinstall (honest v1 scope, §8). **Scope note:** the READ-ONLY telemetry console (Phase 4) needs NO auth and gets none — the telemetry is a LAN broadcast, so a view-login would be cosmetic (it gates the rendered view, not the sniffable data); auth is load-bearing **only** for the control-IN (inbound) channel. This is one item of the (b) HARD gate, not a separable feature — control-IN does not ship until it (and every other checklist item) is met.

---

## 8. Risks & landmines (security-heavy — this is the phase's character)

- **SEC-039 closure correctness.** A linked-but-toothless SHIELD (still effectively ALLOW-everything) is *worse* than the honest stub — it's fictional safety. Mitigation: induced-BLOCK box probe + host CI on the policy/scoring logic + the audit-trail done-when.
- **SEC-014 inbound isolation.** The input parser must run in a less-privileged seL4 process (own VSpace/CSpace, minimal caps) so a parser exploit cannot reach PA's capabilities. The PB-spawn machinery is the proven template.
- **I211 RX is virgin surface.** Only TX has ever run on hardware; RX bring-up (descriptors, DMA, buffer handling) is simultaneously new driver work AND new attack surface — harden + fuzz the frame path before it faces the network.
- **Auth/HMAC key management.** No keyboard, no TLS stack: keys provision at install time (installer/NVMe partition), never over the network; HMAC with nonce/sequence replay protection; rotate = reinstall (honest v1 scope).
- **Rate-limit / DoS.** UDP inbound floods must not starve the inference loop — budget inbound processing; drop early; the appliance's availability is the cheapest thing to attack.
- **Action blast radius.** Allowlist-only + idempotent-or-bounded actions (the PB-restart's worst case = a reboot's effect); L2+ never auto-executes; the audit store makes every action reconstructable.
- **Trust-level misclassification.** Static compile-time levels reviewed at PR time; never LLM-assigned; the 7-day criterion (zero unapproved L2+) is the field check.
- **Prompt injection → action attempt.** Once inbound text and retrieved memory meet an action system, injected text will try to trigger actions. Defense-in-depth: (c)'s allowlist boundary + SHIELD + trust policy + the G3 P6/P7 injection-hygiene lessons applied to any new prompt surface.
- **False-positive interrupts** (<5% criterion): notification cooldowns/budgets per behavior; monitors tuned on the box before the 7-day run.
- **Scope realism.** 6–12 months (canon). Control-IN (6-5) is the long pole; the keystone delivers visible "it acts" value in the first slice — do not let the checklist slip by shipping control-IN "mostly gated."

---

## 9. Boundary — what is NOT Phase 6 (canon, Phase 7)

- **Unsupervised operation** — Phase 6's exit is 7 days *with you present*; the 30-day autonomous run (weekly check-ins) is **Phase 7 #2**.
- **Associative/embedding retrieval ("Instinct")** → **Phase 7 #1**. Phase 6 conversation uses the deployed exact-key retrieval.
- **Self-modification** (AI-generated patches, staged deploy, atomic rollback; immutable core) → **Phase 7 #3**.
- **Larger models / GPU** → **Phase 7 #4** (hardware-gated, ADR 2026-06-16).
- **Cross-session personality** → **Phase 7 #5**.
- **External security audit + v2.0.0** → **Phase 7 #6/#7** (Phase 6's security work is the prerequisite, not the substitute).
- **A local USB keyboard** stays CUT (headless-appliance ADR) — recoverable only as the physically-gated fallback control path.

---

## 10. Honest ceiling (authored)

> **Phase 6's "butler" is a bounded, supervised, audited actor — not an autonomous agent.** It executes
> only from a static, human-reviewed allowlist, at Trust Levels 0–1, with every decision scored by a
> genuinely-linked SHIELD and every action durably audited and reconstructable from the NVMe log.
> "Anticipates" means threshold watchers + consolidated patterns over real observable state — not
> mind-reading; the user model is distilled observable patterns (Phase 5's honest ceiling carries
> forward — never "knows your preferences" beyond what repetition demonstrates). Conversation is
> retrieval-grounded and multi-turn, not persistent-KV continuity. Higher-risk actions ask or stay
> proposals; nothing self-modifies; the human is present for the exit test. SHIELD becomes load-bearing
> for the first time — and that claim is made only after a live induced BLOCK is proven on the box.
> That honesty is the feature.

---

*Mirrors `phase5/docs/PHASE_5_PLAN.md`. The keystone goal doc (`phase6/docs/PHASE_6_GOAL_K_IT_ACTS.md`, mirroring `PHASE_5_GOAL1_EPISODIC_STORE.md`) is deliberately NOT stubbed here — it gets authored at Phase 6 kickoff, after this plan is reviewed/approved, per the plan-first pattern. Weekly cadence resumes at `phase6/weeks/weekN/WEEK_N_STATUS.md` when implementation starts.*
