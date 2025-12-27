# Phase 2 AI Components

Python AI components for Phase 2 Alpha System, designed for Raspberry Pi 4 with UART-based IPC.

## Components

### uart_ipc_client.py

UART serial IPC client for communication with seL4 kernel.

```python
from uart_ipc_client import UARTIPCClient

# Initialize client (connects to /dev/ttyUSB0 by default)
client = UARTIPCClient(port="/dev/ttyUSB0", baudrate=115200)

# Send cache query
response = client.send_query("list files in current directory")

# Check SHIELD risk
result = client.send_shield_check("rm -rf /")

# Get cache statistics
stats = client.get_cache_stats()
```

**Features:**
- CRC-16 CCITT error detection
- Automatic retransmission (3 retries)
- Heartbeat monitoring (5s interval)
- 30-second link timeout
- Frame sequencing and acknowledgment

### system_bootstrap.py

Unified initialization framework with graceful degradation.

```python
from system_bootstrap import SystemBootstrap, BootstrapConfig

# Configure bootstrap
config = BootstrapConfig(
    uart_port="/dev/ttyUSB0",
    enable_shield=True,
    enable_snapshots=True,
    ai_model="tinyllama"
)

# Initialize system
bootstrap = SystemBootstrap(config)
success = bootstrap.initialize()

# Get initialization status
status = bootstrap.get_status()
```

**Features:**
- Component dependency ordering
- Optional component skip on failure
- Initialization status tracking
- Rollback on critical failure

## Testing

```bash
# Run all AI component tests
cd phase2/src/ai
python3 test_uart_ipc_client.py      # 22 tests - UART protocol
python3 test_system_bootstrap.py     # 25 tests - Bootstrap framework
python3 test_integration.py          # 10 tests - Component integration
```

## UART Protocol Summary

### Frame Structure
- Sync: 0xAA55 (2 bytes)
- Type: Message type (1 byte)
- Sequence: Frame number (2 bytes)
- Length: Payload size (2 bytes)
- Flags: Reserved (1 byte)
- Payload: Data (0-240 bytes)
- CRC: CRC-16 CCITT (2 bytes)

### Performance
- Baud rate: 115200 (configurable to 230400)
- Typical query roundtrip: 10-20ms
- Heartbeat interval: 5 seconds
- Link timeout: 30 seconds

## Differences from Phase 1

| Phase 1 (x86/QEMU) | Phase 2 (ARM/Pi 4) |
|--------------------|--------------------|
| mmap shared memory | UART serial |
| 54us latency | 10-20ms latency |
| Phi-3 Mini 3.8B | TinyLlama 1.1B |
| Native build | Cross-compile |

## Dependencies

```
psutil>=5.9.0      # System monitoring
pyserial>=3.5      # UART communication (Phase 2 only)
```

## Hardware Connection

```
Pi 4 GPIO        USB-Serial Adapter
─────────        ──────────────────
GPIO14 (TXD) ──► RXD
GPIO15 (RXD) ◄── TXD
GND          ─── GND
```

## Related Documentation

- `../docs/UART_IPC_PROTOCOL.md` - Full protocol specification
- `../docs/PHASE_2_HARDWARE_PIVOT.md` - Hardware decision rationale
- `../../phase1/src/ai/` - Phase 1 AI components (reference)
