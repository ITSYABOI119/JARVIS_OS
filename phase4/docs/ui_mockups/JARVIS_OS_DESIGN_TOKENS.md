# JARVIS OS — Design Tokens (framebuffer port)

Ports the **"Jarvis OS" design system** (built in Claude Design — see `design-system/`) into terms
the bare-metal **framebuffer renderer** can use. The web bundle is React/CSS and can't run on JARVIS
(seL4, C, no browser); this file is the **reusable part** — the exact colors, type rules, spacing,
and voice — so whatever we draw in C matches the design language.

## Two render targets (the spec gives both)

| | **TODAY — VGA text mode** | **TARGET — RGB framebuffer** |
|---|---|---|
| Surface | 80×25 cells @ `0xB8000` (`vga_text.c`) | R9 280X HDMI linear framebuffer (e.g. 1280×720, 32bpp XRGB) |
| Color | **16 fixed colors** (attribute byte `(bg<<4)\|fg`) | **exact 24-bit hex** |
| Font | one fixed 8×16 cell, monospace | bitmap font(s); mono is the brand texture |
| Used by | the boot-log / status screen (v1) | the richer dashboard (north star) |

The current boot log is text mode → use the **VGA-16 nearest** column. When we stand up a real RGB
framebuffer, switch to the exact hex.

---

## 1. Color

Neutral ramp is cool near-black → near-white; **one** electric-blue accent (flat, no gradients);
GitHub-grade semantic status. Depth comes from the surface ramp, **not** color.

| Token | Hex (RGB target) | C constant | VGA-16 nearest (fg index) |
|---|---|---|---|
| canvas (desktop bg) | `#0A0B0E` | `#define JCLR_CANVAS   0x0A0B0Eu` | 0 black |
| app bg | `#101319` | `JCLR_APP      0x101319u` | 0 black |
| surface / panel | `#161A22` | `JCLR_PANEL    0x161A22u` | 0 black / 8 dk-grey border |
| card | `#1D222C` | `JCLR_CARD     0x1D222Cu` | 8 dark grey |
| inset / hover | `#272D39` | `JCLR_HOVER    0x272D39u` | 8 dark grey |
| text primary (high) | `#E8ECF3` | `JCLR_TEXT     0xE8ECF3u` | 15 white |
| text secondary | `#98A2B6` | `JCLR_TEXT2    0x98A2B6u` | 7 light grey |
| text muted | `#6B7589` | `JCLR_MUTED    0x6B7589u` | 8 dark grey |
| **accent** (electric blue) | `#4F7CFF` | `JCLR_ACCENT   0x4F7CFFu` | **9 light blue** |
| accent hover | `#6E9BFF` | `JCLR_ACCENT_H 0x6E9BFFu` | 9 light blue |
| status **ok** | `#3FB950` | `JCLR_OK       0x3FB950u` | 10 light green |
| status **warn** | `#D6A02A` | `JCLR_WARN     0xD6A02Au` | 14 yellow |
| status **err** | `#F2564B` | `JCLR_ERR      0xF2564Bu` | 12 light red |
| status **live** (stream) | `#46C7C7` | `JCLR_LIVE     0x46C7C7u` | 11 light cyan |
| hairline border | `rgba(255,255,255,.07)` | (blend on `JCLR_CANVAS` → ~`0x20242B`) | n/a (use box-draw glyphs) |
| default border | `rgba(255,255,255,.11)` | (~`0x2B3038`) | 8 dark grey |

Status tints (badge/finding backgrounds) are the same hue at ~14% alpha over canvas — on the
framebuffer, precompute the blend; in text mode, skip the wash and just color the glyph/dot.

---

## 2. Type & casing

Mono is used **liberally and intentionally — it is the brand texture.** Text mode is already 100% mono
(one cell), which suits this perfectly.

- **Families (RGB target):** `IBM Plex Sans` (UI/body), `IBM Plex Mono` (all system data/labels/metrics),
  `Space Grotesk` (display headings). These are **web fonts** — for bare metal, pick the closest bitmap
  faces (a clean mono for data; the boot log can be entirely mono). Don't pull from a CDN.
- **Scale (RGB):** 11 / 12 / 13 / **14 (default UI)** / 16 / 20 / 26 / 34 / 46 / 64 px. In text mode there
  is one size — map "display/title" to **emphasis via color/weight or a 2× cell**, not a font size.
- **Casing (use these NOW, even in text mode):**
  - **Sentence case** for labels, headings, buttons — *"New agent"*, *"Active agents"*.
  - **lowercase mono** for identifiers, agent names, paths, metrics — `watcher`, `/agents/planner`, `18ms`.
  - **UPPERCASE mono** only for eyebrows/section kickers and badges — `SYSTEM · DAEMON`, `LIVE`, `OK`, `PASS`.
  - Numbers + units always paired and mono — `12%`, `4.2 GB`, `5.2 tok/s`, `184 ms`; **value bright, unit muted**.
- **No emoji, ever.** Status is carried by colored dots + badges, never glyphs. No exclamation marks —
  urgency is the `warn`/`err` color, not punctuation.

---

## 3. Spacing, layout, shape

- **4px base grid.** RGB honors it exactly; **text mode snaps to whole cells** (8px horizontal / 16px
  vertical). Scale: 4 / 8 / 12 / 16 / 20 / 24 / 32 / 40 / 48 / 64 / 80 px.
- **OS chrome dims** (RGB target → text-mode cells @ 8×16):
  - icon rail `56px` → **7 cols** · context sidebar `248px` → **31 cols** · topbar `44px` → **≈3 rows**.
  - Dividers are **hairline borders, not shadows** (Swiss). In text mode use box-drawing glyphs
    `─ │ ┌ ┐ └ ┘ ├ ┤ ┬ ┴ ┼`; in RGB, 1px `border-default` lines.
- **Radii (RGB only):** xs 3 / sm 5 / md 8 / lg 12 / xl 16 / full. Controls use `sm`; cards use `lg`.
  Text mode has no radii (cells are square) — that's fine, it reads as "technical."
- **Cards (RGB):** `JCLR_CARD` fill, 1px default border, radius-lg, subtle shadow. Optional hairline-divided
  header (title + mono subtitle + right slot).
- **Elevation (RGB):** low-alpha black shadows `sm 0 1 2 / md 0 4 12 / lg 0 12 32`. Accent **glow**
  (`0 0 16px` accent tint) only on the logo + assistant avatar. Text mode: no shadows.

---

## 4. Status, motion, voice

- **StatusDot** → a colored `●`: ok=green, warn=amber, err=red, **live=cyan with a pulse**. The pulse
  (blink) is the *only* looping animation; respect reduced-motion. In text mode this is a colored bullet
  that toggles bright/dim.
- **Motion (RGB):** quick, **no bounce** — `fast 120ms / base 180ms / slow 280ms`,
  ease `cubic-bezier(0.22,1,0.36,1)`. Boot-log line reveal + the live dot pulse are enough for v1.
- **Voice (the "Operator" register — usable verbatim now):** calm, precise, flight-deck — *not* chatty.
  Address the user as **"you" / "Operator"**; the system is **"Jarvis"**, states status impersonally:
  *"Good morning, Operator."* · *"System · 09:41 · all systems nominal."* · *"2 items worth your attention."*

---

## 5. Applying it to the v1 boot-log screen (`jarvis-ui-bootlog.html`)

Concrete mapping for what we draw first, in text mode:
- background `JCLR_CANVAS 0x0A0B0E` (VGA black) · primary text white · labels/tags muted grey.
- `PASS` / `READY` → `JCLR_OK` green (VGA 10) · `[ tag ]` → muted · values → white.
- the `● ONLINE` status dot → green `●`; a `live` stream → cyan `●` pulsing.
- accent `#4F7CFF` (VGA light-blue 9) for the `JARVIS` wordmark / the one highlighted element.
- everything mono, sentence-case prose, lowercase-mono identifiers, UPPERCASE eyebrows. No emoji.

> Source bundle: `phase4/docs/ui_mockups/design-system/` (open `Jarvis OS.html`, or browse
> `bundle/tokens/*.css` + `bundle/ui_kits/jarvis-os/`). Values there are Claude-Design *proposals* —
> reconcile against the real hardware (the framebuffer res, the bitmap font we ship) as we build.
