# JARVIS Pi 4 Boot Debug Notes

## Current Issue: No Output After Kernel Load

**Symptoms:**
- GPU firmware loads successfully ✅
- Kernel loads successfully: "Loaded 'kernel8.img' to 0x200000 size 0xb4808" ✅
- Kernel relocated: "Kernel relocated to 0x80000" ✅
- UART baud rate set: "uart: Baud rate change done..." ✅
- **Then nothing - no JARVIS output** ❌

## Root Cause Analysis

### Problem: Direct UART Access Fails in seL4 User Space

The code attempts direct memory access to UART at `0xFE201000`:
```c
#define UART_DR (*((volatile uint32_t *)0xFE201000))
```

**Why this fails:**
- seL4 kernel runs in privileged mode and can access hardware
- Rootserver runs in **user space** (unprivileged)
- User space cannot directly access peripheral memory addresses
- Direct access causes **VM fault** (data abort)

### Why elfloader Output Works

The elfloader runs **before** seL4 kernel takes over:
- Runs in privileged mode (before kernel initialization)
- Can access hardware directly
- Output stops when kernel drops to user space

## Solutions to Try

### Option 1: Enable Identity Mapping (if supported)

Check if seL4 can create identity mappings for peripherals:
```cmake
# In settings.cmake or kernel config
set(KernelArmIdentityMapping ON CACHE BOOL "" FORCE)
```

### Option 2: Use seL4 Serial Output Functions

Check if seL4 provides serial output:
```c
#include <sel4/sel4.h>
// Check for sel4_putchar() or similar
```

### Option 3: Map UART with Capabilities

Use proper seL4 memory mapping:
```c
#include <platsupport/plat/serial.h>
#include <platsupport/io.h>

ps_io_ops_t io_ops;
// Initialize io_ops
void *uart_base = ps_io_map(&io_ops, UART0_BASE, 0x1000, ...);
```

### Option 4: Check if Rootserver is Starting

Add a simple test that doesn't require UART:
```c
int main() {
    // Infinite loop - if system hangs, we know main() was called
    while(1) {
        // Do nothing - just verify execution
    }
}
```

## Next Steps

1. Check seL4 kernel configuration for identity mapping
2. Try using seL4's serial output functions
3. Implement proper UART memory mapping with capabilities
4. Verify rootserver is actually starting (add simple test)

## References

- seL4 Memory Management: https://docs.sel4.systems/Tutorials/memory.html
- seL4 Platform Support: https://docs.sel4.systems/projects/sel4runtime/
- TII seL4 Build System: https://github.com/tiiuae/tii_sel4_build


