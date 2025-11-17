# JARVIS AI-OS: Phase 1 Technical Specification

**Version:** 1.0
**Date:** November 2025
**Phase:** Phase 1 - Proof of Concept (Months 6-12)
**Status:** DRAFT

---

## Document Purpose

This document provides detailed technical specifications for implementing Phase 1 of JARVIS AI-OS. It covers architecture, components, interfaces, data structures, and implementation details needed to build a working prototype in QEMU.

**Audience:** Developers implementing Phase 1
**Dependencies:** Phase 0 validation results, seL4 microkernel documentation

---

## System Architecture Overview

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    QEMU Virtual Machine                      │
├─────────────────────────────────────────────────────────────┤
│                                                               │
│  ┌────────────────────────────────────────────────────┐     │
│  │  Ring 3: User Space                                 │     │
│  │                                                      │     │
│  │  ┌──────────────────────────────────────────┐      │     │
│  │  │  AI Decision Engine (Phi-3 Mini 3.8B)    │      │     │
│  │  │  - Decision Cache (200 patterns)         │      │     │
│  │  │  - Single Agent (Device Manager)         │      │     │
│  │  │  - Python + llama-cpp-python             │      │     │
│  │  └──────────────────────────────────────────┘      │     │
│  │         ↕ Lock-Free IPC (<100μs)                    │     │
│  │  ┌──────────────────────────────────────────┐      │     │
│  │  │  User-Space Services                      │      │     │
│  │  │  - Text Shell                             │      │     │
│  │  │  - Command Parser                         │      │     │
│  │  └──────────────────────────────────────────┘      │     │
│  └────────────────────────────────────────────────────┘     │
│                                                               │
│  ┌────────────────────────────────────────────────────┐     │
│  │  Ring 0: seL4 Microkernel                          │     │
│  │  - Interrupt Handling (<1ms)                       │     │
│  │  - Memory Management                               │     │
│  │  - IPC Primitives                                  │     │
│  │  - Minimal Trusted Computing Base (~12K LOC)       │     │
│  └────────────────────────────────────────────────────┘     │
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

### Component Breakdown

**seL4 Microkernel (Ring 0):**
- Version: seL4 12.1.0 (or latest stable)
- CPU cores: 2 (isolated, no AI workload)
- Purpose: Time-critical operations (<1ms latency)
- Codebase: ~12,000 LOC (existing seL4 code)

**AI Decision Engine (Ring 3):**
- Model: Phi-3 Mini 3.8B Q4 (2.23GB)
- CPU cores: 4-6 (dedicated to AI, no interrupts)
- Memory: 8-12GB (model + cache + runtime)
- Purpose: High-level decision making (50-500ms acceptable)

**IPC Layer:**
- Type: Lock-free SPSC ring buffer
- Latency target: <100μs median, <500μs p99
- Throughput: 1M+ msg/sec (validated in Phase 0)
- Implementation: Shared memory + atomic operations

---

## Component 1: Decision Cache

### Purpose
Pre-compile 80% of common AI decisions into fast lookup patterns to avoid slow AI inference (558ms → 0.014ms for cached queries).

### Architecture

```
User Query: "show CPU usage"
     ↓
Normalize Query: "show_cpu_usage"
     ↓
Hash Function: SHA256(normalized_query) → hash
     ↓
Hash Table Lookup: cache[hash]
     ↓
  Found? → Execute cached bytecode (0.014ms)
  Not Found? → AI inference (558ms) → Cache result
```

### Data Structures

**Hash Table:**
```c
#define CACHE_SIZE 256  // Power of 2 for fast modulo

typedef struct {
    char normalized_query[128];     // "show_cpu_usage"
    char bytecode[512];              // Pre-compiled kernel command
    uint64_t hash;                   // SHA256(normalized_query)
    uint32_t hit_count;              // Usage statistics
    uint32_t flags;                  // Metadata
} CacheEntry;

typedef struct {
    CacheEntry entries[CACHE_SIZE];
    uint64_t total_queries;
    uint64_t cache_hits;
    uint64_t cache_misses;
} DecisionCache;
```

**Bytecode Format:**
```
Bytecode: Simple text-based commands for kernel
Examples:
  - "READ_CPU_STATS"
  - "READ_MEMORY_STATS"
  - "LIST_PROCESSES"
  - "READ_DISK_STATS device=/dev/sda"
```

### Implementation Phases

**Month 6 (Week 3-4):**
1. Implement hash table (256 entries, linear probing)
2. Create 50 initial patterns (most common commands)
3. Implement cache lookup function
4. Add cache miss → AI inference fallback
5. Measure hit rate on test workload

**Month 7-8:**
1. Expand to 200 patterns (from usage analysis)
2. Implement cache eviction policy (LRU)
3. Add cache warming (preload common queries at boot)
4. Optimize hash function (if needed)

**Target Metrics:**
- Hit rate: >80% (Phase 0 achieved 78.6%)
- Lookup time: <0.1ms (Phase 0 achieved <0.1ms)
- Speedup: 40,000x (Phase 0 validated)

### Cache Patterns (Examples)

**Pattern Categories:**
1. **System Monitoring** (40%):
   - "show CPU usage" → READ_CPU_STATS
   - "check memory" → READ_MEMORY_STATS
   - "disk space" → READ_DISK_STATS
   - "list processes" → LIST_PROCESSES

2. **Network Operations** (20%):
   - "network status" → READ_NETWORK_STATS
   - "ping google.com" → PING host=google.com
   - "check connection" → CHECK_NETWORK_CONNECTIVITY

3. **File Operations** (15%):
   - "list files" → LIST_FILES path=/
   - "read file" → READ_FILE path={path}
   - "create directory" → CREATE_DIR path={path}

4. **Process Management** (15%):
   - "kill process" → KILL_PROCESS pid={pid}
   - "start service" → START_SERVICE name={service}
   - "stop service" → STOP_SERVICE name={service}

5. **System Control** (10%):
   - "shutdown" → SHUTDOWN
   - "reboot" → REBOOT
   - "suspend" → SUSPEND

---

## Component 2: seL4 Microkernel Integration

### seL4 Configuration

**Platform:** x86_64
**Build:** Debug (Month 6-8), Release (Month 9+)
**Features:**
- Interrupt handling
- Memory management (CSpace, VSpace)
- IPC (Endpoints, Notifications)
- Threads (TCB)

**seL4 Build Configuration:**
```cmake
# CMakeLists.txt excerpt
set(KernelPlatform "x86_64" CACHE STRING "")
set(KernelSel4Arch "x86_64" CACHE STRING "")
set(KernelMaxNumNodes 2 CACHE STRING "")  # 2 cores for kernel
set(KernelDebugBuild ON CACHE BOOL "")    # Enable debugging
```

### Boot Sequence

**Phase 1: seL4 Boot (Months 6-8)**
```
1. QEMU loads seL4 ELF binary
2. seL4 kernel initialization
3. Create initial user task (root task)
4. Root task spawns AI agent process
5. AI agent loads Phi-3 Mini model
6. Text shell starts
7. System ready (boot complete <60s target)
```

### Memory Layout

```
Virtual Memory Layout (x86_64):

0xFFFF_FFFF_FFFF_FFFF  ┌─────────────────┐
                       │  Kernel Memory  │
0xFFFF_8000_0000_0000  ├─────────────────┤
                       │    (Unmapped)   │
0x0000_8000_0000_0000  ├─────────────────┤
                       │  AI Model       │ (8GB: Phi-3 Mini + runtime)
0x0000_4000_0000_0000  ├─────────────────┤
                       │  User Processes │ (4GB: shell, services)
0x0000_0000_4000_0000  ├─────────────────┤
                       │  IPC Buffers    │ (64MB: shared memory)
0x0000_0000_0000_0000  └─────────────────┘
```

### CPU Core Allocation

**Core 0-1: seL4 Microkernel**
- Handles all hardware interrupts
- Memory management
- IPC servicing
- No AI workload (isolated)

**Core 2-5: AI Decision Engine**
- Phi-3 Mini inference
- Decision cache lookups
- Agent coordination
- No interrupt handling

---

## Component 3: Lock-Free IPC

### Design (From Phase 0 Validation)

**Type:** Single-Producer Single-Consumer (SPSC) Ring Buffer
**Implementation:** Shared memory + atomic operations
**Latency:** 54.09μs median, 116.59μs p99 (Phase 0 validated in C)

### Data Structures

```c
#define RING_SIZE 1024  // Power of 2 for fast modulo
#define MESSAGE_SIZE 64 // Bytes per message

// Cache-line aligned (64 bytes) to prevent false sharing
typedef struct __attribute__((aligned(64))) {
    uint64_t head;  // Producer writes here
    uint64_t tail;  // Consumer reads from here
    uint8_t messages[RING_SIZE][MESSAGE_SIZE];
} RingBuffer;
```

### IPC Protocol

**Message Format:**
```c
typedef struct {
    uint32_t msg_type;      // READ_CPU_STATS, LIST_PROCESSES, etc.
    uint32_t request_id;    // For matching request/response
    uint32_t payload_len;   // Length of payload data
    uint8_t payload[52];    // Flexible payload (52 bytes)
} IPCMessage;
```

**Message Types:**
```c
enum MessageType {
    MSG_READ_CPU_STATS      = 1,
    MSG_READ_MEMORY_STATS   = 2,
    MSG_LIST_PROCESSES      = 3,
    MSG_READ_DISK_STATS     = 4,
    MSG_EXECUTE_COMMAND     = 5,
    MSG_RESPONSE            = 100,
    MSG_ERROR               = 200
};
```

### Implementation

**Producer (AI Agent → Kernel):**
```c
bool ring_buffer_send(RingBuffer *rb, IPCMessage *msg) {
    uint64_t head = __atomic_load_n(&rb->head, __ATOMIC_ACQUIRE);
    uint64_t next_head = (head + 1) % RING_SIZE;
    uint64_t tail = __atomic_load_n(&rb->tail, __ATOMIC_ACQUIRE);

    // Check if buffer full
    if (next_head == tail) {
        return false;  // Buffer full
    }

    // Copy message
    memcpy(rb->messages[head], msg, MESSAGE_SIZE);

    // Update head atomically
    __atomic_store_n(&rb->head, next_head, __ATOMIC_RELEASE);
    return true;
}
```

**Consumer (Kernel → AI Agent):**
```c
bool ring_buffer_receive(RingBuffer *rb, IPCMessage *msg) {
    uint64_t tail = __atomic_load_n(&rb->tail, __ATOMIC_ACQUIRE);
    uint64_t head = __atomic_load_n(&rb->head, __ATOMIC_ACQUIRE);

    // Check if buffer empty
    if (tail == head) {
        return false;  // Buffer empty
    }

    // Copy message
    memcpy(msg, rb->messages[tail], MESSAGE_SIZE);

    // Update tail atomically
    uint64_t next_tail = (tail + 1) % RING_SIZE;
    __atomic_store_n(&rb->tail, next_tail, __ATOMIC_RELEASE);
    return true;
}
```

---

## Component 4: AI Agent (Single Agent - Month 6-8)

### Agent Architecture

**Agent Type:** Device Manager (Month 6-8 focus)
**Purpose:** Handle system monitoring and basic device queries

**Agent Structure:**
```python
class DeviceManagerAgent:
    def __init__(self):
        self.model = Llama(model_path="phi-3-mini-4k.gguf",
                          n_ctx=2048, n_gpu_layers=35)
        self.decision_cache = DecisionCache()
        self.ipc_buffer = SharedMemory("ipc_buffer")

    def process_query(self, user_query: str) -> str:
        # 1. Check decision cache first
        cached = self.decision_cache.lookup(user_query)
        if cached:
            return self.execute_cached(cached)

        # 2. Cache miss - use AI inference
        response = self.model(
            f"System query: {user_query}\nGenerate command:",
            max_tokens=50,
            temperature=0.7
        )

        # 3. Parse AI response into kernel command
        command = self.parse_ai_response(response)

        # 4. Send to kernel via IPC
        result = self.send_ipc_command(command)

        # 5. Cache for future use
        self.decision_cache.insert(user_query, command)

        return result
```

### AI Model Configuration

**Model:** Phi-3 Mini 3.8B Q4
**Path:** `/models/Phi-3-mini-4k-instruct-q4.gguf`
**Size:** 2.23 GB
**VRAM:** ~2.5 GB (when loaded on GPU)

**llama-cpp-python Configuration:**
```python
model = Llama(
    model_path="phi-3-mini-4k.gguf",
    n_ctx=2048,           # Context window
    n_gpu_layers=35,      # Offload all layers to GPU
    n_threads=4,          # CPU threads for host operations
    verbose=False
)
```

**Inference Parameters:**
```python
response = model(
    prompt=user_query,
    max_tokens=50,        # Short responses for commands
    temperature=0.7,      # Some randomness (not too creative)
    top_p=0.9,            # Nucleus sampling
    stop=["\\n"],         # Stop at newline
    echo=False            # Don't return prompt
)
```

---

## Component 5: Text Shell Interface

### Shell Architecture

**Type:** Simple text-based REPL (Read-Eval-Print Loop)
**Interface:** Serial console (QEMU -serial stdio)

**Shell Loop:**
```python
def shell_main_loop():
    agent = DeviceManagerAgent()

    print("JARVIS AI-OS v0.1 (Phase 1)")
    print("Type 'help' for commands, 'exit' to quit")

    while True:
        # Read user input
        user_input = input("jarvis> ")

        if user_input == "exit":
            break
        elif user_input == "help":
            print_help()
            continue

        # Process query through AI agent
        try:
            result = agent.process_query(user_input)
            print(result)
        except Exception as e:
            print(f"Error: {e}")
```

### Command Set (Month 6-8)

**Basic Commands (20 minimum for Phase 1 gate):**
1. `show cpu` - CPU usage statistics
2. `show memory` - Memory statistics
3. `show disk` - Disk usage
4. `list processes` - Running processes
5. `network status` - Network connectivity
6. `ping <host>` - Ping remote host
7. `list files` - List files in directory
8. `read file <path>` - Display file contents
9. `create dir <path>` - Create directory
10. `shutdown` - Shutdown system
11. `reboot` - Reboot system
12. `help` - Show help
13. `exit` - Exit shell
14. `cache stats` - Show decision cache statistics
15. `agent status` - Show AI agent status
16. `system info` - System information
17. `uptime` - System uptime
18. `load average` - System load
19. `temperature` - CPU temperature
20. `battery` - Battery status (if applicable)

---

## Performance Targets

### Phase 1 Performance Requirements

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| **IPC Latency (median)** | <100μs | Measure with high-res timer in seL4 |
| **IPC Latency (p99)** | <500μs | Measure 100,000 messages, sort, p99 |
| **Decision Cache Hit Rate** | >80% | (cache_hits / total_queries) × 100 |
| **Decision Cache Lookup** | <0.1ms | Measure hash lookup time |
| **AI Inference (uncached)** | <600ms | Measure Phi-3 Mini inference time |
| **Boot Time** | <60s | From QEMU start to shell prompt |
| **AI Response (cached)** | <2s | User query to shell response |
| **AI Response (uncached)** | <3s | User query to shell response (AI) |
| **Memory Usage** | <12GB | Total RSS of all processes |
| **Stability** | 24+ hours | Zero crashes, zero deadlocks |

---

## Testing Strategy

### Unit Tests

**Decision Cache:**
- Test hash function (no collisions for common patterns)
- Test cache insert/lookup
- Test cache eviction (LRU)
- Test hit rate on sample workload

**IPC:**
- Test ring buffer send/receive
- Test buffer full/empty conditions
- Test latency (median, p99)
- Test message integrity

**AI Agent:**
- Test query parsing
- Test command generation
- Test error handling
- Test failover to cache

### Integration Tests

**End-to-End:**
- Boot JARVIS in QEMU
- Execute 20 test commands
- Verify all responses correct
- Measure latency for each

**Stability:**
- 24-hour continuous operation
- Random command generation
- Monitor for crashes, deadlocks
- Log any errors

### Performance Tests

**IPC Latency:**
```bash
# Run 100,000 IPC round-trips
for i in {1..100000}; do
    ipc_send_receive
done | calculate_latency_stats
```

**Decision Cache Hit Rate:**
```bash
# Run 1000 diverse queries
for query in test_queries.txt; do
    process_query "$query"
done | calculate_hit_rate
```

---

## Development Milestones

### Month 6 Milestones
- [x] QEMU environment setup
- [ ] seL4 boots to serial console
- [ ] Decision cache implementation (50 patterns)
- [ ] Basic IPC working (ping/pong test)

### Month 7-8 Milestones
- [ ] AI agent loads Phi-3 Mini
- [ ] AI agent responds to queries
- [ ] Decision cache expanded (200 patterns)
- [ ] Text shell functional (20 commands)

### Month 9-10 Milestones
- [ ] Decision cache hit rate >80%
- [ ] IPC latency <100μs median
- [ ] 24-hour stability test passed

### Month 11 Milestones
- [ ] Power management (S3 suspend/resume)
- [ ] Boot time <60 seconds
- [ ] All Phase 1 gate criteria met

### Month 12 Milestones
- [ ] Integration testing complete
- [ ] Phase 1 demo to stakeholders
- [ ] Phase 1 final report

---

## Appendices

### Appendix A: seL4 Resources
- seL4 Manual: https://sel4.systems/Info/Docs/seL4-manual-latest.pdf
- seL4 Tutorials: https://docs.sel4.systems/Tutorials/
- seL4 Source: https://github.com/seL4/seL4

### Appendix B: Build Tools
- CMake 3.15+
- Ninja build system
- GCC cross-compiler (x86_64-elf)
- Python 3.11+
- QEMU 7.0+

### Appendix C: Debugging Tools
- GDB (with QEMU remote debugging)
- seL4 kernel logging
- llama-cpp-python verbose mode
- Python debugger (pdb)

---

**Document Version:** 1.0
**Status:** DRAFT
**Next Review:** Month 7 (after first milestone)
**Maintainer:** JARVIS AI-OS Team
