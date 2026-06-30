# Phase 5 — Goal #6: Cache Growth (promote repeated query→action patterns from the episodic log into the decision cache)

**Status:** 🔜 PLANNED (M0 next). *(See D-a: a first-cut live-routing "#6a" already landed under `JARVIS_CACHE_GROWTH` — this doc re-scopes the canonical #6 to promote-from-the-log.)*
**Date:** 2026-06-30
**Prereqs:** #1 episodic store ✅ **box-verified** (M0–M5 — **THE source** #6 promotes from) + the deployed **decision cache** (`g_cache`, ~308 preloaded patterns, `CACHE_SIZE=512`) + decision-cache **FNV-1a key parity** (`cache_hash(cache_normalize_query(...))`, shared with #1/#3 — never re-implemented, so an episodic `query_key` IS a cache key).
**Scope:** ROADMAP goal #6 — a bounded background pass promotes repeated `query→action` patterns out of the episodic log into the decision cache, so future lookups become fast (~1 ms) cache **HITS**. The cache GROWS past its ~308 baseline; this is the last leg of the "it-remembers" MVP arc (#1/#2/#3/#6) → the early **`memory` milestone tag**.
**Sources:** `phase4/docs/ROADMAP.md:65` + `phase5/docs/PHASE_5_PLAN.md:18` (canon); `PHASE_5_PLAN.md:14` (keystone-first — every goal reads #1; cache growth *promotes from* it), `:87` (MVP-4 task: promotion logic + the dormant SEC-024 LRU goes **live**, host-test first), `:103` (telemetry = one deliberate slice, **not** smuggled into reserved bytes), `:111` (the SEC-024-LRU-goes-live risk); `phase3/src/ai/decision_cache.{c,h}` (`cache_insert`/`cache_lookup` + the dormant LRU); `phase3/src/ai/episodic_store.{c,h}` + #3/M5's bounded key-index (the scan source).

> **Goal (canon, `ROADMAP.md:65` / `PHASE_5_PLAN.md:18`):** Cache growth — promote repeated query→action patterns from the episodic log into the decision cache **automatically**, so the cache hit-rate improves measurably with use.

---

## 1. Scope + done-when

- **#6 is a SIBLING of #1, NOT sequenced behind #3** (G3 §1): it depends on the **episodic store only**, so it can land in parallel with retrieval.
- **Closes a ROADMAP done-when** (`ROADMAP.md:72`): **"cache hit rate improves measurably after 1 week of use (target >90%)."** #6 is the mechanism that makes the hit-rate climb — a repeated question, once promoted, is answered from the cache (~50 ms → <1 ms) instead of re-inferred.
- **The cache GROWS:** `entries_used` climbs past the ~308 preload baseline as patterns promote. As it approaches `CACHE_SIZE=512` the **dormant SEC-024 LRU eviction goes LIVE** — host-test that path FIRST (§4); it is currently unreached (the cache has never filled at ~258/512).
- **Boundary:**

| Concern | Owner |
|---|---|
| The durable interaction log (records survive reboot) | **#1 (DONE, box-verified)** — THE source |
| Routing *live inference* through the cache to learn one answer at a time | **explicitly NOT #6** (rejected — **D-a**; canon is "promote from the **log**") |
| Promote repeated `query→action` patterns from the log into the cache | **#6 (this goal)** |
| Surface the growth as a live console signal (`cache_growth_count`) | **#6b** (the telemetry/console slice) |
| LLM-distilled facts/preferences | **#4 (Arc 2)** — not built |

---

## 2. What #6 reads + the realistic promotion

- **The episodic store (#1, THE source):** `epi_record_t {query_key, action, outcome, query[200], resp[256]}` — the durable log of every cache/inference interaction. `query_key = cache_hash(cache_normalize_query(query))` (FNV-1a, decision-cache parity), so an episodic key IS a cache key — no translation.
- **The scan window:** the in-RAM `g_epi_batch` (this boot, before the `[STATS]` commit) ∪ the persisted records reachable via #3/M5's bounded **key-index** (`epi_index_lookup`) / the one-time boot scan — **never an O(N) per-query NVMe scan**.
- **The decision cache (the target):** `cache_insert(query→action, TRUST_AUTO)`; `g_cache.stats.entries_used` is the live growth counter (minus the post-preload baseline).

**Realistic promotion (frequency-based, deterministic):** count `query_key` occurrences in the scan window; a key whose frequency ≥ `PROMOTE_THRESHOLD` (start 2–3) is a "repeated pattern" → promote its `query → action` (the episodic record's logged response/action, truncated to `MAX_ACTION_LEN=256`). Deterministic, cheap, no model.

**The action source:** the promoted "action" is the episodic record's logged `resp`/action — exactly what was returned for that query before. A promoted cache hit therefore returns **the logged episodic action** — generation-equivalent for that query (it IS what was served).

---

## 3. The promotion mechanism

- **BOUNDED, low cadence (D-c):** the promotion pass runs at the `[STATS]` cadence (~every 100 q) or as a one-shot post-boot pass — **NOT per query**. It scans the in-RAM batch + the M5 key-index; it never does an O(N) NVMe scan per query (the <1 ms cache-hit path stays untouched).
- **Promote (D-b):** for each key with frequency ≥ `PROMOTE_THRESHOLD`, `cache_insert(&g_cache, normalized_query, action, TRUST_AUTO)` — action truncated to `MAX_ACTION_LEN=256`. Already-present keys are a no-op (or a refresh).
- **Eviction (D-d):** once `entries_used` reaches `CACHE_SIZE=512`, `cache_insert` must evict — the **dormant SEC-024 LRU** (oldest `last_access_time`) goes live. **Host-tested first** (§4).
- **On/off (D-f):** compile-time `JARVIS_CACHE_GROWTH` (`jarvis_debug.h`, **default OFF**) — the whole promotion block compiles out, deploy byte-identical. Flip ON only after the box growth + hit-rate + clean-eviction proof (M3).
- **Growth signal:** `g_cache_baseline` snapshots `entries_used` post-preload; `cache_growth_count = entries_used − baseline`; a `[CACHE-GROW]` log line.

---

## 4. The verification model — host-test first; the dormant LRU goes live

The promotion logic + the SEC-024 LRU eviction are **PURE / host-testable** → **host + CI FIRST** (the plan's "host-test first", `PLAN:87`). The live promotion, the hit-rate improvement, and the eviction firing are **BOX** (generation / NVMe / scale only exist on the box).

- **Layer A (HOST/CI — deterministic, no device):**
  - the **frequency scan + threshold** (known records → deterministic promote set),
  - the **SEC-024 LRU eviction** — the dormant path goes **live and tested**: oldest `last_access_time` evicted, `entries_used` semantics correct, no corruption (the named risk, `PLAN:111`),
  - `cache_insert`/`cache_lookup` round-trip (already host-tested) — a promoted pattern becomes a lookup HIT.
- **Layer B (BOX):**
  - **the cache GROWS:** `entries_used` climbs past baseline (`[CACHE-GROW]`); promoted patterns now **HIT** the cache fast (the done-when),
  - **eviction fires** once the cache fills (≥512) without corruption,
  - **OFF = behavior-identical** to the recorded baseline (the flag compiles out).
- **`cache_growth_count` telemetry/console (#6b):** CI (golden fixture + key-contract + honesty + e2e) + **box-confirm live** — the M4 fixture-synced-slice pattern (`PLAN:103`).

**THE HONEST CLAIM:** the growth count is real (`entries_used − baseline`); the hit-rate improvement is the **done-when** (measured on the box). A promoted cache hit returns the **logged episodic action** (== what was served before) — generation-equivalent. The cache **learns FREQUENTLY-ASKED queries and serves them fast** (frequency-based, deterministic) — **NEVER "understands".**

---

## 5. Locked decisions (Decision + Rationale)

**D-a — Source = the EPISODIC LOG (#1), NOT live-routing the cache.** Promotion scans the durable episodic log for recurring patterns; it does **not** route every live inference through the cache to learn one answer at a time. *Rationale:* canon is "promote repeated patterns **from the log**" (`ROADMAP:65`) — the log is the system-wide record of what actually recurred, batch-promotable at a low cadence; per-query live-routing is a different mechanism (a fast-path cache wrapper) that this goal does not adopt. *(Rejected alternative explicitly noted — and **already implemented**: a first-cut "#6a" wrapped the inference path with a per-query `cache_lookup`/`cache_insert` under `JARVIS_CACHE_GROWTH` (commit `7e8c30f`, 2026-06-30). D-a **re-scopes** the canonical #6 to promote-from-the-log, so #6a is either superseded by this design or kept as a complementary fast-path — **TO RECONCILE before M1** (incl. whether the promotion pass reuses `JARVIS_CACHE_GROWTH` or gets its own flag).)*

**D-b — "Repeated pattern" = frequency ≥ `PROMOTE_THRESHOLD`** (start 2–3) over the scanned episodic window; promote `query → action` (the record's `resp`/action, truncated to `MAX_ACTION_LEN=256`), `trust = TRUST_AUTO`. *Rationale:* frequency is the canon signal ("repeated"), deterministic and cheap; `TRUST_AUTO` matches a self-derived, non-harmful cached answer.

**D-c — BOUNDED: low-cadence pass, never per-query.** The promotion pass runs at the `[STATS]` cadence (or one-shot post-boot), scanning the in-RAM batch + the M5 key-index — never an O(N) per-query NVMe scan. *Rationale:* the <1 ms cache-hit path must stay untouched; `PLAN:112`'s <50 ms / bounded-scan discipline.

**D-d — the dormant SEC-024 LRU eviction goes LIVE — host-test it FIRST.** Cache growth makes the never-reached eviction path execute; host-test eviction correctness (oldest `last_access_time` evicted, `entries_used` semantics, no corruption) before relying on it. *Rationale:* `PLAN:111` names this exact risk — the cache has never filled (~258/512), so the path is unproven.

**D-e — `cache_growth_count` telemetry is a DELIBERATE, surfaced field** (`PLAN:103` — "not smuggled into reserved bytes") + a `TLM_F_CACHE_GROWTH` flag, landing with the golden-fixture + key-contract + honesty + console tests **together** (the M4 fixture-synced-slice pattern). *Rationale:* `PLAN:103/113` — a wire change is one deliberate, fixture-synced slice. *(Sub-decision at the telemetry milestone: a deliberate vN size-bump OR a deliberately-renamed spare `reserved_i` — settle there; the requirement is "deliberate + surfaced", not hidden.)*

**D-f — Gated `JARVIS_CACHE_GROWTH` compile-default-OFF** until box-proven, then flip ON + the `memory` tag. *Rationale:* keep the new write-path (cache mutation + eviction) dark until the box proves growth + hit-rate + clean eviction. *(Flag-ownership pending D-a's reconciliation.)*

**D-g — Honest ceiling.** "The cache learns FREQUENTLY-ASKED queries and serves them fast (~50 ms → <1 ms)" — frequency-based promotion, deterministic; a promoted hit returns the logged episodic action (generation-equivalent for that query). **NEVER "the cache understands".** *Rationale:* same honesty discipline as G3 (D-f) — claim only what's mechanically true.

---

## 6. Milestones — M0 → M4 (host vs box marked)

- **M0 (HOST/CI)** — pure **promotion logic** (frequency scan + threshold → deterministic promote set) + the **SEC-024 LRU eviction** host-test (the dormant path goes live, tested: oldest evicted, `entries_used` semantics, no corruption) + unit tests. CI-greenable.
- **M1 (BOX)** — wire the **bounded promotion pass** into Process A (scan episodic → `cache_insert` the promoted patterns, gated) + a `[CACHE-GROW]` log; box smoke = the cache **GROWS** (`entries_used` climbs past baseline). *(First: reconcile D-a — promote-pass vs the landed #6a live-routing.)*
- **M2 (CI + BOX)** — **`cache_growth_count`** deliberate telemetry slice + console cache-growth row (auto Capabilities flag + System stat), golden-fixture / key-contract / honesty / e2e green; box-confirm the count live (#6b).
- **M3 (BOX)** — **hit-rate-improvement proof** (promoted patterns now HIT the cache fast) + the **LRU eviction firing** once the cache fills (≥512, no corruption) + flip `JARVIS_CACHE_GROWTH` default-**ON**.
- **M4** — the **`memory` milestone tag** (the it-remembers MVP arc #1/#2/#3/#6 complete) + final doc/week status.

*(M0 is CI-greenable; M1/M3 are inherently box-gated — growth / eviction / hit-rate only exist at scale on the box; M2 is CI + box-confirm.)*

---

## 7. Risks & landmines

- **The dormant SEC-024 LRU goes live** — host-test eviction correctness (oldest `last_access_time` evicted, `entries_used` semantics, no corruption) **before** relying on it; the path has never executed (`PLAN:111`).
- **Promotion threshold tuning** — too low pollutes the cache with one-off queries (and triggers premature eviction); too high = the cache never grows. `PROMOTE_THRESHOLD` is a tunable `#define`; measure on-box.
- **Bounded scan** — never an O(N) per-query NVMe scan; reuse the in-RAM batch + the M5 key-index, run at the `[STATS]` cadence (`PLAN:112`).
- **Telemetry must be deliberate, not smuggled** (`PLAN:103`) — `cache_growth_count` lands as one fixture-synced slice (golden + key-contract + honesty + console together).
- **`cache_insert` action truncation** — episodic `resp`/action truncated to `MAX_ACTION_LEN=256`; copy by length, never `strlen` (episodic text is not NUL-terminated).
- **"Learns ≠ understands"** — frequency-based promotion only; a promoted hit returns the logged episodic action (== what was served), generation-equivalent — never claim comprehension.
- **#6a/#6-promote overlap (D-a)** — a live-routing #6a already ships under `JARVIS_CACHE_GROWTH`; reconcile (supersede vs complementary, flag ownership) before wiring the promote pass at M1.

---

## Done-when (authored — none existed)

> **#6 is done when:** the promotion logic + the SEC-024 LRU eviction pass **host CI** deterministically (the dormant path is live and tested); the cache demonstrably **GROWS** from promoted episodic patterns (`entries_used` climbs past the ~308 baseline, box); promoted patterns become fast cache **HITS** (a measurable hit-rate improvement — the `ROADMAP:72` done-when, box); the **LRU eviction fires** without corruption once the cache fills; `cache_growth_count` is a **real, live console signal**; and the path is gated default-off until box-proven, then flipped ON. Then the **it-remembers MVP arc (#1/#2/#3/#6) is complete → the early `memory` milestone tag.**

---

*Phase 5 cadence: weekly status at `phase5/weeks/weekN/WEEK_N_STATUS.md`. This doc mirrors `phase5/docs/PHASE_5_GOAL3_RETRIEVAL.md`; the plan it serves is `phase5/docs/PHASE_5_PLAN.md`. #6 is a sibling of #1 (it promotes from the episodic log), NOT sequenced behind #3; it is the last leg of the it-remembers MVP arc.*
