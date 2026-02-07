#!/bin/bash
# Flash JARVIS Alpha image to SD card
# Safety checks included to prevent accidental data loss
#
# Usage:
#   flash_sd.sh /dev/sdX jarvis-alpha.img.gz
#   flash_sd.sh /dev/sdX jarvis-alpha.img
#   flash_sd.sh /dev/sdX jarvis-alpha.img.gz --yes-i-am-sure   # Skip confirmation
#
# Requires: dd, lsblk (run as root/sudo)

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

DEVICE=""
IMAGE=""
SKIP_CONFIRM=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --yes-i-am-sure)
            SKIP_CONFIRM=1
            shift
            ;;
        --help|-h)
            echo "Usage: flash_sd.sh DEVICE IMAGE [OPTIONS]"
            echo ""
            echo "Arguments:"
            echo "  DEVICE              Block device (e.g., /dev/sdb, /dev/mmcblk0)"
            echo "  IMAGE               Image file (.img or .img.gz)"
            echo ""
            echo "Options:"
            echo "  --yes-i-am-sure     Skip confirmation prompt (for scripted use)"
            echo "  -h, --help          Show this help"
            echo ""
            echo "Examples:"
            echo "  sudo bash flash_sd.sh /dev/sdb jarvis-alpha.img.gz"
            echo "  sudo bash flash_sd.sh /dev/mmcblk0 jarvis-alpha.img"
            exit 0
            ;;
        *)
            if [ -z "$DEVICE" ]; then
                DEVICE="$1"
            elif [ -z "$IMAGE" ]; then
                IMAGE="$1"
            else
                echo -e "${RED}Unknown argument: $1${NC}"
                exit 1
            fi
            shift
            ;;
    esac
done

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}JARVIS Alpha SD Card Flasher${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (sudo)${NC}"
    exit 1
fi

# Validate arguments
if [ -z "$DEVICE" ] || [ -z "$IMAGE" ]; then
    echo -e "${RED}Error: Both DEVICE and IMAGE are required${NC}"
    echo "Usage: flash_sd.sh /dev/sdX jarvis-alpha.img.gz"
    exit 1
fi

# Check image exists
if [ ! -f "$IMAGE" ]; then
    echo -e "${RED}Error: Image file not found: $IMAGE${NC}"
    exit 1
fi

# Check device exists
if [ ! -b "$DEVICE" ]; then
    echo -e "${RED}Error: Block device not found: $DEVICE${NC}"
    echo "Available devices:"
    lsblk -d -o NAME,SIZE,TYPE,TRAN | grep -E "disk"
    exit 1
fi

# Safety: refuse to flash to system disk
ROOT_DISK=$(lsblk -no PKNAME $(findmnt -no SOURCE /) 2>/dev/null || echo "")
if [ -n "$ROOT_DISK" ] && [ "$DEVICE" = "/dev/$ROOT_DISK" ]; then
    echo -e "${RED}REFUSED: $DEVICE appears to be your system disk!${NC}"
    exit 1
fi

# Show device info
echo -e "${BOLD}Target device:${NC} $DEVICE"
lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT "$DEVICE" 2>/dev/null || true
echo ""

echo -e "${BOLD}Image file:${NC} $IMAGE"
ls -lh "$IMAGE"
echo ""

# Check if device is mounted
MOUNTED=$(lsblk -no MOUNTPOINT "$DEVICE" 2>/dev/null | grep -v "^$" || true)
if [ -n "$MOUNTED" ]; then
    echo -e "${YELLOW}Warning: Device has mounted partitions:${NC}"
    echo "$MOUNTED"
    echo ""
fi

# Confirm
if [ "$SKIP_CONFIRM" -eq 0 ]; then
    echo -e "${RED}${BOLD}WARNING: ALL DATA ON $DEVICE WILL BE DESTROYED${NC}"
    echo ""
    read -p "Type 'yes' to continue: " CONFIRM
    if [ "$CONFIRM" != "yes" ]; then
        echo "Aborted."
        exit 0
    fi
    echo ""
fi

# Unmount all partitions
echo -e "${GREEN}[1/4] Unmounting partitions...${NC}"
for part in $(lsblk -no PATH "$DEVICE" | tail -n +2); do
    umount "$part" 2>/dev/null || true
done
echo "Done."
echo ""

# Flash
echo -e "${GREEN}[2/4] Writing image to $DEVICE...${NC}"
if [[ "$IMAGE" == *.gz ]]; then
    echo "Decompressing and writing..."
    if command -v pv &>/dev/null; then
        GZ_SIZE=$(stat -c%s "$IMAGE")
        gunzip -c "$IMAGE" | pv -s "$GZ_SIZE" | dd of="$DEVICE" bs=4M conv=fsync 2>/dev/null
    else
        gunzip -c "$IMAGE" | dd of="$DEVICE" bs=4M status=progress conv=fsync
    fi
else
    if command -v pv &>/dev/null; then
        pv "$IMAGE" | dd of="$DEVICE" bs=4M conv=fsync 2>/dev/null
    else
        dd if="$IMAGE" of="$DEVICE" bs=4M status=progress conv=fsync
    fi
fi
echo ""

# Sync
echo -e "${GREEN}[3/4] Syncing...${NC}"
sync
echo "Done."
echo ""

# Verify
echo -e "${GREEN}[4/4] Verifying...${NC}"
# Re-read partition table
partprobe "$DEVICE" 2>/dev/null || true
sleep 1

# Check partition table was written
PARTS=$(lsblk -no NAME "$DEVICE" | wc -l)
if [ "$PARTS" -gt 1 ]; then
    echo -e "${GREEN}Partition table OK${NC}"
    lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL "$DEVICE"
else
    echo -e "${YELLOW}Warning: No partitions detected (may need replug)${NC}"
fi
echo ""

# Try to verify boot files
BOOT_PART=""
for p in "${DEVICE}1" "${DEVICE}p1"; do
    if [ -b "$p" ]; then
        BOOT_PART="$p"
        break
    fi
done

if [ -n "$BOOT_PART" ]; then
    VERIFY_MNT=$(mktemp -d)
    if mount -o ro "$BOOT_PART" "$VERIFY_MNT" 2>/dev/null; then
        if [ -f "$VERIFY_MNT/kernel8.img" ]; then
            KSIZE=$(ls -lh "$VERIFY_MNT/kernel8.img" | awk '{print $5}')
            echo -e "${GREEN}Verified: kernel8.img present ($KSIZE)${NC}"
        else
            echo -e "${YELLOW}Warning: kernel8.img not found on boot partition${NC}"
        fi
        FILE_COUNT=$(ls "$VERIFY_MNT/" | wc -l)
        echo "Boot partition: $FILE_COUNT files"
        umount "$VERIFY_MNT"
    fi
    rmdir "$VERIFY_MNT"
fi
echo ""

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}FLASH COMPLETE${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Next steps:"
echo "  1. Remove SD card from computer"
echo "  2. Insert into Raspberry Pi 4"
echo "  3. Connect USB-Serial adapter (GPIO14=TX, GPIO15=RX)"
echo "  4. Open serial console: 115200 baud, 8N1"
echo "  5. Power on Pi 4"
echo ""
