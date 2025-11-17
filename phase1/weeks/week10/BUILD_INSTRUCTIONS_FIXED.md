# Week 10 Build Instructions - FIXED APPROACH

## The Problem

The seL4 init script creates a TEMPORARY source directory (e.g., `hello-worldXXXXXX/`) populated with tutorial templates. This directory is what actually gets built - NOT the `projects/sel4-tutorials/tutorials/hello-world/` directory.

**Root Cause Discovered:**
- `./init --plat pc99 --tut hello-world` creates `~/jarvis-phase1/hello-worldXXXXXX/` (random suffix)
- This temp directory contains tutorial template code ("Hello, World!")
- The build compiles THIS directory, ignoring our modifications elsewhere
- Result: QEMU boots with "Hello, World!" crash instead of JARVIS

## The Solution

Copy JARVIS sources to the CORRECT location AFTER the init script creates the temp directory.

---

## Step-by-Step Build Instructions

### Step 1: Run Init Script to Create Temp Source Directory

```bash
cd ~/jarvis-phase1

# Clean old builds
rm -rf hello-world*_build
rm -rf hello-world????????  # Remove old temp source dirs

# Run init script (creates hello-worldXXXXXX/ directory)
./init --plat pc99 --tut hello-world
```

**What this does:**
- Creates `hello-worldXXXXXX/` directory (random 8-char suffix)
- Creates `hello-worldXXXXXX_build/` build directory
- Populates source directory with tutorial template

**Find the created directory:**
```bash
ls -d ~/jarvis-phase1/hello-world????????
# Output: /home/user/jarvis-phase1/hello-worldABCDEFGH
```

---

### Step 2: Replace Tutorial Sources with JARVIS Sources

**CRITICAL:** Replace `hello-worldXXXXXX` below with your actual directory name from Step 1.

```bash
# Set variable for convenience (replace XXXXXXXX with your actual suffix)
export JARVIS_SRC=~/jarvis-phase1/hello-worldXXXXXXXX

# Navigate to the temp source directory
cd $JARVIS_SRC

# Backup original tutorial files
mkdir -p ~/tutorial-backup
cp -r . ~/tutorial-backup/

# Remove tutorial template sources
rm -rf src/

# Create new src directory
mkdir -p src

# Copy JARVIS main.c
cp ~/jarvis-phase1/apps/jarvis/sel4/main.c src/

# Create symlinks to cache and ipc directories
ln -s ~/jarvis-phase1/apps/jarvis/cache src/cache
ln -s ~/jarvis-phase1/apps/jarvis/ipc src/ipc

# Verify structure
ls -la src/
# Should see: main.c cache/ (symlink) ipc/ (symlink)

# Verify main.c is JARVIS code
head -30 src/main.c | grep "JARVIS"
# Should see: "  JARVIS AI-OS v0.1 - Phase 1"
```

---

### Step 3: Update CMakeLists.txt in Temp Directory

```bash
# Should still be in $JARVIS_SRC (hello-worldXXXXXXXX/)
cd $JARVIS_SRC

# Backup original CMakeLists.txt
cp CMakeLists.txt CMakeLists.txt.original

# Copy fixed CMakeLists.txt from our repository
cp /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/weeks/week10/CMakeLists_for_tutorial.txt CMakeLists.txt

# Verify it's correct
grep "JARVIS" CMakeLists.txt
# Should see: "# JARVIS Phase 1 - seL4 Tutorial Integration"

grep "regenerate" CMakeLists.txt
# Should see: "# sel4_tutorials_regenerate_tutorial" (COMMENTED OUT)
```

**Alternative:** If cp from Windows doesn't work, create CMakeLists.txt manually:
```bash
cat > CMakeLists.txt << 'EOF'
# JARVIS Phase 1 - seL4 Tutorial Integration
cmake_minimum_required(VERSION 3.7.2)
project(hello-world C ASM)

include(${SEL4_TUTORIALS_DIR}/settings.cmake)

# DO NOT regenerate tutorial - using custom JARVIS sources
# sel4_tutorials_regenerate_tutorial(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(hello-world
    src/main.c
    src/cache/decision_cache.c
    src/cache/cache_patterns.c
    src/ipc/ring_buffer.c
    src/ipc/ipc_sel4.c
)

target_include_directories(hello-world PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cache
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ipc
)

target_link_libraries(hello-world
    sel4
    muslc
    utils
    sel4muslcsys
    sel4platsupport
    sel4utils
    sel4debug
)

DeclareRootserver(hello-world)
EOF
```

---

### Step 4: Build JARVIS

```bash
# Navigate to build directory (replace XXXXXXXX with your suffix)
cd ~/jarvis-phase1/hello-worldXXXXXXXX_build

# Build
ninja
```

**Expected output:**
```
[1/XX] Building C object .../src/main.c.o
[2/XX] Building C object .../src/cache/decision_cache.c.o
[3/XX] Building C object .../src/cache/cache_patterns.c.o
[4/XX] Building C object .../src/ipc/ring_buffer.c.o
[5/XX] Building C object .../src/ipc/ipc_sel4.c.o
...
[XX/XX] Linking hello-world
```

**Verify correct source compiled:**
```bash
# Check build.ninja to confirm source path
grep "src/main.c.obj" build.ninja
# Should show: /home/user/jarvis-phase1/hello-worldXXXXXXXX/src/main.c
```

---

### Step 5: Run in QEMU

```bash
# Should be in ~/jarvis-phase1/hello-worldXXXXXXXX_build
./simulate
```

**Expected output:**
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
```

**Exit QEMU:** Ctrl+A, then X

---

## Troubleshooting

### Problem: QEMU boots with "Hello, World!" then crashes

**Symptoms:**
```
Hello, World!
Caught cap fault in send phase at address 0
vm fault on data at address 0 with status 0x4
```

**This means:** The tutorial's original code is running, not JARVIS code.

**Root Cause:** You copied JARVIS sources to the WRONG directory. The init script created a temp directory (`hello-worldXXXXXXXX/`) but you modified a different location.

**Solution:**

```bash
# 1. Find the actual source directory being compiled
cd ~/jarvis-phase1/hello-world*_build
grep "src/main.c.obj" build.ninja
# Output shows: /home/user/jarvis-phase1/hello-worldABCDEFGH/src/main.c
# This is the directory you need to modify!

# 2. Verify what's in that directory
ls -la ~/jarvis-phase1/hello-worldABCDEFGH/src/
# If you see only main.c with tutorial code, that's the problem

# 3. Check the main.c contents
head -30 ~/jarvis-phase1/hello-worldABCDEFGH/src/main.c
# If you see "Hello, World!" = tutorial template (WRONG)
# If you see "JARVIS AI-OS" = JARVIS code (CORRECT)

# 4. If it's tutorial code, go back to Step 2 and copy JARVIS sources to the CORRECT directory
export JARVIS_SRC=~/jarvis-phase1/hello-worldABCDEFGH  # Use actual suffix!
cd $JARVIS_SRC
rm -rf src/
mkdir -p src
cp ~/jarvis-phase1/apps/jarvis/sel4/main.c src/
ln -s ~/jarvis-phase1/apps/jarvis/cache src/cache
ln -s ~/jarvis-phase1/apps/jarvis/ipc src/ipc

# 5. Update CMakeLists.txt in the same directory
cd $JARVIS_SRC
cp /mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/weeks/week10/CMakeLists_for_tutorial.txt CMakeLists.txt

# 6. Rebuild
cd ~/jarvis-phase1/hello-worldABCDEFGH_build  # Use actual suffix!
ninja
./simulate
```

**Expected output after fix:**
```
========================================
  JARVIS AI-OS v0.1 - Phase 1
  Week 4: IPC Integration Complete
========================================

Initializing decision cache...
✓ Cache initialized (256 entries)
✓ Loaded 103 patterns into cache
```

---

## Success Criteria

After completing all steps, you should have:

### Build Success
- ✅ Init script creates `hello-worldXXXXXXXX/` directory
- ✅ JARVIS sources copied to correct temp directory
- ✅ CMakeLists.txt updated with JARVIS sources
- ✅ All 5 JARVIS .c files compile without errors:
  - `src/main.c`
  - `src/cache/decision_cache.c`
  - `src/cache/cache_patterns.c`
  - `src/ipc/ring_buffer.c`
  - `src/ipc/ipc_sel4.c`
- ✅ Executable `hello-world` builds successfully

### Boot Success
- ✅ QEMU boots with JARVIS banner (NOT "Hello, World!")
- ✅ Decision cache initializes (256 entries)
- ✅ 103 patterns loaded into cache
- ✅ IPC system initializes
- ✅ IPC ping/pong test passes (10/10 messages)
- ✅ IPC message handler starts listening
- ✅ No kernel panics or crashes

### Verification Commands

```bash
# Verify correct source directory compiled
cd ~/jarvis-phase1/hello-world*_build
grep "src/main.c.obj" build.ninja
# Should show: /home/user/jarvis-phase1/hello-worldXXXXXXXX/src/main.c

# Verify JARVIS code in source directory
head -30 ~/jarvis-phase1/hello-world????????/src/main.c | grep "JARVIS"
# Should show: "  JARVIS AI-OS v0.1 - Phase 1"

# Verify symlinks exist
ls -la ~/jarvis-phase1/hello-world????????/src/
# Should show: cache -> ... (symlink), ipc -> ... (symlink)

# Verify CMakeLists.txt is correct
grep "regenerate" ~/jarvis-phase1/hello-world????????/CMakeLists.txt
# Should show: "# sel4_tutorials_regenerate_tutorial" (COMMENTED OUT)
```

---

## Key Insights (For Future Reference)

**The seL4 Tutorials Build System:**

1. **Init script behavior:**
   - Creates temp source directory: `~/jarvis-phase1/hello-worldXXXXXXXX/` (8-char random suffix)
   - Creates build directory: `~/jarvis-phase1/hello-worldXXXXXXXX_build/`
   - Populates source dir with tutorial templates from `.tutegen/` cache

2. **What gets compiled:**
   - Build compiles the TEMP directory (`hello-worldXXXXXXXX/`), NOT `projects/sel4-tutorials/tutorials/hello-world/`
   - Check `build.ninja` to see actual source paths

3. **The correct workflow:**
   - Run `./init` FIRST (creates temp dirs)
   - THEN copy JARVIS sources to the temp directory
   - THEN update CMakeLists.txt in temp directory
   - THEN build

4. **Common mistakes:**
   - ❌ Modifying `projects/sel4-tutorials/tutorials/hello-world/` (ignored by build)
   - ❌ Updating CMakeLists.txt before copying sources
   - ❌ Not verifying actual source path in build.ninja

---

**Document Status:** ACTIVE - CORRECTED APPROACH
**Created:** November 17, 2025
**Updated:** November 17, 2025 (fixed root cause - temp directory issue)
**Purpose:** Build JARVIS with seL4 tutorials framework (QEMU integration)
