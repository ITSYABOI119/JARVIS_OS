# Week 33: UART RX Enable via Device Frame Mapping

**Status:** COMPLETE ✅
**Dates:** January 9-10, 2026
**Goal:** Enable bidirectional UART communication (Python <-> seL4)

---

## Objectives

1. ✅ Fix UART TX using `seL4_DebugPutChar()` kernel syscall
2. ✅ Implement device frame mapping for UART RX
3. ✅ Test on Pi 4 hardware - UART RX ENABLED
4. ⏳ Verify Python <-> seL4 bidirectional communication (Week 34)

---

## Problem Statement

**Week 32 Issue:** UART output worked from elfloader (privileged mode) but not from rootserver (user space). RX was completely non-functional.

**Root Cause:** Direct MMIO access to `0xFE201000` fails in seL4 user space:
- seL4 rootserver runs unprivileged
- Cannot directly access peripheral memory addresses
- Direct access causes VM fault (data abort)

**Solution:** Two-pronged approach:
1. **TX:** Use `seL4_DebugPutChar()` - kernel syscall that works in debug builds
2. **RX:** Map UART device frame using seL4 capabilities from bootinfo

---

## Implementation

### 1. TX via seL4_DebugPutChar (main_arm64.c)

```c
static void sel4_putchar(char c)
{
    seL4_DebugPutChar(c);
}

static void sel4_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') sel4_putchar('\r');
        sel4_putchar(*s++);
    }
}
```

**Why this works:** `seL4_DebugPutChar()` is a kernel syscall that outputs directly to the debug console (UART in Pi 4's case). Available in debug builds.

### 2. RX via Device Frame Mapping (uart_pl011.c)

```c
bool uart_map_device_frame(void)
{
    seL4_BootInfo *bi = sel4runtime_bootinfo();

    // Find device untyped covering UART0_BASE (0xFE201000)
    seL4_CPtr ut = find_device_untyped(bi, UART0_BASE, &size_bits);

    // Retype to 4KB frame
    seL4_Untyped_Retype(ut, seL4_ARM_SmallPageObject, ...);

    // Map into virtual address space at 0x5c0000
    // (within existing VSpace page table coverage)
    seL4_ARM_Page_Map(frame_slot, seL4_CapInitThreadVSpace, 0x5c0000, ...);

    // Set MMIO pointer
    uart_mmio_base = (volatile uint32_t *)0x5c0000;
    return true;
}
```

**Key Discovery:** Initial attempt to map at `0x0ffff000` failed with `seL4_FailedLookup` (error 6) because there were no intermediate page tables (PUD/PMD) at that address. Solution was to map at `0x5c0000` which is within the rootserver's existing VSpace range (`0x400000-0x5b9fff`).

### 3. UART RX Functions

```c
static bool uart_mmio_rx_ready(void)
{
    uint32_t fr = uart_reg_read(UART_FR);
    return !(fr & UART_FR_RXFE);  // RXFE=0 means data available
}

static int uart_mmio_getchar(void)
{
    if (!uart_mmio_rx_ready()) return EOF;
    return uart_reg_read(UART_DR) & 0xFF;
}
```

---

## Debug Output (from successful boot)

```
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
```

**Result:** `UART RX: ENABLED (device frame mapped)`

---

## Files Modified

| File | Changes |
|------|---------|
| `phase2/src/sel4/main_arm64.c` | Updated to Week 33, added `sel4_putchar()`, `sel4_puts()`, UART RX status print |
| `phase2/src/drivers/uart_pl011.c` | Added `uart_map_device_frame()`, `find_device_untyped()`, debug output, fixed vaddr to 0x5c0000 |
| `phase2/src/drivers/uart_pl011.h` | Updated header comments |
| `phase2/src/jarvis-sel4-cmake/CMakeLists.txt` | Updated to Week 33 |

---

## Build & Deploy

### Final Kernel
- **File:** `kernel8.img`
- **Size:** 1,573,152 bytes (1.5MB)
- **Built:** January 10, 2026 @ 11:58:13
- **Location:** D:\ (SD card)

### Build Commands
```bash
# Sync sources
wsl.exe -e bash -c 'JARVIS_ROOT=/mnt/c/Users/jluca/Documents/JARVIS_OS; \
  JARVIS_PROJECT=/home/itsme/sel4-workspace/projects/jarvis-sel4; \
  cp -r "$JARVIS_ROOT/phase1/src/cache" "$JARVIS_PROJECT/src/"; \
  cp -r "$JARVIS_ROOT/phase1/src/ipc" "$JARVIS_PROJECT/src/"; \
  cp -r "$JARVIS_ROOT/phase2/src/drivers" "$JARVIS_PROJECT/src/"; \
  cp -r "$JARVIS_ROOT/phase2/src/ipc" "$JARVIS_PROJECT/src/ipc_phase2"; \
  cp "$JARVIS_ROOT/phase2/src/sel4/main_arm64.c" "$JARVIS_PROJECT/src/main.c"; \
  cp "$JARVIS_ROOT/phase2/src/jarvis-sel4-cmake/CMakeLists.txt" "$JARVIS_PROJECT/"'

# Build
wsl.exe -e bash -c 'cd /home/itsme/sel4-workspace/rpi4_jarvis && ninja'

# Copy to firmware
wsl.exe -e bash -c 'cp /home/itsme/sel4-workspace/rpi4_jarvis/images/jarvis-sel4-image-arm-bcm2711 \
  /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/firmware/kernel8.img'

# Copy to SD card
powershell.exe -Command "Copy-Item 'phase2\firmware\kernel8.img' 'D:\kernel8.img' -Force"
```

---

## Boot Output (Success)

```
ELF-loader started on CPU: ARM Ltd. Cortex-A72 r0p3
...
Booting all finished, dropped to user space
[UART] uart_map_device_frame() starting
[UART] Got bootinfo
[UART] Untypeds: 0x000000000000004f
...
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

## Success Criteria

| Criterion | Target | Actual | Status |
|-----------|--------|--------|--------|
| UART TX working | seL4_DebugPutChar output visible | Full banner + logs visible | ✅ |
| UART RX enabled | Device frame mapped successfully | Mapped at 0x5c0000 | ✅ |
| Banner displayed | Full JARVIS banner prints | Week 33 banner shows | ✅ |
| Cache loaded | 258 patterns loaded | 258 patterns loaded | ✅ |
| IPC handler running | "Waiting for Python queries..." | Handler running | ✅ |

---

## Lessons Learned

### 1. VSpace Page Table Coverage
seL4 ARM64 requires intermediate page tables (PGD/PUD/PMD) before mapping pages. The rootserver's VSpace only covers addresses where it's loaded (`0x400000-0x5b9fff`). Mapping outside this range requires creating additional paging structures.

**Solution:** Map device frames within the existing VSpace range.

### 2. Device Untyped Discovery
The BCM2711 peripherals are at `0xFE000000` (not `0x3F000000` like Pi 3). The bootinfo contains device untypeds covering this range:
- `DevUT[5]: paddr=0xfe000000 size=0x1000000` - covers all BCM2711 peripherals including UART at `0xFE201000`

### 3. Debug Output Strategy
Using `seL4_DebugPutChar()` for early debug output is essential for diagnosing boot issues. Adding detailed hex output for addresses and error codes made the vaddr issue immediately obvious.

---

## Next Steps (Week 34)

1. **Test UART RX** - Send characters from PuTTY, verify reception
2. **Python IPC Client** - Connect `uart_ipc_client.py` via USB-UART
3. **Send test query** - Verify cache lookup returns response
4. **Measure latency** - Confirm 10-20ms round-trip time

---

## References

- [seL4 Memory Tutorial](https://docs.sel4.systems/Tutorials/memory.html)
- [seL4 Device Drivers](https://docs.sel4.systems/projects/sel4-tutorials/device-driver.html)
- [BCM2711 Peripherals](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf)
- Boot Debug Notes: `phase2/docs/BOOT_DEBUG_NOTES.md`
