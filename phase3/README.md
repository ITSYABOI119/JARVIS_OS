# JARVIS AI-OS — Phase 3: Beta System

**Status:** PRE-WORK COMPLETE + early Phase 3b development done. Awaiting spare PC assembly.
**Target:** Pure bare-metal seL4 on x86-64 with native AI inference
**Duration:** ~40-44 weeks (8-12 hours/week, solo developer)
**Code so far:** 32 files, 8,636 LOC, 64 tests (all passing)

## Goal

Standalone JARVIS OS on one x86 machine. seL4 microkernel at Ring 0, AI decision engine (ggml/llama.cpp) at Ring 3, shared memory IPC. No Linux, no VMs.

## What's Done Already

| Component | Status | Tests |
|-----------|--------|-------|
| seL4 x86-64 QEMU | Boots, 123/123 kernel tests pass | Verified |
| Custom rootserver | Boots in QEMU, decision cache + SHIELD working | Functional |
| GGUF parser (C-only) | Replaces C++ gguf.cpp for model loading | 9/9 PASS |
| Shared memory IPC | Lock-free ring buffer, 23.7M msg/sec | 10/10 PASS |
| UART 16550A driver | Full serial I/O with mock testing | 7/7 PASS |
| PCI enumeration | Bus scan, BAR parsing, class find | 11/11 PASS |
| AHCI discovery | Controller probe, port detection | 5/5 PASS |
| Portable Phase 2 code | Decision cache, ring buffers, net stack on x86 | 22/22 PASS |

## Directory Structure

```
phase3/
├── docs/
│   ├── PHASE_3_KICKOFF.md            # Objectives + architecture
│   ├── PHASE_3_HARDWARE_RESEARCH.md  # Hardware option analysis
│   ├── PHASE_3_IMPLEMENTATION_PLAN.md # Week-by-week plan
│   ├── SEL4_X86_QEMU_SETUP.md       # seL4 x86 build + QEMU guide
│   ├── SEL4_X86_ROOTSERVER_NOTES.md  # Build integration notes
│   └── PI5_BENCHMARK_RESULTS.md      # Pi 5 LLM benchmark data
├── src/
│   ├── ai/                  # Decision cache, GGUF parser, ggml notes
│   │   ├── decision_cache.c/h       # Ported from Phase 2
│   │   ├── cache_patterns.c/h       # Ported from Phase 2
│   │   ├── gguf_parser.c/h          # C-only GGUF model loader
│   │   └── GGML_PORTABILITY_NOTES.md
│   ├── drivers/             # x86 drivers (implemented + headers)
│   │   ├── uart_16550.c/h           # 16550A serial driver
│   │   ├── pci.c/h                  # PCI bus enumeration
│   │   ├── ahci.c/h                 # AHCI/SATA discovery
│   │   ├── net_stack.c/h            # Ported from Phase 2
│   │   └── net_cmd.c/h              # Ported from Phase 2
│   ├── ipc/                 # IPC protocols
│   │   ├── shmem_ipc.c/h            # Shared memory ring buffer
│   │   ├── ipc_transport.h          # UART/SHMEM abstraction
│   │   ├── ring_buffer.c/h          # Ported from Phase 1
│   │   └── dual_ring_buffer.c/h     # Ported from Phase 2
│   └── sel4/                # seL4 x86-64 rootserver
│       ├── main_x86.c               # Rootserver entry point
│       └── CMakeLists.txt            # Build config
├── scripts/
│   └── pi5_benchmark.sh             # Pi 5 LLM benchmark script
├── firmware/                # Boot images (populated on build)
└── weeks/                   # Weekly status docs
```

## Hardware

| Component | Spec | Status |
|-----------|------|--------|
| CPU | AMD Ryzen 5 5600 (6C/12T) | Pending |
| GPU | NVIDIA RTX 3060 12GB | Pending |
| RAM | G.Skill Ripjaws V 16GB DDR4-3200 | **BOUGHT** |
| Mobo | Gigabyte B550M K (Micro ATX) | Pending |
| Storage | Netac NV3000 500GB NVMe | Pending |
| Case | Antec NX200M | Pending |
| PSU | Corsair RM650e 650W | Pending |

## Build

```bash
# seL4 x86-64 build (in WSL, after repo sync):
cd ~/sel4-x86/jbuild
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake \
  -DPLATFORM=x86_64 -DSIMULATION=1 \
  -DSEL4_CACHE_DIR=$HOME/.sel4_cache \
  -C ../projects/jarvis-x86/settings.cmake \
  ../projects/jarvis-x86
ninja
./simulate   # Boots JARVIS rootserver in QEMU
```

## See Also

- `docs/PHASE_3_IMPLEMENTATION_PLAN.md` — week-by-week plan
- `docs/PHASE_3_KICKOFF.md` — objectives and architecture
- `docs/PHASE_3_HARDWARE_RESEARCH.md` — hardware analysis
- `docs/SEL4_X86_QEMU_SETUP.md` — seL4 x86 build guide
- `../phase2/docs/PHASE_2_FINAL_REPORT.md` — Phase 2 results (GO for Phase 3)
- `../CLAUDE.md` — project context
