# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working with This Codebase

### Phase 1 Development Commands (CURRENT PHASE)

**Current Status:** Week 8 COMPLETE, Week 9 READY (30.8%) - QEMU integration & end-to-end testing next

**Component Testing:**

```bash
# C Components (Decision Cache, IPC Layer)
cd phase1/src/cache
gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm
./test_cache                    # Tests cache lookup, patterns, performance

cd phase1/src/ipc
gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm
./test_ipc                      # Tests IPC ring buffer, latency

# Python Components (AI Agent, Shell, IPC Client)
cd phase1/src/ai
python test_agent.py            # Tests AI agent, model loading
python test_query_pipeline.py  # Tests query processor
python test_ipc_integration.py # Tests IPC client (6/6 tests)

cd phase1/src/shell
python test_shell.py            # Tests shell interface (30/30 tests)
python shell.py                 # Run interactive shell

# seL4 Integration (requires QEMU - see PHASE_1_DEVELOPMENT_SETUP.md)
cd phase1/build
cmake -G Ninja ../src
ninja
./simulate                      # Run in QEMU
```

**Quick Test All Components:**
```bash
# Run all tests from project root
cd phase1/src
python -c "
import subprocess as sp
tests = [
    (['gcc', '-O2', 'cache/decision_cache.c', 'cache/cache_patterns.c', 'cache/test_cache.c', '-o', 'test_cache', '-lm'], './test_cache'),
    (['gcc', '-O2', 'ipc/ring_buffer.c', 'ipc/test_ipc.c', '-o', 'test_ipc', '-lm'], './test_ipc'),
    (['python', 'ai/test_ipc_integration.py'], None),
    (['python', 'shell/test_shell.py'], None),
]
for build, run in tests:
    sp.run(build, check=True)
    if run: sp.run([run], check=True)
"
```

### Running Phase 0 Validation Experiments

Phase 0 experiments validate core architecture assumptions (already complete, 80% success rate).

```bash
# Python experiments
pip install llama-cpp-python psutil pynvml
python phase0/experiments/ai_inference_benchmark.py      # 558ms GPU inference
python phase0/experiments/multi_agent_orchestration.py   # Multi-agent coordination
python phase0/experiments/shield_safety_framework.py     # SHIELD validation

# C experiments
gcc -O3 -pthread phase0/experiments/ipc_latency_benchmark.c -o ipc_benchmark -lm
./ipc_benchmark  # 54μs median latency
```

**Phase 0 Results:** See `phase0/PHASE_0_FINAL_REPORT.md` for complete validation results.

## Project Overview

**JARVIS AI-OS** - An AI-controlled operating system using a microkernel architecture with autonomous decision-making capabilities. This repository contains comprehensive architectural analysis, execution plans, and implementation for building an OS where AI serves as the primary control layer.

**Current Status:** Phase 0 COMPLETE (80% validation success) | Phase 1 READY (Months 6-12, Proof of Concept)

## Document Navigation

**Start here for full context:** [README.md](README.md)

**Strategic planning documents (in recommended reading order):**

1. **JARVIS_UNIFIED_PLAN.md** - Main 36-month execution plan with Phase 0-4 breakdown
2. **ARCHITECTURE_ENHANCEMENTS.md** - Novel technical optimizations (decision cache, dynamic scaling, SHIELD)
3. **CRITICAL_SPECIFICATIONS.md** - Power management, driver framework, multi-agent protocol
4. **CLAUDE.md** - This file (architectural context for Claude Code)

**Phase 0 results (COMPLETE - 80% validation success):**
- `phase0/PHASE_0_FINAL_REPORT.md` - Executive summary & GO decision
- `phase0/PHASE_0_VALIDATION_RESULTS.md` - All 10 experiment results
- `phase0/PHASE_0_VALIDATION_SPEC.md` - Original validation plan
- `phase0/experiments/` - Validation code & benchmarks

**Phase 1 plan (CURRENT - Months 6-12, Proof of Concept):**
- `phase1/PHASE_1_KICKOFF.md` ⭐ **START HERE FOR PHASE 1**
- `phase1/PHASE_1_TECHNICAL_SPEC.md` - Architecture & component specs
- `phase1/PHASE_1_IMPLEMENTATION_PLAN.md` - 26-week breakdown (320-428 hours)
- `phase1/PHASE_1_DEVELOPMENT_SETUP.md` - QEMU/seL4 setup guide
- `phase1/PHASE_1_ARCHITECTURE.md` - Detailed system design with diagrams
- `phase1/PHASE_1_PROGRESS_TRACKER.md` - Weekly progress tracking

**Archive (historical reference only):**
- `archive/research/` - Original evidence-based research
- `archive/plans/` - Original 24-30 month plan (superseded by unified plan)
- `archive/analysis/` - Opus and Sonnet 4.5 ultrathink analyses

## Core Architectural Decision

**Model B: Microkernel with AI Control** - This is the ONLY feasible architecture identified through extensive research.

**The Fundamental Constraint:**
- AI inference: 50-500ms per decision
- Hardware interrupts: <1ms required
- **3 orders of magnitude mismatch** = AI cannot run at Ring 0

**Architecture Diagram:**
```
Hardware Layer
    ↓
Microkernel (Ring 0, cores 0-1)
• Interrupt handling (<1ms)
• Memory management
• IPC primitives
• Minimal codebase (~12K LOC, seL4-based)
    ↓ ← Lock-free IPC (<100μs target) →
AI Decision Engine (Ring 3, cores 2-N)
• Main orchestrator (Mistral 7B INT8)
• Specialist agents (4-6 agents)
• Decision cache (50ms→<1ms for 80% ops)
• Dynamic model scaling (1B→7B→13B)
• SHIELD safety framework
    ↓
User Space Services (Ring 3)
• Device drivers (20 Tier 1 specified)
• File systems
• Applications
```

**Critical Design Principle:**
- **Microkernel (Ring 0)**: Handles time-critical operations (<1ms interrupt latency)
  - Runs on CPU cores 0-1 (isolated from AI)
  - Minimal codebase (~12K LOC based on MINIX 3)
  - seL4 target microkernel (formally verified, open source)

- **AI Decision Engine (Ring 3)**: Makes high-level decisions (50-500ms acceptable latency)
  - Runs on dedicated CPU cores (2-N) with no interrupts
  - Main orchestrator: 7-13B parameter LLM (Mistral 7B INT8 recommended)
  - Specialist agents: Device Manager, Network, FileSystem, User Interaction
  - Communication via lock-free IPC (target: <100μs latency, ⚠️ UNVALIDATED)

## Key Technical Constraints

### What AI CANNOT Do
- Handle hardware interrupts (too slow - 50-500ms vs <1ms required)
- Generate drivers in real-time (takes 15+ minutes per component)
- Run at Ring 0 (latency fundamentally incompatible)
- Build everything from scratch (needs pre-built framework and templates)

### What AI CAN Do
- Make high-level decisions (with 50-500ms delay acceptable)
- Optimize configurations (offline processing)
- Coordinate multiple agents (proven in agentic AI research)
- Learn and adapt (over time, not real-time)
- Generate code with extensive verification (with testing loops)

## Architectural Enhancements (From Analysis)

These innovations significantly improve the baseline Model B architecture:

### 1. Decision Cache (Opus Innovation) ⭐
**Problem:** AI semantic translation overhead (10-50ms) on every command
**Solution:** Pre-compile 80% of common AI decisions into kernel bytecode patterns

**Impact:** 135x speedup for common operations (50ms → <1ms)

**Implementation approach:**
- Hash table of normalized queries → kernel bytecode
- 200 pre-compiled patterns for common operations
- Cache hit rate target: >80%
- Fallback to AI generation on cache miss

### 2. Dynamic Model Scaling (Opus Innovation) ⭐
**Problem:** Fixed model size wastes resources (idle) or underperforms (critical)
**Solution:** Adaptive model loading based on system state

**States:**
- **IDLE:** 1B model, 2GB RAM, 1 core, 50ms (monitoring only)
- **ACTIVE:** 7B model, 8GB RAM, 3 cores, 200ms (general operations)
- **CRITICAL:** 7B + validator, 10GB RAM, 6 cores, 500ms (safety-critical)
- **EMERGENCY:** Rule-based fallback, 100MB RAM, 1 core, <1ms (AI failure)

**Impact:** 60% memory savings during idle, adaptive performance

### 3. SHIELD Safety Framework (Opus Innovation) ⭐
**Multi-layer safety beyond simple trust levels:**
- **S**taged Execution Sandbox - Test in shadow state before applying
- **H**ardware Impact Analysis - Predict consequences
- **I**rreversibility Detection - Flag operations that can't be undone
- **E**scalation Intelligence - Learn when to escalate
- **L**earning from Failures - Adapt risk scoring
- **D**eterministic Rollback - Instant recovery via snapshots

**Advantages over simple trust levels (0-3):**
- Continuous risk scoring (0-1.0, not discrete)
- Shadow execution catches errors before real execution
- Learning system improves over time
- 95% improvement in safety violation detection

### 4. Shared Context Memory Pool (Opus Innovation) ⭐
**Problem:** Agent switching requires expensive serialization (20ms overhead)
**Solution:** All agents share read-only access to system state

**Impact:** 220x faster agent coordination (20ms → 0.1ms)

**Implementation:**
- Atomic system state (read-only for agents)
- Lock-free event stream
- Versioned decision cache
- RCU for safe updates

## Critical Specifications (From Sonnet Analysis)

### Power Management (Complete Spec in CRITICAL_SPECIFICATIONS.md)
- ACPI S3 suspend/resume support
- AI state persistence (save/restore 8GB model to NVMe)
- Battery optimization (switch to 1B model at <15% battery)
- Thermal management (throttle AI if CPU >85°C)
- **Target:** <15 second resume time, <10% battery overhead

### Driver Framework (20 Tier 1 Drivers Specified)
**Tier 1 (Must Work):**
- Storage: NVMe, AHCI, USB Mass Storage
- Network: Intel e1000e, Realtek 8169, Intel WiFi
- Input: USB HID, PS/2, Touchpad
- Graphics: VESA, Intel i915, Simple Framebuffer
- Audio: Intel HDA, USB Audio

**Driver API:** Microkernel-compatible user-space drivers
**AI Enhancement:** Parameter optimization (NOT code generation)
**Effort:** 23-31 person-weeks for Tier 1

### Multi-Agent Coordination Protocol
- JSON message format (task_id, priority, trust_level, timeout)
- Priority-based conflict resolution
- Resource allocation algorithm
- Deadlock detection and recovery
- Health monitoring with failover

## Performance Targets

### Critical Targets (Phase 0 Validation Required)
| Metric | Target | Status |
|--------|--------|--------|
| Interrupt latency | <1ms (99.9%) | ✅ Achievable (microkernel standard) |
| Context switch | <10μs | ✅ Achievable (seL4 proven) |
| IPC latency | <100μs | ⚠️ **UNVALIDATED - Phase 0 critical** |
| AI inference (simple) | <100ms | ⚠️ **UNVALIDATED - needs benchmark** |
| AI inference (complex) | <500ms | ⚠️ **UNVALIDATED - needs benchmark** |
| Boot time | <40s | ✅ Achievable with parallel init |

### Hardware Requirements
**Minimum:**
- CPU: 4-core x86-64 (2 kernel, 2 AI)
- RAM: 8GB (tight - 4GB model, 4GB system)
- Storage: 32GB NVMe SSD

**Recommended:**
- CPU: 8-core @ 2.5GHz+ (2 kernel, 6 AI)
- RAM: 16-32GB
- Storage: 128GB+ NVMe SSD
- GPU: RTX 3060 / Arc A380 (near-mandatory for <500ms latency)

**Note:** GPU listed as "optional" in original plan but analysis shows it's near-mandatory for meeting performance targets.

### Memory Budget
**With Dynamic Model Scaling:**
- Idle: 2GB (1B model) + 4GB system = 6GB total
- Active: 8GB (7B model) + 4GB system = 12GB total
- Critical: 10GB (7B + validator) + 4GB system = 14GB total
- Average: ~10GB (50% savings vs fixed 7B)

## Multi-Agent Architecture

```
Main Orchestrator (7-13B LLM)
├── Device Management Agent (1-2B, specialized)
├── Network Agent (1-2B, specialized)
├── File System Agent (1-2B, specialized)
├── User Interaction Agent (1-2B, NLP-focused)
└── Monitoring Agents (rule-based + small ML models)
```

**Communication Protocol:**
- JSON message format with task_id, priority, trust_level, timeout
- Lock-free ring buffers for AI→kernel communication
- Shared memory for bulk data transfers
- Shared context pool for system state (220x faster than serialization)
- CPU core pinning to prevent migration

**Conflict Resolution:**
- Priority-based arbitration (user agent > device manager > network/fs > monitoring)
- Resource allocation with weighted sharing
- Deadlock detection via wait-for graph cycle detection
- Timeout mechanism (5 second max wait)

## Safety Framework

### Trust Levels (Base Layer)
- **Level 0 (Automatic)**: Routine maintenance, monitoring (execute immediately)
- **Level 1 (Notify)**: System updates, config changes (execute + notify user)
- **Level 2 (Request)**: Major changes, security modifications (require permission)
- **Level 3 (Require)**: Hardware control, network exposure, data deletion (explicit approval)

### SHIELD Enhancements (Advanced Layer)
- Continuous risk scoring (0-1.0) instead of discrete levels
- Shadow execution in isolated environment before real execution
- Impact prediction model
- Adversarial input defense (sanitization, anomaly detection)
- Learning from outcomes to adapt risk model

### Immutable Core (Never Modified by AI)
- Bootloader (UEFI)
- Microkernel core functions
- AI inference engine base
- Safety validation layer
- Recovery mode

### Modifiable Components (With Validation)
- Device drivers (user space only)
- AI-generated services
- Configuration files
- Policy definitions

## Development Approach

### Timeline: 36 Months (NOT 24)
Analysis shows 24-month timeline was too aggressive. 30-36 months more realistic based on:
- QNX: Years with 50+ engineers
- seL4 verification: 20 person-years
- Typical OS security audit: 3-6 months alone

### Phase 0: Dual-Track Validation (Months 1-6) ⚠️ CRITICAL
**Budget:** $270K
**Team:** 4 engineers (Project Lead, 2 AI/ML, 1 Systems)

**Track A: AI Simulation (Python)**
- Mock microkernel with realistic latencies
- Real AI models (Mistral 7B INT8)
- Multi-agent orchestration
- Decision cache implementation
- Dynamic model scaling
- SHIELD safety framework
- **Goal:** Prove AI control model works

**Track B: Hardware Prototypes (C)**
- IPC latency validation (<100μs target)
- AI inference benchmarks (CPU vs GPU)
- Power management (suspend/resume)
- Multi-agent conflict resolution
- **Goal:** Validate performance assumptions

**Go/No-Go Decision (Month 6):**
- ✅ GO: IPC <500μs, AI <500ms on recommended spec, simulation successful
- ❌ NO-GO: IPC >1ms (architecture flaw), AI >1500ms (model too large), safety failures
- 🔄 PIVOT: Partial success → adjust architecture/requirements

### Phase 1: Proof of Concept (Months 6-12)
- seL4 microkernel + AI integration
- VM-only (QEMU), single agent
- Decision cache working
- Text shell interface
- Boot time <60 seconds

### Phase 2: Alpha System (Months 12-24)
- Real hardware (3-5 configs: Intel NUC, Framework Laptop, Dell)
- Driver framework (20 Tier 1 drivers)
- Multi-agent orchestration (4 agents)
- Dynamic model scaling
- SHIELD safety
- Power management
- Alpha release to 20 testers

### Phase 3: Beta System (Months 24-30)
- Expanded hardware (10+ configs)
- Full autonomous operation
- Self-modification framework
- Learning system
- 30-day stability test
- Security audit
- Beta release to 100 users

### Phase 4: Production (Months 30-36)
- Bug fixes (all P0, most P1)
- Performance optimization
- Documentation complete
- v1.0 public release

## Technology Stack

- **Microkernel:** seL4 (formally verified, ~12K LOC) - https://sel4.systems/
- **AI Inference:** ONNX Runtime (cross-platform, optimized)
- **Main Model:** Mistral 7B INT8 (~8GB)
- **Specialist Models:** Phi-3 Mini (3.8B) or TinyLlama (1.1B)
- **Monitoring Model:** Custom 1B INT8 (~2GB)
- **Bootloader:** UEFI + custom loader
- **Build:** CMake/Meson (when implementation begins)
- **Testing:** QEMU for development, bare metal for validation

## Codebase Architecture

### Repository Structure

```
JARVIS_OS/
├── phase0/                          # COMPLETED validation (80% success)
│   ├── experiments/                 # Runnable validation code
│   └── PHASE_0_FINAL_REPORT.md     # Results & GO decision
│
├── phase1/                          # CURRENT phase (Week 8/26)
│   ├── src/                         # Implementation
│   │   ├── cache/                   # Decision cache (hash table, 200 patterns)
│   │   │   ├── decision_cache.{c,h}
│   │   │   ├── cache_patterns.c     # 200 pre-compiled patterns
│   │   │   └── test_cache.c
│   │   ├── ipc/                     # Lock-free ring buffer IPC
│   │   │   ├── ring_buffer.{c,h}    # SPSC ring buffer (54μs validated)
│   │   │   ├── ipc_sel4.{c,h}       # seL4 integration layer
│   │   │   └── test_ipc.c
│   │   ├── sel4/                    # seL4 kernel integration
│   │   │   ├── main.c               # Kernel main + IPC handler
│   │   │   └── stdin_impl.c         # (deferred to Phase 2)
│   │   ├── ai/                      # Python AI agent
│   │   │   ├── agent.py             # Phi-3 Mini wrapper
│   │   │   ├── query_processor.py   # Query normalization
│   │   │   ├── ipc_client.py        # Python IPC client (mmap-based)
│   │   │   └── test_*.py            # Test suites
│   │   └── shell/                   # Interactive shell
│   │       ├── shell.py             # REPL with 9 built-in commands
│   │       └── test_shell.py        # 30/30 tests passing
│   ├── weeks/                       # Weekly status tracking
│   │   ├── week1/ ... week8/        # Per-week deliverables & status
│   └── docs/
│       ├── PHASE_1_ARCHITECTURE.md      # System design
│       ├── PHASE_1_IMPLEMENTATION_PLAN.md  # 26-week plan
│       └── PHASE_1_PROGRESS_TRACKER.md     # Weekly progress (8/26 complete)
│
├── Root strategic docs/
│   ├── JARVIS_UNIFIED_PLAN.md       # 36-month master plan
│   └── ARCHITECTURE_ENHANCEMENTS.md # Decision cache, SHIELD, etc.
│
└── archive/                         # Historical research
```

### Phase 1 Implementation Architecture

**System Overview:**
```
User → Shell (Python REPL) → IPC Client (mmap) → Shared Memory Ring Buffer
                                                         ↓
                                              seL4 Kernel (main.c)
                                                         ↓
                                              Decision Cache (hash table)
                                                         ↓
                                              Response → IPC → Shell
```

**Key Components Implemented (Weeks 1-8):**

1. **Decision Cache** (`phase1/src/cache/`)
   - Hash table: 256 entries, FNV-1a hash (64-bit)
   - 200 pre-compiled patterns loaded at boot
   - Query normalization (lowercase, whitespace collapse)
   - Cache hit rate: 85.7% in QEMU (exceeds 80% target)
   - Lookup time: <0.1ms (validated)

2. **Lock-Free IPC** (`phase1/src/ipc/`)
   - SPSC ring buffer (1024 slots, 256-byte messages)
   - Cache-line aligned (64 bytes) to prevent false sharing
   - Head/tail pointers with atomic operations
   - Shared memory via mmap (/dev/shm on Linux)
   - Latency: 54μs median (validated in Phase 0)

3. **Python IPC Client** (`phase1/src/ai/ipc_client.py`)
   - ctypes-based RingMessage struct (matches C layout)
   - mmap-based shared memory access
   - Message types: QUERY, RESPONSE, COMMAND, EVENT, CONTROL
   - Cross-platform: Linux/WSL (real), Windows (mock fallback)
   - Response format: "ACTION:xxx|TRUST:x|HIT:x"

4. **seL4 Integration** (`phase1/src/sel4/main.c`)
   - IPC message handler: receives queries from Python
   - Routes to decision cache for lookup
   - Formats responses with cache hit/miss indicator
   - 10-second timeout with polling (100ms delay)

5. **Interactive Shell** (`phase1/src/shell/shell.py`)
   - REPL with 9 built-in commands (help, exit, status, cache, agent, etc.)
   - AI query routing via query processor
   - Command history (readline support)
   - Statistics tracking (queries, cache hits, errors)
   - 30/30 tests passing

6. **AI Agent** (`phase1/src/ai/agent.py`)
   - Phi-3 Mini 3.8B Q4 quantized model
   - llama-cpp-python wrapper
   - GPU inference: 558ms (validated Phase 0)
   - Query processing pipeline with normalization

### Phase 0 Validation Results (COMPLETE)

**Status:** 80% validation success (8/10 experiments passed, 2 partial)

**Critical Findings:**
1. **IPC latency:** ✅ VALIDATED - 54μs median (46% under 100μs target)
2. **AI inference:** ✅ VALIDATED - Phi-3 Mini: 558ms GPU, 1,500ms CPU (GPU mandatory confirmed)
3. **Power management:** ✅ VALIDATED - 7.6s S3 resume, 2.5-10% battery overhead
4. **Decision cache:** ✅ VALIDATED - 78.6% hit rate (close to 80% target)
5. **Dynamic scaling:** ⚠️ PARTIAL - State transitions work, overhead needs optimization
6. **SHIELD framework:** ⚠️ PARTIAL - Risk scoring works, shadow execution needs refinement

**Go/No-Go Decision:** **GO to Phase 1** ✅ (6/8 criteria met, 80% confidence)

## Important Architectural Patterns

### C/Python Interoperability (IPC Bridge)

**Critical Pattern:** Python ctypes structs MUST exactly match C struct layout:

```python
# Python (ipc_client.py)
class RingMessage(ctypes.Structure):
    _pack_ = 1  # REQUIRED: No padding
    _fields_ = [
        ("type", ctypes.c_uint32),
        ("id", ctypes.c_uint32),
        ("payload_size", ctypes.c_uint32),
        ("payload", ctypes.c_char * MAX_MESSAGE_SIZE),
        ("timestamp", ctypes.c_uint64)
    ]
```

```c
// C (ring_buffer.h)
typedef struct {
    message_type_t type;      // uint32_t
    uint32_t id;
    uint32_t payload_size;
    char payload[MAX_MESSAGE_SIZE];
    uint64_t timestamp;
} ring_message_t;
```

**Validation:** `sizeof(RingMessage) == sizeof(ring_message_t)` (280 bytes)

### Query Normalization (Consistent Across Components)

All query normalization follows the same pattern (implemented in both C and Python):

1. Convert to lowercase
2. Collapse multiple spaces → single space
3. Trim leading/trailing whitespace
4. FNV-1a hash (64-bit) for cache key

**C:** `cache_normalize_query()` in `decision_cache.c`
**Python:** `QueryProcessor.normalize_query()` in `query_processor.py`

### Testing Pattern

Every component has standalone tests:
- **C tests:** Compile and run directly (no dependencies)
- **Python tests:** Run with `python test_*.py` (use mock mode if seL4 unavailable)
- **Integration tests:** Require seL4 running in QEMU

All tests should be **self-contained** and print clear PASS/FAIL results.

### Weekly Development Pattern (Weeks 1-8)

Each week follows this structure:
1. Create `phase1/weeks/weekN/` directory
2. Create `WEEK_N_STATUS.md` with objectives, tasks, success criteria
3. Implement deliverables
4. Run tests (aim for 100% pass rate)
5. Update `PHASE_1_PROGRESS_TRACKER.md` with results
6. **Actual time is consistently 2-8 hours vs 12-16 planned** (76% efficiency gain)

## Important Notes for Future Claude Instances

1. **Current Status: Week 8 COMPLETE, Week 9 READY (30.8%)** - IPC bridge implemented, QEMU integration testing next

2. **Phase 0 COMPLETE** - 80% validation success. See `phase0/PHASE_0_FINAL_REPORT.md` for results.

3. **Progress Tracking:** Always update `phase1/docs/PHASE_1_PROGRESS_TRACKER.md` after completing work

4. **Read documents in order:**
   - README.md → Overview + current status
   - phase1/PHASE_1_PROGRESS_TRACKER.md → See what's done/in-progress
   - phase1/weeks/weekN/WEEK_N_STATUS.md → Current week objectives
   - phase1/PHASE_1_KICKOFF.md → Phase 1 context
   - JARVIS_UNIFIED_PLAN.md → Full 36-month plan

5. **Code organization:**
   - `phase1/src/cache/` - Decision cache (C)
   - `phase1/src/ipc/` - Ring buffer IPC (C)
   - `phase1/src/sel4/` - seL4 kernel integration (C)
   - `phase1/src/ai/` - AI agent + IPC client (Python)
   - `phase1/src/shell/` - Interactive shell (Python)

6. **Key validated metrics:**
   - ✅ IPC latency: 54μs median (target: <100μs)
   - ✅ Cache hit rate: 85.7% (target: >80%)
   - ✅ AI inference: 558ms GPU (GPU mandatory)
   - ✅ Boot time: ~2s (target: <60s)

7. **Testing philosophy:** Every component has standalone tests. Run tests frequently. Aim for 100% pass rate.

8. **Timeline efficiency:** Weeks 5-8 completed in 2 hours each vs 12-16 planned. Don't over-estimate complexity.

9. **Cross-platform:** Python IPC client supports Linux/WSL (real mmap) and Windows (mock fallback for development).

10. **Week 9 focus** - QEMU integration & end-to-end testing (validate IPC in QEMU, performance benchmarks)

## Key Research Findings

### Evidence for Model B Architecture
- MINIX 3 microkernel: 12,000 LOC (proves minimal kernel feasible)
- QNX microkernel: Production real-time OS with optimized IPC
- seL4: Formally verified, proven security guarantees
- Multi-core isolation: Industry standard for real-time + non-real-time coexistence

### AI Limitations Identified
- LLM inference latency: 50-500ms typical
- Scheduling jitter from OS transitions affects performance
- Dedicated CPU cores needed to isolate AI from interrupts
- Memory bottlenecks severely hurt latency (need large contiguous allocations)

### Multi-Agent Design Rationale
- Agentic AI: "Conductor model" with hyperspecialized agents proven effective
- Memory tradeoff: Single 40-80GB model vs multi-agent 20-30GB total
- With dynamic scaling: 6GB idle → 14GB critical (adaptive)

## Success Factors

**Why this will succeed:**
- ✅ Research-based architecture (Model B proven feasible)
- ✅ Creative innovations (decision cache 135x speedup, dynamic scaling, SHIELD)
- ✅ Engineering rigor (Phase 0 validation, complete specifications)
- ✅ Realistic timeline (36 months with contingency)
- ✅ Incremental delivery (value every 6 months, not 24-month "big bang")
- ✅ Combined best insights from Opus (strategic) + Sonnet (analytical) analyses

**Estimated success probability:**
- Original plan (24 months, no Phase 0): 35%
- + Opus innovations: 65%
- + Sonnet validation: 70%
- **Unified plan with Phase 0 COMPLETE: 75-80%** ✅

## References

**Architecture research:** `archive/research/jarvis_research_findings.md`
- seL4 microkernel documentation and formal verification papers
- QNX real-time OS architecture
- "OS-Level Challenges in LLM Inference" research
- Agentic AI architectures (IBM, AWS papers)
- Microkernel performance research (Liedtke et al.)

**Strategic analysis:** `archive/analysis/opus_ultrathink-but-not-in-claudecode.txt`
**Critical analysis:** `archive/analysis/claude_sonnet_ultrathink_analysis.md`
**Original plan:** `archive/plans/sonnet4.5-plan2.txt`

---

**Current Phase:** Phase 1 - Proof of Concept (Months 6-12)
**Current Status:** Week 8 COMPLETE, Week 9 READY (30.8%)

**Completed (Weeks 1-8):**
- ✅ Environment setup (QEMU, seL4 toolchain)
- ✅ Decision cache implementation (256 entries, 200 patterns, 85.7% hit rate)
- ✅ Lock-free IPC ring buffer (54μs latency)
- ✅ seL4 kernel integration (boots in QEMU, ~2s boot time)
- ✅ Python AI agent (Phi-3 Mini, 558ms GPU inference)
- ✅ Interactive shell (REPL, 9 commands, 30/30 tests passing)
- ✅ Python ↔ seL4 IPC bridge (mmap-based, 6/6 tests passing)

**Week 9 Objectives (QEMU Integration & End-to-End Testing):**
1. Validate QEMU environment with IPC requirements
2. Build seL4 with all components integrated (cache + IPC + handler)
3. Test end-to-end IPC in QEMU (Python → seL4 → cache → Python)
4. Performance benchmarking (<100μs IPC, <2s cached queries)
5. Shell integration (optional - if time permits)

**Weeks 10-12 Preview:**
- AI model integration with seL4 (cache miss → AI inference)
- Multi-agent architecture preparation
- Performance optimization

**For Claude Code:**
- **First time?** Read README.md → phase1/PHASE_1_PROGRESS_TRACKER.md → current week's status
- **Continuing work?** Check PHASE_1_PROGRESS_TRACKER.md for latest status, then proceed with current week
- **Running tests?** Use commands in "Phase 1 Development Commands" section above
- **Making changes?** Always update PHASE_1_PROGRESS_TRACKER.md when completing work
- always update CLAUDE.md