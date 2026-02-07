# Week 40 Status: USB HID Driver Part 1

**Status:** HARDWARE VERIFIED (February 8, 2026)
**Goal:** DWC2 USB host controller init + USB enumeration + HID boot protocol keyboard

---

## Summary

Implemented USB HID keyboard driver using the BCM2711 DWC2 (DesignWare USB 2.0 OTG)
controller in host mode. Supports USB enumeration (device descriptor, set configuration)
and HID boot protocol for keyboard input (8-byte reports, scan code to ASCII).

## Achievements

1. **DWC2 Host Controller Init** (usb_hid.c)
   - Register mapping via seL4 device frame at 0xFE980000
   - 3 MMIO pages: core/host registers + 2 channel data FIFOs
   - Core soft reset, PHY configuration, force host mode
   - FIFO sizing (RX + non-periodic TX + periodic TX)
   - Port power-on and device reset
   - Slave mode (no DMA) - data through FIFO registers

2. **USB Enumeration** (usb_hid.c)
   - Control transfers via DWC2 host channels (channel 0)
   - GET_DESCRIPTOR (device + configuration)
   - SET_ADDRESS, SET_CONFIGURATION
   - HID interface detection (class=3, subclass=1, protocol=1)

3. **HID Boot Protocol Keyboard** (usb_hid.c)
   - SET_PROTOCOL(0) for boot protocol mode
   - Interrupt IN polling via channel 1 for 8-byte keyboard reports
   - Scan code to ASCII conversion (full printable + shifted)

## New Files

| File | LOC | Purpose |
|------|-----|---------|
| usb_hid.h | 403 | DWC2 registers, USB descriptors, HID API |
| usb_hid.c | 1399 | DWC2 init, enumeration, HID keyboard |

## Modified Files

| File | Change |
|------|--------|
| CMakeLists.txt | Added usb_hid.c |
| main_arm64.c | USB HID include, init call + 6 Week 40 tests |

## VSpace Address Layout

```
0x60A000-0x60CFFF - DWC2 USB (3 pages, paddr 0xFE980000)
  Page 0: Core + Host registers (0x000-0xFFF)
  Page 1: Channel 0 Data FIFO (0x1000-0x1FFF)
  Page 2: Channel 1 Data FIFO (0x2000-0x2FFF)
```

## Test Plan

| # | Test | Type | USB needed? | Expected |
|---|------|------|-------------|----------|
| 1 | dwc2_reg_access | Unit | No | PASS or SKIP |
| 2 | dwc2_core_init | Unit | No | PASS or SKIP |
| 3 | port_status | Unit | No | PASS (UP or DOWN) |
| 4 | usb_enumeration | Hardware | Yes | PASS or SKIP |
| 5 | scancode_table | Unit | No | PASS |
| 6 | hid_poll | Hardware | Yes | PASS or SKIP |

## Hardware Test Results

```
Week 40 Tests: 4 PASS, 0 FAIL, 2 SKIP
  dwc2_reg_access=PASS  dwc2_core_init=PASS  port_status=PASS
  usb_enumeration=SKIP (no device)  scancode_table=PASS
  hid_poll=SKIP (no keyboard)

Total: 36 PASS, 0 FAIL, 3 SKIP (12 EMMC + 6 TX + 5 RX/Net + 9 Cmd + 4 USB)
```

### Hardware Observations
- GSNPSID=0x4f54280a confirms DWC2 OTG core (Synopsys)
- Core reset, host mode, FIFO flush, channel halt all succeeded
- HPRT0=0x00001000 (port powered, no device) — expected without USB-C OTG adapter
- Tests 4,6 SKIP gracefully when no USB keyboard connected

### Bug Fixed During Testing
- Initial build used custom buddy skip from untyped base (0xFE000000), but UART/EMMC
  had already advanced the watermark to ~0xFE341000. Retype failed with "Insufficient
  memory". Fixed by switching to `uart_device_map_page()` (shared device mapper with
  correct cursor tracking).

## Notes

- DWC2 on Pi 4 connects to USB-C port (USB-A ports use VL805 xHCI)
- Keyboard requires USB OTG adapter on USB-C port
- Tests 1-3,5 pass without hardware; tests 4,6 need USB keyboard
- DWC2 at 0xFE980000 uses shared device mapper (same untyped as UART/EMMC)
- Slave mode chosen to avoid DMA buffer allocation complexity

---

*Created: February 7, 2026*
*Hardware verified: February 8, 2026*
