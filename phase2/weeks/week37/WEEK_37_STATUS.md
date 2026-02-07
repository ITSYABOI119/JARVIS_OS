# Week 37 Status: Broadcom GENET Ethernet Driver - Part 1

**Status:** HARDWARE VERIFIED (February 7, 2026) - 6 PASS, 0 FAIL
**Effort:** ~8 hours
**Goal:** TX-only GENET Ethernet driver for BCM2711 Raspberry Pi 4

---

## Summary

Implemented a bare-metal TX-only Ethernet driver for the BCM2711 GENET v5 controller
on Raspberry Pi 4 running seL4. The GENET controller is at ARM physical address
0xFD580000 (separate from the 0xFE000000 main peripheral range) and connects to an
external BCM54213PE Gigabit PHY via RGMII.

## Achievements

1. **GENET MMIO mapping** - 6 contiguous 4KB pages at vaddr 0x604000-0x609FFF
   - Dedicated device untyped search (separate from UART's 0xFE device untyped)
   - Binary buddy skip to advance cursor from untyped base to GENET_BASE
   - Full register coverage: SYS, EXT, INTRL2, RBUF, UMAC, TDMA (0x0000-0x5FFF)

2. **UMAC initialization** - Reset, MIB clear, max frame length, port mode
   - RBUF flush sequence (matches Linux driver)
   - TDMA/RDMA disable during reset
   - EXT RGMII OOB configuration for external PHY

3. **PHY management via MDIO** - BCM54213PE at address 1
   - MDIO read/write via UMAC_MDIO_CMD register (offset 0xE14 from GENET base)
   - PHY ID detection, reset, auto-negotiation
   - Link status reporting

4. **TX DMA ring 16** (default ring, 256 descriptors)
   - 12-byte v5 descriptors in GENET hardware RAM (MMIO space)
   - Ring register configuration (read/write pointers, buf size, start/end addr)
   - TDMA enable with ring 16 buffer enable
   - SCB burst size configuration

5. **TX send** - Raw Ethernet frame transmission
   - Copy frame to DMA buffer (from dma_alloc pool, uncacheable)
   - Descriptor write with SOP|EOP|CRC_APPEND flags
   - Producer index doorbell
   - Consumer index poll for completion

6. **6-test driver suite** in main_arm64.c
   - init, read_rev, set_mac, phy_init, tx_ring_init, tx_send_arp
   - ARP broadcast test frame (WHO-HAS 10.0.0.1 from 10.0.0.2)

## New Files

| File | LOC | Purpose |
|------|-----|---------|
| `bcm_genet.h` | 253 | Register definitions, DMA descriptor format, API |
| `bcm_genet.c` | 520 | Full TX-only driver implementation |
| **Total** | **773** | |

## Modified Files

| File | Change |
|------|--------|
| `CMakeLists.txt` | Added `src/drivers/bcm_genet.c` |
| `main_arm64.c` | Added `#include bcm_genet.h` + 6-test GENET section |

## Architecture Notes

### Why Separate Device Untyped

The UART driver maps devices at 0xFE000000+ (GPIO, UART, System Timer, EMMC).
GENET at 0xFD580000 is in a different seL4 device untyped capability.
The UART driver's device mapper cursor is past 0xFE342000, making 0xFD580000
unreachable due to seL4's forward-only watermark constraint.

Solution: GENET driver implements its own `genet_find_device_untyped()` and
`genet_map_mmio()` with independent binary buddy skip.

### Virtual Address Layout (Updated)

```
0x5c0000 - UART PL011
0x5c1000 - GPIO
0x5c2000 - System Timer
0x5c3000 - EMMC
0x5c4000-0x603FFF - DMA pool (256KB)
0x604000-0x609FFF - GENET MMIO (6 pages)
```

### GENET Register Map

```
Offset   Size  Block
0x0000   4KB   SYS + GR_BRIDGE + EXT + INTRL2 + RBUF
0x0800   4KB   UMAC (including MDIO at +0x614)
0x2000   4KB   RDMA descriptors
0x2C00   4KB   RDMA ring registers
0x4000   4KB   TDMA descriptors (256 x 12 = 3072 bytes)
0x4C00   4KB   TDMA ring registers + global DMA control
```

### DMA Descriptor Format (GENET v5)

```
Word 0 (offset +0x00): length_status
  - Bits [31:16]: Buffer length
  - Bit 14: EOP (end of packet)
  - Bit 13: SOP (start of packet)
  - Bits [13:7]: QTAG (ring index)
  - Bit 6: TX_APPEND_CRC
  - Bit 5: TX_DO_CSUM

Word 1 (offset +0x04): address_lo (low 32 bits of buffer paddr)
Word 2 (offset +0x08): address_hi (high 32 bits, 0 on Pi 4)
```

## Dependencies

- `slot_alloc` - Capability slot allocation for device frame mapping
- `dma_alloc` - TX buffer allocation with known physical address
- `seL4_DebugPutChar` - Debug output during init

## Hardware Test Results

All 6 tests verified on Pi 4 bare metal (February 7, 2026):

| Test | Result | Details |
|------|--------|---------|
| init | PASS | MMIO mapping + UMAC reset + DMA alloc |
| read_rev | PASS | REV_CTRL = 0x06000000 (GENET v5) |
| set_mac | PASS | de:ad:be:ef:ca:fe programmed |
| phy_init | PASS | BCM54213PE detected (PHY ID 600d:84a2), auto-neg started, link DOWN (no cable) |
| tx_ring_init | PASS | Ring 16, 256 descs, TDMA enabled |
| tx_send_arp | PASS | 42 bytes ARP broadcast, DMA consumed descriptor |

### Critical Hardware Fix

After GENET PHY/UMAC register writes, `seL4_Untyped_Retype()` causes kernel halt (irq 27).
**Fix:** Pre-allocate DMA buffer BEFORE any GENET MMIO mapping or register writes in `genet_init()`.
This ensures all seL4 capability operations complete before touching GENET hardware.

## Known Limitations

- TX-only (no RX yet - planned for Week 38 Part 2)
- Single TX DMA buffer (no concurrent sends)
- No interrupt handling (polled TX completion)
- Link may not be up if no cable connected (PHY init still succeeds)
- No RX ring setup (RDMA disabled)

## Next Steps (Week 38)

1. RX DMA ring setup (ring 16 RDMA)
2. RX frame receive with interrupt or polling
3. Hardware test on Pi 4 with Ethernet cable
4. Full driver test suite expansion
5. Simple UDP echo or ping support

---

*Created: February 7, 2026*
