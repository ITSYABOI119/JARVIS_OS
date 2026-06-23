#!/bin/bash
# Quick reflash USB with the latest seL4 JARVIS build.
#
# DEFAULT = UEFI (GPT + ESP, grub x86_64-efi --removable) — the v1.0 boot
# standard since Phase 4 goal #2 Step 1 (CSM is sunset; the installer/release
# need UEFI; GOP is the framebuffer path for goal #2). The grub default entry
# is the multiboot2/efi_gop UEFI entry (phase3/firmware/grub/grub.cfg).
#
# Use --bios for the legacy BIOS/CSM (MBR, grub i386-pc) fallback.
#
# Usage: sudo bash reflash_usb.sh [/dev/sdX] [--bios|--uefi]
set -euo pipefail

DEV="/dev/sda"
MODE="uefi"
for a in "$@"; do
    case "$a" in
        --bios) MODE="bios" ;;
        --uefi) MODE="uefi" ;;
        /dev/*) DEV="$a" ;;
        *) echo "Usage: sudo bash reflash_usb.sh [/dev/sdX] [--bios|--uefi]"; exit 1 ;;
    esac
done

BUILD="$HOME/sel4-x86/jbuild/images"
JARVIS="$HOME/Desktop/JARVIS_OS"
KERNEL="$BUILD/kernel-x86_64-pc99"
ROOTSERVER="$BUILD/sel4test-driver-image-x86_64-pc99"
GRUBCFG="$JARVIS/phase3/firmware/grub/grub.cfg"

# Verify it's a block device (not a regular file from a bad dd)
if [ ! -b "$DEV" ]; then
    echo "ERROR: $DEV is not a block device. Unplug and re-plug the USB stick."
    exit 1
fi

# --- Device-safety refusals (same guards as create_boot_usb.sh) ---
# Refuse NVMe devices — almost certainly the system disk, not a USB stick.
if [[ "$DEV" == /dev/nvme* ]]; then
    echo "REFUSED: $DEV is an NVMe device (likely your system disk). Use a USB stick."
    exit 1
fi
# Refuse the disk that holds the root filesystem.
ROOT_DISK=$(lsblk -no PKNAME "$(findmnt -no SOURCE / 2>/dev/null)" 2>/dev/null || echo "")
if [ -n "$ROOT_DISK" ] && [ "$DEV" = "/dev/$ROOT_DISK" ]; then
    echo "REFUSED: $DEV contains your root filesystem (/). Use a USB stick."
    exit 1
fi
[ -f "$KERNEL" ]     || { echo "ERROR: kernel image not found: $KERNEL (build first)"; exit 1; }
[ -f "$ROOTSERVER" ] || { echo "ERROR: rootserver image not found: $ROOTSERVER (build first)"; exit 1; }
[ -f "$GRUBCFG" ]    || { echo "ERROR: grub.cfg not found: $GRUBCFG"; exit 1; }

echo "Reflashing $DEV with JARVIS seL4 (mode=$MODE)..."
umount ${DEV}* 2>/dev/null || true
# No dd — repartition directly. dd can corrupt /dev/sda into a regular file.

if [ "$MODE" = "bios" ]; then
    # --- Legacy BIOS/CSM fallback: MBR + bootable FAT32 ---
    parted -s "$DEV" mklabel msdos
    parted -s "$DEV" mkpart primary fat32 1MiB 100%
    parted -s "$DEV" set 1 boot on
else
    # --- UEFI (DEFAULT): GPT + ESP ---
    parted -s "$DEV" mklabel gpt
    parted -s "$DEV" mkpart primary fat32 1MiB 100%
    parted -s "$DEV" set 1 esp on
fi
partprobe "$DEV" 2>/dev/null || true
sleep 1

PART="${DEV}1"
[ -b "$PART" ] || PART="${DEV}p1"
mkfs.fat -F32 "$PART"
mkdir -p /mnt/usb && mount "$PART" /mnt/usb
mkdir -p /mnt/usb/boot/grub
cp "$KERNEL" /mnt/usb/boot/
cp "$ROOTSERVER" /mnt/usb/boot/
cp "$GRUBCFG" /mnt/usb/boot/grub/
# Model is loaded from NVMe at runtime — not copied to USB.

if [ "$MODE" = "bios" ]; then
    grub-install --target=i386-pc --boot-directory=/mnt/usb/boot "$DEV"
else
    mkdir -p /mnt/usb/EFI/BOOT
    grub-install --target=x86_64-efi --efi-directory=/mnt/usb \
        --boot-directory=/mnt/usb/boot --removable --recheck "$DEV"
fi
sync && umount /mnt/usb
if [ "$MODE" = "bios" ]; then
    echo "Done (BIOS). Reboot, enable CSM, boot the USB."
else
    echo "Done (UEFI). Reboot: CSM off, Secure Boot OS Type 'Other OS', boot the 'UEFI: <stick>' entry."
    echo "Monitor goes dark when seL4 takes over (no framebuffer UI until goal #2 Step 2); validate via the NVMe log."
fi
