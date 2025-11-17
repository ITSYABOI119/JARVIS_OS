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

## Prerequisites

### Verify seL4 Installation

Before building, ensure seL4 is installed at `~/jarvis-phase1`.

**Check seL4 installation:**

```bash
# Check if seL4 directory exists
cd ~/jarvis-phase1 && ls -la

# Should see: kernel/ projects/ tools/ libs/
```

**If seL4 is NOT installed**, follow `PHASE_1_DEVELOPMENT_SETUP.md` Steps 1-6:

```bash
# Quick install (run in WSL):
mkdir -p ~/jarvis-phase1
cd ~/jarvis-phase1
repo init -u https://github.com/seL4/sel4-tutorials-manifest
repo sync  # Takes ~10 minutes
```

---

## Step-by-Step Build Instructions

### Step 1: Copy JARVIS Sources to seL4 Tree

Copy JARVIS components from the repository to the seL4 build tree.

```bash
cd ~/jarvis-phase1

# Create apps/jarvis directory
mkdir -p apps/jarvis

# Copy JARVIS sources (adjust path to your repository location)
# Example Windows path via WSL: /mnt/c/Users/jluca/Documents/JARVIS_OS
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/cache apps/jarvis/
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/ipc apps/jarvis/
cp -r /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/sel4 apps/jarvis/

# Verify files copied successfully
ls -la apps/jarvis/
```

**Expected output:**
```
drwxr-xr-x  4 user user 4096 Nov 17 [time] .
drwxr-xr-x  3 user user 4096 Nov 17 [time] ..
drwxr-xr-x  2 user user 4096 Nov 17 [time] cache
drwxr-xr-x  2 user user 4096 Nov 17 [time] ipc
drwxr-xr-x  2 user user 4096 Nov 17 [time] sel4
```

**Verify critical files exist:**
```bash
ls -la apps/jarvis/sel4/
# Should see: main.c CMakeLists.txt

ls -la apps/jarvis/cache/
# Should see: decision_cache.c decision_cache.h cache_patterns.c cache_patterns.h

ls -la apps/jarvis/ipc/
# Should see: ring_buffer.c ring_buffer.h ipc_sel4.c ipc_sel4.h sel4_atomics.h
```

---

### Step 2: Configure CMake Build

Create and configure the build directory.

```bash
cd ~/jarvis-phase1

# Create build directory
mkdir -p build-jarvis
cd build-jarvis

# Configure with CMake
cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake \
    -DPLATFORM=pc99 \
    -DKernelPlatform=x86_64 \
    -DTUT_BOARD=pc \
    -DTUT_ARCH=x86_64 \
    ../apps/jarvis/sel4
```

**What this does:**
- `-DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake` - Use seL4's GCC toolchain
- `-DPLATFORM=pc99` - Target standard PC platform
- `-DKernelPlatform=x86_64` - x86_64 architecture
- `-DTUT_BOARD=pc` and `-DTUT_ARCH=x86_64` - Tutorial framework compatibility
- `../apps/jarvis/sel4` - Source directory (where CMakeLists.txt is)

**Expected CMake output:**
```
-- The C compiler identification is GNU 13.3.0
-- The ASM compiler identification is GNU
-- Found assembler: /usr/bin/gcc
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/gcc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Found seL4: /home/user/jarvis-phase1/kernel
-- seL4 found at: /home/user/jarvis-phase1/kernel
-- Configuring done
-- Generating done
-- Build files have been written to: /home/user/jarvis-phase1/build-jarvis
```

**If CMake fails**, check:
- ✅ seL4 kernel exists at `../kernel/`
- ✅ `gcc.cmake` exists at `../kernel/gcc.cmake`
- ✅ CMakeLists.txt exists at `../apps/jarvis/sel4/CMakeLists.txt`

---

### Step 3: Build JARVIS

Build the JARVIS seL4 executable.

```bash
# Should still be in build-jarvis directory
ninja
```

**Expected build output:**
```
[1/92] Building C object CMakeFiles/jarvis-sel4.dir/main.c.o
[2/92] Building C object CMakeFiles/jarvis-sel4.dir/../cache/decision_cache.c.o
[3/92] Building C object CMakeFiles/jarvis-sel4.dir/../cache/cache_patterns.c.o
[4/92] Building C object CMakeFiles/jarvis-sel4.dir/../ipc/ring_buffer.c.o
[5/92] Building C object CMakeFiles/jarvis-sel4.dir/../ipc/ipc_sel4.c.o
...
[87/92] Linking CXX shared library libsel4muslcsys.so
[88/92] Linking C executable jarvis-sel4
[89/92] Generating binary image
[90/92] Generating simulation script
[91/92] Installing: simulate
[92/92] Build complete: SUCCESS
```

**Build Success Criteria:**
- ✅ All 5 JARVIS .c files compile (main.c, decision_cache.c, cache_patterns.c, ring_buffer.c, ipc_sel4.c)
- ✅ No compilation errors
- ✅ Linking completes successfully
- ✅ `jarvis-sel4` executable created
- ✅ `simulate` script generated

**Common Build Errors:**

| Error | Cause | Fix |
|-------|-------|-----|
| `decision_cache.h: No such file` | Include paths wrong | Verify CMakeLists.txt has `target_include_directories` with `../cache` and `../ipc` |
| `undefined reference to 'cache_init'` | Source files not in build | Verify CMakeLists.txt `add_executable` includes all .c files |
| `stdatomic.h not found` | Using standard atomics (not available in seL4) | Verify ipc_sel4.c uses `sel4_atomics.h` |
| `FindseL4.cmake not found` | CMake can't find seL4 | Check `-DCMAKE_TOOLCHAIN_FILE` path is correct |

---

### Step 4: Run in QEMU

Boot JARVIS in QEMU to verify integration.

```bash
# Should still be in build-jarvis directory
./simulate
```

**Alternative manual launch:**
```bash
qemu-system-x86_64 \
    -m 8G \
    -smp 4 \
    -serial stdio \
    -nographic \
    -kernel images/jarvis-sel4-image-x86_64-pc99
```

**Expected boot output:**
```
========================================
  JARVIS AI-OS v0.1 - Phase 1
  Week 8: IPC Integration Complete
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

========================================
IPC Message Handler - Listening for Python queries
========================================

Polling for messages from Python...
(Press Ctrl+C to stop, or wait for timeout)
```

**To exit QEMU:**
Press **Ctrl+A**, then **X**

---

## Success Criteria Checklist

After completing Steps 1-4, verify:

### Build Phase
- [ ] CMake configures without errors
- [ ] All 5 JARVIS .c files compile successfully
- [ ] Linking completes (no undefined references)
- [ ] `jarvis-sel4` executable created
- [ ] `simulate` script generated
- [ ] Build completes in <5 minutes

### Boot Phase
- [ ] JARVIS banner displays correctly
- [ ] System information shows correct architecture (x86_64) and platform (pc99)
- [ ] Decision cache initializes (256 entries)
- [ ] 103 patterns loaded into cache
- [ ] IPC system initializes successfully
- [ ] IPC ping/pong test passes (10/10 messages)
- [ ] IPC message handler starts listening
- [ ] No kernel panics or crashes
- [ ] Boot time <10 seconds

**If all checkboxes are ✅:** Task 2.2 COMPLETE - Proceed to Task 3 (End-to-End IPC Testing)

---

## Troubleshooting

### Build Issues

**Problem: CMake can't find seL4**
```
CMake Error at CMakeLists.txt:14 (find_package):
  Could not find a package configuration file provided by "seL4"
```

**Solution:**
1. Verify seL4 is installed: `ls ~/jarvis-phase1/kernel`
2. Check toolchain file path: `ls ~/jarvis-phase1/kernel/gcc.cmake`
3. Rebuild with correct path:
   ```bash
   cd ~/jarvis-phase1/build-jarvis
   rm -rf *
   cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake -DPLATFORM=pc99 -DKernelPlatform=x86_64 ../apps/jarvis/sel4
   ```

**Problem: Header files not found**
```
fatal error: decision_cache.h: No such file or directory
```

**Solution:**
Check CMakeLists.txt has correct include paths:
```cmake
target_include_directories(jarvis-sel4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../cache
    ${CMAKE_CURRENT_SOURCE_DIR}/../ipc
)
```

**Problem: Linking errors (undefined references)**
```
undefined reference to `cache_init'
```

**Solution:**
Verify all .c files are in CMakeLists.txt:
```cmake
add_executable(jarvis-sel4
    main.c
    ../cache/decision_cache.c
    ../cache/cache_patterns.c
    ../ipc/ring_buffer.c
    ../ipc/ipc_sel4.c
)
```

### QEMU Issues

**Problem: QEMU doesn't start / kernel panic**

**Debugging steps:**
1. Check kernel image exists:
   ```bash
   ls -lh ~/jarvis-phase1/build-jarvis/images/jarvis-sel4-image-x86_64-pc99
   ```

2. Run with verbose output:
   ```bash
   qemu-system-x86_64 -m 8G -smp 4 -serial stdio -nographic -kernel images/jarvis-sel4-image-x86_64-pc99 -d int,cpu_reset
   ```

3. Check for initialization errors in seL4 console output

**Problem: IPC test fails**

**Possible causes:**
1. Ring buffer not initialized properly
2. Message size exceeds MAX_MESSAGE_SIZE (256 bytes)
3. Cache-line alignment issue

**Debug:**
- Check main.c `ipc_message_handler()` logic
- Verify ring_buffer.c initialization
- Check message struct alignment

---

## Next Steps After Successful Build

Once build and boot complete successfully:

### ✅ Mark Task 2.2 COMPLETE

Update WEEK_10_STATUS.md:
```markdown
## Task 2: Build seL4 with JARVIS Components
- [x] 2.1: Update CMakeLists.txt with cache and IPC components
- [x] 2.2: Build seL4 with all components integrated
- [x] 2.3: Verify boot in QEMU
Status: ✅ COMPLETE
```

### ⏭️ Task 3: End-to-End IPC Testing (Python ↔ seL4)

**Objectives:**
1. Launch seL4 in QEMU with IPC handler running
2. Run Python IPC client from separate terminal
3. Send queries from Python → seL4
4. Verify responses received (cache lookups)
5. Test 100 queries with 100% success rate

**Commands:**
```bash
# Terminal 1: Launch seL4
cd ~/jarvis-phase1/build-jarvis
./simulate

# Terminal 2: Run Python IPC client
cd /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src/ai
python test_ipc_integration.py
```

**Expected result:** Python sends queries → seL4 cache lookup → responses received

---

### ⏭️ Task 4: Performance Benchmarking

**Objectives:**
1. Measure IPC latency (Python → seL4 → Python)
2. Measure cache hit rate (should be >80%)
3. Measure boot time (should be <10s)
4. Compare results to Phase 0 baseline

**Create benchmark script:** `phase1/src/ai/benchmark_qemu.py`

---

### ⏭️ Task 5: Shell Integration (Optional)

**Objective:** Connect interactive shell to seL4 IPC handler

**Test:** User types command in shell → IPC → seL4 cache → response displayed

---

## Part B: Multi-Agent Implementation

After Part A complete, see WEEK_10_STATUS.md for:
- Task 6: Create 4 specialist agents
- Task 7: Implement agent router
- Task 8: Implement shared context pool
- Task 9: Test multi-agent coordination

---

**Document Status:** ACTIVE
**Created:** November 17, 2025
**Updated:** November 17, 2025 (streamlined instructions)
**For:** Week 10 Part A - Task 2.2 (Build seL4 with JARVIS components)
