#!/bin/bash
# Quick reflash USB with latest seL4 JARVIS build
# Usage: sudo bash reflash_usb.sh [/dev/sdX]
set -euo pipefail
DEV="${1:-/dev/sda}"
BUILD="$HOME/sel4-x86/jbuild/images"
JARVIS="$HOME/Desktop/JARVIS_OS"

# Verify it's a block device (not a regular file from a bad dd)
if [ ! -b "$DEV" ]; then
    echo "ERROR: $DEV is not a block device. Unplug and re-plug the USB stick."
    exit 1
fi

echo "Reflashing $DEV with JARVIS seL4..."
umount ${DEV}* 2>/dev/null || true
# No dd — just repartition directly. dd can corrupt /dev/sda into a regular file.
parted -s $DEV mklabel msdos
parted -s $DEV mkpart primary fat32 1MiB 100%
parted -s $DEV set 1 boot on
sleep 1
mkfs.fat -F32 ${DEV}1
mkdir -p /mnt/usb && mount ${DEV}1 /mnt/usb
mkdir -p /mnt/usb/boot/grub
cp $BUILD/kernel-x86_64-pc99 /mnt/usb/boot/
cp $BUILD/sel4test-driver-image-x86_64-pc99 /mnt/usb/boot/
cp $JARVIS/phase3/firmware/grub/grub.cfg /mnt/usb/boot/grub/

# Copy model file if present
MODEL="$JARVIS/phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"
if [ -f "$MODEL" ]; then
    echo "Copying model ($(du -h "$MODEL" | cut -f1))..."
    cp "$MODEL" /mnt/usb/boot/model.gguf
else
    echo "WARNING: Model not found at $MODEL — skipping"
fi

grub-install --target=i386-pc --boot-directory=/mnt/usb/boot $DEV
sync && umount /mnt/usb
echo "Done. Reboot and select USB."
