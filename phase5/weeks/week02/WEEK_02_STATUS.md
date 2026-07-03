# Phase 5 — Week 02 Status (Memory)

**Period:** 2026-07-02 → 2026-07-03
**Phase:** 5 (Memory) — the "it-remembers" MVP arc (`#1 episodic → #2 context → #3 retrieval → #6 cache-growth`)
**Branch:** `master` · **This week's HEAD:** the #6/M4 docs commit (after `99419fb`)
**Author:** JARVIS Development

---

## Summary

**The it-remembers MVP arc is COMPLETE.** G3 (retrieval) shipped default-ON (2026-07-02, `66e1d18`) and
was box-verified live (boot_id=11); G6 (cache growth) ran M0→M4 in one autonomous session (2026-07-03)
and shipped default-ON (`99419fb`), box-verified live (boot_id=12). The deployed image now intentionally
diverges from v1.0.0 — episodic store + shared context pool + retrieval + cache growth are the deployed
memory stack. The early `memory` milestone tag is **proposed, not created** (user names tags) —
`phase5/docs/GOAL6_FINISH_REPORT.md` §6.

## Done this week

### G3 — Retrieval: M6 SHIPPED + M4d box-verified — GOAL COMPLETE (M0–M6)
- Ship gate = 3× offline OFF-vs-ON A/B: A/B1 exposed P6 cross-topic contamination (fixed `70ca236`,
  exact-key-only + fenced answer-only preamble); A/B2 net-POSITIVE (ON 4/7 blind) but exposed P7
  self-echo (fixed `2bb537b`, clean-boundary truncation + build-on label; tests 53 PASS); A/B3 ALL CLEAN
  (`### W`=0, hit=1 ×14, zero contamination, err=0; blind panel net-neutral-to-slightly-positive, zero
  materially-worse — under greedy decoding the OFF baseline repeats byte-identically, so ON ≥ OFF).
- Flip `66e1d18` (2026-07-02); **M4d live** (boot_id=11): 34 packets v3 crc_ok, `TLM_F_RETRIEVAL`,
  `retrieval_hits` 1→2→3, latency 192→5078 µs, `episodic_count=584`, console row live.

### G6 — Cache Growth: M0→M4 COMPLETE, default-ON (autonomous run 2026-07-03)
- **M1** (`e9ac21d`): bounded promotion pass in PA — Option-B rolling freq aggregate (`cg_freq_bump/get`,
  seeded at the boot recall-scan) + `CG_PROMOTE_HWM=409` cap; box-verified (grow=6, OFF byte-identical).
- **M2** (`cdc1aeb`): `reserved_i`→`cache_growth_count` (no size bump) + `TLM_F_CACHE_GROWTH` 0x100 +
  receiver/fixture/golden lockstep + console "Cache growth — learns frequent queries" row + "Patterns
  promoted" stat + e2e value-pin. Host: 41/41 + 99/99 + 40/40 + 14/14 + 22/22.
- **M3a** (`38e15d2`): gated READ-only `cache_lookup`-before-infer — the two workload lanes ask disjoint
  query sets, so promoted patterns were dead weight until the inference lane consulted the cache. NO
  insert on that path (canon D-a).
- **M3 flip bar 6/6** (S1-snapshot ON/OFF/REF protocol, 1800 s legs): grow 6→9 idempotent;
  **`served=42,404`**, `infer` FROZEN at 17 while **q reached 283,400 err=0** (vs q≈220 OFF — the ×1,300
  multiple is a property of the repeat-heavy deterministic workload, not a universal claim); served text
  = coherent stored answer HEADS (empirical: `episodic_fill` stores the head — the full-answer filter was
  evaluated and NOT needed); `used` max 261 < 409 (LRU never fires; stays host-proven 10/10 +
  unreachable-by-design); OFF byte-identical to the pre-M3a baseline (23/23 INFER, 1 known
  serial-interleave artifact). **Flip `99419fb`**; flip smoke: cache-growth + retrieval co-live, err=0.
- **M3d deploy**: ESP-deployed (checksum-pinned `5de620b6…`), one-shot boot → **live confirm boot_id=12**:
  `flags_list` carries `CACHE_GROWTH`, `cache_growth_count=9`, `retrieval_hits=16`, q=30,700 with
  `q_infer` frozen at 16 and `err=0` at ~10 min uptime, crc_ok on all 333 captured records.
- **M4**: docs + `GOAL6_FINISH_REPORT.md` (verbatim evidence, deviations, gotchas) + the `memory` tag
  proposal.

## Tests / verification
- CI green on every push (M2 `cdc1aeb`, M3a `38e15d2`, flip `99419fb` — runs 28647628134, 28647753543,
  28653472883). Host suites: cache_growth 22, telemetry C 41, receiver 99, honesty 40, logic 14, e2e 22.
- **Honesty note:** generation never runs in CI; growth/serve/hit-rate are box-proven. The durable
  NVMe-log read for boot_id=12 awaits the owner's power-cycle back to Ubuntu.

## Next
1. User: power-cycle the box (returns to Ubuntu automatically), optionally read the durable log for the
   boot_id=12 record; name/create the `memory` tag if the proposal is accepted.
2. Arc 2: #4 semantic memory (Gemma-distilled facts/preferences), #5, #7 — plan docs first.
3. Backlog (ROADMAP): B1 self-healing, B2 "it-acts" keystone (SEC-039 closure), B3 CI generation smoke.

## Notes / risks
- Deployed kernel remains `KernelFastpath=ON` + XSAVE/AVX + SMP `NUM_NODES=6` (functional-but-unverified
  by design); the image now also intentionally diverges from v1.0.0 (memory stack default-ON).
- A cache-served answer is a stored response head served verbatim — fast but frozen (a fresh G3-injected
  inference may build on prior answers). Designed trade for *frequent* repeats; never an "understands"
  claim.
