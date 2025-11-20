# Week 11 Multi-Agent System - Complete Testing Guide

**Date:** November 20, 2025
**Version:** 1.0

---

## Quick Start - Test Everything in 5 Minutes

### 1. Test Multi-Agent System (Fastest)

```bash
# From Windows (your current location)
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\src\ai

# Run comprehensive test suite (7 test suites, 100% pass rate)
python test_multi_agent.py
```

**Expected Output:**
```
TEST 1: Routing Accuracy
  18/18 queries routed correctly
  Accuracy: 100% (target: >90%) [PASS]

TEST 2: Routing Performance
  Avg Routing Time: 0.012ms (target: <5ms) [PASS]

TEST 3: Agent Response Time
  Avg Response Time: 4.65ms (target: <200ms) [PASS]

...

Total: 7/7 tests passed (100%)
[SUCCESS] All tests passed!
```

---

### 2. Test Individual Agents

```bash
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\src\ai

# Test Device Manager Agent
python device_agent.py

# Test Network Agent
python network_agent.py

# Test FileSystem Agent
python filesystem_agent.py

# Test User Interaction Agent
python user_agent.py

# Test Agent Router
python agent_router.py
```

**Expected:** Each agent prints test queries and results

---

### 3. Test Shell + Multi-Agent Integration

```bash
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\weeks\week11

# Quick integration test
python test_shell_multiagent.py
```

**Expected Output:**
```
Testing Shell + Multi-Agent Integration

1. Creating agent router...
   Agents: ['device', 'network', 'filesystem', 'user']

2. Testing queries:
   Query: show disk space
   Agent: device
   Action: show_disk_space
   Routing: 0.005ms | Total: 21.11ms

...

Shell + Multi-Agent Integration Test Complete!
```

---

### 4. Interactive Shell (Manual Testing)

```bash
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\src\shell

# Launch interactive shell with multi-agent support
python shell.py
```

**Test Commands:**
```
jarvis> show disk space        # → Device Agent
jarvis> ping google.com        # → Network Agent
jarvis> list files             # → FileSystem Agent
jarvis> help                   # → User Agent
jarvis> status                 # → User Agent (shows system state)
jarvis> cache stats            # → User Agent (shows cache stats)
jarvis> exit                   # Exit shell
```

**Expected Experience:**
```
jarvis> show disk space
[PROCESSING] (21ms)

[DEVICE AGENT] Action: show_disk_space
  Routing: 0.005ms | Response: 21.06ms

Result: 3 filesystems mounted
```

---

## Detailed Testing (30-60 minutes)

### Test 1: Component Validation (15 mins)

**Run all component tests to verify each agent works:**

```bash
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\src\ai

# Device Manager Agent (hardware queries)
python device_agent.py

# Expected queries:
# - show disk space → Shows filesystems
# - show memory usage → May fail on Windows (needs WSL)
# - show cpu information → May fail on Windows
# - list usb devices → May fail (needs lsusb)
# - show temperature → May fail (needs sensors)

# Network Agent (network operations)
python network_agent.py

# Expected queries:
# - ping google.com → Shows ping results (2-3 seconds)
# - show network status → Shows network interfaces
# - show listening ports → Shows open ports
# - check internet connection → Tests connectivity
# - check dns for github.com → Resolves DNS

# FileSystem Agent (file operations)
python filesystem_agent.py

# Expected queries:
# - list files in . → Shows current directory
# - list files in /tmp → May fail (Windows path issue)
# - find *.py in . → Finds Python files
# - show permissions for . → Shows folder permissions
# - read file agent_base.py → Reads file content

# User Interaction Agent (help/status)
python user_agent.py

# Expected queries:
# - help → Shows help text
# - show status → Shows JARVIS status
# - cache stats → Shows cache statistics
# - agent status → Shows agent health
# - version → Shows JARVIS version
# - what is JARVIS? → General query fallback
```

---

### Test 2: Agent Router (10 mins)

**Test keyword-based routing:**

```bash
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\src\ai

python agent_router.py
```

**Expected:**
- 8 test queries processed
- 100% routing accuracy (8/8 PASS)
- Avg routing time <0.02ms
- Each query routed to correct agent
- Statistics displayed

---

### Test 3: Comprehensive Test Suite (10 mins)

**Run all 7 test suites:**

```bash
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\src\ai

python test_multi_agent.py
```

**Test Coverage:**
1. **Routing Accuracy** - 18 queries, >90% target (100% actual)
2. **Routing Performance** - <5ms overhead target (0.012ms actual)
3. **Agent Response Time** - <200ms target (4.65ms actual)
4. **Shared Context** - System state updates working
5. **Agent Health** - All 4 agents healthy
6. **Multi-Agent Coordination** - Context sharing working
7. **Routing Statistics** - Statistics tracking correct

**Expected:** 7/7 tests PASSED (100%)

---

### Test 4: Shell Integration (15 mins)

**Test interactive shell with multi-agent routing:**

```bash
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\src\shell

python shell.py
```

**Test Sequence:**

```
# 1. Help and Status (User Agent)
jarvis> help
jarvis> status
jarvis> version
jarvis> cache stats
jarvis> agent status

# 2. Device Queries (Device Manager Agent)
jarvis> show disk space
jarvis> show memory usage        # May fail on Windows
jarvis> show cpu info            # May fail on Windows

# 3. Network Queries (Network Agent)
jarvis> ping google.com
jarvis> ping 8.8.8.8
jarvis> check internet connection
jarvis> show network status

# 4. FileSystem Queries (FileSystem Agent)
jarvis> list files in .
jarvis> list files
jarvis> find *.py
jarvis> show permissions for .

# 5. General Queries (User Agent fallback)
jarvis> what is JARVIS?
jarvis> unknown command

# 6. Exit
jarvis> exit
```

**Verify:**
- Each query routes to correct agent
- Agent name displayed (e.g., [DEVICE AGENT])
- Action shown (e.g., Action: show_disk_space)
- Timing displayed (Routing + Response)
- Results printed clearly

---

## Testing in WSL (Recommended for Full Functionality)

**Some commands work better in WSL/Linux:**

```bash
# Open WSL (if installed)
wsl

# Navigate to project
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/ai

# Run tests (all commands will work)
python3 test_multi_agent.py
python3 device_agent.py
python3 network_agent.py
python3 filesystem_agent.py
```

**Why WSL?**
- `free` command (memory usage) works
- `lscpu` command (CPU info) works
- `lsusb` command (USB devices) works
- `/tmp` directory exists
- Full Linux compatibility

---

## Performance Benchmarks

**Expected performance (validated):**

| Component | Target | Actual | Status |
|-----------|--------|--------|--------|
| **Routing Time** | <5ms | 0.012ms | ✅ 417× better |
| **Agent Response** | <200ms | 4.65ms | ✅ 43× better |
| **Routing Accuracy** | >90% | 100% | ✅ Exceeds |
| **Test Pass Rate** | >95% | 100% | ✅ Perfect |

---

## Troubleshooting

### Problem: "Cannot import AgentRouter"

**Solution:**
```bash
# Make sure you're in the right directory
cd C:\Users\jluca\Documents\JARVIS_OS\phase1\src\ai

# Check files exist
dir *.py

# Should see:
# - agent_base.py
# - device_agent.py
# - network_agent.py
# - filesystem_agent.py
# - user_agent.py
# - agent_router.py
# - shared_context.py
# - test_multi_agent.py
```

---

### Problem: "Module psutil not found"

**Solution:**
```bash
# Install psutil for system monitoring
pip install psutil
```

---

### Problem: Commands fail on Windows

**Expected:** Some Linux commands don't work on Windows:
- `free` (memory) → Use WSL or accept error
- `lscpu` (CPU) → Use WSL or accept error
- `lsusb` (USB devices) → Use WSL or accept error
- `sensors` (thermal) → Use WSL or accept error

**Not a bug:** Agents handle errors gracefully and return error messages.

---

### Problem: "UnicodeEncodeError"

**Solution:** Already fixed in code with ASCII replacements (✓ → [PASS])

---

## Success Criteria

**You know it's working when:**

✅ `test_multi_agent.py` shows 7/7 tests PASSED
✅ Each agent test runs without crashing
✅ Shell shows `[Multi-Agent] Router initialized`
✅ Shell queries route to correct agent
✅ Agent name + action displayed
✅ Response times <50ms for local commands
✅ No Python exceptions or crashes

---

## Quick Validation Checklist

```
[ ] test_multi_agent.py → 7/7 PASSED
[ ] device_agent.py → Runs without crash
[ ] network_agent.py → Ping works
[ ] filesystem_agent.py → List files works
[ ] user_agent.py → Help displays
[ ] agent_router.py → 8/8 routing correct
[ ] shell.py → Starts with router initialized
[ ] Shell query "help" → USER agent
[ ] Shell query "show disk space" → DEVICE agent
[ ] Shell query "ping google.com" → NETWORK agent
[ ] Shell query "list files" → FILESYSTEM agent
```

**If all checked:** ✅ Multi-agent system 100% working!

---

## Next Steps After Testing

1. **Test in WSL** (if not already) for full Linux compatibility
2. **Run QEMU tests** (Week 10) to see seL4 integration
3. **Explore shell interactively** with different queries
4. **Review code** to understand agent architecture
5. **Week 12**: Conflict resolution, resource allocation, failover

---

## Files Summary

**Test Files:**
- `phase1/src/ai/test_multi_agent.py` - Comprehensive 7-test suite
- `phase1/weeks/week11/test_shell_multiagent.py` - Shell integration test
- `phase1/src/ai/*_agent.py` - Individual agent tests (run directly)

**Implementation:**
- `phase1/src/ai/agent_base.py` - Base agent class
- `phase1/src/ai/device_agent.py` - Device Manager
- `phase1/src/ai/network_agent.py` - Network Agent
- `phase1/src/ai/filesystem_agent.py` - FileSystem Agent
- `phase1/src/ai/user_agent.py` - User Interaction
- `phase1/src/ai/agent_router.py` - Agent Router
- `phase1/src/ai/shared_context.py` - Shared Context Pool
- `phase1/src/shell/shell.py` - Interactive Shell (multi-agent integrated)

**Documentation:**
- `phase1/weeks/week11/WEEK_11_STATUS.md` - Task breakdown
- `phase1/weeks/week11/WEEK_11_RESULTS.md` - Complete results
- `phase1/weeks/week11/SEL4_MULTI_AGENT_INTEGRATION.md` - Integration plan
- `phase1/weeks/week11/TESTING_GUIDE.md` - This file

---

**Total Testing Time:** 5-60 minutes (depending on depth)
**Quick Test:** 5 minutes (`test_multi_agent.py`)
**Full Test:** 30-60 minutes (all components + shell)

**Have fun testing! 🚀**
