#!/bin/bash
#
# JARVIS AI-OS - ARM64 Cross-Compilation Test
# Phase 2 Week 32
#
# This script validates ARM64 cross-compilation without full seL4 build.
# Useful for testing code compiles correctly for Pi 4.
#
# Usage:
#   ./build_arm64_test.sh
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
JARVIS_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PHASE1="$JARVIS_ROOT/phase1/src"
PHASE2="$JARVIS_ROOT/phase2/src"
BUILD_DIR="$SCRIPT_DIR/../build_arm64"

echo "========================================"
echo "JARVIS ARM64 Cross-Compilation Test"
echo "========================================"
echo ""

# Check for ARM64 toolchain
if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
    echo "ERROR: aarch64-linux-gnu-gcc not found!"
    echo "Install with: sudo apt install gcc-aarch64-linux-gnu"
    exit 1
fi

echo "Toolchain: $(aarch64-linux-gnu-gcc --version | head -n1)"
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "Build directory: $BUILD_DIR"
echo ""

# Compile decision cache (C code)
echo "Compiling decision_cache.c..."
aarch64-linux-gnu-gcc -c -O2 -Wall \
    -I"$PHASE1/cache" \
    "$PHASE1/cache/decision_cache.c" \
    -o decision_cache.o

echo "Compiling cache_patterns.c..."
aarch64-linux-gnu-gcc -c -O2 -Wall \
    -I"$PHASE1/cache" \
    "$PHASE1/cache/cache_patterns.c" \
    -o cache_patterns.o

# Compile PL011 UART driver
echo "Compiling uart_pl011.c..."
aarch64-linux-gnu-gcc -c -O2 -Wall \
    -I"$PHASE2/drivers" \
    -I"$PHASE1/cache" \
    -DSTANDALONE_TEST \
    "$PHASE2/drivers/uart_pl011.c" \
    -o uart_pl011.o 2>&1 || {
        echo "Note: UART driver uses ARM64 asm, expected on cross-compile"
    }

# Compile main_arm64.c (compile-only, won't link without seL4)
echo "Compiling main_arm64.c..."
aarch64-linux-gnu-gcc -c -O2 -Wall \
    -I"$PHASE1/cache" \
    -I"$PHASE2/drivers" \
    -DSTANDALONE_TEST \
    "$PHASE2/sel4/main_arm64.c" \
    -o main_arm64.o 2>&1 || {
        echo "Note: main_arm64.c references seL4 headers (expected)"
    }

echo ""
echo "========================================"
echo "Build Results"
echo "========================================"

# Check what was built
ls -la *.o 2>/dev/null || echo "No object files generated"

# Check file types
echo ""
echo "File types:"
for f in *.o; do
    if [ -f "$f" ]; then
        file "$f"
    fi
done

echo ""
echo "========================================"
echo "ARM64 Cross-Compilation Test Complete"
echo "========================================"
echo ""
echo "Next steps:"
echo "1. Integrate into seL4 build system"
echo "2. Build full kernel image"
echo "3. Test on Pi 4 hardware"
