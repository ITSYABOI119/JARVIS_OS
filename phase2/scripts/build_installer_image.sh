#!/bin/bash
# Build JARVIS Alpha SD card installer image
# Creates a flashable .img file with boot partition and optional root partition
#
# Usage:
#   build_installer_image.sh                    # Default: jarvis-alpha.img.gz
#   build_installer_image.sh --output my.img    # Custom output name
#   build_installer_image.sh --no-compress      # Skip gzip compression
#   build_installer_image.sh --boot-only        # Boot partition only (smaller image)
#
# Requires: losetup, mkfs.fat, dd, gzip (run as root/sudo)

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Defaults
JARVIS_ROOT="/mnt/c/Users/jluca/Documents/JARVIS_OS"
FIRMWARE_DIR="$JARVIS_ROOT/phase2/firmware"
OUTPUT_NAME="jarvis-alpha.img"
COMPRESS=1
BOOT_ONLY=0

# Boot partition size in MB
BOOT_SIZE_MB=256
# Root partition size in MB (models, docs, scripts)
ROOT_SIZE_MB=1024

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --output)
            OUTPUT_NAME="$2"
            shift 2
            ;;
        --no-compress)
            COMPRESS=0
            shift
            ;;
        --boot-only)
            BOOT_ONLY=1
            shift
            ;;
        --help|-h)
            echo "Usage: build_installer_image.sh [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --output FILE     Output image filename (default: jarvis-alpha.img)"
            echo "  --no-compress     Skip gzip compression"
            echo "  --boot-only       Create boot partition only (smaller image)"
            echo "  -h, --help        Show this help"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}JARVIS Alpha SD Card Image Builder${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root (sudo)${NC}"
    echo "  sudo bash build_installer_image.sh"
    exit 1
fi

# Check firmware directory
if [ ! -d "$FIRMWARE_DIR" ]; then
    echo -e "${RED}Error: Firmware directory not found: $FIRMWARE_DIR${NC}"
    exit 1
fi

# Required boot files
BOOT_FILES=(
    "kernel8.img"
    "u-boot.bin"
    "boot.scr"
    "start4.elf"
    "fixup4.dat"
    "config.txt"
    "bcm2711-rpi-4-b.dtb"
)

echo -e "${GREEN}[1/6] Checking boot files...${NC}"
MISSING=0
for f in "${BOOT_FILES[@]}"; do
    if [ ! -f "$FIRMWARE_DIR/$f" ]; then
        echo -e "${RED}  MISSING: $f${NC}"
        MISSING=1
    else
        SIZE=$(ls -lh "$FIRMWARE_DIR/$f" | awk '{print $5}')
        echo "  OK: $f ($SIZE)"
    fi
done

if [ "$MISSING" -eq 1 ]; then
    echo -e "${RED}Error: Missing required boot files. Run build_and_copy_kernel.sh first.${NC}"
    exit 1
fi

# Calculate image size
if [ "$BOOT_ONLY" -eq 1 ]; then
    IMAGE_SIZE_MB=$((BOOT_SIZE_MB + 4))  # 4MB for partition table alignment
    echo -e "${YELLOW}Boot-only mode: ${IMAGE_SIZE_MB}MB image${NC}"
else
    IMAGE_SIZE_MB=$((BOOT_SIZE_MB + ROOT_SIZE_MB + 4))
    echo "Full image: ${IMAGE_SIZE_MB}MB (${BOOT_SIZE_MB}MB boot + ${ROOT_SIZE_MB}MB root)"
fi
echo ""

# Create empty image
echo -e "${GREEN}[2/6] Creating ${IMAGE_SIZE_MB}MB disk image...${NC}"
dd if=/dev/zero of="$OUTPUT_NAME" bs=1M count="$IMAGE_SIZE_MB" status=progress 2>&1
echo ""

# Create partition table
echo -e "${GREEN}[3/6] Creating partition table...${NC}"
BOOT_START=2048  # 1MB alignment
BOOT_SECTORS=$((BOOT_SIZE_MB * 2048))
BOOT_END=$((BOOT_START + BOOT_SECTORS - 1))

if [ "$BOOT_ONLY" -eq 1 ]; then
    # Single FAT32 partition
    sfdisk "$OUTPUT_NAME" <<EOF
label: dos
unit: sectors

${OUTPUT_NAME}1 : start=$BOOT_START, size=$BOOT_SECTORS, type=c
EOF
else
    # Two partitions: FAT32 boot + ext4 root
    ROOT_START=$((BOOT_END + 1))
    ROOT_SECTORS=$((ROOT_SIZE_MB * 2048))
    sfdisk "$OUTPUT_NAME" <<EOF
label: dos
unit: sectors

${OUTPUT_NAME}1 : start=$BOOT_START, size=$BOOT_SECTORS, type=c
${OUTPUT_NAME}2 : start=$ROOT_START, size=$ROOT_SECTORS, type=83
EOF
fi
echo ""

# Setup loop device
echo -e "${GREEN}[4/6] Formatting partitions...${NC}"
LOOP=$(losetup --find --show --partscan "$OUTPUT_NAME")
echo "Loop device: $LOOP"

# Wait for partition devices
sleep 1

# Format boot partition
mkfs.fat -F 32 -n JARVIS_BOOT "${LOOP}p1"

if [ "$BOOT_ONLY" -eq 0 ]; then
    mkfs.ext4 -L JARVIS_ROOT "${LOOP}p2"
fi
echo ""

# Mount and copy files
echo -e "${GREEN}[5/6] Copying files to image...${NC}"
MOUNT_BOOT=$(mktemp -d)

mount "${LOOP}p1" "$MOUNT_BOOT"

# Copy boot files
for f in "${BOOT_FILES[@]}"; do
    cp "$FIRMWARE_DIR/$f" "$MOUNT_BOOT/"
    echo "  boot: $f"
done

# Copy overlays if present
if [ -d "$FIRMWARE_DIR/overlays" ]; then
    mkdir -p "$MOUNT_BOOT/overlays"
    cp -r "$FIRMWARE_DIR/overlays/"* "$MOUNT_BOOT/overlays/" 2>/dev/null || true
    echo "  boot: overlays/"
fi

# Copy driver image if present
if [ -f "$FIRMWARE_DIR/jarvis-sel4-driver-image-arm-bcm2711" ]; then
    cp "$FIRMWARE_DIR/jarvis-sel4-driver-image-arm-bcm2711" "$MOUNT_BOOT/"
    echo "  boot: jarvis-sel4-driver-image-arm-bcm2711"
fi

umount "$MOUNT_BOOT"
rmdir "$MOUNT_BOOT"

# Root partition (optional)
if [ "$BOOT_ONLY" -eq 0 ]; then
    MOUNT_ROOT=$(mktemp -d)
    mount "${LOOP}p2" "$MOUNT_ROOT"

    # Create directory structure
    mkdir -p "$MOUNT_ROOT/opt/jarvis/models"
    mkdir -p "$MOUNT_ROOT/opt/jarvis/scripts"
    mkdir -p "$MOUNT_ROOT/opt/jarvis/docs"

    # Copy Python scripts if present
    if [ -d "$JARVIS_ROOT/phase2/src/ai" ]; then
        cp "$JARVIS_ROOT/phase2/src/ai/"*.py "$MOUNT_ROOT/opt/jarvis/scripts/" 2>/dev/null || true
        echo "  root: /opt/jarvis/scripts/*.py"
    fi

    # Copy installer scripts if present
    for script in install_jarvis.sh; do
        if [ -f "$JARVIS_ROOT/phase2/scripts/$script" ]; then
            cp "$JARVIS_ROOT/phase2/scripts/$script" "$MOUNT_ROOT/opt/jarvis/scripts/"
            echo "  root: /opt/jarvis/scripts/$script"
        fi
    done

    # Copy docs if present
    for doc in USER_GUIDE.md ALPHA_TESTER_GUIDE.md; do
        if [ -f "$JARVIS_ROOT/phase2/docs/$doc" ]; then
            cp "$JARVIS_ROOT/phase2/docs/$doc" "$MOUNT_ROOT/opt/jarvis/docs/"
            echo "  root: /opt/jarvis/docs/$doc"
        fi
    done

    # Write version info
    echo "JARVIS AI-OS Alpha" > "$MOUNT_ROOT/opt/jarvis/VERSION"
    echo "Build: $(date -u '+%Y-%m-%d %H:%M UTC')" >> "$MOUNT_ROOT/opt/jarvis/VERSION"
    echo "Phase: 2 (Pi 4 bare metal)" >> "$MOUNT_ROOT/opt/jarvis/VERSION"
    echo "  root: /opt/jarvis/VERSION"

    umount "$MOUNT_ROOT"
    rmdir "$MOUNT_ROOT"
fi

# Cleanup loop device
losetup -d "$LOOP"
echo ""

# Compress
echo -e "${GREEN}[6/6] Finalizing image...${NC}"
IMAGE_SIZE=$(ls -lh "$OUTPUT_NAME" | awk '{print $5}')
SHA256=$(sha256sum "$OUTPUT_NAME" | awk '{print $1}')
echo "Raw image:  $OUTPUT_NAME ($IMAGE_SIZE)"
echo "SHA256:     $SHA256"

if [ "$COMPRESS" -eq 1 ]; then
    gzip -f "$OUTPUT_NAME"
    GZ_SIZE=$(ls -lh "${OUTPUT_NAME}.gz" | awk '{print $5}')
    GZ_SHA256=$(sha256sum "${OUTPUT_NAME}.gz" | awk '{print $1}')
    echo "Compressed: ${OUTPUT_NAME}.gz ($GZ_SIZE)"
    echo "SHA256(gz): $GZ_SHA256"
    FINAL="${OUTPUT_NAME}.gz"
else
    FINAL="$OUTPUT_NAME"
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}BUILD COMPLETE${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Output: $FINAL"
echo ""
echo "Flash to SD card:"
echo "  sudo bash flash_sd.sh /dev/sdX $FINAL"
echo ""
echo "Or manually:"
if [ "$COMPRESS" -eq 1 ]; then
    echo "  gunzip -c ${OUTPUT_NAME}.gz | sudo dd of=/dev/sdX bs=4M status=progress"
else
    echo "  sudo dd if=$OUTPUT_NAME of=/dev/sdX bs=4M status=progress"
fi
echo "  sync"
echo ""
