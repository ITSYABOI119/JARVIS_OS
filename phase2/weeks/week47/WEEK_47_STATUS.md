# Week 47 Status: SPI + Hardware RNG + PWM Drivers

**Date:** February 10, 2026
**Status:** BUILD VERIFIED (pending hardware test)

---

## Summary

Added three new peripheral drivers for Raspberry Pi 4 bare-metal seL4:

1. **BCM2835 SPI0** - SPI master at 0xFE204000 (polled FIFO, full-duplex)
2. **iproc-rng200 Hardware RNG** - True random number generator at 0xFE104000
3. **BCM2835 PWM** - 2-channel hardware PWM at 0xFE20C000

## New Files

| File | LOC | Description |
|------|-----|-------------|
| `phase2/src/drivers/bcm_spi.h` | 111 | SPI0 register defs + API |
| `phase2/src/drivers/bcm_spi.c` | 317 | SPI0 implementation (polled FIFO transfer) |
| `phase2/src/drivers/bcm_rng.h` | 105 | iproc-rng200 register defs + API |
| `phase2/src/drivers/bcm_rng.c` | 236 | RNG implementation (FIFO entropy read) |
| `phase2/src/drivers/bcm_pwm.h` | 144 | PWM register defs + API |
| `phase2/src/drivers/bcm_pwm.c` | 335 | PWM implementation (M/S mode, mailbox clock) |

## Modified Files

| File | Changes |
|------|---------|
| `phase2/src/sel4/main_arm64.c` | Added includes, RNG early init, GPIO/SPI/PWM init, 11-test W47 suite |
| `phase2/src/drivers/net_cmd.c` | Added `spi`, `rng`, `pwm` command dispatch |
| `phase2/src/drivers/net_cmd.h` | Updated supported commands list |
| `phase2/src/boot/boot_manager.h` | Added BOOT_STAGE_SPI, BOOT_STAGE_RNG, BOOT_STAGE_PWM |
| `phase2/src/boot/boot_manager.c` | Added stage names for spi, rng, pwm |
| `phase2/src/jarvis-sel4-cmake/CMakeLists.txt` | Added 3 new source files |

## Init Order (Device Watermark)

```
systimer  (0xFE003000)  existing
mailbox   (0xFE00B000)  existing
watchdog  (0xFE100000)  existing
RNG       (0xFE104000)  NEW - after watchdog, before UART
FDT       (no MMIO)     existing
UART      (0xFE200000)  existing
SPI       (0xFE204000)  NEW - after UART, before EMMC
PWM       (0xFE20C000)  NEW - after SPI, before EMMC
EMMC      (0xFE340000)  existing
GENET     (0xFD580000)  existing (separate untyped)
GPIO      (reuses UART) existing
I2C       (0xFE804000)  existing
USB       (0xFE980000)  existing
```

## Test Suite (11 tests)

| # | Test | What it verifies |
|---|------|-----------------|
| T1 | spi_init | SPI0 MMIO mapping + init returns 0 |
| T2 | spi_loopback | 4-byte transfer completes without timeout |
| T3 | spi_clock_config | Set CDIV=64 (~1.95 MHz), restore default |
| T4 | spi_cs_select | CS0/CS1 select OK, invalid CS returns error |
| T5 | spi_status_cmd | `cmd_dispatch("spi")` returns output |
| T6 | rng_init | RNG initialized, `rng_is_initialized()` true |
| T7 | rng_read_word | 2 random words non-zero and different |
| T8 | rng_status_cmd | `cmd_dispatch("rng")` returns output |
| T9 | pwm_init | PWM MMIO mapping + init returns 0 |
| T10 | pwm_enable_disable | Set 50% duty, enable ch0, disable ch0 |
| T11 | pwm_status_cmd | `cmd_dispatch("pwm")` returns output |

## Shell Commands

| Command | Output |
|---------|--------|
| `spi` | SPI0 status (clock, CS, CPOL/CPHA, FIFO status) |
| `rng` | RNG status (FIFO count, total bytes read) |
| `pwm` | PWM status (channel state, frequency, duty cycle) |
| `pwm on <ch> <duty>` | Enable PWM channel with duty % |
| `pwm off <ch>` | Disable PWM channel |

## Build Result

- Build: 0 errors, 0 warnings (standard RWX segment warnings only)
- Image: kernel8.img ~1.9 MB
- Expected: 100 PASS, 0 FAIL, 3 SKIP (89 existing + 11 new)

## Key Design Decisions

1. **RNG early init** - Mapped before UART (paddr ordering), uses `seL4_DebugPutChar()` for status
2. **GPIO early call** - `gpio_init()` called before SPI/PWM so ALT0 pins get configured; idempotent, called again at Week 43 section (returns immediately)
3. **SPI polled FIFO** - Simple, reliable; no DMA needed for typical peripheral comms
4. **PWM mailbox clock** - Uses existing `thermal_mailbox_tag()` to set PWM clock via VideoCore; graceful degradation if mailbox unavailable
5. **Auto vaddr** - SPI and PWM use vaddr=0 (auto-assign) since they're mapped in normal cursor order after UART
