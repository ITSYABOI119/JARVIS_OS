#!/bin/bash
# Build JARVIS kernel for Pi 4 and copy to firmware directory
# This script automates the build process

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}JARVIS Pi 4 Kernel Build${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Paths
JARVIS_ROOT="/mnt/c/Users/jluca/Documents/JARVIS_OS"
FIRMWARE_DIR="$JARVIS_ROOT/phase2/firmware"
SEL4_WORKSPACE="$HOME/sel4-workspace"
JARVIS_PROJECT="$SEL4_WORKSPACE/projects/jarvis-sel4"
BUILD_DIR="$SEL4_WORKSPACE/rpi4_jarvis"
RPI4_MEMORY="8192"
IMAGE_START_ADDR="0x80000"

# Check if TII workspace exists
if [ ! -d "$SEL4_WORKSPACE" ]; then
    echo -e "${RED}Error: TII seL4 workspace not found at $SEL4_WORKSPACE${NC}"
    echo ""
    echo "Please set up TII seL4 build system first:"
    echo "  cd ~"
    echo "  mkdir sel4-workspace && cd sel4-workspace"
    echo "  repo init -u https://github.com/tiiuae/tii_sel4_manifest.git -b tii/development"
    echo "  repo sync"
    exit 1
fi

# Check if JARVIS project exists, create if not
if [ ! -d "$JARVIS_PROJECT" ]; then
    echo -e "${YELLOW}Creating JARVIS project structure...${NC}"
    mkdir -p "$JARVIS_PROJECT/src"
    
    # Copy JARVIS sources
    echo "Copying JARVIS sources..."
    cp -r "$JARVIS_ROOT/phase1/src/cache" "$JARVIS_PROJECT/src/"
    cp -r "$JARVIS_ROOT/phase1/src/ipc" "$JARVIS_PROJECT/src/"
    cp -r "$JARVIS_ROOT/phase2/src/drivers" "$JARVIS_PROJECT/src/"
    cp -r "$JARVIS_ROOT/phase2/src/ipc" "$JARVIS_PROJECT/src/ipc_phase2"
    cp "$JARVIS_ROOT/phase2/src/sel4/main_arm64.c" "$JARVIS_PROJECT/src/main.c"
    
    # Copy CMakeLists.txt
    if [ -f "$JARVIS_ROOT/phase2/src/jarvis-sel4-cmake/CMakeLists.txt" ]; then
        cp "$JARVIS_ROOT/phase2/src/jarvis-sel4-cmake/CMakeLists.txt" "$JARVIS_PROJECT/"
    else
        echo -e "${YELLOW}Warning: CMakeLists.txt not found, you may need to create it${NC}"
    fi
    
    echo -e "${GREEN}✓ JARVIS project created${NC}"
fi

# Always sync latest JARVIS sources (avoid stale main.c/drivers)
echo -e "${GREEN}[0/3] Syncing JARVIS sources...${NC}"
mkdir -p "$JARVIS_PROJECT/src"
rm -rf "$JARVIS_PROJECT/src/cache" \
       "$JARVIS_PROJECT/src/ipc" \
       "$JARVIS_PROJECT/src/ipc_phase2" \
       "$JARVIS_PROJECT/src/drivers" \
       "$JARVIS_PROJECT/src/boot"
cp -r "$JARVIS_ROOT/phase1/src/cache" "$JARVIS_PROJECT/src/"
cp -r "$JARVIS_ROOT/phase1/src/ipc" "$JARVIS_PROJECT/src/"
cp -r "$JARVIS_ROOT/phase2/src/drivers" "$JARVIS_PROJECT/src/"
cp -r "$JARVIS_ROOT/phase2/src/boot" "$JARVIS_PROJECT/src/"
cp -r "$JARVIS_ROOT/phase2/src/ipc" "$JARVIS_PROJECT/src/ipc_phase2"
cp "$JARVIS_ROOT/phase2/src/sel4/main_arm64.c" "$JARVIS_PROJECT/src/main.c"
if [ -f "$JARVIS_ROOT/phase2/src/jarvis-sel4-cmake/CMakeLists.txt" ]; then
    cp "$JARVIS_ROOT/phase2/src/jarvis-sel4-cmake/CMakeLists.txt" "$JARVIS_PROJECT/"
else
    echo -e "${YELLOW}Warning: CMakeLists.txt not found in repo, using existing project copy${NC}"
fi

# Create build directory
echo -e "${GREEN}[1/3] Configuring build...${NC}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure CMake
cmake -G Ninja \
    -DCROSS_COMPILER_PREFIX=aarch64-linux-gnu- \
    -DKernelPlatform=bcm2711 \
    -DKernelSel4Arch=aarch64 \
    -DKernelArmPlatform=bcm2711 \
    -DRPI4_MEMORY="$RPI4_MEMORY" \
    -DIMAGE_START_ADDR="$IMAGE_START_ADDR" \
    -DElfloaderImage=binary \
    -DKernelDebugBuild=ON \
    -DKernelPrinting=ON \
    "$JARVIS_PROJECT" 2>&1 | head -20

if [ ${PIPESTATUS[0]} -ne 0 ]; then
    echo -e "${RED}Error: CMake configuration failed${NC}"
    echo "Check that TII seL4 build system is properly set up"
    exit 1
fi

# Build
echo -e "${GREEN}[2/3] Building kernel (this may take a few minutes)...${NC}"
ninja

if [ $? -ne 0 ]; then
    echo -e "${RED}Error: Build failed${NC}"
    exit 1
fi

# Find kernel image (prefer raw driver image for U-Boot go)
KERNEL_IMG=""
KERNEL_IMG=$(ls images/*-driver-image-arm-bcm2711 2>/dev/null | head -1)
if [ -n "$KERNEL_IMG" ]; then
    :
elif [ -f "images/jarvis-sel4-image-arm-bcm2711" ]; then
    KERNEL_IMG="images/jarvis-sel4-image-arm-bcm2711"
elif [ -f "images/capdl-loader-image-arm-bcm2711" ]; then
    KERNEL_IMG="images/capdl-loader-image-arm-bcm2711"
elif [ -f "images/kernel8.img" ]; then
    KERNEL_IMG="images/kernel8.img"
elif [ -f "images/kernel.elf" ]; then
    KERNEL_IMG="images/kernel.elf"
else
    echo -e "${YELLOW}Searching for kernel image...${NC}"
    KERNEL_IMG=$(find images -maxdepth 1 -type f \( -name "*-image-arm-bcm2711" -o -name "kernel8.img" -o -name "kernel.elf" \) | head -1)
    if [ -z "$KERNEL_IMG" ]; then
        echo -e "${RED}Error: Kernel image not found${NC}"
        echo "Build completed but kernel image not found in expected location"
        exit 1
    fi
fi

echo -e "${GREEN}[3/3] Copying kernel to firmware directory...${NC}"
echo "Source: $BUILD_DIR/$KERNEL_IMG"
ls -lh "$BUILD_DIR/$KERNEL_IMG"

# Copy to firmware directory
cp "$BUILD_DIR/$KERNEL_IMG" "$FIRMWARE_DIR/kernel8.img"
RAW_NAME="$(basename "$KERNEL_IMG")"
if [ "$RAW_NAME" != "kernel8.img" ]; then
    cp "$BUILD_DIR/$KERNEL_IMG" "$FIRMWARE_DIR/$RAW_NAME"
fi

# Always keep the ELF image in firmware if it exists
if [ -f "$BUILD_DIR/images/jarvis-sel4-image-arm-bcm2711" ]; then
    cp "$BUILD_DIR/images/jarvis-sel4-image-arm-bcm2711" \
       "$FIRMWARE_DIR/jarvis-sel4-image-arm-bcm2711"
fi
if [ -f "$FIRMWARE_DIR/jarvis-sel4-image-arm-bcm2711" ] && [ ! -f "$FIRMWARE_DIR/jarvis-sel4-driver-image-arm-bcm2711" ]; then
    cp "$FIRMWARE_DIR/jarvis-sel4-image-arm-bcm2711" \
       "$FIRMWARE_DIR/jarvis-sel4-driver-image-arm-bcm2711"
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
echo "  1. Copy files to SD card:"
echo "     cd $JARVIS_ROOT/phase2"
echo "     ./copy_to_sd.bat D:"
echo ""
echo "  2. Connect UART serial adapter (GPIO14/15)"
echo "  3. Open serial console (115200 baud, 8N1)"
echo "  4. Power on Pi 4"
echo ""
