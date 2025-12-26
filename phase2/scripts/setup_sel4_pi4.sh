#!/bin/bash
# seL4 Pi 4 Build Setup Script
# Phase 2 Week 31 Pre-hardware Prep

set -e

# Add repo to path
export PATH="$HOME/.local/bin:$PATH"

echo "=== seL4 Pi 4 Build Setup ==="

# Create working directory
mkdir -p ~/sel4-pi4
cd ~/sel4-pi4

# Check if already initialized
if [ -d ".repo" ]; then
    echo "Repository already initialized"
    echo "Running repo sync..."
    repo sync -j4 2>&1 | tail -20
else
    echo "Initializing seL4 tutorials manifest..."
    repo init -u https://github.com/seL4/sel4-tutorials-manifest.git
    echo "Syncing repositories..."
    repo sync -j4 2>&1 | tail -20
fi

echo ""
echo "=== Checking for Pi 4 support ==="

# Check platforms
if [ -d "kernel/configs" ]; then
    echo "Available kernel platforms:"
    ls kernel/configs/ | grep -i rpi || echo "No rpi configs found in kernel/configs"
fi

if [ -f "kernel/configs/bcm2711.cmake" ] || [ -f "kernel/configs/rpi4.cmake" ]; then
    echo "Pi 4 config found!"
else
    echo "Looking for Pi 4 config files..."
    find . -name "*.cmake" -exec grep -l -i "rpi4\|bcm2711\|raspberrypi" {} \; 2>/dev/null | head -5
fi

echo ""
echo "=== Build Environment Check ==="
echo "ARM64 compiler: $(which aarch64-linux-gnu-gcc 2>/dev/null || echo 'NOT FOUND')"
echo "CMake: $(which cmake 2>/dev/null || echo 'NOT FOUND')"
echo "Ninja: $(which ninja 2>/dev/null || echo 'NOT FOUND')"
echo "Python: $(which python3 2>/dev/null || echo 'NOT FOUND')"

echo ""
echo "Setup complete. Directory: ~/sel4-pi4"
echo "Next: Try building with:"
echo "  cd ~/sel4-pi4"
echo "  mkdir build && cd build"
echo "  ../init-build.sh -DPLATFORM=rpi4 -DAARCH64=1"
echo "  ninja"
