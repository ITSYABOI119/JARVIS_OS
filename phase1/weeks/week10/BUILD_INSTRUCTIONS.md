# Week 10 Build Instructions - Quick Reference

## Week 10 Overview
**Week 10 combines two major workstreams:**
- **Part A:** QEMU Integration (deferred Week 9 tasks 2-5) - 12 hours
- **Part B:** Multi-Agent Implementation (original Week 10 plan) - 16-20 hours

**Total:** 28-32 hours planned (14-20 hours estimated actual based on Week 5-9 efficiency)

---

## Current Status
✅ **Task 2.1 COMPLETE:** CMakeLists.txt updated with cache and IPC components
⏳ **Task 2.2 IN PROGRESS:** Build seL4 with integrated components

**This Guide Covers:** Part A Tasks 2-5 (QEMU Integration)

**After Part A:** Proceed to multi-agent implementation (Tasks 6-9) - see WEEK_10_STATUS.md

---

## Step-by-Step Build Instructions

### Step 1: Navigate to Project Root (in WSL)
```bash
cd /mnt/c/Users/jluca/Documents/JARVIS_OS
```

### Step 2: Clean Previous Build
```bash
./phase1/scripts/build-jarvis.sh clean
```

**Expected Output:**
```
Cleaning build directory...
Done.
```

### Step 3: Build with Updated CMakeLists.txt
```bash
./phase1/scripts/build-jarvis.sh
```

**Expected Output:**
```
JARVIS AI-OS Phase 1 - Build Script
=====================================

Build directory: /home/jluca/jarvis-phase1/build-jarvis
Source directory: /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/sel4

Configuring CMake...
[CMake configuration output...]

Building JARVIS...
[Ninja build output...]
[1/183] Building C object...
[2/183] Building C object...
...
[183/183] Linking...

Build complete!

Executable: /home/jluca/jarvis-phase1/build-jarvis/jarvis-sel4

To run in QEMU:
  cd /home/jluca/jarvis-phase1/build-jarvis
  ./simulate
```

**What to Look For:**
- ✅ CMake configures successfully (no errors)
- ✅ All source files compile:
  - main.c
  - decision_cache.c
  - cache_patterns.c
  - ring_buffer.c
  - ipc_sel4.c
- ✅ Linking completes with no undefined references
- ✅ Total: ~183 build targets (matches previous builds)

---

## Step 4: Boot in QEMU and Validate

### Launch QEMU:
```bash
cd ~/jarvis-phase1/build-jarvis
./simulate
```

### Expected Boot Sequence:
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
Testing lock-free IPC in seL4...

Sending 10 PING messages...
  [0] SENT: PING #0
  [1] SENT: PING #1
  ...
  [9] SENT: PING #9

Receiving messages...
  [0] RECEIVED: PING #0 (type=3, id=1, size=8)
  [1] RECEIVED: PING #1 (type=3, id=2, size=8)
  ...
  [9] RECEIVED: PING #9 (type=3, id=10, size=8)

Total received: 10/10
✓ IPC PING/PONG TEST PASSED

[Demo shell runs automatically...]

========================================
IPC Message Handler - Listening for Python queries
========================================

Polling for messages from Python...
(Press Ctrl+C to stop, or wait for timeout)
```

### To Exit QEMU:
Press **Ctrl+A**, then **X**

---

## Success Criteria Checklist

After running the build and boot, verify:

**Build Phase:**
- [ ] Build completes with 0 errors
- [ ] All 5 .c files compiled successfully
- [ ] jarvis-sel4 executable created
- [ ] No linker errors or missing symbols

**Boot Phase:**
- [ ] JARVIS banner displays
- [ ] Decision cache initializes (103 patterns loaded)
- [ ] IPC initializes successfully
- [ ] IPC ping/pong test passes (10/10 messages)
- [ ] IPC message handler starts listening
- [ ] No kernel panics or crashes
- [ ] Boot time <10 seconds

---

## Troubleshooting Common Issues

### Build Error: "decision_cache.h: No such file or directory"
**Cause:** Include directories not set correctly in CMakeLists.txt

**Fix:**
- Verify `target_include_directories()` is in CMakeLists.txt
- Check paths: `../cache` and `../ipc` are correct relative to `sel4/`
- Re-run build

### Linker Error: "undefined reference to 'cache_init'"
**Cause:** decision_cache.c not in add_executable() list

**Fix:**
- Verify all .c files are listed in `add_executable(jarvis-sel4 ...)`
- Re-run build

### Build Error: "stdatomic.h not found"
**Cause:** ipc_sel4.c uses custom sel4_atomics.h for seL4 compatibility

**Expected:** Should NOT happen - ipc_sel4.c already uses sel4_atomics.h

**Fix (if it does happen):**
- Check that `#include "sel4_atomics.h"` is in ipc_sel4.c (not stdatomic.h)
- Verify sel4_atomics.h exists in phase1/src/ipc/

### QEMU Doesn't Boot / Kernel Panic
**Possible Causes:**
1. Initialization order issue (cache or IPC init fails)
2. Memory allocation failure
3. seL4 configuration issue

**Debugging:**
- Check seL4 console output for specific error messages
- Look for "ERROR:" or "PANIC:" in output
- Verify QEMU command: `-m 8G -smp 4 -serial stdio -nographic`

---

## What to Report Back

When you run the build, please copy and paste:

1. **Build Output** (last 50 lines showing compile/link results)
2. **Boot Output** (full QEMU console output up to "Listening for Python queries")
3. **Any errors or warnings**

**Example:**
```
[Paste build output here]

Build result: SUCCESS / FAILED
Boot result: SUCCESS / FAILED
Issues encountered: [describe any problems]
```

---

## Next Steps After Successful Build

Once build and boot are successful:

### Part A Continuation (QEMU Integration):
1. ✅ Mark Task 2 as COMPLETE
2. ⏭️ Task 3: End-to-End IPC Testing (Python ↔ seL4)
   - Create Python test script
   - Test Python → seL4 communication
   - Validate message delivery
3. ⏭️ Task 4: Performance Benchmarking
   - IPC latency measurements
   - Boot time validation
4. ⏭️ Task 5: Shell Integration (optional)

### After Part A Complete:
Move to **Part B: Multi-Agent Implementation** (Tasks 6-9)
- Task 6: Create 4 specialist agents
- Task 7: Implement agent router
- Task 8: Implement shared context pool
- Task 9: Test multi-agent coordination

See `WEEK_10_STATUS.md` for detailed Part B instructions.

---

**Document Status:** ACTIVE
**Created:** November 17, 2025
**Updated:** November 17, 2025 (added Part B overview)
**For:** Week 10 Part A - Tasks 2-5 (QEMU Integration)
