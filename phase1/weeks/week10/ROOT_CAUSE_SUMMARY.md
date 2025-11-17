# Week 10 Build Issue - Root Cause & Solution

**Date:** November 17, 2025
**Status:** RESOLVED - Instructions updated in BUILD_INSTRUCTIONS_FIXED.md

---

## Root Cause Discovered

### The Problem

QEMU was booting with "Hello, World!" and crashing instead of running JARVIS code.

### Investigation Results

We examined `build.ninja` and found:
```
build CMakeFiles/hello-world.dir/src/main.c.obj: C_COMPILER__hello-world_unscanned_Debug /home/itsme/jarvis-phase1/hello-world8koxdqfw/src/main.c
```

**Key Discovery:** The build was compiling `/home/itsme/jarvis-phase1/hello-world8koxdqfw/src/main.c` (tutorial template "Hello, World!" code), NOT the JARVIS code we placed in `projects/sel4-tutorials/tutorials/hello-world/`.

### Why This Happened

The seL4 `./init` script creates a **temporary source directory** with a random suffix:
- Directory: `~/jarvis-phase1/hello-worldXXXXXXXX/` (8 random chars)
- Build dir: `~/jarvis-phase1/hello-worldXXXXXXXX_build/`
- Contents: Tutorial template code from `.tutegen/` cache

**The build compiles THIS temp directory, not `projects/sel4-tutorials/tutorials/hello-world/`!**

### What We Did Wrong

We were modifying `projects/sel4-tutorials/tutorials/hello-world/` directory, which the build system completely ignores. The init script creates a fresh temp directory every time and builds THAT.

---

## The Solution

**Copy JARVIS sources to the CORRECT location** - the temp directory created by init script.

### Corrected Workflow

1. **Run init script** → Creates `hello-worldXXXXXXXX/` with tutorial template
2. **Find temp directory** → `ls -d ~/jarvis-phase1/hello-world????????`
3. **Copy JARVIS sources** → To `hello-worldXXXXXXXX/src/` (replacing tutorial code)
4. **Update CMakeLists.txt** → In temp directory (comment out regeneration)
5. **Build** → `cd hello-worldXXXXXXXX_build && ninja`
6. **Run** → `./simulate`

---

## Updated Instructions

**See:** `BUILD_INSTRUCTIONS_FIXED.md` (completely rewritten with correct approach)

### Quick Start

```bash
# Step 1: Run init (creates temp dir)
cd ~/jarvis-phase1
rm -rf hello-world*  # Clean old attempts
./init --plat pc99 --tut hello-world

# Step 2: Find the created directory
ls -d ~/jarvis-phase1/hello-world????????
# Output: /home/user/jarvis-phase1/hello-worldABCDEFGH

# Step 3: Set variable and copy JARVIS sources
export JARVIS_SRC=~/jarvis-phase1/hello-worldABCDEFGH  # USE ACTUAL SUFFIX!
cd $JARVIS_SRC
rm -rf src/
mkdir -p src
cp ~/jarvis-phase1/apps/jarvis/sel4/main.c src/
ln -s ~/jarvis-phase1/apps/jarvis/cache src/cache
ln -s ~/jarvis-phase1/apps/jarvis/ipc src/ipc

# Step 4: Update CMakeLists.txt
cp /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/weeks/week10/CMakeLists_for_tutorial.txt CMakeLists.txt

# Step 5: Build and run
cd ~/jarvis-phase1/hello-worldABCDEFGH_build  # USE ACTUAL SUFFIX!
ninja
./simulate
```

---

## Verification

Before running in QEMU, verify you're building the right code:

```bash
# Check build.ninja for source path
cd ~/jarvis-phase1/hello-world*_build
grep "src/main.c.obj" build.ninja
# Should show: /home/user/jarvis-phase1/hello-worldXXXXXXXX/src/main.c

# Check main.c is JARVIS code
head -30 ~/jarvis-phase1/hello-world????????/src/main.c | grep "JARVIS"
# Should show: "  JARVIS AI-OS v0.1 - Phase 1"
# If you see "Hello, World!" = WRONG FILE!
```

---

## Expected QEMU Output

If successful, you should see:

```
========================================
  JARVIS AI-OS v0.1 - Phase 1
  Week 4: IPC Integration Complete
========================================

Initializing decision cache...
✓ Cache initialized (256 entries)
✓ Loaded 103 patterns into cache

Initializing IPC system...
✓ IPC initialized

========== IPC PING/PONG TEST ==========
...
✓ IPC PING/PONG TEST PASSED
```

**NOT:**
```
Hello, World!
Caught cap fault in send phase at address 0
```

---

## Files Updated

1. **BUILD_INSTRUCTIONS_FIXED.md** - Complete rewrite with corrected workflow
2. **CMakeLists_for_tutorial.txt** - Updated to use `src/` directory structure
3. **ROOT_CAUSE_SUMMARY.md** - This file (quick reference)

---

## Next Steps After Successful Boot

Once JARVIS boots successfully in QEMU:

1. ✅ Mark Task 2 (Build seL4) as COMPLETE
2. ⏭️ Proceed to Task 3: End-to-End IPC Testing (Python ↔ seL4)
3. ⏭️ Task 4: Performance benchmarking
4. ⏭️ Task 5: Shell integration (optional)

---

**Document Status:** ACTIVE
**Created:** November 17, 2025
**Purpose:** Document root cause discovery and solution for Week 10 build issue
