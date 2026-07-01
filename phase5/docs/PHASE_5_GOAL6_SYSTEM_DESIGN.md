# Phase 5 — Goal #6: Cache Growth — System Design

**Status:** Design (system design from scratch, feeds the M0–M4 plan in `PHASE_5_GOAL6_CACHE_GROWTH.md`)
**Date:** 2026-07-01
**Author:** design pass over the as-built code (`decision_cache.c`, `episodic_store.c/h`, `main_x86.c`)
**Companion:** `phase5/docs/PHASE_5_GOAL6_CACHE_GROWTH.md` (scope + locked decisions D-a…D-g). This doc is the engineering design under that plan: components, data flow, the concurrency/failure model, and two findings the plan doesn't yet cover (the LRU saturation cliff and the frequency-signal gap in the M5 index).

---

## 1. Problem & goal

The decision cache (`g_cache`) boots with ~308 preloaded patterns and never changes size for the life of the process. Every query the box can't answer from those 308 patterns is a cache miss → full LLM inference (~50 ms–seconds). The episodic store (#1, box-verified) already durably records every interaction as `{query_key, action, outcome, query, resp}`. Nothing reads that log back into the cache.

**Goal (canon, `ROADMAP.md:65`):** a bounded background pass promotes *repeated* `query→action` patterns out of the episodic log into the decision cache, automatically, so a question asked often enough stops being re-inferred and starts being served from the cache in <1 ms. Success = the `ROADMAP:72` done-when: **cache hit-rate improves measurably with use.**

Non-goals (owned elsewhere): the durable log itself (#1), routing live inference through the cache one answer at a time (rejected — D-a, the reverted #6a), and LLM-distilled facts/preferences (#4, Arc 2).

---

## 2. Requirements

**Functional**

- FR1 — Mine the episodic log for `query_key`s that recur at or above a threshold.
- FR2 — Promote each qualifying pattern's `query → action` into `g_cache` via `cache_insert(…, TRUST_AUTO)`.
- FR3 — The cache demonstrably grows: `entries_used` climbs past the post-preload baseline.
- FR4 — A promoted pattern becomes a fast cache **hit** on the next matching query.
- FR5 — Growth is surfaced as one honest, live telemetry field + console row (#6b).

**Non-functional**

- NFR1 — **Never touch the <1 ms hit path.** The per-query `cache_lookup` in the workload loop is untouched; promotion runs out-of-band.
- NFR2 — **Bounded work.** No O(N) NVMe scan per query. The pass runs at the `[STATS]` cadence (~every 100 q) over in-RAM data + a one-time boot aggregate.
- NFR3 — **Deterministic + host-testable.** The promotion decision is a pure function of (records, threshold, cache occupancy). No device, no model needed to test it.
- NFR4 — **Byte-identical when OFF.** Gated by `JARVIS_CACHE_GROWTH` (default 0); the block compiles out.
- NFR5 — **Honest ceiling.** The system learns *frequently-asked queries* and serves them fast. It never "understands" (D-g).
- NFR6 — Fits the deployment: runs in Process A (which has the allocator and a normal stack); mutates only PA-local state.

---

## 3. Constraints from the as-built code (ground truth)

These are read out of the source, not assumed — they shape every decision below.

| # | Constraint | Source | Consequence for #6 |
|---|---|---|---|
| C1 | Key parity: `query_key = cache_hash(cache_normalize_query(query))` | `episodic_store.c:12`, `decision_cache.c:78` | An episodic key **is** a cache key. No translation layer. |
| C2 | `cache_insert` needs the **normalized query string**, not just the key; it `strcmp`s it and rejects `strlen ≥ MAX_QUERY_LEN(128)` | `decision_cache.c:159,182` | Promotion must re-normalize `record.query` and skip ones that don't fit 127 chars. |
| C3 | Episodic `query[200]` / `resp[256]` are **raw, length-prefixed, not NUL-terminated**; `resp` is a ≤256-byte **tail** of the response | `episodic_store.h:81`, `main_x86.c:2966` | Copy by `*_len` into a NUL-terminated temp before use; the promoted action is the stored tail (full answer only when the response was ≤256 B). |
| C4 | `entries_used` increments on insert, is **unchanged on eviction**, decrements on remove | `decision_cache.c:190,237,266` | `cache_growth_count = entries_used − baseline` is well-defined only while no eviction has fired. |
| C5 | The SEC-024 LRU branch runs **only when the table has zero EMPTY slots**; it overwrites the global-oldest entry in place | `decision_cache.c:214` | See §7 — this is a saturation cliff, not a clean ring buffer. |
| C6 | The M5 boot index `g_epi_index` is **deduped to newest** (`key → newest logical_index`) | `main_x86.c:2009`, `episodic_store.h:138` | It cannot supply frequency as-built. See §6. |
| C7 | `g_cache` is `static` in `main_x86.c`; only Process A touches it; PB does inference and never sees it | `main_x86.c:420` | Promotion is single-threaded within PA's loop — **no lock needed**. |
| C8 | Promotion has a natural hook: `epi_commit()` already runs in the `[STATS]` block | `main_x86.c:3085` | Wire the pass right after the commit — same cadence, same data in hand. |
| C9 | Telemetry `flags` is `uint16_t` (used to `0x80`); a spare `reserved_i` (`uint16_t`) exists | `jarvis_telemetry.h:47,53` | `TLM_F_CACHE_GROWTH = 0x100` fits; `reserved_i` can carry the count with no size bump. |
| C10 | `#6a` live-routing (commit `7e8c30f`) was **reverted**; `JARVIS_CACHE_GROWTH` is retained for the promotion pass | `jarvis_debug.h:75` | D-a is effectively reconciled: the promotion pass owns the flag; there is no competing live-router to merge. |

---

## 4. Architecture

Promotion is a bounded consumer that sits beside the existing episodic commit, entirely inside Process A. It reads memory, writes the cache, and emits one signal. It is off the query hot path.

```
Process A (rootserver, single-threaded workload loop)          Process B
┌───────────────────────────────────────────────────────┐    ┌──────────┐
│  per query:                                             │    │ LLM      │
│    cache_lookup(g_cache) ──hit──> serve <1ms  (UNTOUCHED)│    │ inference│
│                         └─miss─> IPC ───────────────────┼───>│ (PB owns │
│    epi_batch_add(query, action, outcome, resp)          │<───┤  no cache)│
│                                                          │    └──────────┘
│  boot (once):  scan persisted store ─> g_epi_index       │
│                                     └─> g_key_freq  (NEW) │
│                                                          │
│  every ~100 q  ([STATS] cadence):                        │
│    epi_commit()                    (existing)            │
│    ┌─────────────────────────────────────────────┐      │
│    │  CACHE-GROWTH PASS  (NEW, gated)             │      │
│    │  1 count freq over batch ∪ persisted aggregate│     │
│    │  2 select keys with freq >= PROMOTE_THRESHOLD │     │
│    │  3 for each: normalize query, take resp tail, │     │
│    │     cache_insert(TRUST_AUTO)  (skip if cached │     │
│    │     or table at high-water mark)              │     │
│    │  4 cache_growth_count = entries_used-baseline │      │
│    │     emit [CACHE-GROW]; set TLM_F_CACHE_GROWTH  │     │
│    └─────────────────────────────────────────────┘      │
└───────────────────────────────────────────────────────┘
        │                                     │
        └── telemetry packet (v3/v4) ─UDP──> telemetry_receiver.py ─SSE─> console
```

Four components, described in §5–§6.

---

## 5. Components

**5.1 Frequency counter.** Produces `freq[key]` over the scan window. Window = the in-RAM `g_epi_batch` (this-boot, uncommitted) **∪** a persisted aggregate built once at boot (§6). Output: a small list of `{key, freq}`. Pure; no device.

**5.2 Promotion policy / filter.** For each key with `freq ≥ PROMOTE_THRESHOLD` (start 2–3):
- resolve a representative record for the key (newest usable one — prefer `action == EPI_ACT_INFER && outcome == EPI_OUT_OK`, mirroring G3's `g3_candidate_usable`, so we promote real answers, not canned cache echoes);
- copy `query_len` bytes → temp, `cache_normalize_query`; **skip** if normalized length ≥ 128 (C2);
- copy `resp_len` bytes → temp action (≤256, C3);
- **skip if already cached** (`cache_lookup` first) and **skip if at the high-water mark** (§7);
- else `cache_insert(&g_cache, norm, action, TRUST_AUTO)`.
Deterministic given (records, threshold, occupancy) → directly host-testable (NFR3).

**5.3 Cache writer + growth accounting.** `g_cache_baseline` snapshots `entries_used` immediately after `cache_load_initial/extended_patterns` (`main_x86.c:3220`). After each pass, `cache_growth_count = entries_used − g_cache_baseline`. Emit `[CACHE-GROW] promoted=… used=… grow=…`.

**5.4 Telemetry / console slice (#6b).** Repurpose `reserved_i → cache_growth_count` (uint16, C9 — max plausible growth ≈ 512−308 ≈ 204 « 65535) and add `TLM_F_CACHE_GROWTH = 0x100`, set only once the pass has promoted something. One fixture-synced slice: `jarvis_telemetry.h` + `telemetry_receiver.py` decode + golden fixture + key-contract + console honesty + e2e, landed together (D-e). Console: a Capabilities "Cache growth" row (auto from the flag) + a System "Patterns promoted" stat. Claim = count only — never "the cache learned to understand."

---

## 6. The frequency-signal gap (finding #1)

The plan says the scan window is "the in-RAM batch ∪ the M5 key-index." But the M5 index is **deduped to newest** (C6) — it answers "where is the latest record for this key," not "how many times was this key seen." As-built it cannot drive a frequency threshold.

Two ways to close it:

| Option | How | Cost | Signal |
|---|---|---|---|
| **A — batch-only frequency (MVP)** | Count duplicate keys within `g_epi_batch` each pass | ~zero | "repeated within the last ~100 q" — weak; misses cross-window repeats |
| **B — rolling count aggregate (recommended)** | Seed `g_key_freq[key]` from the one-time boot scan (already reads every stored record for `g_epi_index`, `main_x86.c:2009`); then **fold each committed batch into it** so it stays current; `freq = g_key_freq[key]` (single source, no batch double-count) | one extra increment per record, in a scan that already runs + one per committed record | "asked N times across the retained window" — the real signal for the 1-week done-when |

Recommendation: **B.** It reuses a scan that already happens, needs no new NVMe I/O, and is what actually makes hit-rate climb over a week. Represent it as a fixed-capacity open-addressed `{key,count}` table sized to `EPI_STORE_MAX_ENTRIES` (same envelope as `g_epi_index`), or fold a `count` field into `epi_index_entry_t`. Either is host-testable in isolation. (Wrap over-count: once the circular store passes 8192 records, keys whose old records were overwritten stay counted — benign, since frequency only needs to cross a small threshold, and it resets on the next boot scan.)

Start with A behind the flag to land M1 quickly if B slips, but treat B as the design target — the done-when depends on it.

---

## 7. The LRU saturation cliff (finding #2)

The plan calls the SEC-024 LRU "dormant" and says to "host-test it before relying on it." Reading the code (`decision_cache.c:214–239`) sharpens *why*:

- The eviction branch is reached **only when the entire 512-slot table has zero EMPTY slots** (the probe loop cycles all slots without finding EMPTY). Below that, inserts always find an EMPTY/tombstone slot.
- When it fires, it overwrites the global-oldest VALID entry **in place** at `lru_index`, then advances `time_counter`. `entries_used` is left unchanged (C4).

Implications, precisely:

1. **It is findability-safe, not a bug.** In a zero-EMPTY table a subsequent `cache_lookup` never hits an early EMPTY terminator, so it scans until it reaches the relocated entry and matches. Nothing becomes unreachable.
2. **But it converts misses into full O(512) scans.** With no EMPTY slot anywhere, every *miss* now probes all 512 entries before returning false (`cache_lookup` only short-circuits on EMPTY). The <1 ms guarantee (NFR1) erodes exactly when the cache is busiest.
3. **Tombstones are never reclaimed to EMPTY** (`cache_remove` sets TOMBSTONE; nothing rehabilitates them), and `entries_used` pins at the fill level, so `cache_growth_count` stops being meaningful (C4) once saturated.

**Design response — don't let the table saturate.** Promotion is the only thing that grows the cache, so cap it: promote only while `entries_used < PROMOTE_HWM`, with `PROMOTE_HWM ≈ 0.75–0.80 × CACHE_SIZE` (≈ 384–410). Consequences:

- The table always retains EMPTY slots → both hits and misses keep short-circuiting → the <1 ms path is preserved by construction.
- LRU eviction becomes a **guardrail** (correctness backstop if the estimate is wrong), not the steady-state mechanism. We still host-test it (M0), but we don't depend on it running in production.
- `cache_growth_count` stays monotonic and meaningful.

Optional, orthogonal: raise `CACHE_SIZE` 512 → 1024 for more promotion headroom (load factor 308/1024 ≈ 0.30). Cost is a larger `g_cache` struct (~426 KB vs ~213 KB — trivial on 32 GB) and re-touching the `% CACHE_SIZE` distribution. Not required for MVP; a good follow-up if the HWM proves too tight.

This reframes plan decision **D-d**: keep the LRU host-tested, but make the promotion HWM the real ceiling.

---

## 8. Data flow (one `[STATS]` tick)

1. Workload loop reaches `q % 100 == 0` → existing `[STATS]`/`[SNAP]` emit, then `epi_commit()` flushes the batch to NVMe.
2. `#if JARVIS_CACHE_GROWTH` pass begins (still holding `g_epi_batch` contents pre-clear, or run before the commit clears it — sequence so the batch is still populated).
3. Fold the batch keys into `g_key_freq` (Option B), so `freq[key] = g_key_freq[key]` is a single, current source.
4. Select keys with `freq ≥ PROMOTE_THRESHOLD` not already cached, while `entries_used < PROMOTE_HWM`.
5. For each: resolve newest usable record (batch first, else `epi_index_lookup` + one bounded `epi_store_read`), normalize + length-guard, `cache_insert(TRUST_AUTO)`.
6. Recompute `cache_growth_count`; emit `[CACHE-GROW]`; set `TLM_F_CACHE_GROWTH` if `> 0`.
7. Next telemetry packet carries `cache_growth_count` + the flag → console.

Everything in 2–6 is bounded by `g_epi_batch` size + the promote list; no per-query cost, no unbounded NVMe.

---

## 9. Concurrency, safety, determinism

- **Single-writer.** `g_cache`, `g_epi_batch`, `g_epi_index`, `g_key_freq` are all PA-local (C7). The pass runs inside PA's single-threaded workload loop. No cross-process sharing, no lock, no seqlock. PB is unaffected.
- **Off the hot path.** The only shared touchpoint with live traffic is `g_cache`, mutated between queries at the `[STATS]` tick, never concurrently with a `cache_lookup`.
- **Determinism.** Given a record set, threshold, and starting occupancy, the promoted set and final cache state are fixed — the basis for host CI (§10).
- **OFF = identical.** Under `JARVIS_CACHE_GROWTH=0` the pass, the `g_key_freq` aggregate, the baseline snapshot, and the telemetry field all compile out; deploy is byte-identical (NFR4). `reserved_i` stays reserved and the flag bit stays clear until the code is compiled in *and* has promoted something.

---

## 10. Verification strategy

**Layer A — host / CI (deterministic, no device):**
- Frequency + threshold: seeded records → exact expected promote set (incl. below-threshold excluded, dedup, usable-record selection).
- Normalization/length guards: query normalizing to ≥128 is skipped; non-NUL-terminated inputs copied by length; resp tail ≤256.
- **SEC-024 LRU (the named risk):** fill to zero-EMPTY, force eviction, assert oldest `last_access_time` evicted, relocated entry still findable, no corruption, `entries_used` semantics.
- **High-water mark:** promotion stops at `PROMOTE_HWM`; table never reaches zero-EMPTY under normal promotion; a saturated-table miss still returns correctly (guardrail proof).
- Round-trip: a promoted pattern is a `cache_lookup` hit returning the stored action.

**Layer B — box (only exists at scale):**
- Cache **grows**: `entries_used` climbs past baseline (`[CACHE-GROW]`).
- Promoted patterns now **hit** fast → measurable hit-rate improvement (the done-when).
- Guardrail: run long enough to approach the HWM; confirm the <1 ms path holds and (if forced past HWM in a test build) eviction fires cleanly.
- **OFF = behavior-identical** to the recorded baseline.

**Telemetry slice:** golden fixture + key-contract + honesty grep + console e2e green in CI, then box-confirm `cache_growth_count` climbs live over the I211.

New test files get a matching `.github/workflows/ci.yml` step (repo rule).

---

## 11. Key decisions & trade-offs

| Decision | Choice | Alternative rejected | Why |
|---|---|---|---|
| Source of "repeated" | Episodic log, promote from it (D-a) | Live-route every inference through cache (#6a) | Canon; #6a already reverted (C10); batch promotion is bounded and off the hot path |
| Frequency signal | Persisted count aggregate built at boot (Opt B, §6) | Reuse M5 index (deduped) / batch-only | The index can't count (C6); batch-only misses cross-window repeats; B is nearly free |
| Growth ceiling | Promotion high-water mark ~0.8 load (§7) | Rely on LRU eviction as the ceiling | Saturation destroys the <1 ms miss path (C5); HWM keeps EMPTY slots by construction |
| Eviction | Keep as host-tested guardrail | Remove it / depend on it | Backstop if HWM mis-estimated; never the steady state |
| What gets promoted | Newest **usable** record's `query → resp` (infer+OK) | Any record incl. cache-echo actions | Mirrors G3 `g3_candidate_usable`; avoids promoting canned strings |
| Telemetry field | Rename spare `reserved_i → cache_growth_count`, flag `0x100` | v4 size-bump (216→224, CRC@220) | No wire-size change; matches the `episodic_count`/`reserved2` precedent; uint16 flags has room (C9) |
| Trust level | `TRUST_AUTO` | Higher trust tiers | A self-derived, non-harmful cached answer (D-b) |
| Gating | `JARVIS_CACHE_GROWTH` default OFF, flip ON after box proof | Ship ON | Keep the new write path dark until growth + hit-rate + guardrail proven (D-f) |

---

## 12. Failure modes & edge cases

- **Threshold too low** → one-off queries pollute the cache and rush the HWM. Mitigate: start `PROMOTE_THRESHOLD` at 2–3, tune on-box; HWM caps the blast radius.
- **Threshold too high** → cache never grows, done-when unmet. Tunable `#define`; measure.
- **Normalized query > 127 chars** → `cache_insert` would reject; skip early (C2).
- **resp is a tail** → long answers promote only their tail (C3). Acceptable for MVP; if fuller answers matter, store the head or widen the episodic schema (Arc 2 concern).
- **Duplicate promotion** → `cache_lookup`-before-insert + `cache_insert`'s in-place update make it idempotent.
- **Reboot** → Option B's aggregate rebuilds from the persisted store at boot, so frequency survives power-cycles (consistent with #1/M5 recall).
- **Saturation despite HWM** (mis-estimate) → LRU guardrail keeps correctness; `[CACHE-GROW]`/`entries_used` flags it for retuning.

---

## 13. Milestones (refines the plan's M0–M4)

- **M0 (host/CI)** — pure promotion logic (freq scan + threshold + usable-record select + normalize/length guards) **and** the SEC-024 LRU test **and** the HWM test. All deterministic, CI-green.
- **M1 (box)** — wire the pass into PA at the `[STATS]` hook (C8) with Option B's boot aggregate + `g_cache_baseline` + `[CACHE-GROW]`, gated. Smoke: cache grows past baseline; <1 ms path intact.
- **M2 (CI + box)** — `cache_growth_count` telemetry slice + console rows (D-e); golden/key-contract/honesty/e2e green; box-confirm the count climbs live.
- **M3 (box)** — hit-rate-improvement proof over a real run; HWM holds (or guardrail eviction fires cleanly); flip `JARVIS_CACHE_GROWTH` default ON.
- **M4** — the `memory` milestone tag (it-remembers arc #1/#2/#3/#6 complete) + doc/week status.

---

## 14. Done-when

The promotion logic, the SEC-024 LRU eviction, and the high-water-mark cap pass host CI deterministically; the cache demonstrably **grows** from promoted episodic patterns on the box (`entries_used` past the ~308 baseline); promoted patterns become fast cache **hits** with a measurable hit-rate improvement (`ROADMAP:72`); the saturation guardrail is proven not to corrupt or to silently degrade the <1 ms path; `cache_growth_count` is a real, live console signal; and the path is gated OFF until box-proven, then flipped ON — completing the it-remembers MVP arc → the early `memory` tag.

---

## 15. Open questions

1. `PROMOTE_THRESHOLD` and `PROMOTE_HWM` starting values — proposed 2–3 and ~0.8×512; both need on-box tuning against the real query mix.
2. Ship Option A (batch-only) at M1 and upgrade to B, or land B directly? B is the design target; A is only a schedule hedge.
3. Raise `CACHE_SIZE` to 1024 now (headroom) or keep 512 + HWM and revisit if the HWM is too tight?
4. `cache_growth_count` field: confirm `reserved_i` is truly spare at implementation time (vs a clean v4 bump) — the plan's D-e sub-decision, settled here toward the rename.
