# Week 33 Results

**Status:** COMPLETE ✅
**Test Date:** January 10, 2026
**Effort:** ~4 hours (debugging + 3 kernel iterations)

---

## Summary

**Week 33 Goal:** Enable UART RX on Pi 4 via seL4 device frame mapping

**Result:** SUCCESS - UART RX enabled, device frame mapped to 0x5c0000

---

## Boot Log (Final Successful Boot)

```
Booting all finished, dropped to user space
[UART] uart_map_device_frame() starting
[UART] Got bootinfo
[UART] Untypeds: 0x000000000000004f
[UART] DevUT[0x0000000000000005]: paddr=0x00000000fe000000 size=0x0000000001000000
[UART] Device untypeds found: 0x000000000000000a
[UART] Looking for UART at 0x00000000fe201000
[UART] Found device untyped, cap=0x00000000000001d7 size_bits=0x0000000000000018
[UART] Using empty slot: 0x0000000000000221
[UART] Retyping to SmallPage...
[UART] Retype succeeded
[UART] Mapping to vaddr=0x00000000005c0000
[UART] Page_Map succeeded
[UART] MMIO base set to 0x00000000005c0000
!!! JARVIS STARTED!!!

*** JARVIS ROOTSERVER STARTING ***

========================================
  JARVIS AI-OS v0.2 - Phase 2 Week 33
  ARM64 Raspberry Pi 4 Port
========================================
  seL4 + PL011 UART + Decision Cache
  Build: Jan 10 2026 11:58:13
========================================

System Information:
  Architecture: aarch64 (ARM64)
  Platform: Raspberry Pi 4 (BCM2711)
  CPU: Cortex-A72 @ 1.8 GHz
  Kernel: seL4 microkernel
  UART: PL011 @ 0xFE201000
  Baud: 115200 8N1
  Phase: 2 Week 33
  UART RX: ENABLED (device frame mapped)

Initializing decision cache...
  Cache initialized (512 entries)
Loaded 50 initial patterns into cache
Loaded 258 total patterns into cache
  Loaded 258 patterns into cache

Cache Stats: 252 entries loaded

Starting UART IPC handler...

========================================
UART IPC Handler Running (ARM64)
Waiting for Python queries...
========================================
```

---

## Status Checklist

| Item | Expected | Actual | Status |
|------|----------|--------|--------|
| U-Boot loads kernel8.img | Yes | 1.5MB in 80ms | ✅ |
| seL4 elfloader runs | Output visible | Cortex-A72 detected | ✅ |
| Rootserver starts | "!!! JARVIS STARTED!!!" | Yes | ✅ |
| Banner displays | Week 33 banner | "Phase 2 Week 33" | ✅ |
| UART RX status | ENABLED | ENABLED (device frame mapped) | ✅ |
| Cache loads | 258 patterns | 258 patterns | ✅ |
| IPC handler starts | "Waiting for Python queries..." | Yes | ✅ |

---

## Debugging Journey

### Iteration 1: Week 32 kernel (wrong version)
- Boot showed "Week 32" banner
- Sources not synced to build directory
- **Fix:** Sync sources before ninja build

### Iteration 2: No debug output
- UART RX showed "DISABLED" but no details
- Couldn't determine failure point
- **Fix:** Add comprehensive debug_puts()/debug_hex() output

### Iteration 3: vaddr 0x0ffff000 failed
```
[UART] Mapping to vaddr=0x000000000ffff000
[UART] ERROR: Page_Map failed, err=0x0000000000000006
```
- Error 6 = seL4_FailedLookup
- No page table structures at that address
- **Fix:** Change vaddr to 0x5c0000 (within rootserver VSpace)

### Iteration 4: SUCCESS
```
[UART] Mapping to vaddr=0x00000000005c0000
[UART] Page_Map succeeded
UART RX: ENABLED (device frame mapped)
```

---

## Key Technical Insights

1. **seL4 VSpace Coverage:** Rootserver VSpace only has page tables for its load range (0x400000-0x5b9fff). Mapping outside requires creating PUD/PMD structures.

2. **Device Untyped Location:** BCM2711 peripherals at 0xFE000000 are covered by a 16MB device untyped in bootinfo.

3. **Retype vs Map:** Retype creates the frame capability (succeeded), but Map requires existing page tables (failed initially).

4. **Debug Strategy:** seL4_DebugPutChar() is invaluable for early boot debugging when printf isn't available.

---

## Files Changed

| File | Lines Changed | Description |
|------|---------------|-------------|
| main_arm64.c | ~20 | Week 33 banner, sel4_puts() |
| uart_pl011.c | ~150 | Device frame mapping, debug output |
| CMakeLists.txt | 1 | Version comment |

---

## Next Steps (Week 34)

1. Test UART RX with PuTTY character input
2. Connect Python uart_ipc_client.py
3. Send UART IPC frame (0xAA55 sync + query)
4. Verify cache lookup response
5. Measure round-trip latency

---

## Metrics

| Metric | Value |
|--------|-------|
| Kernel size | 1,573,152 bytes |
| Boot time | ~6 seconds (GPU to IPC handler) |
| Cache patterns | 258 loaded |
| Device untypeds | 10 found |
| UART mapped at | 0x5c0000 (vaddr) |
| UART physical | 0xFE201000 |
