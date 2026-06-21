# JARVIS AI-OS — UI design docs (Phase 4, goals #2 / #2b)

Two distinct UI surfaces live here:

1. **The bare-metal framebuffer HUD** (goal #2, **shipped**) — rendered in C on the box's GOP framebuffer.
2. **The Remote Telemetry Console** (goal #2b, **planned**) — a web UI on a *separate* machine, seeded by the local-only `design-system/`.

See `docs/decisions/2026-06-21-adopt-headless-appliance-remote-console.md` for the headless-appliance decision that splits these.

## Files (committed)

| File | What it is |
|---|---|
| `jarvis-framebuffer-hud.html` ⭐ | **The IMPLEMENTED on-box HUD spec.** This is what actually renders on the bare-metal box — in C, via `phase3/src/sel4/jarvis_ui_tokens.h` (design tokens) + `phase3/src/drivers/framebuffer.c` (8×16 bitmap font + status panel + STATE badge + route/counters + scrolling event log). Treat it as the source-of-truth mockup for the shipped framebuffer UI. |
| `jarvis-ui-phosphor.html` | HUD exploration — bare-metal-honest terminal/CRT (box-drawing, text load-bars). Closest to framebuffer-native primitives. |
| `jarvis-ui-arc-hud.html` | HUD exploration — tactical arc-reactor HUD (maximal sci-fi; mostly aspirational glow/gradients). |
| `jarvis-ui-mission-control.html` | HUD exploration — calm operations dashboard (panel grid). |
| `jarvis-ui-simple.html` | HUD exploration — calm, conversation-first v1. |
| `jarvis-ui-bootlog.html` | HUD exploration — boot-sequence / log view. |
| `BRIEF.md` | The grounded design brief (data model, framebuffer constraints, tokens, layout). |
| `JARVIS_OS_DESIGN_TOKENS.md` ⭐ | The reusable port of the visual language into framebuffer/C terms (exact colors + VGA-16 nearest, type/casing, spacing, the "Operator" voice). Referenced by `PHASE_4_GOAL2_DISPLAY.md` and the week docs. |

## `design-system/` — LOCAL-ONLY / git-ignored

The full **"Jarvis OS" web design system** (React Command Center + component library + rendered `Jarvis OS.html` + `.zip` bundle, ~5.9 MB) lives in `design-system/` and is **git-ignored** (`.gitignore`: `phase4/docs/ui_mockups/design-system/`). It is kept on disk but **never committed**.

- It is the **UI seed for the Remote Telemetry Console (goal #2b)** — a **WEB UI that does NOT run on the bare-metal box** (no GPU / font engine / compositor there). It renders on a *separate* machine that receives the box's telemetry over the network.
- **It is not yet honest.** As shipped it contains aspirational / not-yet-true elements that **must be corrected before the console ships**:
  - GPU references and **19.7 tok/s** → the real deployed system is **CPU-only, 5.46 tok/s @ NUM_NODES=6** (~1.53 single-thread).
  - **SHIELD shown as blocking** → SHIELD is **monitoring/passive** (live PA↔PB path returns ALLOW, shield.c not linked, SEC-039).
  - **Model tiers / a live agent roster** → there is **one** model (Gemma 4 E2B), no runtime tiers, no live agent roster.
  - **"verified"** → the deployed config is **functional-but-unverified** (KernelFastpath + XSAVE/AVX + SMP), per `docs/decisions/2026-06-16-x86-verification-stance.md`.

## Honesty guardrails (true for BOTH surfaces)

- Never label the running system "formally verified."
- SHIELD is **monitoring/passive**, not a proven "100% blocked" guarantee.
- tok/s: ~1.53 single-thread, **5.46 @ NUM_NODES=6** (M3 threadpool) — never inflated GPU/16T numbers (CPU-only v1.0).
- One model (Gemma 4 E2B), no live agent roster, no model tiers.
