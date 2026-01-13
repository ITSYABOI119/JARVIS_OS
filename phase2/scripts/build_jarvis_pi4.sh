#!/bin/bash
# Build JARVIS seL4 kernel for Raspberry Pi 4
# This script builds kernel8.img using TII seL4 build system
#
# Prerequisites:
#   - TII seL4 workspace at ~/sel4-workspace
#   - JARVIS project at ~/sel4-workspace/projects/jarvis-sel4
#   - Cross-compilation toolchain: aarch64-linux-gnu-gcc
#
# Usage: ./build_jarvis_pi4.sh [clean]

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}JARVIS AI-OS - Pi 4 Build Script${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Configuration
SEL4_WORKSPACE="$HOME/sel4-workspace"
JARVIS_PROJECT="$SEL4_WORKSPACE/projects/jarvis-sel4"
BUILD_DIR="$SEL4_WORKSPACE/rpi4_jarvis"
JARVIS_SRC="/mnt/c/Users/jluca/Documents/JARVIS_OS"

# Check if TII workspace exists
if [ ! -d "$SEL4_WORKSPACE" ]; then
    echo -e "${RED}Error: TII seL4 workspace not found at $SEL4_WORKSPACE${NC}"
    echo "Please set up TII seL4 build system first:"
    echo "  cd ~"
    echo "  mkdir sel4-workspace && cd sel4-workspace"
    echo "  repo init -u https://github.com/tiiuae/tii_sel4_manifest.git -b tii/development"
    echo "  repo sync"
    exit 1
fi

# Check if JARVIS project exists
if [ ! -d "$JARVIS_PROJECT" ]; then
    echo -e "${YELLOW}Warning: JARVIS project not found at $JARVIS_PROJECT${NC}"
    echo "Creating project structure..."
    mkdir -p "$JARVIS_PROJECT/src"
    
    # Copy JARVIS sources
    echo "Copying JARVIS sources..."
    cp -r "$JARVIS_SRC/phase1/src/cache" "$JARVIS_PROJECT/src/"
    cp -r "$JARVIS_SRC/phase1/src/ipc" "$JARVIS_PROJECT/src/"
    cp -r "$JARVIS_SRC/phase2/src/drivers" "$JARVIS_PROJECT/src/"
    cp -r "$JARVIS_SRC/phase2/src/ipc_phase2" "$JARVIS_PROJECT/src/"
    cp "$JARVIS_SRC/phase2/src/sel4/main_arm64.c" "$JARVIS_PROJECT/src/main.c"
    
    echo -e "${GREEN}✓ JARVIS sources copied${NC}"
fi

# Clean if requested
if [ "$1" == "clean" ]; then
    echo -e "${YELLOW}Cleaning build directory...${NC}"
    rm -rf "$BUILD_DIR"
    echo "Done."
    exit 0
fi

# Create build directory
echo "Build directory: $BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure build
echo -e "${GREEN}[1/4] Configuring CMake...${NC}"
cmake -G Ninja \
    -DCROSS_COMPILER_PREFIX=aarch64-linux-gnu- \
    -DKernelPlatform=bcm2711 \
    -DKernelSel4Arch=aarch64 \
    -DKernelArmPlatform=bcm2711 \
    "$JARVIS_PROJECT"

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: CMake configuration failed${NC}"
    exit 1
fi

echo -e "${GREEN}[2/4] Building JARVIS kernel...${NC}"
ninja

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Build failed${NC}"
    exit 1
fi

# Find the kernel image
KERNEL_IMG=""
if [ -f "images/kernel8.img" ]; then
    KERNEL_IMG="images/kernel8.img"
elif [ -f "images/kernel.elf" ]; then
    KERNEL_IMG="images/kernel.elf"
elif [ -f "kernel8.img" ]; then
    KERNEL_IMG="kernel8.img"
else
    echo -e "${YELLOW}Warning: kernel image not found in expected location${NC}"
    echo "Searching for kernel files..."
    find . -name "*.img" -o -name "*.elf" | head -5
    exit 1
fi

echo -e "${GREEN}[3/4] Kernel built successfully!${NC}"
echo "Kernel image: $BUILD_DIR/$KERNEL_IMG"
ls -lh "$BUILD_DIR/$KERNEL_IMG"

# Copy to firmware directory
FIRMWARE_DIR="$JARVIS_SRC/phase2/firmware"
echo -e "${GREEN}[4/4] Copying to firmware directory...${NC}"

# Ensure it's named kernel8.img
if [ "$KERNEL_IMG" != "kernel8.img" ]; then
    cp "$BUILD_DIR/$KERNEL_IMG" "$FIRMWARE_DIR/kernel8.img"
else
    cp "$BUILD_DIR/$KERNEL_IMG" "$FIRMWARE_DIR/"
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}✅ BUILD COMPLETE${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Kernel image: $FIRMWARE_DIR/kernel8.img"
ls -lh "$FIRMWARE_DIR/kernel8.img"
echo ""
echo "Next steps:"
echo "  1. Copy files to SD card: cd $JARVIS_SRC/phase2 && ./copy_to_sd.bat D:"
echo "  2. Connect UART serial adapter (GPIO14/15)"
echo "  3. Open serial console (115200 baud, 8N1)"
echo "  4. Power on Pi 4"
echo ""

