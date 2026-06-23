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
MODEL_PATH="$HOME/Desktop/JARVIS_OS/models/gemma-4-E2B-it-Q4_K_M.gguf"
MIN_FREE_MIB=1024   # 1 GB minimum free space required
DEST_FILENAME="GEMMA2B.GUF"
PARTITION_LABEL="JARVIS_DATA"
# The rootserver hardcodes the model partition's start LBA (main_x86.c
# NVME_FAT32_PART_LBA). JARVIS_DATA MUST begin at exactly this sector or the
# rootserver silently finds no model on bare metal.
REQUIRED_START_SECTOR=32768
# The rootserver compares the FAT 8.3 short name (main_x86.c JARVIS_MODEL_FILE).
EXPECTED_SHORTNAME="GEMMA2B GUF"
MOUNT_POINT=""

usage() {
    echo "Usage: sudo $0 --confirm [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --confirm              Required safety flag"
    echo "  --model FILE           Path to GGUF model file"
    echo "                         (default: ~/Desktop/JARVIS_OS/models/"
    echo "                                    gemma-4-E2B-it-Q4_K_M.gguf)"
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

# ── Helper: validate a FAT 8.3 short name matches what the rootserver wants ──
# Echoes the 8.3 "NAME EXT" form of a basename; "__LFN__" if it is a long name.
fat_8dot3() {
    local base name ext
    base="$(basename "$1")"
    base="$(printf '%s' "$base" | tr '[:lower:]' '[:upper:]')"
    if [[ "$base" == *.* ]]; then
        name="${base%.*}"; ext="${base##*.}"
    else
        name="$base"; ext=""
    fi
    if [ "${#name}" -gt 8 ] || [ "${#ext}" -gt 3 ]; then
        printf '%s' "__LFN__"; return 0
    fi
    printf '%s %s' "$name" "$ext"
}

# Fail loudly unless DEST_FILENAME maps to the rootserver's JARVIS_MODEL_FILE.
DEST_SHORT="$(fat_8dot3 "$DEST_FILENAME")"
if [ "$DEST_SHORT" != "$EXPECTED_SHORTNAME" ]; then
    echo -e "${RED}Error: dest filename '$DEST_FILENAME' (8.3 '$DEST_SHORT') != rootserver JARVIS_MODEL_FILE '$EXPECTED_SHORTNAME'${NC}"
    exit 1
fi

# ── Detect an existing JARVIS_DATA partition ─────────────────────────
# The live box already has JARVIS_DATA at sector 32768. If it exists AND
# starts at the required sector, refresh the model and SKIP repartitioning.
detect_existing_jarvis_data() {
    # Echoes the partition device path (e.g. /dev/nvme0n1p4) of a GPT
    # partition named JARVIS_DATA whose Start == REQUIRED_START_SECTOR,
    # else empty.
    local num start name dev
    # parted machine-readable: lines like "4:32768s:...:...:fat32:JARVIS_DATA:..."
    while IFS=: read -r num start _ _ _ name _; do
        [[ "$num" =~ ^[0-9]+$ ]] || continue
        [ "$name" = "$PARTITION_LABEL" ] || continue
        if [ "$start" = "${REQUIRED_START_SECTOR}s" ]; then
            dev="${NVME_DEV}p${num}"
            [ -b "$dev" ] || dev="${NVME_DEV}${num}"
            printf '%s' "$dev"
            return 0
        fi
    done < <(parted -s -m "$NVME_DEV" unit s print 2>/dev/null || true)
    return 0
}

EXISTING_PART="$(detect_existing_jarvis_data)"

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
    echo "  ~/Desktop/JARVIS_OS/models/gemma-4-E2B-it-Q4_K_M.gguf"
    echo ""
    echo "Place the deployed Gemma 4 E2B Q4_K_M GGUF (~2.89 GiB) there, or pass"
    echo "  --model /path/to/your-model.gguf"
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

# ── Shared: copy + verify the model onto a mounted partition ─────────
# copy_and_verify_model PART_DEV  — mounts PART_DEV, copies the model as
# DEST_FILENAME, verifies size + FAT 8.3 short name, unmounts.
MOUNT_POINT=""
cleanup() {
    if [ -n "$MOUNT_POINT" ] && mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT" 2>/dev/null || true
    fi
    [ -n "$MOUNT_POINT" ] && rmdir "$MOUNT_POINT" 2>/dev/null || true
}
trap cleanup EXIT

copy_and_verify_model() {
    local part="$1" copied
    MOUNT_POINT=$(mktemp -d)
    mount "$part" "$MOUNT_POINT"
    echo "  Mounted $part at $MOUNT_POINT"
    echo "  Copying $MODEL_PATH → $MOUNT_POINT/$DEST_FILENAME ..."
    echo "  (This may take a minute — model is ${MODEL_SIZE_MIB} MiB)"
    cp "$MODEL_PATH" "$MOUNT_POINT/$DEST_FILENAME"
    copied=$(stat -c%s "$MOUNT_POINT/$DEST_FILENAME")
    if [ "$copied" -ne "$MODEL_SIZE_BYTES" ]; then
        echo -e "${RED}Error: Copy verification failed!${NC}"
        echo "  Source size: ${MODEL_SIZE_BYTES} bytes"
        echo "  Dest size:   ${copied} bytes"
        exit 1
    fi
    # Confirm the on-disk 8.3 short name actually matches JARVIS_MODEL_FILE.
    # (DEST_FILENAME is already validated above; this guards a surprise rename.)
    if [ ! -e "$MOUNT_POINT/$DEST_FILENAME" ]; then
        echo -e "${RED}Error: $DEST_FILENAME missing after copy${NC}"
        exit 1
    fi
    echo "  Verified: ${copied} bytes, 8.3 name '${EXPECTED_SHORTNAME}'"
    sync
    umount "$MOUNT_POINT"
    rmdir "$MOUNT_POINT"
    MOUNT_POINT=""
}

# ── Fast path: an existing JARVIS_DATA at sector 32768 is box-safe ───
# Refresh the model on it and SKIP repartitioning entirely.
if [ -n "$EXISTING_PART" ]; then
    echo -e "${GREEN}================================================${NC}"
    echo -e "${GREEN}  Existing ${PARTITION_LABEL} at sector ${REQUIRED_START_SECTOR} detected${NC}"
    echo -e "${GREEN}================================================${NC}"
    echo ""
    echo "  Partition: $EXISTING_PART (start == ${REQUIRED_START_SECTOR}s — correct)"
    echo "  Will refresh the model file in place; NO repartitioning."
    echo ""
    read -r -p "Type 'YES' to refresh the model on $EXISTING_PART: " FINAL_CONFIRM
    if [ "$FINAL_CONFIRM" != "YES" ]; then
        echo "Aborted."
        exit 0
    fi
    echo ""
    echo -e "${GREEN}[1/1] Refreshing model on ${EXISTING_PART}...${NC}"
    copy_and_verify_model "$EXISTING_PART"
    NEW_PART="$EXISTING_PART"
    echo ""
    echo -e "${GREEN}================================================${NC}"
    echo -e "${GREEN}  MODEL REFRESHED (no partition change)${NC}"
    echo -e "${GREEN}================================================${NC}"
    echo ""
    echo "  Device:     $NEW_PART"
    echo "  Label:      $PARTITION_LABEL  (start sector ${REQUIRED_START_SECTOR} — verified)"
    echo "  Model file: /$DEST_FILENAME (${MODEL_SIZE_MIB} MiB)"
    echo ""
    echo "  The rootserver loads JARVIS_MODEL_FILE \"$EXPECTED_SHORTNAME\" from this partition."
    exit 0
fi

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

# ── Determine partition end (start is FIXED at the required sector) ──
echo -e "${BOLD}Calculating partition boundaries...${NC}"

# START IS NON-NEGOTIABLE: the rootserver hardcodes NVME_FAT32_PART_LBA, so
# JARVIS_DATA MUST begin at exactly REQUIRED_START_SECTOR (32768). We do NOT
# use parted's free-space start — we pin the start to that sector.
#
# Confirm the required start sector is actually inside free space, and grab
# the free-space END (in sectors) that contains it as the partition end.
PART_START_S="$REQUIRED_START_SECTOR"
FREE_END_S=""
while IFS=: read -r fstart fend _ ftype _; do
    # Free-space rows from `parted -m unit s print free` look like:
    #   "32768s:4000797326s:3999764559s:free;"
    [ "$ftype" = "free" ] || continue
    fstart_n="${fstart%s}"; fend_n="${fend%s}"
    [[ "$fstart_n" =~ ^[0-9]+$ ]] || continue
    [[ "$fend_n" =~ ^[0-9]+$ ]] || continue
    if [ "$fstart_n" -le "$PART_START_S" ] && [ "$fend_n" -gt "$PART_START_S" ]; then
        FREE_END_S="$fend_n"
        break
    fi
done < <(parted -s -m "$NVME_DEV" unit s print free 2>/dev/null || true)

if [ -z "$FREE_END_S" ]; then
    echo -e "${RED}Error: sector ${REQUIRED_START_SECTOR} is not inside free space on $NVME_DEV.${NC}"
    echo "  The model partition MUST start at sector ${REQUIRED_START_SECTOR}, but that"
    echo "  sector is occupied or unavailable. Free up that region first."
    echo ""
    echo "Free-space map:"
    parted -s "$NVME_DEV" unit s print free 2>/dev/null || true
    exit 1
fi

# Leave the last sector unused for alignment safety.
PART_END_S=$(( FREE_END_S - 1 ))
echo "  Partition span: ${PART_START_S}s — ${PART_END_S}s (start pinned to ${REQUIRED_START_SECTOR})"
echo ""

# ── Final confirmation ───────────────────────────────────────────────
echo -e "${BOLD}Summary of actions:${NC}"
echo ""
echo "  Device:         $NVME_DEV"
echo "  New partition:  ${NEW_PART} (partition ${NEW_PART_NUM})"
echo "  Partition span: ${PART_START_S}s — ${PART_END_S}s"
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

# ── Create partition (start pinned to sector 32768) ──────────────────
echo -e "${GREEN}[1/4] Creating FAT32 partition ${NEW_PART} at ${PART_START_S}s...${NC}"
parted -s "$NVME_DEV" mkpart "$PARTITION_LABEL" fat32 "${PART_START_S}s" "${PART_END_S}s"
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

# ── HARD ASSERT: JARVIS_DATA Start == 32768s ─────────────────────────
# If this is wrong, the rootserver silently finds no model on bare metal,
# so FAIL the script rather than ship a broken layout.
echo -e "${BOLD}Asserting ${PARTITION_LABEL} starts at sector ${REQUIRED_START_SECTOR}...${NC}"
ACTUAL_START_S=$(parted -s -m "$NVME_DEV" unit s print 2>/dev/null \
    | awk -F: -v L="$PARTITION_LABEL" '$6 == L {print $2}' | head -n1)
if [ "$ACTUAL_START_S" != "${REQUIRED_START_SECTOR}s" ]; then
    echo -e "${RED}FATAL: ${PARTITION_LABEL} Start is '${ACTUAL_START_S:-?}', expected '${REQUIRED_START_SECTOR}s'.${NC}"
    echo "  The seL4 rootserver reads NVME_FAT32_PART_LBA = ${REQUIRED_START_SECTOR}; a"
    echo "  different start sector means the model will NOT be found on bare metal."
    echo "  Refusing to continue. Partition table:"
    parted -s "$NVME_DEV" unit s print 2>/dev/null || true
    exit 1
fi
echo "  OK: Start == ${REQUIRED_START_SECTOR}s"
echo ""

# ── Format as FAT32 ──────────────────────────────────────────────────
echo -e "${GREEN}[2/4] Formatting $NEW_PART as FAT32 (label: ${PARTITION_LABEL})...${NC}"
mkfs.fat -F 32 -n "$PARTITION_LABEL" "$NEW_PART"
echo "  Format complete."
echo ""

# ── Mount and copy model (copy + size/8.3-name verify) ───────────────
echo -e "${GREEN}[3/4] Copying model to partition...${NC}"
copy_and_verify_model "$NEW_PART"
echo ""

# ── Synced/unmounted inside copy_and_verify_model ────────────────────
echo -e "${GREEN}[4/4] Done.${NC}"
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
