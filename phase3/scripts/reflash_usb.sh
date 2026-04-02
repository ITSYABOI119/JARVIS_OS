#!/bin/bash
# Quick reflash USB with latest seL4 JARVIS build
# Usage: sudo bash reflash_usb.sh [/dev/sdX]
set -euo pipefail
DEV="${1:-/dev/sda}"
BUILD="$HOME/sel4-x86/jbuild/images"
JARVIS="$HOME/Desktop/JARVIS_OS"
echo "Reflashing $DEV with JARVIS seL4..."
umount ${DEV}* 2>/dev/null || true
dd if=/dev/zero of=$DEV bs=1M count=1 2>/dev/null
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
grub-install --target=i386-pc --boot-directory=/mnt/usb/boot $DEV
sync && umount /mnt/usb
echo "Done. Reboot and select USB."
