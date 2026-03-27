# AHCI Virtual Disk in QEMU -- Research Notes

**Date:** March 27, 2026
**Status:** Research complete, implementation pending spare PC assembly

---

## QEMU Command Line for AHCI Disk

### Creating a Test Disk Image

```bash
# Create a 1MB raw disk image (2048 sectors of 512 bytes)
dd if=/dev/zero of=test.img bs=512 count=2048

# Write a known signature to sector 0 for read-back verification
echo "JARVIS_TEST_SECTOR" | dd of=test.img bs=512 seek=0 conv=notrunc

# (Optional) Write a pattern to sector 1 for multi-sector tests
echo "SECTOR_ONE_DATA_OK" | dd of=test.img bs=512 seek=1 conv=notrunc
```

### Attaching the AHCI Disk to QEMU

Add these flags to the existing QEMU command line:

```bash
qemu-system-x86_64 \
  -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
  -nographic -serial mon:stdio -m size=3G \
  -kernel images/kernel-x86_64-pc99 \
  -initrd images/jarvis-rootserver-image-x86_64-pc99 \
  -drive file=test.img,format=raw,if=none,id=drive0 \
  -device ahci,id=ahci0 \
  -device ide-hd,drive=drive0,bus=ahci0.0
```

**Explanation of flags:**
- `-drive file=test.img,format=raw,if=none,id=drive0` -- Creates a block backend named `drive0` backed by a raw disk image. `if=none` means it is not auto-attached to any bus; we attach it manually below.
- `-device ahci,id=ahci0` -- Adds an ICH9 AHCI controller to the PCI bus. QEMU emulates the Intel ICH9 AHCI (vendor 0x8086, device 0x2922, class 01:06, prog_if 01). This will appear during PCI enumeration.
- `-device ide-hd,drive=drive0,bus=ahci0.0` -- Connects the block backend to AHCI port 0 as a hard disk. `bus=ahci0.0` means port 0 of the ahci0 controller.

### What QEMU Emulates

The QEMU ICH9 AHCI controller provides:
- PCI function at class 01 (storage), subclass 06 (SATA), prog_if 01 (AHCI)
- BAR5 (ABAR) pointing to HBA MMIO registers
- Full AHCI 1.3 register set: GHC, CAP, PI, port registers
- Port 0 with SATA device signature 0x00000101 (ATA disk)
- DMA command processing: command list, FIS receive, PRDT scatter/gather
- ATA commands: IDENTIFY DEVICE (0xEC), READ DMA EXT (0x25), WRITE DMA EXT (0x35)

---

## Existing Code Analysis

### What Is Ready

| Component | File | Status | Notes |
|-----------|------|--------|-------|
| PCI enumeration | `phase3/src/drivers/pci.c` | DONE | `pci_scan()` scans bus 0, `pci_find_device(0x01, 0x06)` finds AHCI |
| PCI BAR5 read | `phase3/src/drivers/pci.c` | DONE | `pci_get_bar_address(dev, 5)` extracts MMIO base |
| PCI bus master enable | `phase3/src/drivers/pci.c` | DONE | `pci_enable_bus_master()` sets CMD.BME |
| AHCI HBA init | `phase3/src/drivers/ahci.c` | DONE | `ahci_discover_init()` reads CAP/VS/PI, sets GHC.AE |
| Port probe | `phase3/src/drivers/ahci.c` | DONE | `ahci_probe_ports()` reads PxSSTS/PxSIG for all ports |
| Port active check | `phase3/src/drivers/ahci.c` | DONE | `ahci_port_check_active()` checks DET + IPM bits |
| H2D FIS builder | `phase3/src/drivers/ahci.c` | DONE | `ahci_build_rw_fis()` for READ/WRITE DMA EXT (LBA48) |
| Command setup | `phase3/src/drivers/ahci.c` | DONE | `ahci_setup_command()` builds cmd header + PRDT entries |
| Command issue + poll | `phase3/src/drivers/ahci.c` | DONE | `ahci_issue_command()` writes PxCI, polls for completion |
| IDENTIFY DEVICE | `phase3/src/drivers/ahci.c` | DONE | `ahci_identify()` sends 0xEC, parses model/serial/capacity |
| READ DMA EXT | `phase3/src/drivers/ahci.c` | DONE | `ahci_read_sectors()` reads via LBA48 |
| WRITE DMA EXT | `phase3/src/drivers/ahci.c` | DONE | `ahci_write_sectors()` writes via LBA48 |
| Identify parsing | `phase3/src/drivers/ahci.c` | DONE | Model, serial, capacity, sector size extraction |
| Block device API | `phase3/src/drivers/blk_dev_x86.c` | DONE (mock) | `blk_dev_init/read/write/get_info` with mock backend |
| Info display | `phase3/src/drivers/ahci.c` | DONE | `ahci_print_info()` prints controller + port summary |
| All structures | `phase3/src/drivers/ahci.h` | DONE | `hba_mem_t`, `hba_port_t`, `hba_cmd_header_t`, `hba_cmd_table_t`, `hba_prd_entry_t`, all FIS types |

**Test counts:** AHCI 13 PASS, PCI 11 PASS, Block device 9 PASS (all mock-tested with `JARVIS_TEST_MOCK`).

### What Is Missing

#### 1. Port Command Engine Start/Stop (Critical)

The existing code never enables the port command engine before issuing commands. AHCI requires:

```c
// Before any command can be issued on a port:
// 1. Set PxCLB/PxFB to point at command list / FIS receive buffers
// 2. Set PxCMD.FRE (FIS Receive Enable, bit 4)
// 3. Wait for PxCMD.FR (FIS Receive Running, bit 14) to go high
// 4. Set PxCMD.ST (Start, bit 0) to begin command processing
//
// To stop the port (before reconfig):
// 1. Clear PxCMD.ST
// 2. Wait for PxCMD.CR (Command List Running, bit 15) to clear
// 3. Clear PxCMD.FRE
// 4. Wait for PxCMD.FR to clear
```

The `ahci_setup_command()` function does set `port->clb` and `port->fb` to point at the static buffers, but it never sets `PORT_CMD_FRE` or `PORT_CMD_ST`. On real (or QEMU-emulated) hardware, `ahci_issue_command()` will hang because the command engine is not running.

**Fix needed in `ahci.c`:** Add `ahci_port_start()` and `ahci_port_stop()` functions, called between buffer setup and first command issue.

#### 2. seL4 Device Frame Mapping for AHCI MMIO

The rootserver (`main_x86.c`) currently has no code to:
- Find AHCI device untypeds in `seL4_BootInfo`
- Retype them into frames
- Map AHCI BAR5 MMIO region into the rootserver VSpace

On seL4, PCI device MMIO regions appear as device untypeds in the boot info. The rootserver must:
1. Read PCI config space (via I/O port 0xCF8/0xCFC -- these work from userspace on x86 seL4 with `seL4_X86_IOPort` capabilities)
2. Find the AHCI controller's BAR5 physical address
3. Locate the matching device untyped in `info->untypedList`
4. `seL4_Untyped_Retype` it into a frame
5. Map the frame into VSpace at a known virtual address

This is the same pattern used in Phase 2 for BCM2711 device mapping, but x86 uses different seL4 APIs (`seL4_X86_Page_Map` vs `seL4_ARM_Page_Map`).

#### 3. DMA Buffer Allocation (Physical Address Requirement)

AHCI DMA requires physically-contiguous buffers with known physical addresses:
- Command list: 1 KB, 1K-aligned
- FIS receive: 256 bytes, 256-byte aligned
- Command table: 128+ bytes, 128-byte aligned
- Data buffers: sector-aligned, physically contiguous

Currently the code uses static `__attribute__((aligned(...)))` buffers. On seL4, these are in the rootserver's virtual address space. The AHCI controller needs their **physical** addresses for the CLB/FB/CTBA registers and PRDT entries.

**Options:**
1. **seL4_Untyped_Retype to frames** -- Allocate from untyped memory, retype to frames, map them. The physical address is known from the untyped's paddr.
2. **Large page mapping** -- Map data buffers as large pages to get contiguous physical memory.
3. **Identity mapping** -- If the rootserver can identity-map a region, virtual = physical. Simplest but not always possible on seL4.

#### 4. I/O Port Capabilities for PCI Config Access

The PCI driver uses x86 I/O ports 0xCF8 and 0xCFC. On seL4, I/O port access requires `seL4_X86_IOPort` capabilities. The rootserver must:
1. Find (or mint) IOPort caps for ports 0xCF8-0xCFF
2. Use `seL4_X86_IOPort_In32` / `seL4_X86_IOPort_Out32` instead of raw `inl`/`outl`

The current `pci.c` uses inline assembly `inl`/`outl` which will fault on seL4 without IOPort caps. seL4 provides IOPort caps to the initial rootserver for the full port range, so the caps should be available -- but the code needs to use the seL4 API rather than raw instructions.

#### 5. Block Device Integration (blk_dev_x86.c Real Mode)

The `blk_dev_init()` non-mock path is a stub returning -1:

```c
#else
    /* Real: probe AHCI, IDENTIFY first disk */
    /* TODO: ahci_discover_init, ahci_probe_ports, ahci_identify */
    return -1;
#endif
```

And `blk_dev_read()`/`blk_dev_write()` pass `NULL` as the port pointer:

```c
    return ahci_read_sectors(NULL, lba, count, buf);
```

This needs to be wired up to the actual AHCI port discovered during init.

---

## seL4-Specific Challenges

### Device Untyped Discovery

seL4 boot info contains `untyped` objects. Device untypeds (with `isDevice=1`) correspond to MMIO regions. The rootserver must scan `info->untypedList[]` to find one whose `paddr` matches the AHCI BAR5 address, then retype it.

```c
// Pseudocode for AHCI MMIO mapping on seL4 x86-64:
seL4_BootInfo *info = platsupport_get_bootinfo();

// 1. Read BAR5 from PCI config space
uint32_t bar5 = pci_get_bar_address(&ahci_dev, 5);

// 2. Find device untyped covering that address
for (i = info->untyped.start; i < info->untyped.end; i++) {
    if (info->untypedList[i - info->untyped.start].isDevice &&
        info->untypedList[i - info->untyped.start].paddr == (bar5 & ~0xFFF)) {
        // Found it -- retype and map
    }
}
```

### DMA Coherency

x86 hardware is cache-coherent for DMA (unlike ARM), so no explicit cache maintenance is needed. However, the compiler may reorder or optimize away MMIO accesses. All port register accesses through `hba_port_t` already use `volatile`, which is correct.

### Interrupt Handling

The current code uses polling (`ahci_issue_command` busy-loops on PxCI). This works but is CPU-wasteful. For Phase 3c, interrupt-driven I/O would use:
- seL4 IRQ handler capabilities
- MSI or legacy PCI interrupts
- `seL4_IRQHandler_Ack` + `seL4_Wait` pattern

Polling is fine for initial bring-up and QEMU testing.

---

## Estimated Effort

| Task | Effort | Depends On |
|------|--------|------------|
| Port start/stop functions | 0.5 days | Nothing -- pure AHCI code |
| seL4 IOPort cap wrappers for PCI | 1 day | seL4 build env |
| seL4 device untyped mapping for AHCI MMIO | 1-2 days | seL4 build env |
| DMA buffer allocation (physical addr) | 1-2 days | seL4 untyped allocation |
| Wire up blk_dev_x86.c real mode | 0.5 days | All above |
| Test: IDENTIFY + read sector 0 in QEMU | 1 day | All above |
| Test: write + read-back verification | 0.5 days | Read working |
| **Total** | **5-7 days (~1 week)** | |

This fits within the Phase 3b Week 15-16 plan for "AHCI full I/O" as documented in `PHASE_3_IMPLEMENTATION_PLAN.md`. The AHCI command infrastructure (FIS building, command setup, PRDT, polling) is already complete and mock-tested (13 PASS). The remaining work is almost entirely seL4 integration -- mapping device MMIO, allocating DMA buffers with known physical addresses, and wiring up the PCI I/O port access to use seL4 capabilities.

---

## Relationship to Phase 3b Plan

Per `CLAUDE.md` and `PHASE_3_IMPLEMENTATION_PLAN.md`:

- **Weeks 15-16:** AHCI full I/O + Block device abstraction
  - PCI enumeration: DONE (11 tests)
  - AHCI discovery + DMA I/O: DONE in mock (13 tests)
  - Block device wrapper: DONE in mock (9 tests)
  - **Remaining:** seL4 integration (device mapping, DMA alloc, IOPort caps)

The AHCI driver was built ahead of schedule during QEMU pre-work. All command-level logic is verified via mock testing. The gap is exclusively the seL4 <-> hardware interface layer: device frame mapping, physical memory allocation, and I/O port capability plumbing.

Once the spare PC is assembled and seL4 boots on real x86-64 hardware, attaching a QEMU AHCI disk (using the command line above) will be the first integration test before moving to real NVMe/SATA.

---

## File Reference

| File | Purpose |
|------|---------|
| `phase3/src/drivers/ahci.h` | AHCI register definitions, DMA structures, all prototypes |
| `phase3/src/drivers/ahci.c` | AHCI discovery, port probe, command setup, IDENTIFY, READ/WRITE DMA EXT |
| `phase3/src/drivers/pci.h` | PCI config space definitions, device descriptor, prototypes |
| `phase3/src/drivers/pci.c` | PCI enumeration (bus 0), BAR parsing, bus master enable |
| `phase3/src/drivers/blk_dev_x86.h` | Block device API (init/read/write/info) |
| `phase3/src/drivers/blk_dev_x86.c` | Block device wrapper (mock backend, AHCI real-mode stub) |
| `phase3/src/sel4/main_x86.c` | x86-64 rootserver (no AHCI/PCI integration yet) |
| `phase3/docs/SEL4_X86_QEMU_SETUP.md` | QEMU build/run instructions |
| `phase3/docs/SEL4_X86_ROOTSERVER_NOTES.md` | Rootserver build integration notes |

---

## Key Functions (Quick Reference)

**PCI:**
- `pci_scan(devices, max)` -- Enumerate PCI bus 0
- `pci_find_device(0x01, 0x06, devices, count)` -- Find AHCI controller
- `pci_get_bar_address(dev, 5)` -- Get AHCI MMIO base from BAR5
- `pci_enable_bus_master(dev)` -- Enable DMA

**AHCI Discovery:**
- `ahci_discover_init(ctrl, mmio_base)` -- Read HBA caps, enable AHCI
- `ahci_probe_ports(ctrl)` -- Detect connected devices
- `ahci_port_check_active(ctrl, port)` -- Check DET + IPM
- `ahci_print_info(ctrl)` -- Print summary

**AHCI I/O:**
- `ahci_setup_command(port, slot, fis, buf, len, write)` -- Build command
- `ahci_issue_command(port, slot)` -- Issue + poll
- `ahci_identify(port, buf)` -- IDENTIFY DEVICE
- `ahci_read_sectors(port, lba, count, buf)` -- READ DMA EXT
- `ahci_write_sectors(port, lba, count, buf)` -- WRITE DMA EXT

**Block Device:**
- `blk_dev_init()` -- Probe and initialize (mock or AHCI)
- `blk_dev_read(lba, count, buf)` -- Read sectors
- `blk_dev_write(lba, count, buf)` -- Write sectors
- `blk_dev_get_info(info)` -- Get disk model/serial/capacity

---

*Notes written: March 27, 2026*
