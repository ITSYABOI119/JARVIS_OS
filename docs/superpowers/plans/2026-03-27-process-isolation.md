# Process Isolation: Split Rootserver into Two seL4 Processes

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the monolithic JARVIS rootserver into two isolated seL4 processes — Process A (rootserver: cache, SHIELD, drivers, IPC) and Process B (inference server: model loading, quantized forward pass, tokenizer, generation) — connected via shared memory IPC, resolving SEC-014.

**Architecture:** The rootserver (Process A) spawns a child process (Process B) from a CPIO-archived ELF binary using seL4's `sel4utils_configure_process` + `sel4utils_spawn_process`. They share two 4KB pages (request ring + response ring) mapped into both VSpaces via `vspace_share_mem`. Process B has the 771MB model embedded in its own .rodata. A seL4_Notification capability signals Process B when a request arrives.

**Tech Stack:** seL4 x86-64 PC99, sel4utils process API, CPIO archive for child ELF, shmem_ipc ring buffers, QEMU+KVM.

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                    seL4 Microkernel (Ring 0)                     │
│              CNode=19 (524K slots), 4GB QEMU+KVM                │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌───────────────────────┐    shared    ┌──────────────────────┐│
│  │  Process A: Rootserver│    memory    │ Process B: Inference  ││
│  │  (sel4test-driver)    │◄════════════►│ (jarvis-inference)    ││
│  │                       │  2×4KB pages │                       ││
│  │  • Decision cache     │  + Notif cap │  • llama_quant.c     ││
│  │  • SHIELD (keyword)   │             │  • gguf_parser.c     ││
│  │  • Shell / IPC handler│  Request →  │  • dequant.c         ││
│  │  • x86 drivers        │  ← Response │  • tensor_ops.c      ││
│  │  • model_scaling.c    │             │  • tokenizer.c       ││
│  │                       │             │  • sampling.c        ││
│  │  CSpace A │ VSpace A  │             │  • gguf_vocab.c      ││
│  │  ~5 MB heap           │             │  • 771MB .rodata     ││
│  └───────────────────────┘             │  • inference_server.c││
│                                         │                       ││
│                                         │  CSpace B │ VSpace B  ││
│                                         │  ~50 MB heap          ││
│                                         └──────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

## Approach: CPIO-Archived Child Process (sel4test Pattern)

**Recommended approach:** Follow the exact pattern used by `sel4test`:
1. Build Process B (`jarvis-inference`) as a separate ELF executable
2. Pack it into a CPIO archive using `MakeCPIO`
3. Link the CPIO archive into Process A (`sel4test-driver`)
4. At runtime, Process A uses `sel4utils_configure_process` to load Process B from the CPIO
5. Map shared memory pages into both VSpaces with `vspace_share_mem`
6. Copy a Notification capability to Process B for IPC signaling
7. Start Process B with `sel4utils_spawn_process_v`

**Why this approach:**
- Proven pattern (sel4test uses it for every test)
- Real process isolation: separate CSpace, VSpace, TCB
- `sel4utils` handles all the complex capability setup
- CPIO embedding is built into the seL4 CMake system (`MakeCPIO`)
- Model embedding via objcopy goes into Process B's ELF (clean separation)

**Why NOT other approaches:**
- Thread-based: doesn't solve SEC-014 (shared address space)
- Manual TCB/CNode creation: error-prone, duplicates sel4utils
- Multiboot modules: seL4 only supports one rootserver image

---

## seL4 API Calls (Sequence)

### Process A (rootserver) init:

```
1. platsupport_get_bootinfo()           — get boot info
2. simple_default_init_bootinfo()       — init simple interface
3. allocman_create_bootstrap()          — create allocator
4. vka_alloc_notification()             — create Notification for IPC signaling
5. sel4utils_configure_process_custom() — create Process B from CPIO ELF
6. vspace_share_mem()                   — map shared pages into Process B
7. sel4utils_copy_cap_to_process()      — give Notification cap to Process B
8. sel4utils_spawn_process_v()          — start Process B
9. seL4_Signal(notification)            — wake Process B when request available
10. seL4_Wait(response_notification)    — wait for response
```

### Process B (inference server) main:

```
1. Receive Notification cap + shared memory vaddr via argv
2. Init shmem_ipc ring buffers at shared vaddr
3. Load model from embedded .rodata (gguf_open_memory + qmodel_load)
4. Extract vocab (gguf_vocab_extract + tokenizer_init)
5. Main loop:
   a. seL4_Wait(request_notification)   — sleep until signaled
   b. shmem_ipc_recv(&request_ring)     — read request
   c. Process: tokenize → qmodel_generate → decode
   d. shmem_ipc_send(&response_ring)    — write response
   e. seL4_Signal(response_notification) — wake Process A
```

---

## File Plan

### New Files to Create

| File | Process | Purpose |
|------|---------|---------|
| `phase3/src/sel4/inference_server.c` | B | Main loop: wait for IPC, run inference, send response |
| `phase3/src/sel4/inference_server.h` | B | Process B entry point and IPC protocol |
| `phase3/src/sel4/CMakeLists_inference.txt` | B | Build config for Process B executable |

### Files to Modify

| File | Change |
|------|--------|
| `phase3/src/sel4/main_x86.c` | Remove inference code, add process spawning + IPC dispatch |
| `phase3/src/sel4/CMakeLists.txt` | Add Process B target, CPIO archiving, model embedding on B |
| `~/sel4-x86/projects/jarvis-x86/apps/` | Add `jarvis-inference/` app directory |

### Files That Stay Unchanged

Process A (rootserver) links: `decision_cache.c`, `cache_patterns.c`, `shield.c`, `model_scaling.c`, `shmem_ipc.c`, `ring_buffer.c`, `dual_ring_buffer.c`

Process B (inference) links: `llama_quant.c`, `gguf_parser.c`, `gguf_vocab.c`, `dequant.c`, `tensor_ops.c`, `tokenizer.c`, `sampling.c`, `shmem_ipc.c`, `llama_load.c`, `llama_forward.c`, `inference.c`

Shared (linked into both): `shmem_ipc.c`

---

## IPC Message Protocol

Reuse existing `shmem_ipc.h` message types:

| Type | Direction | Payload |
|------|-----------|---------|
| MSG_QUERY (0x01) | A→B | Query string (UTF-8, max 240 bytes) |
| MSG_RESPONSE (0x02) | B→A | Generated text (UTF-8, max 240 bytes) |
| MSG_SHIELD_CHECK (0x09) | A→B | Query string for risk assessment |
| MSG_SHIELD_RESULT (0x0A) | B→A | Risk score + decision byte |
| MSG_HEARTBEAT (0x03) | A→B | Keep-alive |
| MSG_HEARTBEAT_ACK (0x04) | B→A | Keep-alive response |

For responses longer than 240 bytes: split into multiple MSG_RESPONSE messages with sequence numbers (already supported by shmem_ipc).

---

## Memory Layout

### Process A (Rootserver)
- Code + data: ~500 KB (cache, shield, drivers, IPC)
- Heap (morecore): ~5 MB (decision cache + IPC buffers)
- Shared pages: 2 × 4KB = 8KB (mapped from rootserver untypeds)
- **No model data** — all inference in Process B

### Process B (Inference Server)
- Code + .rodata: ~771 MB (model embedded via objcopy)
- Heap (morecore): ~50 MB (KV cache + activations + tokenizer)
- Shared pages: 2 × 4KB = 8KB (same physical pages as Process A)

### QEMU Total: 4GB
- seL4 kernel: ~10 MB
- Process A: ~6 MB
- Process B: ~820 MB
- Free: ~3.1 GB (available as untypeds)

---

## Build System Changes

### New: `jarvis-inference/` App Directory

```
~/sel4-x86/projects/jarvis-x86/apps/
├── sel4test-driver/     ← Process A (rootserver), existing
│   ├── CMakeLists.txt   ← Modified: add CPIO of Process B
│   └── src/
│       ├── main.c       ← Modified: spawn + IPC dispatch
│       ├── ai/          ← Cache, shield, model_scaling only
│       └── ipc/         ← shmem_ipc.c
│
└── jarvis-inference/    ← NEW: Process B
    ├── CMakeLists.txt   ← New: builds inference executable
    └── src/
        ├── main.c       ← inference_server.c entry point
        └── ai/          ← All inference code + model embedding
```

### CMake Integration

Process A's CMakeLists.txt adds:
```cmake
include(cpio)
MakeCPIO(archive.o "$<TARGET_FILE:jarvis-inference>")
target_sources(sel4test-driver PRIVATE archive.o)
```

Process B's CMakeLists.txt:
```cmake
add_executable(jarvis-inference EXCLUDE_FROM_ALL
    src/main.c
    src/ai/llama_quant.c src/ai/gguf_parser.c src/ai/gguf_vocab.c
    src/ai/dequant.c src/ai/tensor_ops.c src/ai/tokenizer.c
    src/ai/sampling.c src/ai/llama_load.c src/ai/llama_forward.c
    src/ipc/shmem_ipc.c
)
# Model embedding via objcopy (same pattern as current)
if(JARVIS_EMBED_MODEL)
    # ... objcopy command ...
    target_compile_definitions(jarvis-inference PRIVATE JARVIS_HAS_MODEL=1)
endif()
```

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| sel4utils process API complexity | Medium | High | Follow sel4test pattern exactly |
| CPIO + model = huge rootserver image | Low | Medium | Model goes in Process B's ELF, not CPIO |
| Shared memory mapping fails | Low | High | vspace_share_mem is proven in sel4test |
| Two separate heaps need separate morecore config | Medium | Medium | Configure per-process morecore in CMake |
| Process B can't access .rodata from its own ELF | Low | Medium | objcopy embeds into Process B, not A |
| IPC latency regression (was in-process, now cross-VSpace) | Low | Low | Shared memory is <1μs, seL4 notification is fast |

**Highest risk:** Getting the sel4utils process creation + CPIO packaging + shared memory mapping all working together. The individual APIs are documented and tested in sel4test, but combining them for our use case requires careful integration.

---

## Effort Estimate

| Task | Hours | Notes |
|------|-------|-------|
| Task 1: Create inference_server.c (Process B main) | 4 | IPC loop, model init, generation |
| Task 2: Modify main_x86.c (Process A, spawn + dispatch) | 6 | sel4utils process creation, shared memory |
| Task 3: Build system (CMake, CPIO, two targets) | 4 | Follow sel4test pattern |
| Task 4: Model embedding on Process B | 2 | Move objcopy to Process B's CMakeLists |
| Task 5: QEMU testing + debugging | 8 | Most time here — capability issues |
| Task 6: Regression testing (333 existing tests) | 2 | Verify no breakage |
| **Total** | **~26 hours** | **~3 weeks at 8-12 hrs/week** |

---

## Recommendation

**Do this as Phase 3c work (Weeks 29-34), NOT now.** Reasons:

1. **All QEMU-achievable inference work is done.** Process isolation is an architecture improvement, not a functional prerequisite.
2. **The spare PC (1/7 parts) is the real blocker.** Spending 3 weeks on process isolation delays nothing — there's no hardware to test on.
3. **However**, if the spare PC is delayed further, this is the highest-impact QEMU work remaining. It directly addresses SEC-014 and is required for production (Phase 4).
4. **Risk is manageable**: sel4test provides a complete working example of the exact same pattern. We're not inventing anything — just applying a proven seL4 technique.

**If you want to start now:** Begin with Task 3 (build system) since it's the foundation. If the CPIO + two-target build works, the rest follows.

---

*Plan written: March 27, 2026*
*seL4 APIs researched from: sel4test-manifest sources at ~/sel4-x86/*
*Key reference: sel4test/apps/sel4test-driver/src/testtypes.c (process creation pattern)*
