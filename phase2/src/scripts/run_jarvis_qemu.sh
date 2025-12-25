#!/bin/bash
#
# JARVIS AI-OS - QEMU Launch Script with ivshmem
# Phase 2 Week 30
#
# Launches seL4 QEMU with ivshmem device for Python↔seL4 IPC.
#
# Usage: ./run_jarvis_qemu.sh [--build-dir DIR]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JARVIS_ROOT="${SCRIPT_DIR}/../../.."

# Default build directory (seL4 tutorials build)
BUILD_DIR="${BUILD_DIR:-$HOME/jarvis-phase1/hello-worlde_nd1ae4_build}"

# Shared memory configuration
SHM_PATH="/dev/shm/jarvis_ipc"
SHM_SIZE="567296"  # 567KB - must match dual_ring_buffer_t

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [--build-dir DIR]"
            echo ""
            echo "Launches JARVIS seL4 in QEMU with ivshmem shared memory."
            echo ""
            echo "Options:"
            echo "  --build-dir DIR  seL4 build directory (default: ~/jarvis-phase1/hello-worlde_nd1ae4_build)"
            echo ""
            echo "Environment variables:"
            echo "  BUILD_DIR  Same as --build-dir"
            echo "  SHM_PATH   Shared memory path (default: /dev/shm/jarvis_ipc)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "=========================================="
echo "JARVIS AI-OS - QEMU with ivshmem"
echo "=========================================="
echo "Build dir: $BUILD_DIR"
echo "SHM path:  $SHM_PATH"
echo ""

# Check build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "ERROR: Build directory not found: $BUILD_DIR"
    echo ""
    echo "Please build seL4 first or specify --build-dir"
    exit 1
fi

# Check for kernel and image files
KERNEL_IMAGE="$BUILD_DIR/images/kernel-x86_64-pc99"
APP_IMAGE="$BUILD_DIR/images/hello-world-image-x86_64-pc99"

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "ERROR: Kernel not found: $KERNEL_IMAGE"
    exit 1
fi

if [ ! -f "$APP_IMAGE" ]; then
    echo "ERROR: Application image not found: $APP_IMAGE"
    exit 1
fi

# Create shared memory file if it doesn't exist
if [ ! -f "$SHM_PATH" ]; then
    echo "Creating shared memory file..."
    "$SCRIPT_DIR/create_shm.sh" --path "$SHM_PATH" --size "$SHM_SIZE"
fi

# Verify shared memory size
ACTUAL_SIZE=$(stat -c%s "$SHM_PATH" 2>/dev/null || stat -f%z "$SHM_PATH" 2>/dev/null)
if [ "$ACTUAL_SIZE" -ne "$SHM_SIZE" ]; then
    echo "WARNING: Shared memory size mismatch (expected $SHM_SIZE, got $ACTUAL_SIZE)"
    echo "Recreating..."
    "$SCRIPT_DIR/create_shm.sh" --path "$SHM_PATH" --size "$SHM_SIZE"
fi

echo "Starting QEMU with ivshmem..."
echo "  Kernel: $KERNEL_IMAGE"
echo "  Image:  $APP_IMAGE"
echo "  ivshmem: $SHM_PATH (${SHM_SIZE} bytes)"
echo ""
echo "Press Ctrl+A X to exit QEMU"
echo "=========================================="
echo ""

# Launch QEMU with ivshmem device
# Based on seL4 tutorials simulate script + ivshmem additions
exec qemu-system-x86_64 \
    -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
    -nographic \
    -serial mon:stdio \
    -m size=512M \
    -kernel "$KERNEL_IMAGE" \
    -initrd "$APP_IMAGE" \
    -device ivshmem-plain,memdev=shm0 \
    -object memory-backend-file,size=567K,share=on,mem-path="$SHM_PATH",id=shm0
