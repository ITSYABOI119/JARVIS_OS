# JARVIS AI-OS: Phase 1 Architecture

**Version:** 1.0
**Date:** November 2025
**Phase:** Phase 1 - Proof of Concept
**Status:** SPECIFICATION

---

## Overview

This document defines the complete system architecture for JARVIS AI-OS Phase 1. It includes system diagrams, component specifications, interfaces, data flows, and memory layouts.

**Phase 1 Scope:** Single AI agent (Device Manager) in QEMU virtual machine with decision cache and basic IPC.

**Full Multi-Agent:** Deferred to later weeks (Weeks 10-12)

---

## System Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                       QEMU Virtual Machine                           │
│                     (x86_64, 8GB RAM, 4 cores)                       │
├─────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    Ring 3: User Space                        │   │
│  │                                                               │   │
│  │  ┌──────────────────────────────────────────────┐           │   │
│  │  │  Text Shell (Python)                          │           │   │
│  │  │  - REPL loop                                  │           │   │
│  │  │  - User input parsing                         │           │   │
│  │  │  - Response formatting                        │           │   │
│  │  └──────────────┬───────────────────────────────┘           │   │
│  │                 │ IPC Messages                               │   │
│  │  ┌──────────────▼───────────────────────────────┐           │   │
│  │  │  AI Decision Engine                           │           │   │
│  │  │                                                │           │   │
│  │  │  ┌──────────────────────────────────┐        │           │   │
│  │  │  │  Decision Cache                   │        │           │   │
│  │  │  │  - Hash table (256 entries)       │        │           │   │
│  │  │  │  - 200 pre-compiled patterns      │        │           │   │
│  │  │  │  - Hit rate: >80%                 │        │           │   │
│  │  │  └──────┬────────────────────────────┘        │           │   │
│  │  │         │ Cache Miss                          │           │   │
│  │  │  ┌──────▼────────────────────────────┐        │           │   │
│  │  │  │  AI Agent (Device Manager)         │        │           │   │
│  │  │  │  - Phi-3 Mini 3.8B Q4              │        │           │   │
│  │  │  │  - llama-cpp-python (CUDA)         │        │           │   │
│  │  │  │  - Inference: ~558ms               │        │           │   │
│  │  │  └────────────────────────────────────┘        │           │   │
│  │  └──────────────┬───────────────────────────────┘           │   │
│  └─────────────────┼───────────────────────────────────────────┘   │
│                    │ IPC (Lock-Free Ring Buffer)                    │
│                    │ Latency: <100μs                                │
│  ┌─────────────────▼───────────────────────────────────────────┐   │
│  │                    Ring 0: seL4 Microkernel                   │   │
│  │                                                                │   │
│  │  - Interrupt handling (<1ms)                                  │   │
│  │  - Memory management (CSpace, VSpace)                         │   │
│  │  - IPC primitives (Endpoints, Notifications)                  │   │
│  │  - Thread management (TCB)                                    │   │
│  │  - Hardware abstraction                                       │   │
│  │                                                                │   │
│  │  CPU cores: 0-1 (isolated, no AI workload)                    │   │
│  └────────────────────────────────────────────────────────────────┘   │
│                                                                       │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │                    Hardware (Virtualized)                       │   │
│  │  - NVMe storage (virtual disk)                                 │   │
│  │  - Serial console (stdio)                                      │   │
│  │  - Framebuffer (simple text mode)                              │   │
│  └────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Component Architecture

### Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        User Space                                │
│                                                                   │
│  ┌─────────────┐         ┌──────────────────┐                   │
│  │ Text Shell  │────────▶│ AI Decision      │                   │
│  │  (Python)   │         │ Engine (Python)  │                   │
│  └─────────────┘         └────────┬─────────┘                   │
│        ▲                           │                             │
│        │ Response                  │ Query                       │
│        │                           ▼                             │
│        │                  ┌─────────────────┐                   │
│        │                  │ Decision Cache  │                   │
│        │                  │  (Hash Table)   │                   │
│        │                  └────────┬────────┘                   │
│        │                           │ Cache Miss                  │
│        │                           ▼                             │
│        │                  ┌─────────────────┐                   │
│        │                  │  AI Agent       │                   │
│        │                  │  (Phi-3 Mini)   │                   │
│        │                  └────────┬────────┘                   │
│        │                           │                             │
│        │                           │ Kernel Command              │
│        │                           ▼                             │
│  ┌─────┴─────────────────────────────────────────────────┐     │
│  │           IPC Layer (Shared Memory)                    │     │
│  │  - Lock-Free SPSC Ring Buffer                          │     │
│  │  - 1024 messages × 64 bytes                            │     │
│  │  - Cache-line aligned (64 bytes)                       │     │
│  └────────────────────┬───────────────────────────────────┘     │
└───────────────────────┼─────────────────────────────────────────┘
                        │
┌───────────────────────▼─────────────────────────────────────────┐
│                   Kernel Space                                   │
│                                                                   │
│  ┌─────────────────────────────────────────────────┐            │
│  │          seL4 Microkernel                        │            │
│  │                                                   │            │
│  │  ┌──────────────┐  ┌──────────────┐            │            │
│  │  │ IPC Handler  │  │ Interrupt    │            │            │
│  │  │              │  │ Handler      │            │            │
│  │  └──────┬───────┘  └──────────────┘            │            │
│  │         │                                        │            │
│  │         ▼                                        │            │
│  │  ┌──────────────┐  ┌──────────────┐            │            │
│  │  │ Command      │  │ Memory Mgmt  │            │            │
│  │  │ Dispatcher   │  │ (CSpace/VSpace)           │            │
│  │  └──────┬───────┘  └──────────────┘            │            │
│  │         │                                        │            │
│  │         ▼                                        │            │
│  │  ┌──────────────────────────────┐              │            │
│  │  │  Hardware Abstraction Layer  │              │            │
│  │  └──────────────────────────────┘              │            │
│  └─────────────────────────────────────────────────┘            │
│                          │                                       │
│                          ▼                                       │
│  ┌─────────────────────────────────────────────────┐            │
│  │          Hardware Drivers (User Space)          │            │
│  │  - NVMe (disk I/O)                               │            │
│  │  - Serial Console                                │            │
│  │  - Framebuffer                                   │            │
│  └─────────────────────────────────────────────────┘            │
└───────────────────────────────────────────────────────────────────┘
```

---

## Memory Layout

### Virtual Memory Map (x86_64)

```
Virtual Address Space (48-bit address space on x86_64)

0xFFFF_FFFF_FFFF_FFFF  ┌────────────────────────────────┐
                       │ seL4 Kernel Memory              │
                       │  - Kernel code/data             │
                       │  - Page tables                  │
                       │  - TCBs, Endpoints, etc.        │
0xFFFF_8000_0000_0000  ├────────────────────────────────┤
                       │                                 │
                       │ (Unmapped - Canon ical Gap)     │
                       │                                 │
0x0000_8000_0000_0000  ├────────────────────────────────┤
                       │ AI Model Memory                 │
                       │  - Phi-3 Mini weights: 2.23GB   │
                       │  - Model context: 2GB           │
                       │  - llama-cpp runtime: 1GB       │
                       │  Total: ~6GB                    │
0x0000_6000_0000_0000  ├────────────────────────────────┤
                       │ Decision Cache                  │
                       │  - Hash table: 256 entries      │
                       │  - Pattern storage: ~100MB      │
                       │  - Statistics: ~1MB             │
                       │  Total: ~128MB                  │
0x0000_5F00_0000_0000  ├────────────────────────────────┤
                       │ User Process Memory             │
                       │  - Text Shell: ~50MB            │
                       │  - AI Agent: ~200MB             │
                       │  - Python runtime: ~100MB       │
                       │  Total: ~512MB                  │
0x0000_5E00_0000_0000  ├────────────────────────────────┤
                       │ IPC Shared Memory               │
                       │  - Ring buffers: 64MB           │
                       │  - Control structures: 1MB      │
                       │  Total: 64MB                    │
0x0000_5C00_0000_0000  ├────────────────────────────────┤
                       │ Stack                           │
                       │  - Main thread: 8MB             │
                       │  - AI agent threads: 16MB       │
                       │  Total: 32MB                    │
0x0000_4000_0000_0000  ├────────────────────────────────┤
                       │ Heap                            │
                       │  - Dynamic allocations          │
                       │  - Python objects               │
                       │  Total: 512MB                   │
0x0000_2000_0000_0000  ├────────────────────────────────┤
                       │ Library Mappings                │
                       │  - libc, Python libs, etc.      │
                       │  Total: ~256MB                  │
0x0000_1000_0000_0000  ├────────────────────────────────┤
                       │ Program Code                    │
                       │  - Text section                 │
                       │  Total: ~64MB                   │
0x0000_0000_0040_0000  ├────────────────────────────────┤
                       │ Null Guard Page                 │
0x0000_0000_0000_0000  └────────────────────────────────┘

Total User Space Memory: ~8GB (fits in 12GB QEMU VM allocation)
```

### Physical Memory (QEMU)

```
Physical RAM: 8-12GB

┌────────────────────────────────────┐ 0xFFFFFFFF (4GB, if PAE)
│  Kernel Reserved                   │ or higher on 64-bit
│   - seL4 kernel: ~12MB             │
├────────────────────────────────────┤
│  AI Model (Phi-3 Mini)             │
│   - Model weights: 2.23GB          │
│   - Context buffers: 2GB           │
├────────────────────────────────────┤
│  User Space Processes              │
│   - Shell, AI agent: ~512MB        │
├────────────────────────────────────┤
│  IPC Buffers                       │
│   - Shared memory: 64MB            │
├────────────────────────────────────┤
│  Free Memory / Cache               │
│   - Available: 2-4GB               │
└────────────────────────────────────┘ 0x00000000
```

---

## Component Specifications

### 1. Decision Cache

**Purpose:** Pre-compiled pattern matching to avoid AI inference for common queries.

**Data Structure:**
```c
#define CACHE_SIZE 256  // Hash table size
#define PATTERN_COUNT 200  // Number of pre-compiled patterns

typedef struct {
    char normalized_query[128];      // "show_cpu_usage"
    char bytecode[512];               // "READ_CPU_STATS"
    uint64_t hash;                    // SHA256 hash
    uint32_t hit_count;               // Usage statistics
    uint32_t last_access_time;        // LRU tracking
    uint32_t flags;                   // Metadata
} CacheEntry;

typedef struct {
    CacheEntry entries[CACHE_SIZE];
    uint64_t total_queries;
    uint64_t cache_hits;
    uint64_t cache_misses;
    double hit_rate;                  // Calculated metric
} DecisionCache;
```

**Operations:**
- `cache_lookup(query)` → O(1) average, O(n) worst-case
- `cache_insert(query, bytecode)` → O(1)
- `cache_evict_lru()` → O(n) (called when full)

**Performance:**
- Lookup time: <0.1ms (target)
- Hit rate: >80% (target, Phase 0 achieved 78.6%)
- Memory usage: ~128MB

---

### 2. IPC Layer (Lock-Free Ring Buffer)

**Purpose:** Fast communication between user-space AI and kernel.

**Design:** Single-Producer Single-Consumer (SPSC)

**Data Structure:**
```c
#define RING_SIZE 1024          // Must be power of 2
#define MESSAGE_SIZE 64         // Bytes per message

typedef struct __attribute__((aligned(64))) {
    // Producer writes to head (cache-line aligned)
    uint64_t head;
    uint8_t padding1[56];       // Pad to 64 bytes

    // Consumer reads from tail (separate cache line)
    uint64_t tail;
    uint8_t padding2[56];       // Pad to 64 bytes

    // Message buffer
    uint8_t messages[RING_SIZE][MESSAGE_SIZE];
} RingBuffer;
```

**Message Format:**
```c
typedef struct {
    uint32_t msg_type;          // Command type
    uint32_t request_id;        // For request/response matching
    uint32_t payload_len;       // Payload size
    uint8_t  payload[52];       // Flexible payload
} IPCMessage;
```

**Operations:**
- `ring_send(msg)` → O(1), lock-free
- `ring_receive(msg)` → O(1), lock-free
- Latency: <100μs median (target, Phase 0 validated 54μs)

---

### 3. AI Agent (Device Manager)

**Purpose:** Process user queries and generate kernel commands.

**Architecture:**
```python
class DeviceManagerAgent:
    def __init__(self):
        # Load Phi-3 Mini model
        self.model = Llama(
            model_path="phi-3-mini-4k.gguf",
            n_ctx=2048,
            n_gpu_layers=35,  # Full GPU offload
            verbose=False
        )

        # Decision cache
        self.cache = DecisionCache()

        # IPC connection
        self.ipc = SharedMemory("ipc_buffer")

    def process_query(self, query: str) -> str:
        # 1. Normalize query
        normalized = self.normalize(query)

        # 2. Check cache
        cached = self.cache.lookup(normalized)
        if cached:
            return self.execute_cached(cached)

        # 3. AI inference (cache miss)
        command = self.infer_command(normalized)

        # 4. Cache result
        self.cache.insert(normalized, command)

        # 5. Execute
        return self.execute_command(command)
```

**Performance:**
- Cache hit: <0.1ms (instant)
- Cache miss: ~558ms (Phi-3 Mini inference)
- Combined (80% hit): 0.8 × 0.1ms + 0.2 × 558ms = ~112ms average

---

### 4. seL4 Microkernel

**Configuration:**
- **Platform:** x86_64 PC99
- **CPU cores:** 2 (cores 0-1, isolated from AI)
- **Memory:** Managed via CSpace and VSpace
- **IPC:** Endpoints and Notifications

**Key Capabilities:**
- **Interrupt latency:** <1ms (seL4 standard)
- **Context switch:** <10μs (seL4 proven)
- **IPC (syscall):** ~1-2μs (seL4 base), <100μs (our ring buffer on top)

**Kernel Objects:**
```
- TCB (Thread Control Block): Per-thread state
- Endpoint: Synchronous IPC
- Notification: Asynchronous signaling
- CNode: Capability space node
- Page Table: Virtual memory mapping
```

---

## Data Flow Diagrams

### Query Processing Flow

```
User Types: "show cpu"
        │
        ▼
┌───────────────────┐
│  Text Shell       │
│  - Read input     │
└────────┬──────────┘
         │ IPC: QUERY_REQUEST
         ▼
┌───────────────────────┐
│  AI Decision Engine   │
│  1. Normalize query   │ "show cpu" → "show_cpu_usage"
└────────┬──────────────┘
         │
         ▼
    ┌────────┐
    │ Cache? │───YES──▶ Cached bytecode: "READ_CPU_STATS"
    └───┬────┘                   │
        │ NO                     │
        │ (Cache Miss)           │
        ▼                        │
┌───────────────────────┐        │
│  Phi-3 Mini Inference │        │
│  (~558ms)             │        │
│  Input: "show cpu"    │        │
│  Output: command      │        │
└────────┬──────────────┘        │
         │                       │
         │ Parse & Validate      │
         │                       │
         ▼                       │
    Insert into cache            │
         │                       │
         ├───────────────────────┘
         │
         ▼
┌───────────────────────┐
│  Send via IPC         │
│  Message: READ_CPU_STATS
└────────┬──────────────┘
         │
         ▼
┌───────────────────────┐
│  seL4 Kernel          │
│  - Receive IPC        │
│  - Dispatch command   │
│  - Execute handler    │
└────────┬──────────────┘
         │
         ▼
┌───────────────────────┐
│  System Call          │
│  - Read /proc/stat    │
│  - Parse CPU data     │
└────────┬──────────────┘
         │
         ▼ IPC: RESPONSE
┌───────────────────────┐
│  AI Agent receives    │
│  - Format response    │
└────────┬──────────────┘
         │
         ▼ IPC: DISPLAY
┌───────────────────────┐
│  Text Shell           │
│  - Print to console   │
└───────────────────────┘
         │
         ▼
User sees: "CPU Usage: 25.3%"
```

**Latency Breakdown:**
- User input → Shell: ~1ms
- Shell → AI Engine (IPC): <0.1ms
- Cache lookup: <0.1ms (hit) OR AI inference ~558ms (miss)
- Command → Kernel (IPC): <0.1ms
- Kernel execute: ~1-10ms (depends on operation)
- Response → Shell (IPC): <0.1ms
- **Total (cached):** ~2-12ms (meets <2s target) ✅
- **Total (uncached):** ~560-570ms (meets <3s target) ✅

---

## CPU Core Allocation

### Core Assignment

```
┌─────────────────────────────────────┐
│  Physical CPU: AMD Ryzen 2700X      │
│  8 cores @ 3.2 GHz                  │
└─────────────────────────────────────┘
         │
         ▼
┌─────────────────────────────────────┐
│  QEMU VM Allocation: 4 cores        │
├─────────────────────────────────────┤
│                                     │
│  Core 0-1: seL4 Microkernel         │
│  - Interrupt handling               │
│  - IPC servicing                    │
│  - Memory management                │
│  - NO AI workload (isolated)        │
│                                     │
│  Core 2-3: AI Decision Engine       │
│  - Phi-3 Mini inference             │
│  - Decision cache lookups           │
│  - Query processing                 │
│  - NO interrupt handling            │
│                                     │
└─────────────────────────────────────┘
```

**Why Isolation?**
- Kernel cores (0-1): Need predictable latency for interrupts
- AI cores (2-3): Can tolerate latency variation
- Prevents AI workload from interfering with time-critical operations

---

## Interface Specifications

### 1. IPC Message Protocol

**Message Types:**
```c
enum MessageType {
    // Query messages (Shell → AI Agent)
    MSG_QUERY_REQUEST       = 1,
    MSG_QUERY_RESPONSE      = 2,

    // Kernel command messages (AI Agent → Kernel)
    MSG_READ_CPU_STATS      = 10,
    MSG_READ_MEMORY_STATS   = 11,
    MSG_LIST_PROCESSES      = 12,
    MSG_READ_DISK_STATS     = 13,
    MSG_EXECUTE_COMMAND     = 14,

    // Response messages (Kernel → AI Agent)
    MSG_RESPONSE_OK         = 100,
    MSG_RESPONSE_ERROR      = 101,

    // Control messages
    MSG_SHUTDOWN            = 200,
    MSG_REBOOT              = 201,
    MSG_PING                = 250,
    MSG_PONG                = 251
};
```

**Message Flow Examples:**

**Example 1: CPU Stats**
```
Shell → AI Agent:
  { type: MSG_QUERY_REQUEST, request_id: 1,
    payload: "show cpu" }

AI Agent → Kernel:
  { type: MSG_READ_CPU_STATS, request_id: 1,
    payload: "" }

Kernel → AI Agent:
  { type: MSG_RESPONSE_OK, request_id: 1,
    payload: "cpu_usage=25.3%" }

AI Agent → Shell:
  { type: MSG_QUERY_RESPONSE, request_id: 1,
    payload: "CPU Usage: 25.3%" }
```

---

## Boot Sequence

### Phase 1 Boot Flow

```
Time 0s:  QEMU starts
          │
          ▼
Time 0-2s: seL4 Kernel Boot
          │ - Initialize memory management
          │ - Set up page tables
          │ - Create initial task (root task)
          │ - Configure interrupts
          ▼
Time 2-3s: Root Task Spawns User Processes
          │ - Spawn AI agent process
          │ - Spawn shell process
          │ - Set up IPC shared memory
          ▼
Time 3-5s: AI Agent Initialization
          │ - Load Phi-3 Mini model (~1.5s)
          │ - Initialize decision cache (~0.1s)
          │ - Connect to IPC (~0.1s)
          ▼
Time 5-6s: Shell Initialization
          │ - Start REPL loop
          │ - Print boot message
          │ - Display prompt
          ▼
Time 6s:  Boot Complete
          │
          ▼
         "jarvis> " (ready for input)
```

**Boot Time:** ~6 seconds (well under 60s target) ✅

---

## Security Model

### Trust Boundaries

```
┌─────────────────────────────────────────┐
│  Untrusted: User Input                   │
│  - Text shell input                      │
│  - Natural language queries              │
└──────────────┬──────────────────────────┘
               │ Sanitize, validate
               ▼
┌─────────────────────────────────────────┐
│  Semi-Trusted: AI Decision Engine        │
│  - Can generate commands                 │
│  - SHIELD validation (Phase 1 partial)   │
└──────────────┬──────────────────────────┘
               │ Command validation
               ▼
┌─────────────────────────────────────────┐
│  Trusted: seL4 Microkernel               │
│  - Formally verified                     │
│  - Minimal TCB (~12K LOC)                │
│  - Executes validated commands only      │
└─────────────────────────────────────────┘
```

**SHIELD Integration (Partial in Phase 1):**
- Risk scoring for commands
- Shadow execution for dangerous operations
- Approval required for high-risk commands

---

## Scalability Plan

### Evolution Path

**Phase 1 (Month 6-12):**
- Single AI agent (Device Manager)
- Decision cache (200 patterns)
- Basic IPC
- QEMU only

**Phase 2 (Month 12-24):**
- 4 specialist agents
- Shared context pool
- Conflict resolution
- Real hardware (3 configs)

**Phase 3 (Month 24-30):**
- Full multi-agent (6+ agents)
- Advanced learning
- Self-modification
- 10+ hardware configs

**Architecture is designed to scale:**
- IPC throughput: 1M+ msg/sec (tested in Phase 0)
- Agent addition: Just spawn new process, connect to IPC
- Decision cache: Can expand to 1000+ patterns

---

**Document Version:** 1.0
**Status:** SPECIFICATION
**Next Review:** Week 4 (after seL4 integration)
**Maintainer:** JARVIS AI-OS Team
