# Phase 6 Goal 6-6 — Query Routing (≥95%) — PLAN-FIRST

**Status: PLAN-FIRST, awaiting user sign-off. NO code/flag/wire changes — docs only.** Grounded by a
5-lens research pass + a 4-lens adversarial pre-mortem (2026-07-22).

> Line-number caveat: every `file:line` below was verified once at authoring against HEAD, but line
> numbers drift — **RE-GREP the distinctive string before relying on any citation at implementation
> time.** This is a plan, not a patch.

---

## 1. The canon must be reframed (evidence)

Canon (`ROADMAP.md:93`): *"Device, network, filesystem, and user specialists route queries correctly
(>95% accuracy on test suite)."* That describes a subsystem that was **never built for the deployed
box**:

- The "100% multi-agent routing" metric is **Phase-1 Python only** (`phase1/src/ai/agent_router.py` — a
  priority substring keyword match over 4 objects that shell out to Linux commands; the 8/8 curated
  queries were reverse-engineered from the keyword tables). **ZERO of it is on the box** — `grep phase3/`
  finds no routing code, and `build_jarvis_x86.sh` compiles none.
- The deployed box makes **no domain routing decision**. Per query it decides only two things:
  cache-hit-vs-inference on the synthetic workload lane (PRNG-chosen, `main_x86.c:5294` —
  `cache_lookup(&g_cache, …)`) and allow-vs-refuse on the control-IN query path
  (`query_shield_assess`, `main_x86.c:2703`). Every real query goes to the one Gemma model; there is no
  domain/category field.
- The **literal specialists have no queries to route** on a headless conversational appliance — there is
  no shell, no local devices, no filesystem browsing. `phase4/console/Routing.jsx` already labels them
  honestly: *"a static roadmap diagram — not live in the deployed build"* (Routing.jsx:3, :69).

**Decision: retire the literal-specialist done-when** (implementing it would mean shipping a router with
nothing to route). This is a reframe to what the box **can honestly route**, not a moving of the
goalposts — see §6a.

---

## 2. Reframed goal + done-when

**6-6 reframed:** the box routes each **VALIDATED** (post-HMAC / post-replay / post-SHIELD-ALLOW)
control-IN query to the correct **HANDLER** — SYSTEM-FACTS (answer from real box state) / INFERENCE
(Gemma) / DECLINE (a status-shaped query with no source) — at **≥95%** on a **keyword-BLIND held-out
suite**, with the reply flowing through the existing signed + audited control-IN exit.

**Done-when:**

- [ ] A held-out, **keyword-BLIND** labeled suite scores **≥95% correct-handler accuracy** (host, CI).
- [ ] SYSTEM-FACTS answers **ONLY** from a whitelisted set of human-meaningful box-state fields; a
      no-source status query is an explicit **DECLINE** ("I don't track that"), **NEVER** a fabricated
      inference.
- [ ] The SYSTEM-FACTS reply uses the **SAME** signed + audited exit as INFERENCE (`ctrl_send_reply`
      verdict 0 + `ctrl_in_jact` EXECUTED + `g_ctrl_in_answered++`) — no parallel path.
- [ ] Live on the box (control-IN path), gated `JARVIS_ROUTING` default-0, then flipped default-ON with
      box proof; telemetry + a control-IN-scoped console pane surface it.
- [ ] Honest metric: **≥95% measures a hand-built held-out suite, NOT production traffic.** The dedicated
      control-IN store @ LBA 21,140,000 now accumulates a real corpus to sample/validate against over
      time.

---

## 3. Handler classes (what `route_classify` actually emits)

`route_classify` runs **ONLY on QS_ALLOW queries** (`query_shield`/REFUSE is upstream — NOT a router
class) and emits exactly three:

| Class | Handler | Notes |
|---|---|---|
| SYSTEM-FACTS | Answer from a **WHITELISTED** set of real box-state fields | the net-new capability |
| INFERENCE | Dispatch to Gemma (Process B) | today's control-IN behavior, unchanged |
| DECLINE | Status-shaped query with no valid source → canned *"I don't track that"* | anti-fabrication |

**REFUSE is `query_shield`'s exclusive upstream verdict**, scored on ITS OWN existing suite — excluded
from the routing-accuracy number. **CACHE-on-control-IN is DEFERRED:** `decision_cache` is populated only
by the disjoint synthetic workload queries (cache-growth promotes only `EPI_ACT_INFER` records,
`main_x86.c:6287`), control-IN answers are tag-3 (`EPI_ACT_CONTROL_IN`) and never cached, and
`pa_ctrl_gate` never calls `cache_lookup` (verified: 0 calls). So a control-IN cache hit is
near-impossible today. Making control-IN answers cache-eligible is a documented future slice, not 6-6.

---

## 4. SYSTEM-FACTS field whitelist (the honesty crux)

`sysfacts_answer(query, out)` may read **ONLY** human-meaningful ground-truth fields whose meaning
matches a user's question, enforced by a **host-tested whitelist** (never an honor-system read of
arbitrary state):

- **ELIGIBLE:** uptime (`uptime_ms` — human-meaningful), the loaded model name, `num_nodes`, and a coarse
  health bit (`err==0` → "healthy" vs "errors present") — NOT the raw load-generator count.
- **EXCLUDED (truthful-but-MISLEADING for a human):** `q_total` / `q_hits` / `q_errors` / `q_infer` are
  the PRNG **load-generator's** counters (~175k at the 6-5 flip), NOT the human's conversation — either
  DECLINE, or answer with an explicit "internal workload" label, never "how many times you asked".
- **FORBIDDEN (internal / security state):** the per-gate `g_ctrl_key_ok` / `g_ctrl_floor_ok` /
  `g_ctrl_console_ok` booleans, JACT audit contents, `shield_learn` / monitor internals, `restart_count`.
  `sysfacts_answer` must read from a **fixed struct of allowed `telemetry_packet_t` fields**, asserted in
  a host test.
- **NO SOURCE → DECLINE:** CPU% (PA busy-polls), disk-free, wall-clock / time-of-day (no RTC),
  temperature, "how many people asked you" — a canned honest decline, **NEVER** routed to inference to be
  fabricated.

**Confidentiality:** a SYSTEM-FACTS reply is **SIGNED-not-ENCRYPTED** (it inherits `ctrl_send_reply`) and
carries the same plaintext + "third-host non-observation NOT PROVEN" caveats as every control-IN reply —
**no new confidentiality claim** (these fields are already on the unauthenticated :51000 telemetry
broadcast).

---

## 5. Milestones (this is phase B = 6-6; phase C is a SEPARATE next arc, see §8)

- **B/M0 (host + CI):** `route.c/h` — `route_classify(query, len) → ROUTE_{SYSFACTS, INFER, DECLINE}`
  (pure, host-testable; the `query_shield.c` / `km2b_miss.c` precedent) + the field-whitelist logic + the
  **KEYWORD-BLIND held-out suite** (`routing_suite.h`) + a ≥95% harness + a fuzz harness. The suite is
  authored **WITHOUT** reference to `route_classify`'s keywords (second-party-labeled where possible;
  paraphrases, typos, no-source status questions; a held-out split whose keywords the classifier is known
  to miss). **This is the biggest CI win of the phase.** NO box wiring at M0 (host-only).
- **B/M1 (box, gated `JARVIS_ROUTING` default-0):** wire `route_classify` into `pa_ctrl_gate` after
  QS_ALLOW. SYSTEM-FACTS → `sysfacts_answer` → the **SAME** exit as INFERENCE (`ctrl_send_reply` verdict 0
  + `ctrl_in_jact` EXECUTED + `g_ctrl_in_answered++` + the `EPI_ACT_CONTROL_IN` episodic write); DECLINE →
  same exit with the canned text; INFERENCE → the existing dispatch. `#error JARVIS_ROUTING requires
  JARVIS_CONTROL_IN && JARVIS_ACTIONS`. Serial `[ROUTE] class=/src=`. **OFF-object-identity**
  (`main.c.obj` byte-identical at ROUTING=0) — the `JARVIS_CONTROL_IN_RECALL` precedent.
- **B/M2 (box):** telemetry **v12** — routing counts (sysfacts / infer / decline) as **ADDED fields
  riding the EXISTING `TLM_F_CONTROL_IN` flag** (the u16 flags field is EXHAUSTED — `0x8000` is the last
  bit, verified `jarvis_telemetry.h:41`) + a ROUTING-inited indicator so the honest-0 fill is honest.
  Follow the v11 pattern EXACTLY: the struct grows for everyone, the FILL is gated by `JARVIS_ROUTING` →
  ROUTING=0 emits honest-0 + no live routing rows; **NO OFF-object-identity claim at M2** (the `sizeof`
  change shifts fill offsets — the v11 precedent). Console: a **SEPARATE** control-IN-scoped routing panel
  (do NOT merge with or retire the synthetic-workload aggregate on `Routing.jsx` — two different query
  populations); keep the aggregate caveat until the field exists. Grow `test_console_honesty.py` with
  real-source-only assertions for the new rows. Record the held-out ≥95% number.
- **B flip:** `JARVIS_ROUTING` default-ON with box proof (the established bar) — supervised, real
  control-IN queries, the ≥95% suite green, err=0.

---

## 6. Locked decisions

a. Literal device / network / filesystem / user specialists **RETIRED** — documented; the console is
   already honest about them.
b. **REFUSE (`query_shield`) stays the FIRST gate, UPSTREAM** of `route_classify` — the router only ever
   sees QS_ALLOW and never emits REFUSE. No security regression, no double-REFUSE authority.
c. The SYSTEM-FACTS reply uses the **SAME** signed + audited + counted + episodic-written exit as
   INFERENCE — **no parallel `net_build` reply path** (that would reopen the M4b spoofing hole and blind
   the JACT audit).
d. `sysfacts_answer` reads **ONLY** a host-asserted whitelist of human-meaningful fields (§4); no-source →
   DECLINE, never fabricated; load-generator counters excluded or explicitly labeled.
e. The ≥95% suite is **KEYWORD-BLIND / held-out** (train ≠ test), sampled where possible from the
   accumulating dedicated control-IN store @ LBA 21,140,000; report on the keyword-blind split.
f. All box work gated `JARVIS_ROUTING` default-0. OFF-object-identity for M0/M1; M2 = the v11 fill-gated
   pattern (struct grows for everyone, honest-0 zero-fill + no OFF-object-identity claim).
g. Telemetry rides `TLM_F_CONTROL_IN` (flags exhausted) — routing gets **NO** Capabilities auto-row by
   construction; surface it as field-derived rows on a control-IN-scoped Routing panel (the retrieval-row
   precedent) + a ROUTING-inited indicator.
h. 6-6 **hard-depends on control-IN (default-ON)** — the `#error` enforces it.

---

## 7. Risks

- **Misroute to SYSTEM-FACTS → wrong fact.** Mitigation: conservative classification — SYSFACTS only on
  high-confidence status patterns WITH a whitelisted source; ambiguous → INFERENCE; no-source → DECLINE.
- **Theater risk (the meter measures non-routing).** Mitigation: the ≥95% counts only `route_classify`'s
  live decisions (sysfacts / infer / decline); REFUSE and CACHE are excluded; keyword-blind held-out
  suite.
- **Suite circularity.** Mitigation: §6e (independent labels, keyword-blind split, real-corpus sampling).
- **Flag exhaustion.** Accepted: routing rides `TLM_F_CONTROL_IN` as added v12 fields + a ROUTING-inited
  bit.

---

## 8. Phase C — the embedding arc (NEXT, NOT 6-6's ≥95% deliverable)

The user-endorsed small embedding model (train off-box on the RTX 2070, run on the box CPU) is the next
neural primitive. It is scoped as its **OWN arc, NOT folded into 6-6's routing metric**, for two grounded
reasons the pre-mortem found:

1. **ARCHITECTURE** — PA holds no model / tokenizer / compute, so an embedding classifier must run in
   Process B (with Gemma). Classifying every query just to decide "don't use the big model" would pay a
   PA→PB→PA round-trip + a forward pass on **every** query — defeating the SYSTEM-FACTS fast-path's whole
   point.
2. **MEASUREMENT** — judging an embedder's "paraphrase" advantage on B's keyword-derived suite is
   circular.

So the embedder's honest home is **SEMANTIC MATCHING**: semantic recall (closing 6-5's exact-repeat-only
limitation), a semantic cache, and the harder INFER-vs-SYSFACTS paraphrases — measured on an
**INDEPENDENT** held-out set, with a **real contrastively-trained decoder-arch embedder** (raw last-token
pooling on a causal LM is a known-weak embedder — require retrieval-contrastive training, e.g.
LLM2Vec / e5-style, and a llama/qwen-arch model so `qmodel_load`'s tensor-name path loads it) and an
on-box µs/query cost **measured before any fast-path claim**. Everything B builds (the suite, the classes,
the handlers, the `JARVIS_ROUTING` gate) survives as C's foundation. **C is a Phase-6-tail / Phase-7
arc.**

---

*Companion to `phase4/docs/ROADMAP.md` (goal #6) and the 6-5 control-IN docs (this goal hard-depends on
the deployed two-way channel). PLAN-FIRST — authored 2026-07-22 from a 5-lens research pass + a 4-lens
adversarial pre-mortem; no code, flag, or wire change until user sign-off. RE-GREP every `file:line`
before relying on it.*
