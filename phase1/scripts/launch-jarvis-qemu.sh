#!/bin/bash
# JARVIS AI-OS Phase 1 - QEMU Launch Script
# Week 9: Launch seL4 with JARVIS components in QEMU

set -e  # Exit on error

# ============================================================================
# Configuration
# ============================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# seL4 paths (adjust if different)
SEL4_TUTORIALS_DIR="${SEL4_TUTORIALS_DIR:-$HOME/sel4-tutorials}"
SEL4_APP="${SEL4_APP:-hello-world}"
SEL4_BUILD_DIR="${SEL4_TUTORIALS_DIR}/${SEL4_APP}/build"

# QEMU configuration
QEMU_MEMORY="512M"
QEMU_PLATFORM="x86_64-pc99"

# Shared memory for IPC
SHARED_MEMORY_PATH="/dev/shm/jarvis_ipc"
SHARED_MEMORY_SIZE=4096

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# ============================================================================
# Helper Functions
# ============================================================================

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_prereqs() {
    log_info "Checking prerequisites..."

    # Check QEMU
    if ! command -v qemu-system-x86_64 &> /dev/null; then
        log_error "QEMU not found!"
        echo "Install with: sudo apt-get install qemu-system-x86"
        exit 1
    fi

    QEMU_VERSION=$(qemu-system-x86_64 --version | head -1)
    log_info "QEMU: $QEMU_VERSION"

    # Check seL4 build
    if [ ! -d "$SEL4_BUILD_DIR" ]; then
        log_error "seL4 build directory not found: $SEL4_BUILD_DIR"
        echo "Run: cd $SEL4_TUTORIALS_DIR/$SEL4_APP && mkdir build && cd build && ../init-build.sh"
        exit 1
    fi

    # Check seL4 image
    KERNEL_IMAGE="$SEL4_BUILD_DIR/images/kernel-$QEMU_PLATFORM"
    APP_IMAGE="$SEL4_BUILD_DIR/images/${SEL4_APP}-image-$QEMU_PLATFORM"

    if [ ! -f "$KERNEL_IMAGE" ]; then
        log_error "Kernel image not found: $KERNEL_IMAGE"
        echo "Build seL4 first: cd $SEL4_BUILD_DIR && ninja"
        exit 1
    fi

    if [ ! -f "$APP_IMAGE" ]; then
        log_error "Application image not found: $APP_IMAGE"
        echo "Build seL4 first: cd $SEL4_BUILD_DIR && ninja"
        exit 1
    fi

    log_info "seL4 kernel: $KERNEL_IMAGE"
    log_info "seL4 app: $APP_IMAGE"

    # Check shared memory directory
    if [ ! -d "/dev/shm" ]; then
        log_error "/dev/shm not found!"
        echo "Mount tmpfs: sudo mount -t tmpfs -o size=512M tmpfs /dev/shm"
        exit 1
    fi

    log_info "Shared memory: /dev/shm"
}

setup_shared_memory() {
    log_info "Setting up shared memory for IPC..."

    # Remove old shared memory file if exists
    if [ -f "$SHARED_MEMORY_PATH" ]; then
        log_warn "Removing old shared memory file: $SHARED_MEMORY_PATH"
        rm -f "$SHARED_MEMORY_PATH"
    fi

    # Create shared memory file
    dd if=/dev/zero of="$SHARED_MEMORY_PATH" bs=$SHARED_MEMORY_SIZE count=1 2>/dev/null
    chmod 666 "$SHARED_MEMORY_PATH"

    log_info "Created shared memory: $SHARED_MEMORY_PATH ($SHARED_MEMORY_SIZE bytes)"
}

launch_qemu() {
    log_info "Launching seL4 in QEMU..."
    echo
    log_info "=================================="
    log_info "JARVIS AI-OS - seL4 in QEMU"
    log_info "=================================="
    echo
    log_info "Press Ctrl+A then X to exit QEMU"
    echo
    sleep 2

    # Launch QEMU
    qemu-system-x86_64 \
        -m $QEMU_MEMORY \
        -nographic \
        -serial mon:stdio \
        -kernel "$KERNEL_IMAGE" \
        -initrd "$APP_IMAGE"
}

cleanup() {
    log_info "Cleaning up..."

    # Note: We keep shared memory file for Python client
    # Remove it manually if needed: rm /dev/shm/jarvis_ipc
}

# ============================================================================
# Main
# ============================================================================

main() {
    echo
    echo "========================================================================"
    echo "JARVIS AI-OS Phase 1 - Week 9"
    echo "QEMU Launch Script"
    echo "========================================================================"
    echo

    # Check prerequisites
    check_prereqs

    # Setup shared memory
    setup_shared_memory

    # Launch QEMU
    echo
    launch_qemu

    # Cleanup on exit
    trap cleanup EXIT
}

# Run main
main "$@"
