#!/bin/bash
# Quick QEMU boot for JARVIS seL4 x86-64
# Usage: bash phase3/scripts/qemu_test.sh [/path/to/model.gguf]
set -euo pipefail

BUILD="${SEL4_BUILD_DIR:-$HOME/sel4-x86/jbuild}"
KERNEL="$BUILD/images/kernel-x86_64-pc99"
ROOTSERVER="$BUILD/images/sel4test-driver-image-x86_64-pc99"

if [ ! -f "$KERNEL" ] || [ ! -f "$ROOTSERVER" ]; then
    echo "ERROR: Build images not found in $BUILD/images/"
    echo "Run build_jarvis_x86.sh first."
    exit 1
fi

MODEL="${1:-}"
INITRD="$ROOTSERVER"
if [ -n "$MODEL" ] && [ -f "$MODEL" ]; then
    INITRD="$ROOTSERVER,$MODEL"
    echo "Loading model: $MODEL ($(du -h "$MODEL" | cut -f1))"
else
    echo "No model specified — Process B will idle"
fi

echo "Booting JARVIS seL4 x86-64 in QEMU..."
echo "Exit: Ctrl-A then X"
echo ""

qemu-system-x86_64 \
    -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
    -nographic -serial mon:stdio -m 8G \
    -kernel "$KERNEL" \
    -initrd "$INITRD"
