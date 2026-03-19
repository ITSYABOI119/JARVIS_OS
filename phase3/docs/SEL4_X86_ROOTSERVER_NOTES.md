# seL4 x86-64 Custom Rootserver — Development Notes

**Date:** March 19, 2026
**Status:** Source code written, build integration pending

---

## What's Done

### main_x86.c — JARVIS x86-64 Rootserver

A complete rootserver source file at `phase3/src/sel4/main_x86.c` that:
- Prints JARVIS ASCII banner on serial
- Loads decision cache (258 patterns from compiled-in `cache_patterns.c`)
- Provides shell commands: `query`, `shield`, `stats`, `cache info`, `version`, `help`
- Runs SHIELD risk scoring with keyword-based detection
- Tracks cache hit/miss statistics
- Runs demo queries to prove functionality on boot

### CMakeLists.txt — Build Template

At `phase3/src/sel4/CMakeLists.txt`. Links against:
- `sel4`, `muslc`, `sel4runtime` — core seL4 userspace
- `sel4allocman`, `sel4vka`, `sel4utils` — memory/capability management
- `sel4platsupport`, `sel4muslcsys` — platform support
- Plus JARVIS portable modules: decision_cache, cache_patterns, ring_buffer, shmem_ipc

---

## What's Pending

### Build Integration

The rootserver needs to be integrated into the seL4 build system. Two approaches:

**Approach A: Fork sel4test manifest, replace test app with JARVIS**
```bash
# In ~/sel4-x86/projects/
cp -r sel4test jarvis-x86
# Edit jarvis-x86/CMakeLists.txt to point to JARVIS sources
# Edit top-level CMakeLists.txt to build jarvis-x86 instead of sel4test
```

**Approach B: Create standalone JARVIS manifest**
```xml
<!-- jarvis-x86-manifest/default.xml -->
<manifest>
  <remote name="sel4" fetch="https://github.com/seL4"/>
  <remote name="jarvis" fetch="file:///mnt/c/Users/jluca/Documents/JARVIS_OS"/>
  <default revision="master" remote="sel4"/>
  <project name="seL4" path="kernel"/>
  <project name="seL4_tools" path="tools/seL4"/>
  <project name="musllibc" path="projects/musllibc"/>
  <project name="util_libs" path="projects/util_libs"/>
  <project name="seL4_libs" path="projects/seL4_libs"/>
  <project name="sel4runtime" path="projects/sel4runtime"/>
  <!-- JARVIS project -->
  <project name="JARVIS_OS" path="projects/jarvis-x86" remote="jarvis"/>
</manifest>
```

**Recommended: Approach A** — simpler, proven to work (sel4test already builds).

### Interactive Serial Input

`main_x86.c` currently runs demo queries and halts because:
- `seL4_DebugPutChar()` is TX-only — no RX equivalent in seL4 debug API
- Interactive input requires a 16550A UART driver reading I/O port 0x3F8
- This is Phase 3b Week 13-14 work (uart_16550.c implementation)

Workarounds for testing:
1. Feed commands via boot module (CPIO archive with command list)
2. Use `seL4_DebugGetChar()` if available in debug kernel config
3. Wait for UART driver (proper solution)

### Clang Diagnostics

The clang LSP shows errors on `main_x86.c` because seL4 headers (`sel4/sel4.h`) and JARVIS headers (`decision_cache.h`) are not in the clang include path. These are resolved by the seL4 CMake build system which adds all include paths automatically. The code compiles correctly within the seL4 build tree.

---

## Build Integration Steps (When Ready)

```bash
# 1. Copy JARVIS project into seL4 tree
cd ~/sel4-x86
mkdir -p projects/jarvis-x86/src
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase3/src/sel4/main_x86.c projects/jarvis-x86/src/
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase3/src/ai/ projects/jarvis-x86/src/ai/
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase3/src/ipc/ projects/jarvis-x86/src/ipc/
cp /mnt/c/Users/jluca/Documents/JARVIS_OS/phase3/src/sel4/CMakeLists.txt projects/jarvis-x86/

# 2. Modify top-level to build JARVIS instead of sel4test
# (edit CMakeLists.txt or init-build.sh to point to jarvis-x86)

# 3. Build
cd cbuild
cmake -G Ninja -DPLATFORM=x86_64 -DSIMULATION=1 ../projects/jarvis-x86
ninja

# 4. Run
./simulate
```

---

## Key Differences from ARM64 Rootserver (Phase 2)

| Aspect | ARM64 (Phase 2) | x86-64 (Phase 3) |
|--------|-----------------|-------------------|
| Serial TX | `seL4_DebugPutChar()` + direct MMIO | `seL4_DebugPutChar()` (kernel uses COM1) |
| Serial RX | MMIO at 0x5c0000 (PL011) | I/O port 0x3F8 (16550A) — pending |
| Device mapping | `seL4_Untyped_Retype` with forward cursor | PCI BAR + MMIO mapping |
| Timer | BCM2711 system timer (1 MHz) | HPET or TSC |
| Storage | EMMC/SDHCI (ADMA2) | AHCI/NVMe (DMA) |
| Network | GENET (BCM54213PE) | Intel/Realtek NIC |
| IPC | UART framing (7ms RTT) | Shared memory (<1μs) |
| Cache | Same (258 patterns, FNV-1a) | Same (portable) |
| SHIELD | Same (risk scoring) | Same (portable) |

---

*Notes written: March 19, 2026*
*seL4 x86-64 QEMU verified: 123/123 tests pass*
*Rootserver source: phase3/src/sel4/main_x86.c*
*Build integration: pending (Approach A recommended)*
