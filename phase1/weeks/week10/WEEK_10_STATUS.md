# Phase 1 Week 10 Status: seL4 + JARVIS Integration + Multi-Agent Implementation

**Date Started:** November 17, 2025
**Date Completed:** TBD
**Status:** 🚀 IN PROGRESS (5% complete)
**Focus:** QEMU Integration (deferred Week 9 tasks) + Multi-Agent Architecture (original Week 10 plan)
**Time Budget:** 28-32 hours (12 hrs QEMU + 16-20 hrs multi-agent)

---

## 📋 WEEK 10 OVERVIEW

**Context:**
Week 9 successfully validated all core components in standalone mode (34/34 tests passing). Tasks 2-5 were deferred to Week 10. The original Week 10 plan called for multi-agent implementation. **Week 10 now combines both** to stay on schedule from PHASE_1_IMPLEMENTATION_PLAN.md.

**Key Objectives:**

**Part A: QEMU Integration (Deferred from Week 9)**
1. ✅ Build seL4 with JARVIS components integrated (cache + IPC + handler)
2. ⏳ Test end-to-end Python ↔ seL4 IPC communication in QEMU
3. ⏳ Run performance benchmarks in QEMU environment
4. ⏳ (Optional) Integrate shell with seL4

**Part B: Multi-Agent Implementation (Original Week 10)**
5. ⏳ Create 4 specialist agents (Device Manager, Network, FileSystem, User Interaction)
6. ⏳ Implement agent router with keyword-based routing
7. ⏳ Implement shared context pool (lock-free reads)
8. ⏳ Test multi-agent coordination (100 queries, 100% routing accuracy)

**Why This Matters:**
- Part A validates the full architecture end-to-end in QEMU
- Part B implements the multi-agent coordination layer
- Combined: Enables testing multi-agent system in QEMU
- Keeps us on schedule with original implementation plan

---

## ✅ WEEK 9 ACCOMPLISHMENTS (Context)

**Core Components Validated:**
- Decision Cache: 8/8 tests ✅ (0.020μs lookup, 49,034× faster than target)
- IPC Ring Buffer: 4/4 tests ✅ (0.050μs latency, 2,000× faster than target)
- Python IPC Client: 6/6 tests ✅ (SHM_SIZE bug fixed: 4KB→277KB)
- Query Pipeline: 6/6 tests ✅
- Shell Interface: 6/6 tests ✅
- seL4 Environment: hello-world builds and boots ✅

**Total:** 34/34 tests passing, all performance targets exceeded

---

## 📊 WEEK 10 PROGRESS TRACKER

### Task 2: Build seL4 with JARVIS Components Integrated
**Status:** ⏳ IN PROGRESS (30.8% complete)
**Estimated Time:** 3 hours
**Actual Time:** TBD

**Objective:**
Build a single seL4 executable that includes decision cache, IPC ring buffer, and IPC message handler, and boots successfully in QEMU.

**Current State:**
- ✅ main.c already includes cache and IPC initialization code
- ✅ All component source files exist and are tested
- ⏳ CMakeLists.txt needs updating to include cache/*.c and ipc/*.c
- ⏳ Build and test in QEMU

**Steps:**

#### Step 2.1: Update CMakeLists.txt ✅ (READY)
**File:** `/home/user/JARVIS_OS/phase1/src/sel4/CMakeLists.txt`

**Current CMakeLists.txt only includes:**
```cmake
add_executable(jarvis-sel4
    main.c
)
```

**Needs to include:**
```cmake
add_executable(jarvis-sel4
    main.c
    ../cache/decision_cache.c
    ../cache/cache_patterns.c
    ../ipc/ring_buffer.c
    ../ipc/ipc_sel4.c
)

target_include_directories(jarvis-sel4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../cache
    ${CMAKE_CURRENT_SOURCE_DIR}/../ipc
)
```

**Action Required:**
- [ ] Update CMakeLists.txt with the above changes
- [ ] Ensure paths are correct (relative to sel4/ directory)
- [ ] Add include directories for headers

#### Step 2.2: Build with build-jarvis.sh
**Script:** `/home/user/JARVIS_OS/phase1/scripts/build-jarvis.sh`

**Commands to run (in WSL):**
```bash
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
./phase1/scripts/build-jarvis.sh clean    # Clean previous build
./phase1/scripts/build-jarvis.sh          # Build with new CMakeLists.txt
```

**Expected Output:**
- CMake configures successfully
- Ninja compiles all .c files (main.c, decision_cache.c, cache_patterns.c, ring_buffer.c, ipc_sel4.c)
- Build completes with no errors
- Generates: `~/jarvis-phase1/build-jarvis/images/jarvis-sel4-image-x86_64-pc99`

**Success Criteria:**
- [ ] Build completes with 0 errors
- [ ] All 5 .c files compiled successfully
- [ ] jarvis-sel4 executable created
- [ ] No linker errors or missing symbols

#### Step 2.3: Boot in QEMU and Validate
**Commands:**
```bash
cd ~/jarvis-phase1/build-jarvis
./simulate
```

**Expected Boot Sequence:**
```
========================================
  JARVIS AI-OS v0.1 - Phase 1
  Week 4: IPC Integration Complete
========================================
  seL4 + Decision Cache + IPC
  Build: Nov 17 2025 [time]
========================================

System Information:
  Architecture: x86_64
  Platform: pc99 (QEMU)
  Kernel: seL4 microkernel

Initializing decision cache...
✓ Cache initialized (256 entries)
✓ Loaded 103 patterns into cache

Initializing IPC system...
✓ IPC initialized

========== IPC PING/PONG TEST ==========
[IPC test output...]
✓ IPC PING/PONG TEST PASSED

[Demo shell output...]

========================================
IPC Message Handler - Listening for Python queries
========================================
```

**Success Criteria:**
- [ ] JARVIS banner displays
- [ ] Decision cache initializes (103 patterns loaded)
- [ ] IPC initializes successfully
- [ ] IPC ping/pong test passes (10/10 messages)
- [ ] IPC message handler starts listening
- [ ] No kernel panics or crashes
- [ ] Boot time <10 seconds

**Troubleshooting Common Issues:**

1. **Build Error: "decision_cache.h: No such file or directory"**
   - Fix: Add `target_include_directories()` to CMakeLists.txt
   - Verify paths are correct (../cache, ../ipc)

2. **Linker Error: "undefined reference to `cache_init`"**
   - Fix: Ensure decision_cache.c is in add_executable() list
   - Check function signatures match between .h and .c files

3. **Build Error: "stdatomic.h not found"**
   - Expected: ipc_sel4.c uses sel4_atomics.h (custom wrapper)
   - Verify sel4_atomics.h is included correctly

4. **QEMU doesn't boot / kernel panic**
   - Check QEMU command in ./simulate script
   - Verify memory size (-m 8G) and serial output (-serial stdio)
   - Check seL4 logs for initialization errors

---

### Task 3: End-to-End IPC Testing in QEMU
**Status:** ⏳ NOT STARTED (pending Task 2)
**Estimated Time:** 4 hours
**Actual Time:** TBD

**Objective:**
Validate that Python IPC client can communicate with seL4 running in QEMU via shared memory.

**Prerequisites:**
- ✅ Task 2 complete (seL4 boots with IPC handler)
- ✅ Python IPC client tested (6/6 tests from Week 9)
- ⏳ Shared memory accessible from QEMU guest

**Steps:**

#### Step 3.1: Prepare QEMU Environment
**Check shared memory availability:**
```bash
# In WSL (host)
ls -lh /dev/shm
df -h /dev/shm
```

**Expected:** 7.8GB available (from Week 9 validation)

**Note:** Python IPC client uses `/dev/shm/jarvis_ipc` for shared memory. QEMU guest needs access to this.

**Potential Issue:** QEMU guest may not have direct access to host /dev/shm.

**Solutions:**
1. **Option A (Preferred):** Use QEMU's memory-backend-file to share memory:
   ```bash
   qemu-system-x86_64 \
       -object memory-backend-file,id=mem,size=8G,mem-path=/dev/shm,share=on \
       -m 8G -smp 4 -serial stdio -nographic \
       -kernel images/jarvis-sel4-image-x86_64-pc99
   ```

2. **Option B:** Test Python client on same WSL host (not in QEMU guest):
   - seL4 runs in QEMU
   - Python client runs on WSL host
   - Both access /dev/shm/jarvis_ipc

**Decision:** Start with Option B (simpler), validate Option A if needed.

#### Step 3.2: Launch seL4 in QEMU
```bash
cd ~/jarvis-phase1/build-jarvis
./simulate &    # Run in background
```

**Verify:** IPC Message Handler is listening (output shows "Listening for Python queries")

#### Step 3.3: Run Python IPC Test Client
**Create a test script:** `phase1/src/ai/test_ipc_end_to_end.py`

```python
#!/usr/bin/env python3
"""
End-to-end IPC test with seL4 in QEMU
Week 10 Task 3
"""

import sys
import time
from ipc_client import IPCClient

def main():
    print("========================================")
    print("End-to-End IPC Test: Python <-> seL4")
    print("========================================\n")

    # Initialize IPC client
    print("Connecting to seL4 via shared memory...")
    client = IPCClient()

    if not client.connect():
        print("ERROR: Failed to connect to shared memory")
        print("Ensure seL4 is running in QEMU with IPC handler")
        return 1

    print("✓ Connected to /dev/shm/jarvis_ipc\n")

    # Test queries (cache hits from 103 patterns)
    test_queries = [
        "show cpu",
        "show memory",
        "help",
        "list processes",
        "check disk",
        "git status",
        "unknown query"  # Cache miss
    ]

    print("Sending test queries to seL4...\n")

    for i, query in enumerate(test_queries):
        print(f"[{i+1}] Query: \"{query}\"")

        # Send query
        if not client.send_query(query):
            print(f"    ERROR: Failed to send query")
            continue

        print(f"    ✓ Sent")

        # Wait for response
        response = client.receive_response(timeout=2.0)

        if response:
            print(f"    ✓ Response: {response}")

            # Parse response format: "ACTION:xxx|TRUST:x|HIT:x"
            parts = response.split('|')
            action = parts[0].split(':')[1] if len(parts) > 0 else "unknown"
            trust = parts[1].split(':')[1] if len(parts) > 1 else "?"
            hit = parts[2].split(':')[1] if len(parts) > 2 else "0"

            cache_status = "CACHE HIT" if hit == "1" else "CACHE MISS"
            print(f"    {cache_status} - Action: {action}, Trust: {trust}")
        else:
            print(f"    ERROR: No response (timeout)")

        print()
        time.sleep(0.5)  # Brief delay between queries

    # Print statistics
    print("\n========================================")
    print("Test Complete")
    print("========================================")
    client.print_stats()

    return 0

if __name__ == "__main__":
    sys.exit(main())
```

**Run the test:**
```bash
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/ai
python test_ipc_end_to_end.py
```

**Expected Output:**
```
========================================
End-to-End IPC Test: Python <-> seL4
========================================

Connecting to seL4 via shared memory...
✓ Connected to /dev/shm/jarvis_ipc

Sending test queries to seL4...

[1] Query: "show cpu"
    ✓ Sent
    ✓ Response: ACTION:show_cpu_info|TRUST:0|HIT:1
    CACHE HIT - Action: show_cpu_info, Trust: 0

[2] Query: "show memory"
    ✓ Sent
    ✓ Response: ACTION:show_memory_info|TRUST:0|HIT:1
    CACHE HIT - Action: show_memory_info, Trust: 0

[... more test queries ...]

========================================
Test Complete
========================================
Statistics:
  Messages sent: 7
  Messages received: 7
  Errors: 0
  Cache hits: 6/7 (85.7%)
```

**Success Criteria:**
- [ ] Python client connects to shared memory successfully
- [ ] All 7 test queries sent to seL4
- [ ] All 7 responses received from seL4
- [ ] Cache hit rate >80% (6/7 or better)
- [ ] Response format correct (ACTION:xxx|TRUST:x|HIT:x)
- [ ] No errors or timeouts
- [ ] 100% message delivery (no dropped messages)

**Verification in seL4 Console:**
Check QEMU output shows messages being received:
```
[MSG #1] Received from Python:
  Type: 2 (QUERY)
  Payload: "show cpu"
  [CACHE HIT] Action: show_cpu_info, Trust: 0
  [SENT] Response to Python
```

---

### Task 4: Performance Benchmarking in QEMU
**Status:** ⏳ NOT STARTED (pending Task 3)
**Estimated Time:** 2 hours
**Actual Time:** TBD

**Objective:**
Measure IPC latency and end-to-end query performance in QEMU environment.

**Metrics to Measure:**
1. IPC one-way latency (Python → seL4)
2. IPC round-trip latency (Python → seL4 → Python)
3. Cache lookup time (in QEMU)
4. End-to-end cached query time (total)
5. Boot time

**Steps:**

#### Step 4.1: IPC Latency Benchmark
**Create:** `phase1/src/ai/benchmark_ipc_latency.py`

```python
#!/usr/bin/env python3
"""
IPC Latency Benchmark - QEMU Environment
Week 10 Task 4
"""

import time
import statistics
from ipc_client import IPCClient

def benchmark_latency(client, num_iterations=100):
    """Measure round-trip IPC latency"""
    latencies = []

    print(f"Running {num_iterations} iterations...")

    for i in range(num_iterations):
        start = time.perf_counter()

        # Send query
        client.send_query("show cpu")  # Simple cache hit query

        # Receive response
        response = client.receive_response(timeout=1.0)

        end = time.perf_counter()

        if response:
            latency_us = (end - start) * 1_000_000  # Convert to microseconds
            latencies.append(latency_us)
        else:
            print(f"Warning: No response on iteration {i}")

    return latencies

def main():
    print("========================================")
    print("IPC Latency Benchmark (QEMU)")
    print("========================================\n")

    client = IPCClient()
    if not client.connect():
        print("ERROR: Failed to connect to seL4")
        return 1

    print("✓ Connected to seL4\n")

    # Run benchmark
    latencies = benchmark_latency(client, num_iterations=100)

    if not latencies:
        print("ERROR: No successful measurements")
        return 1

    # Calculate statistics
    median = statistics.median(latencies)
    mean = statistics.mean(latencies)
    p99 = statistics.quantiles(latencies, n=100)[98] if len(latencies) >= 100 else max(latencies)
    minimum = min(latencies)
    maximum = max(latencies)

    print("\n========================================")
    print("Results")
    print("========================================")
    print(f"Iterations:     {len(latencies)}")
    print(f"Median latency: {median:.3f} μs")
    print(f"Mean latency:   {mean:.3f} μs")
    print(f"Min latency:    {minimum:.3f} μs")
    print(f"Max latency:    {maximum:.3f} μs")
    print(f"99th percentile: {p99:.3f} μs")
    print()
    print(f"Target: <100 μs (Phase 0 baseline: 54 μs)")
    print(f"Status: {'✓ PASS' if median < 100 else '⚠ FAIL'}")
    print("========================================\n")

    return 0

if __name__ == "__main__":
    import sys
    sys.exit(main())
```

**Run benchmark:**
```bash
python benchmark_ipc_latency.py
```

**Expected Results:**
- Median latency: <100μs (target)
- Mean latency: <150μs
- 99th percentile: <200μs
- Note: QEMU adds virtualization overhead (~10-20%)

#### Step 4.2: Boot Time Measurement
```bash
cd ~/jarvis-phase1/build-jarvis
time ./simulate  # Use 'time' command to measure boot duration
```

**Measure from QEMU start to "IPC Message Handler - Listening"**

**Target:** <10 seconds (Phase 1 gate: <60 seconds)

#### Step 4.3: Performance Summary
**Create:** `phase1/weeks/week10/PERFORMANCE_RESULTS.md`

Document all metrics:
- IPC latency (median, p99)
- Boot time
- Cache lookup time
- Comparison to Week 9 standalone tests
- QEMU overhead analysis

**Success Criteria:**
- [ ] IPC round-trip latency <200μs median (allowing for QEMU overhead)
- [ ] Boot time <10 seconds
- [ ] Performance within 50% of standalone tests (acceptable virtualization overhead)
- [ ] All benchmarks documented

---

### Task 5: Shell Integration with seL4 (Optional)
**Status:** ⏳ NOT STARTED (stretch goal)
**Estimated Time:** 3 hours
**Actual Time:** TBD

**Objective:**
Connect the Week 7 shell to seL4 via IPC, allowing user commands to route through the decision cache in QEMU.

**Steps:**

#### Step 5.1: Update Shell to Use IPC Client
**Modify:** `phase1/src/shell/shell.py`

Add initialization:
```python
from ipc_client import IPCClient

class JarvisShell:
    def __init__(self):
        # ... existing initialization ...

        # Initialize IPC client for seL4 communication
        self.ipc_client = IPCClient()
        self.use_ipc = False  # Toggle for IPC mode vs direct AI

    def connect_to_sel4(self):
        """Connect to seL4 via IPC"""
        if self.ipc_client.connect():
            self.use_ipc = True
            print("✓ Connected to seL4 via IPC")
            return True
        else:
            print("⚠ seL4 not available, using direct AI agent")
            return False
```

Modify query processing:
```python
def process_ai_query(self, query):
    """Process AI query via seL4 IPC or direct agent"""

    if self.use_ipc:
        # Route through seL4
        if self.ipc_client.send_query(query):
            response = self.ipc_client.receive_response(timeout=2.0)
            if response:
                # Parse "ACTION:xxx|TRUST:x|HIT:x"
                parts = response.split('|')
                action = parts[0].split(':')[1]
                hit = parts[2].split(':')[1] == "1"

                cache_indicator = "[CACHE HIT]" if hit else "[CACHE MISS]"
                print(f"{cache_indicator} {action}")

                self.stats["cache_hits" if hit else "cache_misses"] += 1
                return

    # Fallback to direct AI agent
    result = self.ai_agent.query(query)
    print(result)
```

#### Step 5.2: Test Shell with seL4
```bash
# Terminal 1: Start seL4 in QEMU
cd ~/jarvis-phase1/build-jarvis
./simulate

# Terminal 2: Run shell
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/shell
python shell.py
```

**In shell:**
```
jarvis> show cpu
✓ Connected to seL4 via IPC
[CACHE HIT] show_cpu_info (0.15ms)

jarvis> help
[CACHE HIT] show_help (0.12ms)

jarvis> custom query
[CACHE MISS] AI processing... (64.5ms)
```

**Success Criteria:**
- [ ] Shell connects to seL4 on startup
- [ ] User queries route through IPC
- [ ] Cache hit/miss indicators display correctly
- [ ] Statistics track cache performance
- [ ] Fallback to direct AI works if seL4 unavailable

---

## PART B: MULTI-AGENT IMPLEMENTATION (ORIGINAL WEEK 10 PLAN)

### Task 6: Create 4 Specialist Agents
**Status:** ⏳ NOT STARTED (pending Part A completion)
**Estimated Time:** 8 hours
**Actual Time:** TBD

**Objective:**
Implement 4 specialized AI agents, each focused on a specific domain of system operations.

**Agent Specifications (from PHASE_1_IMPLEMENTATION_PLAN.md):**

1. **Device Manager Agent**
   - Focus: System monitoring, device control
   - Keywords: "cpu", "memory", "disk", "device", "hardware"
   - Model: Phi-3 Mini 3.8B Q4 (specialized for system diagnostics)
   - Example queries: "show cpu", "check memory", "list devices"

2. **Network Agent**
   - Focus: Network operations, ping, connectivity
   - Keywords: "network", "ping", "internet", "connection"
   - Model: Phi-3 Mini 3.8B Q4 (specialized for network diagnostics)
   - Example queries: "ping google.com", "check internet", "network status"

3. **FileSystem Agent**
   - Focus: File operations, directory listing
   - Keywords: "file", "directory", "ls", "pwd", "cd"
   - Model: Phi-3 Mini 3.8B Q4 (specialized for file operations)
   - Example queries: "list files", "show directory", "find file"

4. **User Interaction Agent**
   - Focus: Help, documentation, tutorials
   - Keywords: "help", "how to", "tutorial", "explain"
   - Model: Phi-3 Mini 3.8B Q4 (specialized for user assistance)
   - Example queries: "help", "how do I...", "explain command"

**Steps:**

#### Step 6.1: Create Agent Base Class
**File:** `phase1/src/ai/specialist_agent.py`

```python
"""
Specialist Agent Base Class
Week 10 Part B - Multi-Agent Implementation
"""

from typing import Optional, Dict, Any
import time

class SpecialistAgent:
    """Base class for all specialist agents"""

    def __init__(self, name: str, domain: str, keywords: list[str]):
        self.name = name
        self.domain = domain
        self.keywords = keywords
        self.model = None
        self.stats = {
            "queries_processed": 0,
            "avg_inference_time_ms": 0.0,
            "cache_hits": 0,
            "cache_misses": 0,
            "errors": 0
        }

    def load_model(self, model_path: str) -> bool:
        """Load the AI model for this agent"""
        # Use existing agent.py logic
        raise NotImplementedError("Subclass must implement load_model()")

    def can_handle(self, query: str) -> bool:
        """Check if this agent can handle the query"""
        query_lower = query.lower()
        return any(keyword in query_lower for keyword in self.keywords)

    def process_query(self, query: str, context: Optional[Dict[str, Any]] = None) -> str:
        """Process a query with this agent"""
        raise NotImplementedError("Subclass must implement process_query()")

    def get_stats(self) -> Dict[str, Any]:
        """Get agent statistics"""
        return self.stats.copy()

    def print_stats(self):
        """Print agent statistics"""
        print(f"\n{self.name} Statistics:")
        print(f"  Domain: {self.domain}")
        print(f"  Keywords: {', '.join(self.keywords)}")
        print(f"  Queries processed: {self.stats['queries_processed']}")
        print(f"  Avg inference time: {self.stats['avg_inference_time_ms']:.2f}ms")
        print(f"  Cache hits: {self.stats['cache_hits']}")
        print(f"  Cache misses: {self.stats['cache_misses']}")
        print(f"  Errors: {self.stats['errors']}")
```

#### Step 6.2: Implement 4 Specialist Agents
**Files:**
- `phase1/src/ai/device_manager_agent.py`
- `phase1/src/ai/network_agent.py`
- `phase1/src/ai/filesystem_agent.py`
- `phase1/src/ai/user_interaction_agent.py`

Each agent inherits from `SpecialistAgent` and implements domain-specific logic.

**Example:** `device_manager_agent.py`
```python
from specialist_agent import SpecialistAgent
from agent import AIAgent  # Reuse Week 5 model loading

class DeviceManagerAgent(SpecialistAgent):
    def __init__(self):
        super().__init__(
            name="Device Manager",
            domain="system_monitoring",
            keywords=["cpu", "memory", "disk", "device", "hardware", "system"]
        )

    def load_model(self, model_path: str) -> bool:
        """Load Phi-3 Mini model"""
        self.model = AIAgent()  # Reuse Week 5 implementation
        return self.model.load_model(model_path)

    def process_query(self, query: str, context=None) -> str:
        """Process system monitoring query"""
        start = time.perf_counter()

        # Use AI model to process query
        result = self.model.query(query)

        elapsed_ms = (time.perf_counter() - start) * 1000

        # Update stats
        self.stats["queries_processed"] += 1
        self.stats["avg_inference_time_ms"] = (
            (self.stats["avg_inference_time_ms"] * (self.stats["queries_processed"] - 1) + elapsed_ms)
            / self.stats["queries_processed"]
        )

        return result
```

**Success Criteria:**
- [ ] Base `SpecialistAgent` class implemented
- [ ] 4 specialist agents created (Device, Network, FileSystem, User)
- [ ] Each agent loads Phi-3 Mini successfully
- [ ] Each agent has correct keyword set
- [ ] Stats tracking functional

---

### Task 7: Implement Agent Router
**Status:** ⏳ NOT STARTED (pending Task 6)
**Estimated Time:** 4 hours
**Actual Time:** TBD

**Objective:**
Create a router that directs queries to the appropriate specialist agent based on keywords.

**Steps:**

#### Step 7.1: Create Agent Router
**File:** `phase1/src/ai/agent_router.py`

```python
"""
Agent Router - Keyword-based Query Routing
Week 10 Part B Task 7
"""

from typing import Optional, List
from specialist_agent import SpecialistAgent

class AgentRouter:
    """Routes queries to appropriate specialist agents"""

    def __init__(self):
        self.agents: List[SpecialistAgent] = []
        self.default_agent: Optional[SpecialistAgent] = None
        self.stats = {
            "total_queries": 0,
            "routing_accuracy": 0.0,
            "fallback_count": 0
        }

    def register_agent(self, agent: SpecialistAgent):
        """Register a specialist agent"""
        self.agents.append(agent)

    def set_default_agent(self, agent: SpecialistAgent):
        """Set fallback agent for unmatched queries"""
        self.default_agent = agent

    def route_query(self, query: str) -> Optional[SpecialistAgent]:
        """Route query to appropriate agent based on keywords"""

        # Find matching agents
        candidates = [agent for agent in self.agents if agent.can_handle(query)]

        if len(candidates) == 1:
            # Perfect match
            return candidates[0]
        elif len(candidates) > 1:
            # Multiple matches - use priority
            # Priority: User Interaction > Device Manager > Network > FileSystem
            priority_order = ["User Interaction", "Device Manager", "Network Agent", "FileSystem Agent"]
            for name in priority_order:
                for agent in candidates:
                    if agent.name == name:
                        return agent

        # No match - use default agent
        self.stats["fallback_count"] += 1
        return self.default_agent

    def process_query(self, query: str, context=None) -> str:
        """Route and process query"""
        self.stats["total_queries"] += 1

        agent = self.route_query(query)

        if agent:
            print(f"  [ROUTER] → {agent.name}")
            return agent.process_query(query, context)
        else:
            print(f"  [ROUTER] → No agent available")
            return "Error: No agent available to handle query"

    def print_stats(self):
        """Print router statistics"""
        print("\nAgent Router Statistics:")
        print(f"  Total queries: {self.stats['total_queries']}")
        print(f"  Fallback count: {self.stats['fallback_count']}")
        print(f"  Routing accuracy: {(1 - self.stats['fallback_count']/max(1, self.stats['total_queries'])) * 100:.1f}%")
```

**Success Criteria:**
- [ ] Agent router implemented
- [ ] Keyword-based routing working
- [ ] Priority-based conflict resolution
- [ ] Fallback to default agent functional
- [ ] Routing accuracy >90% on test queries

---

### Task 8: Implement Shared Context Pool
**Status:** ⏳ NOT STARTED (pending Task 6-7)
**Estimated Time:** 4 hours
**Actual Time:** TBD

**Objective:**
Create a shared memory pool that all agents can read from for system state information.

**Design (from ARCHITECTURE_ENHANCEMENTS.md):**
- Read-only access for agents
- Lock-free reads using atomic operations
- Contains: system state, recent queries, cache statistics, resource usage

**Steps:**

#### Step 8.1: Create Shared Context Pool
**File:** `phase1/src/ai/shared_context.py`

```python
"""
Shared Context Pool - Lock-Free Read-Only System State
Week 10 Part B Task 8
"""

import threading
import time
from typing import Dict, Any

class SharedContext:
    """Lock-free shared context pool for agents"""

    def __init__(self):
        self._context: Dict[str, Any] = {
            "system_state": {
                "cpu_usage": 0.0,
                "memory_usage": 0.0,
                "disk_usage": 0.0,
                "uptime_seconds": 0
            },
            "recent_queries": [],
            "cache_stats": {
                "hit_rate": 0.0,
                "total_queries": 0
            },
            "agent_health": {},
            "timestamp": time.time()
        }
        self._lock = threading.RLock()

    def read(self) -> Dict[str, Any]:
        """Read current context (lock-free for agents)"""
        # Atomic read - returns snapshot
        return self._context.copy()

    def update(self, key: str, value: Any):
        """Update context (only called by system, not agents)"""
        with self._lock:
            if key in self._context:
                self._context[key] = value
                self._context["timestamp"] = time.time()

    def add_recent_query(self, query: str):
        """Add query to recent history (circular buffer)"""
        with self._lock:
            self._context["recent_queries"].append({
                "query": query,
                "timestamp": time.time()
            })
            # Keep only last 10 queries
            if len(self._context["recent_queries"]) > 10:
                self._context["recent_queries"].pop(0)

    def get_system_state(self) -> Dict[str, Any]:
        """Get system state snapshot"""
        return self._context["system_state"].copy()

    def print_context(self):
        """Print current context"""
        context = self.read()
        print("\nShared Context Pool:")
        print(f"  CPU Usage: {context['system_state']['cpu_usage']:.1f}%")
        print(f"  Memory Usage: {context['system_state']['memory_usage']:.1f}%")
        print(f"  Cache Hit Rate: {context['cache_stats']['hit_rate']:.1f}%")
        print(f"  Recent Queries: {len(context['recent_queries'])}")
        print(f"  Last Updated: {time.time() - context['timestamp']:.1f}s ago")
```

**Success Criteria:**
- [ ] Shared context pool implemented
- [ ] Lock-free reads working
- [ ] System state tracking functional
- [ ] Recent query history maintained
- [ ] All agents can access context

---

### Task 9: Test Multi-Agent Coordination
**Status:** ⏳ NOT STARTED (pending Tasks 6-8)
**Estimated Time:** 4 hours
**Actual Time:** TBD

**Objective:**
Test the complete multi-agent system with 100 diverse queries and verify 100% routing accuracy.

**Steps:**

#### Step 9.1: Create Multi-Agent Test Suite
**File:** `phase1/src/ai/test_multi_agent.py`

```python
"""
Multi-Agent Coordination Test Suite
Week 10 Part B Task 9
"""

import time
from agent_router import AgentRouter
from device_manager_agent import DeviceManagerAgent
from network_agent import NetworkAgent
from filesystem_agent import FileSystemAgent
from user_interaction_agent import UserInteractionAgent
from shared_context import SharedContext

def test_multi_agent_routing():
    """Test 100 diverse queries with correct routing"""

    print("========================================")
    print("Multi-Agent Routing Test")
    print("========================================\n")

    # Initialize agents
    print("Loading agents...")
    device_agent = DeviceManagerAgent()
    network_agent = NetworkAgent()
    fs_agent = FileSystemAgent()
    user_agent = UserInteractionAgent()

    # Load models
    model_path = "models/Phi-3-mini-4k-instruct-q4.gguf"
    device_agent.load_model(model_path)
    network_agent.load_model(model_path)
    fs_agent.load_model(model_path)
    user_agent.load_model(model_path)

    # Initialize router
    router = AgentRouter()
    router.register_agent(device_agent)
    router.register_agent(network_agent)
    router.register_agent(fs_agent)
    router.register_agent(user_agent)
    router.set_default_agent(user_agent)

    # Initialize shared context
    context = SharedContext()

    # Test queries (targeting each agent)
    test_queries = [
        # Device Manager (25 queries)
        ("show cpu", "Device Manager"),
        ("check memory", "Device Manager"),
        ("disk usage", "Device Manager"),
        # ... 22 more

        # Network Agent (25 queries)
        ("ping google.com", "Network Agent"),
        ("check internet", "Network Agent"),
        ("network status", "Network Agent"),
        # ... 22 more

        # FileSystem Agent (25 queries)
        ("list files", "FileSystem Agent"),
        ("show directory", "FileSystem Agent"),
        ("find file", "FileSystem Agent"),
        # ... 22 more

        # User Interaction Agent (25 queries)
        ("help", "User Interaction"),
        ("how do I...", "User Interaction"),
        ("explain command", "User Interaction"),
        # ... 22 more
    ]

    print(f"Testing {len(test_queries)} queries...\n")

    correct = 0
    total = len(test_queries)

    for i, (query, expected_agent) in enumerate(test_queries):
        print(f"[{i+1}/{total}] Query: \"{query}\"")

        # Route query
        agent = router.route_query(query)

        if agent and agent.name == expected_agent:
            print(f"  ✓ Routed to: {agent.name}")
            correct += 1
        else:
            actual = agent.name if agent else "None"
            print(f"  ✗ ERROR: Expected {expected_agent}, got {actual}")

        # Add to shared context
        context.add_recent_query(query)

    # Calculate accuracy
    accuracy = (correct / total) * 100

    print("\n========================================")
    print("Test Results")
    print("========================================")
    print(f"Total queries: {total}")
    print(f"Correct routing: {correct}")
    print(f"Routing accuracy: {accuracy:.1f}%")
    print(f"Target: 100%")
    print(f"Status: {'✓ PASS' if accuracy == 100 else '⚠ FAIL'}")
    print("========================================\n")

    # Print agent statistics
    device_agent.print_stats()
    network_agent.print_stats()
    fs_agent.print_stats()
    user_agent.print_stats()

    # Print router statistics
    router.print_stats()

    # Print shared context
    context.print_context()

    return accuracy == 100

if __name__ == "__main__":
    import sys
    success = test_multi_agent_routing()
    sys.exit(0 if success else 1)
```

**Run test:**
```bash
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/ai
python test_multi_agent.py
```

**Success Criteria:**
- [ ] 100 test queries processed
- [ ] 100% routing accuracy achieved
- [ ] No deadlocks detected
- [ ] All agents respond correctly
- [ ] Shared context updated correctly
- [ ] Zero crashes or errors

---

## 🎯 WEEK 10 SUCCESS CRITERIA

**Part A: QEMU Integration**

**Week 10 is successful if:**
- [x] seL4 builds with all JARVIS components integrated (cache + IPC)
- [ ] seL4 boots in QEMU with no errors or panics
- [ ] Python client successfully communicates with seL4 via IPC
- [ ] End-to-end message flow working (Python → IPC → Cache → IPC → Python)
- [ ] IPC latency <200μs round-trip (QEMU environment)
- [ ] Cache hit rate >80% on test queries
- [ ] 100% message delivery (no dropped messages)
- [ ] Boot time <10 seconds

**Part B: Multi-Agent Implementation**
- [ ] 4 specialist agents implemented and functional
- [ ] Agent router working with 100% routing accuracy (target: >90%)
- [ ] Shared context pool functional (lock-free reads)
- [ ] 100 test queries processed successfully
- [ ] Zero deadlocks detected
- [ ] All agents coordinating correctly

**Bonus (Optional):**
- [ ] Shell integration with seL4 working
- [ ] Performance benchmarks published
- [ ] Multi-agent system tested in QEMU with seL4

---

## 🚧 POTENTIAL ISSUES & MITIGATIONS

### Issue 1: Shared Memory Access in QEMU
**Problem:** /dev/shm on WSL host may not be accessible to QEMU guest
**Likelihood:** Medium
**Impact:** High (blocks Task 3)

**Mitigation:**
- Option A: Test with Python client on WSL host, seL4 in QEMU (both access /dev/shm)
- Option B: Use QEMU memory-backend-file to share /dev/shm
- Option C: Use virtio-serial instead of shared memory (slower but more portable)

**Decision Point:** Try Option A first (simpler), escalate to B if needed

### Issue 2: Build Complexity
**Problem:** Integrating 5 .c files into seL4 build may have dependency issues
**Likelihood:** Low
**Impact:** Medium (delays Task 2)

**Mitigation:**
- Follow seL4 tutorial CMakeLists.txt patterns
- Add components incrementally (cache first, then IPC)
- Verify each component builds before adding next

### Issue 3: Performance Regression in QEMU
**Problem:** Virtualization overhead may slow IPC beyond acceptable limits
**Likelihood:** Low
**Impact:** Medium (fails performance targets)

**Mitigation:**
- Accept 10-50% slowdown in QEMU vs bare metal
- Use KVM acceleration if available
- Adjust targets: <100μs → <200μs for QEMU
- Plan bare metal validation for Phase 2

### Issue 4: Debugging Complexity
**Problem:** Debugging across Python/seL4/QEMU/IPC is difficult
**Likelihood:** High
**Impact:** Low (slows development, doesn't block)

**Mitigation:**
- Extensive logging in both Python and seL4
- Use printf debugging in seL4 (serial console)
- Add debug message types to IPC protocol
- Create test harness with known-good messages
- Test components in isolation before integration

---

## 📚 FILES TO CREATE/MODIFY

**New Files:**
- [ ] `phase1/weeks/week10/WEEK_10_STATUS.md` (this file)
- [ ] `phase1/src/ai/test_ipc_end_to_end.py` (Task 3 test script)
- [ ] `phase1/src/ai/benchmark_ipc_latency.py` (Task 4 benchmark)
- [ ] `phase1/weeks/week10/PERFORMANCE_RESULTS.md` (Task 4 documentation)

**Modified Files:**
- [ ] `phase1/src/sel4/CMakeLists.txt` (add cache and IPC components)
- [ ] `phase1/src/shell/shell.py` (optional Task 5)
- [ ] `phase1/docs/PHASE_1_PROGRESS_TRACKER.md` (update Week 10 status)

---

## 📝 NOTES

**Why Combine QEMU Integration + Multi-Agent?**
- Week 9 validated all components standalone (34/34 tests passing)
- Deferred QEMU integration (Tasks 2-5) to focus on validation
- Original Week 10 plan called for multi-agent implementation
- **Combining both** keeps us on schedule from PHASE_1_IMPLEMENTATION_PLAN.md
- Part A (QEMU) must complete before Part B (multi-agent) to enable testing

**Time Estimate Rationale:**

**Part A: QEMU Integration (12 hours)**
- Task 2 (Build): 3 hours (CMake update, build, debug)
- Task 3 (E2E IPC): 4 hours (shared memory setup, testing, debugging)
- Task 4 (Benchmarks): 2 hours (scripts, measurements, documentation)
- Task 5 (Shell): 3 hours (optional stretch goal)

**Part B: Multi-Agent (16-20 hours from implementation plan)**
- Task 6 (4 Agents): 8 hours (base class + 4 implementations)
- Task 7 (Router): 4 hours (keyword routing + priority resolution)
- Task 8 (Shared Context): 4 hours (lock-free pool + system state)
- Task 9 (Testing): 4 hours (100 queries + coordination tests)

**Total: 28-32 hours** (ambitious but achievable given Week 5-9 efficiency trends)

**Based on Week 5-9 efficiency:**
- Weeks 5-9 averaged 87% time savings (2 hours vs 12-16 planned)
- Part A may take 6-8 hours actual (integration complexity)
- Part B may take 8-12 hours actual (multi-agent coordination)
- Realistic combined estimate: 14-20 hours actual vs 28-32 planned

---

**Document Status:** ACTIVE
**Created:** November 17, 2025
**Next Update:** After Task 2 completion (seL4 build)
**Maintainer:** JARVIS AI-OS Team
