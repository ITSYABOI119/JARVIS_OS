# Phase 6 — Goal K: "It-Acts" Spine + Live SHIELD (KEYSTONE — closes SEC-039)

**Status:** 🚧 IN PROGRESS — **K/M0 ✅ host/CI (2026-07-04: `action_allowlist` + `shield_action` + `action_audit` + `parse_action_audit.py`, host-tested 4+6+4+46)** · **K/M1 ✅ BOX-VERIFIED (2026-07-04: the SHIELD action gate linked into Process A behind `JARVIS_ACTIONS` (default-0) + the JACT audit store live; the SEC-039 closure MECHANISM proven on the box via the induced-BLOCK probe + the K-e learned-risk loop; deployed image stays action-inert — SEC-039 fully closes at K/M4)**; K/M2 (the PB-restart = first EXECUTED action) next. The flags/LBA/API below are now REAL (M0 landed `cfa2514`); the deploy-default flip stays a K/M4 decision.
**Date:** 2026-07-04
**Prereqs:** `phase6/docs/PHASE_6_PLAN.md` (approved plan — K is §1/§4/§6-K; locked decisions §7; storage §5; risks §8; ceiling §10). Phase 5 stores (episodic ✅ deployed, semantic ✅ gated) + `shield_learn.c/h` ✅ (monitor-only risk map — **this keystone is what makes it LIVE**). The `episodic_store.c/h` raw-LBA template. SEC-039 reality: `shield.c` is NOT linked into the live image — PB's SHIELD_CHECK returns ALLOW, PA runs a 6-word inline keyword check (`SECURITY_AUDIT_2026-04-06.md`, accepted-INFO then, load-bearing now).
**Sources:** `phase4/docs/ROADMAP.md` §Phase 6 (canon, quoted verbatim in §7) + §Backlog B1/B2; `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md`; the April audit (SEC-039, SEC-014) + the project's SHIELD-reality analysis (**divergent SHIELD surfaces**: PA's inline 6-word list, `shield.c`'s own list, PB's always-ALLOW stub with no list); `phase3/src/ai/shield.c/h` + `shield_learn.c/h` + `episodic_store.c/h`; `phase3/src/sel4/main_x86.c` (Process A — the actor) + `inference_server.c` (Process B — spawned from CPIO, the respawn template) + `jarvis_debug.h` (the gate pattern).

> **What K is:** the FIRST time JARVIS acts on its own initiative, through a genuinely-linked SHIELD.
> ONE allowlisted Trust-L0/1 action (the PB-restart), driven by real box state, evaluated by `shield.c`
> **linked into Process A's action path** (closes SEC-039), with a raw-LBA action-audit record and a
> console reflection. It is the spine every later Phase 6 goal routes through — monitors trigger it,
> the ≥5 behaviors ride it, control-IN commands terminate in it.

---

## 1. Mission

Phases 1–5 observe, remember, and answer; nothing ever *acts*, and the live SHIELD is a stub. K builds the **safe action-execution spine**: decision → allowlist → linked SHIELD gate → trust policy → execute-in-PA → durable audit. Scope-honesty (B2 canon): ONE action, allowlisted, Trust Level 0–1; everything else stays in the later Phase 6 goals.

## 2. Scope boundary

**K IS:** the action-decision core (allowlist + trust policy), the linked SHIELD gate (`shield.c` + the ONE immutable blocklist + `shield_learn`'s live risk feed), the raw-LBA action-audit store + parser, the PB-restart action (B1 folded in — fault-detect → respawn → resume), and K's telemetry/console slice (v7).
**K IS NOT** (downstream, K-dependent): monitors/watchers (**goal 6-1**), event-driven wake routing (**6-2**), the ≥5 butler behaviors (**6-3**), the user model / `JARVIS_SEMANTIC` activation (**6-4**), control-IN and everything inbound (**6-5** — K adds NO network surface), routing specialists (**6-6**), the 7-day run (**6-7**).

## 3. Locked decisions (encoding `PHASE_6_PLAN.md` §7 + the strategist review notes)

- **K-a (plan §7a) — SHIELD activation = `shield.c` linked into Process A's action path.** ONE canonical **IMMUTABLE** blocklist (compile-time const; consolidates the divergent SHIELD surfaces — PA's inline 6-word list, `shield.c`'s own list, and PB's always-ALLOW SHIELD_CHECK stub which has no list at all — per the project's SHIELD-reality analysis under SEC-039, not an audit finding); `shield_learn`'s learned `risk_adj` feeds the action score, **monotonic-raise-only** (nothing ever learns a risk DOWN). **SEC-039 is closed only when a LIVE induced-BLOCK is proven on the box — not when the code merely links.**
- **K-b (plan §7c) — actions = a STATIC C allowlist; the LLM SELECTS an id, never synthesizes an action.** This is the structural prompt-injection boundary — **inviolable**: no string from the model, the cache, retrieved memory, or (later) inbound text can mint, parameterize into shell, or mutate an action. Unknown/out-of-list ids are refused before SHIELD is even consulted.
- **K-c (plan §7 trust) — trust levels are the REAL 0..3 enum, REUSED not re-minted:** `trust_level_t { TRUST_AUTO=0, TRUST_NOTIFY=1, TRUST_REQUEST=2, TRUST_REQUIRE=3 }` already exists in `decision_cache.h` (cached patterns already carry one). Per-action trust is a **compile-time constant** in the allowlist, never LLM-assigned. **TRUST_REQUEST is LOG-ONLY (`ACT_PROPOSE_LOG`) until control-IN (goal 6-5) exists** — there is no channel to ask on; TRUST_REQUIRE never auto-executes.
- **K-d (REVIEW NOTE 1, folded) — the PB-restart is SELF-HEALING, not a "butler behavior."** Honest framing: K proves the *spine*; the phase's real thesis — anticipatory, user-facing behaviors — is **goal 6-3's ≥5 behaviors**, and K's done-when is deliberately **"re-proven at 6-3 scale."** Do not count the PB-restart as butler behavior #1 of 5.
- **K-e (REVIEW NOTE 2, folded) — flipping `JARVIS_SHIELD_LEARN` ON at K must be EARNED by the FULL LIVE LOOP on the box,** not by linking `shield.c`: a repeated blocklisted/failed action must show its learned risk RISING and the gate refusing it **harder/sooner on the 2nd attempt** (`[SHIELD-LEARN]`/audit evidence). This is where Phase 5's honestly-deferred criterion — *"Repeated harmful action is blocked faster on second attempt"* — becomes TRUE.
- **K-f (plan §7f/§7g) — gates + honesty:** all K box wiring sits behind a new `JARVIS_ACTIONS` (default **0**) with a box-only `JARVIS_ACTION_PROBE` diagnostic (default 0, the `JARVIS_SHIELD_PROBE` pattern); the default-ON flip is a **deliberate strategist/user decision at K/M4** on box proof (the G3/M6 + #6/M3 precedent). "SHIELD blocks" may be said of the ACTION path **only after** the induced-BLOCK proof; the #5 learning row's monitor-only wording stays for the learning signal itself.

## 4. Architecture — the it-acts spine + the API shapes the milestones implement

```
   trigger (K/M2: PB fault endpoint OR heartbeat-age timeout — internal state only)
        │
        ▼
   decision (cache lookup → else inference; output = an ACTION ID from the
             allowlist — the LLM selects, NEVER composes; unknown id → refuse)
        ▼
   action_lookup(id)  →  const action_def_t*  (NULL = not allowlisted = refuse)
        ▼
   shield_assess(id, ctx)   ← shield.c LINKED into Process A (closes SEC-039)
        │      risk_x100 = base(action_class) + 100×shield_learn_adjustment(key)
        │      verdict BLOCKED iff id ∈ IMMUTABLE_BLOCKLIST
        │                     or risk_x100 >= SHIELD_BLOCK_THRESHOLD_X100
        ▼
   trust_policy(verdict, trust, control_in_available=false /* until 6-5 */)
        ▼
   execute in PA (the actor; PB only generates text)  — or NOTIFY / PROPOSE_LOG / REFUSE
        ▼
   AUDIT: action_audit record (raw-LBA, durable, wrap-order parseable)
          + telemetry/console (restart_count, actions_fired, actions_blocked)
```

**Proposed API shapes** (K/M0 pins the exact fields; names chosen to AVOID the existing collisions — `shield.h` already owns `shield_result_t`/`shield_decision_t`, and `trust_level_t` is reused from `decision_cache.h`):

```c
/* action_allowlist.h — static, compile-time, human-reviewed */
typedef struct {
    uint16_t       id;            /* stable action id (audit/telemetry currency) */
    const char    *name;          /* for logs/console only — never parsed */
    trust_level_t  trust;         /* compile-time constant (K-c) */
    uint16_t       action_class;  /* base-risk class for shield_assess */
} action_def_t;
const action_def_t *action_lookup(uint16_t id);   /* NULL = not allowlisted → refuse */
/* v1 allowlist: ACTION_RESTART_PB (TRUST_NOTIFY) + ACTION_NOTIFY_ANOMALY (TRUST_AUTO, de-risk fallback) */

/* shield_action.h — the linked gate (new module so shield.h's shield_result_t is untouched) */
typedef enum { SHIELD_VERDICT_EXECUTE = 0, SHIELD_VERDICT_BLOCKED = 1 } shield_verdict_t;
typedef struct { uint16_t risk_x100; shield_verdict_t verdict; } shield_action_result_t;
shield_action_result_t shield_assess(uint16_t action_id, const action_ctx_t *ctx);
/*  risk = base(action_class) + shield_learn_adjustment (monotonic-raise-only, K-a);
    BLOCKED iff blocklisted OR risk_x100 >= SHIELD_BLOCK_THRESHOLD_X100. Pure — host-testable. */

typedef enum { ACT_EXECUTE, ACT_NOTIFY, ACT_PROPOSE_LOG, ACT_REFUSE } act_decision_t;
act_decision_t trust_policy(shield_verdict_t v, trust_level_t t, bool control_in_available);
/*  BLOCKED → ACT_REFUSE (always, any trust level)
    TRUST_AUTO    → ACT_EXECUTE
    TRUST_NOTIFY  → ACT_NOTIFY   (execute + notify)
    TRUST_REQUEST → control_in_available ? ask (6-5) : ACT_PROPOSE_LOG (log-only, never executes)
    TRUST_REQUIRE → ACT_REFUSE   (never auto)                                Pure — host-testable. */
```

## 5. Storage — the action-audit store (raw-LBA, the `episodic_store` clone)

- **Region (PROPOSED — not yet in code):** header @ **LBA 21,120,000**, records @ +1..+**4096** → 4097 sectors, ends 21,124,096 (8-sector-aligned; ~5,900 sectors clear of semantic, which ends ≈21,114,096; far inside the reserved ~8 GiB). Magic **"JACT"** (0x4A414354), version 1. Callback-driven, circular, boot_id, XOR header checksum, flush-after-write — the proven clone. **The reserved-region map in `PHASE_5_PLAN.md` §5 / the store headers gets this entry so a future installer/repartition can never overlap it.**
- **Record (512 B, packed; K/M0 pins + `_Static_assert`s):**
  `{ uint32 boot_id; uint32 seq; uint64 t_ms; uint16 action_id; uint16 trust_level; uint16 verdict (EXECUTED / BLOCKED / PROPOSED); uint16 risk_x100; uint16 outcome; uint16 trigger_len; char trigger_snapshot[456]; uint8 pad[28]; }` — the trigger snapshot is a bounded, length-carried text capture of the trigger state (e.g. "PB heartbeat age 12000 ms"), copied by length, never strlen'd.
- **Parser:** `parse_action_audit.py` — wrap-order reader mirroring `parse_episodic.py`; recovery `dd ... skip=21120000 count=4097 | parse_action_audit.py`. Host round-trip tested (the C `--dump` fixture pattern).
- **The audit store is the 7-day criterion's evidence base** — "SHIELD audit trail showing no Level 2+ actions taken without approval" is answered by reading this store.

## 6. Plan — K/M0 → K/M4 (each milestone: contents, tests, done-when)

### K/M0 — host/CI only: the pure decision core *(NO box, NO flags, NO behavior change)*
- `action_allowlist.c/h` (the v1 2-entry list + `action_lookup`), `shield_action.c/h` (`shield_assess` linking `shield.c`'s canonical blocklist + `shield_learn_adjustment`; `trust_policy`), `action_audit.c/h` (the raw-LBA store), `parse_action_audit.py`.
- Host tests (one CI step per new `test_*.c`/`.py`, per repo rule): allowlist lookup + unknown-id refusal; `shield_assess` risk = base + learned (monotonic — a recorded failure RAISES the next score, never lowers; key parity with `shield_learn`/FNV-1a); blocklist → BLOCKED regardless of trust; the `trust_policy` truth table **including TRUST_REQUEST → PROPOSE_LOG when `control_in_available=false`**; audit-store fresh-init / round-trip / XOR-corrupt / wrap / boot_id-bump; parser round-trip.
- **Done when:** all new host suites green in CI; zero deployed-path changes (nothing links into `main_x86.c` yet).

### K/M1 — box: LINK `shield.c` into PA + the induced-BLOCK proof — **SEC-039 closure MECHANISM** ✅ BOX-VERIFIED 2026-07-04
*(Honest scope: M1 proves the closure MECHANISM — the linked gate REFUSES live on the box. All three flags stay default-0, so the DEPLOYED image is not yet protected; **SEC-039 fully closes at the K/M4 `JARVIS_ACTIONS` flip.** Box smoke, S0-snapshot OFF-vs-ON KVM: ON = `[ACTION] audit store ready` + the 3 `[ACTION-PROBE]` lines (benign restart_pb→EXECUTE risk=10 not-executed; poison id=65534→BLOCKED risk=100 audited; probe_high 75 EXECUTE→85 BLOCKED on the 2nd attempt, K-e) + 2 BLOCKED JACT audit records (dd+parse, header checksum OK); OFF = zero `[ACTION*`, `[INFER]` byte-identical (16=16), err=0/0 faults both legs. Tree restored to all-3-flags-0. The PA inline `shield_check()` query-path vestige is deliberately LEFT untouched — SEC-039 closure rides on the ACTION path, its retirement is a later cleanup.)*
- New gates in `jarvis_debug.h`: **`JARVIS_ACTIONS` (default 0)** — the whole spine compiles out when OFF; **`JARVIS_ACTION_PROBE` (default 0, box-only)** — injects a **blocklisted** action id through the full spine and prints `[ACTION-PROBE] id=… risk_x100=… verdict=BLOCKED` with a matching audit record (verdict BLOCKED), proving the gate REFUSES in the live path. **No real action EXECUTES at M1 — the gate must refuse before M2 executes anything.**
- **Flip `JARVIS_SHIELD_LEARN` ON here, EARNED per K-e:** the probe repeats the blocklisted/failing action → `[SHIELD-LEARN]` shows the learned risk rising → the 2nd attempt scores higher / is refused harder — the full live loop demonstrated on the box (Phase 5 criterion 2's live half).
- Box smoke gates: OFF = spine compiled out, `[INFER]`/`[STATS]` byte-identical (the S0-snapshot OFF-vs-ON protocol); ON = induced-BLOCK refused + audited, err=0, no faults.
- **Done when:** the live induced-BLOCK is proven on the box (the SEC-039 closure teeth) + OFF-identity holds. Only after this may any doc/console text say the action path "blocks."

### K/M2 — box: the PB-restart action — the FIRST executed action
- PA detects PB failure via **both** signals: the seL4 fault endpoint (PB faulted) AND a heartbeat-age timeout on the existing IPC path (PB hung) → `shield_assess(ACTION_RESTART_PB)` → TRUST_NOTIFY → **re-spawn PB from the CPIO** (model re-load from NVMe, shmem ring re-init, M3-worker re-create, ready handshake, workload resume) → audit (verdict EXECUTED, outcome OK/FAIL) + `restart_count++`.
- Box smoke: induce a PB fault (probe-gated), confirm PA detects → restarts → audits → **coherent inference resumes**, err rate unaffected, `restart_count=1` and the event reconstructable from the durable log.
- The respawn mechanics (fault-endpoint wiring, re-spawn idempotency, worker TCB teardown/rebuild) are the hard engineering — if the design grows, author the optional companion **`PHASE_6_GOAL_K_SYSTEM_DESIGN.md`** (the G6 SYSTEM_DESIGN precedent) rather than bloating this doc.
- **Done when:** an induced PB crash on the box (or KVM) auto-restarts PB with service resumed and the event durable + visible (B1's canon done-when, verbatim intent).

### K/M3 — telemetry v7 + console (UI-feature parity, the deliberate fixture-synced slice)
- Telemetry v6→**v7 (PROPOSED pin):** append `uint16 restart_count; uint32 actions_fired; uint16 actions_blocked;` → **232 B, CRC@228, version 7**, +`TLM_F_ACTIONS` **0x800**; bump version + CRC offset in lockstep across `jarvis_telemetry.h` / `telemetry_receiver.py` / `telemetry_fixture.py` / `golden_telemetry.json` + regenerated `golden.pcap` / the console (the v2…v6 precedent, one slice).
- Console: an Actions surface (or Capabilities/System rows) rendering the three fields — **`actions_blocked` is the FIRST honest live "blocked" count, valid ONLY post-K/M1's induced-BLOCK proof.** `test_console_honesty.py`: "SHIELD blocks" becomes PERMITTED for the action path (gated on the proof) while the #5 learning row's monitor-only wording stays banned from block-claims; e2e value-pins rendered == live for all three fields.
- **Done when:** host layers green (C/receiver/honesty/logic/e2e), golden-drift gate passes, box smoke confirms the fill.

### K/M4 — box-verified LIVE + the flip decision
- ESP-deploy a `JARVIS_ACTIONS=1` build (deliberate, checksum-pinned — the #6/M3d pattern); live I211 v7 telemetry: `restart_count` climbs on an induced fault, `actions_fired`/`actions_blocked` live and CRC-ok, err=0, coherent, induced-BLOCK re-proven live; console rows render.
- **The `JARVIS_ACTIONS` default-ON flip is a DELIBERATE strategist/user decision gated on this box proof** (the G3/M6 + #6/M3 precedent) — this doc PROPOSES the flip, it does not assume it. Docs + week status close K.
- **Done when:** the canon done-when below is met and the flip decision is made (either way, recorded).

## 7. Exit criteria → ROADMAP done-when (canon, verbatim)

- "**At least one proactive action fired correctly without user prompt (logged + correct)**" → **K/M4** (the PB-restart, logged in the audit store + durable log, correct = service resumed) — and per K-d, deliberately **re-proven at goal 6-3 scale** with real butler behaviors.
- The other three canon done-whens are OUT of K's scope: "*7-day test … SHIELD audit trail*" → goal **6-7** (on K's audit store), "*Multi-agent routing ≥95%*" → **6-6**, "*multi-turn conversation … prior sessions*" → **6-5**.

## 8. Risks (keystone-relevant subset of `PHASE_6_PLAN.md` §8)

- **SEC-039 closure correctness** — a linked-but-toothless SHIELD (never actually refuses) is *fictional safety, worse than the honest stub*. Teeth: the K/M1 induced-BLOCK box probe + the M0 truth-table CI + the audit trail; no "blocks" claim before the proof.
- **Action blast radius** — allowlist-only; the PB-restart's worst case = what a power-cycle already does (PB holds no durable state; the stores are PA-side); a failed respawn leaves the box no worse than a PB hang and is itself audited (outcome FAIL).
- **Trust-level misclassification** — compile-time constants, human-reviewed at PR; the LLM never assigns trust; TRUST_REQUEST/REQUIRE cannot execute pre-control-IN by construction.
- **Prompt-injection → action** — K adds no inbound surface, but retrieved memory already reaches prompts (G3): K-b's select-don't-synthesize boundary + the blocklist + trust policy is the defense-in-depth; the G3 P6/P7 hygiene lessons apply to any prompt that selects actions.
- **Respawn-path regressions** — the PB spawn path (SEC-014 machinery) was built for boot-time; re-entry (double-spawn, stale caps, worker TCBs, ring state) is the M2 engineering risk — the SYSTEM_DESIGN companion triggers if this grows.

## 9. Scope note — strategist review notes 3–5 belong to LATER goal docs (recorded so they aren't lost)

- **Note 3** — "schedule" monitoring (and goal #4's "schedule patterns") needs wall-clock time the box cannot currently read (no RTC driver; a small CMOS/RTC read is net-new) → the **6-1 monitors** goal doc.
- **Note 4** — control-IN is realistically **5+ sub-projects** (auth/HMAC + key provisioning, I211 RX bring-up, hardened/fuzzed parser, SEC-014 less-privileged input process, rate-limit/replay) → the **6-5** goal doc; the checklist stays a HARD gate.
- **Note 5** — the user model needs a **net-new semantic-store READ path** (Phase 5 #4 shipped write-only; nothing reads `semantic_store` today — G3 reads episodic only) → the **6-4** goal doc.

## 10. Honest ceiling (carried from `PHASE_6_PLAN.md` §10)

> K makes JARVIS a **bounded, supervised, audited actor — not an autonomous agent**. It executes only
> from a static, human-reviewed allowlist at Trust Levels 0–1; every decision is scored by a
> genuinely-linked SHIELD and every action is durably audited. The first action is self-healing
> plumbing, not butlering (K-d) — the anticipatory behaviors are goal 6-3's to prove. SHIELD is
> described as **load-bearing only after the K/M1 induced-BLOCK is proven live on the box**; until
> then SEC-039 remains honestly open. Nothing here self-modifies, nothing runs unsupervised, and
> no claim exceeds what the audit store can reconstruct. That honesty is the feature.

---

*Mirrors `phase5/docs/PHASE_5_GOAL1_EPISODIC_STORE.md` (the keystone-doc pattern); the plan it decomposes is `phase6/docs/PHASE_6_PLAN.md`. Optional companion at K/M2 if warranted: `PHASE_6_GOAL_K_SYSTEM_DESIGN.md` (the `PHASE_5_GOAL6_SYSTEM_DESIGN.md` precedent).*
