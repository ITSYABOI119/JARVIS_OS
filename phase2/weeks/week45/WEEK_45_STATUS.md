# Week 45 Status: Device Tree + Boot Timing

**Status:** HARDWARE VERIFIED (February 9, 2026)
**Goal:** Embedded device tree, FDT parser, boot timing instrumentation

---

## Summary

Added embedded Flattened Device Tree (FDT) support: custom jarvis.dts compiled to
DTB and embedded as a C array in the rootserver binary. Minimal FDT parser (~400 LOC)
extracts device addresses, properties, and strings from the DTB at boot time. Added
boot timing instrumentation using BCM2711 System Timer for init phase profiling.
New "dt" shell command shows device tree info.

## Achievements

1. **JARVIS Device Tree Source** (jarvis.dts)
   - Custom DTS with all 9 peripheral nodes using ARM physical addresses
   - JARVIS-specific properties: jarvis,vaddr, jarvis,phase, jarvis,week
   - #address-cells=2, #size-cells=1 layout matching existing driver expectations
   - Compiled via dtc to 2,280-byte DTB binary

2. **Embedded DTB Header** (jarvis_dtb_data.h)
   - DTB compiled and embedded as `static const unsigned char jarvis_dtb[]`
   - Auto-generated via xxd -i from jarvis.dtb
   - Include guards, const qualifiers, size variable

3. **FDT Parser** (fdt_parser.h/c, ~500 LOC)
   - No dynamic allocation - pure pointer arithmetic on raw DTB blob
   - Big-endian byte swapping via `__builtin_bswap32`
   - Path-based node lookup: `/soc/serial@fe201000`
   - Property access: reg, u32, string, compatible
   - Child node counting for enumeration
   - All symbols prefixed `jarvis_fdt_*` to avoid collision with system libfdt

4. **Boot Timing Instrumentation** (main_arm64.c)
   - Captures systimer timestamps at key init phases
   - Reports: early init (mailbox+watchdog+FDT), UART init, banner+sysinfo
   - Total init time in microseconds and milliseconds

5. **Shell Command: "dt"** (net_cmd.c)
   - Shows device tree model, size, SOC device count, phase/week

## New Files

| File | Lines | Purpose |
|------|-------|---------|
| phase2/src/boot/jarvis.dts | 125 | JARVIS device tree source |
| phase2/src/boot/jarvis.dtb | (binary) | Compiled DTB blob (2,280 bytes) |
| phase2/src/boot/jarvis_dtb_data.h | 206 | Embedded DTB as C array |
| phase2/src/boot/fdt_parser.h | 117 | FDT parser API (jarvis_fdt_* prefix) |
| phase2/src/boot/fdt_parser.c | 400 | FDT parser implementation |

## Modified Files

| File | Change |
|------|--------|
| main_arm64.c | FDT init in boot sequence, boot timing, 10 Week 45 tests |
| net_cmd.c | Added "dt" shell command |
| CMakeLists.txt | Added fdt_parser.c, boot include directory |
| build_and_copy_kernel.sh | Added boot directory sync |

## Architecture: Why Embedded DTB?

seL4's rootserver CANNOT access the platform DTB:
- DTB is loaded to kernel virtual address space (0xffffff80...)
- No bootinfo capability covers it
- No untyped covers the DTB physical address (0x1250000)

Solution: Compile a JARVIS-specific DTS to DTB, then embed it as a C byte array
in the rootserver binary. The FDT parser reads it directly from .rodata — no MMIO
or device untyped needed.

## Test Plan

| # | Test | Type | Expected |
|---|------|------|----------|
| 1 | fdt_init | Unit | PASS |
| 2 | fdt_totalsize | Unit | PASS (2280 bytes) |
| 3 | fdt_get_string(model) | Unit | PASS (contains "JARVIS") |
| 4 | fdt_get_u32(phase) | Unit | PASS (2) |
| 5 | fdt_get_u32(week) | Unit | PASS (45) |
| 6 | fdt_get_reg(uart) | Unit | PASS (0xfe201000, 0x1000) |
| 7 | fdt_get_reg(genet) | Unit | PASS (0xfd580000, 0x6000) |
| 8 | fdt_count_children(soc) | Unit | PASS (9) |
| 9 | fdt_get_u32(wdt timeout) | Unit | PASS (10) |
| 10 | dt_cmd | Integration | PASS |

## Test Totals

```
Existing tests unchanged: 69 PASS, 0 FAIL, 3 SKIP
New this week: 10 (8 FDT parser + 1 boot timing + 1 shell cmd)
Expected total: 79 PASS, 0 FAIL, 3 SKIP
```

## Key Design: libfdt Symbol Collision

The TII seL4 build system links `libfdt.a` automatically. Our FDT functions collided
with standard libfdt symbols (e.g., `fdt_get_string`, `fdt_totalsize`). Fixed by
prefixing all public symbols with `jarvis_`:

- `jarvis_fdt_init()`, `jarvis_fdt_is_valid()`, `jarvis_fdt_totalsize()`
- `jarvis_fdt_get_prop()`, `jarvis_fdt_get_reg()`, `jarvis_fdt_get_u32()`
- `jarvis_fdt_get_string()`, `jarvis_fdt_count_children()`
- Struct types: `jarvis_fdt_header`, `jarvis_fdt_prop_result`, `jarvis_fdt_reg_result`
- Constants: `JARVIS_FDT_MAGIC`, `JARVIS_FDT_BEGIN_NODE`, etc.

## Boot Sequence (Updated)

```
systimer_init()     → 0xFE003000 (auto vaddr 0x5c2000)
  ↓ boot_t0 captured
thermal_init()      → 0xFE00B000 (explicit vaddr 0x610000)
watchdog_init()     → 0xFE100000 (explicit vaddr 0x611000)
jarvis_fdt_init()   → parses embedded DTB (no MMIO needed)
  ↓ boot_t1 captured
uart_init()         → 0xFE200000-01 (buddy skip from 0xFE101000)
  ↓ boot_t2 captured
banner + sysinfo
  ↓ boot_t3 captured → timing report printed
```

---

*Created: February 9, 2026*
*Build verified: February 9, 2026*
