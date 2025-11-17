#!/bin/bash
# launch-qemu.sh - JARVIS AI-OS QEMU Launcher
#
# Usage: ./launch-qemu.sh [kernel-image]
#
# Launches seL4 kernel in QEMU with appropriate parameters for Phase 1

set -e

# Configuration
QEMU_MEMORY="8G"        # 8GB RAM for VM
QEMU_CPUS="4"           # 4 CPU cores (2 kernel, 2 AI)
QEMU_ARCH="x86_64"
KERNEL_IMAGE="${1:-../build/images/kernel-x86_64-pc99}"

# Color output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}JARVIS AI-OS Phase 1 - QEMU Launcher${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Configuration:"
echo "  Memory:  $QEMU_MEMORY"
echo "  CPUs:    $QEMU_CPUS"
echo "  Arch:    $QEMU_ARCH"
echo "  Kernel:  $KERNEL_IMAGE"
echo ""

# Check if kernel image exists
if [ ! -f "$KERNEL_IMAGE" ]; then
    echo -e "${YELLOW}Warning: Kernel image not found at $KERNEL_IMAGE${NC}"
    echo "Have you built the kernel yet?"
    echo ""
    echo "To build:"
    echo "  cd ../build"
    echo "  cmake -G Ninja ../src"
    echo "  ninja"
    echo ""
    exit 1
fi

echo -e "${GREEN}Starting QEMU...${NC}"
echo ""

# Launch QEMU
qemu-system-x86_64 \
    -m $QEMU_MEMORY \
    -smp $QEMU_CPUS \
    -serial stdio \
    -nographic \
    -kernel $KERNEL_IMAGE \
    -cpu Broadwell \
    -machine q35 \
    -enable-kvm || true  # Try KVM, fallback to TCG if unavailable

echo ""
echo -e "${GREEN}QEMU exited${NC}"
