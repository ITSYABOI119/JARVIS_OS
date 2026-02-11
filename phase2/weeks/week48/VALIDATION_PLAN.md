# Week 48 Validation Plan

## Code Review Summary

### DMA Driver (bcm_dma.h + bcm_dma.c) - ALL CHECKLIST ITEMS PASS

| # | Check | Result |
|---|-------|--------|
| 1 | Maps MMIO at 0xFE007000 using uart_device_map_page() | PASS |
| 2 | Uses explicit vaddr 0x612000 | PASS |
| 3 | CB struct is 32-byte aligned (8 x uint32_t = 32 bytes, packed) | PASS |
| 4 | Bus address conversion: paddr \| 0xC0000000 | PASS |
| 5 | TI field for memcpy: SRC_INC \| DEST_INC (0x110) | PASS |
| 6 | CS register bits correct (RESET=31, ACTIVE=0, END=1, ERROR=2) | PASS |
| 7 | dma_wait() polls END or ERROR bits with systimer timeout | PASS |
| 8 | dma_cancel() calls chan_reset() which does ABORT then RESET | PASS |
| 9 | No use of channels 7-14 or channel 15 (MAX_CHANNELS=7, 0-6 only) | PASS |
| 10 | Uses dma_alloc() for CB memory (uncached, physically contiguous) | PASS |
| 11 | DSB barriers on all register accesses (dma_reg_read/write) | PASS |
| 12 | No integration with EMMC/GENET (standalone mem2mem only) | PASS |

### Minor Issues Found

**Issue 1 (Low): Misleading error code in dma_wait()**
- File: `phase2/src/drivers/bcm_dma.c`, lines 293 and 319
- What: When DMA_CS_ERROR is detected, returns `DMA_ENGINE_ERR_INIT` (-1) which semantically means "initialization error", not "transfer error"
- Suggested fix: Add `DMA_ENGINE_ERR_XFER (-6)` to bcm_dma.h and return that instead
- Severity: Low - does not affect functionality, only error reporting clarity

**Issue 2 (Low): Consider adding NO_WIDE_BURSTS to TI flags**
- File: `phase2/src/drivers/bcm_dma.h`, line 84
- What: Linux kernel BCM DMA driver adds `DMA_TI_NO_WIDE_BURSTS` (bit 26) for mem2mem transfers as a safety measure against wide AXI bus issues
- Suggested fix: Change `DMA_TI_MEM2MEM` to `(DMA_TI_NO_WIDE_BURSTS | DMA_TI_SRC_INC | DMA_TI_DEST_INC)`
- Severity: Low - transfers should work without it, but Linux sets it for robustness

**Issue 3 (Design): dma_memcpy len==0 returns OK**
- File: `phase2/src/drivers/bcm_dma.c`, line 235
- What: Zero-length transfer returns DMA_ENGINE_OK instead of DMA_ENGINE_ERR_PARAM
- Suggested fix: Return ERR_PARAM for len==0 (or document current behavior as intentional no-op)
- Severity: Design choice - current behavior is safe but may mask caller bugs

### Stability Harness (stability_harness.py) - ALL CHECKLIST ITEMS PASS

| # | Check | Result |
|---|-------|--------|
| 1 | Imports from uart_ipc_client.py correctly | PASS |
| 2 | Configurable duration, interval, mix ratios | PASS |
| 3 | CSV logging with proper format (6-column header) | PASS |
| 4 | Heartbeat-based hang detection (30s timeout) | PASS |
| 5 | Protocol reset on timeout + full reconnect fallback | PASS |
| 6 | Self-test mode (--self-test, 4 mock tests) | PASS |
| 7 | CLI with argparse (--port, --baud, --duration, etc.) | PASS |
| 8 | Summary report (pass/fail/warn %, RTT stats, cache/SHIELD rates) | PASS |

### Minor Issues Found

**Issue 4 (Low): TestMix ratios not validated**
- File: `phase2/src/ai/stability_harness.py`, line 86-92
- What: Docstring says ratios "must sum to 1.0" but no runtime validation
- Suggested fix: Add assertion or normalization in StabilityHarness.__init__()
- Severity: Low - _pick_test() fallback handles the edge case gracefully

**Issue 5 (Cosmetic): CSV timestamp lacks sub-second precision**
- File: `phase2/src/ai/stability_harness.py`, line 197
- What: Timestamp format `%Y-%m-%dT%H:%M:%S` has only second precision
- Suggested fix: Use `%Y-%m-%dT%H:%M:%S.%f` for microsecond precision
- Severity: Cosmetic - the latency_ms column already has precise timing data

---

## Hardware Tests (REQUIRES PI 4 HARDWARE TEST)

1. **DMA MMIO mapping at 0xFE007000**
   - Verify dma_engine_init() succeeds without irq 27 halt
   - Confirm debug output shows vaddr 0x612000
   - Confirm global ENABLE register reads back 0x7F (channels 0-6)
   - Init order: systimer -> DMA -> mailbox (ascending paddr 0xFE003000 -> 0xFE007000 -> 0xFE00B000)

2. **DMA memcpy 256 bytes (data integrity)**
   - Allocate src + dst DMA buffers via dma_alloc()
   - Fill src with known pattern (0xA5 or sequential)
   - DMA memcpy src -> dst
   - Verify dst matches src byte-for-byte
   - Verify dma_wait() returns DMA_ENGINE_OK

3. **DMA memcpy 4096 bytes (full page transfer)**
   - Same as #2 but with a full page to test larger transfers
   - Measure transfer time via systimer (expect < 1ms)

4. **DMA channel allocation + free cycle**
   - Allocate all 7 channels (0-6), verify sequential IDs
   - Attempt 8th allocation, verify returns -1
   - Free all channels, re-allocate, verify IDs available again

5. **DMA error handling: double free, invalid channel**
   - dma_chan_free(-1) -> ERR_PARAM
   - dma_chan_free(7) -> ERR_PARAM
   - dma_chan_free(unallocated) -> ERR_PARAM
   - dma_memcpy on unallocated channel -> ERR_PARAM

6. **1-hour stability harness run over UART**
   - `python stability_harness.py --port /dev/ttyUSB0 --duration 60`
   - Monitor for hangs, protocol resets, reconnects
   - Expect: >3000 test entries, >95% PASS rate

7. **Heartbeat RTT measurement**
   - Extract RTT stats from stability harness summary
   - Expect: avg ~7ms, max < 50ms (consistent with prior measurements)

8. **Shell command execution (all commands)**
   - Run each shell command via stability harness: temp, watchdog, gpio, i2c, spi, rng, pwm, dt, boot, power, usb, stress, ifconfig, netstat
   - Verify all return non-empty responses

9. **CSV log completeness check**
   - Verify stability_log.csv exists and has correct 6-column format
   - Verify row count matches harness stats.total
   - Spot-check 10 random rows for valid timestamps and latency values

10. **DMA "dma" shell command output**
    - Type "dma" at JARVIS shell prompt
    - Verify output shows: engine initialized, channels 0-6, ENABLE register, CB pool paddr

## Software Tests (can verify without hardware)

1. **DMA driver compiles with -Werror**
   - Build with `ninja` in rpi4_jarvis build dir
   - Verify 0 warnings, 0 errors for bcm_dma.c

2. **Stability harness self-tests pass**
   - `python stability_harness.py --self-test`
   - Expect: 4/4 self-tests PASS

3. **CSV format validation**
   - Run `--self-test`, verify generated CSV has correct header
   - Verify data rows have 6 columns: timestamp, test_type, command, response_summary, latency_ms, result

4. **DMA CB alignment (sizeof check)**
   - Verify `sizeof(dma_control_block_t) == 32` at compile time
   - CB pool from dma_alloc() is page-aligned (4096), so each CB at +N*32 is 32-byte aligned

5. **DMA register offset calculations**
   - Channel 0: offset 0x000 + reg
   - Channel 6: offset 0x600 + reg
   - All fit within single 4KB page: 0x600 + 0x20 = 0x620 < 0x1000
   - Wait: channels 0-6 span offsets 0x000 to 0x620. But global registers are at 0xFE0 and 0xFF0 which are also within the page. VERIFIED.

6. **Bus address macro correctness**
   - DMA_BUS_ADDR(0x3F000000) = 0xFF000000 (top 256MB + 0xC0000000)
   - DMA_BUS_ADDR(0x00100000) = 0xC0100000
   - Verified: BCM2711 legacy master DMA bus = ARM paddr | 0xC0000000

## Edge Cases

1. **DMA transfer of 0 bytes**
   - Current behavior: returns DMA_ENGINE_OK (no-op)
   - Verify this does NOT start hardware (no ACTIVE bit set)
   - Acceptable design choice (documented)

2. **Double channel alloc/free**
   - Allocate ch0, free ch0, free ch0 again -> expect ERR_PARAM on second free
   - Allocate ch0, allocate ch0 again -> ch0 already allocated, returns ch1

3. **Timeout handling (simulated hang)**
   - Start DMA memcpy, immediately cancel
   - Verify dma_cancel() succeeds and channel is reusable
   - Start DMA with bad CB address (0x0), verify dma_wait() returns error or timeout

4. **UART buffer overflow (large responses)**
   - Send rapid-fire commands via stability harness (interval=0.05s)
   - Verify no UART framing errors or data corruption
   - Monitor for protocol resets (should recover gracefully)

5. **All channels busy**
   - Allocate 7 channels, start transfers on all 7 simultaneously
   - Wait for all 7 to complete
   - Verify all data integrity checks pass (no cross-channel corruption)

## Pass Criteria

- All 8+ new C tests PASS on hardware (DMA init, memcpy, channel lifecycle, error handling)
- Stability harness 1-hour run completes with >95% PASS rate
- No protocol deadlocks (all hangs recovered via reset/reconnect)
- CSV log has >3000 entries (at 1-second interval over 60 minutes)
- 0 crash events (ideal), <3 acceptable with successful recovery
- DMA memcpy data integrity: 100% match for all tested sizes
- Heartbeat RTT: avg < 15ms, max < 100ms
- SHIELD block rate: 100% for known harmful actions
- Cache hit rate: >80% (consistent with prior 85.7% baseline)
