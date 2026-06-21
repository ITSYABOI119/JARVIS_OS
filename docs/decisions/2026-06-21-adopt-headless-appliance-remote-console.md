# ADR: Adopt the headless-appliance model — Remote Telemetry Console as v1.0 goal #2b; down-rank local keyboard (#3)

**Date:** 2026-06-21
**Status:** Accepted
**Deciders:** JARVIS Development

## Context

- Phase 4 goal #2 (on-box framebuffer HUD) is **v1-complete**: a bare-metal GOP framebuffer renders a status panel + STATE badge + route/counters + a scrolling event log, all mirrored to the durable NVMe log. But it is **capped at bitmap fidelity** — there is no GPU, no font engine, and no compositor on the bare-metal box, and there never will be in v1.0 (GPU compute is deferred — `docs/decisions/2026-06-16-defer-gpu-inference.md`). The on-box UI budget is effectively ~0.
- A rich "Jarvis OS" **web design system** exists locally at `phase4/docs/ui_mockups/design-system/` (React Command Center + component library + rendered `Jarvis OS.html` + bundle, ~5.9 MB, local-only). It envisions a UI the bare-metal box fundamentally **cannot render**.
- The Intel **I211 NIC** on the JARVIS PC is enumerated but **dormant** — no live TX/RX path on bare metal, and a known **virtual→physical DMA bug** in the descriptor path (`nic_i211.c` stores a virtual address; works only under identity mapping).
- A 4-agent feasibility scout assessed a network **Remote Telemetry Console**: the box emits its already-existing `[SNAP]`/`[STATS]`/`[INFER]` telemetry; a browser on a separate machine renders the rich UI. (Telemetry data, packet schema, honesty map, and security analysis are in that scout, archived.)
- This supersedes the earlier "future/optional remote console" draft — the console is now a **scoped v1.0 goal #2b**.

## Decision

- **Adopt the headless-appliance model as JARVIS's primary interaction model.** The box is a **network inference appliance** that **emits** telemetry; a **browser console on a separate machine** renders the rich (honesty-corrected) UI; the box's own monitor keeps the thin framebuffer HUD as a local "alive" readout.
- **Make the Remote Telemetry Console a v1.0 goal #2b** (parallel to goals #2–7). It absorbs part of goal #2's "status UI" intent — the rich UI lives off-box because the box can't render it. The console's UI seed is the local, git-ignored design-system.
- **Down-rank goal #3 (xHCI USB keyboard)** from v1.0-blocker to **optional / standalone-convenience**, kept **recoverable as the safe, physically-gated fallback control path** (no network attack surface).
- **Phase the network channel hard:**
  - **Telemetry-OUT (low-risk) ships in v1.0** — the box only *sends* UDP; no inbound parsing, no control surface.
  - **Control-IN is deferred to ~Phase 6** (when conversation becomes the primary interface), gated on the full security checklist: **auth + HMAC**, **real SHIELD** (close SEC-039 — the live PA↔PB path currently returns ALLOW, shield.c not linked), **rate-limiting**, a **hardened/fuzzed inbound parser**, and ideally a **less-privileged input process** (SEC-014).
- **Build order** — box side: **N-a** (I211 TX first-light bring-up, incl. the virtual→physical DMA fix) → **N-b** (minimal Eth/IP/UDP emit) → **N-c** (hook at the existing `[STATS]` site + a keepalive). Console side: an honesty pass over the design-system → live data binding → a ~60-line UDP receiver. Box-side MVP ≈ **250–300 LOC** + the I211 bring-up (the main risk: the NIC has never run on hardware). Console side ≈ **2.5–4 days**.

## Alternatives rejected

- **A — Build a richer GUI on the bare-metal box.** No GPU / font engine / compositor, and GPU compute is deferred — a heavy on-box GUI is non-executable in v1.0 and would inflate the on-box UI budget that this decision deliberately keeps near zero. Rejected.
- **B — Ship control-IN in v1.0 (two-way console now).** An unauthenticated inbound control channel on a box whose live SHIELD is a no-op (SEC-039), with the I211 RX path unhardened, is an unacceptable attack surface. Rejected — deferred to Phase 6 behind the security checklist.
- **C — Keep the local USB keyboard as the v1.0 primary interface.** It needs xHCI bring-up, only works at the box, and doesn't solve the rich-UI gap. Kept as the optional, physically-gated fallback instead of the primary path.

## Consequences

- The box's **UI budget stays ~0** — we never build a heavy on-box GUI; the framebuffer HUD remains the thin local readout.
- The **dormant I211 NIC gets a real job** (telemetry emit), and its first-light bring-up + DMA fix become scoped goal-#2b work.
- Off-box observability is the **natural endpoint of the log-mirror discipline** — the same `[PANEL]`/`[SNAP]`/`[STATS]` stream that already proves UI-from-log now also feeds a remote renderer.
- **HONEST framing:** this is **NOT a current speedup** — the on-box HUD is already thin, and inference is memory-bandwidth-bound (~2.9 GB/token). The win is **future-proofing + a home for the renderable design + network observability**, not tok/s.
- **The console must render ONLY the real system:** **5.46 tok/s** (CPU, NUM_NODES=6) / **~1.53** single-thread — **never 19.7 or any GPU number**; **SHIELD = "monitoring/passive"** (not a live blocker); **single Gemma 4 E2B** (no model tiers); **"functional-but-unverified"** (never "formally verified"); **no live agent roster**. The design-system as-shipped contains aspirational/not-yet-honest elements (GPU, 19.7 tok/s, SHIELD-blocking, model tiers, agent roster, "verified") that **must be corrected before the console ships**.

## References

- Feasibility scout (4-agent): telemetry data + packet schema + honesty map + security analysis (archived).
- UI seed: `phase4/docs/ui_mockups/design-system/` (local-only / git-ignored); HUD design docs: `phase4/docs/ui_mockups/` (committed).
- Roadmap: `phase4/docs/ROADMAP.md` — goal #2b added; goal #3 down-ranked; control-IN under Phase 6.
- Related: `docs/decisions/2026-06-16-defer-gpu-inference.md` (no on-box GPU), `docs/decisions/2026-06-16-x86-verification-stance.md` ("functional-but-unverified"); SEC-039 (live SHIELD no-op), SEC-014 (process isolation), `phase3/src/drivers/nic_i211.c` (the NIC to bring up).
