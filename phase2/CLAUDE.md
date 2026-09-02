# CLAUDE.md - Phase 2 (Raspberry Pi 4 / BCM2711 / seL4 ARM64) - COMPLETE

Moved here verbatim from the root `CLAUDE.md` on 2026-09-02 (`/doctor`): this file loads only when a session works under `phase2/`. Phase 2 is COMPLETE (all 87 milestones, the 30-day stability run, the security audits); the root file keeps the phase table, the rules and the metrics. The consolidated lessons also live in the auto-memory file `phase2-arm64-lessons.md`.

## Build & Test Commands

### Phase 2: seL4 Pi 4 Build

```bash
# In WSL - build JARVIS kernel for Pi 4
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/scripts
tr -d '\r' < build_and_copy_kernel.sh | bash

# Manual build steps
cd ~/sel4-workspace/rpi4_jarvis
cmake -G Ninja \
    -DCROSS_COMPILER_PREFIX=aarch64-linux-gnu- \
    -DKernelPlatform=bcm2711 \
    -DKernelSel4Arch=aarch64 \
    -DKernelArmPlatform=bcm2711 \
    ../projects/jarvis-sel4
ninja

# Copy to firmware directory
cp images/kernel8.img /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/firmware/

# Deploy to SD card
copy phase2\firmware\kernel8.img D:\
```

### Phase 2: Test Commands

```bash
# Python tests
wsl python3 phase2/src/ai/test_uart_ipc_client.py      # 22 tests - UART protocol
wsl python3 phase2/src/ai/test_system_bootstrap.py     # 25 tests - Bootstrap
wsl python3 phase2/src/ai/test_integration.py          # 10 tests - Integration

# C tests
wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/ipc && \
  gcc -O2 -I../../../phase1/src/cache -I../../../phase1/src/ipc \
  dual_ring_buffer.c test_dual_ring.c ../../../phase1/src/ipc/ring_buffer.c \
  -o test_dual_ring && ./test_dual_ring"                # 12 tests

wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/ipc && \
  gcc -O2 -I../../../phase1/src/cache -I../../../phase1/src/ipc \
  ipc_handler.c dual_ring_buffer.c ../../../phase1/src/ipc/ring_buffer.c \
  ../../../phase1/src/cache/decision_cache.c ../../../phase1/src/cache/cache_patterns.c \
  test_ipc_handler.c -o test_ipc_handler && ./test_ipc_handler"  # 10 tests

wsl bash -c "cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase2/src/drivers && \
  gcc -O2 test_uart_logic.c -o test_uart_logic && ./test_uart_logic"  # 8 tests
```

## Architecture

### Phase 2 Split Deployment (COMPLETE)

```
┌──────────────────┐       UART        ┌──────────────────┐
│   PC (Host)      │◄─────────────────►│   Pi 4 (seL4)    │
│                  │   115200 baud     │                  │
│  Python AI       │                   │  Decision Cache  │
│  - Phi-3 Mini    │   7ms median RTT  │  - 258 patterns  │
│  - Llama 3.2 1B  │                   │  - 85.7% hit     │
│  - SHIELD        │                   │  - <1ms lookup   │
└──────────────────┘                   └──────────────────┘
```

- **Cache hits (85%):** seL4 decision cache answers in <1ms
- **Cache misses (15%):** Forwarded to PC via UART (7ms RTT)
- **No Python on Pi 4:** seL4 userspace is C-only
- Phase 3+ returns to standalone (x86 or Multi-Pi cluster)

## Key Technical Notes

### UART IPC Protocol

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  SYNC    │  TYPE    │   SEQ    │  LENGTH  │  FLAGS   │ PAYLOAD  │  CRC16   │
│ (2 bytes)│ (1 byte) │ (2 bytes)│ (2 bytes)│ (1 byte) │ (0-240)  │ (2 bytes)│
│  0xAA55  │  0x01-0E │  0-65535 │  0-240   │  0x00    │  data    │ CRC-CCITT│
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
```

| Type | Value | Direction | Description |
|------|-------|-----------|-------------|
| QUERY | 0x01 | Py→seL4 | Cache lookup |
| RESPONSE | 0x02 | seL4→Py | Cache result |
| HEARTBEAT | 0x03 | Both | Keep-alive |
| HEARTBEAT_ACK | 0x04 | Both | Keep-alive ack |
| STATS_REQUEST | 0x05 | Py→seL4 | Cache stats req |
| STATS_RESPONSE | 0x06 | seL4→Py | Cache stats |
| COMMAND | 0x07 | Py→seL4 | Shell command |
| COMMAND_RESULT | 0x08 | seL4→Py | Command output |
| SHIELD_CHECK | 0x09 | Py→seL4 | Risk assessment |
| SHIELD_RESULT | 0x0A | seL4→Py | Risk decision |
| ERROR | 0x0B | Both | Error |
| RESET | 0x0C | Both | Protocol reset |
| STATE_CHANGE | 0x0D | Py→seL4 | State change |
| STATE_ACK | 0x0E | seL4→Py | State ack |

Baud: 115200. RTT: 7ms measured. Heartbeat: 5s. Timeout: 30s.
Full spec: `phase2/docs/UART_IPC_PROTOCOL.md`

### BCM2711 Hardware

```
GENET Ethernet:  0xFD580000  (separate device untyped!)
Peripheral Base: 0xFE000000
System Timer:    0xFE003000  (1 MHz free-running counter)
GPIO:            0xFE200000
UART0 (PL011):   0xFE201000
EMMC/SDHCI:      0xFE340000
BSC1 I2C:        0xFE804000  (I2C master, 100/400 kHz)
DWC2 USB:        0xFE980000  (USB OTG host controller)
```

USB-Serial wiring: GPIO14(TXD)→RXD, GPIO15(RXD)←TXD, GND─GND

### SD Card Boot (U-Boot)

```
Boot Partition (FAT32):
├── start4.elf      # GPU firmware
├── fixup4.dat      # Memory config
├── u-boot.bin      # U-Boot 2026.01 bootloader
├── boot.scr        # U-Boot boot script
├── kernel8.img     # JARVIS seL4 boot image
├── bcm2711-rpi-4-b.dtb  # Device tree
└── config.txt      # arm_64bit=1, kernel=u-boot.bin, enable_uart=1

Boot flow: GPU firmware → U-Boot → boot.scr → kernel8.img → seL4
```

U-Boot: Press key during 3s countdown for interactive shell. Auto-boot loads kernel8.img at 0x00080000.
Backup: `temp_sd_backup/uboot_working/`

### seL4 Device Mapping Rules

- **Forward-only cursor:** seL4 Untyped_Retype watermark only moves forward. Map devices in ascending paddr order.
- **VSpace range:** vaddr must be within mapped VSpace (0x400000-0x5b9fff). Error 6 = seL4_FailedLookup means vaddr not in VSpace.
- **Init order:** systimer(0xFE003000) → DMA(0xFE007000) → mailbox(0xFE00B000) → watchdog(0xFE100000) → RNG(0xFE104000) → UART/GPIO(0xFE200000-0xFE201000) → SPI(0xFE204000) → PWM(0xFE20C000) → EMMC(0xFE340000) → I2C(0xFE804000) → USB(0xFE980000)
- **Binary buddy skip:** When timer is mapped before UART, use power-of-2 Untyped retypes to advance watermark from 0xFE004000→0xFE200000 (7 retypes: 16KB→32KB→64KB→128KB→256KB→512KB→1MB) instead of 2MB LargePage skip (which would consume GPIO's frame).
- **Device cursor after mapping:** GPIO_BASE + 2*4KB = 0xFE202000
- **DMA = uncacheable:** seL4 does NOT set `SCTLR_EL1.UCI`, so ALL cache maintenance instructions (`DC IVAC/CIVAC/CVAC`) trap from EL0. Map DMA buffers with `vm_attributes = 0` (uncacheable) instead.

### Virtual Address Layout

```
0x5c0000 - UART PL011 (hardcoded)
0x5c1000 - GPIO (hardcoded)
0x5c2000 - System Timer (auto-assigned)
0x5c3000 - EMMC (auto-assigned)
0x5c4000-0x603FFF - DMA pool (256KB)
0x604000-0x609FFF - GENET MMIO (6 pages, own device untyped)
0x610000 - VideoCore Mailbox (explicit vaddr, maps 0xFE00B000)
0x611000 - PM Watchdog (explicit vaddr, maps 0xFE100000)
0x612000 - DMA Engine (explicit vaddr, maps 0xFE007000)
RNG, SPI, PWM - auto-assigned (0xFE104000, 0xFE204000, 0xFE20C000)
0x60A000-0x60CFFF - DWC2 USB (3 pages, paddr 0xFE980000)
```

### Phase 1 IPC Limitation

Phase 1 used "mock IPC" - Python and seL4 did NOT communicate in real-time. Separate memory spaces. Both proven independently, connected in Phase 2 via UART.

## Quick Reference (Phase 2)

- **Build:** `wsl -e bash -lc "cd .../phase2/scripts && tr -d '\r' < build_and_copy_kernel.sh | bash"`
- **Deploy:** `copy phase2\firmware\kernel8.img D:\`
- **Serial:** 115200 baud, Pi 4 GPIO14/15
- **Main source:** `phase2/src/sel4/main_arm64.c`
- **UART driver:** `phase2/src/drivers/uart_pl011.c`
- **EMMC driver:** `phase2/src/drivers/emmc_sdhci.c`
- **Timer driver:** `phase2/src/drivers/bcm2711_timer.c`
- **Slot allocator:** `phase2/src/drivers/slot_alloc.c`
- **DMA allocator:** `phase2/src/drivers/dma_alloc.c`
- **Block device:** `phase2/src/drivers/blk_dev.c`
- **GENET Ethernet:** `phase2/src/drivers/bcm_genet.c`
- **Net Commands:** `phase2/src/drivers/net_cmd.c`
- **USB HID Keyboard:** `phase2/src/drivers/usb_hid.c`
- **GPIO Driver:** `phase2/src/drivers/bcm_gpio.c`
- **I2C Driver:** `phase2/src/drivers/bcm_i2c.c`
- **Watchdog driver:** `phase2/src/drivers/bcm_watchdog.c`
- **Thermal driver:** `phase2/src/drivers/bcm_thermal.c`
- **Power manager:** `phase2/src/ai/power_manager.py`
- **FDT parser:** `phase2/src/boot/fdt_parser.c`
- **Device tree source:** `phase2/src/boot/jarvis.dts`
- **Embedded DTB:** `phase2/src/boot/jarvis_dtb_data.h`
- **Boot manager:** `phase2/src/boot/boot_manager.c`
- **Warm reboot:** `phase2/src/boot/warm_reboot.c`
- **Power manager:** `phase2/src/drivers/bcm_power.c`
- **SPI driver:** `phase2/src/drivers/bcm_spi.c`
- **RNG driver:** `phase2/src/drivers/bcm_rng.c`
- **PWM driver:** `phase2/src/drivers/bcm_pwm.c`
- **DMA engine:** `phase2/src/drivers/bcm_dma.c`
- **Stability harness:** `phase2/src/ai/stability_harness.py`
- **Build config:** `phase2/src/jarvis-sel4-cmake/CMakeLists.txt`
- **SD Image Builder:** `phase2/scripts/build_installer_image.sh`
- **SD Flasher:** `phase2/scripts/flash_sd.sh`
- **Installer (Linux):** `phase2/scripts/install_jarvis.sh`
- **User Guide:** `phase2/docs/USER_GUIDE.md`
- **Tester Guide:** `phase2/docs/ALPHA_TESTER_GUIDE.md`
- **Platform Guide:** `phase2/docs/PI4_PLATFORM_GUIDE.md`
- **Security Self-Audit:** `phase2/docs/SECURITY_SELF_AUDIT.md`
- **Phase 2 Final Report:** `phase2/docs/PHASE_2_FINAL_REPORT.md`
