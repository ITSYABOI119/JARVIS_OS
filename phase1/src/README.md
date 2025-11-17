# Phase 1 Source Code

This directory contains the implementation of JARVIS AI-OS Phase 1 (Proof of Concept).

## Directory Structure

```
src/
├── sel4/         # seL4 microkernel configuration and custom code
├── cache/        # Decision cache implementation (hash table, patterns)
├── ipc/          # Lock-free ring buffer IPC layer
├── ai/           # AI agent integration (Python + llama-cpp-python)
└── shell/        # Text shell interface (REPL, command parser)
```

## Build System

Phase 1 uses CMake + Ninja (seL4 standard build system):

```bash
# Build (from phase1/ directory)
cd build
cmake -G Ninja ../src
ninja

# Run in QEMU
./simulate
```

## Development Status

- **Week 1:** Environment setup, directory structure ✅
- **Week 2:** seL4 serial console (TBD)
- **Week 3:** Decision cache (TBD)
- **Week 4:** IPC layer (TBD)
- **Weeks 5-26:** See PHASE_1_IMPLEMENTATION_PLAN.md

## Prerequisites

See `PHASE_1_DEVELOPMENT_SETUP.md` for complete setup instructions.

**Required:**
- QEMU 7.0+
- seL4 microkernel (downloaded via repo)
- CMake 3.20+, Ninja 1.10+
- Python 3.11+, llama-cpp-python
- Phi-3 Mini 3.8B Q4 model

## Key Components

### 1. seL4/ - Microkernel Configuration
- seL4 kernel configuration (Ring 0, cores 0-1)
- Custom IPC setup
- Serial console driver
- Memory management configuration

### 2. cache/ - Decision Cache
- Hash table implementation (256 entries)
- 200 pre-compiled patterns
- Query normalization
- Cache statistics tracking

### 3. ipc/ - IPC Layer
- Lock-free SPSC ring buffer (from Phase 0 validation)
- Shared memory management
- Message protocol
- Latency tracking (<100μs target)

### 4. ai/ - AI Agent
- Phi-3 Mini 3.8B integration
- llama-cpp-python wrapper
- Query processing
- Command generation

### 5. shell/ - Text Shell
- REPL loop
- Natural language input parsing
- Response formatting
- Command history

## Code Style

- **C code:** Linux kernel style (for seL4/IPC)
- **Python code:** PEP 8
- **Comments:** Document WHY, not WHAT
- **Performance:** Measure before optimizing

## Testing

Each component has unit tests:
- `cache_test.c` - Decision cache tests
- `ipc_test.c` - IPC latency/throughput tests
- `test_ai.py` - AI agent tests
- `test_shell.py` - Shell integration tests

## Performance Targets

| Component | Target | Measurement |
|-----------|--------|-------------|
| Decision cache lookup | <0.1ms | `cache_test.c` |
| IPC latency (median) | <100μs | `ipc_test.c` |
| AI inference (cached) | <2s | End-to-end test |
| Boot time | <60s | QEMU start → shell prompt |

## Documentation

- `PHASE_1_ARCHITECTURE.md` - Complete system architecture
- `PHASE_1_TECHNICAL_SPEC.md` - Detailed component specifications
- `PHASE_1_IMPLEMENTATION_PLAN.md` - Week-by-week tasks
