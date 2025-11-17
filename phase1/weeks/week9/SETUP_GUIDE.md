# Week 9 QEMU Integration Setup Guide

**Status:** Ready for execution on development workstation
**Created:** November 17, 2025
**Environment:** Requires native Linux or WSL2 with full system access

---

## Prerequisites

Week 9 requires **running on your actual development system** (not in a restricted container) because:

- ✅ QEMU virtualization needs full system access
- ✅ Shared memory (/dev/shm) must be accessible by both host Python and QEMU guest
- ✅ seL4 kernel building requires proper toolchain
- ✅ Performance benchmarking needs real hardware

---

## Step 1: Install QEMU and Dependencies

```bash
# Update package list
sudo apt-get update

# Install QEMU
sudo apt-get install -y qemu-system-x86 qemu-system-arm qemu-utils

# Verify installation
qemu-system-x86_64 --version  # Should show 7.0+ (8.2.2 tested)

# Install seL4 build tools
sudo apt-get install -y build-essential cmake ninja-build \
    gcc-multilib g++-multilib gcc-aarch64-linux-gnu \
    device-tree-compiler libxml2-utils python3-pip

# Install Python dependencies
pip3 install --break-system-packages aenum future pyelftools ply sortedcontainers lxml
```

---

## Step 2: Download seL4 Tutorials (if not already done)

```bash
# Install Google repo tool
mkdir -p ~/bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo
export PATH=~/bin:$PATH

# Download seL4 tutorials (~500MB)
mkdir -p ~/sel4-tutorials
cd ~/sel4-tutorials
repo init -u https://github.com/seL4/sel4-tutorials-manifest
repo sync

# Verify download
ls -la  # Should see: apps, kernel, projects, tools, etc.
```

---

## Step 3: Build JARVIS seL4 Kernel

```bash
# Copy JARVIS components to seL4 project
cd /path/to/JARVIS_OS/phase1

# Option A: Use existing seL4 tutorial structure
# Copy our components into seL4 tutorial hello-world app
cp -r src/cache ~/sel4-tutorials/apps/hello-world/src/
cp -r src/ipc ~/sel4-tutorials/apps/hello-world/src/
cp src/sel4/main.c ~/sel4-tutorials/apps/hello-world/src/

# Option B: Create standalone JARVIS seL4 app (recommended)
# See scripts/build-jarvis-sel4.sh for automated setup

# Build seL4 with JARVIS components
cd ~/sel4-tutorials/hello-world
mkdir build
cd build
../init-build.sh -DPLATFORM=pc99 -DTUTORIAL=hello-world
ninja

# Verify build succeeded
ls -la images/  # Should see hello-world-image-x86_64-pc99
```

---

## Step 4: Configure Shared Memory for IPC

```bash
# Verify /dev/shm exists and is writable
ls -la /dev/shm/
# Should show: drwxrwxrwt (world-writable sticky bit)

# Test shared memory creation
python3 -c "
import mmap
import os
shm_path = '/dev/shm/test_jarvis'
with open(shm_path, 'wb') as f:
    f.write(b'\\x00' * 4096)
fd = os.open(shm_path, os.O_RDWR)
shm = mmap.mmap(fd, 4096, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)
print('Shared memory test: OK')
shm.close()
os.close(fd)
os.unlink(shm_path)
"

# If test fails, check:
# 1. /dev/shm is mounted: mount | grep shm
# 2. Permissions are correct: ls -ld /dev/shm
# 3. tmpfs size is sufficient: df -h /dev/shm
```

---

## Step 5: Launch seL4 in QEMU

```bash
# Navigate to JARVIS scripts directory
cd /path/to/JARVIS_OS/phase1/scripts

# Launch seL4 with QEMU (use provided script)
./launch-jarvis-qemu.sh

# OR manually:
qemu-system-x86_64 \
    -m 512M \
    -nographic \
    -serial mon:stdio \
    -kernel ~/sel4-tutorials/hello-world/build/images/kernel-x86_64-pc99 \
    -initrd ~/sel4-tutorials/hello-world/build/images/hello-world-image-x86_64-pc99

# Expected output:
# [...]
# Booting all finished, dropped to user space
# JARVIS AI-OS Phase 1 - seL4 Integration
# Decision cache initialized: 200 patterns loaded
# IPC ring buffer initialized (head=0, tail=0)
# IPC Message Handler - Listening for Python queries
```

---

## Step 6: Test Python ↔ seL4 IPC

In a **separate terminal** (while seL4 is running in QEMU):

```bash
# Navigate to Python IPC client
cd /path/to/JARVIS_OS/phase1/src/ai

# Run integration test
python3 test_week9_integration.py

# Expected output:
# [TEST] Connecting to seL4 via shared memory...
# [OK] Connected to /dev/shm/jarvis_ipc
# [TEST] Sending query: "show cpu"
# [OK] Query sent via IPC
# [OK] Response received: ACTION:READ_CPU_STATS|TRUST:0|HIT:1
# [OK] Cache hit confirmed
# [...]
# Week 9 Integration Test: PASSED (4/4 tests)
```

---

## Step 7: Performance Benchmarking

```bash
# Run performance benchmarks
python3 test_week9_performance.py

# Expected results:
# IPC Round-trip Latency:  <200μs median (target: <100μs, allowing QEMU overhead)
# Cache Lookup (via IPC):  <2ms total
# End-to-End Query:        <3s (cached)
# Boot Time:               <10s (target: <60s)
# Cache Hit Rate:          >80%
```

---

## Troubleshooting

### Issue: QEMU not found
```bash
which qemu-system-x86_64
# If empty, reinstall QEMU
sudo apt-get install -y qemu-system-x86
```

### Issue: Shared memory not accessible
```bash
# Check if /dev/shm is mounted
mount | grep shm
# Should show: tmpfs on /dev/shm type tmpfs (rw,nosuid,nodev)

# If not mounted:
sudo mount -t tmpfs -o size=512M tmpfs /dev/shm
```

### Issue: seL4 build fails
```bash
# Check CMake version (need 3.12+)
cmake --version

# Check Ninja version
ninja --version

# Rebuild clean
cd ~/sel4-tutorials/hello-world/build
rm -rf *
../init-build.sh -DPLATFORM=pc99 -DTUTORIAL=hello-world
ninja
```

### Issue: Python can't connect to seL4
```bash
# Verify seL4 is running and created shared memory
ls -la /dev/shm/jarvis_ipc
# Should show file owned by user running QEMU

# Check Python IPC client is using correct path
grep "jarvis_ipc" phase1/src/ai/ipc_client.py
# Should match /dev/shm/jarvis_ipc
```

### Issue: No response from seL4
```bash
# Check seL4 console output for errors
# In QEMU window, should see:
# "IPC Message Handler - Listening for Python queries"

# Check if ring buffer is initialized
# seL4 should print:
# "IPC ring buffer initialized (head=0, tail=0)"
```

---

## Expected Week 9 Results

**Success Criteria:**
- ✅ seL4 boots in QEMU (<10 seconds)
- ✅ Decision cache loads 200 patterns
- ✅ IPC ring buffer initializes correctly
- ✅ Python connects to seL4 via shared memory
- ✅ Queries sent from Python appear in seL4 console
- ✅ Responses received by Python (ACTION:xxx|TRUST:x|HIT:x format)
- ✅ Cache hit rate >80%
- ✅ IPC latency <200μs round-trip (QEMU has overhead)
- ✅ Zero kernel panics or crashes

**Performance Targets:**
| Metric | Target | Notes |
|--------|--------|-------|
| Boot time | <60s | Phase 1 gate criteria |
| IPC latency | <100μs | May be higher in QEMU (~200μs acceptable) |
| Cache lookup | <0.1ms | Via IPC |
| Cached query | <2s | End-to-end |
| Uncached query | <3s | With AI inference |
| Cache hit rate | >80% | Validated in Phase 0 |

---

## Next Steps After Week 9

**Week 10-12: Multi-Agent Architecture**
- Integrate multiple specialist agents
- Implement shared context memory pool
- Test agent coordination and conflict resolution

**Prerequisites from Week 9:**
- ✅ IPC bridge validated in QEMU
- ✅ Decision cache working end-to-end
- ✅ Performance targets met

---

## Files Created for Week 9

```
phase1/weeks/week9/
├── SETUP_GUIDE.md                    # This file
├── WEEK_9_STATUS.md                  # Progress tracker
├── test_week9_integration.py         # Integration test (Python + seL4)
├── test_week9_performance.py         # Performance benchmarks
└── launch-jarvis-qemu.sh            # QEMU launcher script

phase1/scripts/
├── build-jarvis-sel4.sh             # Automated seL4 build
└── launch-jarvis-qemu.sh            # QEMU launcher (symlink)
```

---

**Document Version:** 1.0
**Last Updated:** November 17, 2025
**Status:** Ready for execution on development workstation
