# Raspberry Pi 4 Platform Guide

**JARVIS AI-OS - Phase 2 Platform Reference**
**Hardware:** Raspberry Pi 4 Model B (BCM2711, Cortex-A72, 8GB RAM)
**Microkernel:** seL4 (formally verified, ARM64)
**Last Updated:** February 8, 2026 (Week 43)

---

## Table of Contents

1. [Hardware Overview](#hardware-overview)
2. [Driver Compatibility Matrix](#driver-compatibility-matrix)
3. [Physical Address Map](#physical-address-map)
4. [Virtual Address (VSpace) Layout](#virtual-address-vspace-layout)
5. [seL4 Device Mapping Rules](#sel4-device-mapping-rules)
6. [Boot Sequence](#boot-sequence)
7. [Performance Benchmarks](#performance-benchmarks)
8. [Known Limitations](#known-limitations)
9. [Hardware Requirements](#hardware-requirements)
10. [Wiring and Connectivity](#wiring-and-connectivity)
11. [Troubleshooting](#troubleshooting)

---

## Hardware Overview

The Raspberry Pi 4 Model B uses a Broadcom BCM2711 SoC with four Cortex-A72 cores
at 1.5 GHz. JARVIS AI-OS runs as a single seL4 rootserver process on core 0, with
all device drivers compiled into the rootserver binary.

### BCM2711 SoC Features Used

| Feature | Used | Notes |
|---------|------|-------|
| Cortex-A72 (core 0) | Yes | seL4 rootserver runs single-core |
| Cortex-A72 (cores 1-3) | No | Available for future multi-process |
| LPDDR4 (up to 8GB) | Yes | seL4 manages via untyped capabilities |
| PL011 UART0 | Yes | Primary IPC to host PC (115200 baud) |
| GPIO (58 pins) | Yes | Output, input, pull-up/down, alt functions |
| BSC1 I2C | Yes | 100/400 kHz master mode |
| EMMC2 (SD card) | Yes | Read/write, ADMA2 DMA, up to 50 MHz |
| GENET v5 Ethernet | Yes | 1 Gbps, TX+RX, ARP/ICMP stack |
| DWC2 USB (USB-C) | Yes | Host mode, HID boot protocol keyboard |
| System Timer | Yes | 1 MHz free-running counter |
| VideoCore GPU | No | Only used for initial boot (start4.elf) |
| VL805 xHCI (USB-A) | No | PCIe-attached, not supported |
| BCM43455 WiFi/BT | No | Requires SDIO + firmware blob |
| PCIe Gen 2.0 x1 | No | Not supported |
| HDMI (x2) | No | No display output |
| CSI/DSI | No | No camera/display interface |
| SPI | No | Not implemented |
| PWM | No | Not implemented |
| Watchdog Timer | No | Planned (Week 44) |
| Thermal Sensor | No | Planned (Week 44) |

---

## Driver Compatibility Matrix

### Active Drivers (Week 43)

| Driver | Source File | LOC | Paddr | VSpace | Status | Tests | Performance |
|--------|-----------|-----|-------|--------|--------|-------|-------------|
| PL011 UART | uart_pl011.c | 1244 | 0xFE201000 | 0x5c0000 | Production | 8 | 7ms IPC RTT |
| GPIO | bcm_gpio.c | 234 | 0xFE200000 | 0x5c1000 | New (W43) | 8 | <1us toggle |
| System Timer | bcm2711_timer.c | 67 | 0xFE003000 | 0x5c2000 | Production | - | 1 MHz counter |
| EMMC/SDHCI | emmc_sdhci.c | 1547 | 0xFE340000 | 0x5c3000 | Production | 12 | 11.9 MB/s ADMA2 |
| GENET Ethernet | bcm_genet.c | 1134 | 0xFD580000 | 0x604000 | Production | 11 | 1 Gbps link |
| Net Stack | net_stack.c | 606 | - | - | Production | 5 | ARP+ICMP |
| Net Commands | net_cmd.c | 315 | - | - | Production | 10 | ping/ifconfig |
| DWC2 USB HID | usb_hid.c | 1393 | 0xFE980000 | 0x60A000 | Production | 14 | Boot protocol |
| I2C (BSC1) | bcm_i2c.c | 404 | 0xFE804000 | auto | New (W43) | 3 | 100/400 kHz |

### Support Libraries

| Library | Source File | LOC | Purpose |
|---------|-----------|-----|---------|
| DMA Allocator | dma_alloc.c | 319 | Uncacheable DMA buffer pool |
| Slot Allocator | slot_alloc.c | 100 | seL4 capability slot management |
| Block Device | blk_dev.c | 84 | EMMC block abstraction |

### Header Files

| Header | LOC | Driver |
|--------|-----|--------|
| bcm_gpio.h | 109 | GPIO register defs + API |
| bcm_i2c.h | 133 | BSC1 I2C register defs + API |
| uart_pl011.h | - | UART + shared device mapper API |
| emmc_sdhci.h | - | EMMC API |
| bcm_genet.h | - | GENET register defs + API |
| net_stack.h | - | Protocol structs + API |
| net_cmd.h | - | Command dispatcher API |
| usb_hid.h | - | USB HID keyboard API |
| bcm2711_timer.h | - | Timer API |
| dma_alloc.h | - | DMA pool API |
| slot_alloc.h | - | Slot allocator API |
| blk_dev.h | - | Block device API |

### Total Driver Code

| Category | Files | LOC |
|----------|-------|-----|
| Core Drivers | 9 .c files | 7,262 |
| Headers | 12 .h files | ~800 |
| Support Libs | 3 .c files | 503 |
| **Total** | **24 files** | **~8,565** |

---

## Physical Address Map

### BCM2711 Peripheral Addresses

```
 Address Range          Size    Peripheral          Driver
 ──────────────────────────────────────────────────────────
 0xFC000000-0xFD7FFFFF  24MB    GENET Device Untyped (separate!)
   0xFD580000           4KB     GENET v5 Ethernet   bcm_genet.c

 0xFE000000-0xFEFFFFFF  16MB    Main Peripheral Untyped
   0xFE003000           4KB     System Timer        bcm2711_timer.c
   0xFE200000           4KB     GPIO                bcm_gpio.c
   0xFE201000           4KB     PL011 UART0         uart_pl011.c
   0xFE340000           4KB     EMMC2/SDHCI         emmc_sdhci.c
   0xFE804000           4KB     BSC1 I2C            bcm_i2c.c
   0xFE980000           12KB    DWC2 USB OTG        usb_hid.c
```

### Device Untyped Split

The BCM2711 peripherals span TWO separate seL4 device untypeds:

1. **GENET Untyped** at 0xFC000000 (covers 0xFD580000 GENET)
   - Requires separate buddy-skip sequence to reach 0xFD580000
   - 6 pages mapped at vaddr 0x604000-0x609FFF

2. **Main Peripheral Untyped** at 0xFE000000 (covers everything else)
   - Timer, GPIO, UART, EMMC, I2C, USB all share this untyped
   - Forward-only watermark: must map in ascending paddr order

---

## Virtual Address (VSpace) Layout

```
 VSpace Address    Size     Mapping              Notes
 ─────────────────────────────────────────────────────────
 0x400000          ~1.7MB   Code + Data          seL4 rootserver ELF
 0x5c0000          4KB      UART PL011           Hardcoded, maps paddr 0xFE201000
 0x5c1000          4KB      GPIO                 Hardcoded, maps paddr 0xFE200000
 0x5c2000          4KB      System Timer         Auto-assigned by uart_device_map_page()
 0x5c3000          4KB      EMMC/SDHCI           Auto-assigned by uart_device_map_page()
 0x5c4000          256KB    DMA Buffer Pool      Uncacheable (vm_attributes=0)
 0x603FFF          -        End of DMA pool      -
 0x604000          24KB     GENET MMIO           6 pages from GENET device untyped
 0x609FFF          -        End of GENET         -
 0x60A000          12KB     DWC2 USB MMIO        3 pages from main device untyped
 0x60CFFF          -        End of USB           -
 0x60D000+         -        Available            For future device mappings
```

### Device Mapping Cursor State (after all drivers init)

| Untyped | Current Watermark | Next Available |
|---------|-------------------|----------------|
| Main (0xFE000000) | ~0xFE983000 (after USB) | 0xFE983000+ |
| GENET (0xFC000000) | ~0xFD586000 (after GENET) | 0xFD586000+ |

### I2C VSpace Address

The I2C BSC1 controller at paddr 0xFE804000 is mapped via `uart_device_map_page()`,
which auto-assigns the next available VSpace address after the device cursor.
Since 0xFE804000 falls between EMMC (0xFE340000) and USB (0xFE980000) in the
main peripheral untyped, it must be initialized BEFORE USB to respect the
forward-only watermark constraint.

**Init order:** Timer -> UART/GPIO -> EMMC -> I2C -> USB

---

## seL4 Device Mapping Rules

### Forward-Only Watermark

seL4's `Untyped_Retype` operation advances a watermark that can NEVER move backward.
All device frame mappings within a single device untyped MUST be done in ascending
physical address order.

**Correct order (main peripheral untyped):**
```
Timer    (0xFE003000)  -- mapped first
GPIO     (0xFE200000)  -- mapped second (by UART init)
UART     (0xFE201000)  -- mapped third (by UART init)
EMMC     (0xFE340000)  -- mapped fourth
I2C BSC1 (0xFE804000)  -- mapped fifth (NEW Week 43)
USB DWC2 (0xFE980000)  -- mapped sixth
```

### Binary Buddy Skip

To advance the watermark from one device address to the next (skipping unused
address space), the system performs power-of-2 sized retypes. For example, to
skip from 0xFE004000 to 0xFE200000:

```
Skip 1: 16KB   (0xFE004000 -> 0xFE008000)
Skip 2: 32KB   (0xFE008000 -> 0xFE010000)
Skip 3: 64KB   (0xFE010000 -> 0xFE020000)
Skip 4: 128KB  (0xFE020000 -> 0xFE040000)
Skip 5: 256KB  (0xFE040000 -> 0xFE080000)
Skip 6: 512KB  (0xFE080000 -> 0xFE100000)
Skip 7: 1MB    (0xFE100000 -> 0xFE200000)
```

### DMA Buffer Rules

seL4 does NOT set `SCTLR_EL1.UCI`, so ALL cache maintenance instructions
(`DC IVAC/CIVAC/CVAC`) trap from EL0. DMA buffers MUST be mapped with
`vm_attributes = 0` (uncacheable) instead of using software cache management.

### Shared Device Mapper

All drivers (except GENET, which has its own untyped) use the shared device
mapper in `uart_pl011.c`:

- `uart_device_map_page(paddr, vaddr_hint, &mapped_ptr)` - Map a 4KB device page
- `uart_get_gpio_vaddr()` - Get the already-mapped GPIO base address

The GPIO driver does NOT map its own pages. It calls `uart_get_gpio_vaddr()`
to get the GPIO MMIO base that was already mapped during UART initialization.

---

## Boot Sequence

### Power-On to Shell Prompt

```
Step  Time     Component        Action
────  ───────  ───────────────  ──────────────────────────────────
 1    0ms      GPU (VideoCore)  Load start4.elf from SD FAT32
 2    ~50ms    GPU              Read config.txt, load u-boot.bin
 3    ~80ms    U-Boot           Initialize DRAM, scan for boot.scr
 4    ~100ms   U-Boot           Execute boot.scr (load kernel8.img)
 5    ~180ms   seL4 elfloader   Decompress kernel, jump to entry
 6    ~200ms   seL4 kernel      Initialize, create rootserver
 7    ~250ms   JARVIS rootserver Timer init (0xFE003000)
 8    ~260ms   JARVIS rootserver UART init (TX+RX, GPIO mapping)
 9    ~280ms   JARVIS rootserver Decision cache load (258 patterns)
10    ~300ms   JARVIS rootserver EMMC init (card detect, 50MHz)
11    ~350ms   JARVIS rootserver GPIO init (reuse UART mapping)
12    ~360ms   JARVIS rootserver I2C init (BSC1 at 0xFE804000)
13    ~400ms   JARVIS rootserver GENET init (6 pages, PHY link)
14    ~600ms   JARVIS rootserver USB HID init (DWC2, enumeration)
15    ~800ms   JARVIS rootserver DMA pool alloc (256KB uncacheable)
16    ~1s      JARVIS rootserver Networking stack init
17    ~2s      JARVIS rootserver IPC main loop (UART poll + USB poll)
```

Total boot time: approximately 2 seconds from power-on to interactive shell.

### Boot Files (SD Card FAT32 Partition)

| File | Size | Purpose |
|------|------|---------|
| start4.elf | 2.2 MB | VideoCore GPU firmware |
| fixup4.dat | 5.5 KB | GPU memory configuration |
| config.txt | 120 B | Boot settings (arm_64bit=1, enable_uart=1) |
| u-boot.bin | 717 KB | U-Boot 2026.01 bootloader |
| boot.scr | 356 B | U-Boot auto-boot script |
| bcm2711-rpi-4-b.dtb | 56 KB | Device tree blob |
| kernel8.img | ~1.7 MB | JARVIS seL4 rootserver image |

---

## Performance Benchmarks

### IPC Performance

| Metric | Value | Notes |
|--------|-------|-------|
| UART IPC RTT (median) | 7.09 ms | 500-query benchmark |
| UART IPC RTT (p95) | 8.19 ms | Consistent under load |
| UART IPC RTT (p99) | 513.82 ms | Includes retry overhead |
| Cache hit rate | 85.7% | 258 pre-compiled patterns |
| Cache lookup time | <1 ms | In-memory hash lookup |
| IPC success rate | 100% | 500/500 queries (0 timeouts) |

### Storage Performance

| Metric | Value | Notes |
|--------|-------|-------|
| EMMC ADMA2 read | 11.9 MB/s | Multi-block sequential |
| EMMC PIO read | ~2 MB/s | Single-block fallback |
| EMMC PIO write | ~1.5 MB/s | Single-block, verified |

### Network Performance

| Metric | Value | Notes |
|--------|-------|-------|
| GENET link speed | 1000 Mbps FD | Auto-negotiated |
| ARP resolve | <10 ms | With timeout |
| ICMP ping RTT | <1 ms | Local network |
| TX frame send | <10 us | Single frame |
| RX frame receive | Polled | No interrupt-driven RX |

### I2C Performance

| Metric | Value | Notes |
|--------|-------|-------|
| Standard mode | 100 kHz | DIV=1500 (core_clk/150MHz) |
| Fast mode | 400 kHz | DIV=375 (configurable) |
| Bus scan (0x03-0x77) | ~600 ms | 117 addresses, 5ms timeout each |

---

## Known Limitations

### Not Supported

| Feature | Reason |
|---------|--------|
| USB-A ports | VL805 xHCI controller is PCIe-attached; no PCIe driver |
| WiFi/Bluetooth | BCM43455 requires SDIO interface + proprietary firmware blob |
| HDMI output | No display driver; all interaction via serial console |
| SPI | Not implemented; no current use case |
| PWM | Not implemented |
| PCIe | VL805 and NVMe not accessible |
| Multi-process | Single rootserver process; no multi-tasking |
| Interrupts | All I/O is polled; no IRQ-driven drivers |
| File system | Raw block access only; no FAT32/ext4 read/write from seL4 |
| Audio | No HDMI or 3.5mm audio driver |

### Architectural Constraints

| Constraint | Impact |
|------------|--------|
| Forward-only watermark | Drivers must init in ascending paddr order |
| No cache maintenance from EL0 | DMA buffers must be uncacheable |
| Single VSpace | All device mappings share one address space |
| No dynamic loading | All drivers compiled into rootserver |
| 240-byte IPC payload | Max UART frame payload size |

### Known Issues

| Issue | Severity | Workaround |
|-------|----------|------------|
| USB DWC2 enumeration requires reset delay | Low | 200ms delay in init |
| GENET RX index wraps at 65536 | Low | Masked with DMA_INDEX_MASK |
| RBUF prepends 66 bytes to RX frames | Low | Offset calculation in rx_recv |
| EMMC DATA_TIMEOUT during writes | Low | Write-specific interrupt handler |
| I2C scan slow (117 addresses) | Low | 5ms timeout per address |

---

## Hardware Requirements

### Minimum Configuration

- Raspberry Pi 4 Model B (4GB or 8GB)
- MicroSD card (8GB+ Class 10)
- USB-to-UART serial adapter (CP2102 or FTDI)
- 3x Dupont jumper wires (TX, RX, GND)
- 5V 3A USB-C power supply

### Recommended Configuration

- Raspberry Pi 4 Model B **8GB**
- MicroSD card 32GB+ (UHS-I recommended for ADMA2)
- FTDI FT232R USB-UART adapter (most reliable)
- Ethernet cable + router (for networking features)
- USB keyboard (for direct shell input)
- I2C peripherals: sensors, EEPROM, RTC (optional)

### BCM2711 SoC Specifications

| Spec | Value |
|------|-------|
| CPU | Quad Cortex-A72 @ 1.5 GHz |
| Architecture | ARMv8-A (AArch64) |
| L1 Cache | 48KB I-cache, 32KB D-cache per core |
| L2 Cache | 1MB shared |
| RAM | LPDDR4-3200, up to 8GB |
| Ethernet | Broadcom GENET v5 + BCM54213PE PHY |
| USB | 1x DWC2 OTG (USB-C), 4x VL805 xHCI (USB-A) |
| SD | EMMC2 controller (up to SDR104) |
| GPIO | 58 pins, 3.3V logic |
| I2C | 7x BSC controllers (BSC1 used) |
| UART | 2x PL011, 4x mini-UART |

---

## Wiring and Connectivity

### UART Serial Connection

```
  Pi 4 GPIO Header          USB-UART Adapter
  ──────────────────         ─────────────────
  Pin 8  (GPIO14/TXD) ───── RXD
  Pin 10 (GPIO15/RXD) ───── TXD
  Pin 6  (GND)        ───── GND
```

Settings: 115200 baud, 8 data bits, no parity, 1 stop bit (8N1).

### I2C Connection (BSC1)

```
  Pi 4 GPIO Header          I2C Device
  ──────────────────         ──────────
  Pin 3  (GPIO2/SDA1) ───── SDA
  Pin 5  (GPIO3/SCL1) ───── SCL
  Pin 1  (3.3V)       ───── VCC (if 3.3V device)
  Pin 6  (GND)        ───── GND
```

Internal pull-ups enabled by the GPIO driver (GPIO_PULL_UP on pins 2 and 3).

### Ethernet

Standard RJ45 to router/switch. Auto-negotiates 10/100/1000 Mbps.

### USB Keyboard

Connect to the USB-C port (not USB-A ports). DWC2 host mode supports
HID boot protocol keyboards only.

---

## Troubleshooting

### No Serial Output

1. Check wiring: Pi TX (pin 8) goes to adapter RX, Pi RX (pin 10) goes to adapter TX
2. Verify baud rate is 115200
3. Ensure config.txt has `enable_uart=1`
4. Try different USB-UART adapter (CP2102 recommended)

### Boot Hangs at U-Boot

1. Press any key during 3-second countdown for interactive shell
2. Check SD card has all 7 boot files
3. Verify kernel8.img is a valid ELF (file size ~1.5-2 MB)

### EMMC Read/Write Fails

1. Card must be SDHC or SDXC (not old SD)
2. Check card is properly seated
3. Try different SD card (some cards have compatibility issues)

### I2C No Devices Found

1. Verify wiring (SDA to SDA, SCL to SCL, common GND)
2. Check device power (3.3V for most I2C sensors)
3. Some devices need external pull-ups (4.7K to 3.3V)
4. Verify device I2C address (run `i2c` shell command for bus scan)

### Network Not Working

1. Check Ethernet cable connection
2. Verify link status: run `ifconfig` shell command
3. GENET requires auto-negotiation (~2s after cable connect)
4. Verify IP configuration matches network (default: 10.0.0.2)

---

*Generated: February 8, 2026*
*JARVIS AI-OS Phase 2, Week 43*
