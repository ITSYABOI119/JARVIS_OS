# Phase 5 — Goal #1: Episodic Store on NVMe (KEYSTONE)

**Status:** 🔜 PLANNED (Phase 5 started 2026-06-26; this is the keystone goal, built first). No code yet.
**Date:** 2026-06-26
**Scope:** ROADMAP goal #1 — a structured interaction log on NVMe `{timestamp, query, action, outcome, optional feedback}` that **survives reboot**. This is the **keystone**: retrieval (#3) reads it, semantic memory (#4) distills from it, cache growth (#6) promotes from it, consolidation (#7) compacts it. Nothing else in Phase 5 ships until this does.
**Sources:** `phase4/docs/ROADMAP.md:58` (canon goal); `phase3/src/drivers/nvme_log.{c,h}` + `phase3/scripts/parse_nvme_log.py` (the proven raw-LBA store + parser template); the verified NVMe free-space map (`phase5/docs/PHASE_5_PLAN.md` §5).

> **Goal (canon, `ROADMAP.md:58`):** Structured interaction log on NVMe: timestamp, query, action, outcome, optional feedback. Survives reboot.

---

## 1. Design

### Region (raw-LBA, within the reserved 8 GiB memory gap)
The episodic store is a clone of `nvme_log`'s raw-LBA circular store, carved as a **sub-region** of the Phase-5 memory region (`PHASE_5_PLAN.md` §5: base ≈ sector 21,100,000, ~8 GiB reserved, ~3.56 B sectors clear of Ubuntu and the tail telemetry log). The episodic sub-region's exact base/length are fixed in `episodic_store.h` (documented + reserved like `nvme_log.h` documents its tail LBA, so an installer/repartition can't overlap it).

### Record schema (512-byte records)
A fixed **512-byte** record, header + circular cursor exactly like `nvme_log`:
- **Header (64 B):** magic (`"JEPI"`), version, `cursor` (wrapping write slot), `total_entries` (monotonic lifetime), `boot_id`, XOR checksum, reserved.
- **Record (512 B):** `boot_id`, `seq`, `timestamp` (boot-relative T+ms, the existing TSC→ms convention), a **query key** (FNV-1a 64-bit normalized hash — the same normalization the decision cache uses, so #6 cache-growth can match), a compact `action`/route code, an `outcome` code (ok / error / blocked), an optional `feedback` byte, and a bounded truncated query/response tail for human readability. Field layout frozen in `episodic_store.h` with a `_Static_assert(sizeof == 512)`.
- **Circular & durable:** wraps (keeps the latest, never fills); flushed after every committed write; `boot_id` increments each boot and is constant within a boot (the reboot-survival signal).

### Write path (batched, Process-A-side)
Records are filled at the **query / inference site** in `main_x86.c` (Process A — it owns the device and the workload loop) and **batched**, committed at consolidation/idle rather than per query (write-wear discipline, `PHASE_5_PLAN.md` §8). Process B has no allocator/device access, so it contributes outcome/feedback back over the existing shmem response ring (the `MSG_DEBUG`/response path), and Process A writes the record.

### Parser
Ship `phase3/scripts/parse_episodic.py` mirroring `parse_nvme_log.py`: read the raw region via `dd`, validate the XOR checksum, reconstruct wrap-order (oldest→newest from `cursor`), and pretty-print records (honest fields only). This satisfies the canon done-when "episodic log readable and parseable (`parse_*` tool)".

### Console
Surface the **episodic record count** on the read-only console via the planned telemetry **v2** bump — a `TLM_F_MEMORY` capability flag + a `episodic_count` field. This rides the cross-cutting v2 slice (its own sub-task; `PHASE_5_PLAN.md` §7.5) — golden fixtures + key-contract + honesty tests + the Capabilities surface updated together. No hardcoded claim: the count is a real live field or it doesn't ship.

---

## 2. Plan — M0 → M4

Each milestone: **goal · files · exit / done-when · test (+ CI) · doable-now (host/CI) vs box.** Every new `test_*.c` gets a CI step (CLAUDE.md rule).

### M0 — `episodic_store.c/h`: region layout + schema + serialize/deserialize + circular logic *(host/CI)*
- **Goal:** the store as a pure, host-testable module — region layout, the 512-byte record schema, serialize/deserialize, XOR checksum, and the circular cursor/`total_entries` logic — with **no device dependency** (a `read`/`write` callback like `fat32`'s, so the host test drives a mock buffer).
- **Files:** `phase3/src/ai/episodic_store.{c,h}` (or `phase3/src/drivers/`), `phase3/src/ai/test_episodic_store.c` mirroring `test_nvme_log.c` (wrap / no-`-2` / cursor cycles / `total` monotonic / cap / newest-overwrites-oldest / checksum / migration clamp).
- **Done when:** host test green in CI (new CI step "Phase 5: Episodic Store (C)").
- **Doable now:** **host + CI** (no box).

### M1 — batched A-side write path wired into `main_x86.c` *(host logic now; box/KVM smoke)*
- **Goal:** fill an episodic record from live state at the query/inference site and accumulate into a batch, committing the batch to the raw region at the idle/consolidation point (not per query).
- **Files:** `main_x86.c` (the workload-loop query/inference site; the batch buffer + commit trigger), `episodic_store.c` (the device-backed write via `nvme_write_sectors`).
- **Done when:** records accumulate and flush correctly in a QEMU/KVM smoke (batch fills, commits on the trigger, `cursor`/`total` advance).
- **Doable now:** batch/commit **logic host-now**; the device write + smoke = **box/KVM**.

### M2 — read-back + `parse_episodic.py` *(host-now synthetic; box real)*
- **Goal:** a written store round-trips through the parser — the canon "readable and parseable" criterion.
- **Files:** `phase3/scripts/parse_episodic.py` (mirrors `parse_nvme_log.py`); a host fixture/round-trip in `test_episodic_store.c` (write N records → serialize → parse → assert field-exact).
- **Done when:** a written store round-trips through the parser with every field intact (synthetic host fixture green; real on-box `dd` read parses).
- **Doable now:** **host-now** (synthetic) + **box** (real region).

### M3 — reboot-survival on the box (the canon proof) *(box)*
- **Goal:** the load-bearing canon proof — memory survives a reboot.
- **Done when:** write records, **reboot the JARVIS PC**, read them back with `boot_id` **incremented** and the prior records **intact** (parsed via `parse_episodic.py` from the raw region). This is the foundation under the Phase-5 done-when "recall ≥10 preferences after reboot."
- **Doable now:** **needs box** (a real reboot — like the `nvme_log` `boot_id`-constant proof, but across a power cycle).

### M4 — console surfacing via telemetry v2 (episodic count) *(host Playwright/replay; box live)*
- **Goal:** the episodic store becomes a **real live signal** on the read-only console (UI-feature parity).
- **Files:** `jarvis_telemetry.{c,h}` (the v2 packet: `TLM_F_MEMORY` + `episodic_count`), `telemetry_receiver.py` + `packet_to_record`, the golden fixtures (`golden_telemetry.json` / `golden.pcap`), the key-contract + honesty + Playwright tests, and the console Capabilities/System surface — **all in the one deliberate v2 slice** (`PHASE_5_PLAN.md` §7.5).
- **Done when:** the episodic count renders **live** on the console; honesty gate + golden-fixture drift gate + key-contract + Playwright e2e all green.
- **Doable now:** **host** (Playwright + `--replay` against a v2 golden pcap); the live value = **box**.

---

## 3. Risks / open questions

- **Write-wear (the dominant constraint):** episodic writes MUST be batched + committed at idle/consolidation, not per query — see `PHASE_5_PLAN.md` §8. The 512-byte-record / flush-after-commit / circular discipline is inherited from `nvme_log`, which has run durably on the box.
- **Region reservation:** the episodic sub-region base/length must be documented in `episodic_store.h` and reserved against a future installer/repartition (the installer's `--target disk`/`esp` paths must learn about it). Long-term: promote to a `JARVIS_MEM` partition.
- **Key normalization parity:** the episodic query key must use the **exact** decision-cache normalization (lowercase → collapse spaces → trim → FNV-1a 64-bit) so #6 cache-growth can match episodic patterns against cache entries without divergence (the Phase-1/2/3 "both sides normalize identically" rule).
- **The v2 wire bump:** the M4 telemetry change is the one place Phase 5 touches the wire contract — done as a single fixture-synced slice, never piecemeal.
- **Reboot proof needs the box:** M3 is the only milestone that cannot be faked on the host — it is a real power-cycle. Budget a box session for it.

---

## 4. Exit criteria → ROADMAP done-when

This keystone goal directly underwrites two of the five Phase-5 done-when criteria (`ROADMAP.md:68-72`):
- [ ] **Episodic log readable and parseable** (`parse_*` tool) — satisfied at **M2** (`parse_episodic.py`).
- [ ] **Reboot → recalls ≥10 stored preferences** — the **storage half** is satisfied at **M3** (reboot-survival); the *recall* half is completed by retrieval (#3) reading this store.

The remaining done-when criteria are satisfied by the goals that build on this keystone: cache-hit-rate improvement (#6), SHIELD-learning (#5), and the <50 ms retrieval bar (#3).
