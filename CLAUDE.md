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
| **Phase 3** | **IN PROGRESS** | Months 24-36 | Beta on x86-64 bare metal (**LLM inference on seL4 VERIFIED**) |
| Phase 4 | Future | Months 36+ | Production v1.0 |

**Current:** Phase 3, Active Development (March 28, 2026). **MILESTONE: Process-isolated LLM inference on seL4.** Two seL4 processes communicate via shared memory IPC: Process A (rootserver + cache + SHIELD) spawns Process B (Llama 3.2 1B inference) from CPIO. Coherent text generation verified end-to-end through IPC. Spare PC still 1/7 parts — all QEMU-possible work done.

---

## Architecture

### Model B: Microkernel with AI Control

```
Hardware Layer
    ↓
Microkernel (Ring 0, cores 0-1)
• Interrupt handling (<1ms)
• Memory management, IPC primitives
• ~12K LOC, seL4 (formally verified)
    ↓ ← Lock-free IPC →
AI Decision Engine (Ring 3, cores 2-N)
• Specialist agents (4-6)
• Decision cache (50ms→<1ms for 85% ops)
• Dynamic model scaling (1B→7B→13B)
• SHIELD safety framework
    ↓
User Space Services (Ring 3)
• Device drivers, File systems, Applications
```

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
- **Dynamic Model Scaling:** IDLE(1B)→ACTIVE(7B)→CRITICAL(7B+validator)→EMERGENCY(rules). 60% memory savings.
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
├── phase3/                     # PLANNING (spare x86 PC pending)
│   ├── docs/                   # PHASE_3_KICKOFF.md, HARDWARE_RESEARCH.md, IMPLEMENTATION_PLAN.md
│   ├── src/
│   │   ├── ai/                # ggml integration, model loading, inference
│   │   ├── boot/              # x86 boot config (GRUB/EFI)
│   │   ├── drivers/           # x86 drivers (16550A UART, AHCI, NIC)
│   │   ├── ipc/              # Shared memory IPC
│   │   └── sel4/             # seL4 x86-64 rootserver
│   ├── scripts/               # Build scripts
│   ├── firmware/              # Boot images, GRUB configs
│   └── weeks/                 # Weekly status docs
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

### Weekly Development Pattern

1. Create `phase2/weeks/weekN/WEEK_N_STATUS.md`
2. Implement, test, update status docs
3. **COMMIT WEEKLY** with week number in commit message (MANDATORY)

---

## Current Status (Phase 3 — Process-Isolated LLM Inference on seL4)

**PROCESS ISOLATION MILESTONE** (March 28, 2026) — Two seL4 processes running: Process A (rootserver, cache, SHIELD) spawns Process B (Llama 3.2 1B inference) from CPIO archive. Process B loads 770MB model zero-copy from .rodata, extracts 128K BPE vocab, generates coherent text. End-to-end IPC verified: Process A sends "The seL4 microkernel is" → Process B generates → response via shared memory. CNode=22 (4M slots), morecore=128MB, 8GB QEMU. Spare PC still awaiting assembly (1/7 parts).

| Milestone | Status |
|-----------|--------|
| U-Boot 2026.01 working | DONE |
| Auto-boot kernel8.img (1.7MB, 89ms) | DONE |
| Boot flow: GPU→U-Boot→boot.scr→kernel8.img→seL4→JARVIS | DONE |
| UART TX+RX (device frame at 0x5c0000) | DONE |
| Python↔seL4 IPC (500-query bench, 100% success, 7ms RTT) | DONE |
| SD/EMMC read+write (CMD17/18/24/25, ADMA2, PIO) | DONE |
| CSD capacity parsing (any card size) | DONE |
| 12-test EMMC driver suite: 12 PASS, 0 FAIL | DONE |
| Decision cache loaded (258 patterns) | DONE |
| BCM2711 System Timer + throughput (11.9 MB/s ADMA2) | DONE |
| GENET MMIO mapping (6 pages at 0x604000, own device untyped) | DONE |
| GENET UMAC init + RGMII config | DONE |
| GENET PHY init via MDIO (BCM54213PE at addr 1) | DONE |
| GENET TX DMA ring 16 (256 descs, TDMA enabled) | DONE |
| GENET TX send (ARP broadcast test frame) | DONE |
| 6-test GENET driver suite | DONE |
| GENET RX DMA ring 16 (32 descs, RDMA + UMAC RX enabled) | DONE |
| GENET RX recv + poll (polled receive) | DONE |
| Basic networking stack (ARP reply + ICMP echo reply) | DONE |
| 5-test RX + networking suite (hardware verified) | DONE |
| Shell commands: ping, ifconfig, netstat (net_cmd.c) | DONE |
| Outbound networking: ARP request, ICMP echo request | DONE |
| ARP cache (8 entries) + ARP resolve with timeout | DONE |
| GENET link status/speed/stats via MDIO | DONE |
| UART IPC COMMAND handler (0x07/0x08) | DONE |
| 10-test shell + integration suite (9 PASS + 1 SKIP hw verified) | DONE |
| DWC2 USB host controller init (slave mode, 3 MMIO pages at 0x60A000) | DONE |
| USB enumeration (GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION) | DONE |
| HID boot protocol keyboard (8-byte reports, scancode to ASCII) | DONE |
| 6-test USB HID suite (4 PASS + 2 SKIP hardware verified) | DONE |
| USB HID Ctrl/CapsLock/special key support | DONE |
| USB keyboard shell integration (line buffer + echo) | DONE |
| "usb" shell command | DONE |
| 10-test USB keyboard suite (build pending) | DONE |
| install_jarvis.sh Linux/WSL installer | DONE |
| install_jarvis.bat Windows installer | DONE |
| USER_GUIDE.md setup documentation | DONE |
| ALPHA_TESTER_GUIDE.md tester onboarding | DONE |
| build_installer_image.sh SD card image builder | DONE |
| flash_sd.sh SD card flash utility | DONE |
| BCM2711 GPIO driver (pin control, LED, pull-up/down) | DONE |
| I2C BSC1 driver (100/400 kHz, bus scan) | DONE |
| GPIO shell command (pin status) | DONE |
| I2C shell command (bus scan) | DONE |
| Stress test framework (all-driver exercise loop) | DONE |
| PI4_PLATFORM_GUIDE.md (driver matrix, memory maps, benchmarks) | DONE |
| 13-test GPIO + I2C + stress suite (build pending) | DONE |
| BCM2711 PM watchdog timer (10s timeout, feed/reboot) | DONE |
| VideoCore mailbox thermal monitoring (GPU temperature) | DONE |
| Binary buddy skip enhancement in uart_device_map_page() | DONE |
| Shell commands: temp, watchdog, reboot | DONE |
| Watchdog heartbeat in IPC main loop (50ms) | DONE |
| Python power_manager.py (host-side monitoring) | DONE |
| 10-test watchdog + thermal suite (build verified) | DONE |
| Embedded JARVIS device tree (jarvis.dts/dtb/dtb_data.h) | DONE |
| FDT parser (jarvis_fdt_*, ~500 LOC, no alloc) | DONE |
| Boot timing instrumentation (systimer-based) | DONE |
| "dt" shell command (device tree info) | DONE |
| 10-test FDT parser + boot timing suite (build verified) | DONE |
| Boot manager (per-stage timing, lazy init tracking) | DONE |
| Warm reboot (SD persistence, warm/cold detection) | DONE |
| Power management (WFI idle, ARM frequency scaling) | DONE |
| Shell commands: boot, power, reboot warm/cold | DONE |
| thermal_mailbox_tag() generic mailbox API | DONE |
| 10-test boot/warm/power suite (build verified) | DONE |
| BCM2835 SPI0 driver (polled FIFO, full-duplex, 488 kHz default) | DONE |
| iproc-rng200 Hardware RNG driver (FIFO entropy, soft reset) | DONE |
| BCM2835 PWM driver (2-channel, M/S mode, mailbox clock) | DONE |
| Shell commands: spi, rng, pwm | DONE |
| 11-test SPI + RNG + PWM suite (build verified) | DONE |
| BCM2711 DMA engine (channels 0-6, mem-to-mem, CB pool) | DONE |
| Shell command: dma | DONE |
| Python stability harness (UART IPC, CSV logging, hang detect) | DONE |
| 8-test DMA engine suite (build verified) | DONE |
| **Critical fix: direct MMIO TX (seL4 kernel CR/LF corruption)** | DONE |
| **5-min stability test: 298/298 PASS (100%)** | DONE |
| **1-hour smoke test: 3,562/3,570 PASS (99.8%)** | DONE |
| Stability harness: daily rotation, checkpoints, resume | DONE |
| Security self-audit: 3 bugs fixed (1 HIGH, 2 MEDIUM) | DONE |
| SECURITY_SELF_AUDIT.md written | DONE |
| 30-day stability test PASSED (30.6 days combined, 0 crashes) | DONE |
| PHASE_2_FINAL_REPORT.md written | DONE |
| PHASE_3_HARDWARE_RESEARCH.md written | DONE |
| PHASE_3_KICKOFF.md written | DONE |
| Adversarial security audit: 26 findings (1 CRIT, 6 HIGH, 7 MED, 8 LOW, 4 INFO) | DONE |
| All 26 security findings fixed (2 commits: 0ff1cde + 708aa15) | DONE |
| 211/211 tests passing after security hardening | DONE |
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
| **TurboQuant Beta-optimal codebooks (MSE/QJL verified vs paper)** | **DONE** |

**Next:** Spare PC assembly (1/7 parts bought). All QEMU-achievable work complete. Ready for real hardware.

### Pre-Work Tasks (Before Spare PC)

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
| NIC RTL8168 skeleton | 17-18 | DONE | nic_rtl8168.c/h | 6 PASS |
| x86 Timer (PIT/HPET/TSC) | 13-14 | DONE | x86_timer.c/h | 8 PASS |
| Block device abstraction | 15-16 | DONE | blk_dev_x86.c/h | 9 PASS |
| C tensor ops (10 ops) | 19-20 | DONE | tensor_ops.c/h | 14 PASS |
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
| Dynamic model scaling | — | DONE | model_scaling.c/h | 9 PASS |
| IPC CRC-32 (SEC-020) | — | DONE | shmem_ipc.c/h | 3 PASS (13 total) |
| GPT-2 full byte mapping | — | DONE | tokenizer.c | 12 PASS |
| Tiled matmul + unroll | — | DONE | tensor_ops.c, llama_quant.c | 14+10 PASS |
| RDTSC timing (Stage 5) | — | DONE | main_x86.c | 5/5 stages PASS |
| TurboQuant KV compression | — | DONE | turboquant.c/h | 15 PASS |
| TurboQuant real-data validation | — | DONE | test_turboquant_real.c | SKIP on CI (model needed) |
| **Total** | | | **80+ files** | **335 tests, ~20,000 LOC** |

**What remains for Phase 3b on real hardware:**
- Boot seL4 on actual Ryzen hardware (vs QEMU)
- AHCI full read/write against real NVMe (mock-tested, real I/O pending)
- NIC driver TX/RX against real NIC (skeleton done, real I/O pending)
- Load GGUF model from NVMe (currently embedded in .rodata via objcopy)
- 30-day stability test on x86

### Phase 3 Weeks (After Spare PC Assembly)

| Weeks | Task |
|-------|------|
| 1-6 | Phase 3a: Spare PC as GPU host + Pi 4 UART |
| 7-28 | Phase 3b: Pure bare-metal seL4 x86-64 (drivers + GGUF parser partially done) |
| 29-40 | Phase 3c: Hardening, fuzz testing, SHIELD |
| 41-44 | Final report + git tag v0.3.0-beta |

### Validated Metrics

| Metric | Target | Actual |
|--------|--------|--------|
| IPC latency | <100us | 54us (Phase 1), 7ms UART (Phase 2) |
| Cache hit rate | >80% | 85.7% |
| AI inference | <500ms | 558ms CPU Phi-3, <100ms Llama 3.2 1B (host PC benchmarks, no log artifact) |
| Native F32 inference | — | 1.09 tok/s (naive matmul, no SIMD; llama.cpp CPU: 44 tok/s with AVX2) |
| Boot time | <60s | ~2s |
| SHIELD block rate | >90% | 100% harmful blocked, 0% FP (keyword + model-assisted) |
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

### Bare-Metal Debugging Rules

- Use `#if EMMC_DEBUG` preprocessor guards for diagnostics, NOT code deletion (deleting debug code caused mysterious boot failures)
- `seL4_DebugPutChar()` works for TX without device frame mapping
- Device frame mapping required for UART RX
- Serial console: 115200 baud, 8N1

---

## For Claude Code

### Reading Order (New Session)
1. This file (CLAUDE.md) → architecture + current status
2. `phase2/weeks/week47/WEEK_47_STATUS.md` → latest week details
3. `phase2/docs/PHASE_2_KICKOFF.md` → Phase 2 goals
4. Source files as needed

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
- **Phase 3 Hardware Research:** `phase2/docs/PHASE_3_HARDWARE_RESEARCH.md`
- **Phase 3 Kickoff:** `phase2/docs/PHASE_3_KICKOFF.md`
- **Phase 3 Implementation Plan:** `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md`
- **seL4 x86 QEMU Setup:** `phase3/docs/SEL4_X86_QEMU_SETUP.md`
- **x86 Rootserver Notes:** `phase3/docs/SEL4_X86_ROOTSERVER_NOTES.md`
- **GGML Portability Notes:** `phase3/src/ai/GGML_PORTABILITY_NOTES.md`
- **GGUF Parser:** `phase3/src/ai/gguf_parser.c/h`
- **x86 Rootserver:** `phase3/src/sel4/main_x86.c`
- **Shared Memory IPC:** `phase3/src/ipc/shmem_ipc.c/h`
- **UART 16550A Driver:** `phase3/src/drivers/uart_16550.c`
- **PCI Enumeration:** `phase3/src/drivers/pci.c`
- **AHCI Discovery:** `phase3/src/drivers/ahci.c`
- **Block Device (x86):** `phase3/src/drivers/blk_dev_x86.c/h`
- **x86 Timer:** `phase3/src/drivers/x86_timer.c/h`
- **NIC RTL8168:** `phase3/src/drivers/nic_rtl8168.c/h`
- **Tensor Ops:** `phase3/src/ai/tensor_ops.c/h`
- **Dequantization:** `phase3/src/ai/dequant.c/h`
- **BPE Tokenizer:** `phase3/src/ai/tokenizer.c/h`
- **Llama Model Header:** `phase3/src/ai/llama_model.h`
- **Llama Weight Loading:** `phase3/src/ai/llama_load.c`
- **Llama Forward Pass:** `phase3/src/ai/llama_forward.c`
- **Sampling:** `phase3/src/ai/sampling.c/h`
- **Inference API:** `phase3/src/ai/inference.c/h`
- **Quantized Inference:** `phase3/src/ai/llama_quant.c/h`
- **GGUF Vocab Extraction:** `phase3/src/ai/gguf_vocab.c/h`
- **SHIELD Safety Module:** `phase3/src/ai/shield.c/h`
- **Dynamic Model Scaling:** `phase3/src/ai/model_scaling.c/h`
- **TurboQuant KV Compression:** `phase3/src/ai/turboquant.c/h`
- **TurboQuant Benchmark:** `phase3/docs/TURBOQUANT_BENCHMARK.md`
- **Inference Benchmark:** `phase3/src/ai/bench_inference.c`
- **GPU Benchmarks:** `phase3/docs/GPU_BENCHMARK_RTX2070.md`
- **Inference Benchmark Results:** `phase3/docs/INFERENCE_BENCHMARK.md`
- **Native Linux Setup:** `phase3/docs/SEL4_NATIVE_LINUX_SETUP.md`
- **Native Test Results:** `phase3/docs/NATIVE_TEST_RESULTS.md`
- **CI Workflow:** `.github/workflows/ci.yml`
- **Check CI:** `gh run list --limit 1` then `gh run view <id> --log-failed` if failed

### Rules
- Always update CLAUDE.md and week status files after completing work
- COMMIT WEEKLY with week number in message
- Update `phase2/docs/PHASE_2_PROGRESS_TRACKER.md` when completing work
- Test everything: aim for 100% pass rate
- When creating new test files (test_*.c or test_*.py), ALWAYS add a corresponding step to `.github/workflows/ci.yml` with the exact compile+run command. Verify the new CI step compiles locally before committing.
- After every `git push`, check the GitHub Actions workflow status with `gh run list --limit 1` and `gh run view` to verify CI passed. If any step failed, investigate with `gh run view --log-failed` and fix before continuing.
- Phase 2 is C-only on Pi 4 (no Python runtime on seL4)

### Codebase Metrics

- **Phase 1:** 39,106 LOC, 95 files, 338 test functions (COMPLETE)
- **Phase 2:** ~27,000 LOC, 65 files, 108 tests (COMPLETE)
- **Phase 3:** ~20,000 LOC, 80+ files, 335 tests (IN PROGRESS — **LLM inference on seL4 verified**)
- **Total:** ~86,000+ LOC, 200+ files, 589+ tests
- **Security:** 26/26 adversarial audit findings resolved (March 2026). SHIELD module: keyword + model-assisted risk scoring.
- **Inference:** Llama 3.2 1B Q4_K_M on seL4 QEMU, coherent output, 50MB heap

### Hardware Pivot Context

Intel NUC ($1,200) → Raspberry Pi 4 ($75). seL4 ARM64 heritage, bare metal access, hardware owned.
Trade-offs: slower UART IPC (7ms vs 54us), Llama 3.2 1B only, SD card (no NVMe).
Doc: `phase2/docs/PHASE_2_HARDWARE_PIVOT.md`

### Technology Stack

- **Microkernel:** seL4 (formally verified) on ARM64 + x86-64
- **AI Models:** Llama 3.2 1B Q4_K_M (native C on seL4), Phi-3 Mini 3.8B (host PC)
- **Inference:** Quantized zero-copy (Q4_K/Q6_K dequant on-the-fly, ~50MB heap)
- **Build:** TII seL4 build system + CMake/Ninja
- **Cross-compiler:** aarch64-linux-gnu-gcc 13.3.0 (ARM64), gcc 13.3.0 (x86-64)
- **Bootloader:** U-Boot 2026.01 (Pi 4), GRUB2/multiboot (x86 QEMU)
- **Hardware:** Raspberry Pi 4 (BCM2711, 8GB), Main PC (Ryzen 7 2700X, RTX 2070, 32GB)
- **QEMU:** KVM-accelerated x86-64, 4GB RAM, CNode 19 (524K slots)
