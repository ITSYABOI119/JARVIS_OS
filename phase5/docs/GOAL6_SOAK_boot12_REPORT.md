# Forensic Report — boot_id=12 JARVIS run (first cache-growth-ON soak, ~874K queries)

**Date:** 2026-07-03 · **Scope:** READ-ONLY analysis of the persisted stores after the boot_id=12
bare-metal run — the FIRST large-scale run with `JARVIS_CACHE_GROWTH` default-ON (build `99419fb`,
ESP image md5 `5de620b6…`, one-shot boot 2026-07-03).
**Evidence sources:** (1) the durable NVMe telemetry log (`dd skip=4000794624 count=2701 |
parse_nvme_log.py`), (2) the episodic store (`dd skip=21100000 count=8193 | parse_episodic.py`),
(3) a 333-record live `--json` telemetry capture taken over the I211 during the run (seq 293–625),
plus the strategist's live point-samples. No on-box store was modified; no reboot issued during
the analysis.

**Evidence FROZEN (race guard, before any boot_id=13 run):** both regions were dumped to on-box
files — both stores are circular and a cache-growth-ON boot_id=13 run would overwrite this tail
within minutes. Frozen 2026-07-03 21:22, verified byte-sized and parsing to the identical headers
analyzed here:
- `/home/jarvis/boot12_nvmelog.bin` — 1,382,912 B (2701×512), md5 `33ef7acddc4a0ecc3ccfb3a6491e398e`
- `/home/jarvis/boot12_episodic.bin` — 4,194,816 B (8193×512), md5 `8d1a8cf339b6ab24eee2a910552fa495`
Re-parse anytime, reboot-independent: `python3 …/parse_nvme_log.py < boot12_nvmelog.bin` /
`python3 …/parse_episodic.py < boot12_episodic.bin`.

---

## HEALTH VERDICT: ✅ CLEAN — err=0 across the entire visible record at ~874K queries; zero faults; all counters sane; promoted-serve output coherent; the 85% hit-rate target held at scale.

**One observation, called out prominently (not a defect):** the durable log's final entry is
`[12:20652] IPC_STATS [SNAP] T+2092603 … q=873700 err=0` — logging stops **mid-stride at a
perfectly steady cadence** (last inter-entry deltas ≈205 ms per 100 q ≈ 488 q/s, unchanged to the
final line), ~34.9 minutes into the boot. This is the signature of an **external stop (the owner's
power-cycle)**, not a degradation: no error, fault, slowdown, or cadence stretch precedes it. The
strategist's live "~864K" sample sits ~2 minutes before this endpoint, consistent with sampling
live then cycling the box. A silent PA hang at exactly that instant cannot be *excluded* from the
stores alone (a PA fault prints to serial, not the log) — but nothing in the evidence suggests one.

---

## 0. Build confirmation — boot_id=12 IS the cache-growth-ON build (from evidence)

- Live capture (all 333 records, `crc_ok=True`, `version=3` = the pre-v4 `99419fb` wire):
  `flags_list` carries **CACHE_GROWTH** on every record; `cache_growth_count` **6 → 9** climbing
  DURING the run (first record seq=293: `6`; last seq=625: `9`) — growth happened live, on-box.
- Episodic tail: **1,304 promoted-SERVE records** (CACHE-action records answering
  inference-lane questions with stored LLM text) — impossible without the M3a serve path.
- `q_infer` frozen (14 → 16 across 30K queries in the capture; `last=` frozen for the final
  ~135K queries in the log) — the promoted-conversion signature.

## 1. HEALTH

- **err:** every one of the 2,700 rolling durable-log entries reads `err=0`; the live capture's
  `q_errors=0` throughout. Quoted final: `[12:20652] … q=873700 err=0`.
- **Faults:** 0 lines matching fault/halt/assert in the log window.
- **Boot constancy:** all 2,700 log entries are `[12:…]`; log seq runs 17,953→20,652 contiguously;
  `T+` and `q=` strictly monotonic → **no silent reboot**.
- **Self-test:** the boot-time `LOG_SELFTEST` entry has rotated out of the 2,700-entry window
  (it was seq≈1; the window starts at 17,953). The live capture shows `selftest_score=5` on every
  record (the honest real tally; `TLM_F_SELFTEST_PASS` set). Limitation noted in §9.

## 2. CACHE GROWTH at scale

- `cache_growth_count` 6→9 live (all 9 = the distinct promotable inference queries; matches the
  QEMU flip-bar proof exactly). No further growth after all keys promoted — idempotence held for
  the rest of the run (count stable at 9 through the strategist's later samples).
- Growth ceiling honored: with 9 promotions the cache sits ~261/512, far under `hwm=409` — the
  SEC-024 LRU can never fire from this workload (by design; host-proven 10/10 separately).

## 3. PROMOTED-SERVE COHERENCE (the just-shipped path, at scale)

- The episodic tail holds **1,304 serve records spanning ALL 8 inference-lane queries**
  (150–186 each — even coverage): microkernel 186, seL4 165, page fault 163, virtual memory 161,
  TCP 161, drivers 159, scheduler 159, AI 150.
- Verbatim served text (quoted from the store) is **coherent stored answer heads**, e.g.:
  - `q="The seL4 microkernel is" r="The seL4 microkernel is a **formally verified, high-assurance
    microkernel**. Here's a breakdown of what that means and why it's significant: * **Formally
    Verified:** This is the cornerstone of seL"`
  - `q="What is a page fault and how is it handled" r="A page fault is a crucial concept in
    **virtual memory management** … not loaded into physical RAM** (main memory), but"`
- **Zero `EPI_OUT_ERROR` / `EPI_OUT_BLOCKED` records** in the 8,192-record tail — every visible
  record is `CACHE/OK` (all-hit steady state; no miss-that-should-have-hit).

## 4. RETRIEVAL (G3)

- Live capture: `retrieval_hits` 15→16 during the early-run window (retrieval fired on real
  inferences; `retrieval_latency_us` 5,075→67 µs — sane). `TLM_F_RETRIEVAL` set on every record.
- No `[RETR]`/`recall=` lines persist in the durable log (serial-only prints; `BOOT_LOG=0` by
  design) and no INFER records survive in the episodic tail (all inference happened in the first
  minutes, ~90 wraps ago) — retrieval evidence for this run is therefore the live telemetry.

## 5. EPISODIC DURABILITY

- Header: `Total: 743,926` lifetime records, checksum `0x4A4E104E (OK)`; 8,192/8,192 records
  decoded, **0 malformed**, seq contiguous 735,734→743,925.
- This run wrote ≈**743,342 records** (lifetime 743,926 − 584 pre-run) ≈ **90.7 full wraps** of
  the 8,192-slot ring — the circular store behaved perfectly at ~2 orders of magnitude beyond any
  prior run.
- Note: the episodic header's own boot counter reads **6** (it is independent of nvme_log's 12 —
  the episodic store was first initialized 2026-06-27, six JARVIS boots ago). All 8,192 tail
  records carry epi-boot=6 == this run; **0 carried-over records remain visible** (older boots'
  records were overwritten by the ~90 wraps). The boot_id distribution is therefore: 8,192×boot-6
  (=nvme boot 12), 0×earlier.

## 6. CONTEXT POOL (G2)

- `pool_events`/`pool_decisions` **145 → 26,041**, strictly monotonic across all 333 capture
  records, tracking q_hits+q_infer exactly. `TLM_F_CONTEXT` set throughout.

## 7. COUNTER SANITY AT ~874K QUERIES

| Counter | Behavior | Verdict |
|---|---|---|
| `q_total` (u64) | 178 → 873,700, strictly monotonic | ✅ |
| `q_hits` | 743,259 final; window hit-rate **85.14%**, lifetime 85.08% — exactly the workload's design mix (70% cache lane + 15% served inference lane) and **over the >80% ROADMAP target at scale** | ✅ |
| `uptime_ms` (u32) | 2,092,603 ms ≪ wrap; monotonic | ✅ |
| `log_cursor` | constant 2700 = rolling-full cap (by design since the log first filled) | ✅ |
| durable-log seq | 17,953→20,652 contiguous; lifetime 20,653 | ✅ |
| episodic seq | contiguous through ~90 wraps | ✅ |
| `infer_duty_pct` | 95→83 and decaying (inference stopped; duty = inference cycles/uptime) | ✅ expected |
| `cache_growth_count` | 6→9 then stable | ✅ |
| rate | 489.8 q/s sustained over the final 135K queries | ✅ |

Nothing negative, reset, or wrapped incorrectly.

## 8. ANOMALIES

- **None found** beyond the §HEALTH observation (clean mid-stride log cessation ≈ power-cycle).
- The frozen `last="The fundamental difference between **NVMe (Non-…"` across all 1,350 visible
  [SNAP]s is the designed all-promoted end-state (no new inference → no new last_text), not a hang:
  q/hits kept advancing ~490 q/s beneath it.

## 9. Honest limitations

- Both stores are rolling: the durable log shows only the final 2,700 entries (q 738,700→873,700);
  the episodic tail is the last 8,192 of ~743K records. The first ~97% of the run's raw timeline is
  summarized only by the monotonic counters it left behind (which are self-consistent end to end).
- The `LOG_SELFTEST` "3 real, 2 vacuous" line rotated out of the log window; self-test evidence for
  this run is the live `selftest_score=5` telemetry.
- `[CACHE-GROW]`/`[CACHE-SERVE]`/`[RETR]`/`[TOKS]` are serial-only (BOOT_LOG=0 deploy) — per-event
  serve evidence at scale is the episodic store (§3) + live counters, not serial capture.
- The run's absolute wall-clock start/stop and the reason logging ceased are inferred (§HEALTH),
  not directly recorded.

## 10. Two-run comparison — boot_id=13 (the v4 live-tok/s build, optional addendum)

The boot_id=13 tails were frozen the same way (2026-07-03 22:21: `/home/jarvis/cur_nvmelog.bin`
md5 `14a22851380553398fb9d193b84ddeb7`, `/home/jarvis/cur_episodic.bin` md5
`78d45aaadbbfb211a61f6234c5cc2e00`). Provenance note: the boot-12 analysis above was re-run from
the frozen `boot12_*.bin` dumps and diffs **byte-identical** to the pre-freeze device reads.

| Metric | boot_id=12 (`99419fb`, v3 wire) | boot_id=13 (`9671226`, **v4 wire**) |
|---|---|---|
| Final logged q / duration | 873,700 @ T+34.9 min | 560,500 @ T+23.1 min |
| Sustained rate (window) | 489.8 q/s | **490.7 q/s** (v4 timing added no drag) |
| Window hit-rate | 85.14% | 84.93% |
| err / faults (visible window) | 0 / 0 | 0 / 0 |
| Episodic records written | ≈743,342 (~90.7 wraps) | ≈476,567 (~58.2 wraps; lifetime 1,220,493, checksum OK) |
| Episodic tail (8,192) | all epi-boot 6, 8,192× CACHE/OK | all epi-boot 7, 8,192× CACHE/OK |
| Promoted-serve records in tail | 1,304 (all 8 queries) | 1,237 (all 8 queries) |
| Non-OK / malformed | 0 / 0 | 0 / 0 |
| Growth | `cache_growth_count` 6→9 | 0→9 (fresh boot re-promotes; live capture) |
| New in v4 (live capture, 521/521 crc_ok) | — | `version=4`, `infer_last_tok_x100` **551–555 = 5.51–5.55 tok/s measured live** (≈1.6% from the 5.46 benchmark), `infer_gen_tokens=50` real, latch+idle lifecycle on-wire |

**Verdict: the v4 wire bump + always-on generation timing changed nothing operationally** — the
second cache-growth-ON soak reproduces the first's health profile within 0.2% on rate and
hit-rate, at zero errors, while adding the real measured throughput field.

## Follow-ups

1. None required for #6 — the deployed path behaved exactly as the flip bar predicted, at 3×
   the scale of the QEMU proof and ~30× its query count.
2. (Optional, future) a durable `LOG_CACHE_GROW` entry at the [STATS] NVMe cadence would let a
   forensic pass see growth/serve counters without live capture — worth considering if longer
   unattended soaks become routine. (Wire/log-format change; not urgent.)
3. The v4 live-tok/s build (`80103cb`, landed after this run) awaits its box QEMU smoke + deploy —
   boot_id=13 will carry `version=4` and real `[TOKS]`/throughput telemetry.
