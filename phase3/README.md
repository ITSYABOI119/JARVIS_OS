# JARVIS AI-OS — Phase 3: Beta System

**Status:** PLANNING (spare x86 PC not yet assembled)
**Target:** Pure bare-metal seL4 on x86-64 with native AI inference
**Duration:** ~40-44 weeks (8-12 hours/week, solo developer)

## Goal

Standalone JARVIS OS on one x86 machine. seL4 microkernel at Ring 0, AI decision engine (ggml/llama.cpp) at Ring 3, shared memory IPC. No Linux, no VMs.

## Directory Structure

```
phase3/
├── docs/                    # Planning docs, research, guides
│   ├── PHASE_3_KICKOFF.md
│   ├── PHASE_3_HARDWARE_RESEARCH.md
│   └── PHASE_3_IMPLEMENTATION_PLAN.md
├── src/
│   ├── ai/                  # ggml integration, model loading, inference
│   ├── boot/                # x86 boot config (GRUB/EFI)
│   ├── drivers/             # x86 drivers (16550A UART, AHCI, NIC, USB)
│   ├── ipc/                 # Shared memory IPC (replaces UART framing)
│   └── sel4/                # seL4 x86-64 rootserver
├── scripts/                 # Build scripts, deployment
├── firmware/                # Boot images, GRUB configs
└── weeks/                   # Weekly status docs
```

## Hardware

| Component | Spec |
|-----------|------|
| CPU | AMD Ryzen 5 5600 (6C/12T, 3.5-4.4 GHz) |
| GPU | NVIDIA RTX 3060 12GB (Phase 4 — Vulkan) |
| RAM | 16GB DDR4 (upgradeable to 32GB) |
| Storage | NVMe SSD |
| OS | seL4 bare metal (no Linux) |

## Build (placeholder)

```bash
# Phase 3 build commands TBD when development starts
# seL4 x86-64 PC99 platform build
```

## See Also

- `docs/PHASE_3_IMPLEMENTATION_PLAN.md` — week-by-week plan
- `docs/PHASE_3_KICKOFF.md` — objectives and architecture
- `docs/PHASE_3_HARDWARE_RESEARCH.md` — hardware analysis
- `../phase2/docs/PHASE_2_FINAL_REPORT.md` — Phase 2 results (GO for Phase 3)
- `../CLAUDE.md` — project context
