# Phase 4 — Goal #2: Graphical Output (Framebuffer / HDMI Status UI)

**Status:** **Step 1 (UEFI boot migration) DONE 2026-06-18; Step 2a (FB plumbing) + Step 2b (first pixels) DONE 2026-06-19; Step 2c-1 (bitmap font + LIVE status panel) DONE 2026-06-20; Step 2c-2a (observability: [SNAP] log line + T+ms) DONE 2026-06-20; Step 2c-2b (jarvis_ui_tokens.h + scrolling event-log) DONE 2026-06-20; Step 2c-2c (per-state HUD styling: header band + STATE badge + ROUTE + COUNTERS + natural oldest→newest scroll + banner v0.2.1-beta) DONE 2026-06-20.** Goal #2 display is **v1-complete** — remaining backlog is optional (fat32 FAT-sector cache, NVMe progress bar, true-WC / 1920×1080).
**Date:** 2026-06-18 (updated 2026-06-20)
**Goal:** Move beyond the 80×25 VGA text console to a real **graphical framebuffer status UI** on the JARVIS PC monitor — boot progress, model load, query/response, tok/s, SHIELD/err state — rendered from the design tokens in `phase4/docs/ui_mockups/JARVIS_OS_DESIGN_TOKENS.md`. ROADMAP Phase 4 goal #2.
**Scope:** **DISPLAY only.** GPU *compute* (inference) stays deferred (ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`). This goal does not touch the R9 280X as a compute device — only as the firmware-initialized framebuffer it already is.
**Sources:** 3-agent feasibility exploration (2026-06-18): box probe (`fb0` under Linux), seL4 14.0.0 kernel source read, JARVIS rootserver read. Where a web claim conflicts with the box/source read, the read wins.

---

## 1. Verdict — this is plumbing-and-a-blitter, NOT a GPU driver

The single most important finding: **we do not need to write a GPU driver (no GCN command processor, no amdgpu, no Mesa/Vulkan).** The firmware has already done the hard part.

- **The UEFI GOP firmware leaves the R9 280X in a live, writable linear framebuffer.** Proven on the box: under Ubuntu the display is `fb0 = simpledrmdrmfb` at **1920×1080×32**, base **`0xE0000000`**, driven by **`simpledrm`** — the generic "the firmware already set up a framebuffer, just blit to it" driver — **NOT `amdgpu`**. simpledrm working is the proof that a dumb linear writeable framebuffer exists at that address with no device-specific programming. Writing a 32-bit pixel to `fb_base + y*pitch + x*4` lights a pixel. That is the entire "driver."
- **seL4 already has the machinery to hand that framebuffer to the rootserver** — it is just disabled. The x86 kernel parses a multiboot2 framebuffer tag and forwards it to userspace as a bootinfo extra-header `SEL4_BOOTINFO_HEADER_X86_FRAMEBUFFER` (**id = 4**), a `seL4_X86_BootInfo_fb_t` record (`addr/pitch/width/height/bpp/type`). Today our boot path delivers no framebuffer tag, so the kernel sets graphics mode NONE and forwards nothing.

So goal #2 is three small, well-understood pieces: (1) make the boot deliver a framebuffer to the kernel, (2) consume the forwarded id=4 record in the rootserver and map the framebuffer (reusing the existing device-frame map pattern), (3) write a pixel blitter + bitmap font + a status-screen renderer. No new hardware bring-up.

## 2. Decision — UEFI + GOP (not legacy BIOS + VBE)

There are two ways to get a framebuffer tag to seL4. We choose the **UEFI + GOP** path.

| Path | How | Verdict |
|------|-----|---------|
| **UEFI + GOP** *(CHOSEN — release path)* | Boot via UEFI GRUB (`x86_64-efi`); GRUB's `efi_gop`/`all_video` + a multiboot2 **type-5 framebuffer-request tag** in the kernel's MB2 header → firmware GOP framebuffer forwarded as id=4. | The GOP framebuffer is the one **proven live on the box** (`0xE0000000`, simpledrm). Modern boards are increasingly **UEFI-only / CSM-sunset**, so the goal-#4 one-script installer and the goal-#7 public release *must* boot UEFI on other people's machines. The JARVIS PC is natively UEFI. This is the durable path. |
| BIOS + VBE (`KernelMultibootGFXModeLinear`, MB1 id=1) | Legacy BIOS/CSM + GRUB VBE linear mode → multiboot1 framebuffer. | **Throwaway-demo shortcut only.** Works today (we already boot legacy BIOS/multiboot1), but CSM is being removed from modern firmware; a CSM-only display would break the installer and release on UEFI-only boards. **Not the release path.** |

**Consequence that drives Step 1:** today JARVIS boots **legacy BIOS / multiboot1** (`create_boot_usb.sh` / `reflash_usb.sh` install GRUB with `--target=i386-pc`). Adopting UEFI+GOP is a **boot-mode change**, and a boot-mode change can perturb the memory map, device-untyped layout, and PCI access that the *entire existing stack* (NVMe model load, IPC, inference) depends on. Therefore the migration is **risk-isolated as its own step and fully re-validated before any UI code is written.**

## 3. Two-step, risk-isolated plan

### Step 1 — UEFI boot migration + full-stack re-validation  — ✅ **DONE (2026-06-18)**

> **RESULT (bare-metal UEFI, NVMe log boot_id=1, independently verified):** the full existing stack survives the UEFI boot — self-test **5/5**, **real Gemma 4 E2B (2962 MB) loaded over NVMe**, **M3 `NUM_NODES=6` (5 workers)**, **coherent** inference, **`err=0` across `[STATS]` q=100/200/300** (ran to q=382), **0 kernel faults**, decode **~5.53 tok/s** (117 samples; matches M3's 5.46 → **goal-#1 perf holds under UEFI**). Validated first under **OVMF** (GRUB-UEFI → multiboot2 → rootserver → NVMe → model → M3 → inference), then confirmed on real hardware. **Durability:** grub `default → 1` (the multiboot2/efi_gop UEFI entry) + `reflash_usb.sh` and `create_boot_usb.sh` now default to UEFI (BIOS kept as an explicit `--bios`/`--bios-only` fallback) so routine reflashes don't silently revert the migration; `BARE_METAL_BOOT_GUIDE.md` rewritten to the verified UEFI procedure (CSM off, Secure Boot OS Type "Other OS"). The `0xB8000` VGA map was already non-fatal (`main_x86.c:1196–1219`, `vga_ready` default 0) — no rootserver change needed. **Step 2 (the framebuffer UI) is next.**

Switch the boot from legacy BIOS/multiboot1 to **UEFI GRUB** and prove the existing stack survives **before** touching the framebuffer. The monitor will be **dark** under UEFI (no VGA text console exists until the FB UI is built — this is expected, not a failure), so **all validation is via the durable NVMe telemetry log**, not the screen.

- Reflash the USB with UEFI GRUB (`create_boot_usb.sh --uefi-only`, `x86_64-efi`), default boot entry = the multiboot2/UEFI entry (`multiboot2`/`module2` + `insmod efi_gop`/`all_video`).
- Capture config for this run only: `JARVIS_DBG_BOOT_LOG=1` + `JARVIS_M1_MEASURE=1` (full boot→load→inference→`[STATS]`→tok/s timeline to NVMe). Build at `NUM_NODES=6`.
- **Pre-req confirmed (this repo):** the rootserver's `0xB8000` VGA map (`main_x86.c:1196–1219`) is **already non-fatal** — on `vka_alloc_frame_at` failure it prints and continues with `vga_ready=0`, and `putc_serial` only mirrors to VGA `if (vga_ready)` (`main_x86.c:69,191`). So a dark UEFI boot cannot fault on an unmapped VGA window; no guard change is required.
- **Validate from the NVMe log:** rootserver banner + self-test 5/5; **NVMe detected + Gemma loaded** (the big risk — UEFI memory map / device-untyped coverage / the `0xCF8` IOPort PCI cap); M3 workers (`NUM_NODES=6`) + **coherent** inference; `[STATS] q=100 err=0`; **tok/s ≈ 5.46** (goal-#1 perf holds under UEFI).
- **Exit criteria:** the whole stack is byte-for-byte functionally intact under UEFI (load + coherent gen + err=0 + ~5.46 tok/s). **Gate: report results for human review before committing.** Then commit the UEFI migration (the `create_boot_usb` default → UEFI; the VGA map is already non-fatal) with the capture flags reset to 0.

### Step 2 — framebuffer plumbing + blitter + first pixels
The original three sub-pieces, split into risk-isolated steps so a boot/kernel change couldn't silently break the goal-#1 stack:

#### Step 2a — FB plumbing (MB2 tag + id=4 consumer)  — ✅ **DONE (2026-06-18)**
> Kernel **MB2 type-5 framebuffer-request tag** (`build_jarvis_x86.sh` idempotently patches `multiboot.S`; the request is **0/0/0 = "no preference"**, NOT a forced 1920×1080×32 — it pairs with grub `set gfxpayload=keep` to keep the firmware GOP mode) + a rootserver **id=4 consumer** that reads + logs the forwarded `seL4_X86_BootInfo_fb_t`. Validated under OVMF: kernel "Got framebuffer info ... type=1" + rootserver "[JARVIS] FB: addr=... type=1". No drawing. (Review catch: `seL4_X86_BootInfo_fb_t` is an *incomplete* type in userspace — the payload struct is kernel-internal — so the consumer mirrors the layout locally.)

#### Step 2b — device-untyped dump + FB map + test pattern  — ✅ **DONE (2026-06-19, FIRST PIXELS)**
> **RESULT (R9 280X bare metal, boot_id=1 log + photo, independently verified):** the **6-rect test pattern renders on the monitor** — byte order is **BGRX** (top-left rect shows RED with the default `fb_set_rb_swap(0)`; **no R↔B swap** needed — bake that into 2c). The real descriptor was **1920×1080×32 type=1 @ ~0xE0000000**; a device-untyped covered the FB region; the UC map succeeded across ~2025 pages. **No regression:** self-test 5/5, **Gemma 4 E2B 2962 MB / 758480 pages**, M3 `NUM_NODES=6` (5 workers), coherent inference, **err=0 over q=100/200**.
> - `framebuffer.c/h`: a 32bpp linear, pitch-addressed blitter (`fb_init`/`fb_putpixel`/`fb_fill_rect`/`fb_clear`/`fb_flush`). seL4-only (no host test).
> - `main_x86.c`: parse id=4 → dump device-untypeds (so a failed map shows whether `[addr, addr+pitch*height)` is covered) → map the FB **Uncacheable** → draw the pattern. All **NON-FATAL**.
> - **Mapped UNCACHEABLE, not WriteCombining:** `vspace_map_pages`' last arg is `int cacheable` (libsel4utils: `1 → WriteBack`, `0 → Uncached`), **not** a `seL4_X86_VMAttributes` — so passing `0` gives UC, MTRR-independent and guaranteed visible. (Review caught that passing `seL4_X86_WriteCombining`=4 is *truthy* → WriteBack → no pixels.) True WriteCombining (faster) needs a raw `seL4_X86_Page_Map` path → deferred to 2c perf.

#### Step 2c-1 — bitmap font + LIVE status panel  — ✅ **DONE (2026-06-20)**
> **RESULT (JARVIS PC, R9 280X — verified ENTIRELY from the boot_id log, per the "UI reconstructable from the log" rule):** an 8×16 bitmap-font **status panel** replaces the 2b test pattern on the **1024×768×32** GOP framebuffer (the box's actual GOP mode — the 1920×1080 cited for Step 2b below was the Linux/simpledrm value; corrected from the boot_id relog). Fixed-position fields (Title / Display / Model / Threads / Queries / Last), drawn once and updated **in place** (UC FB → strip-clear + redraw, never a scroll). All updates are **off the per-token path**:
> - **Model** shows an ascending **`[loading N%]`** driven by the **NVMe read counter** — the slow phase the user waits on, NOT the ~2 s frame-alloc loop (which froze at 98%). The counter includes FAT-lookup sectors so it's **clamped to 100**; then green **`[loaded]`** on GGUF-magic success.
> - **Queries** ticks **per query** (`q=… err=…`, green→red on first error) via a **draw-only twin** (`fb_status_line_quiet`) so it never floods the no-wrap 2700-entry NVMe log.
> - **Last** refreshes on **each new inference response** (~15% of queries).
> - **`[PANEL]` log mirror:** `fb_status_line` mirrors its verbatim text to the log as `[PANEL] <text>` (BOOT_LOG=1 → durable NVMe; deploy BOOT_LOG=0 → serial-only). The `%` climb is now diagnosable off-box — the chokepoint mandated by the "all on-screen UI must mirror to the log" rule.
> - **`framebuffer.c/h`:** 8×16 IBM-VGA font + `fb_draw_char`/`fb_draw_text`/`fb_font_glyph` (edge-clipped, BGRX, no swap). **Host-tested** — `test_framebuffer.c` (15/15) + a CI step, closing the "no host test" gap.
> - **FB-diagnostics relog RESOLVED:** descriptor + map outcome re-logged via `nvme_log_write` after `nvme_log_init` → `FB 1024x768x32 type=1 @0xe0000000 UC mapped pages=768` is durable in the boot_id log.
>
> **From-log verification:** 700-query run, `[STATS] err=0` throughout; Model `%` climb + green `[loaded]` present as `[PANEL]` lines; **0** per-query `[PANEL] Queries` entries (quiet twin works); self-test 5/5; M3 `NUM_NODES=6` (5 workers).

#### Step 2c-2a — observability (log snapshot + timestamps)  — ✅ **DONE (2026-06-20)**
> **LOG-ONLY (no framebuffer/render/token changes), `main_x86.c`.** A one-line full-state snapshot
> `[SNAP] T+<ms> disp=<WxHxBPP|no-fb> model=<loading|loaded> NN=<n> q=<n> err=<n> last="…"` written
> durably (NVMe log) + serial, at init and at the `[STATS]` every-100q point; plus a boot-relative
> **`T+<ms>`** (TSC→ms, APPROXIMATE — invariant TSC ~3.7 GHz) prepended to the `[STATS]` serial line and
> the `LOG_IPC_STATS` entry. Makes every later UI slice "reconstructable from the log — what AND when".
> **Verified on the QEMU serial smoke** (`BOOT_LOG=0`, `-smp 6`, **q=500 err=0**): `[SNAP]` at init + every
> 100q with climbing `T+ms`/`q`/`err`/`last`, `[STATS]` now carries `T+ms`, no regression (5/5, model load,
> M3 NN=6, coherent inference). The build gate caught a real bug first — the TSC helper was placed next to
> the embedded-model `rdtsc()` under `#ifdef JARVIS_HAS_MODEL` (OFF in the NVMe build) and got compiled
> out; relocated to unconditional file scope with its own `jarvis_rdtsc`. (Durable `[SNAP]` in the boot_id
> log to be confirmed at the next bare-metal boot — low-risk, deferred per the slice plan.)

#### Step 2c-2b — jarvis_ui_tokens.h + scrolling event-log region  — ✅ **DONE (2026-06-20)**
> **Token centralization (byte-identical refactor):** main_x86.c now `#include`s `jarvis_ui_tokens.h` and the inline FBP_*/FBP_Y_* block is deleted — the header is the single source (panel does not shift). framebuffer.c uses the JUI_LOG_* geometry + JCLR_* palette; build_jarvis_x86.sh copies the header to src/ and src/drivers/ so the `#include` resolves out-of-tree.
> **Scrolling event-log region:** a **no-scroll line-ring** of the last 26 completed serial lines (the UC framebuffer can't cheaply memmove-scroll, so each new line clears+redraws one row-strip in place at `count % 26`; `>` marks the head). Fed from `putc_serial` completed lines (low cadence — `[STATS]`/`[INFER]`/boot, never per-token; no recursion — fb_log_append never prints). framebuffer.c: `fb_log_reset`/`fb_log_append`/`fb_log_count`/`fb_log_row` + a 1px JCLR_LINE bordered "EVENT LOG" region.
> **Verified:** host test **24/24** (T8 ring wrap/truncate, T9 render-at-cell/`>`-cursor/no-OOB); the box build gate caught a `*/`-in-comment bug (fixed); QEMU no-regression (FB inert → feed/render skipped); **physical boot PASS** — the event-log region renders + scrolls (`>`=head), the six-field panel is unchanged, boot_id log 5/5 / Gemma 2962 MB / M3 NN=6 / `[STATS] err=0` through q=400 / FB 1024×768×32 pages=768. The no-scroll reading-order (overwrite top→bottom) is **accepted for v1**; natural oldest→newest scroll is deferred to 2c-2c.

#### Step 2c-2c — per-state HUD styling + natural-order scroll  — ✅ **DONE (2026-06-20)**
> **RESULT (R9 280X bare metal — photos + independent boot_id=1 log read):** per-state chrome from `phase4/docs/ui_mockups/jarvis-framebuffer-hud.html`, all live/honest, no faked widgets. **Goal #2 display is v1-complete.**
> - **Per-state chrome (`main_x86.c`):** a header band with a right-aligned **STATE badge** — `fb_badge(state,color)`, a partial-width helper (clears a fixed 16-col box at cols [110,126] so it never wipes the wordmark, draws a 6×6 colored dot — the ● substitute, not in the 0x20–0x7E font — + UPPERCASE label) — `BOOTING` (blue) while `!nvme_model_loaded` → `READY` (green) once resident → `ERROR` (red) if `q_errors>0`; a **ROUTE** ratio line (`Route : CACHE …% | -> LLM …% (<1ms hit)`, separator `|` not `·`) and a **COUNTERS** strip (`q=.. hits=.. infer=.. hb=.. shield=.. err=..`, the six real loop counters) via the quiet draw-only twins. State is purely live: `state = !nvme_model_loaded ? BOOTING : (q_errors>0 ? ERROR : READY)`.
> - **Cadence:** badge/route/counters draw **only** at the `[STATS]` q%100 tick (+ the init/workload-entry badge), **never per-query** — no `[PANEL]` flood; reconstructable from `[SNAP]` (model=/err=) + `[STATS]` (hits/infer/err). Gated on `JARVIS_DBG_STATS` (committed ON).
> - **Natural-order scroll (`framebuffer.c/h`):** the 2c-2b no-scroll line-ring is replaced by a full-window repaint — `fb_log_repaint()` maps oldest→top, newest→bottom with `>` on the newest/bottom; new `fb_log_visible(screen_row)` accessor; `fb_log_row` kept for back-compat. The UC FB is **never memmove'd** (a shadow buffer was rejected as over-engineering — ~26-row repaint is ~3.3 MB UC / ~3 ms at the deploy cadence).
> - **Token geometry (`jarvis_ui_tokens.h`):** two real collisions resolved — `JUI_ROUTE_ROW` 10→11 (abutted `FBP_Y_LAST`), `JUI_COUNTERS_ROW` 43→41; `JUI_SHIELD_ROW` REMOVED (collided with the EVENT LOG label at row 13 — do NOT reintroduce); added `JUI_HDR_*` / `JUI_COUNTERS_DIV_Y` header geometry. `JUI_CPU_COL`/`JUI_AGENTS_ROW` kept as [ASPIRATIONAL] markers only.
> - **Banner:** `v0.2.1-dev` → `v0.2.1-beta`.
> - **Omitted (honesty, hard rule):** NO per-core CPU meter, agent grid, SHIELD risk bar, on-panel tok/s, queue depth, NVMe progress bar, or per-token streaming — none have a live source. (SHIELD is passive — PB returns ALLOW, `shield.c` not linked — so `shield=` is a COUNT, not "blocked".)
> - **Verified:** host **35/35** (`-Wall -Werror -I ../sel4`; T8 re-anchored chronological, T8b partial-fill, T9b bottom-pinned `>` on a 128×640 FB); adversarial review (7 lenses A–G) GO no-fixes; box build clean (NN=6); QEMU smoke no-regression (FB inert in QEMU → chrome/log skipped) + banner `v0.2.1-beta`; **physical boot PASS** — natural scroll, BOOTING→READY badge, ROUTE+COUNTERS in the free bands with no panel/log overlap, no faked widgets; boot_id=1 log: 5/5, FB 1024×768×32 @0xe0000000 UC pages=768, Gemma 2962 MB, M3 NN=6, **`[STATS] err=0` through q=1200**, 0 faults.

#### Goal #2 remaining backlog
1. **fat32 FAT-sector cache + exact data-only load %**  — ✅ **DONE (2026-06-21)**
   > A per-fs FAT-sector cache (`fat32_fs_t.fat_cache`) makes `fat32_next_cluster` read each FAT sector once instead of up to ~128× per chain (~160 MB less NVMe I/O); a `fat32_read_file` progress hook (`fat32_fs_t.progress`) drives an EXACT data-only load % (reaches 100% at completion instead of clamping early off the all-sectors NVMe counter); the dead `g_model_total_sectors` was removed. **Host 10/10** (cache-hit + progress tests; the multi-cluster read still passes *with* the cache); 3× adversarial GO; QEMU coherent inference (cache proven non-corrupting on a real model load). **Physical boot (real Gemma, boot_id=1 log):** `[PANEL] Model … [loading 96%→97%→98%→99%→100%] → [loaded]` (exact, no early clamp); `"MB read"` last tick **2929 MB / ~2962 total** (down from ~3125); GGUF loaded OK; 5/5; FB 1024×768×32 pages=768; M3 NN=6; `[STATS] q=100 err=0`; 0 faults; 421 entries checksum OK.
2. **Model-load progress bar** — ✅ **SHIPPED (2026-06-24, this slice).** `fb_progress_bar` on the HUD, drawn in lockstep with the exact data-only `[loading N%]` text via `model_load_progress` (source = `g_model_load_pct`), FBP_ACCENT loading → FBP_OK at 100% — plus a matching live `model_load_pct` bar on the remote console (UI-feature parity; already an `/events` field, no wire-shape change). On-box visual confirm pending the box-verify session.
3. **true-WC mapping** — evaluated, **DEFERRED.** WC is available (libsel4 `seL4_X86_WriteCombining=4`, kernel PAT-4=WC) but NOT worth it for the low-cadence static HUD — only the one-time `fb_clear` benefits; the FB is not blit-bound — so keep the FB UC; revisit if the FB becomes blit-bound. **1920×1080 GRUB `gfxmode`: REQUESTED** (`set gfxmode=1920x1080x32` in `grub.cfg`, best-effort — firmware may ignore it; pending on-box confirm via the boot_id log `FB <W>x<H>x<bpp> pages=N`). The panel stays resolution-agnostic (reads the id=4 descriptor).
4. **`fat32_init` sector-size handling (corrected framing).** `fat32_init` already rejects sizes outside {512,1024,2048,4096} (SEC-028); the only residual is that 1024/2048/4096 are accepted but unsupported by the 512-byte `fat_cache` — they fail safely (SEC-029 returns -1 at the first FAT read). Optional tightening: reject `!= 512` outright. Never fires on the box's 512B-sector NVMe.

## 4. Key files & references

**seL4 kernel (out-of-tree `~/sel4-x86`; the machinery that already exists):**
- `kernel/src/arch/x86/kernel/boot_sys.c:684` — multiboot2 framebuffer-tag parse.
- `kernel/src/arch/x86/kernel/boot.c:201` — forwards the FB to userspace as bootinfo extra-header **id=4**.
- `kernel/src/arch/x86/multiboot.S` — the kernel's MB2 header is **end-tag-only** today → **Step 2** adds the `type=5` framebuffer-request tag here.
- `kernel/src/arch/x86/config.cmake:192` — `KernelMultibootGFXModeLinear` (the BIOS/VBE id=1 alternative; not the chosen path).
- `libsel4/.../bootinfo_types.h:106` — `SEL4_BOOTINFO_HEADER_X86_FRAMEBUFFER` (id=4).
- `libsel4/.../multiboot2.h:36` — `seL4_X86_BootInfo_fb_t { addr, pitch, width, height, bpp, type }`.

**JARVIS rootserver / drivers (this repo):**
- `phase3/src/sel4/main_x86.c:1196–1219` — the VGA `0xB8000` map (already non-fatal; **augment** with the id=4 FB map in Step 2).
- `phase3/src/sel4/main_x86.c:1273–1308` — the NVMe-BAR0 device-frame map pattern (**reuse** for the FB map).
- `phase3/src/drivers/pci.c` — PCI enumeration / BAR address (for locating/confirming the FB device if needed).
- `phase3/src/drivers/vga_text.c` — current text console; the font/text role extends here in Step 2.
- `phase4/docs/ui_mockups/JARVIS_OS_DESIGN_TOKENS.md` — colors/typography/layout the Step-2 renderer targets.

**Hardware (JARVIS PC, lspci-verified):**
- **R9 280X** = `1002:6798` (Tahiti/GCN1), PCI **bus 9**. **BAR0 `0xE0000000`, 256 MB** (VRAM aperture — the linear framebuffer lives here). **BAR2 `0xFCE00000`, 256 KB** (MMIO registers — not needed; we do not program the GPU).
- Firmware GOP framebuffer: **1920×1080×32 linear @ `0xE0000000`**, ~8.3 MB, simpledrm-proven.

## 5. Risks

- **Boot-mode change (the whole point of isolating Step 1).** UEFI vs legacy BIOS can shift the E820/memory map, the device-untyped layout, and PCI/IOPort access. The NVMe model load + inference stack must be **re-validated end-to-end** under UEFI before any UI work. This is why Step 1 ships and is reviewed on its own.
- **No VGA-text fallback under UEFI.** The legacy `0xB8000` text window is absent/unused under pure UEFI, so the **monitor stays dark until the Step-2 FB UI exists**. Validation is serial + NVMe-log only. The `0xB8000` map must stay non-fatal (confirmed already so, §3).
- **Device-untyped coverage for the FB.** The ~8.3 MB framebuffer at `0xE0000000` must be covered by a device-untyped the rootserver can retype (Step 2). If `vka_alloc_frame_at` can't reach part of the region, the map fails — verify coverage when wiring the id=4 consumer. *(RESOLVED 2b: a device untyped covered the FB region; the per-page UC map succeeded across ~2025 pages on the R9 280X.)*
- **Pixel format (BGRX vs RGBX) + pitch ≠ width×bpp.** GOP pixel order and a pitch larger than `width*4` (scanline padding) must be read from the id=4 record and confirmed with a test pixel **before** trusting design-token colors. *(RESOLVED 2b: BGRX — top-left RED with `fb_set_rb_swap(0)`, no swap; blitter is pitch-addressed.)*

## 6. Honesty / verification stance

- **DISPLAY only.** This goal renders a UI on a firmware-initialized framebuffer; it is **not** GPU compute. GPU inference remains deferred (ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`).
- The running build (`KernelFastpath=ON` + `KernelFPU=XSAVE`/AVX + SMP `NUM_NODES=6`) is **functional-but-unverified by design** (ADR `docs/decisions/2026-06-16-x86-verification-stance.md`). Adopting UEFI boot does not change that stance. **Do not describe the running configuration as "formally verified."**
- Every Step-1 validation claim must come from real box output (NVMe log / serial), never fabricated, and is gated on human review before commit.

---

*Engineering log: `phase4/weeks/weekN/`. Goal #1 (inference perf, CPU) is COMPLETE — see `PHASE_4_GOAL1_BENCHMARK.md`. This is goal #2 of Phase 4 goals #2–7 (display, input, installer, 90-day soak, docs, `v1.0.0`).*
