#!/bin/bash
#
# JARVIS AI-OS — Create Bootable USB for seL4 x86-64
#
# Creates a USB drive that boots GRUB, which then loads the seL4 kernel
# and JARVIS rootserver via multiboot. Supports both BIOS (Legacy/CSM)
# and UEFI boot modes.
#
# Usage:
#   sudo ./create_boot_usb.sh /dev/sdX --confirm
#   sudo ./create_boot_usb.sh /dev/sdX --confirm --uefi-only
#   sudo ./create_boot_usb.sh /dev/sdX --confirm --bios-only
#
# Requirements:
#   - grub-install (grub-pc-bin for BIOS, grub-efi-amd64-bin for UEFI)
#   - parted, mkfs.fat, mount
#   - seL4 build output in ~/sel4-x86/jbuild/images/
#
# WARNING: This will ERASE the target device!
#

set -euo pipefail

# ── Colors ──────────────────────────────────────────────────────────
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

# ── Defaults ────────────────────────────────────────────────────────
DEVICE=""
CONFIRMED=0
BOOT_MODE="both"  # "both", "bios", "uefi"
BUILD_DIR="${SEL4_BUILD_DIR:-$HOME/sel4-x86/jbuild}"
GRUB_CFG_SRC=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# seL4 build output names (from DeclareRootserver(jarvis-x86))
KERNEL_IMAGE="kernel-x86_64-pc99"
ROOTSERVER_IMAGE="jarvis-x86-image-x86_64-pc99"

usage() {
    echo "Usage: sudo $0 DEVICE --confirm [OPTIONS]"
    echo ""
    echo "Arguments:"
    echo "  DEVICE              Block device (e.g., /dev/sdb)"
    echo "  --confirm           Required safety flag"
    echo ""
    echo "Options:"
    echo "  --bios-only         Install GRUB for BIOS/Legacy boot only"
    echo "  --uefi-only         Install GRUB for UEFI boot only"
    echo "  --build-dir DIR     seL4 build directory (default: ~/sel4-x86/jbuild)"
    echo "  --grub-cfg FILE     Custom grub.cfg (default: phase3/firmware/grub/grub.cfg)"
    echo "  -h, --help          Show this help"
    echo ""
    echo "Examples:"
    echo "  sudo $0 /dev/sdb --confirm"
    echo "  sudo $0 /dev/sdb --confirm --uefi-only"
    echo "  sudo $0 /dev/sdb --confirm --build-dir ~/sel4-x86/jbuild"
    exit 0
}

# ── Parse arguments ─────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case $1 in
        --confirm)
            CONFIRMED=1
            shift
            ;;
        --bios-only)
            BOOT_MODE="bios"
            shift
            ;;
        --uefi-only)
            BOOT_MODE="uefi"
            shift
            ;;
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --grub-cfg)
            GRUB_CFG_SRC="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        -*)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
        *)
            if [ -z "$DEVICE" ]; then
                DEVICE="$1"
            else
                echo -e "${RED}Unexpected argument: $1${NC}"
                exit 1
            fi
            shift
            ;;
    esac
done

# ── Default grub.cfg location ──────────────────────────────────────
if [ -z "$GRUB_CFG_SRC" ]; then
    GRUB_CFG_SRC="$PROJECT_ROOT/firmware/grub/grub.cfg"
fi

# ── Banner ──────────────────────────────────────────────────────────
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  JARVIS AI-OS — seL4 x86-64 Boot USB Creator${NC}"
echo -e "${GREEN}================================================${NC}"
echo ""

# ── Safety checks ───────────────────────────────────────────────────

# Must be root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: Must run as root (sudo)${NC}"
    exit 1
fi

# Require device argument
if [ -z "$DEVICE" ]; then
    echo -e "${RED}Error: No device specified${NC}"
    echo "Usage: sudo $0 /dev/sdX --confirm"
    exit 1
fi

# Require --confirm flag
if [ "$CONFIRMED" -ne 1 ]; then
    echo -e "${RED}Error: --confirm flag required (safety measure)${NC}"
    echo "Usage: sudo $0 $DEVICE --confirm"
    exit 1
fi

# Check device exists and is a block device
if [ ! -b "$DEVICE" ]; then
    echo -e "${RED}Error: $DEVICE is not a block device${NC}"
    echo ""
    echo "Available disks:"
    lsblk -d -o NAME,SIZE,TYPE,TRAN,MODEL | grep -E "disk"
    exit 1
fi

# Refuse the disk that holds the root filesystem
ROOT_DISK=$(lsblk -no PKNAME "$(findmnt -no SOURCE /)" 2>/dev/null || echo "")
if [ -n "$ROOT_DISK" ] && [ "$DEVICE" = "/dev/$ROOT_DISK" ]; then
    echo -e "${RED}REFUSED: $DEVICE contains your root filesystem (/)!${NC}"
    exit 1
fi

# Refuse NVMe devices — likely the system NVMe
if [[ "$DEVICE" == /dev/nvme* ]]; then
    echo -e "${RED}REFUSED: NVMe devices are likely your system disk${NC}"
    echo "Use a USB stick or secondary SATA drive."
    exit 1
fi

# Check device is removable (USB stick) — warn if not
REMOVABLE=$(cat "/sys/block/$(basename "$DEVICE")/removable" 2>/dev/null || echo "0")
if [ "$REMOVABLE" != "1" ]; then
    echo -e "${YELLOW}Warning: $DEVICE does not report as removable${NC}"
    echo -e "${YELLOW}Make sure this is the correct device!${NC}"
    echo ""
fi

# ── Check seL4 build images exist ──────────────────────────────────
KERNEL_PATH="$BUILD_DIR/images/$KERNEL_IMAGE"
ROOTSERVER_PATH="$BUILD_DIR/images/$ROOTSERVER_IMAGE"

if [ ! -f "$KERNEL_PATH" ]; then
    echo -e "${RED}Error: Kernel image not found: $KERNEL_PATH${NC}"
    echo ""
    echo "Build seL4 first:"
    echo "  cd ~/sel4-x86/jbuild && ninja"
    echo ""
    echo "Or specify build directory:"
    echo "  sudo $0 $DEVICE --confirm --build-dir /path/to/build"
    exit 1
fi

if [ ! -f "$ROOTSERVER_PATH" ]; then
    echo -e "${RED}Error: Rootserver image not found: $ROOTSERVER_PATH${NC}"
    echo ""
    echo "Expected: $ROOTSERVER_PATH"
    echo ""
    echo "If the image name differs, check ~/sel4-x86/jbuild/images/ and"
    echo "update ROOTSERVER_IMAGE in this script."
    echo ""
    # List available images to help debug
    echo "Available images in $BUILD_DIR/images/:"
    ls -la "$BUILD_DIR/images/" 2>/dev/null || echo "  (directory not found)"
    exit 1
fi

# Check grub.cfg exists
if [ ! -f "$GRUB_CFG_SRC" ]; then
    echo -e "${RED}Error: grub.cfg not found: $GRUB_CFG_SRC${NC}"
    exit 1
fi

# ── Check GRUB packages ────────────────────────────────────────────
GRUB_OK=1
if [ "$BOOT_MODE" = "both" ] || [ "$BOOT_MODE" = "bios" ]; then
    if ! dpkg -l grub-pc-bin &>/dev/null && ! command -v grub-install &>/dev/null; then
        echo -e "${YELLOW}Warning: grub-pc-bin not found (needed for BIOS boot)${NC}"
        echo "  Install: sudo apt install grub-pc-bin"
        if [ "$BOOT_MODE" = "bios" ]; then
            GRUB_OK=0
        fi
    fi
fi
if [ "$BOOT_MODE" = "both" ] || [ "$BOOT_MODE" = "uefi" ]; then
    if ! dpkg -l grub-efi-amd64-bin &>/dev/null; then
        echo -e "${YELLOW}Warning: grub-efi-amd64-bin not found (needed for UEFI boot)${NC}"
        echo "  Install: sudo apt install grub-efi-amd64-bin"
        if [ "$BOOT_MODE" = "uefi" ]; then
            GRUB_OK=0
        fi
    fi
fi
if [ "$GRUB_OK" -eq 0 ]; then
    echo -e "${RED}Missing required GRUB packages. Install them first.${NC}"
    exit 1
fi

# ── Show summary ────────────────────────────────────────────────────
echo -e "${BOLD}Target device:${NC}  $DEVICE"
lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MODEL "$DEVICE" 2>/dev/null || true
echo ""
echo -e "${BOLD}Boot mode:${NC}      $BOOT_MODE"
echo -e "${BOLD}Kernel:${NC}         $KERNEL_PATH ($(du -h "$KERNEL_PATH" | cut -f1))"
echo -e "${BOLD}Rootserver:${NC}     $ROOTSERVER_PATH ($(du -h "$ROOTSERVER_PATH" | cut -f1))"
echo -e "${BOLD}GRUB config:${NC}    $GRUB_CFG_SRC"
echo ""

# ── Final confirmation ──────────────────────────────────────────────
echo -e "${RED}${BOLD}WARNING: ALL DATA ON $DEVICE WILL BE DESTROYED${NC}"
echo ""
read -r -p "Type 'YES' in capitals to proceed: " FINAL_CONFIRM
if [ "$FINAL_CONFIRM" != "YES" ]; then
    echo "Aborted."
    exit 0
fi
echo ""

# ── Unmount existing partitions ─────────────────────────────────────
echo -e "${GREEN}[1/6] Unmounting existing partitions...${NC}"
for part in $(lsblk -no PATH "$DEVICE" | tail -n +2); do
    umount "$part" 2>/dev/null || true
done
echo "Done."
echo ""

# ── Partition the device ────────────────────────────────────────────
echo -e "${GREEN}[2/6] Creating partition table...${NC}"

if [ "$BOOT_MODE" = "uefi" ]; then
    # GPT for UEFI-only
    parted -s "$DEVICE" mklabel gpt
    parted -s "$DEVICE" mkpart primary fat32 1MiB 100%
    parted -s "$DEVICE" set 1 esp on
    echo "Created GPT with ESP partition."
elif [ "$BOOT_MODE" = "bios" ]; then
    # MBR for BIOS-only
    parted -s "$DEVICE" mklabel msdos
    parted -s "$DEVICE" mkpart primary fat32 1MiB 100%
    parted -s "$DEVICE" set 1 boot on
    echo "Created MBR with bootable partition."
else
    # GPT + BIOS boot partition for dual-mode
    parted -s "$DEVICE" mklabel gpt
    parted -s "$DEVICE" mkpart bios_grub 1MiB 2MiB      # BIOS boot partition
    parted -s "$DEVICE" set 1 bios_grub on
    parted -s "$DEVICE" mkpart primary fat32 2MiB 100%   # Data partition
    parted -s "$DEVICE" set 2 esp on
    echo "Created GPT with BIOS boot + ESP partitions."
fi

# Wait for kernel to re-read partition table
partprobe "$DEVICE" 2>/dev/null || true
sleep 2
echo ""

# ── Determine data partition ────────────────────────────────────────
if [ "$BOOT_MODE" = "both" ]; then
    # Dual-mode: data is partition 2
    DATA_PART="${DEVICE}2"
    # Handle /dev/nvmeXnYpZ naming (shouldn't reach here due to NVMe check, but safe)
    if [ ! -b "$DATA_PART" ]; then
        DATA_PART="${DEVICE}p2"
    fi
else
    # Single-mode: data is partition 1
    DATA_PART="${DEVICE}1"
    if [ ! -b "$DATA_PART" ]; then
        DATA_PART="${DEVICE}p1"
    fi
fi

if [ ! -b "$DATA_PART" ]; then
    echo -e "${RED}Error: Data partition not found ($DATA_PART)${NC}"
    echo "Partitions:"
    lsblk "$DEVICE"
    exit 1
fi

# ── Format data partition ───────────────────────────────────────────
echo -e "${GREEN}[3/6] Formatting $DATA_PART as FAT32...${NC}"
mkfs.fat -F 32 -n JARVIS_BOOT "$DATA_PART"
echo "Done."
echo ""

# ── Mount and copy files ────────────────────────────────────────────
echo -e "${GREEN}[4/6] Copying boot files...${NC}"
MOUNT_POINT=$(mktemp -d)
mount "$DATA_PART" "$MOUNT_POINT"

# Create boot directory
mkdir -p "$MOUNT_POINT/boot/grub"

# Copy seL4 kernel and rootserver
cp "$KERNEL_PATH" "$MOUNT_POINT/boot/$KERNEL_IMAGE"
cp "$ROOTSERVER_PATH" "$MOUNT_POINT/boot/$ROOTSERVER_IMAGE"

# Copy GRUB config
cp "$GRUB_CFG_SRC" "$MOUNT_POINT/boot/grub/grub.cfg"

echo "  Copied: boot/$KERNEL_IMAGE ($(du -h "$MOUNT_POINT/boot/$KERNEL_IMAGE" | cut -f1))"
echo "  Copied: boot/$ROOTSERVER_IMAGE ($(du -h "$MOUNT_POINT/boot/$ROOTSERVER_IMAGE" | cut -f1))"
echo "  Copied: boot/grub/grub.cfg"
echo ""

# ── Install GRUB ───────────────────────────────────────────────────
echo -e "${GREEN}[5/6] Installing GRUB bootloader...${NC}"

if [ "$BOOT_MODE" = "bios" ] || [ "$BOOT_MODE" = "both" ]; then
    echo "  Installing GRUB for BIOS/Legacy (i386-pc)..."
    grub-install \
        --target=i386-pc \
        --boot-directory="$MOUNT_POINT/boot" \
        --recheck \
        "$DEVICE" 2>&1 | sed 's/^/    /'
    echo "  BIOS GRUB installed."
fi

if [ "$BOOT_MODE" = "uefi" ] || [ "$BOOT_MODE" = "both" ]; then
    echo "  Installing GRUB for UEFI (x86_64-efi)..."
    mkdir -p "$MOUNT_POINT/EFI/BOOT"
    grub-install \
        --target=x86_64-efi \
        --efi-directory="$MOUNT_POINT" \
        --boot-directory="$MOUNT_POINT/boot" \
        --removable \
        --recheck \
        "$DEVICE" 2>&1 | sed 's/^/    /'
    echo "  UEFI GRUB installed."
fi

echo ""

# ── Sync and unmount ────────────────────────────────────────────────
echo -e "${GREEN}[6/6] Syncing and unmounting...${NC}"
sync
umount "$MOUNT_POINT"
rmdir "$MOUNT_POINT"
echo "Done."
echo ""

# ── Verify ──────────────────────────────────────────────────────────
echo -e "${GREEN}Verifying...${NC}"
VERIFY_MNT=$(mktemp -d)
if mount -o ro "$DATA_PART" "$VERIFY_MNT" 2>/dev/null; then
    echo -e "  Kernel:     $(ls -lh "$VERIFY_MNT/boot/$KERNEL_IMAGE" 2>/dev/null | awk '{print $5}' || echo 'MISSING')"
    echo -e "  Rootserver: $(ls -lh "$VERIFY_MNT/boot/$ROOTSERVER_IMAGE" 2>/dev/null | awk '{print $5}' || echo 'MISSING')"
    echo -e "  grub.cfg:   $(ls -lh "$VERIFY_MNT/boot/grub/grub.cfg" 2>/dev/null | awk '{print $5}' || echo 'MISSING')"
    if [ "$BOOT_MODE" = "uefi" ] || [ "$BOOT_MODE" = "both" ]; then
        echo -e "  EFI:        $(ls "$VERIFY_MNT/EFI/BOOT/" 2>/dev/null | tr '\n' ' ' || echo 'MISSING')"
    fi
    umount "$VERIFY_MNT"
fi
rmdir "$VERIFY_MNT"
echo ""

# ── Done ────────────────────────────────────────────────────────────
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  BOOT USB CREATED SUCCESSFULLY${NC}"
echo -e "${GREEN}================================================${NC}"
echo ""
echo "USB layout:"
echo "  /boot/kernel-x86_64-pc99              — seL4 kernel"
echo "  /boot/jarvis-x86-image-x86_64-pc99   — JARVIS rootserver"
echo "  /boot/grub/grub.cfg                   — GRUB config"
if [ "$BOOT_MODE" = "uefi" ] || [ "$BOOT_MODE" = "both" ]; then
    echo "  /EFI/BOOT/BOOTX64.EFI                — UEFI bootloader"
fi
echo ""
echo "Next steps:"
echo "  1. Remove USB from this machine"
echo "  2. Connect USB-to-serial adapter to spare PC COM1 header"
echo "  3. Open serial terminal: 115200 baud, 8N1"
echo "  4. Insert USB into spare PC"
echo "  5. Enter BIOS: disable Secure Boot, set USB as first boot device"
echo "  6. Save and reboot — GRUB menu should appear"
echo "  7. Select 'JARVIS seL4 x86-64' and watch serial console"
echo ""
echo "See: phase3/docs/BARE_METAL_BOOT_GUIDE.md"
echo ""
