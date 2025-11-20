# Week 11: Multi-Agent Architecture - Specialist Agents

**Date:** November 20, 2025
**Status:** READY TO START
**Effort:** 0/16 hours

---

## Overview

Week 11 implements the multi-agent architecture with 4 specialist agents:
1. **Device Manager Agent** - Hardware operations
2. **Network Agent** - Network operations
3. **FileSystem Agent** - File operations
4. **User Interaction Agent** - User queries and shell commands

This architecture distributes workload, improves response time, and enables specialized decision-making.

---

## Prerequisites

✅ **All prerequisites met from Weeks 1-10:**
- ✅ seL4 boots in QEMU (~2s boot time)
- ✅ Decision cache working (85.7% hit rate, 103 patterns)
- ✅ IPC system functional (10/10 messages, 0% drops)
- ✅ Python IPC client validated (6/6 tests passing)
- ✅ AI agent working (Phi-3 Mini, 64ms inference)
- ✅ Shell interface functional (30/30 tests passing)
- ✅ Test infrastructure created (end-to-end + benchmarking)

---

## Objectives

### Primary Objectives
1. **Create 4 specialist agents** with domain expertise
2. **Implement agent router** to dispatch queries to correct agent
3. **Create shared context pool** for agent coordination
4. **Test multi-agent query handling** with routing and coordination

### Success Criteria
- ✅ 4 agents load successfully (Device, Network, FileSystem, User)
- ✅ Agent router dispatches to correct agent (>90% accuracy)
- ✅ Shared context allows agents to see system state
- ✅ Multi-agent queries work (e.g., "copy file over network")
- ✅ Performance: <200ms response time for routed queries
- ✅ Test suite: 10+ multi-agent tests passing

---

## Tasks

### Task 1: Design Multi-Agent Architecture (2-3 hours)

**Subtasks:**
1. Define agent roles and responsibilities
   - Device Manager: Hardware queries, device status, drivers
   - Network: Network status, connectivity, SSH/HTTP
   - FileSystem: File operations, directory listing, permissions
   - User Interaction: General queries, help, shell commands

2. Design routing keywords
   - Device: "device", "hardware", "disk", "usb", "driver"
   - Network: "network", "ping", "ssh", "http", "connection"
   - FileSystem: "file", "directory", "ls", "cp", "mv", "rm", "chmod"
   - User: "help", "status", "cache", default fallback

3. Design shared context structure
   - System state (memory, CPU, disk usage)
   - Active operations (what other agents are doing)
   - Decision cache reference
   - IPC statistics

4. Design agent communication protocol
   - Query format: `{"agent": "device", "query": "disk space", "context": {...}}`
   - Response format: `{"agent": "device", "action": "show_disk_space", "result": {...}}`

**Deliverables:**
- [ ] Agent roles documented
- [ ] Routing keyword mapping created
- [ ] Shared context structure defined
- [ ] Communication protocol documented

**Files to create:**
- `phase1/src/ai/multi_agent_design.md` - Design document
- `phase1/src/ai/agent_router.py` (skeleton)
- `phase1/src/ai/shared_context.py` (skeleton)

---

### Task 2: Implement Specialist Agents (4-6 hours)

**Subtasks:**
1. Create base agent class
   - Common interface: `process_query(query, context)`
   - Shared initialization: model loading, IPC client
   - Logging and error handling

2. Implement Device Manager Agent
   - Keywords: device, hardware, disk, usb, driver, memory, cpu
   - Actions: show_disk_space, show_memory, show_cpu, list_devices
   - Uses system commands (df, free, lscpu, lsusb)

3. Implement Network Agent
   - Keywords: network, ping, ssh, http, connection, ip, port
   - Actions: show_network_status, ping_host, check_connection
   - Uses network commands (ip, ping, netstat)

4. Implement FileSystem Agent
   - Keywords: file, directory, ls, cp, mv, rm, chmod, find
   - Actions: list_directory, copy_file, move_file, remove_file
   - Uses file commands (ls, cp, mv, rm, find, stat)

5. Implement User Interaction Agent
   - Keywords: help, status, cache, agent, general queries
   - Actions: show_help, show_status, show_cache_stats
   - Default fallback for unrouted queries

**Deliverables:**
- [ ] `agent_base.py` - Base agent class
- [ ] `device_agent.py` - Device Manager Agent
- [ ] `network_agent.py` - Network Agent
- [ ] `filesystem_agent.py` - FileSystem Agent
- [ ] `user_agent.py` - User Interaction Agent
- [ ] Each agent tested independently

**Testing:**
- Each agent should handle 5+ test queries
- Response time <200ms per query
- Correct actions returned for keywords

---

### Task 3: Implement Agent Router (3-4 hours)

**Subtasks:**
1. Create keyword-based routing
   - Parse query for keywords
   - Match to agent (priority: Device > Network > FileSystem > User)
   - Default to User agent if no match

2. Implement shared context management
   - System state snapshot (memory, CPU, disk)
   - Active operations tracking
   - Cache statistics
   - IPC statistics

3. Create agent coordination
   - Route query to selected agent
   - Pass shared context to agent
   - Collect response from agent
   - Update shared context

4. Add logging and monitoring
   - Log all routing decisions
   - Track agent response times
   - Monitor agent health

**Deliverables:**
- [ ] `agent_router.py` - Complete router implementation
- [ ] `shared_context.py` - Context management
- [ ] Routing accuracy >90% on test queries
- [ ] Context sharing working between agents

**Testing:**
- 20+ routing test queries
- Verify correct agent selected
- Verify context passed correctly
- Measure routing overhead (<5ms)

---

### Task 4: Multi-Agent Testing (4-5 hours)

**Subtasks:**
1. Create multi-agent test suite
   - Single-agent queries (verify routing)
   - Multi-agent queries (e.g., "copy file over network")
   - Context sharing tests
   - Error handling tests

2. Performance benchmarking
   - Single-agent response time
   - Multi-agent response time
   - Routing overhead
   - Context access overhead

3. Integration with shell
   - Update shell.py to use agent router
   - Test routing from shell
   - Verify agent responses displayed correctly

4. Create comprehensive tests
   - `test_multi_agent.py` - Multi-agent test suite
   - `test_agent_router.py` - Router-specific tests
   - `test_shared_context.py` - Context management tests

**Deliverables:**
- [ ] `test_multi_agent.py` - 10+ tests
- [ ] `test_agent_router.py` - 8+ tests
- [ ] `test_shared_context.py` - 5+ tests
- [ ] All tests passing (>95% success rate)
- [ ] Performance benchmarks documented

**Testing:**
- Multi-agent queries work correctly
- Response time <200ms (single agent)
- Response time <300ms (multi-agent coordination)
- Routing overhead <5ms
- Context access <1ms

---

### Task 5: Integration with seL4 (3-4 hours) [OPTIONAL]

**Subtasks:**
1. Update main.c IPC handler
   - Support multi-agent queries
   - Route cache misses to appropriate agent
   - Return agent responses via IPC

2. Test Python → seL4 → Agent flow
   - Python sends query via IPC
   - seL4 receives query, checks cache
   - Cache miss → route to agent
   - Agent response → seL4 → Python

3. Create end-to-end test
   - Launch seL4 in QEMU
   - Python sends multi-agent query
   - Verify routing and response

**Deliverables:**
- [ ] main.c updated with multi-agent routing
- [ ] End-to-end test working in QEMU
- [ ] Response time <500ms (including IPC + routing + agent)

**Note:** This task is optional for Week 11. Can be deferred to Week 12 if Week 11 tasks take longer than expected.

---

## Deliverables Summary

**Code Files:**
- [ ] `phase1/src/ai/agent_base.py` - Base agent class
- [ ] `phase1/src/ai/device_agent.py` - Device Manager Agent
- [ ] `phase1/src/ai/network_agent.py` - Network Agent
- [ ] `phase1/src/ai/filesystem_agent.py` - FileSystem Agent
- [ ] `phase1/src/ai/user_agent.py` - User Interaction Agent
- [ ] `phase1/src/ai/agent_router.py` - Agent router with keyword matching
- [ ] `phase1/src/ai/shared_context.py` - Shared context pool

**Test Files:**
- [ ] `phase1/src/ai/test_multi_agent.py` - Multi-agent test suite
- [ ] `phase1/src/ai/test_agent_router.py` - Router tests
- [ ] `phase1/src/ai/test_shared_context.py` - Context tests

**Documentation:**
- [ ] `phase1/src/ai/multi_agent_design.md` - Architecture design
- [ ] `phase1/weeks/week11/WEEK_11_RESULTS.md` - Test results

**Integration:**
- [ ] shell.py updated to use agent router (optional)
- [ ] main.c updated with multi-agent support (optional, can defer to Week 12)

---

## Testing Plan

### Unit Tests (per agent)
- Device Agent: 5+ test queries
- Network Agent: 5+ test queries
- FileSystem Agent: 5+ test queries
- User Agent: 5+ test queries

### Integration Tests
- Agent Router: 20+ routing tests (>90% accuracy)
- Shared Context: 5+ context sharing tests
- Multi-Agent: 10+ coordination tests

### Performance Tests
- Single-agent response time: <200ms
- Multi-agent response time: <300ms
- Routing overhead: <5ms
- Context access: <1ms

### End-to-End Tests (optional)
- Python → seL4 → Agent → seL4 → Python
- Response time: <500ms

**Total test coverage goal:** 50+ tests, >95% passing

---

## Success Criteria

| Criterion | Target | Status |
|-----------|--------|--------|
| **4 agents implemented** | Device, Network, FileSystem, User | ☐ |
| **Agent router working** | >90% routing accuracy | ☐ |
| **Shared context functional** | All agents access context | ☐ |
| **Multi-agent queries** | Coordination working | ☐ |
| **Response time** | <200ms single, <300ms multi | ☐ |
| **Test coverage** | 50+ tests, >95% passing | ☐ |

---

## Timeline

**Total Estimated Effort:** 16-22 hours

| Task | Estimated | Actual | Status |
|------|-----------|--------|--------|
| Task 1: Design | 2-3 hours | - | ☐ Not Started |
| Task 2: Specialist Agents | 4-6 hours | - | ☐ Not Started |
| Task 3: Agent Router | 3-4 hours | - | ☐ Not Started |
| Task 4: Multi-Agent Testing | 4-5 hours | - | ☐ Not Started |
| Task 5: seL4 Integration (optional) | 3-4 hours | - | ☐ Not Started |

**Note:** Based on Weeks 5-10 efficiency (average 3-4 hours actual vs 12-16 planned), Week 11 may complete in 8-10 hours.

---

## Technical Notes

### Why Multi-Agent?

**Advantages:**
1. **Specialization** - Each agent focuses on specific domain
2. **Parallel processing** - Multiple agents can work concurrently (future)
3. **Scalability** - Add new agents without affecting existing ones
4. **Maintainability** - Each agent is self-contained
5. **Performance** - Smaller models for specialized tasks

**Architecture:**
```
Query → Router → [Device Agent, Network Agent, FileSystem Agent, User Agent]
           ↓
    Shared Context Pool (system state, cache, IPC stats)
           ↓
    Response → Shell/IPC
```

### Keywords-Based Routing

**Simple but effective:**
- Fast: O(n) keyword matching, where n = number of keywords (~50)
- Accurate: >90% for well-defined domains
- Extensible: Easy to add new keywords

**Example routing:**
- "show disk space" → Device Agent (keyword: "disk")
- "ping google.com" → Network Agent (keyword: "ping")
- "list files" → FileSystem Agent (keyword: "files")
- "help me" → User Agent (keyword: "help")

### Shared Context Pool

**Why shared context?**
- Avoids expensive serialization (20ms → 0.1ms, Phase 0 validated)
- All agents see consistent system state
- Lock-free read access (RCU pattern)

**Context includes:**
- System metrics (memory, CPU, disk usage)
- Active operations (what agents are doing)
- Cache statistics (hit rate, lookups)
- IPC statistics (send/receive/drops)

---

## Blockers & Risks

**Current Blockers:** None

**Potential Risks:**
1. **Risk:** Agent routing accuracy <90%
   - **Mitigation:** Fallback to User agent, add more keywords iteratively

2. **Risk:** Response time >300ms
   - **Mitigation:** Use lighter models for specialized agents (TinyLlama 1.1B)

3. **Risk:** Shared context overhead
   - **Mitigation:** Use atomic operations, RCU pattern (validated in Phase 0)

4. **Risk:** Agent coordination deadlocks
   - **Mitigation:** Use timeout mechanism (5s max), no circular dependencies

---

## References

**Original Plan:** `phase1/docs/PHASE_1_IMPLEMENTATION_PLAN.md` (Weeks 10-12)
**Plan Comparison:** `phase1/weeks/WEEK_9_10_PLAN_COMPARISON.md`
**Phase 0 Multi-Agent:** `phase0/experiments/multi_agent_orchestration.py`

---

## Notes

**Week 11 Importance:**
- Multi-agent is the core JARVIS architecture
- Enables distributed decision-making
- Prepares for dynamic model scaling (Week 13-14)
- Foundational for SHIELD safety framework (Week 15-18)

**Lessons from Weeks 1-10:**
- Start with simple design, iterate
- Test each component independently
- Integration last, not first
- Expect 50-75% time efficiency gain

**Confidence Level:** 85% (increased from 80% after Week 9-10 success)

---

**Status:** READY TO START
**Next Steps:** Begin Task 1 (Design Multi-Agent Architecture)
**Estimated Completion:** Week 11 end (assuming 8-10 hours actual effort based on Weeks 5-10 pattern)
