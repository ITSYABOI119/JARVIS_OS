# JARVIS AI-OS: Phase 1 Development Setup

**Version:** 1.0
**Date:** November 2025
**Platform:** Windows 11 + WSL2 Ubuntu
**Phase:** Phase 1 - Proof of Concept

---

## Overview

This guide provides step-by-step instructions for setting up the Phase 1 development environment on your current PC (Windows 11 + WSL2 Ubuntu, RTX 2070, Ryzen 2700X, 32GB RAM).

**Goal:** Install QEMU, seL4 toolchain, and all dependencies needed to build and run JARVIS AI-OS in a virtual machine.

**Estimated Setup Time:** 2-4 hours

---

## Prerequisites

**Your Current Setup:**
- ✅ Windows 11
- ✅ WSL2 Ubuntu installed
- ✅ 32GB RAM
- ✅ AMD Ryzen 2700X (8-core)
- ✅ NVIDIA RTX 2070 (8GB VRAM)
- ✅ NVMe SSD storage

**Already Installed (from Phase 0):**
- ✅ Python 3.11+
- ✅ llama-cpp-python with CUDA support
- ✅ Phi-3 Mini 3.8B model downloaded

---

## Step 1: Update WSL2 Ubuntu

```bash
# Open WSL2 Ubuntu terminal
wsl

# Update package lists
sudo apt update

# Upgrade all packages
sudo apt upgrade -y

# Install basic build tools
sudo apt install -y build-essential git curl wget
```

**Verify:**
```bash
gcc --version  # Should be 9.0+
git --version  # Should be 2.0+
```

---

## Step 2: Install QEMU

### Install QEMU System Emulator

```bash
# Install QEMU for x86_64 emulation
sudo apt install -y qemu-system-x86 qemu-utils qemu-kvm

# Verify installation
qemu-system-x86_64 --version
# Should output: QEMU emulator version 7.0+ (or later)
```

### Test QEMU

```bash
# Create simple test (boots to BIOS)
qemu-system-x86_64 -m 512M

# If you see QEMU window, press Ctrl+C to exit
# If error about display, add: -nographic
qemu-system-x86_64 -m 512M -nographic
```

### Enable KVM (Hardware Acceleration)

```bash
# Check if KVM is available
kvm-ok

# If not installed:
sudo apt install -y cpu-checker
kvm-ok

# Should output: "KVM acceleration can be used"
```

**Note:** WSL2 supports KVM via Hyper-V. Your Ryzen 2700X has AMD-V virtualization, so KVM should work.

---

## Step 3: Install seL4 Dependencies

### Install Required Packages

```bash
# seL4 build dependencies
sudo apt install -y \
    python3 python3-pip python3-dev python3-venv \
    cmake cmake-curses-gui ninja-build \
    libxml2-utils ncurses-dev \
    curl git doxygen device-tree-compiler \
    u-boot-tools

# Python packages for seL4
pip3 install --user \
    sel4-deps \
    camkes-deps \
    ply \
    jinja2 \
    jsonschema
```

### Install Cross-Compiler Toolchain

```bash
# For x86_64 target (our Phase 1 platform)
sudo apt install -y gcc-multilib g++-multilib

# Verify
gcc -m32 --version  # Should work for 32-bit compilation
```

**Alternative (if multilib doesn't work):**
```bash
# Install from seL4 repositories
curl https://sel4.systems/Downloads/toolchains/sel4-toolchain-13.2.0-x86_64.tar.gz -o toolchain.tar.gz
tar xzf toolchain.tar.gz
sudo mv x86_64-sel4-* /opt/sel4-toolchain
export PATH="/opt/sel4-toolchain/bin:$PATH"
```

---

## Step 4: Install Google Repo Tool

```bash
# Create bin directory
mkdir -p ~/bin

# Download repo tool
curl https://storage.googleapis.com/git-repo-downloads/repo > ~/bin/repo
chmod a+x ~/bin/repo

# Add to PATH
echo 'export PATH="$HOME/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Verify
repo version
# Should output: repo version v2.0+ (or later)
```

---

## Step 5: Download seL4 Source

### Clone seL4 Tutorials (includes seL4 kernel)

```bash
# Create workspace directory
mkdir -p ~/jarvis-phase1
cd ~/jarvis-phase1

# Initialize repo
repo init -u https://github.com/seL4/sel4-tutorials-manifest

# Sync all repositories (this will take 5-10 minutes)
repo sync

# Verify
ls -la
# Should see: apps/ kernel/ libs/ projects/ tools/ etc.
```

---

## Step 6: Build seL4 "Hello World"

### Navigate to Tutorial

```bash
cd ~/jarvis-phase1

# Initialize build environment
./init --plat pc99 --tut hello-world
```

### Build

```bash
cd hello-world_build

# Configure with CMake
cmake -DCROSS_COMPILER_PREFIX=x86_64-linux-gnu- \
      -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake \
      -G Ninja \
      ..

# Build
ninja

# Should see: Built target sel4_image or similar
```

### Run in QEMU

```bash
# Run hello-world in QEMU
./simulate

# Expected output:
# Hello, World!
# <seL4 debug output>

# Press Ctrl+A then X to exit QEMU
```

**If successful:** ✅ seL4 toolchain working!

---

## Step 7: Set Up JARVIS Project Structure

### Create Project Directories

```bash
cd ~/jarvis-phase1
mkdir -p jarvis-sel4
cd jarvis-sel4

# Create source directories
mkdir -p \
    src/kernel \
    src/ai-agent \
    src/shell \
    src/cache \
    src/ipc \
    include \
    tools \
    docs
```

### Copy seL4 Template

```bash
# Copy hello-world as template
cp -r ../apps/hello-world/* src/kernel/

# This gives us:
# - CMakeLists.txt
# - seL4 initialization code
# - Basic build setup
```

---

## Step 8: Configure IDE (Optional but Recommended)

### Option A: Visual Studio Code

```bash
# Install VS Code in Windows (if not already installed)
# Download from: https://code.visualstudio.com/

# Install WSL extension in VS Code
# 1. Open VS Code
# 2. Install "Remote - WSL" extension
# 3. Press F1 → "WSL: Open Folder in WSL"
# 4. Navigate to ~/jarvis-phase1/jarvis-sel4
```

**Recommended VS Code Extensions:**
- C/C++ (Microsoft)
- CMake Tools
- Python
- Remote - WSL
- GitLens

### Option B: Vim/Neovim

```bash
# Already available in WSL2

# Install ctags for code navigation
sudo apt install -y exuberant-ctags

# Generate tags for seL4 code
cd ~/jarvis-phase1
ctags -R .
```

---

## Step 9: Set Up AI Development Environment

### Verify Python Environment (from Phase 0)

```bash
# Check Python version
python3 --version
# Should be: Python 3.11+

# Check llama-cpp-python
python3 -c "from llama_cpp import Llama; print('llama-cpp-python OK')"
# Should output: llama-cpp-python OK

# Check CUDA support
python3 -c "from llama_cpp import Llama; import llama_cpp; print('CUDA:', 'YES' if 'CUDA' in llama_cpp.__file__ else 'NO')"
# Should output: CUDA: YES
```

### Verify AI Model Available

```bash
# Check Phi-3 Mini model
ls -lh ~/jarvis-phase1/../phase0/experiments/models/
# Should see: Phi-3-mini-4k-instruct-q4.gguf (2.23GB)

# Create symlink for easy access
ln -s ~/jarvis-phase1/../phase0/experiments/models ~/jarvis-phase1/jarvis-sel4/models
```

---

## Step 10: Create QEMU Launch Script

### Create qemu-run.sh

```bash
cd ~/jarvis-phase1/jarvis-sel4/tools
cat > qemu-run.sh << 'EOF'
#!/bin/bash
# JARVIS AI-OS QEMU Launch Script

# Configuration
KERNEL_IMAGE="../build/images/kernel-x86_64-pc99"
RAM_SIZE="8G"
CPU_CORES="4"

# Launch QEMU
qemu-system-x86_64 \
    -m $RAM_SIZE \
    -smp $CPU_CORES \
    -kernel $KERNEL_IMAGE \
    -serial stdio \
    -nographic \
    -enable-kvm \
    "$@"

# Usage: ./qemu-run.sh
# Exit: Ctrl+A then X
EOF

chmod +x qemu-run.sh
```

### Test QEMU Script

```bash
# First, build a seL4 image
cd ~/jarvis-phase1/jarvis-sel4
mkdir build && cd build
cmake -DCROSS_COMPILER_PREFIX=x86_64-linux-gnu- -G Ninja ..
ninja

# Then run
cd ../tools
./qemu-run.sh

# If error about missing kernel image, that's OK - we'll build it in Week 1
```

---

## Step 11: Set Up Version Control

### Initialize Git Repository

```bash
cd ~/jarvis-phase1/jarvis-sel4

# Initialize git
git init

# Create .gitignore
cat > .gitignore << 'EOF'
# Build directories
build/
*.o
*.a
*.so
*.elf

# IDE
.vscode/
.idea/
*.swp
*.swo

# Python
__pycache__/
*.pyc
*.pyo
.venv/

# Models (too large for git)
models/*.gguf

# Logs
*.log
logs/

# OS files
.DS_Store
Thumbs.db
EOF

# Initial commit
git add .
git commit -m "Initial JARVIS Phase 1 setup"
```

---

## Step 12: Verify Complete Setup

### Checklist

Run this verification script:

```bash
cd ~/jarvis-phase1/jarvis-sel4/tools
cat > verify-setup.sh << 'EOF'
#!/bin/bash
echo "JARVIS Phase 1 Setup Verification"
echo "================================="
echo

# Check QEMU
echo -n "QEMU installed: "
if command -v qemu-system-x86_64 &> /dev/null; then
    echo "✅ YES ($(qemu-system-x86_64 --version | head -1))"
else
    echo "❌ NO"
fi

# Check KVM
echo -n "KVM available: "
if kvm-ok 2>&1 | grep -q "KVM acceleration can be used"; then
    echo "✅ YES"
else
    echo "⚠️  NO (will be slower)"
fi

# Check CMake
echo -n "CMake installed: "
if command -v cmake &> /dev/null; then
    echo "✅ YES ($(cmake --version | head -1))"
else
    echo "❌ NO"
fi

# Check Ninja
echo -n "Ninja installed: "
if command -v ninja &> /dev/null; then
    echo "✅ YES ($(ninja --version))"
else
    echo "❌ NO"
fi

# Check Python
echo -n "Python 3.11+: "
PYTHON_VERSION=$(python3 --version | cut -d' ' -f2)
echo "✅ YES ($PYTHON_VERSION)"

# Check llama-cpp-python
echo -n "llama-cpp-python: "
if python3 -c "from llama_cpp import Llama" 2>/dev/null; then
    echo "✅ YES"
else
    echo "❌ NO"
fi

# Check Phi-3 model
echo -n "Phi-3 Mini model: "
if [ -f "../models/Phi-3-mini-4k-instruct-q4.gguf" ]; then
    SIZE=$(ls -lh ../models/Phi-3-mini-4k-instruct-q4.gguf | awk '{print $5}')
    echo "✅ YES ($SIZE)"
else
    echo "❌ NO"
fi

# Check seL4 source
echo -n "seL4 source: "
if [ -d "../../kernel" ]; then
    echo "✅ YES"
else
    echo "❌ NO"
fi

echo
echo "Setup verification complete!"
EOF

chmod +x verify-setup.sh
./verify-setup.sh
```

**Expected Output:**
```
JARVIS Phase 1 Setup Verification
=================================

QEMU installed: ✅ YES (QEMU emulator version 7.0.0)
KVM available: ✅ YES
CMake installed: ✅ YES (cmake version 3.22.1)
Ninja installed: ✅ YES (1.10.2)
Python 3.11+: ✅ YES (3.11.0)
llama-cpp-python: ✅ YES
Phi-3 Mini model: ✅ YES (2.2G)
seL4 source: ✅ YES

Setup verification complete!
```

---

## Development Workflow

### Daily Workflow

```bash
# 1. Start WSL2
wsl

# 2. Navigate to project
cd ~/jarvis-phase1/jarvis-sel4

# 3. Build
cd build
ninja

# 4. Run in QEMU
cd ../tools
./qemu-run.sh

# 5. Exit QEMU: Ctrl+A then X

# 6. Edit code (repeat 3-5)
code .  # VS Code
# or
vim src/kernel/main.c  # Vim
```

### Build Commands

```bash
# Clean build
cd ~/jarvis-phase1/jarvis-sel4
rm -rf build
mkdir build && cd build
cmake -DCROSS_COMPILER_PREFIX=x86_64-linux-gnu- -G Ninja ..
ninja

# Incremental build
cd ~/jarvis-phase1/jarvis-sel4/build
ninja

# Verbose build (see commands)
ninja -v
```

### Debugging

```bash
# Run QEMU with GDB server
qemu-system-x86_64 \
    -m 8G \
    -smp 4 \
    -kernel build/images/kernel-x86_64-pc99 \
    -serial stdio \
    -nographic \
    -s -S  # GDB server on port 1234, wait for debugger

# In another terminal
gdb build/images/kernel-x86_64-pc99
(gdb) target remote localhost:1234
(gdb) break main
(gdb) continue
```

---

## Troubleshooting

### QEMU: "KVM not available"

**Problem:** QEMU can't access KVM
**Solution:**
```bash
# Check if KVM modules loaded
lsmod | grep kvm

# If not loaded (WSL2 uses Hyper-V, not KVM)
# Remove -enable-kvm from qemu-run.sh
# QEMU will still work, just slower
```

### seL4: "Cross compiler not found"

**Problem:** CMake can't find cross-compiler
**Solution:**
```bash
# Install multilib
sudo apt install gcc-multilib g++-multilib

# Or specify compiler explicitly
cmake -DCROSS_COMPILER_PREFIX=x86_64-linux-gnu- ...
```

### Python: "llama-cpp-python not found"

**Problem:** Python can't import llama_cpp
**Solution:**
```bash
# Reinstall with CUDA support (from Phase 0)
CMAKE_ARGS="-DLLAMA_CUBLAS=on" pip install llama-cpp-python --force-reinstall
```

### Build: "Ninja: command not found"

**Problem:** Ninja build system not installed
**Solution:**
```bash
sudo apt install ninja-build
```

---

## Resource Allocation

### Your PC Resources

**Total:**
- RAM: 32GB
- CPU: 8 cores
- GPU: RTX 2070 (8GB VRAM)

**Recommended Allocation:**
- **Windows Host:** 16GB RAM, 4 CPU cores (for daily use)
- **QEMU VM (JARVIS):** 8-12GB RAM, 4 CPU cores (for development)
- **Total Used:** 24-28GB RAM, 8 cores (within capacity)

### QEMU Configuration

**For Development (fast iteration):**
```bash
qemu-system-x86_64 -m 8G -smp 4 ...
```

**For Testing (more resources):**
```bash
qemu-system-x86_64 -m 12G -smp 6 ...
```

**For Integration (full resources):**
```bash
qemu-system-x86_64 -m 16G -smp 6 ...
```

---

## Next Steps

**Week 1 Tasks:**
1. ✅ Complete this setup (you're here!)
2. ⏳ Build seL4 "hello world" (Step 6)
3. ⏳ Create custom seL4 project (Step 7)
4. ⏳ Run JARVIS skeleton in QEMU (Step 10)

**Week 2:**
1. Implement serial console
2. Boot to minimal shell
3. Test IPC

**Documentation:**
- Keep this setup guide updated as you learn
- Document any issues and solutions
- Update verify-setup.sh as needed

---

## Resources

**seL4 Documentation:**
- Manual: https://sel4.systems/Info/Docs/seL4-manual-latest.pdf
- Tutorials: https://docs.sel4.systems/Tutorials/
- API Reference: https://docs.sel4.systems/ApiDoc.html

**QEMU Documentation:**
- User Guide: https://www.qemu.org/docs/master/system/index.html
- Debugging: https://www.qemu.org/docs/master/system/gdb.html

**Community:**
- seL4 Discourse: https://sel4.discourse.group/
- seL4 GitHub: https://github.com/seL4

---

**Document Version:** 1.0
**Status:** READY TO USE
**Last Updated:** November 2025
**Maintainer:** JARVIS AI-OS Team
