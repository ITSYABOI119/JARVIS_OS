# Week 10 Build Instructions - FIXED APPROACH

## The Problem

seL4 tutorials use a special build system with the `init` script. Our standalone CMakeLists.txt doesn't work because `find_package(seL4)` requires the tutorials framework setup.

## The Solution

Use the working Week 1-2 approach: leverage the existing hello-world tutorial build and modify it for JARVIS.

---

## Step-by-Step Build Instructions

### Step 1: Check Existing Build from Week 1-2

```bash
cd ~/jarvis-phase1/build
ls -la
```

**If you see hello-world directories**, proceed to Step 2.
**If not**, run:
```bash
cd ~/jarvis-phase1
./init --plat pc99 --tut hello-world
```

This creates `build/` directory with proper seL4 configuration.

---

### Step 2: Replace hello-world Source with JARVIS Source

The key insight: Reuse the hello-world tutorial's CMake setup, but replace the source files.

```bash
# Navigate to hello-world source in projects
cd ~/jarvis-phase1/projects/sel4-tutorials/tutorials/hello-world

# Backup original
mkdir -p ~/hello-world-backup
cp -r . ~/hello-world-backup/

# Remove original source files
rm -f *.c *.h

# Copy JARVIS main.c
cp ~/jarvis-phase1/apps/jarvis/sel4/main.c .

# Create symlinks to cache and ipc directories
ln -s ~/jarvis-phase1/apps/jarvis/cache cache
ln -s ~/jarvis-phase1/apps/jarvis/ipc ipc

# Verify
ls -la
# Should see: main.c cache/ (symlink) ipc/ (symlink)
```

---

### Step 3: Update hello-world CMakeLists.txt

```bash
cd ~/jarvis-phase1/projects/sel4-tutorials/tutorials/hello-world

# Backup original CMakeLists.txt
cp CMakeLists.txt CMakeLists.txt.original

# Create new CMakeLists.txt for JARVIS
cat > CMakeLists.txt << 'EOF'
# JARVIS Phase 1 - seL4 Tutorial Integration
# Based on hello-world tutorial template

cmake_minimum_required(VERSION 3.7.2)

# This project is set up by the tutorials framework
project(hello-world C ASM)

# Include tutorial helpers (provided by init script)
include(${SEL4_TUTORIALS_DIR}/settings.cmake)

# Define executable with JARVIS sources
add_executable(hello-world
    main.c
    cache/decision_cache.c
    cache/cache_patterns.c
    ipc/ring_buffer.c
    ipc/ipc_sel4.c
)

# Add include directories
target_include_directories(hello-world PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/cache
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc
)

# Link libraries (provided by tutorials framework)
target_link_libraries(hello-world
    sel4
    muslc
    utils
    sel4muslcsys
    sel4platsupport
    sel4utils
    sel4debug
)

# Declare as root server
DeclareRootserver(hello-world)
EOF
```

---

### Step 4: Rebuild with init Script

```bash
cd ~/jarvis-phase1

# Clean previous build
rm -rf build

# Re-initialize with modified hello-world
./init --plat pc99 --tut hello-world

# Build
cd build
ninja
```

**Expected output:**
```
[1/XX] Building C object .../main.c.o
[2/XX] Building C object .../decision_cache.c.o
[3/XX] Building C object .../cache_patterns.c.o
[4/XX] Building C object .../ring_buffer.c.o
[5/XX] Building C object .../ipc_sel4.c.o
...
[XX/XX] Linking hello-world
```

---

### Step 5: Run in QEMU

```bash
# Should be in ~/jarvis-phase1/build
./simulate
```

**Expected output:**
```
========================================
  JARVIS AI-OS v0.1 - Phase 1
  Week 8: IPC Integration Complete
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

**Exit QEMU:** Ctrl+A, then X

---

## Alternative: Use build-jarvis with Toolchain

If the above doesn't work, try this approach using the existing build infrastructure:

```bash
cd ~/jarvis-phase1/build-jarvis

# Remove previous attempt
rm -rf *

# Use the hello-world build as template
# Copy CMakeCache and cmake files from working build
cp -r ../build/CMakeFiles .
cp ../build/CMakeCache.txt .

# Reconfigure
cmake -G Ninja \
    -C ../build/CMakeCache.txt \
    ../apps/jarvis/sel4

ninja
```

---

## If Both Fail: Check Week 1 Build

Run these diagnostics and share output:

```bash
# Check what's in the working Week 1 build
cd ~/jarvis-phase1/build
find . -name "*.cmake" | head -10
cat CMakeCache.txt | grep -i "sel4"

# Check tutorial structure
ls -la ~/jarvis-phase1/projects/sel4-tutorials/tutorials/
```

Then I can provide exact commands based on your setup.

---

## Success Criteria

- ✅ CMake configures without "FindseL4.cmake" error
- ✅ All 5 JARVIS .c files compile
- ✅ Executable builds successfully
- ✅ QEMU boots with JARVIS banner
- ✅ IPC ping/pong test passes

---

**Document Status:** ACTIVE - FIXED APPROACH
**Created:** November 17, 2025
**Purpose:** Resolve CMake FindseL4 error using tutorials framework
