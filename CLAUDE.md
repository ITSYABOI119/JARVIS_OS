# CLAUDE.md

Guidance for Claude Code when working with this repository.

---

## Project Overview

**JARVIS AI-OS** - An AI-controlled operating system using seL4 microkernel with autonomous decision-making. AI runs at Ring 3 (50-500ms decisions), microkernel runs at Ring 0 (<1ms interrupts). 36-month timeline.

| Phase | Status | Period | Summary |
|-------|--------|--------|---------|
| Phase 0 | COMPLETE | Months 1-6 | Validation (80% success, GO decision) |
| Phase 1 | COMPLETE | Months 6-12 | PoC on x86 QEMU (26/26 weeks, Dec 2025) |
| **Phase 2** | **ACTIVE** | Months 12-24 | Alpha on Pi 4 bare metal |
| Phase 3 | Future | Months 24-30 | Beta (10+ configs, security audit) |
| Phase 4 | Future | Months 30-36 | Production v1.0 |

**Current:** Phase 2, Week 49 IN PROGRESS (February 13, 2026). 30-day stability test prep + security self-audit. 3 bugs fixed (1 HIGH, 2 MEDIUM).

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

### Phase 2 Split Deployment (CURRENT)

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
├── phase2/                     # ACTIVE
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

## Current Status (Phase 3 Pre-Work)

**Phase 3 Pre-Work IN PROGRESS** (March 19, 2026) - Interim tasks before spare x86 PC assembly

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

**Next:** All 8 pre-work tasks DONE. Ready for Phase 3a when spare PC is assembled.

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

### Phase 3 Weeks (After Spare PC Assembly)

| Weeks | Task |
|-------|------|
| 1-6 | Phase 3a: Spare PC as GPU host + Pi 4 UART |
| 7-28 | Phase 3b: Pure bare-metal seL4 x86-64 |
| 29-40 | Phase 3c: Hardening, fuzz testing, SHIELD |
| 41-44 | Final report + git tag v0.3.0-beta |

### Validated Metrics

| Metric | Target | Actual |
|--------|--------|--------|
| IPC latency | <100us | 54us (Phase 1), 7ms UART (Phase 2) |
| Cache hit rate | >80% | 85.7% |
| AI inference | <500ms | 558ms GPU Phi-3, <100ms Llama 3.2 1B |
| Boot time | <60s | ~2s |
| SHIELD block rate | >90% | 100% harmful blocked, 0% FP |
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
- **Security Audit:** `phase2/docs/SECURITY_SELF_AUDIT.md`
- **Phase 2 Final Report:** `phase2/docs/PHASE_2_FINAL_REPORT.md`
- **Phase 3 Hardware Research:** `phase2/docs/PHASE_3_HARDWARE_RESEARCH.md`
- **Phase 3 Kickoff:** `phase2/docs/PHASE_3_KICKOFF.md`
- **Phase 3 Implementation Plan:** `phase3/docs/PHASE_3_IMPLEMENTATION_PLAN.md`

### Rules
- Always update CLAUDE.md and week status files after completing work
- COMMIT WEEKLY with week number in message
- Update `phase2/docs/PHASE_2_PROGRESS_TRACKER.md` when completing work
- Test everything: aim for 100% pass rate
- Phase 2 is C-only on Pi 4 (no Python runtime on seL4)

### Codebase Metrics

- **Phase 1:** 39,106 LOC, 95 files, 338 test functions (COMPLETE)
- **Phase 2:** ~10,000+ LOC, growing (drivers, IPC, build system)
- **Total:** ~50,000+ LOC, 127+ markdown docs

### Hardware Pivot Context

Intel NUC ($1,200) → Raspberry Pi 4 ($75). seL4 ARM64 heritage, bare metal access, hardware owned.
Trade-offs: slower UART IPC (7ms vs 54us), Llama 3.2 1B only, SD card (no NVMe).
Doc: `phase2/docs/PHASE_2_HARDWARE_PIVOT.md`

### Technology Stack

- **Microkernel:** seL4 (formally verified) on ARM64
- **AI Models:** Phi-3 Mini 3.8B, Llama 3.2 1B (on host PC)
- **Build:** TII seL4 build system + CMake/Ninja
- **Cross-compiler:** aarch64-linux-gnu-gcc 13.3.0
- **Bootloader:** U-Boot 2026.01
- **Hardware:** Raspberry Pi 4 (BCM2711, Cortex-A72, 8GB RAM)
