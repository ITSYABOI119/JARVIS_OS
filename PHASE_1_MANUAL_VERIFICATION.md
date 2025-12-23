# JARVIS AI-OS Phase 1 - Manual Verification Guide

**Date:** December 23, 2025
**Purpose:** Verify ALL Phase 1 fixes and features work correctly
**Entry Point:** `python3 run_jarvis.py`

---

## Prerequisites

Run via WSL:
```bash
wsl
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src
```

---

## Part 1: Critical Code Fixes Verification

### Test 1: Suspend Command (SHIELD Bypass Fix)

**Command:**
```
python3 run_jarvis.py
> suspend
```

**Expected Output:**
```
Preparing to suspend...

[1/3] Saving system state...
✓ State saved (0.00s)

[2/3] Entering S3 sleep state...

💤 System suspended (simulated)
   Press ENTER to resume (simulates power button press)...
```

**Press ENTER to continue, then you should see:**
```
[3/3] Waking from S3...
[SystemState] State restored: idle
✓ System resumed (0.00s)

Resume Metrics:
  Components restored: 5
  Resume time: 0.00s (target: <15s)
  State size: 1.5 KB

✅ Resume time target MET (<15s)
```

**What Changed:**
- ✅ SHIELD validation now works (no "SHIELD validation skipped: 'allowed'" error)
- ✅ If SHIELD blocks suspend, you'll see: `❌ SHIELD BLOCKED:` with risk score
- ✅ No crashes or exceptions

---

### Test 2: Agents Command (Component Access Fix)

**Command:**
```
> agents
```

**Expected Output:**
```
============================================================
Multi-Agent Architecture (Week 11)
============================================================

📋 Specialist Agents:
  • Device Manager       - Hardware/driver operations
  • Network Agent        - Network operations
  • FileSystem Agent     - File/storage operations
  • User Interaction     - User-facing operations

📊 Routing Statistics:
  Total queries: 0
  Routing accuracy: 0.0%
  Avg routing time: 0.00ms
```

**What Changed:**
- ✅ Now uses `self._injected_agent_router` (direct injection)
- ✅ Shows real statistics (not "not initialized")
- ✅ No hasattr() checks hiding broken wiring

---

### Test 3: Health Command (Component Access Fix)

**Command:**
```
> health
```

**Expected Output:**
```
============================================================
Agent Health Monitoring (Week 12)
============================================================

💚 Monitoring Status:
  Agents monitored: 4
  Healthy agents: 0
  Degraded agents: 0
  Failed agents: 0
  Uptime: 0.0s

📊 Statistics:
  Total heartbeats: 0
  Total failures: 0
  Total recoveries: 0
  State changes: 0
```

**What Changed:**
- ✅ Now uses `self._injected_health_monitor` (direct injection)
- ✅ Shows real statistics
- ✅ Consistent pattern with shield/snapshots commands

---

### Test 4: Scaling Command (Component Access Fix)

**Command:**
```
> scaling
```

**Expected Output:**
```
============================================================
Dynamic Model Scaling (Week 13-15)
============================================================

😴 Current State: IDLE
  Configuration: TinyLlama 1.1B, 2GB RAM, 1 core, 50ms

💻 System Metrics:
  CPU usage: 0.0%
  Memory usage: 5.1%
  Disk usage: 2.2%

📊 Transition Statistics:
  Total transitions: 0
  Successful: 0
  Failed: 0
  Avg transition time: 0.00s

⏱️  Time in States:
  IDLE: 0.0s
  ACTIVE: 0.0s
  CRITICAL: 0.0s
  EMERGENCY: 0.0s
```

**What Changed:**
- ✅ Now uses `self._injected_state_manager` (direct injection)
- ✅ Shows current state (IDLE/ACTIVE)
- ✅ Real system metrics

---

## Part 2: All 27 Commands Verification

### Basic Commands (9 tests)

**Test 5: help**
```
> help
```
**Expected:** Shows 27+ commands listed in categories
**Status:** [ ] PASS

---

**Test 6: status**
```
> status
```
**Expected Output:**
```
======================================================================
JARVIS System Status
======================================================================

  AI Agent:         NOT LOADED
  Query Processor:  ACTIVE

  Session Commands: [number]
  Built-in Cmds:    [number]
  AI Queries:       0
```
**Status:** [ ] PASS

---

**Test 7: cache**
```
> cache
```
**Expected Output:**
```
======================================================================
[QUERY PROCESSOR STATISTICS]
======================================================================
  Total queries:     0
  Cache hits:        0
  Cache misses:      0
  Cache hit rate:    0.0%
  Parse successes:   0
  Parse failures:    0
```
**Note:** 0% is expected without seL4 integration
**Status:** [ ] PASS

---

**Test 8: agent**
```
> agent
```
**Expected:** Shows "[WARNING] AI model not loaded yet" (unless you've made AI queries)
**Status:** [ ] PASS

---

**Test 9-12: clear, cls, exit test**
```
> clear    # Should clear screen
> cls      # Should clear screen (alias)
```
**Status:** [ ] PASS

---

### File Operations (4 tests)

**Test 13: ls**
```
> ls
```
**Expected Output:**
```
======================================================================
Directory: /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src
======================================================================
  [FILE] README.md                                (2,782 bytes)
  [DIR]  ai
  [DIR]  cache
  [DIR]  drivers
  [DIR]  integration_tests
  [DIR]  ipc
  [FILE] run_jarvis.py                            (14,046 bytes)
  ...
Total: [number] entries
```
**Status:** [ ] PASS

---

**Test 14: cat**
```
> cat README.md
```
**Expected:** Shows file contents
**Status:** [ ] PASS

---

**Test 15: mkdir**
```
> mkdir /tmp/test_jarvis_mkdir
```
**Expected:** Creates directory or shows SHIELD validation result
**Status:** [ ] PASS

---

**Test 16: rm (dangerous)**
```
> rm -rf /
```
**Expected:** SHIELD BLOCKED or trust level error
**Status:** [ ] PASS

---

### Process Management (3 tests)

**Test 17: ps**
```
> ps
```
**Expected Output:**
```
================================================================================
Running Processes (Top 20 by CPU)
================================================================================
PID      Name                           CPU%     Memory%
--------------------------------------------------------------------------------
1        systemd                        0.0      0.1
...
```
**Status:** [ ] PASS

---

**Test 18: top**
```
> top
```
**Expected:** Similar to ps, shows top processes by CPU
**Status:** [ ] PASS

---

**Test 19: kill (system PID)**
```
> kill 1
```
**Expected:** Error or SHIELD block (can't kill PID 1)
**Status:** [ ] PASS

---

### System Control (4 tests)

**Test 20: shutdown** ⚠️ **CRITICAL TEST**
```
> shutdown
```
**Expected Output:**
```
[SHIELD] BLOCKED: shutdown
    Risk: 1.00
    Reason: Critical system operation
```
**Status:** [ ] PASS

---

**Test 21: reboot** ⚠️ **CRITICAL TEST**
```
> reboot
```
**Expected Output:**
```
[SHIELD] BLOCKED: reboot
    Risk: 1.00
    Reason: Critical system operation
```
**Status:** [ ] PASS

---

**Test 22: uptime**
```
> uptime
```
**Expected Output:**
```
======================================================================
System Uptime
======================================================================
  Boot Time: 2025-12-16 12:58:39
  Uptime: 0 days, 0 hours, [minutes] minutes
```
**Status:** [ ] PASS

---

**Test 23: battery**
```
> battery
```
**Expected Output:**
```
======================================================================
Battery Status
======================================================================
  No battery detected (desktop system)
```
**Status:** [ ] PASS

---

### Network Diagnostics (2 tests)

**Test 24: ping**
```
> ping 8.8.8.8
```
**Expected Output:**
```
Pinging 8.8.8.8...
PING 8.8.8.8 (8.8.8.8) 56(84) bytes of data.
64 bytes from 8.8.8.8: icmp_seq=1 ttl=114 time=34.1 ms
...
--- 8.8.8.8 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss
```
**Status:** [ ] PASS

---

**Test 25: ifconfig**
```
> ifconfig
```
**Expected Output:**
```
======================================================================
Network Interfaces
======================================================================

lo:
  Status: Up
  Speed: 0 Mbps
  IPv4: 127.0.0.1
  ...

eth0:
  Status: Up
  Speed: 10000 Mbps
  IPv4: 172.29.236.25
  ...
```
**Status:** [ ] PASS

---

### Advanced Features (5 tests) - **THESE WERE FIXED**

**Test 26: shield** ✅ **VERIFIED WORKING**
```
> shield
```
**Expected Output:**
```
============================================================
SHIELD Safety Framework (Week 16)
============================================================

[SHIELD] Components:
  • Staged Execution Sandbox
  • Hardware Impact Analysis
  • Irreversibility Detection
  • Escalation Intelligence
  • Learning from Failures
  • Deterministic Rollback

📊 Validation Statistics:
  Total validations: [number]
  Blocked rate: [percent]%
  Shadow tested rate: [percent]%
  Approval required rate: [percent]%
  Automatic execution rate: [percent]%

📋 Action Type Coverage:
  100 action types across 10 categories
  Categories: File, Process, Network, System, Service,
              User, Security, Power, Package, Hardware
```
**Status:** [ ] PASS

---

**Test 27: snapshots** ✅ **VERIFIED WORKING**
```
> snapshots
```
**Expected Output:**
```
============================================================
Snapshots & Rollback (Week 17)
============================================================

💾 Storage Configuration:
  Memory snapshots: 0 / 5 max
  Disk snapshots: 0 / 20 max

📊 Statistics:
  Total snapshots created: 0
  Memory snapshots: 0
  Disk snapshots: 0
  Rollbacks executed: 0
  Rollback success rate: 0.0%
  Avg snapshot time: 0.0ms
  Avg rollback time: 0.0ms
```
**Status:** [ ] PASS

---

**Test 28: resume**
```
> resume
```
**Expected Output:**
```
======================================================================
Suspend/Resume Metrics
======================================================================

Status:
  State: RUNNING
  Registered components: 5
  Components: state_manager, device, network, filesystem, user

Performance:
  Last suspend time: [time]s (target: <5s)
  Last resume time: [time]s (target: <15s)

Statistics:
  Total suspends: [number]
  Successful: [number]
  Failed: 0

  Total resumes: [number]
  Successful: [number]
  Failed: 0

  Suspend success rate: 100.0%
  Resume success rate: 100.0%

Saved State Size: 1.5 KB (target: <10 MB)
  Location: /tmp/jarvis_state
```
**Status:** [ ] PASS

---

## Part 3: Multi-Agent Routing Verification

Test natural language queries route to correct specialist agents:

**Test 29: FileSystem Agent**
```
> list files in home
```
**Expected Output:**
```
[PROCESSING] (100-150ms)

[FILESYSTEM AGENT] Action: list_directory
  Routing: 0.019ms | Response: 11.99ms

Result: 7 directories, 5 files in .
```
**Status:** [ ] PASS

---

**Test 30: Device Agent**
```
> what is the cpu usage
```
**Expected Output:**
```
[PROCESSING] (100-150ms)

[DEVICE AGENT] Action: show_cpu
  Routing: 0.015ms | Response: 9.20ms

Result: AMD Ryzen 7 2700X Eight-Core Processor (16 cores)
```
**Status:** [ ] PASS

---

**Test 31: Network Agent**
```
> show network status
```
**Expected Output:**
```
[PROCESSING] (100-150ms)

[NETWORK AGENT] Action: show_network
  Routing: [time]ms | Response: [time]ms

Result: [network status]
```
**Status:** [ ] PASS

---

## Part 4: SHIELD Safety Verification

**Test 32: Dangerous Command Blocking**
```
> delete all files
```
**Expected Output:**
```
[PROCESSING] (101ms)

[FILESYSTEM AGENT] Action: remove_file
  Routing: 0.027ms | Response: 0.01ms

[ERROR] Remove operations disabled for safety (trust level 3 required)
```
**Status:** [ ] PASS

---

## Part 5: Exit Test

**Test 33: Exit Command**
```
> exit
```
**Expected Output:**
```
======================================================================
JARVIS Shell Session Summary
======================================================================
  Commands executed:  [number]
  Built-in commands:  [number]
  AI queries:         [number]
  Cache hits:         0 (0.0%)
  Cache misses:       0
  Errors:             [number]
======================================================================

Thank you for using JARVIS AI-OS. Goodbye!
```
**Status:** [ ] PASS

---

## Success Criteria

After completing all tests above, verify:

- [ ] **All 27/27 commands working** (not 24/27)
- [ ] **No crashes** during entire session
- [ ] **SHIELD blocking dangerous operations** (shutdown, reboot blocked)
- [ ] **Real statistics displayed** for all component commands
- [ ] **agents/health/scaling commands show data** (not "not initialized")
- [ ] **suspend command works without SHIELD bypass**
- [ ] **Multi-agent routing to correct specialists**
- [ ] **Zero "KeyError" or "NoneType" exceptions**

---

## Quick Test Script (Optional)

Save this to `test_all_commands.txt`:
```
help
status
cache
agent
agents
health
scaling
shield
snapshots
ls
ps
top
uptime
battery
ifconfig
ping 8.8.8.8
suspend
[ENTER to resume]
resume
list files
what is the cpu usage
shutdown
reboot
exit
```

Then run:
```bash
python3 run_jarvis.py < test_all_commands.txt
```

---

## Troubleshooting

**If you see "⚠️ [Feature] not available":**
- Make sure you're running `python3 run_jarvis.py` (NOT `python3 shell.py`)
- Check that all dependencies installed: `./setup_wsl_dependencies.sh`

**If commands crash:**
- Check for Python exceptions in output
- Verify you're in WSL (not Windows Python)
- Report the exact command and error

---

## Verification Results

**Verification Date:** December 23, 2025
**Verified By:** Jarnos (Solo Developer)
**Status:** ✅ PASS

### Summary

All Phase 1 verification tests completed successfully:

**Part 1: Critical Code Fixes** - ✅ 4/4 PASS
- [x] Test 1: Suspend command - PASS (clean execution, no warnings)
- [x] Test 2: Agents command - PASS (shows 4 specialists)
- [x] Test 3: Health command - PASS (shows monitoring status)
- [x] Test 4: Scaling command - PASS (shows IDLE state)

**Part 2: All 27 Commands** - ✅ 27/27 PASS
- All commands executed successfully
- Zero crashes during testing
- No exceptions or errors

**Part 3: Multi-Agent Routing** - ✅ 4/4 PASS
- FileSystem agent routes correctly
- Device agent routes correctly
- Network agent routes correctly
- User agent routes correctly

**Part 4: SHIELD Safety** - ✅ 6/6 PASS
- Shutdown/reboot properly blocked
- mkdir /tmp properly allowed
- rm -rf / properly blocked
- kill 1 properly blocked
- All dangerous operations validated

**Part 5: Exit Test** - ✅ PASS
- Clean exit, session summary displayed

### Post-Week 26 Bug Fixes Verified

Additional bugs fixed and verified on December 23, 2025:

1. ✅ **Suspend SHIELD validation** - Fixed action type, clean execution
2. ✅ **mkdir action type** - Fixed type mismatch, /tmp allowed
3. ✅ **Suspend risk threshold** - Lowered base_risk, execution allowed
4. ✅ **Cache 0% explanation** - Documented as expected behavior

### Notes

- All 27/27 commands verified working after final bug fixes
- SHIELD safety validations working correctly
- Multi-agent routing accurate and fast
- System stable throughout testing
- Ready for Phase 2 transition

**Overall Result:** ✅ **PASS - Phase 1 Verification Complete**
