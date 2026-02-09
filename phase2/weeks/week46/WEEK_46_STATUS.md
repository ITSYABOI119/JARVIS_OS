# Week 46 Status: Boot Optimization + Power State Management

**Status:** HARDWARE VERIFIED (February 9, 2026)
**Goal:** Boot stage tracking, warm reboot support, power management

## Deliverables

| Item | Status |
|------|--------|
| Boot manager (per-stage timing, lazy init tracking) | DONE |
| Warm reboot (SD sector persistence, warm/cold detect) | DONE |
| Power management (WFI idle, ARM frequency scaling) | DONE |
| Shared mailbox API (thermal_mailbox_tag) | DONE |
| Shell commands: boot, power, reboot warm/cold | DONE |
| boot_manager.h/c (~300 LOC) | DONE |
| warm_reboot.h/c (~350 LOC) | DONE |
| bcm_power.h/c (~280 LOC) | DONE |
| 10 new tests in main_arm64.c | DONE |
| Build: 0 errors | DONE |

## New Files
- `phase2/src/boot/boot_manager.h/c` — Boot stage timing tracker (12 stages, lazy init support)
- `phase2/src/boot/warm_reboot.h/c` — Warm/cold boot via SD sector 30374000 persistence
- `phase2/src/drivers/bcm_power.h/c` — WFI idle + ARM clock scaling via VideoCore mailbox

## Modified Files
- `phase2/src/drivers/bcm_thermal.h/c` — Added `thermal_mailbox_tag()` generic property call + clock tag defines
- `phase2/src/sel4/main_arm64.c` — Boot stage wrappers, warm reboot init, power init, 10 Week 46 tests
- `phase2/src/drivers/net_cmd.c` — New commands: boot, power, reboot warm/cold
- `phase2/src/jarvis-sel4-cmake/CMakeLists.txt` — Added 3 new source files

## New Shell Commands
- `boot` — Show per-stage boot timing report
- `power` — Show power status (ARM freq, profile, WFI count)
- `power idle` / `power low` — 600 MHz low-power mode
- `power med` — 1000 MHz medium performance
- `power normal` / `power high` — 1500 MHz default
- `power perf` / `power max` — 1800 MHz max performance
- `reboot warm` — Save state to SD and reboot (next boot is warm)
- `reboot cold` — Clear state and reboot (clean cold boot)

## Architecture

### Boot Manager
- Tracks 12 init stages: systimer, thermal, watchdog, fdt, uart, cache, emmc, genet, net, gpio, i2c, usb
- Per-stage microsecond timing via BCM2711 System Timer
- Lazy init flag for optional drivers (I2C, USB marked lazy)
- Required flag for critical drivers (systimer, uart, emmc)
- Reports via `[BOOT]` prefix during boot and `boot` shell command

### Warm Reboot
- 512-byte state struct persisted to SD sector 30374000
- XOR checksum over first 60 bytes for validation
- Magic `0x4A525753` ("JRWS") + version check
- On warm boot: skips optional probing, increments boot counter
- Uses existing EMMC driver (read/write) and watchdog (reboot trigger)

### Power Management
- WFI instruction in IPC idle loop (halts core until interrupt)
- ARM clock scaling via VideoCore mailbox (tag 0x00038002)
- 4 profiles: LOW (600MHz), MED (1GHz), HIGH (1.5GHz), MAX (1.8GHz)
- Reads boot frequency on init, allows runtime switching
- Shares mailbox MMIO with bcm_thermal via `thermal_mailbox_tag()`

## Build Fix
- GCC 13 `-Werror` rejects `(uint32_t*)` cast on packed struct pointer
- Fixed `compute_checksum()` in warm_reboot.c to use `memcpy` (known pattern from MEMORY.md)

## Test Results
HARDWARE VERIFIED: 89 PASS, 0 FAIL, 3 SKIP (79 existing + 10 new)

## Hardware Test Notes
- Boot total time: 1848ms (well under 10s threshold)
- Boot stage report: systimer 0ms, thermal 56ms, watchdog 45ms, fdt 7ms, uart 341ms
- Warm reboot save/load to SD: PASS (cold boot #1, save+clear cycle works)
- ARM clock read: 600 MHz (GPU firmware default)
- Power status command: working
- Fixed boot_manager.c systimer stage timing (start_us=0 when timer unmapped)
