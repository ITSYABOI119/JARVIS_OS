# Phase 2 Source Code

Phase 2 implements the Alpha System running on Raspberry Pi 4 with UART-based Python-seL4 IPC.

## Directory Structure

```
phase2/src/
├── ai/                     # Python AI components
│   ├── uart_ipc_client.py  # UART serial IPC client
│   ├── system_bootstrap.py # Unified initialization framework
│   └── test_*.py           # Unit tests
├── drivers/                # Hardware drivers
│   ├── uart_pl011.c/h      # PL011 UART driver for Pi 4
│   └── test_uart_logic.c   # Driver logic tests
├── ipc/                    # Inter-Process Communication
│   ├── dual_ring_buffer.c/h # Bidirectional ring buffer
│   ├── ipc_handler.c/h     # IPC message handler
│   └── test_*.c            # C unit tests
├── scripts/                # Build and setup scripts
│   ├── setup_sel4_pi4.sh   # ARM64 toolchain setup
│   └── run_jarvis_qemu.sh  # QEMU launch script
└── sel4/                   # seL4 kernel integration
    └── main_week28.c       # Dual ring buffer integration
```

## Key Differences from Phase 1

| Aspect | Phase 1 | Phase 2 |
|--------|---------|---------|
| Architecture | x86_64 QEMU | ARM64 Raspberry Pi 4 |
| IPC Protocol | Shared memory (54us) | UART serial (10-20ms) |
| Hardware | Virtual only | Real bare metal |
| AI Model | Phi-3 Mini 3.8B | TinyLlama 1.1B |
| Cross-compile | Native x86 | aarch64-linux-gnu-gcc |

## Building

### Prerequisites

```bash
# Install ARM64 cross-compiler (Ubuntu/WSL)
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

### C Components

```bash
# Build dual ring buffer tests
cd phase2/src/ipc
gcc -O2 -I../../../phase1/src/cache -I../../../phase1/src/ipc \
  dual_ring_buffer.c test_dual_ring.c ../../../phase1/src/ipc/ring_buffer.c \
  -o test_dual_ring && ./test_dual_ring

# Build UART driver logic tests
cd phase2/src/drivers
gcc -O2 test_uart_logic.c -o test_uart_logic && ./test_uart_logic
```

### Python Components

```bash
# Run UART IPC client tests
python3 phase2/src/ai/test_uart_ipc_client.py      # 22 tests

# Run SystemBootstrap tests
python3 phase2/src/ai/test_system_bootstrap.py     # 25 tests

# Run integration tests
python3 phase2/src/ai/test_integration.py          # 10 tests
```

## UART IPC Protocol

Phase 2 uses UART serial (115200 baud) instead of shared memory for Python-seL4 communication.

### Frame Format

```
┌──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬──────────┐
│  SYNC    │  TYPE    │   SEQ    │  LENGTH  │  FLAGS   │ PAYLOAD  │  CRC16   │
│ (2 bytes)│ (1 byte) │ (2 bytes)│ (2 bytes)│ (1 byte) │ (0-240)  │ (2 bytes)│
│  0xAA55  │  0x01-0E │  0-65535 │  0-240   │  0x00    │  data    │ CRC-CCITT│
└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴──────────┘
```

### Message Types

- `QUERY` (0x01): Cache lookup request
- `RESPONSE` (0x02): Cache lookup result
- `HEARTBEAT` (0x03): Keep-alive ping
- `SHIELD_CHECK` (0x07): Risk assessment request
- `COMMAND` (0x09): Shell command

See `phase2/docs/UART_IPC_PROTOCOL.md` for full specification.

## Test Coverage

| Test Suite | Tests | Status |
|------------|-------|--------|
| test_dual_ring.c | 12 | PASS |
| test_ipc_handler.c | 10 | PASS |
| test_uart_logic.c | 8 | PASS |
| test_uart_ipc_client.py | 22 | PASS |
| test_system_bootstrap.py | 25 | PASS |
| test_integration.py | 10 | PASS |
| **Total** | **87** | **100%** |

## Hardware Target

- **Board**: Raspberry Pi 4 (BCM2711)
- **RAM**: 8GB
- **CPU**: Quad-core Cortex-A72 @ 1.8GHz
- **Storage**: 32GB SD card
- **UART**: PL011 (GPIO14/GPIO15)

## Related Documentation

- `phase2/docs/PHASE_2_KICKOFF.md` - Phase 2 goals
- `phase2/docs/PHASE_2_HARDWARE_PIVOT.md` - Pi 4 decision rationale
- `phase2/docs/UART_IPC_PROTOCOL.md` - Protocol specification
