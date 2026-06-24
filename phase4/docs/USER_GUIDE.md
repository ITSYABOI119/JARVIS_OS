# JARVIS AI-OS — User Guide (v1.0, x86-64)

**Audience:** someone setting up and running JARVIS on the dedicated x86-64 "JARVIS PC" appliance.
**Scope:** the standalone bare-metal seL4 build — the LLM runs **on the box**, and you observe/validate it from a **second machine on the same LAN**.

> **Honesty note up front (this whole guide holds to it):** JARVIS is a real research OS with real limits.
> The running seL4 configuration is **functional-but-unverified by design** (it enables `KernelFastpath=ON` + SMP, outside seL4's verified X64 configuration) — it is **not** "formally verified" as deployed. SHIELD on the live path is **passive/monitoring only** (it does not block). One model, CPU-only, ~5.46 tok/s. See **§14 Known Limitations**.

---

## 1. System Overview

JARVIS is an AI-controlled operating system: the **seL4 microkernel** (Ring 0) handles interrupts/IPC/memory, and an **LLM inference engine** (Ring 3) makes the high-level decisions. On x86-64 it runs **standalone** — unlike the Phase 2 Pi 4 split (AI on a host PC over UART), here **Gemma 4 E2B runs directly on the JARVIS PC** from a model file on its NVMe.

The deployment is a **headless appliance**:

- The **on-box monitor** shows a thin **framebuffer HUD** (status panel, STATE badge, a live model-load progress bar, scrolling event log) at **1024×768** — a local "it's alive" readout.
- The **primary UI is a remote telemetry console**: the box broadcasts its live state over the network (UDP), and a **browser console on a second machine** renders it. The console is **read-only** today (telemetry-out only — no control-in yet).

Two seL4 processes do the work: **Process A** (rootserver — drivers, NVMe model load, decision cache, IPC, the HUD, telemetry emit) and **Process B** (the quantized inference engine), connected by lock-free shared-memory rings.

---

## 2. Hardware

**The JARVIS PC (the appliance):**

| Part | Requirement (reference build) |
|------|-------------------------------|
| CPU | x86-64 with **AVX2** (reference: Ryzen 7 2700X, 8 cores) |
| RAM | **32 GB** (the model + working set need headroom) |
| Storage | **NVMe SSD** (reference: 2 TB Lexar NM790) — holds the model partition |
| GPU/Display | Any GOP-capable output for the monitor (reference: R9 280X — **display only**, not used for inference) |
| NIC | **Intel I211** (for the telemetry broadcast; reference: X470-F onboard) |

**Also needed:**
- A **USB stick ≥ 2 GB** (the boot image; the model is **not** on the stick — it's on NVMe).
- A **second machine on the same LAN** (the console host) — Windows or Linux, with Python 3.

> CPU-only inference. There is **no GPU compute path** in v1.0.

---

## 3. Getting the Model

JARVIS v1.0 ships a **single model: Gemma 4 E2B, Q4_K_M GGUF** (~2.89 GiB).

- On the NVMe it lives on a FAT32 partition labelled **`JARVIS_DATA`**, as the file **`GEMMA2B.GUF`** (the rootserver opens it by its FAT 8.3 short name **`GEMMA2B GUF`** — hardcoded in `main_x86.c`).
- You do **not** copy it manually — the installer (or `phase3/scripts/setup_nvme_partition.sh`) provisions the partition and writes the model. You only need the source `.gguf` on the JARVIS PC (path passed via `--model`, with a sensible default).

---

## 4. Installation

Two equivalent paths — both run on the **JARVIS PC** (Ubuntu) against your built images.

**A) One-script installer (recommended)** — wraps build → boot-USB → NVMe model provision → verify → checklist:

```bash
cd ~/Desktop/JARVIS_OS
# preview everything first (executes nothing destructive):
bash phase3/scripts/install_jarvis_x86.sh --usb /dev/sdX --dry-run
# then for real (only --target usb is supported in v1.0):
sudo HOME=/home/jarvis bash phase3/scripts/install_jarvis_x86.sh --usb /dev/sdX --confirm
```

**B) Just the boot USB** (`create_boot_usb.sh`, the box-confirmed flash command):

```bash
sudo HOME=/home/jarvis bash phase3/scripts/create_boot_usb.sh /dev/sdX --confirm
# then type YES when prompted
```

> ### ⚠️ The `sudo HOME=/home/jarvis` gotcha (important)
> `sudo` resets `$HOME` to `/root`, but the build output lives under `/home/jarvis`. **You must pass `HOME=/home/jarvis`** to the sudo command or the flasher won't find the build dir. This is box-confirmed and the #1 install snag.

The flasher **refuses to write to an NVMe device or the disk backing `/`** — it only writes removable USB. Double-check `/dev/sdX` with `lsblk` first (see §13).

---

## 5. NVMe Model Partition

The rootserver reads the model from a **fixed sector**: the `JARVIS_DATA` FAT32 partition **must start at sector 32768** (`NVME_FAT32_PART_LBA = 32768` in `main_x86.c`). If it starts anywhere else, the box boots but **silently finds no model**.

The installer / `setup_nvme_partition.sh` create it at exactly sector 32768 and **hard-assert** it. To verify by hand:

```bash
sudo parted /dev/nvme0n1 unit s print
# the JARVIS_DATA line's Start must read 32768s
```

The model file on that partition is **`GEMMA2B.GUF`** (8.3 short name `GEMMA2B GUF`); `setup_nvme_partition.sh` validates the short name after copying.

> On an already-provisioned box, the setup detects an existing correct `JARVIS_DATA` (name + start 32768s), refreshes/keeps the model, and **does not repartition** — it never destroys a working layout.

---

## 6. Boot USB

The stick is **UEFI** (GPT + an EFI System Partition with GRUB):

```
USB (GPT)
├── ESP (FAT32)
│   ├── /EFI/BOOT/BOOTX64.EFI      # GRUB (UEFI)
│   └── /boot/grub/grub.cfg        # default=1 → "JARVIS seL4 x86-64 (UEFI)" (multiboot2)
└── boot images: kernel-x86_64-pc99 + sel4test-driver-image-x86_64-pc99
```

The **model is not on the USB** and is not a GRUB module — it's loaded at runtime from the NVMe `JARVIS_DATA` partition.

---

## 7. BIOS / UEFI Settings

On the JARVIS PC firmware (reference: ASUS X470-F):

| Setting | Value | Why |
|---------|-------|-----|
| **CSM (Compatibility Support Module)** | **Disabled** | JARVIS boots pure UEFI; CSM can force legacy/VGA paths |
| **Secure Boot** | **Disabled / "Other OS"** | the images aren't signed for Secure Boot |
| **IOMMU / AMD-Vi** | **Disabled** | the NVMe driver DMAs to physical addresses (no IOMMU translation) |
| **Boot order / F8** | USB first, or use the **F8** one-time boot menu | to select the stick |

---

## 8. First Boot

1. Insert the boot USB; power on.
2. Press **F8** at POST → choose the **`UEFI: <stick>`** entry.
3. GRUB auto-selects the UEFI entry; **seL4 boots in ~2 s**.
4. **The monitor shows the JARVIS HUD at 1024×768×32** — the status panel (Display / Model / Threads / Queries / Last), the **STATE badge** (BOOTING → READY), the **live model-load progress bar** climbing to 100%, and the scrolling event log.

> **Resolution is 1024×768.** 1080p is **not available** bare-metal on this hardware — the firmware GOP only offers 1024×768, and the `gfxmode=1920x1080x32` request in GRUB is ignored by the firmware (confirmed on-box). 1080p would require an OS GPU driver (amdgpu), which the bare-metal build does not have. See §14.

---

## 9. Validation (is it alive?)

The **primary validation channel is the network telemetry console**, run on the **second LAN machine**. The box broadcasts a CRC'd telemetry packet over **UDP port 51000** (Intel I211) ~once per second.

> **Use your OS's native Python — on Windows that's `py -3`, NOT WSL.** WSL2 runs behind a NAT and **cannot see the LAN broadcast**; the receiver will sit silent under WSL even though the box is fine.

**Text (one packet then exit):**
```
py -3 phase3\scripts\telemetry_receiver.py --once
```

**Browser console (live):**
```
py -3 phase3\scripts\telemetry_receiver.py --sse
# then open http://localhost:8800
```

A healthy box reports: `model="Gemma 4 E2B"`, `load=100%`, `NN=6`, `self=5/5`, `CRC=OK`, `err=0`, `fb=1024x768x32`.

**Deeper channel (on-box log):** with a `JARVIS_DBG_BOOT_LOG=1` build, the durable NVMe log captures the full boot timeline:
```bash
sudo dd if=/dev/nvme0n1 bs=512 skip=4000794624 count=2700 | python3 phase3/scripts/parse_nvme_log.py
```

---

## 10. Remote Telemetry Console

`telemetry_receiver.py --sse` serves a browser console (default `http://localhost:8800`) that renders the live stream across **6 screens**:

1. **Command Center** — live state tiles (queries, route, errors, throughput) + the **live Model-load bar** + activity feed.
2. **Routing** — cache vs → LLM ratio.
3. **Last Response** — the most recent (truncated) inference output.
4. **Models** — the deployed model + bench-off context.
5. **SHIELD** — the passive check **count** (see honesty note).
6. **Capabilities** — auto-populated from the live telemetry flags (a new box capability shows up on its own).

> **Honest scope:** the console is **read-only** — it consumes telemetry-out only. There is **no control-in** (you cannot drive the box from it) in v1.0; a two-way channel is a future phase, gated on a security checklist. The console shows only real, live state — nothing fabricated.
>
> The console serves from the repo's `phase4/console/` automatically regardless of your working directory; pass `--web-dir <dir>` to override.

---

## 11. On-Box HUD

The monitor's framebuffer HUD (1024×768×32, drawn by the rootserver) shows:
- the status **panel** (Title / Display / Model / Threads / Queries / Last),
- the **STATE badge** (BOOTING / READY / ERROR — derived from real state),
- the **live model-load progress bar** (climbs while the model loads, green at 100%),
- a scrolling **event log**.

It is a local readout; the full UI is the remote console (§10). Everything on the HUD is also mirrored to the NVMe log so the on-screen state is reconstructable headlessly.

---

## 12. Updating

- **Rebuild** after code changes (on the JARVIS PC): `bash phase3/scripts/build_jarvis_x86.sh ~/Desktop/JARVIS_OS`, then re-flash the USB (§4).
- **Swap the model:** re-run `setup_nvme_partition.sh --model <new.gguf>` (note the rootserver currently opens `GEMMA2B.GUF` by name — a different file name needs the corresponding `JARVIS_MODEL_FILE` rebuild).

---

## 13. Troubleshooting

| Symptom | Cause / Fix |
|---------|-------------|
| Flasher "build dir not found" under sudo | The **`sudo HOME=/home/jarvis`** gotcha (§4) — sudo reset `$HOME`. Re-run with `HOME=/home/jarvis`. |
| Not sure which `/dev/sdX` | `lsblk -dno NAME,TRAN,SIZE,MODEL` — pick the USB (`usb` transport). The flasher **refuses NVMe and the root disk** as a safety net. |
| Won't boot from USB | CSM still on, or Secure Boot blocking — set Secure Boot → "Other OS", CSM Disabled (§7); use F8. |
| Boots but **model not found** | The `JARVIS_DATA` partition isn't at **sector 32768**, or the file isn't `GEMMA2B.GUF`. Verify with `sudo parted /dev/nvme0n1 unit s print` (Start = 32768s) and re-provision (§5). |
| NVMe DMA / triple fault | **IOMMU/AMD-Vi** still enabled — disable it (§7). |
| Telemetry console shows nothing | (a) You're running it under **WSL** — use **Windows-native `py -3`** (WSL2 NAT can't see the LAN broadcast). (b) **Windows Firewall** is blocking inbound **UDP 51000** — allow it. (c) The console host isn't on the **same subnet** as the box. |
| `GET /` returns 404 in the console | Old behavior when run from the wrong directory — now fixed (the `--web-dir` default is repo-relative). If you still hit it, pass `--web-dir <repo>/phase4/console`. |
| Monitor dark after GRUB | The GOP HUD renders at 1024×768 since goal #2 — check the monitor is on the active output and the firmware GOP is active (not legacy/CSM). Validate via the telemetry console / NVMe log. |

---

## 14. Known Limitations (honest)

- **Single model:** Gemma 4 E2B only (the bench-off winner).
- **CPU-only:** no GPU inference path in v1.0.
- **Speed:** deployed inference is **~5.46 tok/s @ `NUM_NODES=6`** (CPU, AVX2 + seL4 threadpool). (Higher native-bench numbers exist for a *different, non-deployed* engine build — they are not what the box runs.)
- **SHIELD is passive:** on the live inference path SHIELD **monitors only and returns ALLOW** — it is **not a live blocker**. The `shield=` field is a check **count**, not a block count.
- **No local input:** there is no on-box keyboard interface yet; interaction is via the (read-only) remote console.
- **Console is read-only:** telemetry-out only; no control-in.
- **Display is 1024×768:** 1080p is not available bare-metal (firmware GOP limit; no OS GPU driver).
- **Verification stance:** the running seL4 config is **functional-but-unverified by design** (`KernelFastpath=ON` + SMP `NUM_NODES=6` are outside seL4's verified X64 configuration). Do **not** describe the deployed build as "formally verified."

---

## 15. Architecture Reference

**Boot chain:** UEFI firmware → GRUB (multiboot2) → seL4 kernel → rootserver (Process A) → self-test → NVMe model load → spawn Process B (inference) → IPC workload loop + HUD + telemetry emit.

**NVMe map (`/dev/nvme0n1`):**
- Model partition `JARVIS_DATA` (FAT32) — **starts at sector 32768**, file `GEMMA2B.GUF`.
- Durable telemetry/boot log — **LBA 4000794624** (read with `dd … | parse_nvme_log.py`).

**Telemetry packet:** a 200-byte, versioned, zlib-CRC'd binary record (`phase3/src/drivers/jarvis_telemetry.h`) broadcast over UDP :51000 and decoded by `telemetry_receiver.py` (validates `zlib.crc32(pkt[:196])`).

**See also:** `phase3/docs/BARE_METAL_BOOT_GUIDE.md` (detailed boot/flash/validation), `phase4/docs/PHASE_4_GOAL2_DISPLAY.md` (the HUD), `phase4/docs/ROADMAP.md` (phases beyond v1.0).
