# Phase 4 — Goal #2: Graphical Output (Framebuffer / HDMI Status UI)

**Status:** PLAN + Step 1 in progress (UEFI boot migration). No framebuffer/UI code yet (that is Step 2).
**Date:** 2026-06-18
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

### Step 1 — UEFI boot migration + full-stack re-validation  *(THIS task; no graphics code)*
Switch the boot from legacy BIOS/multiboot1 to **UEFI GRUB** and prove the existing stack survives **before** touching the framebuffer. The monitor will be **dark** under UEFI (no VGA text console exists until the FB UI is built — this is expected, not a failure), so **all validation is via the durable NVMe telemetry log**, not the screen.

- Reflash the USB with UEFI GRUB (`create_boot_usb.sh --uefi-only`, `x86_64-efi`), default boot entry = the multiboot2/UEFI entry (`multiboot2`/`module2` + `insmod efi_gop`/`all_video`).
- Capture config for this run only: `JARVIS_DBG_BOOT_LOG=1` + `JARVIS_M1_MEASURE=1` (full boot→load→inference→`[STATS]`→tok/s timeline to NVMe). Build at `NUM_NODES=6`.
- **Pre-req confirmed (this repo):** the rootserver's `0xB8000` VGA map (`main_x86.c:1196–1219`) is **already non-fatal** — on `vka_alloc_frame_at` failure it prints and continues with `vga_ready=0`, and `putc_serial` only mirrors to VGA `if (vga_ready)` (`main_x86.c:69,191`). So a dark UEFI boot cannot fault on an unmapped VGA window; no guard change is required.
- **Validate from the NVMe log:** rootserver banner + self-test 5/5; **NVMe detected + Gemma loaded** (the big risk — UEFI memory map / device-untyped coverage / the `0xCF8` IOPort PCI cap); M3 workers (`NUM_NODES=6`) + **coherent** inference; `[STATS] q=100 err=0`; **tok/s ≈ 5.46** (goal-#1 perf holds under UEFI).
- **Exit criteria:** the whole stack is byte-for-byte functionally intact under UEFI (load + coherent gen + err=0 + ~5.46 tok/s). **Gate: report results for human review before committing.** Then commit the UEFI migration (the `create_boot_usb` default → UEFI; the VGA map is already non-fatal) with the capture flags reset to 0.

### Step 2 — framebuffer plumbing + blitter + status UI  *(NEXT task, after Step 1 is validated + committed)*
Only once UEFI boot is proven:

1. **MB2 type-5 framebuffer-request tag** in the kernel's multiboot2 header (`multiboot.S` — today the MB2 header is **end-tag-only**; add the `type=5` tag requesting linear 1920×1080×32) so the firmware/GRUB hands the GOP framebuffer to seL4, which forwards it as **id=4**.
2. **Rootserver id=4 consumer:** read the `seL4_X86_BootInfo_fb_t` (addr/pitch/width/height/bpp/type) from the forwarded bootinfo extra-header, and **map the framebuffer** (~8.3 MB / ~2025 4 KB frames at `0xE0000000`) by **reusing the existing NVMe-BAR0 device-frame map pattern** (`main_x86.c:1273–1308`: `vka_alloc_frame_at(paddr+i*4096)` per page → `vspace_map_pages`). Confirm device-untyped coverage spans the full FB region.
3. **Pixel blitter + bitmap font:** a `fb_putpixel`/`fb_fill`/`fb_blit` core and an 8×16 bitmap font (augmenting `vga_text.c`'s text role), then a **status-screen renderer** that draws the boot-log → live status UI per `phase4/docs/ui_mockups/JARVIS_OS_DESIGN_TOKENS.md`.
4. **Confirm pixel format (BGRX vs RGBX)** from the id=4 `type` field + a known-color test pixel **before** trusting the design-token hex colors.

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
- **Device-untyped coverage for the FB.** The ~8.3 MB framebuffer at `0xE0000000` must be covered by a device-untyped the rootserver can retype (Step 2). If `vka_alloc_frame_at` can't reach part of the region, the map fails — verify coverage when wiring the id=4 consumer.
- **Pixel format (BGRX vs RGBX) + pitch ≠ width×bpp.** GOP pixel order and a pitch larger than `width*4` (scanline padding) must be read from the id=4 record and confirmed with a test pixel **before** trusting design-token colors.

## 6. Honesty / verification stance

- **DISPLAY only.** This goal renders a UI on a firmware-initialized framebuffer; it is **not** GPU compute. GPU inference remains deferred (ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`).
- The running build (`KernelFastpath=ON` + `KernelFPU=XSAVE`/AVX + SMP `NUM_NODES=6`) is **functional-but-unverified by design** (ADR `docs/decisions/2026-06-16-x86-verification-stance.md`). Adopting UEFI boot does not change that stance. **Do not describe the running configuration as "formally verified."**
- Every Step-1 validation claim must come from real box output (NVMe log / serial), never fabricated, and is gated on human review before commit.

---

*Engineering log: `phase4/weeks/weekN/`. Goal #1 (inference perf, CPU) is COMPLETE — see `PHASE_4_GOAL1_BENCHMARK.md`. This is goal #2 of Phase 4 goals #2–7 (display, input, installer, 90-day soak, docs, `v1.0.0`).*
