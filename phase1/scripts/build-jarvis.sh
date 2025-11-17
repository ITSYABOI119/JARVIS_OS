#!/bin/bash
# build-jarvis.sh - Build JARVIS seL4 application
#
# This script builds the custom JARVIS seL4 project (not the tutorial)
#
# Usage: ./build-jarvis.sh [clean]

set -e

# Configuration
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SEL4_ROOT="$HOME/jarvis-phase1"
BUILD_DIR="$SEL4_ROOT/build-jarvis"
SOURCE_DIR="$PROJECT_ROOT/phase1/src/sel4"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}JARVIS AI-OS Phase 1 - Build Script${NC}"
echo -e "${GREEN}=====================================${NC}"
echo ""

# Check if source directory exists
if [ ! -d "$SOURCE_DIR" ]; then
    echo -e "${RED}Error: Source directory not found: $SOURCE_DIR${NC}"
    exit 1
fi

# Check if seL4 tutorials are downloaded
if [ ! -d "$SEL4_ROOT/kernel" ]; then
    echo -e "${RED}Error: seL4 not found at $SEL4_ROOT${NC}"
    echo "Please run: cd ~ && mkdir jarvis-phase1 && cd jarvis-phase1"
    echo "Then: repo init -u https://github.com/seL4/sel4-tutorials-manifest && repo sync"
    exit 1
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
echo "Source directory: $SOURCE_DIR"
echo ""

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo -e "${GREEN}Configuring CMake...${NC}"
cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$SEL4_ROOT/kernel/gcc.cmake" \
    -DPLATFORM=pc99 \
    -DKernelPlatform=x86_64 \
    -DTUT_BOARD=pc \
    -DTUT_ARCH=x86_64 \
    "$SOURCE_DIR"

echo ""
echo -e "${GREEN}Building JARVIS...${NC}"
ninja

echo ""
echo -e "${GREEN}Build complete!${NC}"
echo ""
echo "Executable: $BUILD_DIR/jarvis-sel4"
echo ""
echo "To run in QEMU:"
echo "  cd $BUILD_DIR"
echo "  ./simulate"
echo ""
echo "Or manually:"
echo "  qemu-system-x86_64 -m 8G -smp 4 -serial stdio -nographic -kernel images/jarvis-sel4-image-x86_64-pc99"
echo ""
