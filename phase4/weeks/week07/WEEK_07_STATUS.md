# Week 07 Status: Phase 4 Goal #2 (Graphical Output) — UEFI migration + FIRST PIXELS

**Date:** June 19, 2026
**Status:** Goal #2 **Steps 1 → 2c-2c DONE (2026-06-18 → 2026-06-20)** — UEFI migration, first pixels, bitmap-font status panel, observability ([SNAP]+T+ms), scrolling event log, and per-state HUD chrome (header band + STATE badge + ROUTE + COUNTERS + natural oldest→newest scroll). The R9 280X monitor shows a live, styled JARVIS HUD driven by seL4. **Goal #2 display is v1-complete** (remaining backlog optional).
**Phase:** Phase 4 — Production (v1.0). Goal #1 (inference perf, CPU) COMPLETE at M4 (week06); goal #2 (display) is the current workstream. Hardware-in-the-loop on the JARVIS PC (R9 280X, UEFI/GOP).

---

## Summary

Goal #2 is **plumbing-and-a-blitter, not a GPU driver**: the UEFI GOP firmware already leaves the R9 280X in a live 1920×1080×32 linear framebuffer at `0xE0000000`, and seL4 already forwards it to the rootserver as multiboot2 bootinfo **id=4** — it was just never requested. The work was risk-isolated into three steps so a boot-mode/kernel change couldn't silently break the goal-#1 stack. Each step was adversarially reviewed before the (expensive) bare-metal boot.

## Step 1 — UEFI boot migration (DONE 2026-06-18, commit `14b213f`)

Switched the boot from legacy BIOS/multiboot1 to **UEFI/multiboot2** — the v1.0 standard (CSM is sunset; the installer/release need it; GOP is the framebuffer path). grub `default` → the UEFI/`efi_gop` entry; `reflash_usb.sh` + `create_boot_usb.sh` now **default to UEFI** (BIOS kept as an explicit `--bios`/`--bios-only` fallback) so routine reflashes don't silently revert; `BARE_METAL_BOOT_GUIDE.md` rewritten (CSM off, Secure Boot OS Type "Other OS"). **Verified bare metal:** the full stack survives UEFI — self-test 5/5, Gemma 4 E2B 2962 MB loaded over NVMe, M3 `NUM_NODES=6`, coherent inference, err=0 over q=300+, ~5.5 tok/s (goal-#1 perf holds under UEFI). Validated under OVMF first, then on hardware.

## Step 2a — FB plumbing (DONE 2026-06-18)

Kernel **MB2 type-5 framebuffer-request tag** (an idempotent `multiboot.S` patch injected by `build_jarvis_x86.sh`; request is **0/0/0 = "no preference"** + grub `set gfxpayload=keep` to keep the firmware GOP mode) + a rootserver **id=4 consumer** that reads + logs the forwarded `seL4_X86_BootInfo_fb_t`. Validated under OVMF: the kernel prints "Got framebuffer info in multiboot2 ... type=1" and the rootserver prints "[JARVIS] FB: addr=... type=1". No drawing. Review caught a would-be compile failure: `seL4_X86_BootInfo_fb_t` is an **incomplete** type in userspace (the payload struct lives in a kernel-internal header), so the consumer mirrors the layout locally.

## Step 2b — first pixels (DONE 2026-06-19) 🎉

Added a minimal blitter and drew a test pattern on the real monitor:
- **`phase3/src/drivers/framebuffer.c/h`** (NEW) — 32bpp linear, pitch-addressed blitter: `fb_init`/`fb_putpixel`/`fb_fill_rect`/`fb_clear`/`fb_flush`, plus a runtime R↔B swap helper. seL4-only (no host test, like `threadpool_sel4.c`).
- **`main_x86.c`** — after the id=4 consumer: **dump every device-untyped** (so a failed map shows whether `[addr, addr+pitch*height)` is covered), **map the FB Uncacheable**, then **draw the test pattern**. All **NON-FATAL** (any failure logs and continues to NVMe/Gemma/M3).

**Verified on the R9 280X (boot_id=1 NVMe log + photo, independently confirmed):** the **6-rect test pattern renders** (dark canvas + RED/GREEN/BLUE byte-order row + accent/ok/err row). **Byte order is BGRX** — the top-left rect shows RED with the default `fb_set_rb_swap(0)`, so **no R↔B swap** is needed (baked into 2c). The descriptor was the real **1920×1080×32 type=1 @ ~0xE0000000**; a device-untyped covered the FB region; the UC map succeeded across ~2025 pages. **No regression:** self-test 5/5, **Gemma 2962 MB / 758480 pages**, M3 NN=6, coherent inference, **err=0 over q=100/200**.

### The bug adversarial review caught (would have been a dark monitor)

The FB was first written to map **Write-Combining** via `vspace_map_pages(..., seL4_X86_WriteCombining)`. But `vspace_map_pages`' last arg is `int cacheable` (libsel4utils maps `1 → WriteBack`, `0 → Uncached`), **not** a `seL4_X86_VMAttributes` — so passing the WC enum value (4) is *truthy* → a **WriteBack/cacheable** mapping, which the GPU scanout never sees (no cache snoop) → blank screen. Fixed by passing **`0` (Uncacheable)** — MTRR-independent and guaranteed visible, the same device-memory mapping the NVMe BAR0/DMA path uses. True WriteCombining (faster) needs a raw `seL4_X86_Page_Map` path — deferred to 2c.

## Step 2c-1 — bitmap font + LIVE status panel (DONE 2026-06-20) 🎉

The R9 280X monitor now shows a live **status panel** (8×16 bitmap font) on the **1024×768×32** GOP framebuffer — replacing the 2b test pattern. Fixed-position fields (Title / Display / Model / Threads / Queries / Last), updated in place (UC FB → strip-clear + redraw). All updates stay **off the per-token path**:
- **Model** `[loading N%]` driven by the **NVMe read counter** (the slow phase; the first cut wrongly tracked the ~2 s frame-alloc loop and froze at 98%). Counter includes FAT-lookup sectors → **clamped to 100**; then green **`[loaded]`**.
- **Queries** ticks **per query** via a draw-only twin (`fb_status_line_quiet`) so it can't flood the no-wrap log; **Last** refreshes **per inference**.
- **`[PANEL]` log mirror:** `fb_status_line` mirrors its text to the log as `[PANEL] <text>` (BOOT_LOG=1 → durable NVMe), so the on-screen `%` climb is diagnosable off-box — the new standing rule "all on-screen UI must mirror to the log."
- **`framebuffer.c/h`:** 8×16 font + `fb_draw_char`/`fb_draw_text`/`fb_font_glyph`; **host-tested** `test_framebuffer.c` (15/15) + CI step.
- **Relog resolved:** FB descriptor re-logged after `nvme_log_init` → `FB 1024x768x32 … UC mapped pages=768` durable.

**Verified ENTIRELY from the boot_id log** (UI-reconstructable-from-log): 700-query run, `[STATS] err=0` throughout; Model `%` climb + green `[loaded]` as `[PANEL]` lines; **0** per-query `[PANEL] Queries` flood; self-test 5/5; M3 `NUM_NODES=6` (5 workers); `FB 1024x768x32 … pages=768` relog.

## Step 2c-2a — observability: [SNAP] log line + T+ms (DONE 2026-06-20)

LOG-ONLY (`main_x86.c`) — runs with or without a framebuffer, fully verified on the QEMU serial smoke (no physical boot needed). Adds:
- **`[SNAP]` full-state line** — `[SNAP] T+<ms> disp=<WxHxBPP|no-fb> model=<loading|loaded> NN=<n> q=<n> err=<n> last="…"`, written durable (NVMe log) + serial, at init and at the `[STATS]` every-100q point.
- **Boot-relative `T+<ms>`** (TSC→ms, approximate — invariant TSC ~3.7 GHz) prepended to the `[STATS]` serial line + the `LOG_IPC_STATS` entry → telemetry now carries **what AND when**.

**Verified on QEMU serial** (`BOOT_LOG=0`, `-smp 6`, **q=500, err=0**): `[SNAP]` at init + every 100q with climbing `T+ms`/`q`/`err`/`last`; `[STATS]` carries `T+ms`; no regression (5/5, model load, M3 NN=6, coherent inference). The box build gate caught a real bug first — the TSC helper sat next to the embedded-model `rdtsc()` under `#ifdef JARVIS_HAS_MODEL` (OFF in the NVMe build) and compiled out; relocated to unconditional file scope with its own `jarvis_rdtsc`. This makes every later UI slice reconstructable from the log (the standing UI-mirror rule).

## Step 2c-2b — jarvis_ui_tokens.h + scrolling event-log (DONE 2026-06-20)

- **Byte-identical token refactor:** main_x86.c `#include`s `jarvis_ui_tokens.h`; the inline FBP_*/FBP_Y_* block is deleted (single source, panel doesn't shift). build_jarvis_x86.sh copies the header to src/ + src/drivers/ so the out-of-tree `#include` resolves.
- **No-scroll event-log ring** (`fb_log_reset`/`append`/`count`/`row` in framebuffer.c): last 26 completed serial lines, each written in place at `count % 26` (`>`=head) — the UC framebuffer can't cheaply memmove-scroll. Fed from `putc_serial` completed lines (low cadence, never per-token, no recursion). 1px JCLR_LINE bordered "EVENT LOG" region.
- **Verified:** host **24/24** (T8 wrap/truncate, T9 render-at-cell/cursor/no-OOB); box build gate caught a `*/`-in-comment bug (fixed); QEMU no-regression; **physical boot PASS** — event-log renders + scrolls, panel unchanged, boot_id log 5/5 / Gemma 2962 MB / M3 NN=6 / `[STATS] err=0` q=400 / FB 1024×768×32 pages=768. No-scroll order accepted v1; natural scroll → 2c-2c.

## Step 2c-2c — per-state HUD styling + natural-order scroll (DONE 2026-06-20) 🎉

The styled HUD with a real terminal-style scroll — **goal #2 display is v1-complete.**
- **Per-state chrome (`main_x86.c`):** header band + right-aligned **STATE badge** (`fb_badge` — partial-width so it never wipes the wordmark; 6×6 colored dot + UPPERCASE label) `BOOTING`(blue)→`READY`(green)→`ERROR`(red), driven purely by `nvme_model_loaded`/`q_errors`; a **ROUTE** ratio line (`CACHE …% | -> LLM …%`) and a **COUNTERS** strip (the six real loop counters) via the quiet draw-only twins. Drawn **only at the `[STATS]` q%100 tick** (never per-query) → no `[PANEL]` flood; reconstructable from `[SNAP]`+`[STATS]`. Gated on `JARVIS_DBG_STATS` (ON).
- **Natural-order scroll (`framebuffer.c/h`):** replaced the no-scroll ring with a full-window repaint (`fb_log_repaint` + `fb_log_visible`) — oldest top, newest bottom, `>` on the newest. UC FB never memmove'd (shadow buffer rejected as over-engineering; ~3 ms repaint at deploy cadence).
- **Tokens (`jarvis_ui_tokens.h`):** collisions fixed (ROUTE 10→11, COUNTERS 43→41), `JUI_SHIELD_ROW` removed (collided with the EVENT LOG label), header geometry added. Banner `v0.2.1-dev`→`v0.2.1-beta`.
- **Honesty:** no faked widgets — no CPU meter / agent grid / SHIELD bar / tok/s / queue depth / progress bar (no live source). SHIELD is passive (PB ALLOWs; `shield.c` not linked) → `shield=` is a count, not "blocked."
- **Verified:** host **35/35** (`-Wall -Werror`); adversarial review (7 lenses) GO no-fixes; box build clean; QEMU no-regression + banner `v0.2.1-beta`; **physical boot PASS** (photos + boot_id=1 log) — natural scroll, BOOTING→READY badge, ROUTE+COUNTERS in the free bands with no overlap, no faked widgets; log: 5/5, FB 1024×768×32 @0xe0000000 UC pages=768, Gemma 2962 MB, M3 NN=6, **`[STATS] err=0` through q=1200**, 0 faults, 2376 entries checksum OK.

## fat32 FAT-sector cache + exact data-only load % (DONE 2026-06-21)

A goal-#2 backlog item — boot-speed + an honest load %.
- **FAT-sector cache (`fat32.c/h`):** `fat32_fs_t.fat_cache` caches the last FAT sector; `fat32_next_cluster` now reads each FAT sector once instead of up to ~128× per chain (read-only FS → never invalidated). ~160 MB less redundant NVMe I/O on the Gemma load.
- **Exact data-only % (`fat32.c` + `main_x86.c`):** a `fat32_read_file` progress hook (`fat32_fs_t.progress`) reports cumulative DATA bytes; `model_load_progress` (throttled on pct-change, ≤101 `[PANEL]` lines) drives the Model field. Reaches **exactly 100%** at completion instead of clamping early off the all-sectors NVMe counter. The dead `g_model_total_sectors` was removed; the "NNN MB read" throughput telemetry is kept (honest).
- **Verified:** host **10/10** (new `test_fat_sector_cached` = 1 FAT read not 2; `test_read_file_progress` = done==file_size; multi-cluster read still passes *with* the cache); **3× adversarial review GO** (cross-FAT-sector-boundary reload proven correct — no chain corruption); box build clean (NN=6); QEMU smoke coherent inference (cache non-corrupting); **physical boot (real Gemma, boot_id=1 log):** `Model … [loading 96%→100%] → [loaded]` exact, `"MB read"` ~2929 / ~2962 (down from ~3125), GGUF OK, 5/5, FB 1024×768×32 pages=768, M3 NN=6, `[STATS] q=100 err=0`, 0 faults, 421 entries checksum OK.
- **Noted (not done here):** a pre-existing 512-byte-sector-only assumption in `fat32_next_cluster` — LOW-pri hardening to reject `bytes_per_sector != 512` in `fat32_init` (SEC-029-guarded, never fires on the box).

## Next — goal #2 backlog (optional) → goals #3–7

Goal #2 display is **v1-complete**; the fat32 cache + exact % shipped. Remaining goal-#2 items are all optional/deferred:
1. **Optional** NVMe progress bar (on the new exact %), true-WC mapping (raw `seL4_X86_Page_Map`), 1920×1080 GRUB `gfxmode`.
2. **LOW-pri** hardening: reject `bytes_per_sector != 512` in `fat32_init`.

Then Phase 4 **goals #3–7**: xHCI USB keyboard input, one-script installer, 90-day soak, docs, `v1.0.0` MIT release.

---

*Phase 4 cadence: weekly status at `phase4/weeks/weekN/WEEK_N_STATUS.md`. Goal #1 (inference perf, CPU) COMPLETE — see week06 / `PHASE_4_GOAL1_BENCHMARK.md`. This is goal #2 of Phase 4 goals #2–7 (display, input, installer, 90-day soak, docs, `v1.0.0`).*
