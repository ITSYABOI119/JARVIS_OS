# Week 9 Quick-Start Guide (Claude CLI)

**Created:** November 17, 2025
**For:** Running Week 9 QEMU integration tests via Claude CLI on your local machine

---

## Why Use Claude CLI for Week 9?

Week 9 requires **full system access** that containerized environments don't have:

- ✅ **sudo permissions** - Install QEMU and dependencies
- ✅ **QEMU virtualization** - Run seL4 kernel in VM
- ✅ **Shared memory** - /dev/shm must be accessible by both host and QEMU guest
- ✅ **Real hardware** - Performance benchmarking needs actual CPU/memory

**Solution:** Open this project in Claude CLI on your development workstation!

---

## Step 1: Install Dependencies (One-Time Setup)

Open Claude CLI on your local machine and run:

```bash
# Navigate to JARVIS_OS project
cd /path/to/JARVIS_OS

# Install QEMU and build tools
sudo apt-get update
sudo apt-get install -y qemu-system-x86 qemu-system-arm qemu-utils \
    build-essential cmake ninja-build gcc-multilib g++-multilib \
    device-tree-compiler libxml2-utils

# Verify QEMU installed
qemu-system-x86_64 --version  # Should show 7.0+ (8.2.2+ recommended)

# Install Python dependencies
pip3 install --break-system-packages aenum future pyelftools ply sortedcontainers lxml llama-cpp-python
```

---

## Step 2: Download seL4 Tutorials (One-Time Setup)

```bash
# Install Google repo tool
mkdir -p ~/bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo
export PATH=~/bin:$PATH

# Download seL4 tutorials (~500MB, takes 5-10 minutes)
mkdir -p ~/sel4-tutorials
cd ~/sel4-tutorials
repo init -u https://github.com/seL4/sel4-tutorials-manifest
repo sync

# Verify
ls -la  # Should see: apps, kernel, projects, tools, etc.
```

---

## Step 3: Build seL4 with JARVIS Components

```bash
# Copy JARVIS components to seL4 hello-world app
cd /path/to/JARVIS_OS/phase1

# Copy decision cache
cp -r src/cache ~/sel4-tutorials/hello-world/src/

# Copy IPC layer
cp -r src/ipc ~/sel4-tutorials/hello-world/src/

# Copy seL4 main (with IPC handler)
cp src/sel4/main.c ~/sel4-tutorials/hello-world/src/

# Update CMakeLists.txt to include our components
# (TODO: automated script needed - for now, manual integration)

# Build seL4
cd ~/sel4-tutorials/hello-world
mkdir -p build
cd build
../init-build.sh -DPLATFORM=pc99 -DTUTORIAL=hello-world
ninja

# Verify build succeeded
ls -la images/hello-world-image-x86_64-pc99  # Should exist
```

---

## Step 4: Run Week 9 Integration Test

**Terminal 1 - Launch seL4 in QEMU:**

```bash
cd /path/to/JARVIS_OS/phase1/scripts
./launch-jarvis-qemu.sh

# Expected output:
# [INFO] Checking prerequisites...
# [INFO] QEMU: QEMU emulator version 8.2.2
# [INFO] Setting up shared memory for IPC...
# [INFO] Launching seL4 in QEMU...
# [...]
# Booting all finished, dropped to user space
# JARVIS AI-OS Phase 1
# Decision cache initialized: 200 patterns loaded
# IPC ring buffer initialized (head=0, tail=0)
# IPC Message Handler - Listening for Python queries
```

**Terminal 2 - Run Integration Test:**

```bash
cd /path/to/JARVIS_OS/phase1/weeks/week9
python3 test_week9_integration.py

# Expected output:
# ======================================================================
# JARVIS AI-OS - Phase 1 Week 9
# End-to-End Integration Test
# ======================================================================
#
# Press ENTER when seL4 is ready in QEMU...
# [Press ENTER]
#
# [TEST] Connecting to shared memory: /dev/shm/jarvis_ipc
# [PASS] Connected successfully
# [...]
# [WEEK 9 INTEGRATION TEST: PASSED]
# ✅ Python ↔ seL4 IPC bridge working end-to-end
```

---

## Step 5: Review Results

**Week 9 Success Criteria:**

- ✅ seL4 boots in QEMU (<10 seconds)
- ✅ Decision cache loaded (200 patterns)
- ✅ IPC ring buffer initialized
- ✅ Python connects via shared memory
- ✅ Queries sent and responses received
- ✅ Cache hit rate >80%
- ✅ IPC latency <200μs (QEMU allows overhead vs 100μs bare metal)
- ✅ Zero kernel panics

**If all tests pass:**

Week 9 is COMPLETE! ✅ Ready for Week 10 (Multi-Agent Architecture).

**If tests fail:**

Check:
1. seL4 console (Terminal 1) for error messages
2. Shared memory permissions: `ls -la /dev/shm/jarvis_ipc`
3. QEMU process running: `ps aux | grep qemu`
4. Python IPC client using correct path (check ipc_client.py)

---

## Troubleshooting

### "QEMU not found"

```bash
which qemu-system-x86_64
# If empty, reinstall: sudo apt-get install qemu-system-x86
```

### "seL4 build directory not found"

```bash
# Make sure you ran Step 3 completely
cd ~/sel4-tutorials/hello-world/build
ninja  # Rebuild if needed
```

### "Permission denied on /dev/shm"

```bash
# Check /dev/shm is mounted and writable
mount | grep shm
ls -ld /dev/shm  # Should show: drwxrwxrwt

# If not mounted:
sudo mount -t tmpfs -o size=512M tmpfs /dev/shm
```

### "No response from seL4"

```bash
# Check seL4 console (Terminal 1) shows:
# "IPC Message Handler - Listening for Python queries"

# If not, seL4 didn't boot correctly. Check:
# 1. Kernel image exists
# 2. QEMU launched without errors
# 3. seL4 didn't panic (check console output)
```

---

## Next Steps

**After Week 9 Passes:**

1. **Update Progress Tracker:**
   ```bash
   cd /path/to/JARVIS_OS/phase1/docs
   # Edit PHASE_1_PROGRESS_TRACKER.md
   # Mark Week 9 as COMPLETE ✅
   ```

2. **Commit Results:**
   ```bash
   git add phase1/weeks/week9/
   git commit -m "Week 9 COMPLETE: QEMU integration validated"
   git push origin claude/fix-todo-mi2jkb61w34ginf3-01EetfAzrBvubbfquzVZPsU9
   ```

3. **Begin Week 10:**
   - Multi-agent architecture
   - Shared context memory pool
   - Agent coordination testing

---

## What We Built for Week 9

```
phase1/
├── weeks/week9/
│   ├── SETUP_GUIDE.md              # Detailed setup instructions
│   ├── test_week9_integration.py   # Integration test (Python ↔ seL4)
│   ├── WEEK_9_STATUS.md            # Progress tracking
│   └── (results will be generated)
│
├── scripts/
│   └── launch-jarvis-qemu.sh       # QEMU launcher (automated)
│
└── src/
    ├── cache/          # Decision cache (integrated with seL4) ✅
    ├── ipc/            # Lock-free ring buffer ✅
    ├── sel4/           # seL4 kernel + IPC handler ✅
    ├── ai/             # Python IPC client + query processor ✅
    └── shell/          # Interactive shell ✅
```

---

## Performance Expectations (Week 9 in QEMU)

| Metric | Target | Expected (QEMU) | Notes |
|--------|--------|-----------------|-------|
| Boot time | <60s | ~3-5s | ✅ Well under target |
| IPC latency | <100μs | <200μs | QEMU adds overhead |
| Cache lookup | <0.1ms | <0.2ms | Via IPC |
| Cached query (total) | <2s | <3s | End-to-end |
| Cache hit rate | >80% | 85%+ | Validated Phase 0 |

---

**Document Version:** 1.0 (CLI Quick-Start)
**Last Updated:** November 17, 2025
**Ready to execute via Claude CLI on your local machine!**
