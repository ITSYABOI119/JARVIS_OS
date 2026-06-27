# Phase 5 — Goal #2: Shared Context Pool (C)

**Status:** 🔜 PLANNED (M0 next). No code yet.
**Date:** 2026-06-27
**Prerequisite:** G1 (episodic store) ✅ COMPLETE / box-verified 2026-06-27 (`phase5/docs/PHASE_5_GOAL1_EPISODIC_STORE.md`). G2 reads from the deployed two-process stack (PA rootserver + PB quantized engine, shared-memory IPC, the committed episodic store + its uncommitted RAM batch).
**Scope:** ROADMAP goal #2 — a bare-metal **working-memory** layer (system state, event stream, recent decisions) that the inference path and the G3 retrieval path read **without serialization**. Build-order: MVP-2, **depends on #1** (`PHASE_5_PLAN.md` §6). This goal ships the **container + index + plumbing**; the consumer logic (relevance scoring, preamble assembly, prompt injection) is **#3 (retrieval)** and stays **unmerged**.
**Sources:** `phase4/docs/ROADMAP.md:61` + `phase5/docs/PHASE_5_PLAN.md:28` (canon goal); `ARCHITECTURE_ENHANCEMENTS.md:837` (the Phase-1 "Shared Context Pool" C++ SPEC sketch this parallels — no C backing exists); `phase3/src/ipc/shmem_ipc.{c,h}` (the proven cross-process shared-page + atomic discipline); `phase1/src/ai/shared_context.py` (the Python `RLock` reference, NOT lock-free).

> **Goal (canon, `ROADMAP.md:61` / `PHASE_5_PLAN.md:28`):** Shared context pool (C) — Port the working-memory layer to bare metal: system state, event stream, recent decisions. All agents/processes read without serialization.

---

## 1. What G2 is (and isn't) — scope + boundary

**G2 is a GREENFIELD C build of a SPEC, not a port of live code.** The canon says "port the working-memory layer," but there is nothing in C to port:

- **No C backing exists.** `grep` for `shared_context_pool` / `system_state_t` / `lockfree_queue` / `versioned_cache` / `shared_context` across every `.c/.h` returns **zero matches**. The only "Shared Context Pool" artifact is the **C++ sketch** at `ARCHITECTURE_ENHANCEMENTS.md:837` (`atomic_ptr<system_state_t>`, `lockfree_queue<event_t>`, `versioned_cache<decision_t>`) — a design sketch using C++ templates, with no implementation behind it.
- **The Phase-1 Python is not a model to copy.** `phase1/src/ai/shared_context.py` is an **`RLock`-guarded dict** (`class SharedContext`, `self._lock = RLock()`) — i.e. lock-based, *not* lock-free — and it tracks fields tied to a multi-agent Python deployment that does not map to the 2-process bare-metal system.
- **The "220×" headline is not a bare-metal number.** The "220× faster than serialization" figure (`ARCHITECTURE_ENHANCEMENTS.md` / CLAUDE.md) is an early microbench, **not** a measurement of anything that runs on the box. Do not cite it as a G2 result.

**The agent-coordination purpose is vestigial here.** The SPEC's reason for existing — many agents reading shared state without serializing — assumes a multi-agent process model. The deployed system has **two address spaces**: Process A (rootserver: drivers, workload loop, the device, the decision cache, the episodic store) and Process B (the quantized inference engine). There is no agent swarm. So G2 is **not** an MPMC coordination fabric; it is a **single in-RAM working-state object** that two readers consume: Process A's own retrieval path (G3, the real consumer) and Process B during generation.

### Boundary — what G2 owns vs what reads/uses it

| Concern | Owner | Notes |
|---|---|---|
| Live in-RAM working state (system snapshot, event ring, recent-decision index) | **#2 (this goal)** | volatile; the container + the lock-free read discipline + the index |
| Durable interaction records on NVMe | **#1 (done)** | reboot-survival is #1's job, not the pool's |
| Deciding relevance + assembling the preamble + **injecting** it into the prompt | **#3 (retrieval)** | the consumer logic — **NOT G2** |
| Promoting repeated query→action patterns into the decision cache | **#6 (cache growth)** | reads the pool/episodic index |

The **#2/#3 seam is the dangerous one.** G2 ships the container, the index, and the PA↔PB plumbing; G3 is the consumer that scores relevance and **injects** context (which *will* change generation by design). **Keep them unmerged.** **G2 does NOT inject context into inference** — a G2 with injection in it cannot be regression-tested against byte-identical greedy output (see §7).

---

## 2. Locked design decisions

**D1 — Retrieval shape (bounds the whole feature).** Exact-key lookup (`cache_hash(cache_normalize_query(query))`) **plus** a recency fallback (most-recent-N on a key miss). **No similarity / embeddings** (that is Phase 7 "Instinct," `PHASE_5_PLAN.md` §9). *Rationale:* it pins what the pool must expose — **both** an exact-key map **and** recency iteration. The scoring that chooses between them is G3; G2 only provides the two access paths.

**D2 — Process A packs; Process B never allocates.** PA assembles the preamble text blob into a shared staging buffer; PB only **reads and tokenizes** it. *Rationale:* PB has no allocator and no device access (the same constraint that made G1's write path PA-side). All assembly is PA-side.

**D3 — Budget: start at 1–2 truncated `query→resp` pairs.** Enlarge PB's `prompt_ids[128]` toward `[256]` if needed (**NOT** the 512 KV cap), and measure prefill latency against the **<50 ms** retrieval bar (`ROADMAP.md:74`). *Rationale:* the preamble must fit the engine's token envelope (§4); a small fixed budget keeps prefill cheap and the latency bar reachable.

**D4 — The pool DOUBLES as the recent-history in-RAM index the plan assumes — and it must include the uncommitted batch.** `PHASE_5_PLAN.md` §8 (`:112`) assumes "a batched in-RAM index" for <50 ms retrieval, **but that index does not exist today.** G2's recent-decision ring **is** that index, keyed via `cache_hash`. It **MUST** include records still sitting in G1's uncommitted `g_epi_batch` (committed to NVMe only at the `[STATS]` cadence) — otherwise the newest memories are invisible to retrieval until the next commit. *Rationale:* "remembers what just happened" fails if the last N queries aren't visible until a flush.

**D5 — The pool is VOLATILE; rebuilt from NVMe at boot.** Reboot-survival is #1's job. "Recall ≥10 preferences after reboot" is met by **re-reading the episodic store**, not by the pool persisting. *Rationale:* keeps the pool a pure RAM working-set with no second durability mechanism to get wrong; one source of truth (the NVMe episodic store) for persistence.

**D6 — Region: one shared 4 KB page, two sub-regions.** A working-state struct + a preamble staging buffer, in a single page. *Rationale:* one more page on the existing PA↔PB mapping is the cheapest footprint; the 4 KB page is the unit the shmem path already uses and `_Static_assert`s against (`shmem_ipc.h:67`).

**D7 — Port the SPEC field-set, populated from REAL PA state.** The fields from `ARCHITECTURE_ENHANCEMENTS.md:837`: a **system_state** snapshot, an **event_stream** ring, and a **recent_decisions** keyed ring — filled from live Process-A workload state (status / active-operation counters / the per-iteration route + outcome), never invented. *Rationale:* honesty/UI-parity (D8) requires every field to have a real live source.

**D8 — Honesty / UI-feature-parity: a pool console signal rides a DELIBERATE `telemetry_packet_t` v2 SIZE BUMP.** Any pool metric on the console (e.g. a pool version counter or event count) ships as **one** fixture-synced v2 slice (golden + key-contract + honesty + console together — `PHASE_5_PLAN.md` §7.5). *Rationale:* the spare `reserved2` word was **already spent by G1/M4** (`episodic_count`), so there are no free reserved bytes left — a new pool metric **must** take the real v2 bump; do **not** smuggle it into reserved space.

---

## 3. Architecture (recommended — Approach A)

**A third shared page, mapped PA↔PB, read with a seqlock.**

- **Third PA↔PB page.** Extend `spawn_inference_process`'s shared-frame loop from 2 frames to 3 (`main_x86.c` ~1110–1150). PA maps it after the request/response rings at `SHMEM_VADDR_A + 2*PAGE`; the same physical frame is cap-duplicated into PB at `SHMEM_VADDR_B + 2*PAGE` (`SHMEM_VADDR_A=0x10000000`, `SHMEM_VADDR_B=0x50000000`, `MODEL_VADDR_B=0x60000000` — `main_x86.c:966-972`). **PB derives it as the 3rd contiguous page — no new `argv` slot.** WB-cacheable is fine: the deployed IPC path already proves cross-core atomics under `NUM_NODES=6`.
- **Seqlock for torn-read safety.** The pool is **read-without-remove**, which the existing shmem rings cannot serve (they are **consume-once SPSC queues** — `shmem_ipc.c` `read_idx`/`write_idx` advance on consume). So clone the *raw `__atomic_*` discipline* (acquire/release, the `shmem_ipc.c` ~72–144 pattern), not the ring semantics: a **seqlock** — the writer (PA) bumps a cache-line-aligned sequence counter to **odd** before a write and to **even** after; readers (PB, and PA's own G3 path) snapshot, then **retry on odd-or-mismatch**. Single writer (PA) + multiple readers, no locks, no blocking.
- **Data model in the page.** A `system_state` snapshot (versioned via the seqlock), an `event_stream` ring (latest-N events), and a `recent_decisions` keyed ring (D4/D7) — the keyed ring is the recency+exact-key index G3 reads.

*(Approach A reuses the one mechanism already proven cross-process — a shared seL4 page with atomic access — and adds only the seqlock, which is the minimal correct primitive for read-without-remove. No new IPC message type, no new device.)*

---

## 4. Constraints (carried from the recon — verified)

- **shmem payload = 240 B** (`SHMEM_MAX_PAYLOAD`, `shmem_ipc.h:20`; hard-rejected past the slot). **The preamble NEVER rides `MSG_QUERY`** — it lives in the shared page, not in a 240 B ring slot.
- **`prompt_ids[128]`** (`inference_server.c:134`) with ~10 fixed Gemma chat-template tokens of overhead → a working budget of **~118 tokens** for query + preamble combined. This is the **binding token limit today** (D3's `[256]` enlargement is the lever if needed).
- **KV window = 512** (`LLAMA_MAX_SEQ_LEN`, `llama_model.h:83`) → ~334 tokens of headroom past the prompt template. **Do NOT raise the cap** (it sizes the KV cache + RoPE tables).
- **Gemma 4 has NO system role.** A preamble lands **inside the user turn, before the question, delimited** — this is **G3's injection**, documented here only as the context G2's container must make assemble-able. **G2 does not inject.**
- **PB stack < 8 KB.** PB reads the pool **from the mapped page** (and copies only bounded fields it needs), never onto a large stack temporary — the same `fwd_scratch` discipline G1/the engine already follow.

---

## 5. Done-when (authored — none existed)

> **G2 is done when:** (a) the pool struct + seqlock read/write discipline pass a **host test under ThreadSanitizer** (single-writer / multi-reader, no torn reads, reduction == serial); (b) **Process A populates** `system_state` / `event_stream` / `recent_decisions` **live on the box** each workload iteration **without serialization**, `err=0`, with a monotonic version/generation counter advancing; (c) **Process B reads the pool non-faulting** in `handle_query()` via seqlock retry from the mapped page, **without perturbing greedy output** (byte-identical generation — because **G2 does not inject**); (d) a **real pool signal** (version counter or event count) is surfaced **honestly** on the console via the **v2 telemetry packet**. Then **#3 (retrieval)** can read the pool to score relevance and assemble/inject the preamble. *(G2 itself does NOT inject context — that is #3.)*

---

## 6. Milestones — M0 → M4 (mirror G1; host vs box marked)

Each milestone: **goal · files · done-when · host/CI vs box.** Every new `test_*.c` gets a CI step (CLAUDE.md rule).

### M0 — `shared_context.{c,h}`: struct + seqlock read/write *(HOST/CI)*
- **Goal:** the pool as a pure, host-testable module — the `system_state`/`event_stream`/`recent_decisions` struct in one page-sized region, the single-writer seqlock write path, and the reader snapshot-retry path — with **no device/seL4 dependency** (plain memory + `__atomic_*`).
- **Files:** `phase3/src/ai/shared_context.{c,h}`, `phase3/src/ai/test_shared_context.c` (TSan: single-writer/multi-reader, no torn read, recency + exact-key access, `g_epi_batch`-inclusion stub).
- **Done when:** host test green **under ThreadSanitizer** in CI (new step "Phase 5: Shared Context Pool (C, TSan)") — mirrors the `test_threadpool_sel4` TSan precedent.
- **Doable now:** **host + CI** (no box).

### M1 — third PA↔PB page mapped *(BOX)*
- **Goal:** extend the shared-frame loop 2→3 and map the pool page into PB at `SHMEM_VADDR_B + 2*PAGE` (PB derives it as the 3rd contiguous page; no new `argv`).
- **Files:** `main_x86.c` (`spawn_inference_process` frame loop ~1110–1150), `inference_server.c` (derive the pool pointer).
- **Done when:** QEMU/KVM smoke — PB maps + reads the page **non-faulting**, `err=0`, generation unchanged.
- **Doable now:** **box/KVM** (the map is seL4-only).

### M1a — init-decoupling guard *(BOX)*
- **Goal:** gate pool init on its own readiness flag (e.g. `g_ctx_ready`), independent of unrelated bring-up — mirror G1/M1a, where episodic init was wrongly nested in `nvme_log_init`'s success block.
- **Done when:** pool init runs whenever its prerequisites are wired, regardless of nvme_log/other init outcomes; verified in the box log.
- **Doable now:** **box** (config/log verification).

### M2 — live PA population each loop *(BOX)*
- **Goal:** Process A writes `system_state`/`event_stream`/`recent_decisions` from real workload state every iteration via the seqlock — including the uncommitted `g_epi_batch` records (D4).
- **Done when:** the version counter advances + the rings fill, confirmed via the NVMe log + telemetry; **generation byte-identical** (G2 is read-only into inference).
- **Doable now:** **box**.

### M3 — PB reads the pool in `handle_query` *(BOX — "unblocks G3")*
- **Goal:** PB reads the pool (seqlock retry, from the mapped page) inside `handle_query()`.
- **Done when:** coherent generation, **does not perturb greedy output** (**no injection** — that is G3); the read is the hook G3 builds on.
- **Doable now:** **box**.

### M4 — telemetry v2 + console signal *(HOST + BOX)* — mandatory UI-feature-parity step
- **Goal:** a real pool metric (version counter / event count) on the read-only console via the **deliberate `telemetry_packet_t` v2 SIZE bump** (D8).
- **Files:** `jarvis_telemetry.{c,h}` (v2: a new field + capability flag — **a real size bump**, reserved2 already spent by G1/M4), `telemetry_receiver.py` + `packet_to_record`, `golden_telemetry.json` + `golden.pcap` (regenerated), the key-contract + honesty + Playwright tests, the console Capabilities/System surface — **all in one fixture-synced slice**.
- **Done when:** the pool signal renders **live** on the console; honesty gate + golden-drift gate + key-contract + Playwright e2e all green; live value confirmed on the box (rides the I211, not emulated by QEMU).
- **Doable now:** **host** (Playwright + `--replay` against a v2 golden pcap); the live value = **box**.

---

## 7. Risks & landmines

- **Greenfield, not a port.** There is no C "working-memory layer" to port (§1); the C++ sketch and the Python `RLock` dict are references, not code. Budget M0 as a real build.
- **Canon "all agents read" ≠ 2-process reality.** Design **SPSC/seqlock** (single writer PA, readers PB + PA-G3), **not** MPMC/RCU. The agent-coordination framing is vestigial on bare metal.
- **The assumed in-RAM index does not exist.** `PHASE_5_PLAN.md` §8 (`:112`) presumes a batched in-RAM index for <50 ms retrieval; G2's recent-decisions ring **is** it (D4) — and it **must** include `g_epi_batch` or the newest memories are invisible until the next `[STATS]` commit.
- **240 B slot.** The preamble never rides `MSG_QUERY`; it lives in the shared page (§4).
- **Token window — do NOT raise 512.** The KV cap sizes the KV cache + RoPE tables; the preamble fits the ~118-token prompt budget (D3), it does not grow the window.
- **PB stack < 8 KB.** Read fields from the mapped page; never stage the pool on PB's stack.
- **Reboot = repopulate from NVMe (D5).** The pool is volatile; "recall after reboot" is #1's durability, re-read at boot — not a second persistence path.
- **Greedy determinism is the regression guard — so G2 MUST stay read-only.** Preamble **injection is G3** and will change generation **by design**; if G2 injected, byte-identical-generation tests would flag a false regression. Keep G2 non-injecting so M2/M3 can assert byte-identical output (the #2/#3 seam, kept unmerged).
- **SHIELD live no-op (SEC-039).** The preamble is **not** filtered; Phase 5 SHIELD stays **monitor-only** (`PHASE_5_PLAN.md` §7.3). Live filtering of injected context is Phase 6.
- **Poll-don't-`Wait` Heisenbug.** Any PB-side read added to the workload/response path must respect the documented poll-don't-`seL4_Wait(resp_notif)` rule; verify by **err-rate at `JARVIS_DBG_IPC=0`**, not per-event logs.
- **Reserved-byte exhaustion.** `reserved2` is gone (G1/M4) — the M4 pool metric forces the real v2 size bump (D8); there is no shortcut.
- **Episodic field hygiene.** The episodic `feedback` byte is currently unused, and `query`/`resp` are **not** NUL-terminated — honor `query_len`/`resp_len` when the pool ingests episodic records into the recent index.

---

*Phase 5 cadence: weekly status at `phase5/weeks/weekN/WEEK_N_STATUS.md`. This doc mirrors the keystone `phase5/docs/PHASE_5_GOAL1_EPISODIC_STORE.md`; the plan it serves is `phase5/docs/PHASE_5_PLAN.md`. G2 ships the pool + index + plumbing; G3 (retrieval) is the consumer and stays unmerged.*
