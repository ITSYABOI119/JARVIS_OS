# Phase 1 Week 1 Setup Status

**Date:** November 15, 2025
**Status:** IN PROGRESS
**Time Spent:** 1 hour

---

## Environment Check

### Already Installed ✅
- **GCC:** 13.3.0 (Ubuntu) ✅
- **Git:** 2.43.0 ✅
- **Python3:** Installed ✅
- **WSL2:** Ubuntu 24.04 ✅

### Needs Installation ⚠️
- **CMake:** Not installed
- **Ninja:** Not installed
- **QEMU:** Not installed
- **seL4 dependencies:** Not installed
- **Google repo tool:** Not installed

---

## Installation Issues Encountered

### Problem: `apt install` Commands Timing Out

**Commands that timed out:**
```bash
sudo apt update         # Timed out after 2+ minutes
sudo apt upgrade -y     # Timed out after 5+ minutes
sudo apt install -y cmake ninja-build python3-pip qemu-system-x86 qemu-utils # Timed out after 5+ minutes
```

**Likely causes:**
1. Slow network connection to Ubuntu package mirrors
2. Large number of packages to update
3. Apt lock held by another process

---

## Manual Installation Steps (For User)

Since automated installation is timing out, please run these commands manually in WSL2:

### Step 1: Update Package Lists
```bash
wsl
sudo apt update
```
*This may take 2-5 minutes. Wait for completion.*

### Step 2: Install seL4 Dependencies
```bash
sudo apt install -y cmake ninja-build python3-pip \
    libxml2-utils ncurses-dev python3-dev python3-pip \
    python3-setuptools device-tree-compiler u-boot-tools \
    qemu-system-x86 qemu-utils gcc-aarch64-linux-gnu \
    gcc-arm-none-eabi gcc-arm-linux-gnueabi binutils-arm-linux-gnueabi
```
*This may take 10-15 minutes on slow connection. Let it run.*

### Step 3: Install Google Repo Tool
```bash
mkdir -p ~/bin
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo
echo 'export PATH="$HOME/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

### Step 4: Verify Installations
```bash
cmake --version       # Should be 3.20+
ninja --version       # Should be 1.10+
qemu-system-x86_64 --version  # Should be 7.0+
repo --version        # Should show repo launcher version
```

---

## Next Steps After Manual Install

Once the above installations complete successfully:

1. Download seL4 tutorials:
   ```bash
   mkdir ~/jarvis-phase1
   cd ~/jarvis-phase1
   repo init -u https://github.com/seL4/sel4-tutorials-manifest
   repo sync
   ```

2. Build hello-world tutorial:
   ```bash
   cd ~/jarvis-phase1
   mkdir build
   cd build
   ../init-build.sh -DTUTORIAL=hello-world
   ninja
   ```

3. Run in QEMU:
   ```bash
   ./simulate
   ```

4. Expected output:
   ```
   Booting all finished, dropped to user space
   Hello, World!
   ```

---

## Estimated Time Remaining

- **Manual installations:** 15-30 minutes (depending on network speed)
- **seL4 download:** 5-10 minutes (repo sync downloads ~500MB)
- **seL4 build:** 5-10 minutes (first build compiles kernel + tutorial)
- **Total:** 25-50 minutes

---

## Fallback Plan

If installation continues to fail:
1. Use Windows native seL4 setup (more complex but documented)
2. Use Docker container with seL4 pre-installed
3. Use GitHub Codespaces (cloud development environment)

---

**Status:** Waiting for manual installation completion
**Blocker:** Network/apt timeout issues
**Next:** Verify installations, then download seL4
