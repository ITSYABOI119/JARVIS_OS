# Phase 5: Memory — Plan

**Status:** IN PROGRESS (started 2026-06-26). Plan committed; no code yet.
**Prerequisite:** Phase 4 (Production / v1.0) — engineering complete, `v1.0.0` release in prep. Phase 5 builds on the deployed bare-metal seL4 x86-64 stack (Process A rootserver + Process B quantized engine, shared-memory IPC, NVMe runtime model load, the rolling `nvme_log` raw-sector telemetry log, the decision cache, and the read-only Remote Telemetry Console).
**Estimated effort:** 4–8 months.
**Sources:** `phase4/docs/ROADMAP.md` §Phase 5 (the locked goals + done-when, canon); the on-box NVMe partition map read read-only via `ssh jarvis` (2026-06-26); the proven `nvme_log.{c,h}` raw-LBA pattern. **Where a recon claim conflicts with the box read, the box read wins** (the building-blocks recon claimed free space was scarce; the direct read disproved it — see §5).

> **Mission (canon, `ROADMAP.md:54`):** JARVIS stops forgetting — it remembers **what happened**, **what you prefer**, and **what failed**, **without slowing down routine requests**.

---

## 1. Strategy — keystone-first, shipped as an "it-remembers" MVP arc

Phase 5 is sequenced **keystone-first**: goal #1 (the episodic store on NVMe) is the keystone every other goal reads from — retrieval reads it, semantic memory distills from it, cache growth promotes from it, consolidation compacts it. So #1 ships first, then the goals that turn a raw log into a system that visibly *remembers*.

It is delivered in two arcs:

- **MVP arc — "it remembers" (#1 → #2 → #3 → #6):** an episodic store that survives reboot, a shared context pool, retrieval-before-inference that injects relevant past entries into Process B's prompt, and automatic promotion of repeated query→action patterns into the decision cache. This arc alone satisfies the headline done-when (recall ≥10 preferences after reboot; cache hit-rate improvement) and earns an early **"memory" milestone tag**.
- **Arc 2 — distill & learn (#4 → #5 → #7):** distilled semantic memory, persisted SHIELD failure-learning (monitor-only — see §7), and a low-priority consolidation job.

This mirrors Phase 4's pattern: a keystone (there, inference perf; here, the episodic store) shipped first, then the goals that make it user-visible (there, the HUD + console; here, retrieval + cache growth).

---

## 2. Goals (canon, `ROADMAP.md:56-64`)

1. **Episodic store** ⭐ KEYSTONE — Structured interaction log on NVMe: timestamp, query, action, outcome, optional feedback. Survives reboot.
2. **Shared context pool (C)** — Port the working-memory layer to bare metal: system state, event stream, recent decisions. All agents/processes read without serialization.
3. **Retrieval before inference** — Process A retrieves relevant episodic + semantic entries and injects them into Process B's context before generation.
4. **Semantic memory** — Distilled facts and preferences (e.g. "prefers briefings at 7am", "do not interrupt during deep work"). Stored separately from the raw episodic log.
5. **SHIELD learning on bare metal** — Port failure-learning from the Phase 1 Python: failed actions increase risk score, persisted on NVMe (monitor-only — see §7.3).
6. **Cache growth** — Promote repeated query→action patterns from the episodic log into the decision cache automatically.
7. **Consolidation job** — Low-priority offline process: compact episodic → semantic, prune stale entries, promote hot patterns to cache.

---

## 3. Done when (canon, `ROADMAP.md:68-72`, verbatim)

- [ ] Reboot → JARVIS recalls at least 10 stored preferences without re-prompting
- [ ] Repeated harmful action is blocked faster on second attempt (SHIELD learning verified)
- [ ] Cache hit rate improves measurably after 1 week of use (target: >90%)
- [ ] Episodic log readable and parseable (`parse_*` tool or equivalent)
- [ ] Inference latency for cache hits still <1 ms; retrieval adds <50 ms to cache misses

The MVP arc (#1/#2/#3/#6) satisfies criteria 1, 3, 4, and the <50 ms half of 5; arc 2 (#5) satisfies criterion 2.

---

## 4. Architecture & the shared store template

Memory reuses what the deployed system already proves, rather than inventing new subsystems:

- **Storage template = the `nvme_log` raw-LBA circular store.** Each memory store is a clone of the proven `nvme_log.{c,h}` pattern: a raw-LBA region with a 64-byte header (magic, version, `cursor`, `total_entries`, `boot_id`, XOR checksum) + fixed **512-byte records**, written via the existing `nvme_write_sectors` path, **flushed after every write**, **circular** (keeps the latest, never fills). This is a Process-A-side facility (PB has no allocator and no direct device access) — the same side the telemetry log lives on. No FAT32 writes (the `fat32` driver is read-only), no new device driver.
- **Context budget is hard and load-bearing.** Retrieval must fit the existing wire/engine envelope: the shmem IPC payload is **240 B**, Process B's prompt buffer is **`prompt_ids[128]`**, and the KV window is **512 tokens**. Retrieval is a *preamble* (a handful of compact, retrieved facts prepended to the prompt), not unbounded context — this constraint drives decision §7.4.
- **Honesty / UI-feature parity (CLAUDE.md rule).** Every memory layer that ships must surface a **real, live** signal on the read-only Remote Telemetry Console (or its auto-populated Capabilities section), never a hardcoded claim. A memory feature that exists but isn't on the console is a gap; anything on the console without a live source is fiction to remove. Memory metrics ride a deliberate telemetry **v2** bump (see §7.5), not smuggled into reserved bytes.

---

## 5. NVMe memory-region map + storage decision (VERIFIED on the box, 2026-06-26)

Disk = **4,000,797,360 sectors** (Lexar NM790 2 TB). Read read-only via `ssh jarvis` (no writes):

```
p1  MSR           34            – 32,767
p2  JARVIS_DATA   32,768        – 21,094,399        (10 GiB — the Gemma model partition, LBA 32768)
    >>> FREE GAP  21,094,400    – 3,581,364,223     = 3,560,269,824 sectors ≈ 1.66 TiB  <<<
p3  ext4/Ubuntu   3,581,364,224 – 3,997,272,063     (198 GiB — the build/ssh host OS)
p4  ESP           3,997,272,064 – 4,000,794,623     (1.7 GiB — dual-boot ESP, JARVIS + Ubuntu)
    telemetry log @ 4,000,794,624 (tail, full — the rolling nvme_log region)
```

Ubuntu sits at the **far end** of the disk, so there is a **~1.66 TiB contiguous unpartitioned gap immediately after `JARVIS_DATA`**. (This directly disproves the building-blocks recon's "free space is scarce" — that conflated the full ESP/tail with the whole disk.)

**Storage decision (grounded in the read):** carve a **raw-LBA MEMORY region near the start of the gap** — proposed **base ≈ sector 21,100,000, reserve ~8 GiB** (≈ 16,777,216 sectors → ends ≈ 37,877,215). This leaves comfortable margin after `JARVIS_DATA` (p2 ends at 21,094,399) and sits **~3.56 billion sectors away** from Ubuntu (p3) and the tail telemetry log — zero collision risk. Per-store sub-regions (episodic / semantic / SHIELD-state / consolidation scratch) are carved **within** this 8 GiB and defined in the per-goal docs (episodic: `phase5/docs/PHASE_5_GOAL1_EPISODIC_STORE.md`).

**Why raw-LBA, not a partition:** it reuses the proven `nvme_log` precedent (already trusted for durable telemetry across reboots), needs **no repartition** and **no FAT32-write driver**, and is Process-A-side. The region is **documented + reserved** (like `nvme_log.h` documents its tail LBA) so a future installer/repartition can't overlap it. It can be **promoted to a real `JARVIS_MEM` partition later** if we ever want the cleanliness — a cheap migration, not needed for the MVP.

---

## 6. Build order (keystone-first; ROADMAP goal #s in parens)

| Order | Goal | Arc | Depends on | Doable now (host/CI) vs box |
|-------|------|-----|------------|------------------------------|
| MVP-1 | **#1 Episodic store on NVMe** ⭐ KEYSTONE | MVP | — | schema / keying / serialize / circular logic / parser → **host+CI now**; reboot-survival → **box** |
| MVP-2 | **#2 Shared context pool (C)** | MVP | #1 | struct + lock-free read port → **host now**; live wiring → **box** |
| MVP-3 | **#3 Retrieval before inference** | MVP | #1, #2 | keyword + recency + FNV-1a retrieval + preamble pack → **host+CI now**; prompt inject in PB → **box/KVM** |
| MVP-4 | **#6 Cache growth** | MVP | #1 | promotion logic (the dormant SEC-024 LRU goes **live** — host-test first) → **host+CI now**; box |
| — | **"it-remembers" MVP / `memory` tag** | — | MVP-1..4 | the early milestone tag once #1/#2/#3/#6 are green |
| A2-1 | **#4 Semantic memory** | Arc 2 | #1, #7 | deterministic distill rules + separate store → **host+CI now**; box |
| A2-2 | **#5 SHIELD learning (monitor + persist)** | Arc 2 | #1 | risk-score raise/persist logic → **host+CI now**; box (criterion 2 proof) |
| A2-3 | **#7 Consolidation job** | Arc 2 | #1, #4, #6 | deterministic compaction/prune/promote → **host+CI now**; low-prio box job |

Like Phase 4, most logic is host/CI-testable up front; only reboot-survival, live wiring, and on-box latency need the JARVIS PC.

---

## 7. Locked technical decisions

1. **Storage = a carved raw-LBA region** (per §5). FAT32 is read-only; there is no file-backed store.
2. **Retrieval = keyword + recency + FNV-1a exact-match; NO embeddings.** Associative / embedding-based retrieval ("Instinct") is explicitly **Phase 7 #1**, not Phase 5. Phase 5 retrieval is cheap, deterministic, and fits the <50 ms bar.
3. **SHIELD = learn + persist + MONITOR only.** Failed actions raise a persisted risk score, **monotonic-raise-only**, with an **immutable blocklist**. Live *enforcement* (closing SEC-039) stays **Phase 6 #5**, gated on the full inbound-control security checklist (auth + HMAC, rate-limit, hardened/fuzzed parser, less-privileged input process). Phase 5 proves the *learning* signal, not a live blocker.
4. **Context = retrieved-preamble only; NO multi-turn KV continuity.** The 512-token KV window is too tight for conversational continuity and KV does not survive reboot — so memory is delivered as a compact retrieved preamble prepended to the prompt, not as persistent KV state. (Multi-turn conversation referencing prior sessions is a **Phase 6** done-when.)
5. **Telemetry = one deliberate `telemetry_packet_t` v2 bump** for memory metrics (episodic count, retrieval hit/latency, SHIELD-learn count, cache-growth count) — done as **one** explicit slice with the golden fixtures + key-contract + honesty tests + console updated **together**, not smuggled into reserved bytes. (Phase 4 set the precedent that a wire change is one deliberate, fixture-synced slice.)
6. **Consolidation = deterministic first; LLM distillation gated / Phase 7.** The consolidation job (compact, prune, promote) is rule-based and deterministic for v1 of Phase 5; using the LLM itself to distill semantic memory is gated behind correctness review and leans toward Phase 7.

---

## 8. Risks

- **NVMe write-wear.** Per-query episodic writes would burn the SSD; **batch** episodic records and commit at consolidation / idle, not per query (the measured ~3k queries/day rate makes batched commits cheap). Mirrors the `nvme_log` cadence discipline.
- **The dormant SEC-024 LRU goes live.** Cache growth (#6) makes the decision-cache eviction path — implemented but currently unreached (the cache never fills at ~258/512) — actually execute. **Host-test the eviction path before relying on it.**
- **Retrieval latency vs the <50 ms bar.** Keyword + recency + FNV-1a over a batched in-RAM index must stay under 50 ms on a cache miss; measure on-box, keep the index small and the preamble bounded.
- **The v2 wire bump churns golden fixtures.** The `telemetry_packet_t` v2 change touches `golden_telemetry.json` / `golden.pcap` / the key-contract + honesty tests + the console — done as **one** deliberate, fixture-synced slice (decision §7.5), never piecemeal.
- **Raw-LBA region must be documented + reserved.** The region (§5) must be recorded like `nvme_log.h` records its tail LBA, so a future installer or repartition cannot overlap it. Promotion to a `JARVIS_MEM` partition is the clean long-term option.

---

## 9. Boundary — what is NOT Phase 5

- **Embedding / associative ("Instinct") retrieval** → **Phase 7 #1** (Hopfield/associative memory). Phase 5 is exact-match only.
- **Live SHIELD blocking + control-IN (two-way console)** → **Phase 6 #5**, behind the security checklist (`docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md`). Phase 5 SHIELD is monitor + persist only.
- **GPU / larger models** → **Phase 7 #4**. Phase 5 runs on the same CPU Gemma 4 E2B deployed in Phase 4.
- **Multi-turn conversational continuity referencing prior sessions** → **Phase 6** done-when. Phase 5 delivers retrieved-preamble memory, not persistent dialogue state.

---

*Phase 5 cadence: weekly status at `phase5/weeks/weekN/WEEK_N_STATUS.md` (resumes the Phase 2/4 weekly pattern). Keystone goal detail: `phase5/docs/PHASE_5_GOAL1_EPISODIC_STORE.md`. This plan mirrors `phase4/docs/ROADMAP.md` + `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md`.*
