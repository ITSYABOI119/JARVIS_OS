# IPC Ring Buffer Component

Lock-free Single-Producer Single-Consumer (SPSC) ring buffer for inter-process communication between Python AI agent and seL4 kernel.

## Overview

The ring buffer provides low-latency IPC using shared memory, achieving 54us median latency (46% under 100us target). It's the primary communication channel between the user-space AI and the kernel.

## Files

| File | Description |
|------|-------------|
| `ring_buffer.c` | SPSC ring buffer implementation |
| `ring_buffer.h` | API declarations and structures |
| `ipc_sel4.c` | seL4-specific IPC integration |
| `ipc_sel4.h` | seL4 IPC interface |
| `test_ipc.c` | Unit tests (6 tests) |

## Key Features

- **Lock-Free**: Uses atomic head/tail pointers, no mutex contention
- **Cache-Line Aligned**: 64-byte alignment prevents false sharing
- **Memory Barrier**: Ensures correct ordering on multi-core systems
- **Fixed-Size Messages**: 256-byte message slots for predictable performance

## API

```c
#include "ring_buffer.h"

// Create ring buffer in shared memory
ring_buffer_t* rb = ring_buffer_create("/jarvis_ipc", 1024);

// Producer: Send message
ring_message_t msg = {
    .type = MSG_QUERY,
    .id = 1,
    .payload_size = strlen("list files"),
};
strncpy(msg.payload, "list files", MAX_MESSAGE_SIZE);
ring_buffer_push(rb, &msg);

// Consumer: Receive message
ring_message_t recv;
if (ring_buffer_pop(rb, &recv)) {
    printf("Received: %s\n", recv.payload);
}

// Cleanup
ring_buffer_destroy(rb);
```

## Message Types

```c
typedef enum {
    MSG_QUERY     = 1,  // Cache lookup request
    MSG_RESPONSE  = 2,  // Cache lookup response
    MSG_COMMAND   = 3,  // Shell command
    MSG_EVENT     = 4,  // System event notification
    MSG_CONTROL   = 5   // Control message (shutdown, etc.)
} message_type_t;
```

## Message Structure

```c
typedef struct {
    message_type_t type;           // 4 bytes
    uint32_t id;                   // 4 bytes - Message ID
    uint32_t payload_size;         // 4 bytes
    char payload[MAX_MESSAGE_SIZE]; // 256 bytes
    uint64_t timestamp;            // 8 bytes
} ring_message_t;  // Total: 280 bytes (padded)
```

## Ring Buffer Structure

```c
typedef struct {
    volatile uint32_t head;        // Producer writes here
    char _pad1[60];                // Cache line padding
    volatile uint32_t tail;        // Consumer reads here
    char _pad2[60];                // Cache line padding
    uint32_t capacity;             // Number of slots
    uint32_t mask;                 // Capacity - 1 (for modulo)
    ring_message_t* buffer;        // Message array
} ring_buffer_t;
```

## Performance

| Metric | Value | Target |
|--------|-------|--------|
| Median latency | 54us | <100us |
| 99th percentile | 89us | - |
| Throughput | 18,500 msg/s | - |
| Memory per slot | 280 bytes | - |

## Shared Memory

The ring buffer uses POSIX shared memory (`/dev/shm`) for cross-process access:

```c
// Create shared memory segment
int fd = shm_open("/jarvis_ipc", O_CREAT | O_RDWR, 0666);
ftruncate(fd, sizeof(ring_buffer_t) + capacity * sizeof(ring_message_t));
void* ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
```

## Python Integration

The Python IPC client (`phase1/src/ai/ipc_client.py`) uses ctypes to access the same shared memory:

```python
import ctypes
import mmap

class RingMessage(ctypes.Structure):
    _pack_ = 1  # Match C struct packing
    _fields_ = [
        ("type", ctypes.c_uint32),
        ("id", ctypes.c_uint32),
        ("payload_size", ctypes.c_uint32),
        ("payload", ctypes.c_char * 256),
        ("timestamp", ctypes.c_uint64)
    ]
```

## Building

```bash
cd phase1/src/ipc
gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm -lrt
./test_ipc
```

## seL4 Integration

The `ipc_sel4.c` layer adapts the ring buffer for seL4's capability-based IPC:

```c
// Initialize IPC in seL4 rootserver
ipc_sel4_init();

// Main event loop
while (1) {
    ring_message_t msg;
    if (ipc_sel4_receive(&msg)) {
        // Process message
        cache_result_t result;
        cache_lookup(cache, msg.payload, &result);

        // Send response
        ipc_sel4_send(format_response(&result));
    }
}
```

## Related Documentation

- `phase1/PHASE_1_ARCHITECTURE.md` - System design
- `phase0/experiments/ipc_latency_benchmark.c` - Initial validation
- `phase1/src/ai/ipc_client.py` - Python client
- `phase2/src/ipc/` - Phase 2 dual ring buffer (UART version)
