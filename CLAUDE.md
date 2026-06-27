# CLAUDE.md

Guidance for Claude Code when working with this repository.

---

## Project Overview

**JARVIS AI-OS** - An AI-controlled operating system using seL4 microkernel with autonomous decision-making. AI runs at Ring 3 (50-500ms decisions), microkernel runs at Ring 0 (<1ms interrupts). 36-month timeline.

| Phase | Status | Period | Summary |
|-------|--------|--------|---------|
| Phase 0 | COMPLETE | Months 1-6 | Validation (80% success, GO decision) |
| Phase 1 | COMPLETE | Months 6-12 | PoC on x86 QEMU (26/26 weeks, Dec 2025) |
| Phase 2 | COMPLETE | Months 12-24 | Alpha on Pi 4 bare metal (21 drivers, 30-day stability) |
| **Phase 3** | **COMPLETE (beta)** | Months 24-36 | Beta on x86-64 bare metal (**LLM inference on seL4 VERIFIED**) — v0.2.1-beta tagged (30-day soak deferred) |
| Phase 4 | IN PROGRESS | Months 36+ | Production v1.0 — engineering complete; v1.0.0 release in prep (tag not cut) |
| Phase 5 | IN PROGRESS | Months 36+ | Memory — keystone episodic store first ("it-remembers" MVP arc; plan: `phase5/docs/PHASE_5_PLAN.md`) |

**Current:** Phase 4 started (2026-06-16). Phase 3 CLOSED — v0.2.1-beta tagged @ 06de75c (2026-06-16). **MILESTONE: JARVIS engine bench-off COMPLETE — 11/11 models load and generate across 6 model families (Llama, Gemma 4, Phi-3, Mistral, Qwen3, Qwen3.5 DeltaNet SSM).** Gemma 4 E2B (#1 quality, 8.40/10) validated on seL4 QEMU. Fused QKV/gate-up support unlocked Phi-3. Gated DeltaNet SSM (~1200 LOC) unlocked Qwen3.5 hybrid architecture. JARVIS engine speed (NATIVE dev engine, Llama 1B — NOT the deployed seL4 build): 3.22 tok/s (1T), 19.79 tok/s (16T) — 2x gap to llama.cpp (down from 40x); the deployed system is Gemma 4 E2B **5.46 tok/s @ NUM_NODES=6**. Fused qdot + SIMD attention + RoPE tables + pthread. Phase 3b complete (bare-metal boot, NVMe model loading, IPC workload, I211 NIC). Phase 3c hardening done (fuzz testing, security audit 25 findings). NVMe persistent logging operational — auto-captures all serial output with [VGA]/[SER]/[PB] tags. Bare-metal **burn-in passed** (2026-06-15 — hours, ~400 queries, err=0; NOT a 30-day soak): x86 boot/model-load/coherent gen, heartbeat/shield IPC fix err=0 over 400 real-hardware queries, durable NVMe log (boot_id constant). Formal 30-day x86 soak **DEFERRED — descoped from v0.2.1-beta gating (risk-accepted, ADR `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`)** — next: Phase 4 perf (AVX2/threading); TurboQuant/RotorQuant evaluated → deferred to Phase 4 (ADR `docs/decisions/2026-06-15-defer-turboquant-rotorquant-to-phase4.md`). **Phase 3 final report written (`phase3/docs/PHASE_3_FINAL_REPORT.md`); v0.2.1-beta TAGGED @ 06de75c (2026-06-16).** Two-PC setup: Main PC (5600/2070/32GB) for dev, JARVIS PC (2700X/280X/32GB/2TB NVMe) running bare-metal seL4. Phase 4 goal #1 reframed to **Inference performance** — v1.0 = CPU AVX2+threading in the seL4 build (~8–9 tok/s Gemma E2B @16T target); GPU inference DEFERRED (no usable GPU — ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`); GPU display/UI (goal #2) unaffected. **M0 DONE (2026-06-16):** seL4 kernel rebuilt to XSAVE (feature-set 7) so it context-switches AVX YMM — AVX2-under-preemption safety gate PASSED on KVM (XCR0.avx=1 in PA+PB, 0 mismatches / 24k probes, self-test 5/5); M1 DONE (2026-06-17): AVX2 enabled in the seL4 build → **Gemma 4 E2B ~1.53 tok/s bare metal, single-thread** (vs ~0.2 scalar, ~7.6×; both M0 carve-outs closed); **M2 DONE (2026-06-17): seL4 SMP enabled (`-DSMP=ON -DNUM_NODES=2`, BSP + 1 AP) — multicore foundation validated KVM + bare metal: numNodes==2, self-test 5/5, Gemma load/gen + shmem IPC intact under SMP, `q=100 err=0`, ~1.53 tok/s 1T (no SMP penalty vs M1). E1: seL4 does NOT auto-distribute → M3 workers need explicit `seL4_TCB_SetAffinity`; no inference speedup yet (threadpool is M3). Branch A (ADR `docs/decisions/2026-06-17-enable-smp-branch-a.md`); a per-build config-verification gate in `build_jarvis_x86.sh` asserts SMP-on + M0/M1 invariants. **M3 DONE (2026-06-18): seL4-native threadpool — PA creates N−1 worker TCBs in PB's VSpace/CSpace (PB has no allocator), resolving the worker entry from PB's ET_EXEC `.symtab`; workers pin to distinct cores (`seL4_TCB_SetAffinity`), block on per-worker wake notifications, run the corrected work-stealing core over `qmatmul_vec` (generation publish + active-counter join). Verified BYTE-IDENTICAL to the serial path (greedy determinism, all prompts). Bare-metal N-sweep → Gemma 4 E2B `5.46 tok/s @ NUM_NODES=6` (3.57× the 1.53 1T), `err=0` over 800 queries; production `NUM_NODES=6`. Two bring-up bugs fixed (unchecked `sel4utils_copy_cap_to_process` → null-cap deadlock, caught by adversarial review; worker started outside sel4runtime → `seL4_SetIPCBuffer`, caught by QEMU smoke). M4 DONE (2026-06-18): goal #1 (inference performance, CPU) COMPLETE — seL4-build benchmark recorded + reproducible (`phase4/docs/PHASE_4_GOAL1_BENCHMARK.md` + the canonical tool `phase3/scripts/bench_sel4_inference.sh`); ROADMAP goal-#1 CPU "Done when" ticked. 5.46 is inside the tempered ~4–6 target (the earlier ~8–9 @16T aspiration walked back as memory-bandwidth-bound, ~2.9 GB/token). Goals #2–7 (graphical output, USB input, installer, 90-day soak, docs, v1.0.0) open *(superseded — see the v1.0 SCOPE FROZEN block at the end of this section)*.** **Goal #2 (graphical output): Step 1 (UEFI boot migration) DONE 2026-06-18; Step 2 (first pixels) DONE 2026-06-19; Step 2c-1 (font + live status panel) DONE 2026-06-20 — 8×16 bitmap font + on-screen status panel on the FB (1024×768×32 GOP); load % tracks the NVMe read [loading 1%..100%]→[loaded]; panel mirrored to the durable log as [PANEL] (per-query Queries quiet so the no-wrap log can't flood); verified ENTIRELY from the boot_id log (700-query run, err=0, M3 NN=6, FB 1024×768×32 UC mapped pages=768 relog). Step 2c-2a (observability: [SNAP] full-state log line + boot-relative T+ms timestamps, LOG-ONLY) DONE 2026-06-20 — verified on QEMU serial (q=500, err=0; [SNAP] at init + every 100q, [STATS] now carries T+ms). Step 2c-2b (jarvis_ui_tokens.h centralization + scrolling event-log region) DONE 2026-06-20 — the token header is now the single FBP_*/FBP_Y_* source (panel byte-identical, no shift); a no-scroll line-ring fed from putc_serial completed lines (low cadence, never per-token, no recursion), host 24/24, physical-boot verified (event-log renders+scrolls with `>`=head, panel unchanged, boot_id log 5/5 / err=0 q=400 / FB 1024×768×32 pages=768). No-scroll reading-order (2c-2b) superseded by Step 2c-2c (per-state HUD styling: header band + STATE badge BOOTING/READY/ERROR + live ROUTE ratio + COUNTERS strip + natural oldest→newest event-log scroll + banner v0.2.1-beta) DONE 2026-06-20 — verified on hardware (photos + boot_id log, err=0 q=1200, no faults). Goal #2 display v1-complete. **fat32 FAT-sector cache + exact data-only load % DONE 2026-06-21** (each FAT sector read once not ~128×, ~160 MB less NVMe I/O — read ~3125→~2962 MB; load % hits exactly 100% at completion; physical-boot verified on real Gemma, err=0). **Goal #2b N-a (Remote Telemetry Console step 1/3): Intel I211 NIC TX first-light DONE 2026-06-21** — dormant I211 brought up on bare metal (fixed v→phys DMA descriptor + added TXDCTL.ENABLE + post-reset settle/MAC-validity + DD poll); rootserver maps BAR0 32pp UC + 2 DMA frames, sends one broadcast frame, all NON-FATAL (QEMU not-found path proven); boot_id log: MAC 0c:9d:92:0e:39:9a AV=1, link 1000, 5× DD=1, err=0. **Goal #2b N-b (telemetry-OUT step 2/3): minimal Eth/IPv4/UDP emit DONE 2026-06-21** — `net_udp.c` (RFC1071 IP checksum, UDP cksum=0) wraps a UDP limited-broadcast (192.168.100.143→255.255.255.255:51000, no ARP) around `i211_send_phys`; sent 10× non-fatal; VERIFIED on the wire (Wireshark: 10 frames, IP cksum 0x5584 VALID, payload "JARVIS-TELEMETRY-HELLO") + on-box (UDP TX 0..9 DD=1, first-light OK, zero regression q=200 err=0). **Goal #2b N-c-1 (telemetry-OUT step 3/3): continuous ~1 Hz CRC'd binary telemetry DONE 2026-06-22** — new `jarvis_telemetry.c/h` (200-byte packed versioned `telemetry_packet_t` + zlib/IEEE CRC-32 over [:196]); the `main_x86.c` workload loop fills it from live state and emits over the I211 (`net_udp` + `i211_send_phys`, fire-and-forget, `g_net.ready`-gated) at the `[STATS]` q%100 site + a 1 Hz keepalive that also ticks from inside the inference response-poll → true ~1 Hz even through a ~12 s inference (counters frozen, uptime advances); VERIFIED on the wire (Wireshark: 914/914 packets CRC-valid, mean 1.000 s cadence, 13-packet mid-inference heartbeat, num_nodes=6, model "Gemma 4 E2B", err=0) + on-box (boot_id=1: self-test 5/5, Gemma 2962 MB, M3 NN=6, [STATS] err=0 through q=500, 0 faults) + host (test_jarvis_telemetry 22/22, canonical zlib 0xCBF43926). **Box-side telemetry-OUT (N-a→N-b→N-c-1) COMPLETE.** Next: goal #2b N-c-2 (~60-line UDP receiver) + N-c-3 (honesty-corrected React console); goal #2 backlog then goals #3–7.** **Goal #2 display-polish DONE 2026-06-24:** live model-load progress bar on the HUD (`fb_progress_bar`, lockstep with the `[loading N%]` text, FBP_ACCENT→FBP_OK; test_framebuffer 46/46) + a matching live `model_load_pct` bar on the remote console (UI-feature parity closed — already an `/events` field, golden fixture unchanged, 4 console layers green incl. e2e 12); 1920×1080 `gfxmode` requested in `grub.cfg` but the firmware GOP **DECLINED** it — stays **1024×768** (confirmed on-box boot_id=3, 2026-06-24, live telemetry `fb=1024x768x32`); bare-metal UEFI GOP only offers 1024×768 (1080p needs an OS GPU driver we don't have). HUD renders fine at 1024×768; true-WC evaluated + DEFERRED (not worth it for the low-cadence static HUD); fat32 sector-size framing corrected (SEC-028 already rejects, SEC-029 backstops). **Goal #4 (one-script installer) DONE.** **Goal #6 (docs): v1.0 USER_GUIDE drafted 2026-06-24** (`phase4/docs/USER_GUIDE.md` — x86 headless-appliance: install + `sudo HOME=` gotcha, NVMe model partition @ LBA 32768, BIOS checklist, 1024×768 first-boot HUD, network-telemetry validation (Windows-native `py -3`, not WSL), read-only console scope, honest limitations — no soak); box-day 2026-06-24 also corrected the 1080p docs (firmware declined gfxmode → 1024×768) + stale "monitor dark under UEFI" claims (the GOP HUD renders) + `telemetry_receiver.py --web-dir` now serves from any CWD. This build runs `KernelFastpath=ON` **+ SMP (`NUM_NODES=6`)** → outside seL4's verified X64 config; functional-but-unverified by design (see `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §3). **Pre-release quality sweep DONE 2026-06-24:** SSE disconnect-traceback swallowed (`_QuietThreadingHTTPServer`), the two model-gated test SKIPs now emit `::warning::` (so a CI skip isn't mistaken for coverage), the durable `LOG_SELFTEST` line logs the REAL self-test tally + "2 vacuous" caveat (was a hardcoded "5/5 PASS"), and the per-query `[PB] tokens` dump is `#if JARVIS_DBG_PB`-gated — no wire-contract / golden-fixture / model-path change. (NVMe log-wrap split to its own later slice; the box-verify showed `log=2700/2700` = the no-wrap telemetry log is now FULL.) **NVMe log-wrap DONE 2026-06-24:** the durable telemetry log is now a CIRCULAR / rolling 2700-entry buffer (keeps the latest; `cursor`=wrapping write slot, `total_entries`=monotonic lifetime, stale pre-wrap cursor clamped on init) — `nvme_log_cursor()` returns entries-stored-capped so the telemetry `log_cursor` value/shape is unchanged (**golden fixture byte-identical, NO wire change**); `parse_nvme_log.py` reads wrap-order; console reframed "rolling (keeps latest)"; recovery `dd` → `count=2701`; `test_nvme_log.c` wrap test (no -2 / cursor cycles / total monotonic / cap / newest-overwrites-oldest / migration clamp). **Installer `--target esp` (reversible dual-boot) — CODE + DRY-RUN DONE 2026-06-24:** `install_jarvis_x86.sh` gained `--esp <part>` + `guard_esp_device` (vfat-ESP-partition + ADD-JARVIS double-confirm; accepts an NVMe partition, refuses a whole disk) + `write_boot_esp` (mounts the EXISTING internal ESP, copies to `EFI/jarvis/` ONLY, sed-rewrites grub.cfg → `/EFI/jarvis/boot/`, `grub-mkstandalone` `EFI/jarvis/grubx64.efi` — never `--removable`/whole-disk/`EFI/BOOT`/`EFI/ubuntu`; additive `efibootmgr` entry with Ubuntu kept `BootOrder[0]`; stale-ESP-root-model cleanup runs FIRST (the box ESP is full) behind an explicit `DELETE-STALE-ESP-MODEL` confirm, then staging (guarded cp's) — ESP copy only, JARVIS_DATA untouched; rollback + Secure-Boot-"Other OS" + BootNext one-shot validation printed). test 35/35, shellcheck-clean (full strictness), zero box writes — **the real on-box dual-boot install is a SEPARATE session**. Live ESP = `/dev/nvme0n1p4` (Boot0001* ubuntu only entry). **On-SSD dual-boot DONE + VERIFIED on-box 2026-06-25:** `install_jarvis_x86.sh --target esp --esp /dev/nvme0n1p4` adds JARVIS to the internal NVMe ESP (p4) alongside Ubuntu (additive `efibootmgr`, Ubuntu kept `BootOrder[0]`) — VERIFIED live (boot_id=5, coherent Gemma 4 E2B, NN=6, fb=1024x768x32, err=0; live telemetry + durable NVMe log). **The boot USB is now a RECOVERY/re-flash device, NOT required to boot** (`--target usb` still works + CI-tested). Installer F3 fixed: `add_jarvis_boot_entry` no longer aborts under `set -e` when ubuntu is the only prior UEFI entry (it left JARVIS the default; hand-fixed on-box, now fixed in-script + regression test T9).  **v1.0 SCOPE FROZEN (2026-06-26):** Phase 4 engineering is COMPLETE — only the release remains. Final goal scoreboard: **#1 Inference perf (CPU)** ✅ DONE (Gemma 4 E2B **5.46 tok/s @ NUM_NODES=6**, M0–M4; GPU deferred) · **#2 Graphical output** ✅ DONE (1024×768 GOP HUD) · **#2b Remote Telemetry Console** ✅ DONE (read-only; control-IN is Phase 6) · **#3 USB keyboard** ✂️ CUT (2026-06-26 — headless appliance; interactive input is Phase 6 console control-IN, not a local keyboard) · **#4 Installer** ✅ DONE / feature-complete (usb appliance / esp dual-boot, VERIFIED on-box boot_id=5 / disk full single-OS, CODE+DRY-RUN ONLY) · **#5 90-day soak** ❌ NOT run (owner-scheduled) · **#6 Docs** ✅ DONE (USER_GUIDE) · **#7 v1.0.0 MIT release** ⏳ IN PROGRESS (LICENSE + THIRD_PARTY_LICENSES + doc-honesty pass + PHASE_4_FINAL_REPORT done; tag not yet cut). No further feature work for v1.0 — only the release remains; see `phase4/docs/PHASE_4_FINAL_REPORT.md`. Phase 5 (Memory) is next. **Phase 5 (Memory) plan stood up 2026-06-26** (`phase5/docs/PHASE_5_PLAN.md` + keystone `phase5/docs/PHASE_5_GOAL1_EPISODIC_STORE.md`): keystone-first "it-remembers" MVP arc (#1 episodic → #2 context → #3 retrieval → #6 cache growth) then #4/#5/#7; storage = a carved raw-LBA region in the verified ~1.66 TiB free gap after JARVIS_DATA (no repartition, nvme_log precedent); M0 episodic store engine landed 2026-06-27 (first Phase 5 code: `phase3/src/ai/episodic_store.c/h` raw-LBA circular store + `test_episodic_store.c` host-CI; M1 now wires it into Process A's workload loop 2026-06-27 — a RAM batch fills per cache/inference query (episodic_fill), committed to NVMe at the [STATS] cadence (epi_commit, gated on g_episodic_ready); box build + QEMU smoke pending).

---

## Architecture

### Model B: Microkernel with AI Control

```
Hardware Layer
    ↓
Microkernel (Ring 0, cores 0-1)
• Interrupt handling (<1ms)
• Memory management, IPC primitives
• ~12K LOC, seL4 (formally verified*)
    ↓ ← Lock-free IPC →
AI Decision Engine (Ring 3, cores 2-N)
• Specialist agents (4-6) — domain-expert agents (device, network, filesystem, user), NOT model-size tiers
• Decision cache (50ms→<1ms for 85% ops)
• SHIELD safety framework
    ↓
User Space Services (Ring 3)
• Device drivers, File systems, Applications
```

> *\*seL4 "(formally verified)" refers to its verified configurations. JARVIS's x86-64 build runs `KernelFastpath=ON`, which is **outside** the verified X64 config ("C-level functional correctness, **no fast path**") — so JARVIS runs a functional-but-unverified seL4 configuration by design (IPC/AVX perf > holding the proof). See `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §3.*

**Why:** AI inference (50-500ms) is 3 orders of magnitude too slow for Ring 0 interrupt handling (<1ms). Microkernel isolates time-critical ops from AI.

### Phase 2 Split Deployment (COMPLETE)

```
┌──────────────────┐       UART        ┌──────────────────┐
│   PC (Host)      │◄─────────────────►│   Pi 4 (seL4)    │
│                  │   115200 baud     │                  │
│  Python AI       │                   │  Decision Cache  │
│  - Phi-3 Mini    │   7ms median RTT  │  - 258 patterns  │
│  - Llama 3.2 1B  │                   │  - 85.7% hit     │
│  - SHIELD        │                   │  - <1ms lookup   │
└──────────────────┘                   └──────────────────┘
```

- **Cache hits (85%):** seL4 decision cache answers in <1ms
- **Cache misses (15%):** Forwarded to PC via UART (7ms RTT)
- **No Python on Pi 4:** seL4 userspace is C-only
- Phase 3+ returns to standalone (x86 or Multi-Pi cluster)

### Architecture Enhancements

- **Decision Cache:** Pre-compiled query→bytecode patterns. 135x speedup (50ms→<1ms), 85.7% hit rate.
- **SHIELD Safety:** Staged execution sandbox, continuous risk scoring (0-1.0), shadow execution, deterministic rollback.
- **Shared Context Pool:** Lock-free shared state for agent coordination. 220x faster than serialization.

### Key Constraints

**AI cannot:** handle interrupts (<1ms), generate drivers real-time, run at Ring 0
**AI can:** make high-level decisions (50-500ms), optimize configs, coordinate agents, learn over time

---

## Build & Test Commands

### Phase 2: seL4 Pi 4 Build

```bash
# In WSL - build JARVIS kernel for Pi 4
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/scripts
tr -d '\r' < build_and_copy_kernel.sh | bash

# Manual build steps
cd ~/sel4-workspace/rpi4_jarvis
cmake -G Ninja \
    -DCROSS_COMPILER_PREFIX=aarch64-linux-gnu- \
    -DKernelPlatform=bcm2711 \
    -DKernelSel4Arch=aarch64 \
    -DKernelArmPlatform=bcm2711 \
    ../projects/jarvis-sel4
ninja

# Copy to firmware directory
cp images/kernel8.img /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/firmware/

# Deploy to SD card
copy phase2\firmware\kernel8.img D:\
```

### Phase 2: Test Commands

```bash
# Python tests
wsl python3 phase2/src/ai/test_uart_ipc_client.py      # 22 tests - UART protocol
wsl python3 phase2/src/ai/test_system_bootstrap.py     # 25 tests - Bootstrap
wsl python3 phase2/src/ai/test_integration.py          # 10 tests - Integration

# C tests
wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/ipc && \
  gcc -O2 -I../../../phase1/src/cache -I../../../phase1/src/ipc \
  dual_ring_buffer.c test_dual_ring.c ../../../phase1/src/ipc/ring_buffer.c \
  -o test_dual_ring && ./test_dual_ring"                # 12 tests

wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/ipc && \
  gcc -O2 -I../../../phase1/src/cache -I../../../phase1/src/ipc \
  ipc_handler.c dual_ring_buffer.c ../../../phase1/src/ipc/ring_buffer.c \
  ../../../phase1/src/cache/decision_cache.c ../../../phase1/src/cache/cache_patterns.c \
  test_ipc_handler.c -o test_ipc_handler && ./test_ipc_handler"  # 10 tests

wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/drivers && \
  gcc -O2 test_uart_logic.c -o test_uart_logic && ./test_uart_logic"  # 8 tests
```

### Phase 1: Comprehensive Test (All 27 tests)

```bash
wsl
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
./setup_wsl_dependencies.sh  # First time only
./run_all_tests_wsl.sh       # 25-35 min
```

---

## File Structure

```
JARVIS_OS/
├── phase0/                     # COMPLETE - validation experiments
├── phase1/                     # COMPLETE (26/26 weeks)
│   └── src/                    # cache/, ipc/, sel4/, ai/, shell/
├── phase2/                     # COMPLETE
│   ├── docs/                   # PHASE_2_KICKOFF.md, UART_IPC_PROTOCOL.md, USER_GUIDE.md, ALPHA_TESTER_GUIDE.md, PI4_PLATFORM_GUIDE.md
│   ├── firmware/               # kernel8.img, u-boot.bin, boot files
│   ├── src/
│   │   ├── ipc/               # dual_ring_buffer, ipc_handler + tests
│   │   ├── drivers/           # uart_pl011, emmc_sdhci, bcm2711_timer,
│   │   │                      # bcm_genet, net_stack, net_cmd, usb_hid, bcm_gpio, bcm_i2c,
│   │   │                      # bcm_spi, bcm_rng, bcm_pwm, bcm_dma, slot_alloc, dma_alloc, blk_dev
│   │   ├── ai/                # uart_ipc_client.py, system_bootstrap.py + tests
│   │   ├── boot/              # jarvis.dts, jarvis.dtb, jarvis_dtb_data.h, fdt_parser.h/c
│   │   ├── sel4/              # main_arm64.c, CMakeLists.txt
│   │   └── jarvis-sel4-cmake/ # CMakeLists.txt for TII build system
│   ├── weeks/                  # week27-week47 status docs
│   └── scripts/               # build_and_copy_kernel.sh, build_installer_image.sh, flash_sd.sh, install_jarvis.sh
├── phase3/                     # IN PROGRESS (JARVIS Project PC operational)
│   ├── docs/                   # PHASE_3_KICKOFF.md, HARDWARE_RESEARCH.md, IMPLEMENTATION_PLAN.md
│   ├── src/
│   │   ├── ai/                # ggml integration, model loading, inference
│   │   ├── boot/              # x86 boot config (GRUB/EFI)
│   │   ├── drivers/           # x86 drivers (16550A UART, AHCI, NIC)
│   │   ├── ipc/              # Shared memory IPC
│   │   └── sel4/             # seL4 x86-64 rootserver
│   ├── scripts/               # Build scripts
│   ├── firmware/              # Boot images, GRUB configs
│   └── weeks/ (empty/unused)  # Phase 3 used phase3/docs/ topic docs + docs/decisions/ ADRs + PHASE_3_FINAL_REPORT.md, not weekly status files
├── phase4/                     # IN PROGRESS (Production v1.0)
│   ├── docs/                   # ROADMAP.md (Phases 4-7), PHASE_4_GOAL1_INFERENCE_PERF.md (goal #1 research+plan)
│   └── weeks/                  # weekN/WEEK_N_STATUS.md (weekly status resumes, like Phase 2)
├── phase5/                     # IN PROGRESS (Memory) — plan docs only, no code yet
│   ├── docs/                   # PHASE_5_PLAN.md, PHASE_5_GOAL1_EPISODIC_STORE.md (keystone)
│   └── weeks/                  # weekN/WEEK_N_STATUS.md (weekly status, like Phase 4)
├── JARVIS_UNIFIED_PLAN.md     # 36-month master plan
├── ARCHITECTURE_ENHANCEMENTS.md
├── archive/                    # Historical research
└── temp_sd_backup/            # SD card backups
```

---

## Coding Conventions & Patterns

### C/Python IPC Struct Alignment

Python ctypes structs MUST exactly match C struct layout:

```python
class RingMessage(ctypes.Structure):
    _pack_ = 1  # REQUIRED: No padding
    _fields_ = [
        ("type", ctypes.c_uint32),
        ("id", ctypes.c_uint32),
        ("payload_size", ctypes.c_uint32),
        ("payload", ctypes.c_char * MAX_MESSAGE_SIZE),
        ("timestamp", ctypes.c_uint64)
    ]
```

### Query Normalization

Both C and Python must normalize identically: lowercase → collapse spaces → trim → FNV-1a 64-bit hash.
- C: `cache_normalize_query()` in `decision_cache.c`
- Python: `QueryProcessor.normalize_query()` in `query_processor.py`

### seL4 Tutorials CMakeLists.txt Pattern

```cmake
include(${SEL4_TUTORIALS_DIR}/settings.cmake)
sel4_tutorials_regenerate_tutorial(${CMAKE_CURRENT_SOURCE_DIR})
include(rootserver)
include(simulation)
add_executable(hello-world src/main.c ...)
target_link_libraries(hello-world sel4 muslc utils sel4muslcsys sel4platsupport sel4utils sel4debug)
DeclareRootserver(hello-world)
```

Note: `DeclareTutorialApp()` does NOT exist. Use `add_executable()` + `DeclareRootserver()`.

### Testing Philosophy

- Every component has standalone tests printing PASS/FAIL
- C tests: compile and run directly (no deps)
- Python tests: `python test_*.py` (mock mode if seL4 unavailable)
- Always aim for 100% pass rate

### Development Documentation Pattern

- **Phase 2** used weekly status files: `phase2/weeks/weekN/WEEK_N_STATUS.md` (weeks 27-49), committed weekly with the week number in the message.
- **Phase 3** was documented via topic/milestone docs in `phase3/docs/` (KICKOFF, HARDWARE_RESEARCH, IMPLEMENTATION_PLAN, the QEMU/native test-results docs, MODEL_BENCH_OFF, the security audits), the ADRs in `docs/decisions/`, and `phase3/docs/PHASE_3_FINAL_REPORT.md` — **not** weekly status files (there is no `phase3/weeks/`).
- **Phase 4** resumes weekly status files at `phase4/weeks/weekN/WEEK_N_STATUS.md` (like Phase 2); Phase 3 used topic docs.
- Implement, test, update the relevant doc; commit with a descriptive subject referencing the milestone/ADR where applicable.

### Bare-Metal Development Rules

- **Always test in QEMU before flashing USB** — run `phase3/scripts/qemu_test.sh` after every build. If it crashes in QEMU, don't waste time reflashing.
- **Build without embedded model for fast iteration** — use `-DJARVIS_EMBED_MODEL=""` for quick boot tests. Model loading is tested separately via the NVMe FAT32 runtime path (the GRUB-module approach was abandoned — seL4 overwrites module memory during rootserver relocation).
- **Verify GRUB menu entry works** — wrong image names cause silent boot failures (keyboard dies, no output). Check grub.cfg image names match `ls ~/sel4-x86/jbuild/images/`.
- **All on-screen UI MUST mirror to the NVMe log (verify UI from the log, not photos)** — the box is headless-except-monitor and showing on-screen UI to a reviewer is impractical and gets worse as the UI grows. So every render routes through a single log-mirror chokepoint (e.g. `fb_status_line` → `[PANEL] <text>`); emit a periodic full-UI snapshot (all fields/widgets in one block) at init + a steady cadence; add a boot-relative timestamp (TSC→ms) so the log shows what **and when**. "UI state fully reconstructable from the log" is an **exit criterion** for every goal-#2+ UI slice. Caveat: the log proves **content, not pixels** — do ONE visual confirm per rendering/font/layout change, then trust the log. Gotcha: anything drawn before `nvme_log_init` is serial-only (re-emit it via the post-init snapshot).
- **UI must track real features (the UI is the living truth-map)** — every user-visible feature/capability MUST be reflected in the UI: primarily the **Remote Telemetry Console** (`phase4/console/`, fed by the `/events` SSE telemetry), secondarily the on-box HUD. When a feature ships, either surface its **real live signal** on the relevant console screen, or expose it in the console's auto-populated **Capabilities/Features** section (driven from real telemetry / a declared feature manifest — never hardcoded claims). Honesty corollary: the UI shows ONLY real, live state — a feature that exists but is missing from the UI is a **gap to close**; anything shown without a live source is **fiction to remove**. So "what's working vs what isn't (or isn't meant to be there)" stays obvious to both the user and the AI at a glance. A new feature means updating the console (or its feature section) **in the same change**; enforced by the console honesty gate (`phase4/console/test_console_honesty.py`).

---

## Current Status (Phase 3 — Process-Isolated LLM Inference on seL4)

**NVMe RUNTIME MODEL LOADING MILESTONE** (April 4, 2026) — **LLM inference with runtime NVMe model loading on bare-metal seL4 on Ryzen 7 2700X.** 1.6MB USB boot image loads the model GGUF from the NVMe FAT32 partition (Lexar NM790, PCI bus 1) at runtime — model-agnostic, loading whatever `JARVIS_MODEL_FILE` names (current default `"GEMMA2B GUF"` = Gemma 4 E2B Q4_K_M, ~2.89 GiB). NVMe driver: PCI detection → BAR0 MMIO → admin queue → IDENTIFY → I/O queue → FAT32 read → frames → Process B. (Original Apr 4 milestone: 770MB Llama 3.2 1B Q4_K_M → 197K frames.) Coherent text generation via process-isolated IPC. No embedded model binary. Self-test 5/5 PASS, 181 untypeds, 32410MB RAM.

| Milestone | Status |
|-----------|--------|
| **Phase 2 Pi 4 (ALL 87 milestones DONE)** | **DONE** |
| U-Boot boot, UART IPC, SD/EMMC, GENET networking, USB HID keyboard | DONE |
| 21 BCM2711 drivers (GPIO, I2C, SPI, RNG, PWM, DMA, watchdog, thermal) | DONE |
| 30-day stability test (30.6 days, 0 crashes, 99.8% pass rate) | DONE |
| Phase 2 security audit: 6 C findings (4 fixed) + Snyk dependency audit | DONE |
| Cross-codebase adversarial audit (Mar 2026, phase2+phase3 C ~35K LOC): 26 findings fixed, 211/211 tests | DONE |
| **Phase 3a: GPU benchmarks (RTX 2070, 1B: 273 tok/s GPU, 44 CPU)** | **DONE** |
| **Phase 3a: seL4 native Linux build env (123/123 PASS, KVM)** | **DONE** |
| **Phase 3a: 247/247 Phase 3 tests validated on native x86-64** | **DONE** |
| **GGUF memory API (gguf_open_memory via fmemopen)** | **DONE** |
| **Q4_K/Q6_K dequantization (verified bit-exact vs llama.cpp)** | **DONE** |
| **Quantized zero-copy model loading (llama_quant.c, ~50MB heap)** | **DONE** |
| **GGUF vocab extraction (128,256 BPE tokens from raw binary)** | **DONE** |
| **Weight tying (output.weight → token_embd.weight fallback)** | **DONE** |
| **RoPE freq factors (rope_freqs.weight loaded, applied as divisors)** | **DONE** |
| **4/4 inference stages PASS on seL4 QEMU (parse→load→tokenize→generate)** | **DONE** |
| **Coherent text: "a microkernel implementation of the L4 architecture..."** | **DONE** |
| **Logits verified vs llama.cpp reference (top-5 match exactly)** | **DONE** |
| **Process isolation: Process A spawns Process B from CPIO (SEC-014)** | **DONE** |
| **Shared memory IPC between two seL4 processes (direct page mapping)** | **DONE** |
| **End-to-end IPC: Process A query → Process B inference → response** | **DONE** |
| **CNode=22, morecore=128MB, allocator pools scaled for 230K frames** | **DONE** |
| Hardware recon (lspci verified: NVMe, I211, R9 280X, xHCI) | DONE |
| VGA text mode output (80x25, 0xB8000) via vka_alloc_frame_at | DONE |
| GRUB2 boot config + USB creator script | DONE |
| x86 source sync/build script | DONE |
| Bare-metal boot on Ryzen 7 2700X | DONE |
| Self-test 5/5 PASS on real hardware | DONE |
| Process isolation on real hardware | DONE |
| Bare-metal boot guide + BIOS checklist | DONE |
| **Llama 3.2 1B inference on bare-metal Ryzen 2700X** | **DONE (Apr 15)** |
| **Coherent text generation via process-isolated IPC on real hardware** | **DONE (Apr 15)** |
| NVMe driver (PCI detect, BAR0 map, admin queue, IDENTIFY, I/O read) | DONE |
| FAT32 read-only parser (BPB, root dir scan, cluster chain) | DONE |
| NVMe IDENTIFY succeeds in QEMU ("QEMU NVMe Ctrl") | DONE |
| NVMe sector read verified in QEMU | DONE |
| NVMe → FAT32 → GGUF magic verified in QEMU | DONE |
| **NVMe full model loading: 770MB from FAT32 → 197K frames → Process B** | **DONE** |
| **Runtime inference from NVMe (no embedded model)** | **DONE** |
| NVMe on bare metal (Lexar NM790 — no HMB needed) | DONE |
| FAT32 partition LBA = 32768 (JARVIS_DATA partition); whole-disk (LBA 0) fallback for QEMU | DONE |
| PCI multi-bus scan (buses 0-15) for NVMe on bus 1 | DONE |
| Continuous IPC workload loop (63 queries, xorshift PRNG, stats, error tracking) | DONE |
| Intel I211 NIC driver (PCI 8086:1539, TX+RX polled) | DONE |
| IPC response drain fix (multi-message responses) | DONE |
| Debug config (compile-time IPC/PB/ring/stats flags) | DONE |
| SHMEM ring overflow fix (16→15 slots, _Static_assert) | DONE |
| Fuzz testing harness (net_stack, shmem_ipc, gguf_parser, 300K iterations) | DONE |
| Phase 3c security audit: 14 findings (4 HIGH, 3 MED, 4 LOW, 3 INFO) | DONE |
| All 8 HIGH/MED findings fixed (fat32, nvme, nic_i211, vga, tokenizer) | DONE |
| Phase 3c audit gap fill: 11 new findings (1 HIGH, 2 MED, 4 LOW, 4 INFO) — pci, ahci, uart_16550, posix_stubs | DONE |
| All 4 HIGH/MED gap-fill findings fixed (pci BAR5, ahci buf_len overflow, uart timeout, posix clock_gettime honest failure) | DONE |
| Model bench-off: open discovery (20 models surveyed) | DONE |
| Model bench-off: compatibility verification (GGUF byte-verified) | DONE |
| Model bench-off: speed bench (11 models, Ryzen 2700X + Ryzen 5600 + RTX 2070) | DONE |
| Model bench-off: perplexity bench (WikiText-2 full corpus, 10 models, RTX 2070) | DONE |
| Model bench-off: quality bench (10 prompts, 11 models, Claude-judged blind) | DONE |
| **Bench-off winner: Gemma 4 E2B (8.40/10 avg, 7 judges, 19.7 tok/s) — single-model deployment** | **DECISION** |
| **Llama 3.1 8B disqualified** — 5.06/10 (#8 of 11), training data contamination | **DROPPED** |
| Bench-off deferred contenders (Gemma 4, Qwen3, Qwen3.5 SSM) — ALL since IMPLEMENTED (rows below); RotorQuant/TurboQuant evaluated → deferred to Phase 4 (ADR 2026-06-15) | DONE |
| **Gemma 4 E2B engine: ~4,400 LOC across 27 files — 17 fixes on `feature/gemma4-arch`** | **DONE** |
| **Gemma 4 E2B validated: main PC native test — coherent English** | **DONE** |
| **Gemma 4 E2B validated: JARVIS PC seL4 QEMU Process B (4/4 queries coherent)** | **DONE** |
| **JARVIS engine bench harness (bench_engine.c, llama-bench table format)** | **DONE** |
| **JARVIS engine bench: 11/11 models load, 6 model families supported** | **DONE** |
| Fused QKV + fused gate-up tensor support (Phi-3) | DONE |
| Phi-3 + Qwen3 chat template wrapping in bench_engine | DONE |
| Partial RoPE support (Qwen3.5: 64 of 256 dims) | DONE |
| **Gated DeltaNet SSM implementation (ssm.c/h, ~1200 LOC, 7 unit tests)** | **DONE** |
| **Qwen3.5 4B + 9B hybrid SSM — loads and generates** | **DONE** |
| Zero-embedding forward for invalid tokens (SSM temporal continuity) | DONE |
| NVMe write logging (raw sector telemetry) | DONE |
| NVMe log auto-capture (puts_serial intercept, [VGA]/[SER] tags) | DONE |
| MSG_DEBUG IPC for Process B -> NVMe log ([PB] tag) | DONE |
| NVMe log bare-metal verified (boot 5, full boot timeline captured) | DONE |
| **Bare-metal IPC inference VERIFIED: 7 queries, 0 crashes (stack overflow fixed)** | **DONE** |
| Bare-metal debug: equal prio, poll timeout, ring overflow, QEMU NVMe, probe, fwd_scratch | DONE |
| Fused qdot (7 types, AVX2) + SIMD attn + RoPE tables + pthread threadpool | DONE |
| **Performance: Llama 1B 0.99 -> 3.22 (1T) -> 19.79 tok/s (16T)** | **DONE** |

**Next:** **Phase 4 engineering COMPLETE — only the v1.0.0 release remains.** Goal #1 (inference perf, CPU) DONE (Gemma 4 E2B 5.46 tok/s @ NUM_NODES=6, M0–M4); #2 graphical output, #2b remote telemetry console, #4 installer (usb/esp/disk), #6 docs all DONE; **#3 USB keyboard CUT** (headless appliance — interactive input is Phase 6 console control-IN, not a local keyboard). #5 90-day soak NOT run — owner-scheduled. #7 v1.0.0 MIT release IN PROGRESS — LICENSE + THIRD_PARTY_LICENSES + doc-honesty pass + `phase4/docs/PHASE_4_FINAL_REPORT.md` done; tag not yet cut (owner names/cuts it). GPU inference DEFERRED (no usable GPU — ADR `docs/decisions/2026-06-16-defer-gpu-inference.md`); TurboQuant/RotorQuant deferred (ADR `docs/decisions/2026-06-15-defer-turboquant-rotorquant-to-phase4.md`). **Phase 5 (Memory) is the next phase** — see `phase4/docs/ROADMAP.md`.

### Pre-Work Tasks (Before JARVIS Project PC)

| # | Task | Status |
|---|------|--------|
| 1 | seL4 x86-64 on QEMU (main PC WSL) | DONE — 123/123 tests pass, build env documented |
| 2 | ggml standalone musl compilation test | DONE — 5 stubs needed, C++ backend is main challenge |
| 3 | Shared memory IPC protocol design + test | DONE — 10/10 PASS, 23.7M msg/sec |
| 4 | Port portable code to x86 build | DONE — 22/22 tests pass, 5/7 modules zero changes |
| 5 | x86 driver skeleton headers | DONE — uart_16550.h, pci.h, ahci.h |
| 6 | Git tag v0.2.0-alpha | DONE |
| 7 | Pi 5 llama.cpp benchmark | DONE — script + research estimates written |
| 8 | Custom rootserver in QEMU | DONE — builds and runs in QEMU, cache loads 308 patterns, SHIELD works |

### Phase 3 Early Work (Done in QEMU — ahead of schedule)

| Component | Planned Week | Status | Files | Tests |
|-----------|-------------|--------|-------|-------|
| GGUF parser (C-only) | 19-20 | DONE | gguf_parser.h/c | 12 PASS |
| UART 16550A driver | 13-14 | DONE | uart_16550.c/h | 7 PASS |
| PCI enumeration | 15-16 | DONE | pci.c/h | 11 PASS |
| AHCI full I/O | 15-16 | DONE | ahci.c/h | 13 PASS |
| NIC RTL8168 (TX + RX delivery) | 17-18 | DONE | nic_rtl8168.c/h | 7 PASS |
| VGA text mode driver | 13-14 | DONE | vga_text.c/h | 7 PASS |
| x86 Timer (PIT/HPET/TSC) | 13-14 | DONE | x86_timer.c/h | 8 PASS |
| Block device abstraction | 15-16 | DONE | blk_dev_x86.c/h | 9 PASS |
| C tensor ops (9 ops, AVX2+FMA) | 19-20 | DONE | tensor_ops.c/h | 12 PASS |
| Dequantization (Q4_0/Q8_0/F16/Q4_K/Q6_K) | 19-20 | DONE | dequant.c/h | 36 PASS |
| BPE Tokenizer | 21-22 | DONE | tokenizer.c/h | 12 PASS |
| Model architecture + loading | 21-22 | DONE | llama_model.h, llama_load.c | 7 PASS |
| Sampling (greedy + top-k) | 21-22 | DONE | sampling.c/h | 9 PASS |
| Transformer forward pass | 21-22 | DONE | llama_forward.c | 9 PASS |
| Inference API | 21-22 | DONE | inference.c/h | 4 PASS |
| Shared memory IPC | 23-24 | DONE | shmem_ipc.c/h | 10 PASS |
| Quantized zero-copy inference | 21-22 | DONE | llama_quant.c/h | 10 PASS |
| GGUF vocab extraction | 21-22 | DONE | gguf_vocab.c/h | 10 PASS |
| GGUF memory API | 19-20 | DONE | gguf_parser.c/h (extended) | 7 PASS |
| F32 vs quantized comparison | — | DONE | test_forward_compare.c | SKIP on CI |
| Custom x86 rootserver | 9-12 | DONE (QEMU) | main_x86.c | 5/5 self-test + 4/4 inference PASS |
| SHIELD safety module | — | DONE | shield.c/h | 8 PASS |
| Generation quality tests | — | DONE | test_generation.c | 6 PASS (model needed) |
| IPC CRC-32 (SEC-020) | — | DONE | shmem_ipc.c/h | 3 PASS (13 total) |
| GPT-2 full byte mapping | — | DONE | tokenizer.c | 12 PASS |
| Tiled matmul + unroll | — | DONE | tensor_ops.c, llama_quant.c | 14+10 PASS |
| RDTSC timing (Stage 5) | — | DONE | main_x86.c | 5/5 stages PASS |
| NVMe driver (polled I/O) | 15-16 | DONE | nvme.c/h | 10 PASS |
| FAT32 read-only parser | 15-16 | DONE | fat32.c/h | 8 PASS |
| NIC Intel I211 (TX + RX polled) | 17-18 | DONE | nic_i211.c/h | 11 PASS |
| Net UDP builder (Eth/IPv4/UDP broadcast) | — | DONE | net_udp.c/h | 24 PASS |
| Telemetry packet (binary, JTEL, zlib CRC-32) | — | DONE | jarvis_telemetry.c/h | 22 PASS |
| Fused QKV + gate-up tensor split | — | DONE | llama_quant.c | (load-time, no new tests) |
| SSM / Gated DeltaNet (Qwen3.5) | — | DONE | ssm.c/h | 7 PASS |
| Engine bench harness | — | DONE | bench_engine.c | (compile-only CI) |
| AVX2 qdot correctness (-mavx2) | — | DONE | test_avx2_correctness.c | 24 PASS |
| NVMe write (opcode 0x01) | — | DONE | nvme.c (extended) | 10 PASS |
| NVMe log module | — | DONE | nvme_log.c/h | (bare-metal only) |
| NVMe log parser | — | DONE | parse_nvme_log.py | (Python) |
| Threadpool join race fix + parallel_for ABI test | — | DONE | threadpool.c, test_parallel_for.c | ~6 PASS |
| seL4-native threadpool (M3) | — | DONE | threadpool_sel4.c, main_x86.c, inference_server.c | bare-metal (Gemma 5.46 tok/s @ NN=6) + host CI (H1) |
| seL4 threadpool host CI (M3, H1) | — | DONE | test_threadpool_sel4.c, test_sel4_stubs/sel4/sel4.h | host CI: 25/25 work-steal/join, TSan + O2, threads 1/2/4/6/8 |
| **Total** | | | **123 code files (41 test)** | **383+ test asserts, ~35,700 LOC** |

**Phase 3b on real hardware — bare-metal burn-in passed (2026-06-15, NOT a 30-day soak):** clean boot, NVMe model load (LBA-32768), coherent Gemma 4 E2B, heartbeat/shield IPC fix holding at err=0 over 400 queries (q=100/200/300/400), durable NVMe logging with constant boot_id. Formal 30-day x86 soak **DEFERRED — descoped from v0.2.1-beta gating (risk-accepted)**: the new x86 surfaces (shmem_ipc, in-process inference, NVMe load) are covered by this ~400-query burn-in + 300K-iter fuzz + 2 audits + the IPC fix; Phase 2's 30.6-day Pi 4 run (different IPC/ARM stack) is supporting context, not equivalent coverage. See `docs/decisions/2026-06-15-defer-30-day-x86-stability-soak.md`.

### Phase 3 Weeks

| Weeks | Task |
|-------|------|
| 1-6 | Phase 3a: GPU benchmarks + native Linux build env (COMPLETE) |
| 7-28 | Phase 3b: Pure bare-metal seL4 x86-64 — BARE-METAL BOOT ACHIEVED |
| 29-40 | Phase 3c: Hardening, fuzz testing, SHIELD |
| 41-44 | Final report + git tag v0.2.1-beta |

### Validated Metrics

| Metric | Target | Actual |
|--------|--------|--------|
| IPC latency | <100us | 54us (Phase 1), 7ms UART (Phase 2) |
| Cache hit rate | >80% | 85.7% |
| AI inference | <500ms | 558ms CPU Phi-3, <100ms Llama 3.2 1B (host PC benchmarks, no log artifact) |
| JARVIS engine (quantized, 1T) | — | Llama 1B: 3.22, Gemma 4 E2B: 1.70, Mistral 7B: 0.56 tok/s |
| JARVIS engine (quantized, 16T) | — | Llama 1B: 19.79, Gemma 4 E2B: 8.63, Mistral 7B: 4.16 tok/s |
| llama.cpp ref (8T) | — | Llama 1B: 40.4, Gemma 4 E2B: 19.7 tok/s |
| **seL4 build, bare metal** (M1 1T → M3 threaded) | — | M1 (1T AVX2): **Gemma 4 E2B ~1.53 tok/s** (vs ~0.2 scalar, ~7.6×). **M3 (2026-06-18): Gemma 4 E2B 5.46 tok/s @ `NUM_NODES=6` threaded (3.57× the 1.53 1T; bare-metal N-sweep, err=0 over 800 q; NN=4 = 4.21).** Held under M2 SMP with no 1T penalty. NOTE: the three rows above are the NATIVE dev engine — do not conflate with this deployed seL4-build figure. |
| Models supported | — | 11/11 load, 6 model families (Llama, Gemma 4, Phi-3, Mistral, Qwen3, Qwen3.5 DeltaNet SSM) |
| Boot time | <60s | ~2s |
| SHIELD block rate | >90% target | **Host harness only: 100% harmful blocked / 0% FP (test_shield.c on shield.c). LIVE PA↔PB path is passive/monitoring — Process B returns ALLOW, shield.c not linked (SEC-039); NOT a live blocker.** |
| Multi-agent routing | >90% | 100% |

---

## Key Technical Notes

### UART IPC Protocol

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  SYNC    │  TYPE    │   SEQ    │  LENGTH  │  FLAGS   │ PAYLOAD  │  CRC16   │
│ (2 bytes)│ (1 byte) │ (2 bytes)│ (2 bytes)│ (1 byte) │ (0-240)  │ (2 bytes)│
│  0xAA55  │  0x01-0E │  0-65535 │  0-240   │  0x00    │  data    │ CRC-CCITT│
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
```

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| QUERY | 0x01 | Py→seL4 | Cache lookup |
| RESPONSE | 0x02 | seL4→Py | Cache result |
| HEARTBEAT | 0x03 | Both | Keep-alive |
| HEARTBEAT_ACK | 0x04 | Both | Keep-alive ack |
| STATS_REQUEST | 0x05 | Py→seL4 | Cache stats req |
| STATS_RESPONSE | 0x06 | seL4→Py | Cache stats |
| COMMAND | 0x07 | Py→seL4 | Shell command |
| COMMAND_RESULT | 0x08 | seL4→Py | Command output |
| SHIELD_CHECK | 0x09 | Py→seL4 | Risk assessment |
| SHIELD_RESULT | 0x0A | seL4→Py | Risk decision |
| ERROR | 0x0B | Both | Error |
| RESET | 0x0C | Both | Protocol reset |
| STATE_CHANGE | 0x0D | Py→seL4 | State change |
| STATE_ACK | 0x0E | seL4→Py | State ack |

Baud: 115200. RTT: 7ms measured. Heartbeat: 5s. Timeout: 30s.
Full spec: `phase2/docs/UART_IPC_PROTOCOL.md`

### BCM2711 Hardware

```
GENET Ethernet:  0xFD580000  (separate device untyped!)
Peripheral Base: 0xFE000000
System Timer:    0xFE003000  (1 MHz free-running counter)
GPIO:            0xFE200000
UART0 (PL011):   0xFE201000
EMMC/SDHCI:      0xFE340000
BSC1 I2C:        0xFE804000  (I2C master, 100/400 kHz)
DWC2 USB:        0xFE980000  (USB OTG host controller)
```

USB-Serial wiring: GPIO14(TXD)→RXD, GPIO15(RXD)←TXD, GND─GND

### SD Card Boot (U-Boot)

```
Boot Partition (FAT32):
├── start4.elf      # GPU firmware
├── fixup4.dat      # Memory config
├── u-boot.bin      # U-Boot 2026.01 bootloader
├── boot.scr        # U-Boot boot script
├── kernel8.img     # JARVIS seL4 boot image
├── bcm2711-rpi-4-b.dtb  # Device tree
└── config.txt      # arm_64bit=1, kernel=u-boot.bin, enable_uart=1

Boot flow: GPU firmware → U-Boot → boot.scr → kernel8.img → seL4
```

U-Boot: Press key during 3s countdown for interactive shell. Auto-boot loads kernel8.img at 0x00080000.
Backup: `temp_sd_backup/uboot_working/`

### seL4 Device Mapping Rules

- **Forward-only cursor:** seL4 Untyped_Retype watermark only moves forward. Map devices in ascending paddr order.
- **VSpace range:** vaddr must be within mapped VSpace (0x400000-0x5b9fff). Error 6 = seL4_FailedLookup means vaddr not in VSpace.
- **Init order:** systimer(0xFE003000) → DMA(0xFE007000) → mailbox(0xFE00B000) → watchdog(0xFE100000) → RNG(0xFE104000) → UART/GPIO(0xFE200000-0xFE201000) → SPI(0xFE204000) → PWM(0xFE20C000) → EMMC(0xFE340000) → I2C(0xFE804000) → USB(0xFE980000)
- **Binary buddy skip:** When timer is mapped before UART, use power-of-2 Untyped retypes to advance watermark from 0xFE004000→0xFE200000 (7 retypes: 16KB→32KB→64KB→128KB→256KB→512KB→1MB) instead of 2MB LargePage skip (which would consume GPIO's frame).
- **Device cursor after mapping:** GPIO_BASE + 2*4KB = 0xFE202000
- **DMA = uncacheable:** seL4 does NOT set `SCTLR_EL1.UCI`, so ALL cache maintenance instructions (`DC IVAC/CIVAC/CVAC`) trap from EL0. Map DMA buffers with `vm_attributes = 0` (uncacheable) instead.

### Virtual Address Layout

```
0x5c0000 - UART PL011 (hardcoded)
0x5c1000 - GPIO (hardcoded)
0x5c2000 - System Timer (auto-assigned)
0x5c3000 - EMMC (auto-assigned)
0x5c4000-0x603FFF - DMA pool (256KB)
0x604000-0x609FFF - GENET MMIO (6 pages, own device untyped)
0x610000 - VideoCore Mailbox (explicit vaddr, maps 0xFE00B000)
0x611000 - PM Watchdog (explicit vaddr, maps 0xFE100000)
0x612000 - DMA Engine (explicit vaddr, maps 0xFE007000)
RNG, SPI, PWM - auto-assigned (0xFE104000, 0xFE204000, 0xFE20C000)
0x60A000-0x60CFFF - DWC2 USB (3 pages, paddr 0xFE980000)
```

### Phase 1 IPC Limitation

Phase 1 used "mock IPC" - Python and seL4 did NOT communicate in real-time. Separate memory spaces. Both proven independently, connected in Phase 2 via UART.

### NVMe Write Logging

- Log region: LBA 4000794624 (past p4, the 1.7G vfat tail partition). Drive confirmed via lsblk: **Lexar NM790 2TB**, 4,000,797,360 sectors (1907 GiB). The log's 2700 sectors fit the ~2,736-sector tail with only ~36 sectors (~18 KB) of headroom — TIGHT. (nvme_log.h's header comments are now corrected to match — "Lexar NM790 2TB", "~2,736-sector tail, ~36 sectors spare".)
- Max entries: 2700 — **circular / rolling buffer** (keeps the latest 2700; never fills): `cursor` is the wrapping write slot (0..2699), `total_entries` is the monotonic lifetime count, and `nvme_log_cursor()` returns entries-stored-capped (rolling-full once total ≥ 2700). `nvme_log_write` no longer returns -2; a stale pre-wrap cursor (== 2700 from the old no-wrap build) is clamped on init. The telemetry `log_cursor` field value/shape is **unchanged** by this (identical until the first wrap) — no wire-contract change.
- Periodic stats cadence: serial `[STATS]` prints every 100 queries (free); the NVMe `LOG_IPC_STATS` entry is written every `JARVIS_STATS_NVME_INTERVAL` = **100** queries (`jarvis_debug.h`). Justified by the **measured** bare-metal rate ~3k queries/day (single core, scalar) → ~870 entries over 30 days, well under the 2700-entry cap. (Task 3 originally used 1000 on a 16k/day estimate the bare-metal run disproved; q=400 err=0 verified on real hardware 2026-06-15. If a future build speeds the query rate up ~5x+, raise this back toward 1000.)
- Format: 512-byte records (header + entries), XOR checksum
- Auto-capture: `puts_serial()` intercept writes every line to NVMe log
- Tags: `[VGA]` = visible on monitor, `[SER]` = serial only, `[PB]` = Process B via MSG_DEBUG IPC
- Recovery: `sudo dd if=/dev/nvme0n1 bs=512 skip=4000794624 count=2701 | python3 phase3/scripts/parse_nvme_log.py` (count=2701 = header + all 2700 slots; the parser reads wrap-order — oldest→newest from `cursor` — once the buffer has rolled)
- `seL4_Yield` only yields to equal-priority — PA and PB both at MaxPrio

### Bare-Metal Debugging Rules

- Use `#if EMMC_DEBUG` preprocessor guards for diagnostics, NOT code deletion (deleting debug code caused mysterious boot failures)
- `seL4_DebugPutChar()` works for TX without device frame mapping
- Device frame mapping required for UART RX
- Serial console: 115200 baud, 8N1
- `put_dec()` and `put_hex()` go through `putc_serial()` -> NVMe log. To suppress NVMe logging temporarily (e.g. verbose dumps), save/clear `g_nvme_ptr`.
- Process A and Process B run at `seL4_MaxPrio` (equal priority). `seL4_Yield` only works between equal-priority threads — MaxPrio-1 gets starved.
- Response ring has 15 slots. PB must reserve 3 for MSG_RESPONSE — use `pb_can_log()` before debug writes.
- `qmodel_forward` stack budget: keep below ~8KB. Large temporaries (>4KB) MUST use `state->fwd_scratch` heap buffer. seL4 Process B has limited stack.
- **PA response consumption: POLL the ring, do NOT `seL4_Wait(resp_notif)` in the workload loop.** The inference path polls and never consumes `resp_notif`, leaving it signaled; heartbeat/shield used to `seL4_Wait(resp_notif)` + a single recv, which returned immediately on that stale signal and read before PB responded (~7% spurious errors, all in hb/shield). Fixed via `wait_for_response()` which polls the ring for the expected type and drains stale messages (race-free, like inference). The only legit `seL4_Wait(resp_notif)` is the pre-loop ready handshake. NOTE: the race is **Heisenbug** — enabling `JARVIS_DBG_IPC` serial prints inserts a scheduling window that masks it, so verify by err-rate ([STATS]) at `JARVIS_DBG_IPC=0`, not per-event logs.

---

## For Claude Code

### Reading Order (New Session)
1. This file (CLAUDE.md) → architecture + current status
2. `phase4/docs/PHASE_4_FINAL_REPORT.md` → current-phase (Phase 4 / v1.0) summary + honest scoreboard + ADR pointers
3. `phase4/weeks/week07/WEEK_07_STATUS.md` → latest Phase 4 weekly detail
4. `phase3/docs/PHASE_3_FINAL_REPORT.md` → Phase 3 (beta) summary (history)
5. Source files as needed

### Quick Reference
- **Build:** `wsl -e bash -lc "cd .../phase2/scripts && tr -d '\r' < build_and_copy_kernel.sh | bash"`
- **Deploy:** `copy phase2\firmware\kernel8.img D:\`
- **Serial:** 115200 baud, Pi 4 GPIO14/15
- **Main source:** `phase2/src/sel4/main_arm64.c`
- **UART driver:** `phase2/src/drivers/uart_pl011.c`
- **EMMC driver:** `phase2/src/drivers/emmc_sdhci.c`
- **Timer driver:** `phase2/src/drivers/bcm2711_timer.c`
- **Slot allocator:** `phase2/src/drivers/slot_alloc.c`
- **DMA allocator:** `phase2/src/drivers/dma_alloc.c`
- **Block device:** `phase2/src/drivers/blk_dev.c`
- **GENET Ethernet:** `phase2/src/drivers/bcm_genet.c`
- **Net Stack:** `phase2/src/drivers/net_stack.c`
- **Net Commands:** `phase2/src/drivers/net_cmd.c`
- **USB HID Keyboard:** `phase2/src/drivers/usb_hid.c`
- **GPIO Driver:** `phase2/src/drivers/bcm_gpio.c`
- **I2C Driver:** `phase2/src/drivers/bcm_i2c.c`
- **Watchdog driver:** `phase2/src/drivers/bcm_watchdog.c`
- **Thermal driver:** `phase2/src/drivers/bcm_thermal.c`
- **Power manager:** `phase2/src/ai/power_manager.py`
- **FDT parser:** `phase2/src/boot/fdt_parser.c`
- **Device tree source:** `phase2/src/boot/jarvis.dts`
- **Embedded DTB:** `phase2/src/boot/jarvis_dtb_data.h`
- **Boot manager:** `phase2/src/boot/boot_manager.c`
- **Warm reboot:** `phase2/src/boot/warm_reboot.c`
- **Power manager:** `phase2/src/drivers/bcm_power.c`
- **SPI driver:** `phase2/src/drivers/bcm_spi.c`
- **RNG driver:** `phase2/src/drivers/bcm_rng.c`
- **PWM driver:** `phase2/src/drivers/bcm_pwm.c`
- **DMA engine:** `phase2/src/drivers/bcm_dma.c`
- **Stability harness:** `phase2/src/ai/stability_harness.py`
- **Build config:** `phase2/src/jarvis-sel4-cmake/CMakeLists.txt`
- **SD Image Builder:** `phase2/scripts/build_installer_image.sh`
- **SD Flasher:** `phase2/scripts/flash_sd.sh`
- **Installer (Linux):** `phase2/scripts/install_jarvis.sh`
- **User Guide:** `phase2/docs/USER_GUIDE.md`
- **Tester Guide:** `phase2/docs/ALPHA_TESTER_GUIDE.md`
- **Platform Guide:** `phase2/docs/PI4_PLATFORM_GUIDE.md`
- **Security Self-Audit:** `phase2/docs/SECURITY_SELF_AUDIT.md`
- **Adversarial Security Audit:** `phase3/docs/SECURITY_AUDIT_2026-03-22.md`
- **Phase 2 Final Report:** `phase2/docs/PHASE_2_FINAL_REPORT.md`
- **Phase 3 Hardware Research:** `phase3/docs/PHASE_3_HARDWARE_RESEARCH.md`
- **Phase 3 Kickoff:** `phase3/docs/PHASE_3_KICKOFF.md`
- **Phase 3 Implementation Plan:** `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md`
- **Phase 3 Final Report:** `phase3/docs/PHASE_3_FINAL_REPORT.md` — beta summary (soak/scaling/TQ-RQ deferred; v0.2.1-beta tagged 06de75c)
- **Decision records (ADRs):** `docs/decisions/` — dynamic-scaling removal (2026-04-17), 30-day soak deferral + TurboQuant/RotorQuant deferral (2026-06-15), GPU-inference deferral (2026-06-16), x86 verification stance (2026-06-16), **enable SMP / Branch A (2026-06-17)**
- **Phase 4 Goal #1 (Inference Perf) Plan:** `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` — AVX2/XSAVE + SMP findings (VERIFIED) + M0–M4 plan (all DONE — goal #1 CPU COMPLETE 2026-06-18)
- **Phase 4 Goal #1 Benchmark (M4):** `phase4/docs/PHASE_4_GOAL1_BENCHMARK.md` — recorded seL4-build tok/s (Gemma 4 E2B **5.46 @ NN=6**, 3.57× 1T; progression + F1/F2 sweep tables + methodology + honest target-vs-actual)
- **Phase 4 Final Report:** `phase4/docs/PHASE_4_FINAL_REPORT.md` — v1.0 final report (what shipped per goal, metrics scorecard, honest success-criteria table — #3 keyboard CUT / #5 soak NOT run, #7 release in progress — limitations, Phase 5 handoff, fresh codebase stats); mirrors `phase3/docs/PHASE_3_FINAL_REPORT.md`
- **Phase 5 Plan (Memory):** `phase5/docs/PHASE_5_PLAN.md` — keystone-first plan (mission + 5 canon done-when; "it-remembers" MVP arc #1/#2/#3/#6 then #4/#5/#7; locked decisions; VERIFIED on-box NVMe free-space map + raw-LBA storage decision in the ~1.66 TiB gap; risks; Phase 5/6/7 boundary). Mirrors `phase4/docs/ROADMAP.md`.
- **Phase 5 Goal #1 (Episodic Store, KEYSTONE):** `phase5/docs/PHASE_5_GOAL1_EPISODIC_STORE.md` — raw-LBA 512 B record store (nvme_log template), batched Process-A write path, `parse_episodic.py`, milestones M0–M4. Mirrors `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md`.
- **Episodic Store (Phase 5 G1/M0):** `phase3/src/ai/episodic_store.c/h` — raw-LBA circular 512 B-record store (nvme_log pattern, struct+callback-driven so the later semantic/SHIELD stores reuse the core; no device dependency — M1 wires the NVMe callbacks). Record = `{boot_id, seq, t_ms, query_key, action, outcome, feedback, query, resp}`; `query_key = cache_hash(cache_normalize_query(query))` (decision-cache FNV-1a parity for #6 cache-growth). Host-tested `test_episodic_store.c` (7/7: fresh-init, append+read-back round-trip, boot_id increment, checksum corruption, wrap+migration, key-parity, batched-fill) → CI step "Phase 5: Episodic Store (C)". **M1 (2026-06-27):** `episodic_fill` (fill-only) split out, `epi_store_append` now stamps boot_id/seq at write time; wired into Process A (`main_x86.c`) — `epi_batch_add` fills a RAM batch per cache/inference query, `epi_commit` flushes to NVMe (via `epi_nvme_read/write` bounce) at the [STATS] cadence (gated on `g_episodic_ready`); `build_jarvis_x86.sh` compiles it into the Process-A source list. Box build + QEMU/KVM smoke pending (no host compile of `main_x86.c`). **M1a (2026-06-27):** episodic init decoupled from nvme_log (was nested in its success block → never ran when nvme_log_init fails, incl. the 12 G QEMU image); now gated only on g_nvme_ptr/bounce. Box smoke re-run pending. **M2 (2026-06-27):** `parse_episodic.py` (reads the raw region from a file/`dd` pipe, validates the JEPI header XOR, wrap-aware, decodes `epi_record_t`) + `test_parse_episodic.py` C->Python round-trip (C `--dump` fixed fixture → parse → field-exact + FNV-1a key parity, teeth-proven) → CI step "Phase 5: Episodic Parser round-trip (Python)". Canon done-when "episodic log readable/parseable" met (host); box-real `dd` parse pending.
- **seL4-build bench tool:** `phase3/scripts/bench_sel4_inference.sh` — canonical seL4-build tok/s measurement (`qemu`|`baremetal` modes; `JARVIS_M1_MEASURE` RDTSC; self-cleaning — resets measure flags on exit; CI: N/A, drives the box)
- **Phase 4 M3 Threadpool Design:** `phase4/docs/PHASE_4_M3_THREADPOOL_DESIGN.md` — seL4-native worker pool (verified API; PA allocates workers since PB has no allocator; N≈5–6 bandwidth knee; §Implemented = as-built, shipped at M3 as `threadpool_sel4.c`)
- **Phase 4 Weeks:** `phase4/weeks/weekN/WEEK_N_STATUS.md` (latest: **week06** — M4 benchmark recorded, goal #1 CPU COMPLETE; week05 — M3 seL4 threadpool, Gemma 5.46 tok/s @ NN=6; week04 — M2 SMP; week03 — M1 AVX2)
- **seL4 x86 QEMU Setup:** `phase3/docs/SEL4_X86_QEMU_SETUP.md`
- **x86 Rootserver Notes:** `phase3/docs/SEL4_X86_ROOTSERVER_NOTES.md`
- **GGML Portability Notes:** `phase3/src/ai/GGML_PORTABILITY_NOTES.md`
- **GGUF Parser:** `phase3/src/ai/gguf_parser.c/h`
- **x86 Rootserver:** `phase3/src/sel4/main_x86.c`
- **Shared Memory IPC:** `phase3/src/ipc/shmem_ipc.c/h`
- **USB Boot Creator:** `phase3/scripts/create_boot_usb.sh` — boot USB is now a RECOVERY/re-flash device; the DEPLOYED path is on-SSD dual-boot (`install_jarvis_x86.sh --target esp`, VERIFIED on-box 2026-06-25)
- **UART 16550A Driver:** `phase3/src/drivers/uart_16550.c`
- **PCI Enumeration:** `phase3/src/drivers/pci.c`
- **AHCI Discovery:** `phase3/src/drivers/ahci.c`
- **Bare-Metal Boot Guide:** `phase3/docs/BARE_METAL_BOOT_GUIDE.md`
- **User Guide (v1.0, x86):** `phase4/docs/USER_GUIDE.md` — x86 headless-appliance user guide (install via installer/create_boot_usb incl. the `sudo HOME=/home/jarvis` gotcha; NVMe model partition @ LBA 32768 / GEMMA2B.GUF; BIOS checklist; 1024×768 first-boot HUD; network-telemetry validation via Windows-native `py -3 telemetry_receiver.py` (not WSL); read-only console scope; honest limitations; **on-SSD dual-boot via `--target esp` is the deployed path on the box, the boot USB now recovery/re-flash**). goal #6.
- **Block Device (x86):** `phase3/src/drivers/blk_dev_x86.c/h`
- **x86 Timer:** `phase3/src/drivers/x86_timer.c/h`
- **NIC RTL8168:** `phase3/src/drivers/nic_rtl8168.c/h`
- **NIC Intel I211:** `phase3/src/drivers/nic_i211.c/h` — **LIVE (goal #2b N-a)**: TX-only first-light on bare metal. `i211_nic_init` (reset settle + STATUS.PF_RST_DONE + MAC-validity via RAH.AV + TXDCTL.ENABLE), `i211_send_phys` (writes the TX buffer's PHYSICAL addr — IOMMU off). Rootserver brings it up NON-FATAL (BAR0 32pp UC + 2 DMA frames, DD-polled) in the `main_x86.c` `[NET]` block. Host `test_nic_i211.c` 11/11 (B1 phys-addr + B2 TXDCTL).
- **Net UDP Builder:** `phase3/src/drivers/net_udp.c/h` — dependency-free Eth/IPv4/UDP limited-broadcast frame builder (`net_build_udp_broadcast` + RFC1071 `net_ip_checksum`; big-endian headers; UDP cksum=0 per IPv4; pad ≥60) wrapped around `i211_send_phys`; goal #2b N-b telemetry-OUT framing — the `[NET]` block emits a UDP broadcast to 255.255.255.255:51000. Host `test_net_udp.c` 24/24 (Wireshark-verified valid IP checksum on the box).
- **Telemetry Packet:** `phase3/src/drivers/jarvis_telemetry.c/h` — 200-byte packed versioned `telemetry_packet_t` + zlib/IEEE CRC-32 (`jarvis_tlm_crc32`/`jarvis_tlm_finalize`); the `main_x86.c` workload loop fills it from live state and emits it over the I211 (`net_udp` + `i211_send_phys`, fire-and-forget) at the `[STATS]` cadence + a 1 Hz keepalive (also ticked from the inference response-poll → true ~1 Hz). **System fields (Tier 0/1):** `total_ram_mb` is now real (sum of non-device untypeds) and 4 fields ride former reserved bytes — `infer_active`, `infer_duty_pct` (workload duty cycle = inference cycles/uptime, NOT a CPU gauge — PA busy-polls), `nvme_total_mb`, `log_cursor` (telemetry-log fullness, of 2700); packet stays 200 B, CRC over [:196] unchanged. goal #2b N-c-1 telemetry-OUT. Host `test_jarvis_telemetry.c` 29/29.
- **Telemetry Receiver / SSE bridge:** `phase3/scripts/telemetry_receiver.py` — Main-PC Python UDP receiver for the N-c-1 telemetry_packet_t (decode + zlib CRC-32 validate + seq-gap drop detection; `--once/--follow/--json`; honest fields only). `--sse` runs a stdlib `http.server` bridge: a `/events` `text/event-stream` of decoded packet records (`packet_to_record` — real fields + `crc_ok`, no fabricated keys) + static serving of `phase4/console`; `--replay <pcap>` drives it from a captured pcap for box-free dev (`packet_to_record`/`iter_pcap_telemetry` host-tested). Test `test_telemetry_receiver.py` (C↔Python wire-compat + golden key-contract, 81/81). goal #2b N-c-2 (receiver) + N-c-3a (SSE bridge). `--web-dir` defaults to the repo's `phase4/console` (script-relative — the console serves from ANY working directory; explicit `--web-dir` still honored). Run it Windows-native (`py -3`), NOT WSL — WSL2 NAT can't see the box's LAN broadcast.
- **Telemetry Console:** `phase4/console/` — honest, read-only browser console for the Remote Telemetry Console (7 screens: CommandCenter / Routing / LastResponse / Models / SHIELD / **Capabilities** / **System** — Capabilities auto-populated from the telemetry `flags_list` per the UI–feature-parity rule: one row per reported flag, unknown flags surfaced as "new capability" never dropped, plus derived-from-real-field rows; **System** renders ONLY the real System telemetry: RAM available to JARVIS (`total_ram_mb`), model resident + fixed floor, inference ACTIVE/IDLE + workload duty cycle, NVMe size + telemetry-log fullness — no CPU% gauge, no disk-activity graph, no fabricated used/free/IOPS/SMART). Reuses the JARVIS design system; served by `telemetry_receiver.py --web-dir phase4/console`; consumes the `/events` SSE (`EventSource`) with recv_ts liveness + stale watchdog, CRC gate, seq-gap drop count, cold-start guards, and one real rolling-buffer sparkline; box-free preview is clearly badged **SIMULATED**. No GPU / live-tok-s / model-tiers / SHIELD-blocking / "verified" — CI-enforced by `phase4/console/test_console_honesty.py` (honesty grep over the authored source, 40/40). goal #2b N-c-3b/c/d. (`_ds_bundle.js`/`styles.css`/`tokens/` are the vendored DS runtime — excluded from the honesty scan, as they carry the other kits' demo data.)
- **Console vendored libs:** `phase4/console/vendor/` — pinned offline runtime libs (React 18.3.1 + ReactDOM dev builds, `@babel/standalone` 7.29.0, **Lucide 1.21.0**) so CI/serve is hermetic — no live CDN (replaced the `lucide@latest` supply-chain risk). `index.html` loads `./vendor/*.js` (no SRI needed for local files). The honesty gate's non-recursive `os.listdir` scan does not reach this subdir.
- **Console golden fixture:** `phase4/console/fixtures/golden_telemetry.json` — the single source for the `/events` key contract (`meta.keys`/`fmt`/`size` + a deliberate frame sequence: loading×2, STATS, INFER, STATE, HAS_ERROR, seq-gap, corrupt) — plus `golden.pcap` (generated, committed; CI-gated for drift).
- **Telemetry fixture (shared packer):** `phase3/scripts/telemetry_fixture.py` — `build_packet`/`build_pcap_many`/`frame_to_packet` + `REQUIRED_RECORD_KEYS` (derived from `packet_to_record`, never hardcoded). Used by `test_telemetry_receiver.py` (key-contract Layer A → 81/81) and `gen_golden_pcap.py`.
- **Golden pcap generator:** `phase3/scripts/gen_golden_pcap.py` — regenerates `golden.pcap` from the JSON (guards `struct.calcsize==200` + the canonical 0xCBF43926 CRC vector); CI step "Phase 4: Golden telemetry fixture up-to-date" fails on drift.
- **Console logic test (Layer B):** `phase4/console/test_console_logic.py` — Playwright-Python (headless Chromium) drives the real `telemetry.js` with a stubbed `EventSource` + `page.clock` (no real SSE); deterministically asserts connState live/stale (the 2800 ms watchdog)/crcfail, seq-gap `droppedPackets`, new-boot reset, and cold-start `queriesPerSec()` via `getState()`; zero console errors. 14/14. CI: "Phase 4: Console logic unit test".
- **Console E2E test (Layer C):** `phase4/console/test_console_e2e.py` — Playwright-Python boots the real `telemetry_receiver.py --replay golden.pcap`, asserts the page mounts (Babel+React, no errors), connState `live`, model/`num_nodes`/INFER-`last_text` render, `simulated==false` (false-pass guard — no fallback) and **every live `flags_list` flag renders a Capabilities row** (feature-not-shown = hard fail); plus a no-`/events` fallback phase (SIMULATED badge appears) **+ the System screen renders the real RAM/inference-state/storage fields** (RAM-available + workload-duty + ACTIVE/IDLE pill). 16/16. CI: "Phase 4: Console E2E". Local: needs `playwright==1.60.0` + `playwright install --only-shell chromium` (CI installs with `--with-deps`).
- **Tensor Ops:** `phase3/src/ai/tensor_ops.c/h`
- **Dequantization:** `phase3/src/ai/dequant.c/h`
- **BPE Tokenizer:** `phase3/src/ai/tokenizer.c/h`
- **Llama Model Header:** `phase3/src/ai/llama_model.h`
- **Llama Weight Loading:** `phase3/src/ai/llama_load.c`
- **Llama Forward Pass:** `phase3/src/ai/llama_forward.c`
- **Sampling:** `phase3/src/ai/sampling.c/h`
- **Inference API:** `phase3/src/ai/inference.c/h`
- **Quantized Inference:** `phase3/src/ai/llama_quant.c/h`
- **Load-time quant-type gate (H2):** `qmodel_load` now HARD-REJECTS unsupported quant tensor types at load (whitelist F32/F16/BF16/Q4_0/Q8_0/Q4_K/Q5_K/Q6_K via `dequant_type_supported` = `dequant_type_block_size != 0`); a post-resolve sweep over every non-NULL `qtensor_t` (the 6 globals + all 25 per-layer fields, incl. inline-assigned w_gate/wq) `goto fail`s on the first unsupported type — no more silent all-zero activations → coherent-looking garbage. Hot path untouched. Regression in `test_llama_quant.c` (predicate truth table + F32 positive control + Q2_K token_embd/ffn_gate negatives).
- **GGUF Vocab Extraction:** `phase3/src/ai/gguf_vocab.c/h`
- **SHIELD Safety Module:** `phase3/src/ai/shield.c/h`
- **SSM / Gated DeltaNet:** `phase3/src/ai/ssm.c/h` — Qwen3.5 hybrid recurrent layers
- **Inference Benchmark (stale F32):** `phase3/src/ai/bench_inference.c`
- **Engine Bench (quantized, all models):** `phase3/src/ai/bench_engine.c`
- **AVX2 Correctness Test:** `phase3/src/ai/test_avx2_correctness.c`
- **Parallel-for ABI Test:** `phase3/src/ai/test_parallel_for.c` — jarvis_parallel_for work-stealing correctness (reduction==serial, ctx-copy, 100x multi-dispatch, swept threads)
- **Threadpool join (note):** `phase3/src/ai/threadpool.c` uses a per-dispatch generation counter — cross-dispatch over-decrement race fixed 2026-06-17 (caught by `test_parallel_for.c`); the seL4 M3 backend ports this corrected work-stealing core
- **seL4 Threadpool (M3):** `phase3/src/ai/threadpool_sel4.c` — PB worker pool (generation publish + active-counter join + per-worker wake notifications). PA creates the N−1 workers in PB's VSpace/CSpace (PB has no allocator) and resolves `jarvis_sel4_worker_entry` from PB's ET_EXEC `.symtab` (PA never links it); PB-only `JARVIS_SEL4_SMP`. Production `NUM_NODES=6` → Gemma 4 E2B 5.46 tok/s (3.57×). Worker MUST `seL4_SetIPCBuffer` (started outside sel4runtime). **Host-CI-covered since H1** (was bare-metal-verified only / zero CI) — see next.
- **seL4 Threadpool host test (M3, H1):** `phase3/src/ai/test_threadpool_sel4.c` + `phase3/src/ai/test_sel4_stubs/sel4/sel4.h` (host-only fake `<sel4/sel4.h>` backing the Notification primitives with POSIX semaphores — a cptr carries a `sem_t*`; `seL4_Wait`/`seL4_Signal` → `sem_wait`/`sem_post`). Runs the DEPLOYED work-stealing pool on the host under **ThreadSanitizer**: T1 exactly-once visit / T2 serial-equiv reduction / T3 cross-dispatch state (the race the pthread backend once bit) / T4 serial fallback (<256) / T5 ctx-copy, swept over 1/2/4/6/8 threads (25/25, zero TSan reports). `threadpool_sel4.c` compiled UNCHANGED (verified byte-identical to HEAD; teeth-proven via a sabotaged copy). CI steps: "Phase 3: seL4 Threadpool work-stealing (host shim, TSan)" + "(host shim, O2)". Needs `-D_POSIX_C_SOURCE=200809L -DJARVIS_SEL4_SMP -I phase3/src/ai/test_sel4_stubs`.
- **Quantized Inference AVX2 CI build (H1):** `test_llama_quant.c` gained Tests 11–12 (Q8_0 + Q4_K `qdot_row` vs a `dequant_row`+double-dot reference, err ~3e-8 / ~3e-7 vs tol 1e-3 / 1e-2); a new CI step "Phase 3: Quantized Inference (AVX2 build)" recompiles the quantized-inference suite with `-mavx2 -mfma` so the deployed AVX2 forward + qdot kernels are exercised in CI (the default step is scalar).
- **AVX2 Probe (M0, seL4 on-box):** `phase3/src/sel4/avx2_probe.h` — `target("avx2,fma")`-isolated YMM save/restore probe (`JARVIS_AVX2_PROBE`, default off)
- **SMP Probe (M2, seL4 on-box):** `phase3/src/sel4/smp_probe.h` — `smp_apic_id()` (CPUID leaf 1) for the numNodes gate + PA/PB core placement (`JARVIS_SMP_PROBE`, default off)
- **RotorQuant Reference:** `phase3/docs/ROTORQUANT_REFERENCE.md` — KV cache compression alternative (on `experiment/turboquant-benchmark` branch)
- **Perf Optimization Spec:** `docs/superpowers/specs/2026-04-13-perf-fused-dequant-dot-design.md` — Week 35 plan, fused dequant-dot + SIMD attention
- **GRUB Config:** `phase3/firmware/grub/grub.cfg`
- **GPU Benchmarks:** `phase3/docs/GPU_BENCHMARK_RTX2070.md`
- **Inference Benchmark Results:** `phase3/docs/INFERENCE_BENCHMARK.md`
- **Native Linux Setup:** `phase3/docs/SEL4_NATIVE_LINUX_SETUP.md`
- **Native Test Results:** `phase3/docs/NATIVE_TEST_RESULTS.md`
- **CI Workflow:** `.github/workflows/ci.yml`
- **VGA Text Driver:** `phase3/src/drivers/vga_text.c/h`
- **Framebuffer (goal #2):** `phase3/src/drivers/framebuffer.c/h` — 32bpp linear pitch-addressed blitter (fb_init/fb_putpixel/fb_fill_rect/fb_clear/fb_flush/**fb_progress_bar**) + 8×16 bitmap font (fb_draw_char/fb_draw_text, edge-clipped, BGRX no-swap); rootserver maps the GOP FB (bootinfo id=4, **1024×768×32** on the box) Uncacheable + draws a **live status panel** (Model load %/Queries/Last) mirrored to the log as `[PANEL]` lines (per-query Queries uses a draw-only twin so it can't flood the no-wrap log) + a scrolling **event-log ring** (fb_log_reset/append/count/row/visible — natural oldest→newest order, full-window UC repaint (no memmove), `>`=newest/bottom, fed from `putc_serial` completed lines) + **per-state chrome** (fb_badge BOOTING/READY/ERROR header badge + live ROUTE ratio + COUNTERS strip, drawn at the [STATS] cadence via the quiet draw-only twins, no faked widgets); palette/geometry from `jarvis_ui_tokens.h` design tokens (single FBP_* source) + a **live model-load progress bar** (`fb_progress_bar`, drawn from the throttled `model_load_progress` path in lockstep with the `[loading N%]` text — FBP_ACCENT loading → FBP_OK at 100%; `[PANEL]`-mirrored via that text, no extra log line; JUI_PBAR_* tokens, row 10/y160 clear of ROUTE/COUNTERS). **Host-tested:** `test_framebuffer.c` (46/46, CI step). The FB-map/panel/chrome/load-bar runtime path is seL4-only.
- **Debug Config:** `phase3/src/sel4/jarvis_debug.h`
  - `JARVIS_DBG_IPC` — Per-query IPC tracing in Process A (`[DBG] q=N slot=N`)
  - `JARVIS_DBG_PB` — Process B inference tracing (`[PB] handle_query, generating, decoded`)
  - `JARVIS_DBG_RING` — Ring health checks before send (`[PB] ring @... magic=... w= r=`)
  - `JARVIS_DBG_STATS` — Periodic stats every 100 queries (default: ON)
  - `JARVIS_DBG_INFER_SUMMARY` — Per-inference summary line (default: ON)
  - `JARVIS_DBG_BOOT_LOG` — NVMe log at every boot stage + auto-capture serial output (default: OFF)
  - Committed defaults ARE the stability config: STATS + INFER_SUMMARY on, IPC/PB/RING/BOOT_LOG off (BOOT_LOG off avoids NVMe write wear over 30 days)
- **NVMe Driver:** `phase3/src/drivers/nvme.c/h`
- **NVMe Log:** `phase3/src/drivers/nvme_log.c/h` — raw sector logging for bare-metal telemetry
- **NVMe Log Parser:** `phase3/scripts/parse_nvme_log.py` — extract + format log from raw device
- **NVMe Log Reset:** `phase3/scripts/clear_nvme_log.sh` — zero the telemetry-log region (derives LBA/count from `nvme_log.h`; archive first, `--yes` to write)
- **FAT32 Parser:** `phase3/src/drivers/fat32.c/h` — read-only FAT32; **FAT-sector cache** (`fat32_fs_t.fat_cache` — `fat32_next_cluster` reads each FAT sector once vs up to ~128× per chain → ~160 MB less NVMe I/O on the Gemma load) + a `fat32_read_file` **progress hook** (`fat32_fs_t.progress` → exact data-only load %). `fat32_init` already rejects sizes outside {512,1024,2048,4096} (SEC-028); 1024/2048/4096 are accepted but unsupported by the 512-byte `fat_cache` and fail safely (SEC-029 returns -1 at the first FAT read). Optional tightening: reject `!=512` outright. Never fires on the box's 512B NVMe. **fat32_read_file final partial-sector overflow fixed (H3)** — the trailing partial sector is read into a stack bounce buffer and only the leftover `tail` bytes copied, so it never writes up to bytes_per_sector-1 bytes past `buf+file_size` (delivers exactly file_size; deployed Gemma load byte-identical, only the unwritten junk-tail changes); regression in `test_fat32.c` (0xAA canary, single- + multi-cluster sub-sector files; fail pre-fix).
- **Fuzz Harness:** `phase3/src/drivers/fuzz_harness.c`
- **Security Audit (Apr 2026):** `phase3/docs/SECURITY_AUDIT_2026-04-06.md`
- **Model Bench-Off:** `phase3/docs/MODEL_BENCH_OFF_2026-04-07.md` — tiers, compatibility, deferred contenders (Gemma 4 §7, Qwen3.5 §7b, Qwen3 §9)
- **Bench Scripts:**
  - `phase3/scripts/bench_models.sh` — unified bench (speed/perplexity/quality) for JARVIS PC
  - `phase3/scripts/bench_engine_models.sh` — JARVIS engine bench (all 11 models, quantized path)
  - `phase3/scripts/bench_speed_windows.ps1` — Windows speed bench (CPU + GPU modes)
  - `phase3/scripts/bench_perplexity_windows.ps1` — Windows perplexity bench (GPU, full WikiText-2)
- **Bench Results:** `models/bench_results/bench_results.txt` (JARVIS PC speed), `models/bench_results/bench_results_mainpc.txt` (5600 CPU), `models/bench_results/bench_results_mainpc_gpu.txt` (5600 + RTX 2070)
- **Engine Bench Results:** `models/bench_results/jarvis_engine_bench.txt` (JARVIS engine, 11/11 models, quantized path, 6 model families)
- **Perplexity Results:** `models/bench_results/perplexity_results.txt`
- **Quality Results:** `models/quality_results/ALL_RESPONSES.txt` (11 models × 10 prompts, Claude-judged)
- **Judge Consensus:** `models/quality_results/JUDGE_CONSENSUS.txt` (5-agent blind consensus)
- **Final Scores:** `models/quality_results/FINAL_SCORES.txt` (7-judge combined: quality + speed + PPL + tier decisions)
- **x86 Build Script:** `phase3/scripts/build_jarvis_x86.sh`
- **QEMU NVMe Test:** `phase3/scripts/qemu_test.sh` (pass model path as arg)
- **QEMU NVMe Disk:** `~/nvme_test.img` FAT32 with the model file named to match `JARVIS_MODEL_FILE` (currently `GEMMA2B GUF`); `-drive file=...,if=none,id=nvme0,format=raw -device nvme,serial=QEMU_NVME,drive=nvme0`
- **Inference Server (PB):** `phase3/src/sel4/inference_server.c`
- **NVMe Partition Setup:** `phase3/scripts/setup_nvme_partition.sh` — creates `JARVIS_DATA` at EXACTLY sector 32768 (hard-asserted via `parted unit s print`; was free-space start → silent model-not-found), default model = Gemma 4 E2B Q4_K_M, copies as `GEMMA2B.GUF` + validates the FAT 8.3 short name == `JARVIS_MODEL_FILE` (`GEMMA2B GUF`), detects+verifies an existing `JARVIS_DATA` instead of recreating (box-safe)
- **x86 Installer (one-script):** `phase3/scripts/install_jarvis_x86.sh` — orchestrates build → boot-USB → NVMe model provision → verify → BIOS/validation checklist; `--build/--usb/--model/--nvme/--repo/--skip-*/--dry-run`; `--target usb` (repartition+format a USB) **and `--target esp` — reversible DUAL-BOOT on the existing internal ESP** (`--esp /dev/nvme0n1p4`: `guard_esp_device` = vfat-ESP-partition check + `ADD-JARVIS` double-confirm, deliberately accepts an NVMe PARTITION but refuses a whole disk; `write_boot_esp` mounts the EXISTING ESP, copies to `EFI/jarvis/` ONLY, sed-rewrites grub.cfg → `/EFI/jarvis/boot/`, `grub-mkstandalone`s `EFI/jarvis/grubx64.efi` — **never** `--removable` / whole-disk / `EFI/BOOT` / `EFI/ubuntu`; additive `efibootmgr` entry with Ubuntu kept `BootOrder[0]`; stale-ESP-root-model cleanup runs FIRST (the box ESP is ~100% full with a 1.79 GB stale copy) behind an explicit `DELETE-STALE-ESP-MODEL` re-type, THEN staging (so the cp's can't ENOSPC-fail; the cp's are guarded to fail loud + unmount on a still-full ESP) — ESP copy only, JARVIS_DATA p2 untouched; rollback printed; **reversible dual-boot — VERIFIED on-box 2026-06-25 (boot_id=5, coherent Gemma 4 E2B, NN=6, err=0; Ubuntu kept `BootOrder[0]`); the boot USB is now RECOVERY-only, not required to boot. F3 fixed (no `set -e` abort when ubuntu is the only prior UEFI entry; regression test T9)**); `disk` = full single-OS on-SSD install that WIPES the whole disk (JARVIS only, no Ubuntu): `guard_disk_device` (whole-disk only; refuses the booted/root + mounted disk + partition paths; WIPE-AND-INSTALL-JARVIS + DESTROY-ALL-DATA triple-confirm) + `write_boot_disk` (wipefs/sgdisk zap → GPT → JARVIS_DATA @ 32768 ~10 GiB + ESP `--removable` BOOTX64.EFI; telemetry-log tail unpartitioned) — **CODE + DRY-RUN ONLY, NEVER run on the dev box** (ADR `docs/decisions/2026-06-25-target-disk-full-ssd-install.md`); forward-structured (`stage_boot_payload(esp_mount, mode)` shared by USB); derives the rootserver image name dynamically (no hardcoded `sel4test-driver-image`); device-safety refuses `/dev/nvme*` + root disk; sourceable pure helpers. **Phase 4 goal #4 (core installer) DONE.** Host test `phase3/scripts/test_install_jarvis_x86.sh` (helpers + full dry-run incl. the esp + disk dry-runs, no-`--esp`/`--disk` errors, partition/root-disk refusals + touches-nothing sentinels, **50 PASS**); both shellcheck-clean (full strictness) + dry-run CI-gated. `reflash_usb.sh` now refuses NVMe/root disk (matching `create_boot_usb.sh`). `BARE_METAL_BOOT_GUIDE.md` corrected (real `sel4test-driver-image` name, 32GB/R9 280X hardware, +NVMe-provisioning + network-telemetry validation sections).
- **Check CI:** `gh run list --limit 1` then `gh run view <id> --log-failed` if failed

### Rules
- Always update CLAUDE.md and week status files after completing work
- COMMIT WEEKLY with week number in message
- Update `phase2/docs/PHASE_2_PROGRESS_TRACKER.md` when completing work
- Test everything: aim for 100% pass rate
- When creating new test files (test_*.c or test_*.py), ALWAYS add a corresponding step to `.github/workflows/ci.yml` with the exact compile+run command. Verify the new CI step compiles locally before committing.
- After every `git push`, check the GitHub Actions workflow status with `gh run list --limit 1` and `gh run view` to verify CI passed. If any step failed, investigate with `gh run view --log-failed` and fix before continuing.
- Phase 2 is C-only on Pi 4 (no Python runtime on seL4)
- **UI–feature parity:** when shipping a user-visible feature, update the Remote Telemetry Console (`phase4/console/`) **in the same change** — add its real live signal to the relevant screen or its auto-populated Capabilities/Features section (never hardcoded). The UI shows only real/live state: a real feature missing from the UI is a gap; UI showing anything without a live source is fiction. Enforced by `phase4/console/test_console_honesty.py`.
- **Frontend (`phase4/console/`) stays correct:** runtime libs (React/Babel/Lucide) are **vendored** under `phase4/console/vendor/` (pinned — no live CDN) so CI is hermetic. Layered tests, all Python (Playwright's **Python** binding — `pip`, no Node): the honesty grep gate + a **key-contract** test (`jarvis_telemetry.h`→`packet_to_record`→`telemetry.js` shape, against one golden fixture) + a **logic** test (`telemetry.js` connState/CRC/stale-watchdog via Playwright `page.clock`) + a real-SSE **e2e smoke** (page boots / no console errors / `simulated==false` / **every live `flags_list` flag renders a Capabilities row**). **All four layers now exist and are CI-gated** (honesty 40 + key-contract 81 + logic 14 + e2e 16; Playwright installed in CI via `--with-deps --only-shell chromium`, pinned `playwright==1.60.0`). Keep them green; one visual confirm per visual change (the e2e covers the rest).

### Codebase Metrics

Tracked `.c/.h/.py` files and their `wc -l` LOC via `git ls-files` (measured 2026-06-14; Phase 3 + totals recounted 2026-06-22 for N-c-1/N-c-2):

- **Phase 0:** 4,919 LOC, 11 code files (COMPLETE)
- **Phase 1:** 40,837 LOC, 102 code files, 41 test files (COMPLETE)
- **Phase 2:** 27,236 LOC, 65 code files, 8 test files (COMPLETE)
- **Phase 3:** 39,827 LOC, 144 code files, 48 test files (IN PROGRESS — **11/11 models, 6 families, qdot + seL4 threadpool M3; +framebuffer font/panel/event-log; +goal #2b NIC I211 first-light + net_udp Eth/IPv4/UDP emit + jarvis_telemetry telemetry_packet over I211 + telemetry_receiver.py (Python consumer/SSE) + telemetry_fixture.py + gen_golden_pcap.py + test_net_udp.c + test_jarvis_telemetry.c + test_telemetry_receiver.py; +H1 host-CI for the deployed inference path: test_threadpool_sel4.c (work-stealing pool under TSan, via test_sel4_stubs/ fake seL4 headers) + an AVX2 build of the quantized-inference test; +Phase 5 G1/M0 `episodic_store.c/h` (raw-LBA circular 512 B-record store, nvme_log pattern, struct+callback-driven) + `test_episodic_store.c`**)
- **Total (phases):** 112,819 LOC, 309 code files, 94 test files (725 tracked files repo-wide)
- **Phase 4 console (`phase4/console/`):** 22-file browser UI — 7 `.jsx` + `telemetry.js` + `index.html` + the vendored DS runtime (`_ds_bundle.js`, `styles.css`, 4 `tokens/*.css`) + 4 pinned runtime libs (`vendor/` — React/ReactDOM/Babel/Lucide) + 2 fixtures (`fixtures/golden_telemetry.json` + `golden.pcap`) + `test_console_honesty.py` (honesty gate). The web/JS/JSON/pcap files are not counted in the `.c/.h/.py` LOC above; the frontend-test foundation's Python (`telemetry_fixture.py`, `gen_golden_pcap.py`) lives under `phase3/scripts/`.
- **Phase 4 installer (goal #4):** `phase3/scripts/{install_jarvis_x86.sh, test_install_jarvis_x86.sh}` — host build/flash tooling (`.sh`, not counted in the `.c/.h/.py` LOC above, like the other `phase3/scripts/*.sh`); the test is `.sh` so the 45 `.c/.py` test-file count is unchanged. Both are CI-gated (shellcheck + dry-run). **`--target esp` (reversible on-SSD dual-boot) VERIFIED on-box 2026-06-25 (boot_id=5; Ubuntu kept `BootOrder[0]`) = the deployed path; the boot USB is now recovery/re-flash; `--target usb` still supported. goal #4 DONE incl. on-SSD dual-boot; the target matrix is now COMPLETE — `--target disk` (full single-OS wipe) is implemented CODE + DRY-RUN ONLY (never run on the dev box; ADR `docs/decisions/2026-06-25-target-disk-full-ssd-install.md`).**
- Per-assertion PASS counts run higher than file counts (self-reported ~383+ Phase 3 / ~635+ total).
- **Security:** March 2026 adversarial audit — 26 findings, all resolved (phase2+phase3 C). April 2026 Phase 3c audit — 25 findings (all 5 HIGH + 5 MEDIUM fixed, 7 LOW/INFO accepted). Phase 2's own audit: 6 C findings (4 fixed) + Snyk. SHIELD: host harness only (test_shield.c exercises shield.c — keyword + model-assisted scoring); the LIVE PA↔PB path is passive — Process B returns ALLOW, shield.c not linked (SEC-039), so the deployed system is NOT a live blocker.
- **Inference:** 11 GGUF models across 6 model families on JARVIS engine. Gemma 4 E2B (8.40/10 quality) is the single deployed model. Qwen3.5 DeltaNet SSM hybrid working. 3.22 tok/s (1T), 19.79 tok/s (16T) — NATIVE dev engine (Llama 1B), NOT the deployed seL4 build; deployed = Gemma 4 E2B **5.46 tok/s @ NUM_NODES=6**.

### Hardware Setup

**Two-PC setup (April 2026):**
- **Main PC:** Ryzen 5 5600, RTX 2070, 32GB, ASRock B550M-ITX/ac — daily driver
- **JARVIS PC:** Ryzen 7 2700X, R9 280X (display only), 32GB DDR4 (swapped from 16GB), ASUS X470-F, 2TB NVMe, Ubuntu — dedicated, wipeable for bare-metal seL4
- **Pi 4 8GB:** Phase 2 complete, ARM fallback
- CPU-only AI inference on JARVIS PC (no GPU compute). GPU deferred to Phase 4.
- X470-F Gaming NIC: Intel I211-AT — needs igb-family driver (nic_rtl8168.c targets B550M's RTL8111H)
- Phase 2 hardware pivot doc: `phase2/docs/PHASE_2_HARDWARE_PIVOT.md`

### Technology Stack

- **Microkernel:** seL4 (formally verified*) on ARM64 + x86-64 — *\*the x86-64 build runs `KernelFastpath=ON`, outside seL4's verified X64 config ("no fast path"); functional-but-unverified by design (perf > proof) — see `phase4/docs/PHASE_4_GOAL1_INFERENCE_PERF.md` §3*
- **AI Models:** 11 GGUF models across 6 model families — Llama, Gemma 4 (PLE/SWA/KV-sharing), Phi-3 (fused QKV), Mistral (Q8_0), Qwen3 (QK norms), Qwen3.5 (DeltaNet SSM hybrid)
- **Inference:** Quantized zero-copy (Q4_0/Q4_K/Q5_K/Q6_K/Q8_0/BF16 dequant on-the-fly, ~50MB heap). Gated DeltaNet SSM for Qwen3.5 hybrid layers.
- **Build:** TII seL4 build system + CMake/Ninja
- **Cross-compiler:** aarch64-linux-gnu-gcc 13.3.0 (ARM64), gcc 13.3.0 (x86-64)
- **SIMD:** AVX2+FMA for tensor_matmul/rms_norm/qmatmul_vec/qdot/attention (`-mavx2 -mfma`); `#ifdef __AVX2__` fallback to scalar
- **Bootloader:** U-Boot 2026.01 (Pi 4), GRUB2/multiboot (x86 QEMU)
- **Hardware:** Raspberry Pi 4 (BCM2711, 8GB), JARVIS PC (Ryzen 7 2700X, R9 280X display, 32GB, 2TB NVMe, ASUS X470-F, Ubuntu), Main PC (Ryzen 5 5600, RTX 2070, 32GB, ASRock B550M-ITX/ac)
- **QEMU:** KVM-accelerated x86-64, 4GB RAM, CNode 19 (524K slots)
- **Design tooling — Claude Design** (Anthropic Labs, claude.ai/design): a repo-aware AI design tool — it reads the codebase + design files to build a design system, then designs polished pages/prototypes using our real components/tokens, and hands off to Claude Code to land the code. The current Remote Telemetry Console (`phase4/console/`) UI was produced this way. New console screens (e.g. the System screen) are prompt-designed in Claude Design against the repo's design system, then implemented + honesty-gated by Claude Code. Claude Design designs the look; it cannot invent live data — every rendered field must have a real telemetry source.
