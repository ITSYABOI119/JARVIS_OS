#!/bin/bash
# =============================================================================
# JARVIS AI-OS Installer for Raspberry Pi 4
# =============================================================================
#
# Automated deployment of JARVIS seL4 to SD card for Raspberry Pi 4.
#
# What this script does:
#   1. Partitions and formats an SD card (FAT32 boot + ext4 root)
#   2. Copies firmware files (start4.elf, fixup4.dat, config.txt, kernel8.img,
#      bcm2711-rpi-4-b.dtb, boot.scr, u-boot.bin) to the boot partition
#   3. Sets up /opt/jarvis/ directory structure on the root partition
#   4. Optionally copies AI model files (Phi-3 Mini GGUF, TinyLlama GGUF)
#   5. Optionally sets up the Python host-side environment (venv + IPC client)
#   6. Detects UART serial adapter for host-side communication
#   7. Runs post-install verification
#
# Boot flow: GPU firmware -> U-Boot -> boot.scr -> kernel8.img -> seL4 -> JARVIS
# UART: 115200 baud, 8N1, GPIO14(TX) / GPIO15(RX)
#
# Usage:
#   ./install_jarvis.sh --device /dev/sdX
#   ./install_jarvis.sh --device /dev/sdX --skip-models
#   ./install_jarvis.sh --device /dev/sdX --dry-run
#   ./install_jarvis.sh --help
#
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
readonly VERSION="1.0.0"
readonly SCRIPT_NAME="$(basename "$0")"

readonly GREEN='\033[0;32m'
readonly YELLOW='\033[1;33m'
readonly RED='\033[0;31m'
readonly CYAN='\033[0;36m'
readonly BOLD='\033[1m'
readonly NC='\033[0m'

# Required firmware files and their minimum expected sizes (bytes).
# If a file is smaller than this, the copy likely failed.
declare -A FIRMWARE_FILES=(
    ["start4.elf"]=2000000
    ["fixup4.dat"]=5000
    ["config.txt"]=100
    ["kernel8.img"]=500000
    ["bcm2711-rpi-4-b.dtb"]=50000
    ["boot.scr"]=300
    ["u-boot.bin"]=500000
)

# Partition sizes
readonly BOOT_SIZE_MB=256
readonly BOOT_LABEL="JARVIS-BOOT"
readonly ROOT_LABEL="jarvis-root"

# ---------------------------------------------------------------------------
# Resolve JARVIS_ROOT (two levels up from this script)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JARVIS_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FIRMWARE_DIR="$JARVIS_ROOT/phase2/firmware"
AI_SRC_DIR="$JARVIS_ROOT/phase2/src/ai"
MODELS_DIR="$JARVIS_ROOT/models"

# ---------------------------------------------------------------------------
# Default options
# ---------------------------------------------------------------------------
DEVICE=""
DRY_RUN=false
SKIP_MODELS=false
SKIP_VENV=false
FORCE=false
BOOT_MNT=""
ROOT_MNT=""

# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------
info()    { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }
step()    { echo -e "${CYAN}[STEP]${NC}  ${BOLD}$*${NC}"; }
dry_info(){ echo -e "${YELLOW}[DRY-RUN]${NC} $*"; }

# Cleanup on exit: unmount partitions, remove temp mount points
cleanup() {
    local rc=$?
    if [ -n "$BOOT_MNT" ] && mountpoint -q "$BOOT_MNT" 2>/dev/null; then
        info "Unmounting boot partition..."
        umount "$BOOT_MNT" 2>/dev/null || true
    fi
    if [ -n "$ROOT_MNT" ] && mountpoint -q "$ROOT_MNT" 2>/dev/null; then
        info "Unmounting root partition..."
        umount "$ROOT_MNT" 2>/dev/null || true
    fi
    [ -n "$BOOT_MNT" ] && [ -d "$BOOT_MNT" ] && rmdir "$BOOT_MNT" 2>/dev/null || true
    [ -n "$ROOT_MNT" ] && [ -d "$ROOT_MNT" ] && rmdir "$ROOT_MNT" 2>/dev/null || true
    if [ $rc -ne 0 ] && [ "$DRY_RUN" = false ]; then
        error "Installation failed. SD card may be in a partial state."
    fi
}
trap cleanup EXIT

# ---------------------------------------------------------------------------
# Usage / Help
# ---------------------------------------------------------------------------
usage() {
    cat <<EOF
${BOLD}JARVIS AI-OS Installer v${VERSION}${NC}
Deploy JARVIS seL4 to a Raspberry Pi 4 SD card.

${BOLD}USAGE:${NC}
    $SCRIPT_NAME --device /dev/sdX [OPTIONS]

${BOLD}OPTIONS:${NC}
    --device, -d DEV    SD card block device (e.g. /dev/sdb). REQUIRED.
    --dry-run           Show what would be done without making changes.
    --skip-models       Skip copying AI model files to SD card.
    --skip-venv         Skip Python virtualenv setup on the host.
    --force             Skip Pi 4 hardware check (for host-side SD prep).
    --help, -h          Show this help message.

${BOLD}EXAMPLES:${NC}
    # Full install to /dev/sdb
    sudo $SCRIPT_NAME --device /dev/sdb

    # Preview without changes
    sudo $SCRIPT_NAME --device /dev/sdb --dry-run

    # Quick firmware-only install (no models, no venv)
    sudo $SCRIPT_NAME --device /dev/sdb --skip-models --skip-venv

${BOLD}REQUIREMENTS:${NC}
    - Linux or WSL2 with root privileges
    - parted, mkfs.vfat, mkfs.ext4 (usually in dosfstools, e2fsprogs)
    - SD card reader with card inserted

${BOLD}BOOT FLOW:${NC}
    GPU firmware -> U-Boot -> boot.scr -> kernel8.img -> seL4 -> JARVIS

${BOLD}UART WIRING:${NC}
    Pi GPIO14 (TXD) -> USB-Serial RXD
    Pi GPIO15 (RXD) <- USB-Serial TXD
    Pi GND          -- USB-Serial GND
    Baud: 115200, 8N1

EOF
    exit 0
}

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --device|-d)
                shift
                DEVICE="${1:-}"
                if [ -z "$DEVICE" ]; then
                    error "--device requires a block device argument"
                    exit 1
                fi
                ;;
            --dry-run)
                DRY_RUN=true
                ;;
            --skip-models)
                SKIP_MODELS=true
                ;;
            --skip-venv)
                SKIP_VENV=true
                ;;
            --force)
                FORCE=true
                ;;
            --help|-h)
                usage
                ;;
            *)
                error "Unknown option: $1"
                echo "Run '$SCRIPT_NAME --help' for usage."
                exit 1
                ;;
        esac
        shift
    done

    if [ -z "$DEVICE" ]; then
        error "No device specified. Use --device /dev/sdX"
        echo "Run '$SCRIPT_NAME --help' for usage."
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
preflight() {
    step "Running pre-flight checks"

    # Must be root (unless dry-run)
    if [ "$(id -u)" -ne 0 ] && [ "$DRY_RUN" = false ]; then
        error "This script must be run as root (sudo)."
        exit 1
    fi

    # Check required tools (skip in dry-run — no partitioning actually happens)
    if [ "$DRY_RUN" = false ]; then
        local missing=()
        for cmd in parted mkfs.vfat mkfs.ext4 lsblk blkid; do
            if ! command -v "$cmd" &>/dev/null; then
                missing+=("$cmd")
            fi
        done
        if [ ${#missing[@]} -gt 0 ]; then
            error "Missing required tools: ${missing[*]}"
            echo "Install with: apt-get install parted dosfstools e2fsprogs util-linux"
            exit 1
        fi
    fi

    # Validate device exists and is a block device (skip in dry-run)
    if [ "$DRY_RUN" = false ] && [ ! -b "$DEVICE" ]; then
        error "$DEVICE is not a block device."
        echo "Available block devices:"
        lsblk -d -o NAME,SIZE,TYPE,MODEL 2>/dev/null || true
        exit 1
    fi

    # Safety: refuse to operate on mounted root filesystem or anything that
    # looks like a system disk
    local dev_name
    dev_name="$(basename "$DEVICE")"
    local root_dev
    root_dev="$(lsblk -no PKNAME "$(findmnt -no SOURCE /)" 2>/dev/null || echo "")"
    if [ "$dev_name" = "$root_dev" ]; then
        error "Refusing to operate on $DEVICE -- it contains the root filesystem!"
        exit 1
    fi

    # Check for NVMe / system-looking disks
    if [[ "$DEVICE" == *"nvme0n1"* ]] || [[ "$DEVICE" == *"sda"* ]]; then
        warn "$DEVICE looks like it could be a system disk."
        echo -n "Are you SURE this is your SD card? (type 'YES' to continue): "
        local confirm
        read -r confirm
        if [ "$confirm" != "YES" ]; then
            info "Aborted."
            exit 0
        fi
    fi

    # Firmware directory must exist
    if [ ! -d "$FIRMWARE_DIR" ]; then
        error "Firmware directory not found: $FIRMWARE_DIR"
        error "Build the kernel first: cd phase2/scripts && bash build_and_copy_kernel.sh"
        exit 1
    fi

    # Check all required firmware files exist
    for fname in "${!FIRMWARE_FILES[@]}"; do
        if [ ! -f "$FIRMWARE_DIR/$fname" ]; then
            error "Missing firmware file: $FIRMWARE_DIR/$fname"
            exit 1
        fi
    done

    info "Device:    $DEVICE"
    info "Firmware:  $FIRMWARE_DIR"
    info "Dry run:   $DRY_RUN"
    info "Models:    $([ "$SKIP_MODELS" = true ] && echo "skip" || echo "install")"
    echo ""
}

# ---------------------------------------------------------------------------
# Show device info and get user confirmation
# ---------------------------------------------------------------------------
confirm_device() {
    step "Device information for $DEVICE"
    echo ""
    lsblk -o NAME,SIZE,TYPE,FSTYPE,MOUNTPOINT "$DEVICE" 2>/dev/null || true
    echo ""

    local dev_size
    dev_size="$(lsblk -bdn -o SIZE "$DEVICE" 2>/dev/null || echo "0")"
    local dev_size_gb
    dev_size_gb=$(( dev_size / 1073741824 ))

    if [ "$dev_size_gb" -lt 2 ]; then
        error "Device is ${dev_size_gb}GB -- too small for JARVIS (minimum 4GB recommended)."
        exit 1
    fi
    info "Device size: ${dev_size_gb} GB"

    # Check if any partitions are mounted
    local mounted
    mounted="$(lsblk -ln -o MOUNTPOINT "$DEVICE" 2>/dev/null | grep -v '^$' || true)"
    if [ -n "$mounted" ]; then
        warn "Partitions on $DEVICE are currently mounted:"
        echo "$mounted"
        echo ""
        warn "They will be unmounted before formatting."
    fi

    echo ""
    echo -e "${RED}${BOLD}WARNING: ALL DATA ON $DEVICE WILL BE DESTROYED!${NC}"
    echo ""
    if [ "$DRY_RUN" = true ]; then
        dry_info "Would prompt for confirmation here."
        return 0
    fi

    echo -n "Type 'INSTALL' to proceed: "
    local confirm
    read -r confirm
    if [ "$confirm" != "INSTALL" ]; then
        info "Aborted by user."
        exit 0
    fi
    echo ""
}

# ---------------------------------------------------------------------------
# Unmount existing partitions
# ---------------------------------------------------------------------------
unmount_device() {
    step "Unmounting existing partitions on $DEVICE"
    local parts
    parts="$(lsblk -ln -o NAME "$DEVICE" | tail -n +2)"
    for p in $parts; do
        if mountpoint -q "/dev/$p" 2>/dev/null || grep -q "/dev/$p" /proc/mounts 2>/dev/null; then
            if [ "$DRY_RUN" = true ]; then
                dry_info "Would unmount /dev/$p"
            else
                umount "/dev/$p" 2>/dev/null || true
                info "Unmounted /dev/$p"
            fi
        fi
    done
}

# ---------------------------------------------------------------------------
# Partition the SD card
# ---------------------------------------------------------------------------
partition_device() {
    step "Partitioning $DEVICE (${BOOT_SIZE_MB}MB FAT32 boot + ext4 root)"

    if [ "$DRY_RUN" = true ]; then
        dry_info "Would create GPT partition table on $DEVICE"
        dry_info "Would create ${BOOT_SIZE_MB}MB FAT32 partition (boot)"
        dry_info "Would create ext4 partition (root, remaining space)"
        return 0
    fi

    # Wipe existing partition table
    dd if=/dev/zero of="$DEVICE" bs=1M count=1 status=none 2>/dev/null || true

    # Create partitions with parted
    parted -s "$DEVICE" \
        mklabel msdos \
        mkpart primary fat32 1MiB "${BOOT_SIZE_MB}MiB" \
        set 1 boot on \
        set 1 lba on \
        mkpart primary ext4 "${BOOT_SIZE_MB}MiB" 100%

    # Let the kernel re-read the partition table
    partprobe "$DEVICE" 2>/dev/null || true
    sleep 2

    info "Partition table created."
}

# ---------------------------------------------------------------------------
# Determine partition device names
# (handles /dev/sdX1 vs /dev/mmcblkXp1 naming)
# ---------------------------------------------------------------------------
get_partition_names() {
    local base="$DEVICE"
    if [[ "$base" == *"mmcblk"* ]] || [[ "$base" == *"nvme"* ]] || [[ "$base" == *"loop"* ]]; then
        BOOT_PART="${base}p1"
        ROOT_PART="${base}p2"
    else
        BOOT_PART="${base}1"
        ROOT_PART="${base}2"
    fi
}

# ---------------------------------------------------------------------------
# Format partitions
# ---------------------------------------------------------------------------
format_partitions() {
    step "Formatting partitions"
    get_partition_names

    if [ "$DRY_RUN" = true ]; then
        dry_info "Would format $BOOT_PART as FAT32 (label: $BOOT_LABEL)"
        dry_info "Would format $ROOT_PART as ext4 (label: $ROOT_LABEL)"
        return 0
    fi

    # Wait for partition devices to appear
    local retries=10
    while [ ! -b "$BOOT_PART" ] && [ $retries -gt 0 ]; do
        sleep 1
        retries=$((retries - 1))
    done
    if [ ! -b "$BOOT_PART" ]; then
        error "Partition $BOOT_PART did not appear. Partitioning may have failed."
        exit 1
    fi

    info "Formatting $BOOT_PART as FAT32..."
    mkfs.vfat -F 32 -n "$BOOT_LABEL" "$BOOT_PART"

    info "Formatting $ROOT_PART as ext4..."
    mkfs.ext4 -L "$ROOT_LABEL" -q "$ROOT_PART"

    info "Formatting complete."
}

# ---------------------------------------------------------------------------
# Mount partitions
# ---------------------------------------------------------------------------
mount_partitions() {
    step "Mounting partitions"
    get_partition_names

    BOOT_MNT="$(mktemp -d /tmp/jarvis-boot.XXXXXX)"
    ROOT_MNT="$(mktemp -d /tmp/jarvis-root.XXXXXX)"

    if [ "$DRY_RUN" = true ]; then
        dry_info "Would mount $BOOT_PART at $BOOT_MNT"
        dry_info "Would mount $ROOT_PART at $ROOT_MNT"
        return 0
    fi

    mount "$BOOT_PART" "$BOOT_MNT"
    mount "$ROOT_PART" "$ROOT_MNT"

    info "Boot partition mounted at $BOOT_MNT"
    info "Root partition mounted at $ROOT_MNT"
}

# ---------------------------------------------------------------------------
# Copy firmware files to boot partition
# ---------------------------------------------------------------------------
copy_firmware() {
    step "Copying firmware files to boot partition"

    for fname in "${!FIRMWARE_FILES[@]}"; do
        local src="$FIRMWARE_DIR/$fname"
        if [ "$DRY_RUN" = true ]; then
            local sz
            sz="$(stat -c %s "$src" 2>/dev/null || echo "?")"
            dry_info "Would copy $fname ($sz bytes)"
        else
            cp "$src" "$BOOT_MNT/$fname"
            info "Copied $fname"
        fi
    done

    # Copy overlays directory if present
    if [ -d "$FIRMWARE_DIR/overlays" ]; then
        if [ "$DRY_RUN" = true ]; then
            dry_info "Would copy overlays/ directory"
        else
            cp -r "$FIRMWARE_DIR/overlays" "$BOOT_MNT/overlays"
            info "Copied overlays/"
        fi
    fi

    info "Firmware files copied."
}

# ---------------------------------------------------------------------------
# Set up root partition directory structure
# ---------------------------------------------------------------------------
setup_root() {
    step "Setting up root partition directory structure"

    local dirs=(
        "opt/jarvis"
        "opt/jarvis/models"
        "opt/jarvis/config"
        "opt/jarvis/logs"
        "opt/jarvis/cache"
        "opt/jarvis/scripts"
    )

    for d in "${dirs[@]}"; do
        if [ "$DRY_RUN" = true ]; then
            dry_info "Would create $ROOT_MNT/$d"
        else
            mkdir -p "$ROOT_MNT/$d"
        fi
    done

    # Write a version marker
    if [ "$DRY_RUN" = false ]; then
        cat > "$ROOT_MNT/opt/jarvis/config/install_info.txt" <<MARKER
JARVIS AI-OS Install Information
================================
Installer version: $VERSION
Install date:      $(date -u '+%Y-%m-%d %H:%M:%S UTC')
Source:            $JARVIS_ROOT
Device:            $DEVICE
Kernel image:      kernel8.img
Boot flow:         GPU -> U-Boot -> boot.scr -> kernel8.img -> seL4 -> JARVIS
UART baud:         115200
MARKER
        info "Install info written to /opt/jarvis/config/install_info.txt"
    fi

    info "Root filesystem structure created."
}

# ---------------------------------------------------------------------------
# Copy AI models (optional)
# ---------------------------------------------------------------------------
copy_models() {
    if [ "$SKIP_MODELS" = true ]; then
        info "Skipping model copy (--skip-models)."
        return 0
    fi

    step "Copying AI model files"

    if [ ! -d "$MODELS_DIR" ]; then
        warn "Models directory not found at $MODELS_DIR"
        warn "Skipping model copy. You can copy models manually later to"
        warn "  /opt/jarvis/models/ on the SD card root partition."
        return 0
    fi

    local model_count=0
    for model_file in "$MODELS_DIR"/*.gguf "$MODELS_DIR"/*.bin; do
        [ -f "$model_file" ] || continue
        local fname
        fname="$(basename "$model_file")"
        local sz
        sz="$(stat -c %s "$model_file" 2>/dev/null || echo "0")"
        local sz_mb=$(( sz / 1048576 ))

        if [ "$DRY_RUN" = true ]; then
            dry_info "Would copy $fname (${sz_mb} MB)"
        else
            info "Copying $fname (${sz_mb} MB)... this may take a while"
            cp "$model_file" "$ROOT_MNT/opt/jarvis/models/$fname"
            info "Copied $fname"
        fi
        model_count=$((model_count + 1))
    done

    if [ "$model_count" -eq 0 ]; then
        warn "No model files (*.gguf, *.bin) found in $MODELS_DIR"
        warn "You can copy models manually later to /opt/jarvis/models/"
    else
        info "$model_count model file(s) copied."
    fi
}

# ---------------------------------------------------------------------------
# Set up Python host-side environment (optional)
# ---------------------------------------------------------------------------
setup_host_venv() {
    if [ "$SKIP_VENV" = true ]; then
        info "Skipping Python venv setup (--skip-venv)."
        return 0
    fi

    step "Setting up Python host-side environment"

    if ! command -v python3 &>/dev/null; then
        warn "python3 not found. Skipping venv setup."
        warn "Install Python 3.8+ and re-run, or set up manually."
        return 0
    fi

    local venv_dir="$JARVIS_ROOT/venv"
    if [ -d "$venv_dir" ]; then
        info "Virtual environment already exists at $venv_dir"
    else
        if [ "$DRY_RUN" = true ]; then
            dry_info "Would create Python venv at $venv_dir"
        else
            info "Creating Python virtual environment..."
            python3 -m venv "$venv_dir"
            info "Virtual environment created at $venv_dir"
        fi
    fi

    if [ "$DRY_RUN" = false ] && [ -d "$venv_dir" ]; then
        info "Installing Python dependencies..."
        "$venv_dir/bin/pip" install --quiet --upgrade pip 2>/dev/null || true
        "$venv_dir/bin/pip" install --quiet pyserial 2>/dev/null || true
        info "Python dependencies installed (pyserial)."
    fi

    # Detect UART serial adapter
    step "Detecting UART serial adapter"
    local serial_found=false
    for tty_path in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1; do
        if [ -e "$tty_path" ]; then
            info "Found serial adapter: $tty_path"
            serial_found=true
            break
        fi
    done
    if [ "$serial_found" = false ]; then
        warn "No USB serial adapter detected."
        warn "Connect a USB-serial adapter and it should appear as /dev/ttyUSB0."
        warn "You may need to: sudo usermod -aG dialout \$USER"
    fi
}

# ---------------------------------------------------------------------------
# Post-install verification
# ---------------------------------------------------------------------------
verify_install() {
    step "Running post-install verification"

    if [ "$DRY_RUN" = true ]; then
        dry_info "Would verify all firmware files on boot partition."
        return 0
    fi

    local pass=0
    local fail=0

    # Check each firmware file
    for fname in "${!FIRMWARE_FILES[@]}"; do
        local min_size="${FIRMWARE_FILES[$fname]}"
        local fpath="$BOOT_MNT/$fname"

        if [ ! -f "$fpath" ]; then
            error "MISSING: $fname"
            fail=$((fail + 1))
            continue
        fi

        local actual_size
        actual_size="$(stat -c %s "$fpath" 2>/dev/null || echo "0")"
        if [ "$actual_size" -lt "$min_size" ]; then
            error "TOO SMALL: $fname ($actual_size bytes, expected >=$min_size)"
            fail=$((fail + 1))
            continue
        fi

        info "OK: $fname ($actual_size bytes)"
        pass=$((pass + 1))
    done

    # Verify config.txt contains required settings
    if [ -f "$BOOT_MNT/config.txt" ]; then
        local config_ok=true
        for setting in "arm_64bit=1" "enable_uart=1" "kernel=u-boot.bin"; do
            if ! grep -q "$setting" "$BOOT_MNT/config.txt"; then
                error "config.txt missing: $setting"
                config_ok=false
                fail=$((fail + 1))
            fi
        done
        if [ "$config_ok" = true ]; then
            info "OK: config.txt settings verified"
            pass=$((pass + 1))
        fi
    fi

    # Verify root filesystem structure
    if mountpoint -q "$ROOT_MNT" 2>/dev/null; then
        if [ -d "$ROOT_MNT/opt/jarvis" ]; then
            info "OK: /opt/jarvis directory structure"
            pass=$((pass + 1))
        else
            error "MISSING: /opt/jarvis directory structure"
            fail=$((fail + 1))
        fi
    fi

    echo ""
    if [ "$fail" -eq 0 ]; then
        info "Verification: ${pass}/${pass} checks passed"
    else
        error "Verification: ${fail} check(s) FAILED out of $((pass + fail))"
    fi

    return "$fail"
}

# ---------------------------------------------------------------------------
# Sync and unmount
# ---------------------------------------------------------------------------
finalize() {
    step "Finalizing installation"

    if [ "$DRY_RUN" = true ]; then
        dry_info "Would sync and unmount partitions."
        return 0
    fi

    info "Syncing filesystem buffers..."
    sync

    if [ -n "$BOOT_MNT" ] && mountpoint -q "$BOOT_MNT" 2>/dev/null; then
        umount "$BOOT_MNT"
        info "Boot partition unmounted."
    fi
    if [ -n "$ROOT_MNT" ] && mountpoint -q "$ROOT_MNT" 2>/dev/null; then
        umount "$ROOT_MNT"
        info "Root partition unmounted."
    fi

    # Clear mount vars so cleanup() doesn't try again
    BOOT_MNT=""
    ROOT_MNT=""

    info "SD card is safe to remove."
}

# ---------------------------------------------------------------------------
# Print completion banner
# ---------------------------------------------------------------------------
print_banner() {
    echo ""
    echo -e "${GREEN}========================================${NC}"
    if [ "$DRY_RUN" = true ]; then
        echo -e "${GREEN}  DRY RUN COMPLETE                      ${NC}"
    else
        echo -e "${GREEN}  JARVIS INSTALLATION COMPLETE           ${NC}"
    fi
    echo -e "${GREEN}========================================${NC}"
    echo ""
    echo "Next steps:"
    echo "  1. Remove the SD card from this machine."
    echo "  2. Insert it into the Raspberry Pi 4."
    echo "  3. Connect USB-serial adapter:"
    echo "       Pi GPIO14 (TXD) -> USB-Serial RXD"
    echo "       Pi GPIO15 (RXD) <- USB-Serial TXD"
    echo "       Pi GND          -- USB-Serial GND"
    echo "  4. Open a serial console: 115200 baud, 8N1"
    echo "       Linux:   screen /dev/ttyUSB0 115200"
    echo "       Windows: PuTTY -> Serial -> COM port -> 115200"
    echo "  5. Power on the Pi 4."
    echo "  6. You should see the JARVIS banner on the serial console."
    echo ""
    echo "To start the AI host-side bridge:"
    echo "  source $JARVIS_ROOT/venv/bin/activate"
    echo "  python $AI_SRC_DIR/uart_ipc_client.py --port /dev/ttyUSB0"
    echo ""
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo ""
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  JARVIS AI-OS Installer v${VERSION}    ${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo ""

    parse_args "$@"
    preflight
    confirm_device
    unmount_device
    partition_device
    format_partitions
    mount_partitions
    copy_firmware
    setup_root
    copy_models
    setup_host_venv
    verify_install
    finalize
    print_banner
}

main "$@"
