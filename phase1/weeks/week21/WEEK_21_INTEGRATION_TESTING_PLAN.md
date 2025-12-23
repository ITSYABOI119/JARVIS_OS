# Week 21: Comprehensive Integration & Stability Testing Plan

**Status:** IN PROGRESS
**Estimated Effort:** 18-24 hours
**Type:** Original Week 20 Integration Testing (deferred from Week 20)

---

## Overview

This plan implements the **original Week 20 integration testing requirements** that were deferred in favor of Command Set Expansion. Now that we have 27 commands, 288+ cache patterns, and all components validated individually, we need comprehensive end-to-end validation.

**Objectives:**
1. End-to-end integration test (QEMU boot → agents → commands)
2. Performance testing (100K IPC samples, 1000 cache queries, 100 AI queries)
3. Stability testing (12-hour automated continuous run)
4. Issue resolution (fix any crashes, deadlocks, performance problems)

---

## Test Architecture

### Directory Structure
```
phase1/src/integration_tests/
├── test_e2e_integration.py          # Task 1: End-to-end integration
├── test_performance_benchmark.py    # Task 2: Performance testing
├── test_stability_12h.py            # Task 3: 12-hour stability
├── utils/
│   ├── qemu_manager.py              # QEMU startup/shutdown
│   ├── random_command_generator.py  # Random command generation
│   ├── system_monitor.py            # CPU/memory/deadlock monitoring
│   └── test_logger.py               # Structured logging
└── logs/
    ├── e2e_results_YYYYMMDD_HHMMSS.log
    ├── performance_YYYYMMDD_HHMMSS.json
    └── stability_12h_YYYYMMDD_HHMMSS.log
```

### Integration with Existing Infrastructure

**Extend existing test runner:**
- Add integration tests to `run_all_tests_wsl.sh` (optional phase)
- Create separate `run_integration_tests.sh` for focused execution
- Reuse existing test utilities (SystemStateManager, SHIELD, agents)

### Logging Strategy

**Three-tier logging:**
1. **Console output** - Real-time progress (INFO level)
2. **Detailed logs** - All events to timestamped log files (DEBUG level)
3. **Summary reports** - JSON format for automated analysis

**Log format:**
```python
[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] [Component] Message
[2025-11-28 14:30:45.123] [INFO] [E2E_Test] QEMU boot successful (2.1s)
[2025-11-28 14:30:48.456] [DEBUG] [Agent_Router] Device Manager agent loaded
[2025-11-28 14:30:52.789] [ERROR] [Command_Test] 'shutdown' blocked by SHIELD (expected)
```

---

## Task 1: End-to-End Integration Test

**Goal:** Validate complete QEMU → Shell → Agents → Commands pipeline

### Test Phases

#### Phase 1.1: QEMU Boot Validation
```python
def test_qemu_boot_sequence():
    """
    Validates QEMU boots successfully with JARVIS

    Steps:
    1. Start QEMU with seL4 + JARVIS image
    2. Monitor boot messages
    3. Verify decision cache loaded (103 patterns expected)
    4. Verify IPC ring buffer initialized
    5. Measure boot time (<60s target, expecting ~2s)

    Success Criteria:
    - Boot completes without errors
    - Boot time <60s (expecting ~2s based on Week 19)
    - Cache patterns loaded: 103
    - IPC ready: True
    """
    pass
```

**Implementation approach:**
- Use `subprocess.Popen()` to launch QEMU
- Parse stdout for "Decision cache initialized" message
- Timeout: 120 seconds (generous, expecting 2s)
- Capture boot time from seL4 kernel timestamp

#### Phase 1.2: Multi-Agent Loading
```python
def test_multi_agent_initialization():
    """
    Validates all 4 agents load and register with router

    Agents to load:
    1. Device Manager Agent (device operations)
    2. Network Agent (network diagnostics)
    3. FileSystem Agent (file operations)
    4. User Interaction Agent (shell commands)

    Steps:
    1. Initialize AgentRouter
    2. Load each agent sequentially
    3. Verify agent registration
    4. Perform health check (all agents HEALTHY)
    5. Validate routing accuracy (100% expected)

    Success Criteria:
    - All 4 agents loaded: True
    - All agents HEALTHY: True
    - Router accuracy: 100%
    - Total load time: <10s
    """
    pass
```

**Reuse existing code:**
- `agent_router.py` - AgentRouter class
- `agent_health.py` - HealthMonitor
- `test_routing_accuracy.py` - Routing validation logic

#### Phase 1.3: Command Execution Test (All 27 Commands)
```python
def test_all_commands_execution():
    """
    Execute all 27 commands and validate SHIELD integration

    Command Categories:
    - File operations (4): ls, cat, mkdir, rm
    - Process management (3): ps, kill, top
    - System control (4): shutdown, reboot, battery, uptime
    - Network (2): ping, ifconfig
    - Original (14): help, exit, status, cache, agent, shield, query, clear, etc.

    Validation:
    - Safe commands execute successfully
    - Dangerous commands validated by SHIELD
    - Critical commands blocked (shutdown, reboot)
    - All results logged

    Success Criteria:
    - Safe commands: 100% execution success
    - SHIELD validation: 100% for dangerous ops (5 commands)
    - Critical blocked: 100% (shutdown, reboot)
    - False positives: 0%
    - Total execution time: <5 minutes
    """

    # Test matrix
    commands = {
        'safe': ['help', 'status', 'ls', 'ps', 'battery', 'uptime', 'ifconfig'],
        'shield_validated': ['mkdir /tmp/test', 'rm /tmp/test', 'kill 12345'],
        'shield_blocked': ['shutdown', 'reboot'],
    }

    results = {
        'safe_passed': 0,
        'shield_validated_passed': 0,
        'shield_blocked_passed': 0,
        'failures': []
    }

    # Execute each category
    # ... implementation ...

    return results
```

**SHIELD integration validation:**
- Inject SHIELD framework into shell
- Monitor for "[SHIELD] BLOCKED" messages
- Verify risk scores match expected values
- Validate shadow execution for rm command

#### Phase 1.4: Cache Hit Rate Validation
```python
def test_decision_cache_integration():
    """
    Validates decision cache in end-to-end context

    Steps:
    1. Query cache with 100 known patterns
    2. Measure hit rate (expecting 85.7% based on Week 19)
    3. Validate cache lookup time (<1ms)
    4. Test cache miss fallback

    Success Criteria:
    - Cache hit rate: >80% (expecting 85.7%)
    - Lookup time: <1ms median
    - Cache size: 288+ patterns loaded
    """
    pass
```

### Estimated Time: 4-6 hours
- Implementation: 3-4 hours
- Execution: 30-60 minutes
- Analysis: 30-60 minutes

---

## Task 2: Performance Testing

**Goal:** Benchmark critical performance metrics with large sample sizes

### Benchmark 2.1: IPC Latency (100,000 samples)

```python
def benchmark_ipc_latency_100k():
    """
    Measure IPC latency with 100,000 message samples

    Test methodology:
    1. Initialize IPC ring buffer
    2. Send 100,000 ping messages
    3. Measure round-trip time for each
    4. Calculate statistics

    Metrics:
    - Median latency (target: <100μs, expecting ~54μs)
    - 99th percentile latency
    - Max latency
    - Throughput (messages/second)

    Success Criteria:
    - Median latency: <100μs
    - 99th percentile: <200μs
    - Zero message drops
    - Total test time: <10 seconds
    """

    import time
    import statistics

    latencies = []

    for i in range(100_000):
        start = time.perf_counter()
        # Send ping message via IPC
        ipc_client.send_message("PING")
        response = ipc_client.receive_message(timeout=0.001)
        end = time.perf_counter()

        latency_us = (end - start) * 1_000_000
        latencies.append(latency_us)

    results = {
        'median_us': statistics.median(latencies),
        'p99_us': statistics.quantiles(latencies, n=100)[98],
        'max_us': max(latencies),
        'min_us': min(latencies),
        'mean_us': statistics.mean(latencies),
        'throughput_msgs_per_sec': 100_000 / sum(latencies) * 1_000_000
    }

    return results
```

**Expected results (based on Phase 0 validation):**
- Median: ~54μs (46% under target)
- P99: ~60-80μs
- Throughput: ~16-20 million messages/second

### Benchmark 2.2: Decision Cache Hit Rate (1,000 queries)

```python
def benchmark_cache_hit_rate_1000q():
    """
    Measure cache hit rate with diverse query set

    Query composition:
    - 80% common queries (should hit cache)
    - 15% variations (may hit with normalization)
    - 5% novel queries (should miss)

    Metrics:
    - Overall hit rate (target: >80%, expecting 85.7%)
    - Hit rate by category
    - Average lookup time

    Success Criteria:
    - Overall hit rate: >80%
    - Lookup time: <1ms median
    - Zero cache errors
    """

    queries = generate_diverse_queries(1000)
    # 800 common: "list files", "show processes", etc.
    # 150 variations: "show me files", "what processes running"
    # 50 novel: "calculate fibonacci", "play music"

    hits = 0
    misses = 0
    lookup_times = []

    for query in queries:
        start = time.perf_counter()
        result = decision_cache.lookup(query)
        end = time.perf_counter()

        if result.hit:
            hits += 1
        else:
            misses += 1

        lookup_times.append((end - start) * 1000)  # ms

    hit_rate = hits / 1000

    return {
        'hit_rate': hit_rate,
        'hits': hits,
        'misses': misses,
        'median_lookup_ms': statistics.median(lookup_times),
        'p99_lookup_ms': statistics.quantiles(lookup_times, n=100)[98]
    }
```

**Query generation strategy:**
- Load 288+ cache patterns
- Create variations (synonym substitution, word order)
- Add noise queries (out of domain)

### Benchmark 2.3: AI Response Time (100 queries)

```python
def benchmark_ai_response_time_100q():
    """
    Measure AI inference time with 100 diverse queries

    Query types:
    - Simple (cache hit): 40 queries
    - Medium (Phi-3 inference): 40 queries
    - Complex (multi-agent routing): 20 queries

    Metrics:
    - Cached response time (target: <2s, expecting <1s)
    - Uncached response time (target: <3s, expecting ~558ms GPU)
    - Multi-agent response time

    Success Criteria:
    - Cached: <2s median
    - Uncached: <3s median
    - GPU inference: <600ms (Phi-3 Mini Q4)
    - CPU fallback: <1500ms (if GPU unavailable)
    """

    queries_simple = [...]  # Cache hits
    queries_medium = [...]  # Phi-3 inference required
    queries_complex = [...] # Multi-agent coordination

    results = {
        'cached_times': [],
        'uncached_phi3_times': [],
        'multi_agent_times': []
    }

    # Test each category
    for query in queries_simple:
        start = time.time()
        response = shell.process_query(query)
        end = time.time()
        results['cached_times'].append(end - start)

    # Calculate statistics
    return {
        'cached_median_ms': statistics.median(results['cached_times']) * 1000,
        'uncached_median_ms': statistics.median(results['uncached_phi3_times']) * 1000,
        'multi_agent_median_ms': statistics.median(results['multi_agent_times']) * 1000
    }
```

**Model considerations:**
- Use Phi-3 Mini 3.8B Q4 (primary model)
- Test TinyLlama 1.1B Q4 fallback
- Measure dynamic scaling transitions (IDLE→ACTIVE→CRITICAL)

### Performance Report Format

```json
{
  "test_date": "2025-11-28T14:30:00Z",
  "system_info": {
    "os": "WSL Ubuntu 24.04",
    "gpu": "RTX 2070",
    "cpu_cores": 8,
    "ram_gb": 16
  },
  "ipc_latency": {
    "samples": 100000,
    "median_us": 54.2,
    "p99_us": 62.1,
    "max_us": 120.5,
    "throughput_msgs_per_sec": 18450000,
    "target_met": true
  },
  "cache_hit_rate": {
    "queries": 1000,
    "hit_rate": 0.857,
    "hits": 857,
    "misses": 143,
    "median_lookup_ms": 0.021,
    "target_met": true
  },
  "ai_response_time": {
    "queries": 100,
    "cached_median_ms": 85.3,
    "uncached_median_ms": 558.7,
    "multi_agent_median_ms": 612.4,
    "targets_met": true
  },
  "overall_status": "PASS"
}
```

### Estimated Time: 6-8 hours
- Implementation: 4-5 hours
- Execution: 1-2 hours (100K samples + 1000 queries + 100 AI queries)
- Analysis: 1 hour

---

## Task 3: Stability Testing (12-hour automated)

**Goal:** Validate system stability under continuous operation

### Test Design

```python
class StabilityTest12Hour:
    """
    12-hour continuous stability test with automated monitoring

    Test flow:
    1. Initialize all components (QEMU, agents, SHIELD)
    2. Enter continuous loop:
       - Generate random command
       - Execute command
       - Monitor system state
       - Log results
       - Sleep random interval (1-10s)
    3. After 12 hours:
       - Generate summary report
       - Shutdown gracefully

    Monitoring:
    - Memory usage (detect leaks)
    - CPU usage (detect runaway processes)
    - Crash detection (process termination)
    - Deadlock detection (timeout on operations)
    - Cache coherency (hit rate drift)
    - Agent health (restart counts)
    """

    def __init__(self):
        self.start_time = None
        self.end_time = None
        self.duration_hours = 12

        # Statistics
        self.stats = {
            'commands_executed': 0,
            'commands_succeeded': 0,
            'commands_failed': 0,
            'shield_blocks': 0,
            'agent_restarts': 0,
            'cache_hits': 0,
            'cache_misses': 0,
            'memory_samples': [],
            'cpu_samples': [],
            'crashes': [],
            'deadlocks': []
        }

        # Components
        self.qemu_manager = QEMUManager()
        self.agent_router = AgentRouter()
        self.shield = SHIELDFramework()
        self.monitor = SystemMonitor()
        self.logger = TestLogger('stability_12h')
        self.cmd_generator = RandomCommandGenerator()

    def run(self):
        """Execute 12-hour stability test"""
        self.logger.info("Starting 12-hour stability test")
        self.start_time = time.time()
        target_end_time = self.start_time + (12 * 3600)

        try:
            # Initialize components
            self._initialize_components()

            # Main test loop
            while time.time() < target_end_time:
                self._run_iteration()

            # Graceful shutdown
            self._shutdown_components()

        except KeyboardInterrupt:
            self.logger.warning("Test interrupted by user")
            self._emergency_shutdown()
        except Exception as e:
            self.logger.error(f"Test crashed: {e}")
            self._emergency_shutdown()
        finally:
            self._generate_report()

    def _run_iteration(self):
        """Single test iteration"""
        # Generate random command
        cmd = self.cmd_generator.generate()

        # Monitor system before execution
        mem_before = self.monitor.get_memory_usage()
        cpu_before = self.monitor.get_cpu_usage()

        # Execute command with timeout
        try:
            with Timeout(30):  # 30s timeout per command
                result = self._execute_command(cmd)
                self.stats['commands_executed'] += 1

                if result.success:
                    self.stats['commands_succeeded'] += 1
                else:
                    self.stats['commands_failed'] += 1

                if result.shield_blocked:
                    self.stats['shield_blocks'] += 1

                if result.cache_hit:
                    self.stats['cache_hits'] += 1
                else:
                    self.stats['cache_misses'] += 1

        except TimeoutError:
            self.logger.error(f"Command timeout: {cmd}")
            self.stats['deadlocks'].append({
                'time': time.time(),
                'command': cmd
            })

        # Monitor system after execution
        mem_after = self.monitor.get_memory_usage()
        cpu_after = self.monitor.get_cpu_usage()

        # Record metrics
        self.stats['memory_samples'].append(mem_after)
        self.stats['cpu_samples'].append(cpu_after)

        # Check for memory leaks
        if mem_after > mem_before + 100:  # 100MB increase
            self.logger.warning(f"Memory spike detected: {mem_after - mem_before}MB")

        # Random sleep (1-10 seconds)
        time.sleep(random.uniform(1, 10))

        # Periodic health check (every 100 iterations)
        if self.stats['commands_executed'] % 100 == 0:
            self._health_check()

    def _health_check(self):
        """Periodic health check"""
        # Check agent health
        for agent in self.agent_router.agents:
            if agent.status != AgentStatus.HEALTHY:
                self.logger.warning(f"Agent {agent.name} unhealthy, restarting")
                agent.restart()
                self.stats['agent_restarts'] += 1

        # Check cache coherency
        current_hit_rate = self.stats['cache_hits'] / (self.stats['cache_hits'] + self.stats['cache_misses'])
        if current_hit_rate < 0.75:  # 5% tolerance below 80% target
            self.logger.warning(f"Cache hit rate degraded: {current_hit_rate:.1%}")

        # Log progress
        elapsed_hours = (time.time() - self.start_time) / 3600
        self.logger.info(f"Progress: {elapsed_hours:.1f}/12 hours, {self.stats['commands_executed']} commands")
```

### Random Command Generator

```python
class RandomCommandGenerator:
    """Generate random valid commands for stress testing"""

    def __init__(self):
        self.commands = {
            'safe': [
                'help',
                'status',
                'cache',
                'agent',
                'shield',
                'ls',
                'ps',
                'top',
                'battery',
                'uptime',
                'ifconfig',
                'ping 127.0.0.1'
            ],
            'shield_validated': [
                'mkdir /tmp/test_{id}',
                'rm /tmp/test_{id}',
                'kill {pid}'  # Use own PID (safe)
            ],
            'shield_blocked': [
                'shutdown',
                'reboot'
            ]
        }

        # Weight distribution (favor safe commands)
        self.weights = {
            'safe': 0.80,           # 80% safe
            'shield_validated': 0.15, # 15% validated
            'shield_blocked': 0.05   # 5% blocked (test SHIELD)
        }

    def generate(self):
        """Generate random command"""
        category = random.choices(
            list(self.weights.keys()),
            weights=list(self.weights.values())
        )[0]

        cmd_template = random.choice(self.commands[category])

        # Fill in placeholders
        cmd = cmd_template.replace('{id}', str(random.randint(1000, 9999)))
        cmd = cmd_template.replace('{pid}', str(os.getpid()))

        return cmd
```

### System Monitor

```python
class SystemMonitor:
    """Monitor system resources during stability test"""

    def __init__(self):
        import psutil
        self.psutil = psutil
        self.process = psutil.Process()

    def get_memory_usage(self):
        """Get current memory usage in MB"""
        return self.process.memory_info().rss / 1024 / 1024

    def get_cpu_usage(self):
        """Get current CPU usage %"""
        return self.process.cpu_percent(interval=0.1)

    def detect_deadlock(self):
        """Simple deadlock detection"""
        # Check if any threads are blocked for >60s
        # (Implementation depends on threading model)
        pass
```

### Overnight Run Strategy

**Before starting (evening):**
1. Compile C components (cache, IPC)
2. Verify Python dependencies installed
3. Set up logging directory with disk space check
4. Configure QEMU to run headless
5. Start test script in background with nohup

**During run (overnight):**
- Log rotates every hour (12 log files)
- Periodic checkpoints every 2 hours (memory snapshot)
- Automatic recovery on minor errors
- Emergency shutdown on critical errors

**Morning verification:**
1. Check if test completed (12-hour mark reached)
2. Review summary report
3. Check for crashes (search logs for "ERROR", "CRASH")
4. Verify memory stability (no leaks)
5. Check cache hit rate (should stay >80%)

### Startup Script

```bash
#!/bin/bash
# run_stability_12h.sh

# Set up logging
export LOG_DIR="phase1/src/integration_tests/logs"
mkdir -p $LOG_DIR

# Compile C components
cd phase1/src/cache
gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm

cd ../ipc
gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm

cd ../..

# Run stability test in background
nohup python3 src/integration_tests/test_stability_12h.py > $LOG_DIR/stability_stdout.log 2>&1 &

echo "Stability test started (PID: $!)"
echo "Check progress: tail -f $LOG_DIR/stability_12h_*.log"
echo "Morning verification: python3 src/integration_tests/verify_stability_results.py"
```

### Estimated Time: 12+ hours
- Implementation: 4-6 hours
- Execution: 12 hours (overnight)
- Analysis: 1-2 hours (morning)

---

## Task 4: Issue Resolution Strategy

### Issue Classification

**P0 (Critical - Must Fix):**
- QEMU boot failure
- System crashes during stability test
- Deadlocks (>3 occurrences in 12 hours)
- Memory leaks (>500MB growth over 12 hours)
- SHIELD false positives (blocking safe commands)

**P1 (High Priority - Should Fix):**
- Performance regression (>20% slower than target)
- Agent restart failures
- Cache hit rate degradation (below 75%)
- Command execution failures (>5% failure rate)

**P2 (Medium Priority - Nice to Fix):**
- Performance optimization opportunities
- Logging improvements
- Documentation gaps
- Non-critical warnings

### Common Failure Scenarios & Fixes

#### Scenario 1: Memory Leak Detected
```python
# Symptom: Memory usage grows from 2GB to 3GB+ over 12 hours
# Root cause: AI model not releasing memory after inference

# Fix:
import gc
import torch

def execute_ai_query(query):
    result = model.generate(query)

    # Force garbage collection
    gc.collect()

    # Clear GPU cache (if using PyTorch)
    if torch.cuda.is_available():
        torch.cuda.empty_cache()

    return result
```

#### Scenario 2: Deadlock on IPC
```python
# Symptom: Command hangs for >30s
# Root cause: IPC ring buffer full, sender blocked

# Fix: Add timeout to IPC send
def send_with_timeout(message, timeout=5.0):
    start = time.time()
    while not ring_buffer.try_send(message):
        if time.time() - start > timeout:
            raise TimeoutError("IPC send timeout")
        time.sleep(0.001)
```

#### Scenario 3: Cache Hit Rate Degradation
```python
# Symptom: Hit rate drops from 85% to 70% during test
# Root cause: Query normalization inconsistency

# Fix: Standardize normalization
def normalize_query(query):
    # Convert to lowercase
    query = query.lower()

    # Collapse whitespace
    query = ' '.join(query.split())

    # Remove punctuation (except essential)
    query = query.rstrip('?!.')

    # Apply consistent stemming
    # ... implementation ...

    return query
```

### Re-run Strategy

**When to re-run:**
- After fixing P0 issues (always)
- After fixing multiple P1 issues (if >3 fixed)
- After major code changes

**What to re-run:**
- Full 12-hour stability test (if crash/deadlock fixed)
- Performance benchmarks only (if performance fix)
- Specific failed test (if isolated issue)

---

## Implementation Phases

### Phase 1: Test Infrastructure (4-6 hours)
**Tasks:**
- Create directory structure
- Implement utility classes (QEMUManager, SystemMonitor, TestLogger)
- Set up logging framework
- Create random command generator
- Validate with smoke tests

**Deliverables:**
- `utils/qemu_manager.py`
- `utils/system_monitor.py`
- `utils/test_logger.py`
- `utils/random_command_generator.py`

### Phase 2: Task 1 - E2E Integration (4-6 hours)
**Tasks:**
- Implement `test_e2e_integration.py`
- QEMU boot validation
- Multi-agent loading test
- All 27 commands execution test
- Run and validate results

**Deliverables:**
- `test_e2e_integration.py` (complete)
- E2E test report (JSON)

### Phase 3: Task 2 - Performance Benchmarks (6-8 hours)
**Tasks:**
- Implement `test_performance_benchmark.py`
- IPC latency benchmark (100K samples)
- Cache hit rate benchmark (1000 queries)
- AI response time benchmark (100 queries)
- Run and analyze results

**Deliverables:**
- `test_performance_benchmark.py` (complete)
- Performance report (JSON)

### Phase 4: Task 3 - Stability Test (14-18 hours)
**Tasks:**
- Implement `test_stability_12h.py`
- Create startup script (`run_stability_12h.sh`)
- Create verification script (`verify_stability_results.py`)
- Run overnight (12 hours)
- Morning verification and analysis

**Deliverables:**
- `test_stability_12h.py` (complete)
- 12-hour stability log
- Stability summary report

### Phase 5: Task 4 - Issue Resolution (Variable)
**Tasks:**
- Review all test results
- Classify issues (P0/P1/P2)
- Fix P0 issues
- Fix P1 issues (time permitting)
- Re-run affected tests
- Update documentation

**Deliverables:**
- Issue resolution log
- Updated code with fixes
- Re-run test results

### Phase 6: Documentation & Wrap-Up (2-3 hours)
**Tasks:**
- Create WEEK_21_STATUS.md
- Create WEEK_21_RESULTS.md
- Update PHASE_1_PROGRESS_TRACKER.md
- Update CLAUDE.md
- Summary presentation of results

**Deliverables:**
- Complete Week 21 documentation
- Phase 1 progress update (80.8%, 21/26 weeks)

---

## Success Criteria

### Task 1: End-to-End Integration
- [x] QEMU boots successfully (<60s)
- [x] All 4 agents load (Device, Network, FileSystem, User)
- [x] All 27 commands execute (safe + SHIELD validated + blocked)
- [x] SHIELD: 100% dangerous ops validated, 0% false positives
- [x] Cache hit rate: >80%

### Task 2: Performance Testing
- [x] IPC latency: <100μs median (expecting ~54μs)
- [x] Cache hit rate: >80% (expecting 85.7%)
- [x] AI response cached: <2s (expecting <1s)
- [x] AI response uncached: <3s (expecting ~558ms GPU)
- [x] Zero crashes during benchmarks

### Task 3: Stability Testing
- [x] 12-hour continuous operation: complete
- [x] Commands executed: >1000
- [x] Crash count: 0
- [x] Deadlock count: <3
- [x] Memory growth: <500MB over 12 hours
- [x] Cache hit rate stability: >75% throughout
- [x] Agent restart count: <10

### Task 4: Issue Resolution
- [x] All P0 issues fixed and validated
- [x] All P1 issues fixed or documented for Phase 2
- [x] P2 issues documented in technical debt log

### Overall Week 21 Completion
- [x] All 4 tasks complete
- [x] All gate criteria met
- [x] Documentation complete
- [x] Phase 1 at 80.8% (21/26 weeks)

---

## Files to Create

### New Files (7)
1. `phase1/src/integration_tests/test_e2e_integration.py` (~400 lines)
2. `phase1/src/integration_tests/test_performance_benchmark.py` (~500 lines)
3. `phase1/src/integration_tests/test_stability_12h.py` (~600 lines)
4. `phase1/src/integration_tests/utils/qemu_manager.py` (~200 lines)
5. `phase1/src/integration_tests/utils/system_monitor.py` (~150 lines)
6. `phase1/src/integration_tests/utils/test_logger.py` (~100 lines)
7. `phase1/src/integration_tests/utils/random_command_generator.py` (~150 lines)

### Scripts (3)
1. `phase1/src/integration_tests/run_stability_12h.sh`
2. `phase1/src/integration_tests/verify_stability_results.py`
3. `phase1/src/integration_tests/run_integration_tests.sh`

### Documentation (2)
1. `phase1/weeks/week21/WEEK_21_STATUS.md`
2. `phase1/weeks/week21/WEEK_21_RESULTS.md`

### Modified Files (2)
1. `phase1/docs/PHASE_1_PROGRESS_TRACKER.md` (add Week 21 entry)
2. `CLAUDE.md` (update current status to Week 21)

---

## Total Estimated Effort

| Phase | Tasks | Estimated Hours |
|-------|-------|-----------------|
| Phase 1 | Infrastructure | 4-6 hours |
| Phase 2 | E2E Integration | 4-6 hours |
| Phase 3 | Performance | 6-8 hours |
| Phase 4 | Stability (12h run) | 14-18 hours |
| Phase 5 | Issue Resolution | Variable (2-8 hours) |
| Phase 6 | Documentation | 2-3 hours |
| **TOTAL** | | **32-49 hours** |

**Note:** Phase 4 includes 12-hour overnight run (unattended). Active work is ~18-24 hours.

---

## Next Steps

1. Review this plan and confirm approach
2. Start Phase 1: Test Infrastructure (4-6 hours)
3. Progress through phases sequentially
4. Start overnight stability test (evening)
5. Morning verification and issue resolution
6. Complete documentation

**Ready to begin implementation?**
