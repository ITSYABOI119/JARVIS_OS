#!/bin/bash
# QEMU boot with NVMe emulation for testing NVMe driver + FAT32 model loading
# Usage: bash phase3/scripts/qemu_test_nvme.sh [/path/to/model.gguf]
set -euo pipefail

BUILD="${SEL4_BUILD_DIR:-$HOME/sel4-x86/jbuild}"
KERNEL="$BUILD/images/kernel-x86_64-pc99"
ROOTSERVER="$BUILD/images/sel4test-driver-image-x86_64-pc99"
MODEL="${1:-$HOME/Desktop/JARVIS_OS/phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf}"
DISK_IMG="/tmp/jarvis_data.img"

if [ ! -f "$KERNEL" ] || [ ! -f "$ROOTSERVER" ]; then
    echo "ERROR: Build images not found. Run build_jarvis_x86.sh first."
    exit 1
fi

# Create FAT32 disk image with model
echo "Creating FAT32 disk image with model..."
dd if=/dev/zero of="$DISK_IMG" bs=1M count=1024 2>/dev/null
mkfs.fat -F 32 -n JARVIS_DATA "$DISK_IMG" >/dev/null
MOUNT_DIR=$(mktemp -d)
sudo mount "$DISK_IMG" "$MOUNT_DIR"
if [ -f "$MODEL" ]; then
    echo "Copying model ($(du -h "$MODEL" | cut -f1))..."
    sudo cp "$MODEL" "$MOUNT_DIR/MODEL.GUF"
else
    echo "WARNING: Model not found at $MODEL"
fi
sudo umount "$MOUNT_DIR"
rmdir "$MOUNT_DIR"

echo "Booting JARVIS seL4 x86-64 with NVMe emulation..."
echo "Exit: Ctrl-A then X"
echo ""

# Use KVM if available (100x faster inference)
KVM_OPTS=""
if [ -e /dev/kvm ]; then
    KVM_OPTS="-enable-kvm -cpu host"
    echo "KVM acceleration: enabled"
else
    echo "KVM acceleration: not available (inference will be slow)"
fi

qemu-system-x86_64 \
    ${KVM_OPTS:--cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce} \
    -nographic -serial mon:stdio -m 8G \
    -kernel "$KERNEL" \
    -initrd "$ROOTSERVER" \
    -drive file="$DISK_IMG",if=none,id=nvme0,format=raw \
    -device nvme,serial=JARVIS_DATA,drive=nvme0
