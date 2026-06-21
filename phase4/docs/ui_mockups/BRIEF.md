# JARVIS AI-OS — UI Design Brief

> Paste-ready brief for **Claude / claude.ai artifacts** ("Claude design"). Drop this whole file
> in and say *"build this as an interactive mockup"*, or pair it with one of the HTML files in this
> folder and say *"iterate on this design using the brief."* This is Phase 4 **goal #2** — the
> bare-metal framebuffer/HDMI display for JARVIS AI-OS. It is a **design exploration**, not OS source.

---

## What JARVIS is (so the UI means something)

JARVIS AI-OS is a real AI-controlled operating system built on the **seL4 microkernel**, running
**bare metal** on x86-64 (the "JARVIS PC": Ryzen 7 2700X, Radeon R9 280X driving the display over
HDMI). The split:

- **Ring 0 — seL4 microkernel:** interrupts (<1 ms), memory, lock-free IPC. ~12K LOC.
- **Ring 3 — AI decision engine:** 50–500 ms decisions, specialist agents, the SHIELD safety
  framework, user-space services.
- **Two isolated processes:** **Process A** (rootserver — decision cache, x86 drivers, shared-memory
  IPC, SHIELD) and **Process B** (LLM inference). They talk over lock-free shared-memory rings (CRC-32).

The screen is a **2D framebuffer over HDMI** on a Radeon R9 280X — *display only, not GPU compute*.
Input is keyboard/USB-HID first (no guaranteed mouse). So this is a **HUD/dashboard**, not a
mouse-driven desktop.

## The heart of it: an AI-OS is conversational

The primary element is a **conversation**: the user asks JARVIS something, and the AI answers,
**streaming tokens** as the model generates. Around that, the OS surfaces its own live state — which
is the fun part of an *AI* OS: you can see it think, route, cache, and protect.

## Data model — what to display (use these as realistic live/mock values)

| Element | Value / behavior |
|---|---|
| **Model** | `Gemma 4 E2B` (Q4_K_M, ~2.89 GiB), loaded from NVMe. Engine supports 6 model families. |
| **Inference speed** | ~**5.2 tok/s** live (multi-core threadpool, ~5 of 8 cores); single-thread baseline ~1.5 tok/s. Stream tokens into the response area at roughly this rate. |
| **Decision cache** | **85.7%** hit rate, **<1 ms** lookup, ~308 compiled query→action patterns. Most queries are answered instantly by the cache; the rest escalate to the LLM. Show a "CACHE HIT" vs "→ LLM" indicator per query. |
| **CPU** | Ryzen 7 2700X — **8 cores / 16 threads**. Show per-core activity; ~5 cores busy during inference (the M3 threadpool), the rest idle. |
| **Memory** | 32 GB RAM. **NVMe**: Lexar NM790 2 TB. |
| **System** | Boot ~2 s. Show uptime, total query count, IPC messages/sec. |
| **Specialist agents** | Domain experts (not model sizes): **Device · Network · Filesystem · User**. Show status/activity per agent. |
| **SHIELD** | Safety framework — show a **risk meter / "monitoring"** status. ⚠️ Label it honestly as monitoring; **do not** claim "100% blocked" (it's aspirational in the live build). |
| **Kernel** | `seL4 microkernel · Ring 0`. ⚠️ **Never** print "formally verified" — the running performance config is intentionally outside seL4's verified set. |

## Framebuffer reality (keep the design buildable)

What renders natively on a bare-metal framebuffer: **filled rectangles, lines, monospace bitmap
text, simple bars and arcs/circles**. Things like soft glows, gradients, blur, and anti-aliased
rounded corners are **aspirational** — the framebuffer would only approximate them. Lean
**monospace-forward**, panel-and-line based, with flat color fills. It's fine to design something
prettier than v1 can render — just keep a clear line between "ships now" and "aspirational glow."

## Aesthetic / design tokens (starting point — vary freely)

- **Mood:** dark, calm-but-alive, "JARVIS / arc-reactor" — confident sci-fi, never cluttered.
- **Background:** near-black `#05080d` → charcoal `#0e1217`.
- **Primary accent:** electric cyan `#22d3ee` / `#00e5ff` (the "arc reactor" blue).
- **Secondary:** teal `#2dd4bf`; **warm amber** `#f5a524` for alerts / SHIELD risk.
- **Text:** off-white `#e6f1ff` primary, muted `#5b7085` for labels (small-caps).
- **Type:** monospace primary (`ui-monospace, 'Cascadia Code', Consolas, 'Courier New', monospace`);
  a clean `system-ui` is OK for big headings.
- **Canvas:** 1280×720 (16:9), centered with a subtle bezel/letterbox so it reads as a screen.
- **Motion (subtle):** a running clock, a pulsing AI core that intensifies while generating,
  per-core meters drifting, tokens streaming, a blinking cursor.

## Layout (one strong default — rearrange as you like)

```
┌──────────────────────────────────────────────────────────────────────┐
│  TOP BAR:  ◉ JARVIS  ·  seL4 · Ring 0  ·  Gemma 4 E2B  ·  5.2 tok/s  ·  ⏱ uptime · clock │
├───────────────┬──────────────────────────────────┬─────────────────────┤
│  SYSTEM       │   CONVERSATION (the centerpiece)  │   AI ENGINE         │
│  vitals       │                                   │   model · tok/s     │
│  · 8 cores    │   user> ...                        │   cache 85.7% hit   │
│  · 32GB RAM   │   JARVIS> streaming tokens…▌        │   threadpool 5 cores│
│  · NVMe 2TB   │                                   │   ── AGENTS ──      │
│  · uptime     │   [CACHE HIT <1ms] / [→ LLM]       │   Device  Network   │
│  · queries    │                                   │   Filesys User      │
│               │                                   │   ── SHIELD ──      │
│  (arc CORE)   │                                   │   risk ▁▂▃ monitoring│
├───────────────┴──────────────────────────────────┴─────────────────────┤
│  BOTTOM TICKER:  IPC events · decisions · boot log …                    │
└──────────────────────────────────────────────────────────────────────┘
```

## How to iterate in Claude design

1. Open any `jarvis-ui-*.html` in this folder in a browser to see the three directions.
2. Paste your favorite HTML file into claude.ai and say *"iterate on this"* — or paste **this brief**
   and say *"build this,"* or *"combine the Arc HUD core with Mission Control's readability."*
3. Good next prompts: *"add a settings/agents screen,"* *"show a multi-turn conversation,"*
   *"design the boot sequence,"* *"make a 1920×1080 variant,"* *"reduce to only framebuffer-native
   primitives (no glow/gradients) so it maps 1:1 to what we can draw."*

## Honesty guardrails (keep these true in any version)

- Don't label the running system "formally verified."
- SHIELD is **monitoring/aspirational**, not a proven "100% blocked" guarantee.
- tok/s figures: ~1.5 single-thread, ~4–6 multi-core (M3 threadpool) — don't inflate to GPU numbers
  (there is no usable GPU; v1.0 is CPU-only).
