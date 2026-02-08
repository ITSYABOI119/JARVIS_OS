# Week 44 Status: Watchdog + Thermal Monitoring

**Status:** HARDWARE VERIFIED (February 8, 2026)
**Goal:** PM watchdog timer, VideoCore thermal monitoring, power management

---

## Summary

Added BCM2711 PM watchdog timer (auto-reboot on hang, 10s timeout) and VideoCore
mailbox thermal monitoring (GPU temperature reading). Created Python host-side
power manager for remote monitoring. Added shell commands: temp, watchdog, reboot.
Enhanced uart_device_map_page() with binary buddy skip for efficient large-gap
device mapping.

## Achievements

1. **PM Watchdog Driver** (bcm_watchdog.h/c)
   - PM registers at paddr 0xFE100000, mapped to explicit vaddr 0x611000
   - Password-protected register writes (0x5A000000)
   - 20-bit counter at ~62.5 kHz (max 16.7s timeout)
   - Start/stop/feed/reboot API
   - Status reporting with remaining time and last feed timestamp
   - 200 LOC implementation + 90 LOC header

2. **Thermal Monitoring Driver** (bcm_thermal.h/c)
   - VideoCore mailbox at paddr 0xFE00B000, mapped to explicit vaddr 0x610000
   - Property tag interface: TAG_GET_TEMPERATURE (0x00030006)
   - DMA buffer for property tags (GPU needs bus address = paddr | 0xC0000000)
   - Temperature in millidegrees C with formatted status output
   - 200 LOC implementation + 85 LOC header

3. **Binary Buddy Skip Enhancement** (uart_pl011.c)
   - uart_device_map_page() now uses buddy skip for gaps > 16 pages
   - Reuses same algorithm as uart_map_device_frame()
   - ~5 retypes instead of 244 page-by-page skips for 0xFE00C000->0xFE100000

4. **Power Manager** (power_manager.py)
   - Python host-side thermal monitoring via UART IPC
   - Continuous monitoring with adaptive polling (faster when critical)
   - Thermal thresholds: WARN 70C, CRITICAL 80C, EMERGENCY 85C
   - CLI: --temp, --watchdog-status, --reboot, --monitor

5. **Shell Commands** (net_cmd.c)
   - "temp" command: shows SoC temperature
   - "watchdog" command: shows watchdog status, timeout, remaining time
   - "reboot" command: triggers immediate PM watchdog reboot

6. **Watchdog Heartbeat** (main_arm64.c)
   - Fed every IPC loop iteration (~50ms)
   - 10-second timeout: >200x margin over loop period

## New Files

| File | Lines | Purpose |
|------|-------|---------|
| phase2/src/drivers/bcm_watchdog.h | 90 | PM watchdog register defs, API |
| phase2/src/drivers/bcm_watchdog.c | 200 | PM watchdog driver implementation |
| phase2/src/drivers/bcm_thermal.h | 85 | Mailbox thermal register defs, API |
| phase2/src/drivers/bcm_thermal.c | 200 | Thermal monitoring implementation |
| phase2/src/ai/power_manager.py | 200 | Python host-side power manager |

## Modified Files

| File | Change |
|------|--------|
| uart_pl011.c | Enhanced uart_device_map_page() with buddy skip for gaps > 16 pages |
| main_arm64.c | Early mailbox+PM mapping, watchdog feed in IPC loop, 10 tests |
| net_cmd.c | Added temp, watchdog, reboot commands |
| CMakeLists.txt | Added bcm_watchdog.c and bcm_thermal.c to build |
| CLAUDE.md | Updated to Week 44, new milestones and files |

## Critical Design: Early Device Mapping

seL4's device untyped watermark only moves forward. Mailbox (0xFE00B000) and PM
(0xFE100000) are at physical addresses BEFORE UART (0xFE200000), so they must be
mapped in the init sequence between systimer and uart_init():

```
systimer_init()   -> cursor = 0xFE004000  (auto vaddr 0x5c2000)
thermal_init()    -> maps 0xFE00B000      (explicit vaddr 0x610000, buddy skip)
watchdog_init()   -> maps 0xFE100000      (explicit vaddr 0x611000, buddy skip)
uart_init()       -> maps 0xFE200000+     (buddy skip from 0xFE101000)
```

Explicit vaddr (0x610000, 0x611000) avoids shifting existing device auto-assignments
and DMA pool collision at 0x5c4000.

## Test Plan

| # | Test | Type | Expected |
|---|------|------|----------|
| 1 | watchdog_init | Unit | PASS |
| 2 | watchdog_start_stop | Unit | PASS |
| 3 | watchdog_feed | Unit | PASS |
| 4 | watchdog_status_cmd | Integration | PASS |
| 5 | thermal_init | Unit | PASS |
| 6 | thermal_read_temp | Unit | PASS |
| 7 | thermal_status_cmd | Integration | PASS |
| 8 | reboot_cmd_exists | Unit | PASS |
| 9 | watchdog_feed_timing | Unit | PASS |
| 10 | early_mapping_order | Integration | PASS |

## Test Totals

```
Existing tests unchanged: 59 PASS, 0 FAIL, 3 SKIP
New this week: 10 (4 Watchdog + 3 Thermal + 3 Integration)
Total: 69 PASS, 0 FAIL, 3 SKIP
```

## Hardware Test Results

```
Week 44 Tests: 10 PASS, 0 FAIL, 0 SKIP
  watchdog_init=PASS  watchdog_start_stop=PASS  watchdog_feed=PASS
  watchdog_status_cmd=PASS  thermal_init=PASS
  thermal_read_temp=PASS (38.4C)  thermal_status_cmd=PASS
  reboot_cmd_exists=PASS  watchdog_feed_timing=PASS
  early_mapping_order=PASS

Total: 69 PASS, 0 FAIL, 3 SKIP (12 EMMC + 6 TX + 5 RX/Net + 9 Cmd + 4 USB
  + 10 Kbd + 13 GPIO/I2C/Stress + 10 Watchdog/Thermal)
```

### Hardware Observations
- Mailbox mapped at paddr 0xFE00B000 -> vaddr 0x610000 (7 page skips)
- PM watchdog mapped at paddr 0xFE100000 -> vaddr 0x611000 (buddy skip: 5 retypes)
- UART buddy skip from 0xFE101000 -> 0xFE200000: 8 retypes (4KB..512KB)
- SoC temperature: 38.4C (idle, no heatsink fan)
- Watchdog started with 10s timeout, fed every ~50ms in IPC loop
- I2C BSC1 now maps at vaddr 0x60a000 (vaddr skip fix prevents DMA pool collision)
- EMMC ADMA2 still 11.9 MB/s (no regression from early device mapping)
- System stable through entire 69-test suite with watchdog active

### Bugfix: DMA/Device VAddr Collision
- **Root cause:** `uart_device_map_page()` auto-assign overlapped DMA pool (0x5c4000)
- **Impact:** thermal tag buffer overwritten by I2C device mapping -> mailbox read failure
- **Fix:** Auto-assign now skips 0x5c4000-0x609FFF (DMA pool + GENET MMIO)
- **Result:** I2C maps at 0x60a000, thermal DMA buffer at 0x5c4000 survives intact

---

*Created: February 8, 2026*
*Hardware verified: February 8, 2026*
