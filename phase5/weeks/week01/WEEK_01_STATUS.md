# Phase 5 — Week 01 Status (Memory)

**Period:** 2026-06-26 → 2026-07-01
**Phase:** 5 (Memory) — the "it-remembers" MVP arc (`#1 episodic → #2 context → #3 retrieval → #6 cache-growth`)
**Branch:** `master` · **This week's HEAD:** `2650253`
**Author:** JARVIS Development

> First Phase 5 weekly status. It back-fills the cadence that lapsed while G1/G2/G3 were built (the
> per-goal design docs under `phase5/docs/` carried the running detail); from here the weekly cadence
> resumes per the CLAUDE.md rule. Mirrors the Phase 2/4 `weekN/WEEK_N_STATUS.md` pattern.

---

## Summary

Phase 5 stood up (2026-06-26, right after v1.0.0 shipped) and drove the MVP recall arc to near-complete:
**G1 (Episodic Store), G2 (Shared Context Pool), and G3 (Retrieval Before Inference) are all box-verified
on the real Ryzen 7 2700X.** As of this week's close, **G3/M6 (flip retrieval ON by default) is PARKED** pending the offline OFF-vs-ON A/B —
retrieval stays opt-in (default-OFF): it works and is used (`[PROBE] hit=1`), but whether it *helps* is unproven.
The only open MVP leg is **#6 (Cache Growth)**, re-scoped this week to the canon "promote-from-the-log"
design after a first-cut route-through-cache attempt (#6a) was reverted.

## Done this week

### G1 — Episodic Store (KEYSTONE) — M0–M4, box-verified 2026-06-27
- Raw-LBA circular 512 B-record store (`phase3/src/ai/episodic_store.{c,h}`), nvme_log pattern; base LBA
  21,100,000, 8192 entries. `query_key = cache_hash(cache_normalize_query(...))` (decision-cache FNV-1a parity).
- Wired into Process A: `epi_batch_add` per query, `epi_commit` at the `[STATS]` cadence (batched → low write-wear).
- **Reboot survival proven on hardware:** 3-boot hard-power-cycle lineage (335 → +84 = 419 records, `boot_id`→3,
  no clobber), read back from Ubuntu via `dd | parse_episodic.py`.
- `episodic_count` live on the console over the real I211 NIC (`TLM_F_MEMORY`, no packet-size bump).
- Host CI: `test_episodic_store.c` (8/8, incl. `epi_index_lookup`), `test_parse_episodic.py` round-trip.

### G2 — Shared Context Pool — M0–M4, box-verified 2026-06-28
- Page-sized seqlock working-memory pool (`phase3/src/ai/shared_context.{c,h}`): system_state snapshot +
  event ring + recent-decisions keyed ring + a `preamble[1024]` staging buffer, `__atomic_*`/TSan-clean.
- 3rd PA↔PB shared page mapped (`SCTX_VADDR_A/B`); PA publishes per query, PB reads read-only in `handle_query`.
- Telemetry **v2** (200→208 B, CRC@204, `TLM_F_CONTEXT`); on-box `pool_events`/`pool_decisions` == q_hits+q_infer, err=0.
- Host CI: `test_shared_context.c` (40/40, TSan + O2, incl. T6 preamble-handoff).

### G3 — Retrieval Before Inference — M0–M5 box-verified; M6 parked pending A/B
- Scorer + preamble assembler (`phase3/src/ai/g3_retrieval.{c,h}`): exact-key + recency select, `g3_candidate_usable`
  filter (successful INFER records only), `g3_prompt_budget` (cap 160 preamble toks, 48-tok query floor).
- PA packs the preamble into the pool; PB injects it between the Gemma user-turn `\n` and the question
  (`prompt_ids[128]→[256]`, KV stays 512). Box-verified: OFF byte-identical, ON coherent + differs (`e4e4a56`).
- **M4** (2026-06-30): retrieval latency <50 ms (`lat_us`=0/1), **v3** telemetry (208→216, CRC@212,
  `retrieval_hits`/`retrieval_latency_us`, `TLM_F_RETRIEVAL`), console retrieval row, synthetic-fact probe
  (`[PROBE] hit=1` — present-AND-used).
- **M5** (2026-06-30): NVMe-backed post-reboot recall — bounded key→record fetch (`epi_index_lookup` + one
  `epi_store_read`, not an O(N) scan); box-proven `[RECALL] index n=330`, 6 distinct `recall=1`, ~0.5 ms.
- **M6** (2026-07-01, this session): flag flipped `0`→`1` (`d4c58ff`) then **PARKED back to 0** — retrieval stays
  opt-in (default-OFF). Flip-to-production-default is a product call gated on an offline OFF-vs-ON A/B: retrieval
  works + is used (`[PROBE] hit=1`), but M2/M3 showed it broadens answers, so whether it *helps* is unproven.
  Wiring sound (PA pack + PB inject reach it; console honesty gate 40/40). Re-flip only after the A/B.
- Host CI: `test_g3_retrieval.c` (30/30).

### #6 — Cache Growth — re-scoped (plan only)
- First-cut route-through-cache (#6a, `7e8c30f`) **REVERTED** (`2650253`) as wrong design vs canon.
- Canon design doc authored (`phase5/docs/PHASE_5_GOAL6_CACHE_GROWTH.md`): promote repeated `query→action`
  patterns **from the episodic log** into the decision cache; `JARVIS_CACHE_GROWTH` flag retained (default 0).
- Status: 🔜 PLANNED (M0 next).

## Tests / verification
- Host CI green on HEAD `2650253` (run 28446105188, 1m27s): G1 episodic 8/8 + parser round-trip, G2 shared-context
  40/40 (TSan+O2), G3 retrieval 30/30, console honesty 40/40 + key-contract 81 + logic 14 + e2e 20.
- M6 this session: flag flipped then **PARKED back to 0** (opt-in); console honesty gate re-run **40/40**; flag-flip wiring verified by inspection on both processes.
- **Honesty note:** generation never runs in CI, so G3 coherence/latency/recall are box-proven (M2–M5), not CI-proven.

## Next
1. **G3/M6 offline A/B** — run the OFF-vs-ON helpfulness A/B (Claude-judged); re-flip `JARVIS_G3_RETRIEVAL` to the
   shipped default only if it shows retrieval *helps*, not just works.
2. **#6 / M0 (host-first)** — canon promote-from-log frequency scan + light up the dormant SEC-024 LRU eviction
   (host-test that path FIRST — the cache has never filled at ~308/512). Reconcile the `JARVIS_CACHE_GROWTH`
   flag ownership after the #6a revert.
3. Land #6 + box-prove growth/hit-rate → cut the early **`memory` milestone tag** (closes the MVP arc).

## Notes / risks
- Deployed kernel remains `KernelFastpath=ON` + XSAVE/AVX + SMP `NUM_NODES=6` = functional-but-unverified by design.
- All Phase 5 code lives under `phase3/src/ai/` (there is no `phase5/src/` tree); `phase5/` is docs + weeks.

---

## [Appended 2026-07-02/03] G3/M6 SHIPPED — retrieval default-ON (closes "Next" item 1)

The offline A/B arc ran to completion after this week's close and **cleared the M6 gate**:

- **A/B1** (2026-07-01, harness `9ba7007`, OFF=22/ON=10): net-neutral-to-slightly-positive (blind 3-2), but one
  hard **P6** cross-topic contamination failure (recency fallback + embedded prior-question text) → **fixed
  `70ca236`**: `g3_select_exact_only` (recency fallback DROPPED for injection) + `g3_build_preamble_answer_only`
  (fenced, answer-only).
- **A/B2** (on `70ca236`): blind panel **net-POSITIVE (ON 4/7)**, contamination-free — but exposed **P7** self-echo
  (dangling `### W` markdown fragment from mid-token truncation + verbatim lead restatement) → **fixed `2bb537b`**:
  `g3_clean_answer_len` clean-boundary truncation + "use as reference; add new detail, do not repeat" build-on
  label; `test_g3_retrieval.c` 43→**53 PASS** (T10 clean-truncation).
- **A/B3** (2026-07-02, on `2bb537b`, OFF=22/ON=13): **ALL CLEAN** — `### W`=0, `[RETR] hit=1`×14, zero
  contamination, zero label leakage, coherent, err=0. Blind panel (13 pairs × 3 lenses + artifact-hunter +
  repeat-progression analysts): net-neutral-to-slightly-positive, **zero materially-worse cases** — under greedy
  decoding the OFF baseline gives a repeat-asker byte-identical output, so ON ≥ OFF everywhere, with clear ON wins
  where build-on compresses covered framing into new substance. The residual lead-sentence overlap on repeats was
  **proven the model's natural answer** (OFF produces the identical opening with no injection); the
  first-sentence-only excerpt follow-up was evaluated and NOT taken.
- **Ship (2026-07-02):** `JARVIS_G3_RETRIEVAL` `0`→`1` in `jarvis_debug.h` — retrieval is the deployed default.
  **The image is intentionally no longer byte-identical to v1.0.0** (episodic + context pool + retrieval = the
  deployed memory stack); `G3_PROBE`/`G3_AB`/`CACHE_GROWTH`/`BOOT_LOG` stay 0 with their OFF-is-inert guarantee.
  Honesty stance unchanged: hits + latency are the metrics; "memory helped" is never a system claim.
- **Remaining:** G3/M4d — bare-metal boot of the default-ON build (ESP deploy) with live v3 `TLM_F_RETRIEVAL`
  telemetry over the real I211 + durable-log `[RETR]` evidence.

### [Appended 2026-07-03] M4d BOX-VERIFIED LIVE — G3 COMPLETE (M0–M6)

The flipped build (`66e1d18`) was ESP-deployed and booted bare metal (boot_id=11). Live I211 v3
telemetry on the Main-PC receiver: **34 packets `version=3`/216 B crc_ok**, `flags_list` carrying
`RETRIEVAL` (0x80), **`retrieval_hits` climbing 1→2→3**, `retrieval_latency_us` 192→5078 µs,
`q_errors=0`, coherent Gemma, `episodic_count=584`; the console "Retrieval before inference"
Capabilities row renders live. **Goal #3 is COMPLETE (M0–M6) — retrieval is deployed and
box-verified live.** The "it-remembers" MVP arc now has only #6 (cache growth) open.
