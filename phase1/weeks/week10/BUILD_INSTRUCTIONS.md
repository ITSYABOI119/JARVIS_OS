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

### Step 1: Copy JARVIS Components to seL4 Tutorials Tree

The issue in the error was that we need to build within the seL4 tutorials framework, not standalone.

**In WSL, run these commands:**

```bash
# Navigate to seL4 tutorials directory
cd ~/jarvis-phase1

# Create a new app directory for JARVIS if it doesn't exist
mkdir -p apps/jarvis

# Copy all JARVIS source files to the seL4 app directory
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/* apps/jarvis/

# Verify files were copied
ls -la apps/jarvis/
```

**Expected Output:**
```
drwxr-xr-x  6 itsme itsme 4096 Nov 17 [time] .
drwxr-xr-x  5 itsme itsme 4096 Nov 17 [time] ..
drwxr-xr-x  2 itsme itsme 4096 Nov 17 [time] ai
drwxr-xr-x  2 itsme itsme 4096 Nov 17 [time] cache
drwxr-xr-x  2 itsme itsme 4096 Nov 17 [time] ipc
drwxr-xr-x  2 itsme itsme 4096 Nov 17 [time] sel4
drwxr-xr-x  2 itsme itsme 4096 Nov 17 [time] shell
```

### Step 2: Update CMakeLists.txt Paths

The CMakeLists.txt paths need to be adjusted since we're now in the apps/jarvis/sel4 directory.

**Run:**
```bash
cd ~/jarvis-phase1/apps/jarvis/sel4

# Create a backup of the original
cp CMakeLists.txt CMakeLists.txt.backup

# Update the paths (they need to go up one more level)
sed -i 's|\.\./cache/|../../cache/|g' CMakeLists.txt
sed -i 's|\.\./ipc/|../../ipc/|g' CMakeLists.txt
sed -i 's|${CMAKE_CURRENT_SOURCE_DIR}/\.\./cache|${CMAKE_CURRENT_SOURCE_DIR}/../../cache|g' CMakeLists.txt
sed -i 's|${CMAKE_CURRENT_SOURCE_DIR}/\.\./ipc|${CMAKE_CURRENT_SOURCE_DIR}/../../ipc|g' CMakeLists.txt

# Verify changes
cat CMakeLists.txt | grep -A 10 "add_executable"
```

**Expected to see:**
```cmake
add_executable(jarvis-sel4
    main.c
    ../../cache/decision_cache.c
    ../../cache/cache_patterns.c
    ../../ipc/ring_buffer.c
    ../../ipc/ipc_sel4.c
)

# Add include directories for headers
target_include_directories(jarvis-sel4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../cache
    ${CMAKE_CURRENT_SOURCE_DIR}/../../ipc
)
```

### Step 3: Clean Previous Build
```bash
cd ~/jarvis-phase1
rm -rf build-jarvis
```

### Step 4: Configure and Build with CMake
```bash
cd ~/jarvis-phase1

# Create build directory
mkdir -p build-jarvis
cd build-jarvis

# Configure CMake (pointing to our app)
cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake \
    -DPLATFORM=pc99 \
    -DKernelPlatform=x86_64 \
    -DTUT_BOARD=pc \
    -DTUT_ARCH=x86_64 \
    ../apps/jarvis/sel4

# Build
ninja
```

**Expected CMake Output:**
```
-- The C compiler identification is GNU 13.3.0
-- The ASM compiler identification is GNU
-- Found assembler: /usr/bin/gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Found seL4: /home/itsme/jarvis-phase1/kernel
-- seL4 found at: /home/itsme/jarvis-phase1/kernel
-- Configuring done
-- Generating done
-- Build files have been written to: /home/itsme/jarvis-phase1/build-jarvis
```

**Expected Ninja Build Output:**
```
[1/92] Building C object CMakeFiles/jarvis-sel4.dir/main.c.o
[2/92] Building C object CMakeFiles/jarvis-sel4.dir/../../cache/decision_cache.c.o
[3/92] Building C object CMakeFiles/jarvis-sel4.dir/../../cache/cache_patterns.c.o
[4/92] Building C object CMakeFiles/jarvis-sel4.dir/../../ipc/ring_buffer.c.o
[5/92] Building C object CMakeFiles/jarvis-sel4.dir/../../ipc/ipc_sel4.c.o
...
[89/92] Linking C executable jarvis-sel4
[90/92] Generating binary image
[91/92] Generating simulation script
[92/92] Build complete

Build result: SUCCESS
```

**What to Look For:**
- ✅ CMake finds seL4 successfully (no "FindseL4.cmake" error)
- ✅ All 5 JARVIS source files compile:
  - main.c
  - decision_cache.c
  - cache_patterns.c
  - ring_buffer.c
  - ipc_sel4.c
- ✅ Linking completes with no undefined references
- ✅ jarvis-sel4 executable created
- ✅ Simulation script generated

---

## Step 5: Boot in QEMU and Validate

### Launch QEMU:
```bash
# Should already be in build-jarvis directory from Step 4
# If not: cd ~/jarvis-phase1/build-jarvis

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
