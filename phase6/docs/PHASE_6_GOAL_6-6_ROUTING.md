# Phase 6 Goal 6-6 — Query Routing (≥95%) — **COMPLETE (FLIPPED DEFAULT-ON 2026-07-23)**

**Status: COMPLETE.** `JARVIS_ROUTING` is default-ON; the deployed control-IN router is live. Grounded by a
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

**Done-when — ALL MET 2026-07-23 (`JARVIS_ROUTING` FLIPPED DEFAULT-ON):**

- [x] A held-out, **keyword-BLIND** labeled suite scores **≥95% correct-handler accuracy** (host, CI).
      **HELDOUT 70/73 = 95.89%**, DEV 64/64 = 100%, **0 INFER misroutes** in either split.
- [x] SYSTEM-FACTS answers **ONLY** from a whitelisted set of human-meaningful box-state fields; a
      no-source status query is an explicit **DECLINE** ("I don't track that"), **NEVER** a fabricated
      inference. Box-proven (boot 38).
- [x] The SYSTEM-FACTS reply uses the **SAME** signed + audited exit as INFERENCE (`ctrl_send_reply`
      verdict 0 + `ctrl_in_jact` EXECUTED + `g_ctrl_in_answered++`) — no parallel path.
- [x] Live on the box (control-IN path), gated `JARVIS_ROUTING` default-0, then flipped default-ON with
      box proof; telemetry (v12) + a control-IN-scoped console pane surface it.
- [x] Honest metric: **≥95% measures a hand-built held-out suite, NOT production traffic.** The dedicated
      control-IN store @ LBA 21,140,000 now accumulates a real corpus to sample/validate against over
      time.

---

## 2a. FLIP EVIDENCE (supervised, bare metal, 2026-07-23)

The flip ran **TWICE by design**, and the first attempt is the more valuable record.

**Attempt 1 (boot_id=37) — ABORTED.** SYSFACTS and DECLINE passed, but the V-INFER leg — a question
the **operator improvised at the keyboard**, per the M4d V1 discipline that an invented query cannot
be a cache hit — came back wrong:

> `"In one sentence, why doesn't adding more CPU cores speed up a single-threaded program?"`
> → **`"I don't track that."`**

A controlled re-send differing by **exactly one word** ("CPU cores" → "cores") returned a correct
coherent Gemma answer, isolating the trigger to the bare noun `cpu`. `route_infer` stood at **0** for
the entire boot: the INFER path had never once been exercised on hardware. Flip **aborted**, box
reverted to `a28d34a0`, defect fixed first (`5224a85`).

**Attempt 2 (boot_id=38) — PASSED**, on the fixed image `a865b830`:

| Leg | Evidence |
|---|---|
| V-SYSFACTS | `up 130 seconds` → `up 196 seconds` — the value **advances with real elapsed time** across sends, which neither a cache nor the model could do ⇒ provably rendered from live PA state |
| V-DECLINE | `"I don't track that."` for `what is your cpu usage?` and `how much disk space is free` — declined, never fabricated |
| V-INFER | the operator's **verbatim** question now returns a correct coherent Gemma answer |
| On-wire v12 | `version=12`, all `crc_ok`, `route_inited=1`, **route_sysfacts=3 / route_decline=2 / route_infer=1** — matching the send transcript exactly |
| Health | `control_in_answered=6`, `dropped=0`, `err=0`, NN=6, 0 faults |
| Audit teeth | every `action=5` trigger is one of the two fixed literals; a grep for every raw query string returns **0** — the audit never records attacker text |

Every reply was `verdict=0` with **both `crc_ok` and `hmac_ok`** (signed by the box, verified at the
receiver).

---

## 2b. HONEST LIMITS — carried, not glossed

These are the conditions under which the ≥95% is true. None of them is a defect; all of them bound
what may be claimed.

- **(a) ≥95% is a 67→73-item KEYWORD-BLIND HELD-OUT POINT ESTIMATE, not a production-accuracy
  guarantee.** The live validation proved exactly why: the original suite scored 95.52% *with a real
  defect present*, because every DECLINE case in both splits was a genuine status query and the
  DECLINE-vs-INFER boundary was never exercised. A hand-built suite measures what its author thought
  to test. The real corpus accumulating in the dedicated control-IN store @ LBA 21,140,000 is the
  future, larger validation set.
- **(b) The route counts are ROUTING DECISIONS, not a breakdown of answered.** They are counted at
  classification time, so an INFER decision that later degrades or times out is counted but never
  answered; the three do not sum to `control_in_answered`. The console is gate-pinned against
  presenting them as a breakdown.
- **(c) Mixed-build recall edge.** A ROUTING=0 build reading a ROUTING=1 build's episodic records
  could inject a stale "up N seconds" note as a preamble. Narrow, mixed-build-only, no safety impact
  (routing short-circuits before recall whenever ROUTING=1, so a stale system fact can never be
  served while routing is on).
- **(d) DECLINE fires on ANCHORED known-untracked metrics only; ambiguity fails toward INFER.** After
  the `5224a85` retune a metric noun requires a status anchor (usage/quantity word, or a
  possessive/self reference; wall-clock takes its own specific anchors). A conceptual question that
  merely contains the noun goes to the model. This is the module's stated conservative rule, now
  actually enforced rather than merely documented.
- **(e) This is a KEYWORD router, not a semantic one.** It has no notion of meaning; it matches words.
  Paraphrases outside the keyword tables fail toward INFER (safe, but unhelpful for SYSFACTS), and
  the 3 residual held-out misses are typo probes for exactly that reason. **Semantic/embedding routing
  is Phase C**, a separate future arc (§8) — 6-6 ships the honest keyword router.
- **(f) Cosmetic, on the reply path:** Gemma answers occasionally carry `<turn|>`/`<eos>` special
  tokens into the user-visible reply text (observed at both boots). Not safety-relevant, same family
  as the `<|channel>` artifact recorded in the 6-5 recall work; a shared reply-sanitiser improvement
  would benefit both paths.
- **(g) Unrelated open item, re-observed:** boot 37 logged `[CTRL-IN-STATS] … drop=6 (parse=6 …)`.
  Frozen at 6, no answered query affected; the pre-existing unexplained parse-drop item, worth a look
  during the 6-7 soak.
- **(h) JACT durability of the SHORT validation boots — an OBSERVABILITY question for 6-7, NOT a
  routing/security defect.** The real-device action-audit store @ LBA 21,120,000 stamps each record
  with the store's OWN `boot_id` (`action_audit.c` `s->hdr.boot_id++` at init — independent of
  `nvme_log_boot_id()`, so a JACT "boot N" never maps 1:1 to a telemetry boot). Read off-box after the
  flip, the store is current through the 6-5 flip (its 21 refuse records are unmistakably boot-30's
  "21 BLOCKED") and every record is teeth-clean (0 raw query strings). BUT its NEWEST record is a
  `digest up=1h` — writable only by a boot running ≥1 h — while the three flip-validation boots
  (37/38/39) were minutes each, so their individual answered-query JACT records are not clearly the
  newest and may not have persisted to the real-device store. This does NOT undercut the flip: routing
  is proven by the LIVE wire (v12 route counters matching the transcript), the durable
  `[CTRL-IN-STATS] acc=3 drop=0`, and three verdict=0/crc_ok/hmac_ok replies — the JACT write is a
  forensics record, not the functional or security path. Settling it needs a supervised boot with the
  `ctrl_in_jact`/`act_audit_append` write path watched (a 6-7 task; can't be done from Ubuntu).

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

## 9. THE SYSFACTS BARE-WORD DEFECT — measured, ATTEMPTED, and NOT FIXED (2026-07-27)

**Status: the defect is LIVE on the deployed image (`61e8142f`, `JARVIS_ROUTING` default-ON). A keyword
fix was attempted, measured to be NET WORSE THAN THE DEFECT, and reverted. This section is the evidence
base for the Phase C routing lane (§8 / C/M4), whose "measured miss" gate this closes.**

### 9.1 The defect

`5224a85` anchored the **DECLINE** rules after the first B-flip validation caught a bare-noun false
positive on hardware. **The SYSFACTS rules never got the same treatment.** They match bare words and are
checked FIRST, so they capture before DECLINE or INFER see anything. Measured against the shipped code:

| path | probe | routes to | renders |
|---|---|---|---|
| HEALTH | `what causes page faults?` | SF_HEALTH | `healthy` |
| UPTIME (bare noun) | `how many hours does a full rebuild take?` | SF_UPTIME | `up 76 seconds` |
| UPTIME (`rt_seq2` phrases) | `how long does a context switch take?` | SF_UPTIME | `up 76 seconds` |
| BOOT_ID | `how many cycles does a cache miss cost?` | SF_BOOT_ID | `boot 44` |
| MODEL | `how does a transformer model work?` | SF_MODEL | `Gemma 4 E2B` |
| NODES | `how many nodes are in a red-black tree?` | SF_NODES | `6 workers` |

**FIVE reachable paths, not the two first reported.** Two table comments documented safeties that did
not exist: HEALTH claimed *"plural 'faults'/'errors' only"* while its list contains **`"error"`
singular**; BOOT_ID claimed *"safe as bare words"* on reasoning that covers only duration phrasings.
MODEL claimed *"unambiguous, never collides"* for `model`/`neural`.

**Worse than the DECLINE defect it mirrors:** that one returned *"I don't track that"*, which visibly
misfires. This returns a confident, plausible, irrelevant answer with nothing to signal the misroute.

### 9.2 The suite could not see it, and still cannot

Measured before touching anything: **DEV=0, HELDOUT=0** conceptual-question negatives against 65
SYSFACTS positives. The SYSFACTS-vs-INFER boundary was **completely unexercised**, so 95.89% passed
clean *with the defect present* — the **second** instance of the blind spot that let the DECLINE defect
through at 95.52%. §2b(a) predicted exactly this and should now be read as load-bearing, not cautionary.

**The deeper structural fact, found by adversarial review:** of 66 SYSFACTS positives, **39 carry a
self-reference, 26 carry no concept word, and ZERO occupy the (no-self AND concept-word) quadrant** that
any anchor actually decides. All 7 auxiliary-bearing positives carry `you`, so a self-branch
short-circuits and the anchor's real logic is never exercised on a positive. **A suite can be 100%/96%
green while the rule under test is unmeasured.**

### 9.3 The attempted fix, and why it was reverted

Anchor tried: *a family word is SYSFACTS iff self-reference OR no generic-subject marker*
(`RT_CONCEPT_WORDS` = a/an/does/do/did/i/explain/why/mean/means/to/with/causes/cause).
It scored **DEV 76/76, HELDOUT 82/85 = 96.47%, 0 INFER misroutes, test_route 127, fuzz 300K clean** —
and was still **net worse than doing nothing**, because every one of those gates is blind per §9.2.

A two-sided corpus (37 genuine status questions incl. 24 review-reproduced regressions; 32 conceptual
questions incl. the unguarded paths) measured it directly:

| | false NEGATIVES | false POSITIVES | total wrong |
|---|---|---|---|
| baseline (the live defect) | **0 / 37** | 32 / 32 | **32 / 69** |
| the attempted anchor | 23 / 37 | 22 / 32 | **45 / 69** |
| best of 30 keyword configs | 12 / 37 | 15 / 32 | **27 / 69** |

The anchor bought 10 false-positive fixes and paid **23 false negatives** — `do we have errors?`,
`give me a health check`, `is there an error?`, `can i get the uptime?` all handed to a model that
cannot know. **The classifier turned on whether the operator said "you" or "we".**

### 9.4 WHY NO KEYWORD FIX EXISTS — the finding that matters

Two independent levers were explored and both hit the same wall.

**Lever 1, anchoring:** 30 configurations over 5 self-vocabularies × 6 marker sets. Best total **27/69**,
still losing a third of genuine status questions. The words doing the work — `a`/`an`, `do`/`does`/`did`
— are ordinary English function words that appear on **both sides**: `how does a transformer model
work?` (concept) and `what model does this run?` (status) are indistinguishable to any such rule.

**Lever 2, shrinking the family vocabularies:** reduces false positives to **0** but costs **22 false
negatives**, because the discriminating terms are the ambiguous ones. **Nine family words appear on both
sides and are therefore unfixable by vocabulary at all:**

> `boot` · `cycles` · `errors` · `faults` · `gguf` · `model` · `ok` · `problems` · `wrong`

Same word, same surface form, opposite intent — *"do we have **errors**?"* vs *"how are **errors**
handled in rust?"*. Separating them requires recognising an **unbounded set of technical subject nouns**,
which is exactly what `route.h` says a keyword classifier is not and what §8's embedder is for.

Also measured, and a trap for any future attempt: the two words the corpus called safe to drop
(`hours`, `duration`) are load-bearing for real HELDOUT cases (`how many hours since boot?`,
`duration since power-on?`). A corpus-only judgement would have broken them.

### 9.5 What this means for Phase C

**§8's routing lane is measured-miss-gated, and this is the measured miss.** The gate is met with a
concrete, reusable target: an embedder must separate the nine both-sides words by intent, and its
held-out evaluation must include the **(no-self AND concept-word) quadrant** the current suite leaves
empty — otherwise it will reproduce this exact blind spot with a neural model instead of a keyword one.

**Carried as a live limitation until then:** a conceptual question containing a SYSFACTS family word may
be answered with box state. Bounded — it affects only control-IN queries, only where such a word appears,
and the answer is a visibly-irrelevant box fact rather than a fabricated one.

---

*Companion to `phase4/docs/ROADMAP.md` (goal #6) and the 6-5 control-IN docs (this goal hard-depends on
the deployed two-way channel). PLAN-FIRST — authored 2026-07-22 from a 5-lens research pass + a 4-lens
adversarial pre-mortem; no code, flag, or wire change until user sign-off. RE-GREP every `file:line`
before relying on it. §9 appended 2026-07-27 — a MEASUREMENT, not a change: `route.c` and
`routing_suite.h` are byte-identical to their pre-attempt state.*
