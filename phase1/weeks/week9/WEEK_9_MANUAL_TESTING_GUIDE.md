# Week 9 Manual Testing Guide

**For:** Running Week 9 QEMU integration tests manually on your local machine
**Created:** November 17, 2025
**Status:** Ready for execution

---

## 📋 Overview

Since you're using the web version of Claude, you'll run Week 9 tests manually on your machine and report back the results. This guide provides **exact commands** to copy/paste.

**What We're Testing:**
1. ✅ QEMU environment setup
2. ✅ Python ↔ seL4 IPC communication
3. ✅ Decision cache in QEMU
4. ✅ Performance benchmarks

**Time Required:** ~30-60 minutes for first-time setup

---

## 🔧 Step 1: Install Prerequisites

### On Linux/WSL2:

```bash
# Update package lists
sudo apt-get update

# Install QEMU
sudo apt-get install -y qemu-system-x86 qemu-system-arm qemu-utils

# Install build tools (if not already installed)
sudo apt-get install -y build-essential cmake ninja-build git python3 python3-pip

# Install Python dependencies
pip3 install llama-cpp-python psutil

# Verify installations
qemu-system-x86_64 --version
cmake --version
ninja --version
python3 --version
```

**Copy the output of these commands and save it** - we'll need it for the results report.

---

## 🔧 Step 2: Check Your Project Setup

```bash
# Navigate to project directory
cd ~/JARVIS_OS  # Adjust path if different

# Verify you're on the right branch
git branch --show-current
# Should show: claude/ipc-cache-week9-prep-01EVEPPTzQu28tELaNSodprp

# Pull latest changes
git pull origin claude/ipc-cache-week9-prep-01EVEPPTzQu28tELaNSodprp

# Verify Week 9 files exist
ls -la phase1/weeks/week9/
ls -la phase1/scripts/launch-jarvis-qemu.sh

# Make scripts executable
chmod +x phase1/scripts/launch-jarvis-qemu.sh
chmod +x phase1/weeks/week9/test_week9_integration.py
```

---

## 🔧 Step 3: Test C Components (Standalone)

These tests don't require QEMU - they validate the core components work on your machine.

```bash
# Navigate to source directory
cd ~/JARVIS_OS/phase1/src

# Test 1: Decision Cache
cd cache
gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm
./test_cache

# Test 2: IPC Ring Buffer
cd ../ipc
gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm
./test_ipc

# Test 3: Python IPC Client
cd ../ai
python3 test_ipc_integration.py

# Test 4: Query Processor with IPC
python3 test_ipc_cache_lookup.py
```

**IMPORTANT:** Copy and paste all the output from these tests. We need to see:
- Number of tests passed/failed
- Any error messages
- Performance numbers (latency, etc.)

---

## 🔧 Step 4: Check seL4 Setup (Week 1)

Week 9 requires the seL4 environment from Week 1. Let's verify it exists:

```bash
# Check if seL4 tutorials exist
ls -la ~/sel4-tutorials/
ls -la ~/sel4-tutorials/hello-world/

# If the directory doesn't exist, we need to set it up
# (Skip to Step 5 if it exists)
```

### If seL4 NOT Found - Quick Setup:

```bash
# Create working directory
mkdir -p ~/sel4-work
cd ~/sel4-work

# Install seL4 dependencies
sudo apt-get install -y \
    python3-dev python3-pip python3-setuptools python3-wheel \
    libxml2-utils ncurses-dev \
    curl git doxygen device-tree-compiler u-boot-tools

# Install seL4 Python tools
pip3 install --user sel4-deps

# Clone seL4 tutorials
mkdir -p ~/sel4-tutorials
cd ~/sel4-tutorials
repo init -u https://github.com/seL4/sel4-tutorials-manifest
repo sync

# Initialize hello-world tutorial
cd ~/sel4-tutorials
./init --plat pc99 --tut hello-world
```

**NOTE:** This can take 10-20 minutes the first time. Copy any error messages if it fails.

---

## 🔧 Step 5: Build seL4 with JARVIS Components

```bash
# Navigate to hello-world tutorial
cd ~/sel4-tutorials/hello-world

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPLATFORM=pc99 \
    -DSIMULATION=TRUE \
    ..

# Build
ninja

# Verify binary was created
ls -lh images/kernel-x86_64-pc99
ls -lh images/capdl-loader-image-x86_64-pc99
```

**Copy the output** - especially any build errors or warnings.

---

## 🔧 Step 6: Quick QEMU Test (Vanilla seL4)

Before testing JARVIS components, let's make sure seL4 boots in QEMU:

```bash
# From ~/sel4-tutorials/hello-world/build directory
./simulate

# You should see seL4 boot messages
# Press Ctrl+A then X to exit QEMU
```

**Expected output:**
```
ELF-loader started on CPU: IA32 (0 cores)
...
<seL4(CPU 0) [decodeInvocation/530 T0xffffff8000203400 "rootserver" @4006f8]> ...
main@main.c:23 hello world
```

**Copy the first ~20 lines of output** - this confirms QEMU works.

---

## 🔧 Step 7: Run Python-Only Integration Tests

These tests use mock IPC to validate the Python components work:

```bash
# Navigate to Week 9 tests
cd ~/JARVIS_OS/phase1/weeks/week9

# Run integration test in MOCK mode (no QEMU required)
python3 test_week9_integration.py

# The test will detect no QEMU and use mock mode
```

**Expected output:**
```
======================================================================
JARVIS AI-OS Phase 1 - Week 9 Integration Test
======================================================================

Checking IPC client availability...
WARNING: IPC client not available (QEMU not running)
Running in MOCK MODE for development testing

----------------------------------------------------------------------
TEST 1: IPC Connection
----------------------------------------------------------------------
[PASS] IPC client initialization (mock mode)

... etc ...
```

**Copy the full test output** - especially the summary at the end.

---

## 🔧 Step 8: Check Shared Memory Setup

```bash
# Check /dev/shm exists and is writable
ls -la /dev/shm/
df -h /dev/shm

# Create test shared memory file
echo "JARVIS IPC test" > /dev/shm/test_jarvis_ipc
cat /dev/shm/test_jarvis_ipc
rm /dev/shm/test_jarvis_ipc

# Check permissions
stat /dev/shm
```

**Copy the output** - we need to verify shared memory is available.

---

## 🔧 Step 9: Integration Test Checklist

Run through this checklist and note the results:

```bash
# Checklist
cd ~/JARVIS_OS

# [ ] 1. C components compile and run
cd phase1/src/cache && gcc -O2 decision_cache.c cache_patterns.c test_cache.c -o test_cache -lm && ./test_cache

# [ ] 2. IPC ring buffer works
cd ~/JARVIS_OS/phase1/src/ipc && gcc -O2 ring_buffer.c test_ipc.c -o test_ipc -lm && ./test_ipc

# [ ] 3. Python IPC client works
cd ~/JARVIS_OS/phase1/src/ai && python3 test_ipc_integration.py

# [ ] 4. Query processor with IPC works
cd ~/JARVIS_OS/phase1/src/ai && python3 test_ipc_cache_lookup.py

# [ ] 5. QEMU installed and working
qemu-system-x86_64 --version

# [ ] 6. seL4 builds successfully
cd ~/sel4-tutorials/hello-world/build && ninja

# [ ] 7. seL4 boots in QEMU
cd ~/sel4-tutorials/hello-world/build && ./simulate
# (Press Ctrl+A then X to exit)

# [ ] 8. Shared memory available
ls -la /dev/shm/
```

---

## 📊 Step 10: Collect Results

Create a file with your results:

```bash
cd ~/JARVIS_OS

# Create results file
cat > WEEK_9_RESULTS.txt << 'EOF'
========================================
JARVIS AI-OS Week 9 Manual Test Results
========================================
Date: [FILL IN]
System: [e.g., Ubuntu 22.04, WSL2, etc.]
CPU: [e.g., Intel i7-12700K]
RAM: [e.g., 32GB]

----------------------------------------
Step 1: Prerequisites
----------------------------------------
QEMU version:
[PASTE OUTPUT OF: qemu-system-x86_64 --version]

CMake version:
[PASTE OUTPUT OF: cmake --version]

Python version:
[PASTE OUTPUT OF: python3 --version]

----------------------------------------
Step 3: C Components Test Results
----------------------------------------
Decision Cache Test:
[PASTE FULL OUTPUT OF: ./test_cache]

IPC Ring Buffer Test:
[PASTE FULL OUTPUT OF: ./test_ipc]

Python IPC Integration Test:
[PASTE FULL OUTPUT OF: python3 test_ipc_integration.py]

Python IPC Cache Lookup Test:
[PASTE FULL OUTPUT OF: python3 test_ipc_cache_lookup.py]

----------------------------------------
Step 5: seL4 Build Results
----------------------------------------
[PASTE OUTPUT OF: ninja]

Build successful? [YES/NO]
Any warnings? [LIST THEM]

----------------------------------------
Step 6: seL4 QEMU Boot Test
----------------------------------------
[PASTE FIRST 20 LINES OF: ./simulate]

Did seL4 boot? [YES/NO]
Any errors? [LIST THEM]

----------------------------------------
Step 8: Shared Memory Check
----------------------------------------
[PASTE OUTPUT OF: ls -la /dev/shm/]
[PASTE OUTPUT OF: df -h /dev/shm]

Shared memory working? [YES/NO]

----------------------------------------
Step 9: Checklist Results
----------------------------------------
[ ] 1. C components compile and run
[ ] 2. IPC ring buffer works
[ ] 3. Python IPC client works
[ ] 4. Query processor with IPC works
[ ] 5. QEMU installed and working
[ ] 6. seL4 builds successfully
[ ] 7. seL4 boots in QEMU
[ ] 8. Shared memory available

----------------------------------------
Issues Encountered
----------------------------------------
[DESCRIBE ANY PROBLEMS, ERRORS, OR UNEXPECTED BEHAVIOR]

----------------------------------------
Performance Notes
----------------------------------------
[ANY OBSERVATIONS ABOUT SPEED, LATENCY, ETC.]

========================================
END OF RESULTS
========================================
EOF

# Open the file for editing
nano WEEK_9_RESULTS.txt
# OR
vim WEEK_9_RESULTS.txt
# OR
code WEEK_9_RESULTS.txt  # If using VS Code
```

---

## 📤 Step 11: Report Back to Claude

Once you've completed the tests and filled in `WEEK_9_RESULTS.txt`:

1. **Copy the contents** of `WEEK_9_RESULTS.txt`
2. **Paste it** in your next message to Claude (web version)
3. **Include any questions** or issues you encountered

Claude will:
- ✅ Analyze the results
- ✅ Identify any issues
- ✅ Provide solutions for problems
- ✅ Determine if Week 9 is complete
- ✅ Plan next steps (Week 10 or troubleshooting)

---

## 🆘 Common Issues & Quick Fixes

### Issue 1: QEMU Not Found
```bash
# Fix: Install QEMU
sudo apt-get update
sudo apt-get install -y qemu-system-x86 qemu-utils
```

### Issue 2: Permission Denied on /dev/shm
```bash
# Fix: Check permissions
sudo chmod 1777 /dev/shm
```

### Issue 3: seL4 Build Fails - Missing Dependencies
```bash
# Fix: Install all dependencies
sudo apt-get install -y build-essential cmake ninja-build \
    python3-dev python3-pip libxml2-utils ncurses-dev \
    device-tree-compiler git curl
```

### Issue 4: Python Module Not Found
```bash
# Fix: Install Python dependencies
pip3 install --user llama-cpp-python psutil
```

### Issue 5: seL4 Repo Init Fails
```bash
# Fix: Install repo tool
sudo apt-get install -y repo
# OR manually:
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod +x ~/bin/repo
export PATH=$HOME/bin:$PATH
```

---

## ⏱️ Estimated Time

- **Step 1** (Prerequisites): 5-10 minutes
- **Step 2** (Project setup): 2 minutes
- **Step 3** (C components): 5 minutes
- **Step 4-5** (seL4 setup): 15-30 minutes (first time only)
- **Step 6** (QEMU test): 2 minutes
- **Step 7** (Python tests): 5 minutes
- **Step 8** (Shared memory): 2 minutes
- **Step 9** (Checklist): 10 minutes
- **Step 10** (Results): 5 minutes

**Total:** ~30-60 minutes (first time), ~15-20 minutes (if seL4 already set up)

---

## 🎯 Success Criteria

Week 9 is successful if:

- [x] All C components compile and run (test_cache, test_ipc)
- [x] Python IPC client tests pass (6/6 tests)
- [x] Python query processor tests pass (4/4 tests)
- [x] QEMU installed and working
- [x] seL4 builds without errors
- [x] seL4 boots in QEMU
- [x] Shared memory (/dev/shm) accessible

**Stretch Goals** (not required for Week 9):
- [ ] Python ↔ seL4 communication in QEMU (Week 9 extended / Week 10)
- [ ] IPC latency benchmarks in QEMU

---

## 📞 Need Help?

If you get stuck:

1. **Copy the exact error message**
2. **Copy the command that failed**
3. **Note your operating system** (Ubuntu version, WSL version, etc.)
4. **Paste it all** in your message to Claude

Claude will help troubleshoot!

---

**Ready?** Start with **Step 1** and work through sequentially. Good luck! 🚀
