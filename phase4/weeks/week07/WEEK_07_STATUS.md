# Week 07 Status: Phase 4 Goal #2 (Graphical Output) — UEFI migration + FIRST PIXELS

**Date:** June 19, 2026
**Status:** Goal #2 **Step 1 (UEFI boot migration) DONE 2026-06-18**, **Step 2a (FB plumbing) + Step 2b (first pixels) DONE 2026-06-19**. The R9 280X monitor now shows a rendered test pattern driven by seL4 — the first graphical output of JARVIS. Step 2c (bitmap font + status UI) is next.
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

## Next — Step 2c (font + status UI)

1. **Bitmap font + status-screen renderer** from `phase4/docs/ui_mockups/JARVIS_OS_DESIGN_TOKENS.md` (boot-log → live status: model load, query/response, tok/s, err). Use the design-token hex directly (BGRX, no swap).
2. **FB diagnostics are serial-only** — the FB block runs **before `nvme_log_init`**, so the `[JARVIS] FB:` descriptor/dump lines never reach the durable NVMe log. **Re-log the descriptor + map result via a structured `nvme_log_write` after init** so a dark boot is diagnosable from the log, not just serial.
3. **Optional true-WC mapping** (raw `seL4_X86_Page_Map`) if UC fill latency matters.

---

*Phase 4 cadence: weekly status at `phase4/weeks/weekN/WEEK_N_STATUS.md`. Goal #1 (inference perf, CPU) COMPLETE — see week06 / `PHASE_4_GOAL1_BENCHMARK.md`. This is goal #2 of Phase 4 goals #2–7 (display, input, installer, 90-day soak, docs, `v1.0.0`).*
