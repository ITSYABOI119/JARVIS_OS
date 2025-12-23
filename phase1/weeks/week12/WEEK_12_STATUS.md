# Week 12: Agent Health Monitoring & Failover

**Date:** November 20, 2025
**Status:** IN PROGRESS
**Effort:** 0/10-14 hours

---

## Overview

Week 12 implements robust health monitoring and automatic failover mechanisms for all 4 specialist agents. This ensures zero service interruption when agents fail, crash, or become unresponsive.

---

## Prerequisites

✅ **All prerequisites met from Weeks 1-11:**
- ✅ seL4 boots in QEMU (~2s boot time)
- ✅ Decision cache working (85.7% hit rate, 103 patterns)
- ✅ IPC system functional (0.050μs latency)
- ✅ Python IPC client validated (6/6 tests passing)
- ✅ AI agent working (Phi-3 Mini, 64ms inference)
- ✅ Shell interface functional (30/30 tests passing)
- ✅ **Multi-agent architecture complete (4 agents, 100% routing accuracy)**
- ✅ **Shared context pool functional**
- ✅ **Conflict resolution complete (49/49 tests passing)**
- ✅ **Agent router working (112/112 total tests passing)**

---

## Objectives

### Primary Objectives
1. **Implement health monitoring system** - Track agent heartbeats, detect failures
2. **Create failover mechanism** - Automatic recovery when agents fail
3. **Build agent lifecycle management** - Start/stop/restart agents cleanly
4. **Test fault tolerance** - Validate zero service interruption

### Success Criteria
- ✅ Health monitoring tracks all 4 agents (heartbeat every 5s)
- ✅ Failover recovery time <5 seconds
- ✅ Agent restart working (100% success rate)
- ✅ Zero service interruptions during failover
- ✅ Test coverage: 45+ tests passing (100% pass rate)
- ✅ Health check overhead <0.1ms
- ✅ Long-running stability (1+ hour with random failures)

---

## Tasks

### Task 1: Health Monitoring System (2-3 hours)

**Subtasks:**
1. Design `AgentHealthMonitor` class API
   - Heartbeat tracking (5-second intervals)
   - Timeout detection (10 seconds = failed)
   - Health state transitions (healthy → degraded → failed)
   - Statistics tracking (uptime, failures, recoveries)

2. Implement heartbeat mechanism
   - Each agent sends heartbeat every 5 seconds
   - Monitor receives and timestamps heartbeats
   - Background thread checks for timeouts

3. Implement health state machine
   - **Healthy:** Receiving heartbeats on time
   - **Degraded:** Late heartbeats (>7s but <10s)
   - **Failed:** No heartbeat for 10+ seconds
   - Transitions logged and tracked

4. Add health statistics
   - Total uptime per agent
   - Failure count per agent
   - Recovery count per agent
   - Current health status

**Deliverables:**
- [ ] `agent_health.py` - AgentHealthMonitor class (~400 lines)
- [ ] Heartbeat mechanism integrated into all 4 agents
- [ ] Health state machine implemented
- [ ] Statistics tracking functional

**Testing:**
- [ ] Test 1: Heartbeat tracking
- [ ] Test 2: Timeout detection (agent stops responding)
- [ ] Test 3: Health state transitions
- [ ] Test 4: Multiple agent health tracking
- [ ] Test 5: Health statistics
- [ ] Tests 6-10: Edge cases (late heartbeat, intermittent failures)

**Total: 10 health monitoring tests**

---

### Task 2: Failover Mechanism (3-4 hours)

**Subtasks:**
1. Design `AgentFailoverManager` class API
   - Detect failed agents from health monitor
   - Retry query on agent failure
   - Fall back to rule-based handler if agent unavailable
   - Track failover events

2. Implement retry logic
   - Retry failed query up to 3 times (exponential backoff)
   - Switch to backup strategy if retries fail
   - Log all retry attempts

3. Create rule-based fallback handlers
   - Device agent fallback: Simple system commands (df, free, etc.)
   - Network agent fallback: Basic connectivity checks
   - FileSystem agent fallback: Direct file operations
   - User agent fallback: Static help text

4. Implement recovery strategies
   - **Strategy 1:** Restart agent (most common)
   - **Strategy 2:** Use backup agent (if available)
   - **Strategy 3:** Degrade gracefully (inform user)

5. Ensure zero service interruption
   - User queries continue during agent restart
   - Fallback handlers provide immediate response
   - Transparent failover (user doesn't notice)

**Deliverables:**
- [ ] `agent_failover.py` - AgentFailoverManager class (~350 lines)
- [ ] Retry logic with exponential backoff
- [ ] Rule-based fallback handlers for all 4 agents
- [ ] Recovery strategies implemented
- [ ] Failover events logged

**Testing:**
- [ ] Test 1: Detect failed agent
- [ ] Test 2: Retry query on failure
- [ ] Test 3: Fall back to rule-based handler
- [ ] Test 4: Failover without service interruption
- [ ] Test 5: Log failures correctly
- [ ] Tests 6-10: Different agent failures (Device, Network, FileSystem, User)
- [ ] Tests 11-15: Multi-agent failures, cascading failures

**Total: 15 failover tests**

---

### Task 3: Agent Lifecycle Management (2-3 hours)

**Subtasks:**
1. Design `AgentLifecycleManager` class API
   - Start agent process
   - Stop agent process cleanly
   - Restart agent process
   - Check agent process status

2. Implement process management
   - Use Python `subprocess` module
   - Track process IDs (PIDs)
   - Monitor process health via heartbeats
   - Kill process if hung (SIGTERM → SIGKILL)

3. Implement model reload handling
   - Check if model needs reloading on restart
   - Reload Phi-3 Mini if agent process was killed
   - Cache model in memory if possible (avoid reload delay)

4. Implement state preservation
   - Define minimal critical state (statistics, open connections)
   - Serialize state on shutdown
   - Restore state on restart
   - Agents can restart clean if state lost (not critical)

5. Add restart verification
   - Verify agent process started successfully
   - Wait for first heartbeat (confirms operational)
   - Timeout if restart fails (10 seconds)

**Deliverables:**
- [ ] `agent_lifecycle.py` - AgentLifecycleManager class (~300 lines)
- [ ] Process start/stop/restart methods
- [ ] Model reload handling (Phi-3 Mini)
- [ ] State preservation (minimal)
- [ ] Restart verification

**Testing:**
- [ ] Test 1: Start agent process
- [ ] Test 2: Stop agent process cleanly
- [ ] Test 3: Restart agent process
- [ ] Test 4: Detect crashed agent
- [ ] Test 5: Reload AI model on restart
- [ ] Test 6: Restore agent context/state
- [ ] Test 7: Verify agent operational after restart
- [ ] Tests 8-10: Different restart scenarios (clean shutdown, crash, hang)

**Total: 10 restart tests**

---

### Task 4: Integration & Testing (2-3 hours)

**Subtasks:**
1. Integrate with `AgentRouter` (from Week 11)
   - Add health monitoring to router
   - Route queries through failover manager
   - Update shared context with health status

2. Update shell to show failover status
   - Add "agent status" command
   - Show health of all 4 agents
   - Display failover events in real-time
   - Show recovery time statistics

3. Create comprehensive integration tests
   - End-to-end failover (query → fail → recover → query)
   - Performance during failover
   - Multiple simultaneous failures
   - Long-running stability test (1+ hour with random failures)

4. Document results
   - Create WEEK_12_RESULTS.md
   - Record test results (45+ tests)
   - Performance metrics (recovery time, overhead)
   - Lessons learned

**Deliverables:**
- [ ] `test_agent_failover.py` - Comprehensive test suite (~500 lines)
- [ ] Integration with AgentRouter
- [ ] Shell updates (agent status command)
- [ ] WEEK_12_RESULTS.md - Results documentation

**Testing:**
- [ ] Test 1: End-to-end failover (query → fail → recover → query)
- [ ] Test 2: Performance during failover (measure overhead)
- [ ] Test 3: Multiple simultaneous failures
- [ ] Test 4: Cascading failures (one agent failure triggers others)
- [ ] Test 5: Long-running stability (1+ hour, random failures)
- [ ] Test 6: Recovery time <5 seconds
- [ ] Test 7: Health check overhead <0.1ms
- [ ] Test 8: Agent restart <10 seconds
- [ ] Test 9: Zero service interruptions (user queries continue)
- [ ] Test 10: Failover statistics accurate

**Total: 10 integration tests**

---

## Deliverables Summary

**Code Files:**
- [ ] `phase1/src/ai/agent_health.py` - Health monitoring (~400 lines)
- [ ] `phase1/src/ai/agent_failover.py` - Failover manager (~350 lines)
- [ ] `phase1/src/ai/agent_lifecycle.py` - Lifecycle manager (~300 lines)
- [ ] `phase1/src/ai/test_agent_failover.py` - Test suite (~500 lines)

**Test Files:**
- [ ] Health monitoring tests: 10 tests
- [ ] Failover tests: 15 tests
- [ ] Restart tests: 10 tests
- [ ] Integration tests: 10 tests
- [ ] **Total: 45+ tests**

**Documentation:**
- [ ] `phase1/weeks/week12/WEEK_12_STATUS.md` - This file
- [ ] `phase1/weeks/week12/WEEK_12_RESULTS.md` - Test results

**Integration:**
- [ ] agent_router.py updated (health monitoring integration)
- [ ] shell.py updated (agent status command)

**Total New Code:** ~1,550 lines of implementation + ~500 lines of tests = **2,050+ lines**

---

## Testing Plan

### Test Coverage Goal: 45+ tests, 100% pass rate

**Test Suite 1: Health Monitoring (10 tests)**
- Heartbeat tracking
- Timeout detection
- Health state transitions
- Multi-agent monitoring
- Statistics tracking
- Edge cases

**Test Suite 2: Failover Logic (15 tests)**
- Failed agent detection
- Retry mechanisms
- Rule-based fallbacks
- Recovery strategies
- Service continuity
- Agent-specific failures
- Multi-agent failures

**Test Suite 3: Lifecycle Management (10 tests)**
- Process start/stop/restart
- Model reload
- State preservation
- Crash detection
- Restart verification
- Different scenarios

**Test Suite 4: Integration (10 tests)**
- End-to-end failover
- Performance metrics
- Multiple failures
- Long-running stability
- Statistics accuracy

---

## Success Criteria

| Criterion | Target | Status |
|-----------|--------|--------|
| **Health monitoring working** | 100% uptime detection | ☐ |
| **Failover recovery time** | <5 seconds | ☐ |
| **Agent restart success** | 100% success rate | ☐ |
| **Service interruptions** | 0 during failover | ☐ |
| **Test coverage** | 45+ tests, 100% pass | ☐ |
| **Health check overhead** | <0.1ms | ☐ |
| **Agent restart time** | <10 seconds | ☐ |
| **Long-running stability** | 1+ hour, random failures | ☐ |

---

## Timeline

**Total Estimated Effort:** 10-14 hours

| Task | Estimated | Actual | Status |
|------|-----------|--------|--------|
| Task 1: Health Monitoring | 2-3 hours | - | ☐ Not Started |
| Task 2: Failover Mechanism | 3-4 hours | - | ☐ Not Started |
| Task 3: Lifecycle Management | 2-3 hours | - | ☐ Not Started |
| Task 4: Integration & Testing | 2-3 hours | - | ☐ Not Started |

**Note:** Based on Week 11 efficiency (32-45% ahead of schedule), Week 12 may complete in 6-9 hours.

---

## Technical Notes

### Health Monitoring Design

**Heartbeat Protocol:**
```python
# Each agent sends heartbeat every 5 seconds
heartbeat = {
    "agent_name": "device",
    "timestamp": time.time(),
    "status": "healthy",
    "queries_processed": 123,
    "avg_response_time": 4.5
}
```

**Health States:**
- **Healthy:** Heartbeat received within 5 seconds
- **Degraded:** Heartbeat received between 5-10 seconds (warning)
- **Failed:** No heartbeat for 10+ seconds (trigger failover)

**Monitoring Thread:**
- Background thread checks heartbeats every 1 second
- Detects timeouts and triggers failover
- Updates shared context with health status

### Failover Strategies

**Strategy 1: Restart Agent** (most common)
- Detect failed agent via health monitor
- Kill agent process (if still running)
- Start new agent process
- Reload model if needed
- Wait for first heartbeat
- Resume normal operation

**Strategy 2: Rule-Based Fallback** (temporary)
- Use simple rule-based handler while agent restarts
- Device: `df`, `free`, `lscpu` commands
- Network: `ping`, `ip addr` commands
- FileSystem: Direct Python file operations
- User: Static help text

**Strategy 3: Degrade Gracefully** (last resort)
- Inform user agent is unavailable
- Log failure for analysis
- Continue with remaining agents

### Process Management

**Using Python subprocess:**
```python
import subprocess

# Start agent
process = subprocess.Popen(
    ["python", "device_agent.py"],
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE
)

# Monitor process
if process.poll() is None:
    # Process running
else:
    # Process crashed
```

**Restart Procedure:**
1. Detect failure (no heartbeat for 10s)
2. Kill process (SIGTERM, wait 5s, SIGKILL if needed)
3. Start new process
4. Reload model (Phi-3 Mini ~1.2s)
5. Wait for first heartbeat (confirm operational)
6. Resume routing queries to agent

---

## Blockers & Risks

**Current Blockers:** None

**Potential Risks:**
1. **Risk:** Process restart slow (>10s with model reload)
   - **Mitigation:** Cache model in memory, use lazy loading

2. **Risk:** State loss on restart
   - **Mitigation:** Preserve minimal critical state, agents can restart clean

3. **Risk:** Cascading failures (one agent triggers others)
   - **Mitigation:** Independent health monitoring, isolation

4. **Risk:** Failover complexity in WSL2 environment
   - **Mitigation:** Start with simple subprocess management, enhance later

---

## References

**Original Plan:** `phase1/docs/PHASE_1_IMPLEMENTATION_PLAN.md` (Week 12)
**Critical Specs:** `CRITICAL_SPECIFICATIONS.md` (lines 805-853, Agent Health Monitoring)
**Week 11 Multi-Agent:** `phase1/weeks/week11/WEEK_11_RESULTS.md`

---

## Notes

**Week 12 Importance:**
- Ensures system reliability and fault tolerance
- Critical for production readiness
- Enables autonomous recovery (no manual intervention)
- Foundation for dynamic scaling (Week 13-14)

**Lessons from Week 11:**
- Start with simple design, iterate
- Test each component independently
- Integration last, not first
- Expect 30-45% time efficiency gain

**Confidence Level:** 90% (based on Week 11 success at 95%)

---

**Status:** IN PROGRESS
**Next Steps:** Begin Task 1 (Health Monitoring System)
**Estimated Completion:** Week 12 end (6-9 hours actual effort expected)
