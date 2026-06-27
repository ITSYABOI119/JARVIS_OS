# Phase 5 — Goal #3: Retrieval Before Inference

**Status:** 🔜 PLANNED (M0 next). No code yet.
**Date:** 2026-06-28
**Prereqs:** G1 episodic store ✅ (M0–M4, box-verified) + G2 shared context pool ✅ (M0–M4, box-verified). G3 consumes both: G2's in-RAM keyed index (`sctx_lookup_key`/`sctx_recent`) and G1's durable text records.
**Scope:** ROADMAP goal #3 — Process A retrieves relevant past entries and injects them into Process B's prompt **before** generation. This is the consumer that finally *uses* the memory G1/G2 built; it closes the "it-remembers" MVP arc's recall half.
**Sources:** `phase4/docs/ROADMAP.md:62` + `phase5/docs/PHASE_5_PLAN.md:29` (canon); `phase5/docs/PHASE_5_GOAL2_SHARED_CONTEXT_POOL.md` (the pool + read path this builds on, esp. M3); `phase3/src/sel4/inference_server.c` `handle_query` (the one injection site); `phase3/src/ai/{shared_context,episodic_store,decision_cache}.{c,h}`.

> **Goal (canon, `ROADMAP.md:62` / `PHASE_5_PLAN.md:29`):** Retrieval before inference — Process A retrieves relevant episodic + semantic entries and injects them into Process B's context before generation.

---

## 1. Scope + done-when

- **MVP-arc G3 is EPISODIC-ONLY.** The canon's "**+ semantic** entries" is **#4 (Arc 2)** — not built; treat it as **aspirational** here. G3 retrieves from the episodic store + the live pool only.
- **Closes two ROADMAP done-when criteria** (`ROADMAP.md:68-72`):
  - **#1 (reboot → recalls ≥10 stored preferences)** — *jointly with G1*. G1 did only the **storage** half (records survive a power-cycle); G3 is the **recall** half (read them back and put them in front of the model). Neither alone closes it.
  - **#5 (retrieval adds <50 ms on cache MISSES)** — the cache-**hit** <1 ms path must stay **untouched**, so **retrieval runs on cache misses only** (a hit is answered in PA and never reaches PB; see §3).
- **Boundary:**

| Concern | Owner |
|---|---|
| The pool container/index/plumbing + the cross-process read path | **#2 (DONE, box-verified)** |
| Scoring (which records are relevant) + preamble assembly + **injection** into PB's prompt | **#3 (this goal)** |
| Promote repeated query→action patterns into the decision cache | **#6 (cache growth)** — a **SIBLING**: it depends on **#1 only, NOT #3** (don't sequence it behind retrieval) |
| Distilled facts/preferences ("+ semantic") | **#4 (Arc 2)** — not built |

---

## 2. What G3 reads + the realistic scorer

Three stores, all keyed on `cache_hash(cache_normalize_query(query))` (decision-cache FNV-1a — the Phase-1/2/3 normalize-identically rule):

- **The live pool** (G2, in-RAM, `g_sctx_pb`): exact-key (`sctx_lookup_key`) + recency (`sctx_recent`) — **key + recency only, NO text**. ~ns reads, the <50 ms-friendly path.
- **The episodic store** (G1, NVMe): the real **text** (`query[200]`/`resp[256]`) — but **logical-index-only** (`epi_store_read(i)`), **no key→record map**, ~1 NVMe sector per call.
- **The decision cache** (PA-only): the hit/action source; G3 fires only when the cache *misses*.

**Only exact-key + recency exist today** — "keyword" retrieval (canon language) has **no index** and is **net-new** (a future scorer upgrade, not MVP). **Realistic MVP scorer:** `key → sctx_lookup_key` (exact hit) → `sctx_recent(N)` (recency fallback on miss).

**Two gaps to design around:**
1. **The TEXT gap.** The pool has key+recency but **no text**; G1's `g_epi_batch` RAM text is **wiped ~every 100 queries** at the `[STATS]` commit; older / post-reboot text lives only on NVMe (logical-index-only). So MVP injection text comes from the in-RAM batch when warm, and **M5** adds a *bounded* key→record NVMe fetch (NOT a scan) for post-reboot recall.
2. **The dead `committed` flag** (G2 records it **always 0** — D4 marks every pool decision uncommitted-batch). Harmless for MVP; a real committed/uncommitted split is future work.

**NUL-termination:** episodic `query`/`resp` are **NOT** NUL-terminated — always copy by `query_len`/`resp_len`, never `strlen`.

---

## 3. Injection mechanism

- **ONE site:** `inference_server.c` `handle_query` (cache **hits** are answered in PA and never reach PB, so injection is inherently cache-miss-only — §1 #5). The preamble is tokenized **between the Gemma user-turn `\n` and the question** (currently ~`:163`→`:182`, recon at implementation — line numbers drift):
  `<bos> <|turn> user \n {PREAMBLE} {QUERY} <turn|> \n <|turn> model \n <|think|>`. **No system role** (Gemma 4 has none) — the preamble rides inside the user turn, before the question, delimited.
- **Transport (D-b/D2/D6):** PA packs the assembled preamble blob into the pool's dormant `preamble[1024]` staging buffer (a new `sctx_pack_preamble()`, single-writer **release-store of `preamble_len` last**); PB reads it from `g_sctx_pb` and **tokenizes** it on the heap (the same path the query encode uses). The **240 B `MSG_QUERY` slot is untouched** — the preamble never rides the ring.
- **Token budget (D-c):** grow `prompt_ids[128]`→`[256]` (the `128 - n_prompt - 6` budget self-adjusts as preamble tokens consume `n_prompt`); cap a `PREAMBLE_TOK_MAX` so the preamble can't starve the query. Worst case ~296 < **512 KV** — **do NOT raise the 512 KV cap** (it sizes the KV cache + RoPE tables).
- **On/off (D-e):** compile-time `JARVIS_G3_RETRIEVAL` (`jarvis_debug.h`, **default OFF**) + a runtime `preamble_len == 0` skip. Flip the default ON only after the box dual-proof (M3).

---

## 4. The verification model — THE shift (read this before writing tests)

G1/G2 leaned on **byte-identical generation** as the safety net. **G3 breaks that on purpose:** injection changes the prompt, so ON-path output *differs by design*. And **generation runs ONLY on the box/KVM, never in CI**. So:

> **"Byte-identical generation" is a BOX gate, never a CI claim.** Any doc/PR that asserts byte-identical generation must scope it to the box.

- **Layer A (HOST/CI — deterministic, NO model):**
  - the **scorer** (known records → deterministic selection),
  - the preamble **ASSEMBLY** string (byte-exact: truncation, `query_len`/`resp_len`-honoring, delimiters),
  - the prompt **STRUCTURE** via a **stub tokenizer** (OFF → identical token sequence to today; ON → preamble tokens land between the user `\n` and the query).
  - **NOT host-testable:** real Gemma BPE ids / decoded text (no committed vocab fixture) → **model-gated SKIP with `::warning::`**.
- **Layer B (BOX — with the model):**
  - **OFF = byte-identical to the recorded pre-G3 baseline** (the safety net still exists, just box-side).
  - **ON =** the **real preamble bytes in the real prompt** — a `[RETR]` log line + the `[PB]` token dump is the **honest center of proof**; the retrieval **hit fired** + **latency <50 ms**; output is **coherent AND differs** from OFF (assert `is_coherent` / `no_excessive_repeats`, **NEVER an exact output string**).
  - **Dual proof = a two-boot log diff:** the OFF-boot log == baseline; the ON-boot log differs **only where `[RETR] hit=1`**.
- **Reproducibility hazard NEW to G3:** the preamble depends on **reboot-surviving circular-store state**, so reproducible box tests MUST **control store state** — zero the EPI region or pre-seed a known record set before the run.
- **THE HONEST CLAIM:** green tests prove **assembled-from-real-records + present-in-the-prompt + hit + <50 ms + coherent-and-differs**. They do **NOT** prove the model *used* it. Honest ceiling = **one controlled synthetic-fact probe** (a coined token surfaces ON, not OFF, under a controlled store). "Did it help?" = **owner-run Claude A/B offline, explicitly NOT a gate.** **UI corollary:** show hit / latency / facts-retrieved / preamble-present (all real); **NEVER "memory used" / "memory helped".**

---

## 5. Locked decisions (Decision + Rationale)

**D-a — Telemetry: a deliberate v3 size-bump** carrying `retrieval_hit` + `retrieval_latency_ms`, **co-landing #6's `cache_growth_count`** in the **same** fixture-synced slice (honors `PHASE_5_PLAN.md` §7.5 — one deliberate wire change, not piecemeal). *Rationale:* batches the next two memory features' metrics into one golden/key-contract/console churn. *(Revisitable at M4: the alt is reuse the last spare `reserved_i` (u16) for one metric with no size bump — cheaper if only one metric is ready.)*

**D-b — PA packs the preamble; PB reads + tokenizes.** A new `sctx_pack_preamble()` writes the blob to `preamble[1024]` with the `preamble_len` **release-store last**; PB reads via `g_sctx_pb`. *Rationale:* PB has no text source and no device/allocator; PA owns assembly (same side as G1's writes), and the release-store-last makes the reader see a complete blob or none.

**D-c — `prompt_ids[128]`→`[256]`; KV stays 512.** *Rationale:* the preamble needs head-room beyond the ~118-token query budget; 256 covers preamble+query worst-case (~296 < 512) without touching the KV/RoPE sizing.

**D-d — 1–2 truncated `query→resp` pairs**, with the pair count + per-field truncation a tunable `#define`. *Rationale:* keeps the preamble small (latency + token budget) while proving the mechanism; tunable so M4 can measure the latency/quality trade.

**D-e — compile-time `JARVIS_G3_RETRIEVAL` default OFF + runtime `preamble_len == 0` skip;** flip the default ON only after the box dual-proof (M3). *Rationale:* injection changes output — keep it dark until the box proves OFF==baseline and ON is coherent.

**D-f — automated tests assert mechanical facts only** (assembled / present-in-prompt / hit / latency / coherent-differs) **+ ONE controlled synthetic-fact `contains_ci()` probe**; helpfulness = offline Claude A/B, **never a gate**. *Rationale:* a small greedy model may ignore the preamble; gating CI on "it helped" would be a flaky, dishonest test.

---

## 6. Milestones — M0 → M6 (mirror G1/G2; host vs box marked)

- **M0 (HOST/CI)** — retrieval **scorer** (exact-key + recency selection over known records) + preamble **assembler** (`char[]`→`char[]` with truncation + `*_len` honoring); `test_g3_retrieval.c` (deterministic selection + byte-exact assembly) + a prompt-**structure** test with a stub tokenizer (OFF == today; ON places preamble before the query). CI-greenable.
- **M1 (HOST TSan + BOX)** — PA-side `sctx_pack_preamble()` + the **cache-miss-path** scoring/pack, gated. Host: pack/seqlock under TSan. Box: pack runs, no regression.
- **M2 (BOX)** — PB-side **injection** (read preamble from `g_sctx_pb`, encode after `:163`, `prompt_ids[256]`); **OFF = byte-identical** to the recorded baseline. *(box-gated — generation never runs in CI.)*
- **M3 (BOX)** — the **injection proof**: `[RETR]` + `[PB]`-token dump against a **controlled store fixture**; ON = preamble-in-prompt + coherent + differs from OFF (the dual two-boot log diff).
- **M4 (BOX + CI)** — **latency <50 ms** on a cache miss + the **v3 telemetry slice** (D-a, co-landing #6's metric) + console retrieval row + the **synthetic-fact probe**.
- **M5 (BOX)** — **NVMe-backed post-reboot recall**: a **bounded** key→record fetch (NOT an O(N) sector scan) so ≥10 items recall after a power-cycle — closes done-when #1's recall half.
- **M6** — flip the `JARVIS_G3_RETRIEVAL` default ON + console honesty pass + final doc/week status.

*(M0–M1 are CI-greenable; M2/M3/M5 are inherently box-gated — generation never runs in CI.)*

---

## 7. Risks & landmines

- **The model may ignore the preamble** (small, greedy) — **never assert helpfulness** in an automated test (D-f).
- **Token-window overflow** — cap the preamble (`PREAMBLE_TOK_MAX`); **never raise the 512 KV cap**.
- **The <50 ms bar** — score on the **IN-RAM pool**, never an `epi_store_read` scan (O(N) NVMe sectors); the NVMe text fetch (M5) must be a bounded key→record lookup.
- **Brittle LLM-output asserts** — assert `is_coherent` / `no_excessive_repeats`, never an exact string.
- **Reproducibility** — control store state (zero or pre-seed the EPI region) before box tests; the circular store survives reboot, so a stale record set silently changes the preamble.
- **NUL-termination** — copy episodic `query`/`resp` by `query_len`/`resp_len`, never `strlen`.
- **Byte-identical is a BOX gate now** (§4) — CI cannot prove it (no model in CI); don't claim it CI-side.
- **Plan-vs-code gaps to fix in-flight:** "keyword" has **no index** (net-new); the `committed` flag is **dead** (always 0); there is **no key→record map** (M5 builds the bounded fetch); there is **no `g_retrieval_enabled` runtime global** yet (D-e adds compile flag + `preamble_len==0` skip); the exact cache-miss reach-PB path needs recon; **no committed vocab fixture** exists for host BPE (Layer A uses a stub tokenizer; real-id checks are box-only).

---

## Done-when (authored — none existed)

> **G3 is done when:** the scorer + assembler pass host CI deterministically; **OFF** generation is **byte-identical to the pre-G3 baseline on the box**; **ON** injects a real preamble (`[RETR]` + `[PB]`-token proven) that retrieves **≥10 stored items after a reboot**, adds **<50 ms on a cache miss**, and the output is **coherent**; a **real** retrieval signal (hit / latency) is on the console; helpfulness is assessed **offline** (Claude A/B, not a gate). Then **#6 (cache growth)** and the early **`memory` milestone tag** follow.

---

*Phase 5 cadence: weekly status at `phase5/weeks/weekN/WEEK_N_STATUS.md`. This doc mirrors `phase5/docs/PHASE_5_GOAL2_SHARED_CONTEXT_POOL.md`; the plan it serves is `phase5/docs/PHASE_5_PLAN.md`. G3 is the consumer of G1 (text) + G2 (index/read path); #6 cache-growth is a sibling of #1, not sequenced behind G3.*
