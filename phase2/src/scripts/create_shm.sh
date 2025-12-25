#!/bin/bash
#
# JARVIS AI-OS - Shared Memory Setup Script
# Phase 2 Week 30
#
# Creates the shared memory file for JARVIS IPC before QEMU starts.
# This file is mapped by both Python (host) and seL4 (QEMU guest via ivshmem).
#
# Usage: ./create_shm.sh [--size SIZE] [--path PATH]
#

set -e

# Default values
SHM_PATH="${SHM_PATH:-/dev/shm/jarvis_ipc}"
SHM_SIZE="${SHM_SIZE:-567296}"  # 567KB for dual_ring_buffer_t

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --size)
            SHM_SIZE="$2"
            shift 2
            ;;
        --path)
            SHM_PATH="$2"
            shift 2
            ;;
        --help)
            echo "Usage: $0 [--size SIZE] [--path PATH]"
            echo ""
            echo "Creates shared memory file for JARVIS IPC."
            echo ""
            echo "Options:"
            echo "  --size SIZE  Size in bytes (default: 567296)"
            echo "  --path PATH  File path (default: /dev/shm/jarvis_ipc)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "=========================================="
echo "JARVIS IPC Shared Memory Setup"
echo "=========================================="
echo "Path: $SHM_PATH"
echo "Size: $SHM_SIZE bytes ($(($SHM_SIZE / 1024)) KB)"
echo ""

# Remove existing file if present
if [ -f "$SHM_PATH" ]; then
    echo "Removing existing file..."
    rm -f "$SHM_PATH"
fi

# Create file with correct size (zero-initialized)
echo "Creating shared memory file..."
dd if=/dev/zero of="$SHM_PATH" bs=1 count=$SHM_SIZE status=none

# Set permissions (readable/writable by all)
chmod 666 "$SHM_PATH"

# Verify
ACTUAL_SIZE=$(stat -c%s "$SHM_PATH" 2>/dev/null || stat -f%z "$SHM_PATH" 2>/dev/null)
if [ "$ACTUAL_SIZE" -eq "$SHM_SIZE" ]; then
    echo ""
    echo "SUCCESS: Shared memory ready"
    echo "  Path: $SHM_PATH"
    echo "  Size: $ACTUAL_SIZE bytes"
    echo ""
    echo "Next steps:"
    echo "  1. Start QEMU with ivshmem device"
    echo "  2. Run Python IPC client"
    echo "=========================================="
else
    echo "ERROR: Size mismatch (expected $SHM_SIZE, got $ACTUAL_SIZE)"
    exit 1
fi
