# seL4 Raspberry Pi 5 (BCM2712) Porting Research

**Date:** December 27, 2025
**Status:** Research/Documentation
**Purpose:** Evaluate feasibility and requirements for porting seL4 to Raspberry Pi 5

---

## Executive Summary

**Current Status:** seL4 does NOT support Raspberry Pi 5 (BCM2712)

The newest officially supported Raspberry Pi is the **Pi 4 (BCM2711)**. A Pi 5 port would require significant effort due to:
1. New SoC architecture (BCM2712 vs BCM2711)
2. RP1 "south bridge" chip requiring PCIe driver
3. Different memory map and peripheral addresses
4. No official BCM2712 datasheet from Broadcom

**Estimated Effort:** 4-8 weeks for experienced seL4 developer
**Complexity:** HIGH (more complex than Pi 3 → Pi 4 port)

---

## Hardware Comparison: Pi 4 vs Pi 5

| Aspect | Pi 4 (BCM2711) | Pi 5 (BCM2712) |
|--------|----------------|----------------|
| **CPU** | Cortex-A72 (4x 1.5GHz) | Cortex-A76 (4x 2.4GHz) |
| **Architecture** | ARMv8-A | ARMv8.2-A |
| **L2 Cache** | 1MB shared | 512KB per-core |
| **L3 Cache** | None | 2MB shared |
| **RAM** | Up to 8GB LPDDR4 | Up to 16GB LPDDR4X |
| **Interrupt Controller** | GIC-400 (GICv2) | GIC-400 (GICv2) |
| **I/O Architecture** | Direct SoC | RP1 via PCIe x4 |
| **Peripheral Base** | 0xFE000000 | 0x107c000000 (40-bit) |
| **seL4 Status** | Supported (unverified) | NOT SUPPORTED |

### Critical Architectural Difference: RP1 Chip

The Pi 5 introduces the **RP1 "south bridge"** chip:
- Contains most peripherals (GPIO, UART, USB, Ethernet, etc.)
- Connected to BCM2712 via PCIe 2.0 x4 interface
- Requires PCIe driver to access peripherals
- Memory mapped at 0x1F_0000_0000 → RP1 0x4000_0000

**Impact:** Cannot access GPIO/UART directly like Pi 4. Must:
1. Initialize PCIe root complex, OR
2. Use `pciex4_reset=0` in config.txt (leaves PCIe pre-configured)
3. Map RP1 address space through PCIe BAR

---

## seL4 Porting Requirements

Based on the [official seL4 porting guide](https://docs.sel4.systems/projects/sel4/porting.html):

### 1. Device Tree Source (DTS)

**Required:** Create `src/plat/rpi5/overlay-rpi5.dts`

Key information from Linux kernel [bcm2712.dtsi](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi):

```dts
// BCM2712 Key Addresses (40-bit addressing, 0x10 prefix)

// GIC-400 Interrupt Controller
gic: interrupt-controller@7fff9000 {
    compatible = "arm,gic-400";
    reg = <0x10 0x7fff9000 0x1000>,   // Distributor
          <0x10 0x7fffa000 0x2000>,   // CPU interface
          <0x10 0x7fffc000 0x2000>,   // Virtual interface
          <0x10 0x7fffe000 0x2000>;   // Virtual control
};

// System Timer (1MHz)
system_timer: timer@7c003000 {
    compatible = "brcm,bcm2835-system-timer";
    reg = <0x10 0x7c003000 0x1000>;
    clock-frequency = <1000000>;
    interrupts = <GIC_SPI 64 ...>;
};

// ARM Generic Timer (PPI interrupts)
timer {
    compatible = "arm,armv8-timer";
    interrupts = <GIC_PPI 13 ...>,  // Secure
                 <GIC_PPI 14 ...>,  // Non-secure
                 <GIC_PPI 11 ...>,  // Virtual
                 <GIC_PPI 10 ...>;  // Hypervisor
};

// UART (PL011 - on BCM2712, NOT RP1)
uart0: serial@7d001000 {
    compatible = "arm,pl011", "arm,primecell";
    reg = <0x10 0x7d001000 0x200>;
    interrupts = <GIC_SPI 121 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&clk_uart>;
    clock-frequency = <9216000>;
};

// Mailbox (for GPU communication)
mailbox: mailbox@7c013880 {
    compatible = "brcm,bcm2835-mbox";
    reg = <0x10 0x7c013880 0x40>;
    interrupts = <GIC_SPI 33 IRQ_TYPE_LEVEL_HIGH>;
};
```

### 2. Platform Configuration Files

**Create:** `kernel/src/plat/rpi5/config.cmake`

```cmake
# Based on rpi4 config, adapted for BCM2712

declare_platform(rpi5 KernelPlatformRpi5 KernelSel4ArchAarch64)

if(KernelPlatformRpi5)
    # BCM2712 uses GIC-400 like BCM2711
    config_set(KernelArmGicV2 ARM_GIC_V2 ON)

    # 40-bit physical addressing
    config_set(KernelArmPhysicalAddressBits ARM_PA_SIZE_BITS "40")

    # Memory configuration (variable RAM sizes)
    declare_option(RPI5_MEMORY VALID_STRINGS "4096;8192" DEFAULT "8192"
        DESCRIPTION "Amount of RAM in MB")

    # Timer configuration
    config_set(KernelArmPlatformCortexA76 ARM_CORTEX_A76 ON)

    # UART for debug output
    config_set(KernelDebugBuildPL011 DEBUG_BUILD_PL011 ON)
endif()
```

### 3. ELF-Loader Modifications

**Location:** `tools/elfloader-tool/`

The ELF-loader initializes hardware before seL4. For Pi 5:
- UART driver: PL011 already supported (same as Pi 4)
- May need 40-bit address handling adjustments
- Timer setup for Cortex-A76 (CNTHCTL_EL2 configuration at EL2→EL1 transition)

### 4. Kernel Driver Requirements

| Driver | Pi 4 Status | Pi 5 Notes |
|--------|-------------|------------|
| **GIC-400** | Supported | Same IP, should work |
| **ARM Generic Timer** | Supported | Cortex-A76 compatible |
| **PL011 UART** | Supported | Different base address |
| **System Timer** | Supported | BCM2835 compatible |
| **Mailbox** | Supported | Same protocol |

### 5. Memory Map

```
BCM2712 Physical Address Map (40-bit)

0x00_0000_0000 - 0x00_7FFF_FFFF  : SDRAM (up to 16GB)
0x10_7C00_0000 - 0x10_7CFF_FFFF  : BCM2712 Peripherals (16MB)
0x10_7D00_0000 - 0x10_7DFF_FFFF  : Additional Peripherals
0x10_7FFF_9000 - 0x10_7FFF_FFFF  : GIC-400
0x1F_0000_0000 - 0x1F_003F_FFFF  : RP1 (via PCIe BAR)

Key Peripheral Addresses:
- GIC Distributor:     0x10_7FFF_9000
- GIC CPU Interface:   0x10_7FFF_A000
- System Timer:        0x10_7C00_3000
- UART0 (PL011):       0x10_7D00_1000
- Mailbox:             0x10_7C01_3880
- Watchdog:            0x10_7D20_0000
```

---

## Key Challenges

### Challenge 1: 40-bit Physical Addressing

BCM2712 uses 40-bit physical addresses (0x10_ prefix for peripherals).
- seL4 ARM64 supports up to 48-bit PA
- May require `KernelArmPhysicalAddressBits` adjustment
- DTS overlay must correctly specify full 40-bit addresses

### Challenge 2: RP1 Peripheral Access

Most I/O peripherals are on RP1, requiring PCIe:
- **Option A:** Implement basic PCIe root complex driver (complex)
- **Option B:** Use `pciex4_reset=0` config.txt flag (simple, but fragile)
- **Option C:** Use only BCM2712-native peripherals (limited)

For minimal seL4 boot, Option C is viable (UART0 is on BCM2712, not RP1).

### Challenge 3: No Official BCM2712 Datasheet

Broadcom does not publish BCM2712 peripheral documentation.
- Must rely on Linux kernel DTS files
- Community reverse-engineering (forums)
- RP1 datasheet available: [rp1-peripherals.pdf](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf)

### Challenge 4: Timer Configuration at EL2

Cortex-A76 requires proper CNTHCTL_EL2 configuration:
- Must enable CNTP* register access before dropping to EL1
- seL4 ELF-loader must handle this

---

## Reference: Zephyr RTOS Pi 5 Support

Zephyr successfully supports Pi 5, providing a reference implementation:
- Board identifier: `rpi_5/bcm2712`
- Source: [Zephyr rpi_5](https://docs.zephyrproject.org/latest/boards/raspberrypi/rpi_5/doc/index.html)

Supported peripherals in Zephyr:
- ARM Cortex-A76 CPU
- ARM Architected Timer
- ARM PL011 UART
- BRCMSTB GPIO
- iProc RNG200

---

## Porting Strategy (Recommended)

### Phase 1: Minimal Boot (2-3 weeks)

1. **Fork seL4 kernel repository**
2. **Create platform files:**
   - `kernel/src/plat/rpi5/config.cmake`
   - `kernel/src/plat/rpi5/overlay-rpi5.dts`
3. **Verify existing drivers work:**
   - GIC-400 (same as Pi 4)
   - PL011 UART (new address)
   - ARM Generic Timer
4. **Boot seL4 in QEMU first** (if BCM2712 emulation available)
5. **Test on real hardware:**
   - config.txt: `arm_64bit=1`, `enable_uart=1`, `pciex4_reset=0`
   - Use USB-serial adapter on UART0 pins

### Phase 2: Full Platform Support (2-4 weeks)

1. **Add libplatsupport files:**
   - `util_libs/libplatsupport/plat_include/rpi5/`
   - Timer, serial, GPIO drivers
2. **Run seL4test suite**
3. **Document limitations** (no RP1 access = no GPIO/Ethernet/USB)

### Phase 3: RP1 Integration (4-8 weeks, optional)

1. **Implement PCIe root complex driver**
2. **Map RP1 peripheral space**
3. **Port RP1 GPIO/UART/USB drivers**
4. **Full hardware support**

---

## Boot Configuration (config.txt)

```ini
# Raspberry Pi 5 seL4 Boot Configuration

# Required: 64-bit mode
arm_64bit=1

# Required: Enable UART for serial debug
enable_uart=1
uart_2ndstage=1

# Required: Leave PCIe configured for RP1 access
pciex4_reset=0

# Kernel file (after building seL4)
kernel=kernel8.img

# Optional: Disable GPU features to simplify boot
gpu_mem=16
hdmi_safe=0
```

---

## File Structure for seL4 Pi 5 Port

```
seL4/
├── kernel/
│   └── src/
│       └── plat/
│           └── rpi5/
│               ├── config.cmake           # Platform configuration
│               ├── overlay-rpi5.dts       # Device tree overlay
│               └── linker.lds            # (if needed)
│
├── tools/
│   └── elfloader-tool/
│       └── src/
│           └── plat/
│               └── rpi5/
│                   └── sys_fns.c         # Platform init functions
│
└── util_libs/
    └── libplatsupport/
        ├── plat_include/
        │   └── rpi5/
        │       ├── clock.h
        │       ├── gpio.h
        │       ├── serial.h
        │       └── timer.h
        └── src/
            └── plat/
                └── rpi5/
                    ├── chardev.c
                    ├── ltimer.c
                    └── serial.c
```

---

## Key Resources

### Official Documentation
- [seL4 Porting Guide](https://docs.sel4.systems/projects/sel4/porting.html)
- [seL4 Supported Platforms](https://docs.sel4.systems/Hardware/)
- [seL4 Pi 4 Documentation](https://docs.sel4.systems/Hardware/Rpi4.html)
- [Raspberry Pi Processors](https://www.raspberrypi.com/documentation/computers/processors.html)

### Device Tree Sources
- [bcm2712.dtsi (Linux kernel)](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm64/boot/dts/broadcom/bcm2712.dtsi)
- [Zephyr rpi_5 board files](https://github.com/zephyrproject-rtos/zephyr/tree/main/boards/raspberrypi/rpi_5)

### Community Resources
- [Raspberry Pi Forums - BCM2712 bare metal](https://forums.raspberrypi.com/viewtopic.php?t=368402)
- [seL4 Discourse Forum](https://sel4.discourse.group/)
- [RP1 Peripherals Datasheet](https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf)

### ARM Documentation
- [ARM Cortex-A76 Optimization Guide](https://developer.arm.com/documentation/pjdoc466751330-7215/latest/)
- [ARM GIC-400 Technical Reference Manual](https://developer.arm.com/documentation/ddi0471/latest/)
- [ARM Debug Probe for Pi 5](https://developer.arm.com/documentation/ka006096/latest/)

---

## Conclusion

Porting seL4 to Raspberry Pi 5 is **feasible but non-trivial**. The main challenges are:

1. **40-bit addressing** - Manageable with DTS adjustments
2. **RP1 peripheral access** - Can be bypassed initially using BCM2712-native UART
3. **Missing datasheet** - Linux DTS provides sufficient information
4. **No community precedent** - First seL4 Pi 5 port would be novel work

**Recommendation for JARVIS Phase 2:**

Given that Phase 2 targets Raspberry Pi 4 with proven seL4 support, **defer Pi 5 porting** to Phase 3+. The Pi 4 port provides:
- Stable, documented seL4 platform
- Simpler peripheral access (no RP1)
- Known working UART IPC
- Lower development risk

If Pi 5 is required, allocate 4-8 weeks of dedicated platform porting work after gaining experience with Pi 4 bare-metal seL4 development.

---

**Document Version:** 1.0
**Last Updated:** December 27, 2025
**Author:** Claude Code (Research)
