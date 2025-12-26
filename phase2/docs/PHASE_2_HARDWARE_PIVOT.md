# Phase 2 Hardware Pivot: Intel NUC → Raspberry Pi 4

**Version:** 1.0
**Date:** December 2025
**Status:** APPROVED

---

## Executive Summary

Phase 2 hardware target is pivoting from Intel NUC ($1,200) to Raspberry Pi 4 8GB ($75).

**Decision:** Raspberry Pi 4 8GB selected as Phase 2 hardware platform.

**Key Benefits:**
- **$1,065 savings** (89% cost reduction)
- seL4 officially supports Pi 4 (BCM2711, Cortex-A72)
- ARM64 is seL4's native architecture (originally designed for ARM)
- Full bare metal control (no QEMU/tutorials framework limitations)
- Hardware already owned (no acquisition delay)

**Trade-offs Accepted:**
- Slower IPC (~1-10ms UART vs 54μs shared memory)
- Lower AI performance (TinyLlama 1.1B only, 5-15 tokens/sec)
- No NVMe storage (SD card instead)
- No PCIe bus (limits expansion)

---

## 1. Rationale for Pivot

### Budget Constraint
- Intel NUC 13 Pro: $1,200 (not budgeted)
- Raspberry Pi 4 8GB: Already owned ($0 additional)
- Phase 2 total hardware: $135 (SD card, power, cables) vs $5,000 (original plan)

### seL4 ARM Heritage
- seL4 was originally designed for ARM (L4 microkernel lineage)
- ARM64 verification is mature and production-ready
- Pi 4 (BCM2711) is an officially supported platform
- Better documentation and community support for ARM

### Bare Metal Access
- No QEMU limitations (Week 30 ivshmem issues)
- Direct hardware control via seL4 capabilities
- Real performance validation (not virtualized)
- Production-representative environment

### AI Feasibility
- TinyLlama 1.1B Q4: 5-15 tokens/sec on Pi 5 CPU
- Adequate for cache miss fallback (10-30s response acceptable)
- Decision cache handles 80%+ of queries (<1ms)
- Future: Pi 5 cluster for higher AI performance

---

## 2. seL4 Platform Support

### Officially Supported Raspberry Pi Models

| Model | SoC | CPU | seL4 Status |
|-------|-----|-----|-------------|
| Raspberry Pi 3B | BCM2837 | Cortex-A53 | ARM verified |
| **Raspberry Pi 4B** | **BCM2711** | **Cortex-A72** | **AARCH64 verified** |
| Raspberry Pi 5 | BCM2712 | Cortex-A76 | NOT SUPPORTED |

**Source:** [seL4 Supported Platforms](https://docs.sel4.systems/Hardware/)

### Pi 4 (BCM2711) Technical Details

| Feature | Specification |
|---------|---------------|
| CPU | Cortex-A72 quad-core @ 1.8 GHz |
| Architecture | ARMv8-A (64-bit) |
| RAM | 8GB LPDDR4-3200 |
| GPU | VideoCore VI @ 500 MHz |
| Ethernet | Broadcom GENET (Gigabit) |
| USB | 2× USB 3.0, 2× USB 2.0 |
| Storage | MicroSD (SDIO), USB boot |
| UART | PL011 @ 0xFE201000 |
| GPIO | 40-pin header (I2C, SPI, UART) |

### seL4 Pi 4 Build Requirements

**Toolchain:** `aarch64-linux-gnu-gcc`

**Boot Chain:**
1. GPU firmware (`start4.elf`, `fixup4.dat`)
2. U-Boot bootloader (`u-boot.bin`)
3. seL4 kernel loaded to `0x10000000`

**Build Configuration:**
```bash
# Cross-compile for Pi 4
cmake -DPLATFORM=rpi4 \
      -DAARCH64=1 \
      -DRPI4_MEMORY=8192 \
      -G Ninja ..

ninja
```

**References:**
- [seL4 RPi4 Documentation](https://docs.sel4.systems/Hardware/Rpi4.html)
- [TII seL4 Build](https://github.com/tiiuae/tii_sel4_build)

---

## 3. x86-Specific Code Requiring Changes

### Files with x86 Dependencies

| File | Issue | Severity | Action |
|------|-------|----------|--------|
| `stdin_impl.c` | x86 `inb` instruction, COM1 port 0x3F8 | CRITICAL | Replace with PL011 UART |
| `pci_ivshmem.h` | PCI ports 0xCF8/0xCFC | HIGH | Remove (not applicable) |
| `pci_ivshmem.c` | x86 PCI port I/O | HIGH | Remove (not applicable) |
| `main.c` | Hardcoded "x86_64" string | LOW | Update to "aarch64" |
| `main_week28.c` | Hardcoded "x86_64" string | LOW | Update to "aarch64" |

### Critical: stdin_impl.c (Lines 21-32)

**Current x86 Code:**
```c
#define SERIAL_PORT_BASE 0x3F8  // x86 COM1

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}
```

**Required ARM64 Replacement:**
```c
#define UART_BASE 0xFE201000  // BCM2711 PL011 UART

// PL011 register offsets
#define UART_DR   0x00  // Data Register
#define UART_FR   0x18  // Flag Register
#define UART_FR_TXFF (1 << 5)  // TX FIFO Full
#define UART_FR_RXFE (1 << 4)  // RX FIFO Empty

static volatile uint32_t *uart = (volatile uint32_t *)UART_BASE;

static inline void uart_putc(char c) {
    while (uart[UART_FR/4] & UART_FR_TXFF);
    uart[UART_DR/4] = c;
}

static inline char uart_getc(void) {
    while (uart[UART_FR/4] & UART_FR_RXFE);
    return uart[UART_DR/4] & 0xFF;
}
```

### Already Platform-Independent (No Changes Needed)

- Decision cache (`phase1/src/cache/*`) - Pure C, no hardware access
- Ring buffer IPC (`phase1/src/ipc/ring_buffer.c`) - Memory operations only
- All Python code (`phase1/src/ai/*`) - Runs on Linux host
- IPC handler logic (`ipc_handler.c`) - Uses abstracted UART interface
- Cache patterns (`cache_patterns.c`) - Data only

---

## 4. IPC Approach: UART

### Why Not ivshmem?

ivshmem (Inter-VM Shared Memory) is a QEMU feature for host-guest communication:
- Requires QEMU virtualization
- Not available on bare metal Pi 4
- Week 30 demonstrated limitations (VM fault, no device memory mapping)

### Selected: UART IPC

**Protocol Design:**
```
+--------+------+----+----------+-----+
| LENGTH | TYPE | ID | PAYLOAD  | CRC |
| 2 bytes| 1    | 4  | N bytes  | 2   |
+--------+------+----+----------+-----+
```

**Message Types:**
- `0x01`: CACHE_LOOKUP (Python → seL4)
- `0x02`: CACHE_RESPONSE (seL4 → Python)
- `0x03`: HEARTBEAT (bidirectional)
- `0x04`: CACHE_STATS (seL4 → Python)

**Configuration:**
- Baud rate: 115200
- Data bits: 8
- Parity: None
- Stop bits: 1
- Flow control: None

**Performance:**
| Metric | UART | ivshmem (x86) |
|--------|------|---------------|
| Latency | 1-10ms | 54μs |
| Bandwidth | 14.4 KB/s | 100+ MB/s |
| Complexity | Low | High |
| Reliability | High | Medium |

**Trade-off Justification:**
- Cache queries are small (<256 bytes)
- 80%+ queries hit cache (<1ms)
- 10ms acceptable for cache miss + AI fallback
- UART is simpler, more reliable, well-tested

### Future Upgrade Path

If higher bandwidth needed:
1. **DMA-based shared memory** via `/dev/mem` mapping
2. **VideoCore mailbox** for GPU shared memory
3. **Custom CMA region** for high-bandwidth IPC
4. **Pi 5 port** with PCIe-based shared memory

---

## 5. Driver Changes

### Removed (x86-Specific)

| Driver | Reason |
|--------|--------|
| NVMe | Requires PCIe (Pi 4 has none) |
| Intel e1000e | Intel NIC (Pi 4 uses Broadcom) |
| Intel i915 | Intel GPU (Pi 4 uses VideoCore) |
| PS/2 keyboard | Legacy x86 interface |
| ACPI S3 | x86 power management |
| PCI ivshmem | QEMU-only feature |

### Added (Pi 4-Specific)

| Driver | Purpose | Week |
|--------|---------|------|
| PL011 UART | Serial console + IPC | 32 |
| SD/EMMC | Boot + storage | 35-36 |
| Broadcom GENET | Ethernet | 37-38 |
| Device Tree | Hardware description | 32 |
| GPIO | General I/O (future) | TBD |
| VideoCore VI | GPU (future) | TBD |

### Unchanged (Platform-Independent)

| Driver | Notes |
|--------|-------|
| USB HID | Keyboard/mouse via USB host |
| Ring buffer | In-memory IPC (portable C) |
| Decision cache | Pure C hash table |

---

## 6. Revised Week 31-34 Timeline

### Original Plan (Intel NUC)

| Week | Original Task |
|------|---------------|
| 31 | Hardware acquisition ($1,200 NUC) |
| 32 | Intel NUC boot validation |
| 33 | NVMe driver design |
| 34 | NVMe driver implementation |

### Revised Plan (Raspberry Pi 4)

| Week | Revised Task |
|------|--------------|
| 31 | seL4 Pi 4 environment setup |
| 32 | JARVIS ARM64 port |
| 33 | UART IPC implementation |
| 34 | Integration & testing |

### Week 31: seL4 Pi 4 Environment Setup

**Tasks:**
1. Set up ARM64 cross-compilation toolchain
   - Install `aarch64-linux-gnu-gcc` on WSL
   - Configure CMake for cross-compilation
2. Build seL4 for Raspberry Pi 4
   - Clone TII seL4 build system
   - Configure for BCM2711, 8GB RAM
   - Build kernel.elf + elfloader
3. Prepare Pi 4 boot media
   - Format SD card (FAT32 boot, ext4 root)
   - Copy GPU firmware (`start4.elf`, `fixup4.dat`)
   - Configure U-Boot bootloader
4. First seL4 boot on Pi 4
   - Boot hello-world via UART console
   - Validate serial output (115200 baud)

**Deliverables:**
- Cross-compilation toolchain configured
- seL4 boots on Pi 4 (hello-world)
- UART serial console working

**Estimated Effort:** 10-14 hours

### Week 32: JARVIS ARM64 Port

**Tasks:**
1. Remove x86-specific code
   - `stdin_impl.c`: Replace with PL011 UART
   - `pci_ivshmem.c`: Remove entirely
   - `main.c`: Update arch string to "aarch64"
2. Implement ARM64 serial driver
   - New file: `uart_pl011.{c,h}`
   - BCM2711 PL011 UART at 0xFE201000
3. Update build system
   - CMakeLists.txt for ARM64 target
   - Device Tree support
4. Build and boot JARVIS on Pi 4
   - Decision cache initialization
   - Cache pattern loading

**Deliverables:**
- x86 code removed/replaced
- ARM64 UART driver operational
- JARVIS boots on Pi 4

**Estimated Effort:** 12-16 hours

### Week 33: UART IPC Implementation

**Tasks:**
1. Design UART IPC protocol
   - Message format defined
   - CRC validation
2. Implement seL4 UART IPC handler
   - New file: `uart_ipc.{c,h}`
   - Receive queries, send responses
3. Implement Python UART client
   - New file: `uart_ipc_client.py`
   - Serial connection via `/dev/ttyUSB0`
4. Test UART IPC
   - Loopback test
   - Cache lookup test

**Deliverables:**
- UART IPC protocol designed
- seL4 UART handler operational
- Python client working

**Estimated Effort:** 10-14 hours

### Week 34: Integration & Testing

**Tasks:**
1. Full Python↔seL4 IPC via UART
   - End-to-end cache lookup
2. Validate cache hit rate
   - Target: >80%
3. Performance benchmarking
   - UART latency: 1-10ms expected
4. TinyLlama integration (optional)
   - AI fallback for cache misses

**Deliverables:**
- Full IPC working via UART
- Cache hit rate >80% validated
- Performance documented

**Estimated Effort:** 10-14 hours

---

## 7. Cost Comparison

| Item | Intel NUC | Raspberry Pi 4 |
|------|-----------|----------------|
| Hardware | $1,200 | $75 (already owned) |
| RAM upgrade | Included | N/A (8GB built-in) |
| Storage | Included (NVMe) | $30 (64GB SD card) |
| Power supply | Included | $15 (USB-C 5V/3A) |
| Cooling | Included | $10 (heatsink + fan) |
| UART cable | N/A | $5 (USB-serial adapter) |
| **Total** | **$1,200** | **$135** |
| **Savings** | - | **$1,065 (89%)** |

### Full Phase 2 Hardware Budget

**Original (Intel NUC + Framework + Dell):**
- Intel NUC: $1,200
- Framework Laptop: $1,800
- Dell Precision: $2,000
- **Total: $5,000**

**Revised (Raspberry Pi 4 only):**
- Pi 4 accessories: $135
- Optional Pi 5 (AI): $80
- **Total: $215**

**Savings: $4,785 (96%)**

---

## 8. Risk Assessment

### Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| UART IPC too slow | Low | Medium | Acceptable for cache queries; upgrade to DMA later |
| Pi 4 seL4 build issues | Medium | Medium | TII build system well-documented; community support |
| ARM64 porting complexity | Medium | Medium | Most code platform-independent; only stdin_impl.c critical |
| SD card reliability | Medium | Low | Use high-quality card; future USB boot |
| TinyLlama too slow | Medium | Low | Decision cache handles 80%+ queries; AI is fallback |

### Mitigations Applied

1. **UART latency**: Cache hit rate >80% means most queries bypass IPC entirely
2. **Build complexity**: Using proven TII seL4 build system
3. **Porting effort**: x86-specific code already isolated in 3-4 files
4. **Storage**: Samsung/SanDisk Endurance SD cards rated for continuous write
5. **AI performance**: TinyLlama acceptable for 10-30s response on cache miss

---

## 9. Success Criteria

### Week 34 Checkpoint (Pi 4 Integration Complete)

| Criterion | Target | Measurement |
|-----------|--------|-------------|
| seL4 boots on Pi 4 | Yes | Serial console output |
| JARVIS decision cache works | Yes | Cache lookup via UART |
| Cache hit rate | >80% | 100 test queries |
| UART IPC latency | <10ms | Benchmark script |
| Python↔seL4 communication | Working | End-to-end test |

### Phase 2 Gate Criteria (Updated)

| Criterion | Original (x86) | Revised (ARM64) |
|-----------|----------------|-----------------|
| Real hardware boot | Intel NUC | Raspberry Pi 4 |
| Python↔seL4 IPC | ivshmem (<100μs) | UART (<10ms) |
| Storage driver | NVMe | SD/EMMC |
| Network driver | Intel e1000e | Broadcom GENET |
| Hardware configs | 3 (NUC, Framework, Dell) | 1 (Pi 4) |

---

## 10. Future Considerations

### Pi 5 Upgrade Path

If seL4 Pi 5 support becomes available:
- Port seL4 to BCM2712 (Cortex-A76)
- 33% faster CPU (2.4 GHz vs 1.8 GHz)
- PCIe 2.0 x1 for NVMe storage
- Better AI performance (TinyLlama 15-25 tokens/sec)

### Multi-Pi Cluster

For production deployment:
- Pi 4: seL4 microkernel (bare metal)
- Pi 5: Linux + TinyLlama (AI inference)
- UART connection between boards
- Dedicated resources, no contention

### x86 Return (Phase 3+)

If budget becomes available:
- Intel NUC for desktop deployment
- Maintain ARM64 + x86 dual target
- ivshmem for high-bandwidth IPC
- NVMe for production storage

---

## Appendix A: Reference Links

- [seL4 Supported Platforms](https://docs.sel4.systems/Hardware/)
- [seL4 RPi4 Documentation](https://docs.sel4.systems/Hardware/Rpi4.html)
- [TII seL4 Build System](https://github.com/tiiuae/tii_sel4_build)
- [BCM2711 Datasheet](https://datasheets.raspberrypi.com/bcm2711/bcm2711-peripherals.pdf)
- [PL011 UART Technical Reference](https://developer.arm.com/documentation/ddi0183/latest/)
- [TinyLlama Performance on Pi](https://aicompetence.org/running-llama-on-raspberry-pi-5/)

---

## Appendix B: Hardware Checklist

**Ordered (December 2025):**
- [x] Raspberry Pi 4 Model B 8GB (CE06974) - $145.00
- [x] Official USB-C Power Supply 5.1V 15.3W (CE06427) - $11.90
- [x] MicroSD 16GB Class 10 (CE04628) - $17.25
- [x] Large Heatsink (CE09095) - $2.40
- [x] USB to TTL Serial Cable - Adafruit (ADA954) - $20.75

**Order Total:**
- Subtotal: $197.30
- Shipping: $18.52
- GST: $19.62
- **Grand Total: $215.82 AUD**

**Note:** 16GB SD card is sufficient for initial development. Consider upgrading to 64GB+ if AI models need to be stored locally.

**Optional (Future):**
- [ ] Raspberry Pi 5 (for AI inference) (~$120 AUD)
- [ ] Larger SD card 64GB+ (~$30 AUD)
- [ ] Active cooling case (~$35 AUD)

---

*Document created: December 2025*
*Decision: Raspberry Pi 4 8GB selected as Phase 2 hardware target*
*Author: JARVIS Development Team*
