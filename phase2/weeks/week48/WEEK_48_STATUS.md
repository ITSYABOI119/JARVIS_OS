# Week 48 Status: DMA Engine + Stability Harness

**Date:** February 10-11, 2026
**Status:** HARDWARE VERIFIED - 100% Stability (298/298 PASS)

## Summary

Week 48 implements the BCM2711 DMA engine driver for memory-to-memory transfers and a Python stability test harness for automated UART IPC regression testing. Critical UART TX bug found and fixed: `seL4_DebugPutChar()` performs CR/LF translation on binary data, corrupting IPC frames. Switched to direct MMIO TX.

## Key Achievement: 100% Stability Test Pass Rate

5-minute automated stability test exercising all subsystems via UART IPC:

| Metric | Value |
|--------|-------|
| Total tests | 298 |
| Pass rate | **100.0%** |
| Fail rate | 0.0% |
| Crash events | 0 |
| Heartbeat RTT | avg=4.2ms, min=3.4ms, max=5.2ms |
| Cache hit rate | 87.4% (111/127) |
| SHIELD block rate | 39.3% (11/28) |
| Throughput | 1.0 test/sec |

## Critical Bug Fix: UART TX Corruption

**Root cause:** `uart_write()` sent binary IPC frame bytes through `seL4_DebugPutChar()`, which calls the kernel's `putDebugChar()`. The seL4 kernel performs CR/LF translation (`\n` → `\r\n`), inserting extra `0x0D` bytes into the binary stream whenever a frame byte happened to be `0x0A`. This corrupted frame structure and caused CRC mismatches on the Python side.

**Symptoms:** 43% pass rate, persistent CRC errors on all message types, cascading frame desynchronization.

**Fix:** `uart_write()` now uses direct MMIO writes to the PL011 TX FIFO (`uart_mmio_base[UART_DR/4]`), bypassing `seL4_DebugPutChar()` entirely. Waits for `UART_FR_TXFF` (TX FIFO Full) flag before each byte.

**Additional fix (from prior session):** `delay_1ms()` in `uart_recv_frame()` changed from volatile loop (55x too fast at 600 MHz) to systimer-based real 1ms delay. `power_wfi()` removed from IPC loop (caused 100-500ms sleep gaps).

## New Files

| File | Description |
|------|-------------|
| `phase2/src/drivers/bcm_dma.h` | DMA engine header (control block struct, register defs, API) |
| `phase2/src/drivers/bcm_dma.c` | DMA engine driver (channels 0-6, mem2mem, CB pool) |
| `phase2/src/ai/stability_harness.py` | Python stability test harness (UART IPC, CSV logging, hang detect) |

## Modified Files

| File | Change |
|------|--------|
| `phase2/src/jarvis-sel4-cmake/CMakeLists.txt` | Added `src/drivers/bcm_dma.c` |
| `phase2/src/sel4/main_arm64.c` | DMA init, 8 W48 tests, systimer delay, WFI removal |
| `phase2/src/drivers/uart_pl011.c` | **Direct MMIO TX in `uart_write()` (bypasses kernel CR/LF)** |
| `phase2/src/drivers/net_cmd.c` | Added `#include bcm_dma.h`, "dma" shell command |
| `phase2/src/drivers/net_cmd.h` | Updated supported command list |
| `phase2/src/boot/boot_manager.h` | Added `BOOT_STAGE_DMA` enum |
| `phase2/src/boot/boot_manager.c` | Added "dma" stage name |

## Test Suite (8 DMA Tests)

| Test | Description | Result |
|------|-------------|--------|
| T1 | dma_engine_init | PASS |
| T2 | dma_chan_alloc | PASS |
| T3 | dma_memcpy (256B pattern verify) | PASS |
| T4 | dma_chan_free + realloc | PASS |
| T5 | dma_wait after memcpy | PASS |
| T6 | dma_invalid_channel rejection | PASS |
| T7 | dma_bad_channel_memcpy rejection | PASS |
| T8 | dma_status_cmd via cmd_dispatch | PASS |

## Build Result

- **Total Tests:** 108 PASS, 0 FAIL, 3 SKIP (expected)
- **Build:** VERIFIED (0 errors, 1.9MB kernel8.img)
- **Stability:** 298/298 PASS (5-min harness, hardware verified)

## DMA Engine Details

- **Registers:** paddr 0xFE007000, mapped at vaddr 0x612000
- **Channels:** 0-6 (normal DMA, full-featured), 7-14 (lite, skipped), 15 (separate address, skipped)
- **Transfer Types:** Memory-to-memory (SRC_INC | DEST_INC)
- **Control Blocks:** 32-byte aligned, allocated from DMA pool
- **Bus Addresses:** ARM paddr | 0xC0000000 (uncached alias)
- **Init Order:** After systimer (0xFE003000), before mailbox (0xFE00B000)

## Stability Harness Details

- **Protocol:** UART IPC (SYNC 0xAA55, CRC-CCITT, direct MMIO TX)
- **Test Mix:** 40% cache queries, 25% shell commands, 15% heartbeat, 10% SHIELD, 10% network
- **Logging:** CSV format with timestamps, latency, pass/fail
- **Hang Detection:** 30s heartbeat timeout, auto protocol reset + reconnect
- **Duration:** Configurable (default 60 min, tested 5 min)

## Lessons Learned

1. **NEVER send binary data through `seL4_DebugPutChar()`** — the kernel does CR/LF translation
2. Use direct MMIO for binary protocols, kernel debug path only for human-readable text
3. Volatile delay loops are unreliable on fast CPUs — always use hardware timers
4. WFI (Wait For Interrupt) is incompatible with polled UART IPC loops
