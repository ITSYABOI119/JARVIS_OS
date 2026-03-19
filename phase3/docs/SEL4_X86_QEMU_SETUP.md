# seL4 x86-64 QEMU Setup Guide

**Date:** March 19, 2026
**Platform:** x86-64 PC99
**Host:** WSL2 Ubuntu 24.04 on Windows 11
**Result:** 123 tests passed, 0 failed, 57 disabled

---

## Prerequisites

### WSL Packages (already installed)

```bash
sudo apt install build-essential cmake ninja-build python3 python3-pip \
  gcc-multilib g++-multilib qemu-system-x86 repo git libxml2-utils
```

### Python Packages (required for nanopb protobuf generation)

```bash
pip3 install --break-system-packages protobuf grpcio-tools pyelftools
```

**Note:** Without `protobuf` and `grpcio-tools`, the build fails at step 204/266 with `ModuleNotFoundError: No module named 'proto.nanopb_pb2'`.

---

## Build Steps

### 1. Clone seL4 Test Manifest

```bash
mkdir -p ~/sel4-x86 && cd ~/sel4-x86
repo init -u https://github.com/seL4/sel4test-manifest.git
repo sync -j4
```

**Time:** ~5-10 minutes for initial sync (~2-3 GB download)

### 2. Configure for x86-64 Simulation

```bash
mkdir -p cbuild && cd cbuild
../init-build.sh -DPLATFORM=x86_64 -DSIMULATION=1
```

### 3. Build

```bash
ninja
```

**Time:** ~2-3 minutes (266 build steps)

**Output:** Two images in `images/`:
- `kernel-x86_64-pc99` — seL4 kernel
- `sel4test-driver-image-x86_64-pc99` — test rootserver image

### 4. Run in QEMU

```bash
./simulate
```

Or manually:
```bash
qemu-system-x86_64 \
  -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
  -nographic -serial mon:stdio -m size=3G \
  -kernel images/kernel-x86_64-pc99 \
  -initrd images/sel4test-driver-image-x86_64-pc99
```

**Exit QEMU:** `Ctrl-A, X`

---

## Test Results

```
Test suite passed. 123 tests passed. 57 tests disabled.
All is well in the universe
```

### Sample test categories that passed:

- TRIVIAL: Framework functions, allocator
- IPC: Interprocess communication (call, reply, wait, signal)
- SCHED: Scheduling, priorities, time slices
- VSPACE: Virtual memory, ASID pools
- CSPACE: Capability space operations
- FRAME: Page frame mapping
- IRQ: Interrupt handling
- TLS: Thread-local storage
- SCHED_CONTEXT: Scheduling context operations

---

## Boot Sequence (Captured)

```
SeaBIOS → iPXE → seL4 kernel boot
  ↓
Boot config: debug_port = 0x3f8
Detected 1 boot module(s)
ACPI: 1 CPU(s) detected
  ↓
ELF-loading userland images
Starting node #0 with APIC ID 0
Booting all finished, dropped to user space
  ↓
123 tests pass
```

### Hardware detected in QEMU:

| Resource | Address |
|----------|---------|
| Debug serial port | 0x3F8 (COM1, 16550A) |
| LAPIC | 0xFEE00000 |
| IOAPIC | 0xFEC00000 |
| Available RAM | 0x100000 - 0xBFFE0000 (~3GB) |
| ACPI RSDP | 0xF52A0 |
| Kernel loaded | 0x100000 - 0xA13000 (9.1MB) |
| Userland loaded | 0x400000 - 0x7DE000 (3.9MB) |

### Untyped Memory Available:

Multiple device untypeds + RAM untypeds provided to rootserver. This matches the seL4 capability model — rootserver gets all physical resources as untyped capabilities.

---

## Key Observations for JARVIS Phase 3b

1. **seL4 x86-64 is solid.** 123/123 tests pass. IPC, scheduling, memory management all work.

2. **Serial port at 0x3F8.** Same 16550A UART our skeleton header (`uart_16550.h`) targets. No surprise — standard PC hardware.

3. **ACPI parsed automatically.** seL4 kernel reads ACPI tables (RSDP, RSDT, FADT, MADT) for CPU and IOAPIC discovery. We don't need to write ACPI parsing.

4. **3GB RAM available.** QEMU configured with `-m 3G`, rootserver sees ~3GB of untypeds. Real hardware (16GB) will have much more.

5. **Build system works.** The sel4test manifest + init-build.sh + ninja pipeline is straightforward. Our custom rootserver can follow the same pattern.

6. **QEMU emulates Nehalem CPU.** No AVX2/FMA — just SSE4.2. Real Ryzen 5 5600 will have AVX2 for ggml SIMD. QEMU is fine for functional testing but performance benchmarks must wait for real hardware.

---

## Next Steps

- **Task 8:** Create custom JARVIS rootserver project using this build environment
- Load decision cache (258 patterns) from compiled-in data
- Serial console shell with cache lookups
- Benchmark seL4 IPC latency in QEMU (not representative of real hardware but validates functionality)

---

*Setup verified: March 19, 2026*
*Host: WSL2 Ubuntu 24.04, QEMU 8.2.2*
*seL4 kernel: from sel4test-manifest (latest)*
*Build time: ~3 minutes*
*Test result: 123 PASS, 0 FAIL*
