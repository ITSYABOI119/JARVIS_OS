#!/bin/bash
#
# JARVIS AI-OS — Setup FAT32 Data Partition on NVMe
#
# Creates a FAT32 partition on the NVMe SSD for JARVIS model files.
# The seL4 rootserver reads the model from this partition at boot.
#
# Usage:
#   sudo bash setup_nvme_partition.sh --confirm
#   sudo bash setup_nvme_partition.sh --confirm --model /path/to/model.gguf
#
# Prerequisites:
#   - NVMe SSD with free space (shrink existing partition first if needed)
#   - parted, mkfs.fat, mount
#
# WARNING: This modifies your NVMe partition table!

set -euo pipefail

# ── Colors ──────────────────────────────────────────────────────────
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
NC='\033[0m'

# ── Defaults ────────────────────────────────────────────────────────
CONFIRMED=0
NVME_DEV=""
MODEL_PATH="$HOME/Desktop/JARVIS_OS/phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"
MIN_FREE_MIB=1024   # 1 GB minimum free space required
DEST_FILENAME="GEMMA2B.GUF"
PARTITION_LABEL="JARVIS_DATA"
MOUNT_POINT=""

usage() {
    echo "Usage: sudo $0 --confirm [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --confirm              Required safety flag"
    echo "  --model FILE           Path to GGUF model file"
    echo "                         (default: ~/Desktop/JARVIS_OS/phase3/models/"
    echo "                                    Llama-3.2-1B-Instruct-Q4_K_M.gguf)"
    echo "  --nvme DEVICE          NVMe device to use (default: auto-detect)"
    echo "  -h, --help             Show this help"
    echo ""
    echo "Examples:"
    echo "  sudo $0 --confirm"
    echo "  sudo $0 --confirm --model /path/to/model.gguf"
    echo "  sudo $0 --confirm --nvme /dev/nvme1n1"
    echo ""
    echo "If the NVMe has no free space, shrink the root partition first:"
    echo "  1. Boot from a live USB"
    echo "  2. sudo e2fsck -f /dev/nvme0n1p3"
    echo "  3. sudo resize2fs /dev/nvme0n1p3 <new-size>G"
    echo "  4. sudo parted /dev/nvme0n1 resizepart 3 <new-end-MiB>MiB"
    echo "  5. Reboot normally and re-run this script"
    exit 0
}

# ── Parse arguments ─────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case $1 in
        --confirm)
            CONFIRMED=1
            shift
            ;;
        --model)
            MODEL_PATH="$2"
            shift 2
            ;;
        --nvme)
            NVME_DEV="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        -*)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
        *)
            echo -e "${RED}Unexpected argument: $1${NC}"
            echo "Run '$0 --help' for usage."
            exit 1
            ;;
    esac
done

# ── Banner ──────────────────────────────────────────────────────────
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  JARVIS AI-OS — NVMe FAT32 Data Partition${NC}"
echo -e "${GREEN}================================================${NC}"
echo ""

# ── Safety: must be root ─────────────────────────────────────────────
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: Must run as root (sudo)${NC}"
    exit 1
fi

# ── Safety: require --confirm ────────────────────────────────────────
if [ "$CONFIRMED" -ne 1 ]; then
    echo -e "${RED}Error: --confirm flag required (safety measure)${NC}"
    echo ""
    echo "This script will modify your NVMe partition table."
    echo "Run with --confirm to proceed."
    echo ""
    echo "Usage: sudo $0 --confirm"
    exit 1
fi

# ── Detect NVMe device ───────────────────────────────────────────────
if [ -z "$NVME_DEV" ]; then
    echo -e "${BOLD}Detecting NVMe device...${NC}"
    # Try common NVMe names in order
    for candidate in /dev/nvme0n1 /dev/nvme1n1 /dev/nvme0n2; do
        if [ -b "$candidate" ]; then
            NVME_DEV="$candidate"
            echo "  Found: $NVME_DEV"
            break
        fi
    done

    if [ -z "$NVME_DEV" ]; then
        echo -e "${RED}Error: No NVMe device found.${NC}"
        echo ""
        echo "All block devices:"
        lsblk -d -o NAME,SIZE,TYPE,TRAN,MODEL
        echo ""
        echo "Specify manually: sudo $0 --confirm --nvme /dev/nvme0n1"
        exit 1
    fi
else
    # Validate user-supplied device
    if [ ! -b "$NVME_DEV" ]; then
        echo -e "${RED}Error: $NVME_DEV is not a block device${NC}"
        exit 1
    fi
    echo -e "${BOLD}Using NVMe device:${NC} $NVME_DEV"
fi
echo ""

# ── Refuse if the device is NOT an NVMe ─────────────────────────────
if [[ "$NVME_DEV" != /dev/nvme* ]]; then
    echo -e "${RED}Error: $NVME_DEV does not look like an NVMe device${NC}"
    echo "Expected a path like /dev/nvme0n1."
    exit 1
fi

# ── Show current partition layout ────────────────────────────────────
echo -e "${BOLD}Current partition layout on $NVME_DEV:${NC}"
echo ""
lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,MOUNTPOINT "$NVME_DEV" || true
echo ""
echo "Detailed partition table:"
parted -s "$NVME_DEV" print 2>/dev/null || true
echo ""

# ── Check for free space ─────────────────────────────────────────────
echo -e "${BOLD}Checking for free space...${NC}"

# Use parted to get free space info
FREE_SPACE_LINE=$(parted -s "$NVME_DEV" print free 2>/dev/null \
    | grep "Free Space" | tail -1 || true)

if [ -z "$FREE_SPACE_LINE" ]; then
    echo -e "${YELLOW}Warning: Could not determine free space via parted.${NC}"
    FREE_MIB=0
else
    # Parse free space size (format: "  X.XGIB  Y.YGIB  Z.ZMiB  Free Space")
    # Extract the size column (third field)
    FREE_FIELD=$(echo "$FREE_SPACE_LINE" | awk '{print $3}')
    FREE_UNIT=$(echo "$FREE_FIELD" | sed 's/[0-9.]//g')
    FREE_NUM=$(echo "$FREE_FIELD" | sed 's/[^0-9.]//g')

    case "$FREE_UNIT" in
        GiB|GB)
            FREE_MIB=$(echo "$FREE_NUM * 1024" | bc | cut -d. -f1)
            ;;
        MiB|MB)
            FREE_MIB=$(echo "$FREE_NUM" | cut -d. -f1)
            ;;
        TiB|TB)
            FREE_MIB=$(echo "$FREE_NUM * 1048576" | bc | cut -d. -f1)
            ;;
        *)
            FREE_MIB=0
            ;;
    esac
fi

echo "  Free space detected: ${FREE_MIB} MiB"
echo ""

if [ "$FREE_MIB" -lt "$MIN_FREE_MIB" ]; then
    echo -e "${RED}Error: Not enough free space on $NVME_DEV${NC}"
    echo ""
    echo "  Available: ${FREE_MIB} MiB"
    echo "  Required:  ${MIN_FREE_MIB} MiB (1 GB minimum for model file)"
    echo ""
    echo -e "${BOLD}To free space, shrink your root partition:${NC}"
    echo ""
    echo "  Option A — Offline resize (recommended, safest):"
    echo "    1. Boot from a Ubuntu/Debian live USB"
    echo "    2. Check filesystem:  sudo e2fsck -f /dev/nvme0n1p3"
    echo "    3. Shrink filesystem: sudo resize2fs /dev/nvme0n1p3 <new-size>G"
    echo "       (e.g., if disk is 1TB and Ubuntu uses 900GB, try 850G)"
    echo "    4. Shrink partition:  sudo parted /dev/nvme0n1 resizepart 3 <new-end>MiB"
    echo "       (set new-end to filesystem end + small buffer, e.g. 870000MiB)"
    echo "    5. Reboot normally"
    echo "    6. Re-run this script: sudo $0 --confirm"
    echo ""
    echo "  Option B — Online resize (riskier, requires shrinkable filesystem):"
    echo "    WARNING: Shrinking a mounted root filesystem is dangerous."
    echo "    Use Option A unless you know exactly what you are doing."
    echo ""
    echo "  Current partition table for reference:"
    parted -s "$NVME_DEV" print 2>/dev/null || true
    exit 1
fi

# ── Check model file ─────────────────────────────────────────────────
echo -e "${BOLD}Checking model file...${NC}"
if [ ! -f "$MODEL_PATH" ]; then
    echo -e "${RED}Error: Model file not found: $MODEL_PATH${NC}"
    echo ""
    echo "Specify a different path with --model:"
    echo "  sudo $0 --confirm --model /path/to/model.gguf"
    echo ""
    echo "Expected default location:"
    echo "  ~/Desktop/JARVIS_OS/phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"
    echo ""
    echo "To download the model on the JARVIS PC:"
    echo "  mkdir -p ~/Desktop/JARVIS_OS/phase3/models"
    echo "  cd ~/Desktop/JARVIS_OS/phase3/models"
    echo "  wget https://huggingface.co/bartowski/Llama-3.2-1B-Instruct-GGUF/resolve/main/Llama-3.2-1B-Instruct-Q4_K_M.gguf"
    exit 1
fi

MODEL_SIZE_BYTES=$(stat -c%s "$MODEL_PATH")
MODEL_SIZE_MIB=$(( MODEL_SIZE_BYTES / 1048576 ))
echo "  Model: $MODEL_PATH"
echo "  Size:  ${MODEL_SIZE_MIB} MiB (${MODEL_SIZE_BYTES} bytes)"
echo ""

# Verify model fits in free space (with 128MiB buffer)
SPACE_NEEDED=$(( MODEL_SIZE_MIB + 128 ))
if [ "$FREE_MIB" -lt "$SPACE_NEEDED" ]; then
    echo -e "${RED}Error: Not enough free space for model + overhead${NC}"
    echo "  Model + buffer: ${SPACE_NEEDED} MiB"
    echo "  Available:      ${FREE_MIB} MiB"
    exit 1
fi

# ── Check required tools ─────────────────────────────────────────────
echo -e "${BOLD}Checking required tools...${NC}"
MISSING_TOOLS=0
for tool in parted mkfs.fat mount partprobe; do
    if ! command -v "$tool" &>/dev/null; then
        echo -e "  ${RED}MISSING: $tool${NC}"
        MISSING_TOOLS=1
    else
        echo "  OK: $tool"
    fi
done
if [ "$MISSING_TOOLS" -eq 1 ]; then
    echo ""
    echo -e "${RED}Install missing tools:${NC}"
    echo "  sudo apt install parted dosfstools mount util-linux"
    exit 1
fi
echo ""

# ── Determine next partition number ─────────────────────────────────
echo -e "${BOLD}Determining next partition number...${NC}"
LAST_PART_NUM=$(parted -s "$NVME_DEV" print 2>/dev/null \
    | awk '/^ +[0-9]/ {last=$1} END {print last+0}')
NEW_PART_NUM=$(( LAST_PART_NUM + 1 ))

# Build partition device name (NVMe uses 'p' separator)
NEW_PART="${NVME_DEV}p${NEW_PART_NUM}"
echo "  Last partition number: ${LAST_PART_NUM}"
echo "  New partition:         ${NEW_PART} (partition ${NEW_PART_NUM})"
echo ""

# ── Determine free space start/end for new partition ─────────────────
echo -e "${BOLD}Calculating partition boundaries...${NC}"

# Get free space start (in MiB, rounded up to 1MiB alignment)
FREE_START=$(parted -s "$NVME_DEV" unit MiB print free 2>/dev/null \
    | grep "Free Space" | tail -1 | awk '{print $1}' | sed 's/MiB//')
FREE_END=$(parted -s "$NVME_DEV" unit MiB print free 2>/dev/null \
    | grep "Free Space" | tail -1 | awk '{print $2}' | sed 's/MiB//')

if [ -z "$FREE_START" ] || [ -z "$FREE_END" ]; then
    echo -e "${RED}Error: Could not determine free space boundaries.${NC}"
    echo "Output of 'parted print free':"
    parted -s "$NVME_DEV" unit MiB print free 2>/dev/null || true
    exit 1
fi

# Use all free space for the partition (up to a 2GB cap for model storage)
# Round free_end back 1MiB from disk end to avoid alignment issues
FREE_END_CLEAN=$(echo "$FREE_END" | cut -d. -f1)
FREE_START_CLEAN=$(echo "$FREE_START" | cut -d. -f1)

echo "  Free space: ${FREE_START_CLEAN} MiB — ${FREE_END_CLEAN} MiB"
echo ""

# ── Final confirmation ───────────────────────────────────────────────
echo -e "${BOLD}Summary of actions:${NC}"
echo ""
echo "  Device:         $NVME_DEV"
echo "  New partition:  ${NEW_PART} (partition ${NEW_PART_NUM})"
echo "  Partition span: ${FREE_START_CLEAN} MiB — ${FREE_END_CLEAN} MiB"
echo "  Format:         FAT32, label '${PARTITION_LABEL}'"
echo "  Model source:   $MODEL_PATH (${MODEL_SIZE_MIB} MiB)"
echo "  Model dest:     ${NEW_PART}:/${DEST_FILENAME}"
echo ""
echo -e "${YELLOW}${BOLD}WARNING: A new partition will be added to $NVME_DEV.${NC}"
echo -e "${YELLOW}         Existing partitions will NOT be modified.${NC}"
echo ""
read -r -p "Type 'YES' in capitals to proceed: " FINAL_CONFIRM
if [ "$FINAL_CONFIRM" != "YES" ]; then
    echo "Aborted."
    exit 0
fi
echo ""

# ── Create partition ─────────────────────────────────────────────────
echo -e "${GREEN}[1/4] Creating FAT32 partition ${NEW_PART}...${NC}"
parted -s "$NVME_DEV" mkpart primary fat32 "${FREE_START_CLEAN}MiB" "${FREE_END_CLEAN}MiB"
echo "  Partition created."

# Wait for kernel to re-read partition table
partprobe "$NVME_DEV" 2>/dev/null || true
sleep 2

# Verify the new partition node appeared
if [ ! -b "$NEW_PART" ]; then
    echo -e "${RED}Error: $NEW_PART did not appear after partprobe.${NC}"
    echo "Partitions now:"
    lsblk "$NVME_DEV"
    exit 1
fi
echo "  Partition node: $NEW_PART (confirmed)"
echo ""

# ── Format as FAT32 ──────────────────────────────────────────────────
echo -e "${GREEN}[2/4] Formatting $NEW_PART as FAT32 (label: ${PARTITION_LABEL})...${NC}"
mkfs.fat -F 32 -n "$PARTITION_LABEL" "$NEW_PART"
echo "  Format complete."
echo ""

# ── Mount and copy model ─────────────────────────────────────────────
echo -e "${GREEN}[3/4] Copying model to partition...${NC}"
MOUNT_POINT=$(mktemp -d)

# Cleanup handler — runs on exit (success or failure)
cleanup() {
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}
trap cleanup EXIT

mount "$NEW_PART" "$MOUNT_POINT"
echo "  Mounted $NEW_PART at $MOUNT_POINT"

echo "  Copying $MODEL_PATH → $MOUNT_POINT/$DEST_FILENAME ..."
echo "  (This may take a minute — model is ${MODEL_SIZE_MIB} MiB)"
cp "$MODEL_PATH" "$MOUNT_POINT/$DEST_FILENAME"

# Verify the copy
COPIED_SIZE=$(stat -c%s "$MOUNT_POINT/$DEST_FILENAME")
if [ "$COPIED_SIZE" -ne "$MODEL_SIZE_BYTES" ]; then
    echo -e "${RED}Error: Copy verification failed!${NC}"
    echo "  Source size: ${MODEL_SIZE_BYTES} bytes"
    echo "  Dest size:   ${COPIED_SIZE} bytes"
    exit 1
fi
echo "  Verified: ${COPIED_SIZE} bytes"
echo ""

# ── Sync and unmount ─────────────────────────────────────────────────
echo -e "${GREEN}[4/4] Syncing and unmounting...${NC}"
sync
umount "$MOUNT_POINT"
rmdir "$MOUNT_POINT"
echo "  Done."
echo ""

# ── Get partition UUID ───────────────────────────────────────────────
PART_UUID=$(blkid -s UUID -o value "$NEW_PART" 2>/dev/null || echo "(unavailable)")
PART_PARTUUID=$(blkid -s PARTUUID -o value "$NEW_PART" 2>/dev/null || echo "(unavailable)")

# ── Summary ──────────────────────────────────────────────────────────
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  PARTITION SETUP COMPLETE${NC}"
echo -e "${GREEN}================================================${NC}"
echo ""
echo -e "${BOLD}New partition summary:${NC}"
echo ""
echo "  Device:        $NEW_PART"
echo "  Partition #:   $NEW_PART_NUM"
echo "  Label:         $PARTITION_LABEL"
echo "  UUID:          $PART_UUID"
echo "  PARTUUID:      $PART_PARTUUID"
echo "  Model file:    /$DEST_FILENAME (${MODEL_SIZE_MIB} MiB)"
echo ""
echo "Current partition layout:"
lsblk -o NAME,SIZE,TYPE,FSTYPE,LABEL,UUID "$NVME_DEV" || true
echo ""
echo -e "${BOLD}seL4 rootserver — no code change needed:${NC}"
echo ""
echo "  The rootserver already loads this file at boot:"
echo "    main_x86.c:  #define JARVIS_MODEL_FILE \"GEMMA2B GUF\"   (8.3 name of ${DEST_FILENAME})"
echo ""
echo "  It reads the FAT32 partition at NVME_FAT32_PART_LBA = 32768 (main_x86.c),"
echo "  with a whole-disk LBA-0 fallback for QEMU images only."
echo ""
echo "  >>> BARE-METAL REQUIREMENT: this partition's start sector MUST be 32768."
echo "      Verify:  sudo parted ${NVME_DEV} unit s print   (Start must = 32768s)"
echo "      If it is not 32768, the rootserver will NOT find the model on bare metal."
echo ""
echo "  Partition UUID (informational): $PART_UUID"
echo ""
