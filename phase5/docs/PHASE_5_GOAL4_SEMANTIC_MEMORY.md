# Phase 5 — Goal #4: Semantic Memory (deterministic distill — separate fact store, NO LLM, NO embeddings)

**Status:** 🚧 IN PROGRESS — **M0 ✅ host/CI (2026-07-04: `semantic_store.c/h` raw-LBA circular fact store + `semantic_distill.c/h` pure deterministic distill, host-tested)** · **M1 ✅ BOX-VERIFIED (2026-07-04: gated `JARVIS_SEMANTIC` default-0 PA wiring — boot-scan tail-window distill → `sem_store_upsert`, WRITE-ONLY; S0-snapshot OFF-vs-ON smoke: ON `[SEM] window=1024 facts=1 upserted=1 stored=1`, OFF 0×`[SEM]`, `[INFER]` byte-identical 16=16, err=0/0 faults)**; M2 (telemetry/console slice) next. **Arc 2, the distill workstream — #7's core lands here.**
**Date:** 2026-07-04
**Prereqs:** #1 episodic store ✅ box-verified (the distill SOURCE — `epi_record_t` records with decision-cache-parity `query_key`s). Intertwined with **#7 (consolidation)**: the "compact episodic → semantic" half of #7 IS this goal's distill; #7's remaining scope (prune + promote scheduling as a low-priority job) builds on it.
**Sources:** `phase5/docs/PHASE_5_PLAN.md` §2 goal 4 (canon), §7.2 (NO embeddings — Phase 7), §7.6 (consolidation deterministic first; LLM distillation gated/Phase 7), §5 (the reserved raw-LBA memory region); `phase4/docs/ROADMAP.md:64` #4 + `:66` #7.

> **Goal (canon, `ROADMAP.md` Phase 5 #4):** Semantic memory — distilled facts and preferences.
> Stored separately from the raw episodic log.
>
> **Honest scope note:** the canon's example wording ("prefers briefings at 7am") describes the
> *aspiration*; what a deterministic, no-LLM distill can actually extract is **observable patterns**
> — see the honest ceiling below. The preference-shaped reading needs semantic understanding that is
> deliberately out of Phase 5 (§7.2/§7.6).

---

## 1. Scope + done-when

- **#4 builds two things:** a **separate semantic-fact store** (raw-LBA circular, the `episodic_store`
  pattern at a new base — D-a) and a **deterministic distill** (`sd_distill` — D-b) that compacts
  episodic records into durable `semantic_fact_t`s. Store + distill ONLY — the retrieval hook (G3
  reading "episodic + semantic", `PLAN §2` goal 3) is a FUTURE slice, not this goal (D-d).
- **Beyond the canonical done-when (deliberate):** Phase 5's §3 criteria are **already met** — the MVP
  arc closed 1/3/4/5 and #5 closed criterion 2. **#4/#7 are completeness + Phase-6-readiness goals**
  (the ROADMAP lists them as Phase-5 goals and #7's compaction protects the episodic window
  long-term), NOT a required-criterion closer. If priorities shift, parking #4 after M0 is a
  defensible call.
- **NO LLM, NO embeddings (locked, `PLAN §7.2`/`§7.6`):** the distill is rule-based, deterministic,
  frequency/consistency-driven. LLM distillation and similarity retrieval are Phase 7.

| Concern | Owner |
|---|---|
| The durable raw interaction log | **#1 (DONE)** — the distill SOURCE |
| Separate durable fact store + deterministic distill | **#4 (this goal)** — also #7's compact-core |
| Prune stale episodic entries + scheduled low-prio job | **#7** (builds on this) |
| Retrieval reads the semantic store (G3 "episodic + semantic") | **future G3 slice** — NOT #4 (D-d) |
| Preference *understanding* / LLM distillation / embeddings | **explicitly NOT Phase 5** — Phase 7 |

## 2. Honest ceiling (authored — the non-negotiable framing)

> **#4 compacts what the system has repeatedly seen into a durable fact store.** The deterministic
> distill extracts **observable patterns** from the episodic log — recurring query topics and stable,
> frequently-repeated Q&A — selected by **frequency and consistency**, nothing else. It does NOT
> extract stated user preferences, does NOT infer intent, and never "knows your preferences" or
> "understands" anything: a `semantic_fact_t` is "this exact question was asked ≥N times and its
> newest consistent answer was X," persisted. That honesty is the feature; the preference-shaped
> aspiration in the canon wording waits for semantic understanding (Phase 7).

## 3. Locked decisions

- **D-a — Store = a SEPARATE raw-LBA circular fact store** (the `episodic_store.c/h` pattern:
  header + fixed 512 B records, XOR header checksum, boot_id, circular cursor + monotonic
  total_entries, flush-after-write, device-independent read/write callbacks) at a **new base in the
  reserved Phase-5 gap**: `SEM_STORE_BASE_LBA = 21,110,000` (8-sector-aligned), **4096 facts × 512 B**
  = 4097 sectors (~2 MiB), ending ≈ 21,114,096 — clear of episodic (21,100,000 + 8193 → ends
  21,108,192, ~1,800 sectors of margin) and far inside the reserved ~8 GiB (`PLAN §5`). The region is
  RESERVED like the episodic one — installers/repartitions must not overlap it.
- **D-b — Distill = DETERMINISTIC rule (no LLM/embeddings):** group episodic records by `query_key`,
  count **support** = usable records (`action==EPI_ACT_INFER && outcome==EPI_OUT_OK && resp_len>0` —
  the proven #6/G3 usable filter, so canned cache echoes and failures never become "facts"), take the
  **newest** usable answer, and emit a `semantic_fact_t` when `support >= SEM_MIN_SUPPORT` (**start
  3**). `confidence_x100` = the share of usable same-key answers byte-identical to the chosen one
  (greedy decoding makes true repeats identical → typically 100; disagreement shows honestly).
  **This IS #7's "compact episodic → semantic" core — it lands here.**
- **D-c — Distinct from #6 (state the boundary):** #6 caches query→answer into the **decision cache**
  for FAST SERVING (<1 ms hits, volatile, capped at the HWM); #4 distills into a **semantic store**
  as durable facts — a future retrieval SOURCE (G3 reads "episodic + semantic"), NOT a serve path.
  Same source log, different consumers; neither replaces the other.
- **D-d — The retrieval hook is FUTURE:** G3 reading the semantic store is a later G3 slice with its
  own injection-hygiene review (the P6/P7 lessons apply to any new preamble source). #4 = store +
  distill only; nothing #4 ships changes generation.
- **D-e — Telemetry (M2) = one deliberate slice:** `semantic_fact_count` on the wire + a flag-gated
  console row, fixture-synced (golden + key-contract + honesty + e2e together — the v2..v5
  precedent), worded to the honest ceiling ("distilled repeated Q&A patterns" — never "knows your
  preferences").
- **D-f — Gated `JARVIS_SEMANTIC`** (jarvis_debug.h, **default 0**, introduced at M1 with the box
  wiring): all box wiring compiles out when OFF; deploy behavior-identical until a deliberate flip
  decision (M4 call — and like #5, "proven but gated-off" is an acceptable end state).

## 4. Mechanism (M1 shape — mirrors #5/#6 M1)

- **Distill site:** the `[STATS]`-cadence pass in Process A (BEFORE `epi_commit` clears the batch),
  plus a one-shot boot-scan distill over the persisted store (reusing the recall-scan walk) — both
  gated `JARVIS_SEMANTIC`.
- **Write path:** `sem_store_upsert` — a repeated subject UPDATES its fact in place (support raised
  monotonically via max — idempotent across boot re-distills), never a duplicate; new subjects append
  circularly. Low cadence (facts change on the [STATS] tick at most) → negligible NVMe wear.
- **Proof line:** `[SEM] facts=<n> new=<i> upd=<u>` at the distill cadence — honest counters only.

## 5. Verification model — host-test first

- **Layer A (HOST/CI, M0 — DONE):** both modules are pure (no seL4/device dep): the store runs
  against mock callbacks (fresh-init / round-trip / reboot-bump / corrupt-header / wrap / upsert),
  the distill against synthetic `epi_record_t[]` (threshold, support, newest-wins, consistency,
  dedup, usable-filter, n=0, max-cap, length-not-strlen). CI steps "Phase 5: Semantic store (C)" +
  "Phase 5: Semantic distill (C)".
- **Layer B (BOX, M1/M3):** gated wiring smoke (distill runs, `[SEM]` line, err=0, OFF =
  behavior-identical) and the M3 reboot-survival proof (facts persist across a power cycle,
  boot_id bumps, re-distill is idempotent — support does not double-count).

## 6. Milestones

- **M0 (HOST/CI)** ✅ **DONE 2026-07-04** — `semantic_store.{c,h}` (raw-LBA circular fact store,
  512 B `semantic_fact_t`, callback-driven, + `sem_store_upsert` insert-or-raise-support) +
  `semantic_distill.{c,h}` (`sd_distill` — the pure deterministic distill, #7's core) +
  `test_semantic_store.c` / `test_semantic_distill.c` + the two CI steps.
- **M1 (BOX)** ✅ **DONE 2026-07-04 (box-verified)** — gated `JARVIS_SEMANTIC` (default-0) wiring in
  Process A: `sem_store_init` after episodic-ready (nvme_log-independent); the boot recall-scan
  buffers the newest ≤1024 records **tail-only/chronologically** (no ring buffer — `sd_distill`'s
  newest-wins is position-based, so a chronological window is load-bearing) and runs the one-shot
  distill → `sem_store_upsert` with the `[SEM] window=/facts=/upserted=/stored=` proof line;
  `build_jarvis_x86.sh` injects both .c's into the PA source list. WRITE-ONLY (D-d honored).
  **S0-snapshot OFF-vs-ON smoke** (regions dd-restored between legs): ON `[SEM] window=1024
  facts=1 upserted=1 stored=1` / OFF zero `[SEM]` / **`[INFER]` byte-identical (16=16 exact)** /
  err=0, 0 faults. facts=1 is the honest tail composition (cache-serving dominates the newest
  records; usable INFER records are rare). *(The M1-shape "batch distill at the [STATS] cadence"
  from §4 was deliberately deferred — the boot-scan distill alone proves the mechanism; a live
  cadence fold is an M2+ call if the fact flow warrants it.)*
- **M2 (CI + BOX)** — the deliberate telemetry/console slice (D-e): `semantic_fact_count`,
  fixture-synced, honest wording.
- **M3 (BOX)** — reboot-survival: facts persist across a power cycle; re-distill idempotent
  (support monotonic, no double-count).
- **M4** — docs + week status; flag decision (default-ON only if the honest display earns it —
  the #5 "proven but gated-off" outcome is equally acceptable).

## 7. Risks & landmines

- **Overclaiming.** The one non-negotiable: no "knows your preferences" / "understands" anywhere
  (docs, console, telemetry captions). The honesty gate fences the console; keep M2 inside it.
- **Fact quality = answer quality.** The distill stores answer HEADS from the episodic log
  (`resp[256]`, head-stored — G6's correction); a distilled fact is only as good as its newest
  consistent answer. Consistency (`confidence_x100`) is surfaced, not judged.
- **Upsert scan cost.** `sem_store_upsert` linear-scans the store via the read callback (worst
  ~4096 sector reads ≈ 2 MiB); acceptable at the [STATS] cadence, NOT a hot path. If it ever
  matters, an in-RAM key index (the G3/M5 `epi_index` pattern) is the known fix.
- **Double-count on re-distill.** Boot re-scans re-see the same records; upsert uses
  max(existing, incoming) support — monotonic and idempotent — so re-distills cannot inflate
  support. The M3 box gate proves it.
- **Wrap semantics.** Both stores are circular: episodic wraps (~8192 records) limit what a boot
  re-distill can see; facts distilled earlier survive in the semantic store even after their source
  records rolled off — that is the point of #4.

---

*Mirrors `PHASE_5_GOAL5_SHIELD_LEARNING.md` / `PHASE_5_GOAL6_CACHE_GROWTH.md`; the plan it serves is
`PHASE_5_PLAN.md` (§2 goal 4, §7.2, §7.6). #7's remaining scope (prune + job scheduling) gets its own
doc when opened.*
