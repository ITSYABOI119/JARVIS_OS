# Phase 5 — Goal #1: Episodic Store on NVMe (KEYSTONE)

**Status:** ✅ COMPLETE — M0–M4 done and **box-verified on real hardware 2026-06-27** (reboot-survival across a 3-boot power-cycle lineage + live `episodic_count` on the console over the real I211 NIC). Keystone done.
**Date:** 2026-06-26 (closed out 2026-06-27)
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

### M0 — `episodic_store.c/h`: region layout + schema + serialize/deserialize + circular logic — ✅ DONE *(host/CI)*
- **Goal:** the store as a pure, host-testable module — region layout, the 512-byte record schema, serialize/deserialize, XOR checksum, and the circular cursor/`total_entries` logic — with **no device dependency** (a `read`/`write` callback like `fat32`'s, so the host test drives a mock buffer).
- **Files:** `phase3/src/ai/episodic_store.{c,h}` (or `phase3/src/drivers/`), `phase3/src/ai/test_episodic_store.c` mirroring `test_nvme_log.c` (wrap / no-`-2` / cursor cycles / `total` monotonic / cap / newest-overwrites-oldest / checksum / migration clamp).
- **Done when:** host test green in CI (new CI step "Phase 5: Episodic Store (C)").
- **Doable now:** **host + CI** (no box).

### M1 — batched A-side write path wired into `main_x86.c` — ✅ DONE *(M1a init-decouple fix; box-verified via the M3 lineage 2026-06-27)*
- **Goal:** fill an episodic record from live state at the query/inference site and accumulate into a batch, committing the batch to the raw region at the idle/consolidation point (not per query).
- **Files:** `main_x86.c` (the workload-loop query/inference site; the batch buffer + commit trigger), `episodic_store.c` (the device-backed write via `nvme_write_sectors`).
- **Done when:** records accumulate and flush correctly in a QEMU/KVM smoke (batch fills, commits on the trigger, `cursor`/`total` advance).
- **Doable now:** batch/commit **logic host-now**; the device write + smoke = **box/KVM**.

### M2 — read-back + `parse_episodic.py` — ✅ DONE *(host round-trip + box `dd` read-back 2026-06-27)*
- **Goal:** a written store round-trips through the parser — the canon "readable and parseable" criterion.
- **Files:** `phase3/scripts/parse_episodic.py` (mirrors `parse_nvme_log.py`); a host fixture/round-trip in `test_episodic_store.c` (write N records → serialize → parse → assert field-exact).
- **Done when:** a written store round-trips through the parser with every field intact (synthetic host fixture green; real on-box `dd` read parses).
- **Doable now:** **host-now** (synthetic) + **box** (real region).

### M3 — reboot-survival on the box (the canon proof) — ✅ DONE (box-verified 2026-06-27)
- **Goal:** the load-bearing canon proof — memory survives a reboot.
- **Done when:** write records, **reboot the JARVIS PC**, read them back with `boot_id` **incremented** and the prior records **intact** (parsed via `parse_episodic.py` from the raw region). This is the foundation under the Phase-5 done-when "recall ≥10 preferences after reboot."
- **PROVEN ON HARDWARE (2026-06-27):** a **3-boot episodic lineage** on the JARVIS PC (`/dev/nvme0n1` @ LBA 21,100,000) with **hard power-cycles** between boots — **boot 1** committed 335 records (cache + Gemma inference); **boot 2** re-init read the persisted 335 and appended 84 (idx 335–418, total 419) with **no clobber**; **boot 3** re-init read 419 and bumped `boot_id`→3 (no new commit — power-cycled before the q=100 cadence). Each read: header valid (real XOR checksum OK, magic `JEPI`), 419/419 cursor/total, 419 decoded, read back coherent from Ubuntu via `sudo dd ... skip=21100000 count=8193 | parse_episodic.py`. The re-init/append-without-clobber path is proven on real hardware across power loss (stronger than the QEMU 2-boot proxy that preceded it).

### M4 — console surfacing via telemetry (episodic count) — ✅ DONE (box-verified 2026-06-27)
- **Goal:** the episodic store becomes a **real live signal** on the read-only console (UI-feature parity).
- **Files:** `jarvis_telemetry.{c,h}` (`TLM_F_MEMORY 0x20` + `episodic_count` — the spare `reserved2` word repurposed at offset 192, **NO packet-size bump**; CRC@196 unchanged, `jarvis_telemetry.c` untouched), `telemetry_receiver.py` + `packet_to_record`, the golden fixtures (`golden_telemetry.json` / `golden.pcap`), the key-contract + honesty + Playwright tests, and the console Capabilities/System surface.
- **Done when:** the episodic count renders **live** on the console; honesty gate + golden-fixture drift gate + key-contract + Playwright e2e all green.
- **CONFIRMED ON HARDWARE (2026-06-27):** `episodic_count` climbed **live 335→419** and `TLM_F_MEMORY` rendered the console "Episodic memory store" Capabilities row + "Episodic records" System stat over the **real I211 NIC** (not QEMU), NN=6, err=0. Shipped **without** the planned v2 size bump — the spare `reserved2` word sufficed; a deliberate v2 bump stays reserved for the next memory metrics (retrieval / cache-growth).

---

## 3. Risks / open questions

- **Write-wear (the dominant constraint):** episodic writes MUST be batched + committed at idle/consolidation, not per query — see `PHASE_5_PLAN.md` §8. The 512-byte-record / flush-after-commit / circular discipline is inherited from `nvme_log`, which has run durably on the box.
- **Region reservation:** the episodic sub-region base/length must be documented in `episodic_store.h` and reserved against a future installer/repartition (the installer's `--target disk`/`esp` paths must learn about it). Long-term: promote to a `JARVIS_MEM` partition.
- **Key normalization parity:** the episodic query key must use the **exact** decision-cache normalization (lowercase → collapse spaces → trim → FNV-1a 64-bit) so #6 cache-growth can match episodic patterns against cache entries without divergence (the Phase-1/2/3 "both sides normalize identically" rule).
- **The v2 wire bump:** the M4 telemetry change is the one place Phase 5 touches the wire contract — done as a single fixture-synced slice, never piecemeal.
- **Reboot proof (DONE):** M3 was the only milestone that couldn't be faked on the host — **proven on the box 2026-06-27** (3-boot power-cycle lineage; 419 records survived, `boot_id`→3, no clobber).

---

## 4. Exit criteria → ROADMAP done-when

This keystone goal directly underwrites two of the five Phase-5 done-when criteria (`ROADMAP.md:68-72`):
- [x] **Episodic log readable and parseable** (`parse_*` tool) — satisfied at **M2** (`parse_episodic.py`); **box-verified 2026-06-27** (419 records read back from Ubuntu).
- [ ] **Reboot → recalls ≥10 stored preferences** — the **storage half** is **DONE / box-verified at M3** (reboot-survival: 3-boot lineage, 335+84=419 survived hard power-cycles); the *recall* half is completed by retrieval (#3) reading this store (still open — this box stays unchecked until recall lands).

The remaining done-when criteria are satisfied by the goals that build on this keystone: cache-hit-rate improvement (#6), SHIELD-learning (#5), and the <50 ms retrieval bar (#3).
