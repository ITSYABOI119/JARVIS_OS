#!/bin/bash
# =============================================================================
# JARVIS AI-OS — x86-64 Installer (Phase 4 Goal #4, v1.0 core)
# =============================================================================
#
# One-shot orchestrator for deploying JARVIS seL4 x86-64 to the JARVIS PC:
#
#   1. preflight()        — validate build tree, tools, target device
#   2. build              — build_jarvis_x86.sh (unless --skip-build)
#   3. boot USB           — stage_boot_payload(): copy kernel + rootserver +
#                           grub.cfg to a FAT32 boot device + grub-install
#                           (unless --skip-usb)
#   4. model provision    — setup_nvme_partition.sh: create/verify JARVIS_DATA
#                           at sector 32768 and copy the Gemma GGUF as
#                           GEMMA2B.GUF (unless --skip-model)
#   5. verify()           — confirm the staged payload
#   6. BIOS + validation checklist
#
# Boot flow: UEFI firmware -> GRUB (multiboot2) -> seL4 kernel -> JARVIS
#            rootserver -> loads the model from NVMe FAT32 (JARVIS_DATA) at
#            runtime. The model is NOT on the boot USB.
#
# Only --target usb is implemented in v1.0. --target esp / --target disk
# (on-SSD install) are placeholders that exit "not yet implemented".
#
# Usage:
#   sudo ./install_jarvis_x86.sh --usb /dev/sdb
#   sudo ./install_jarvis_x86.sh --usb /dev/sdb --model /path/to/model.gguf
#   ./install_jarvis_x86.sh --dry-run --usb /dev/sdb --skip-build --skip-model
#   ./install_jarvis_x86.sh --help
#
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
readonly VERSION="1.0.0"
SCRIPT_NAME="$(basename "${BASH_SOURCE[0]}")"
readonly SCRIPT_NAME

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'
readonly GREEN YELLOW RED CYAN BOLD NC

# Resolve paths from this script's location.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"   # phase3/
readonly PROJECT_ROOT
REPO_DEFAULT="${JARVIS_REPO:-$HOME/Desktop/JARVIS_OS}"

# seL4 kernel image name is fixed; the rootserver image name is DERIVED at
# runtime from the build output (see derive_rootserver_image).
readonly KERNEL_IMAGE="kernel-x86_64-pc99"

# ---------------------------------------------------------------------------
# Default options
# ---------------------------------------------------------------------------
DO_BUILD=false      # build only when --build is given OR neither skip given? -> see parse
USB_DEV=""
MODEL_PATH=""       # resolved after parse (default = Gemma GGUF in repo models/)
NVME_DEV="/dev/nvme0n1"
REPO_PATH="$REPO_DEFAULT"
SKIP_BUILD=false
SKIP_USB=false
SKIP_MODEL=false
TARGET="usb"
ESP_PART=""         # --esp <partition> (e.g. /dev/nvme0n1p4) for --target esp; never a whole disk
DRY_RUN=false
WANT_BUILD=false    # set by --build

# ---------------------------------------------------------------------------
# Logging helpers
# ---------------------------------------------------------------------------
info()     { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()     { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error()    { echo -e "${RED}[ERROR]${NC} $*" >&2; }
step()     { echo -e "${CYAN}[STEP]${NC}  ${BOLD}$*${NC}"; }
dry_would(){ echo -e "${YELLOW}[dry-run] would:${NC} $*"; }

# ---------------------------------------------------------------------------
# Pure / sourceable helpers (exercised directly by the host test)
# ---------------------------------------------------------------------------

# jarvis_model_file — echo the JARVIS_MODEL_FILE value the rootserver expects,
# read straight from main_x86.c (single source of truth). For "GEMMA2B GUF".
jarvis_model_file() {
    local src="$PROJECT_ROOT/src/sel4/main_x86.c"
    if [ ! -f "$src" ]; then
        # Fall back to the repo path if PROJECT_ROOT isn't the live checkout.
        src="$REPO_PATH/phase3/src/sel4/main_x86.c"
    fi
    # Extract the text inside the quotes after `#define JARVIS_MODEL_FILE`.
    sed -n 's/.*#define[[:space:]]\+JARVIS_MODEL_FILE[[:space:]]\+"\([^"]*\)".*/\1/p' \
        "$src" 2>/dev/null | head -n1
}

# fat_8dot3 NAME — echo the FAT 8.3 short-name form of a basename, in the
# "NAME EXT" space-joined form the rootserver compares against (e.g.
# "GEMMA2B.GUF" -> "GEMMA2B GUF"). A name part > 8 chars or ext > 3 chars
# is NOT a valid 8.3 short name and yields an empty/mismatching result.
fat_8dot3() {
    local base name ext
    base="$(basename "$1")"
    base="$(printf '%s' "$base" | tr '[:lower:]' '[:upper:]')"
    if [[ "$base" == *.* ]]; then
        name="${base%.*}"
        ext="${base##*.}"
    else
        name="$base"
        ext=""
    fi
    # A real 8.3 short name has name<=8 and ext<=3. Anything longer is a
    # long filename (would be mangled with ~1 etc.) — reject by returning
    # a sentinel that cannot equal a valid "NAME EXT".
    if [ "${#name}" -gt 8 ] || [ "${#ext}" -gt 3 ]; then
        printf '%s' "__LFN__"
        return 0
    fi
    printf '%s %s' "$name" "$ext"
}

# jarvis_model_shortname_ok NAME — return 0 iff NAME's FAT 8.3 short name
# equals jarvis_model_file().
jarvis_model_shortname_ok() {
    local want got
    want="$(jarvis_model_file)"
    got="$(fat_8dot3 "$1")"
    [ -n "$want" ] && [ "$got" = "$want" ]
}

# jarvis_partition_start_ok PARTED_TEXT — return 0 iff the supplied
# `parted unit s print` text has a line naming JARVIS_DATA whose Start
# column is exactly 32768s.
jarvis_partition_start_ok() {
    printf '%s\n' "$1" | grep -E 'JARVIS_DATA' | grep -qE '(^|[[:space:]])32768s([[:space:]]|$)'
}

# root_fs_disk — echo the whole-disk device backing / (e.g. /dev/sda),
# or empty if it cannot be determined (e.g. in CI containers).
root_fs_disk() {
    local src pk
    src="$(findmnt -no SOURCE / 2>/dev/null || true)"
    [ -n "$src" ] || return 0
    pk="$(lsblk -no PKNAME "$src" 2>/dev/null || true)"
    if [ -n "$pk" ]; then
        printf '/dev/%s' "$pk"
        return 0
    fi
    # Fallback: strip a trailing partition number from the source path.
    # /dev/sda3 -> /dev/sda ; /dev/nvme0n1p2 -> /dev/nvme0n1
    if [[ "$src" =~ ^(/dev/nvme[0-9]+n[0-9]+)p[0-9]+$ ]]; then
        printf '%s' "${BASH_REMATCH[1]}"
    elif [[ "$src" =~ ^(/dev/[a-z]+)[0-9]+$ ]]; then
        printf '%s' "${BASH_REMATCH[1]}"
    fi
}

# jarvis_is_unsafe_usb_target DEV — return 0 (UNSAFE) if DEV is an NVMe
# device OR is the disk backing the root filesystem. The NVMe rule works
# even when findmnt/lsblk are unavailable (CI).
jarvis_is_unsafe_usb_target() {
    local dev="$1" root
    if [[ "$dev" == /dev/nvme* ]]; then
        return 0
    fi
    root="$(root_fs_disk)"
    if [ -n "$root" ] && [ "$dev" = "$root" ]; then
        return 0
    fi
    return 1
}

# derive_rootserver_image — echo the single rootserver image basename found
# in the build images dir. Fails loudly on 0 or >1 matches.
derive_rootserver_image() {
    local imgdir="$1"
    local matches=()
    local f
    # The seL4 'sel4test-driver' target produces *image*x86_64-pc99 but NOT
    # the bare kernel — exclude the kernel image explicitly.
    for f in "$imgdir"/*image*x86_64-pc99; do
        [ -e "$f" ] || continue
        [ "$(basename "$f")" = "$KERNEL_IMAGE" ] && continue
        matches+=("$(basename "$f")")
    done
    if [ "${#matches[@]}" -eq 0 ]; then
        error "No rootserver image (*image*x86_64-pc99) found in $imgdir"
        return 1
    fi
    if [ "${#matches[@]}" -gt 1 ]; then
        error "Ambiguous rootserver image — ${#matches[@]} matches in $imgdir: ${matches[*]}"
        return 1
    fi
    printf '%s' "${matches[0]}"
}

# ---------------------------------------------------------------------------
# Usage
# ---------------------------------------------------------------------------
usage() {
    cat <<EOF
${BOLD}JARVIS AI-OS x86-64 Installer v${VERSION}${NC}
Build + flash a JARVIS seL4 x86-64 boot USB and provision the NVMe model.

${BOLD}Usage:${NC}
    sudo $SCRIPT_NAME --usb /dev/sdX [OPTIONS]

${BOLD}Options:${NC}
    --build              Force a build via build_jarvis_x86.sh.
    --usb /dev/sdX       Boot USB target block device (required unless --skip-usb).
    --model <gguf>       Model GGUF to provision onto NVMe JARVIS_DATA
                         (default: <repo>/models/gemma-4-E2B-it-Q4_K_M.gguf).
    --nvme /dev/nvme0n1  NVMe device for the JARVIS_DATA model partition (default).
    --repo <path>        Repo root (default: \$HOME/Desktop/JARVIS_OS).
    --target {usb|esp|disk}  Install target (default: usb). 'esp' = reversible dual-boot
                         on the internal ESP (adds JARVIS alongside Ubuntu; never
                         repartitions/formats). 'disk' (full on-SSD) not yet implemented.
    --esp /dev/nvme0n1p4 Existing ESP PARTITION for --target esp (never a whole disk).
    --skip-build         Do not build; use existing images.
    --skip-usb           Do not write the boot USB.
    --skip-model         Do not touch the NVMe model partition.
    --dry-run            Print every destructive action ("[dry-run] would:") and do NONE.
    -h, --help           Show this help.

${BOLD}Examples:${NC}
    sudo $SCRIPT_NAME --build --usb /dev/sdb
    sudo $SCRIPT_NAME --usb /dev/sdb --model ~/models/gemma-4-E2B-it-Q4_K_M.gguf
    sudo $SCRIPT_NAME --target esp --esp /dev/nvme0n1p4   # reversible dual-boot, keeps Ubuntu
    $SCRIPT_NAME --dry-run --usb /dev/sdb --skip-build --skip-model

${BOLD}Dual-boot (--target esp):${NC}
    Adds a "JARVIS seL4" UEFI entry + an EFI/jarvis/ payload to the EXISTING internal
    ESP. Ubuntu stays the default; never repartitions, never touches EFI/ubuntu. Validate
    with a one-shot \`efibootmgr --bootnext\`; rollback by deleting EFI/jarvis + the entry.

${BOLD}Boot flow:${NC}
    UEFI firmware -> GRUB (multiboot2) -> seL4 kernel -> JARVIS rootserver
    -> model loaded from NVMe FAT32 JARVIS_DATA (sector 32768) at runtime.
EOF
    exit 0
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            --build)       WANT_BUILD=true ;;
            --usb)         shift; USB_DEV="${1:-}" ;;
            --model)       shift; MODEL_PATH="${1:-}" ;;
            --nvme)        shift; NVME_DEV="${1:-}" ;;
            --repo)        shift; REPO_PATH="${1:-}" ;;
            --target)      shift; TARGET="${1:-}" ;;
            --esp)         shift; ESP_PART="${1:-}" ;;
            --skip-build)  SKIP_BUILD=true ;;
            --skip-usb)    SKIP_USB=true ;;
            --skip-model)  SKIP_MODEL=true ;;
            --dry-run)     DRY_RUN=true ;;
            -h|--help)     usage ;;
            *)
                error "Unknown option: $1"
                echo "Run '$SCRIPT_NAME --help' for usage."
                exit 1
                ;;
        esac
        shift
    done

    # Build runs unless explicitly skipped (or when --build is given).
    if [ "$WANT_BUILD" = true ]; then
        DO_BUILD=true
    elif [ "$SKIP_BUILD" = true ]; then
        DO_BUILD=false
    else
        DO_BUILD=true
    fi

    # Default model path: repo-relative models/ Gemma GGUF.
    if [ -z "$MODEL_PATH" ]; then
        MODEL_PATH="$REPO_PATH/models/gemma-4-E2B-it-Q4_K_M.gguf"
    fi
}

# ---------------------------------------------------------------------------
# Target gate — only USB is implemented in v1.0.
# Exits BEFORE any device-safety refusals so the message is unambiguous.
# ---------------------------------------------------------------------------
check_target() {
    case "$TARGET" in
        usb)
            ;;
        esp)
            # Implemented: reversible dual-boot on the existing internal ESP
            # (additive; never repartitions). See guard_esp_device + write_boot_esp.
            ;;
        disk)
            error "on-SSD full install not yet implemented — only --target usb / esp supported in v1.0 (see memory installer-on-ssd-goal / ROADMAP)"
            exit 2
            ;;
        *)
            error "Unknown --target '$TARGET' (expected usb|esp|disk)"
            exit 2
            ;;
    esac
}

# ---------------------------------------------------------------------------
# Pre-flight checks. In --dry-run, missing build tree / tools / devices are
# WARNINGS (so it runs in CI with no box), otherwise hard errors.
# ---------------------------------------------------------------------------
BUILD_DIR=""
KERNEL_PATH=""
ROOTSERVER_IMAGE=""
ROOTSERVER_PATH=""
GRUB_CFG=""

preflight() {
    step "Pre-flight checks"

    local soft="$DRY_RUN"   # true => downgrade failures to warnings

    # Root check (only matters for real runs).
    if [ "$DRY_RUN" = false ] && [ "$(id -u)" -ne 0 ]; then
        error "Must run as root (sudo) for a real install."
        exit 1
    fi

    # Required host tools (target-aware: esp uses grub-mkstandalone + efibootmgr and
    # NEVER parted/mkfs — it adds to the EXISTING ESP, no repartition/format).
    local missing=()
    local cmd
    local tools=(lsblk)
    if [ "$TARGET" = "esp" ]; then
        tools+=(grub-mkstandalone efibootmgr mount)
    else
        tools+=(parted mkfs.fat grub-install)
    fi
    for cmd in "${tools[@]}"; do
        command -v "$cmd" &>/dev/null || missing+=("$cmd")
    done
    if [ "${#missing[@]}" -gt 0 ]; then
        if [ "$soft" = true ]; then
            warn "Missing tools (dry-run, continuing): ${missing[*]}"
        else
            error "Missing required tools: ${missing[*]}"
            echo "Install: sudo apt install parted dosfstools grub2-common grub-efi-amd64-bin util-linux"
            exit 1
        fi
    fi

    # Build tree.
    BUILD_DIR="${SEL4_BUILD_DIR:-$HOME/sel4-x86/jbuild}"
    local imgdir="$BUILD_DIR/images"
    KERNEL_PATH="$imgdir/$KERNEL_IMAGE"
    GRUB_CFG="$PROJECT_ROOT/firmware/grub/grub.cfg"
    if [ ! -f "$GRUB_CFG" ]; then
        GRUB_CFG="$REPO_PATH/phase3/firmware/grub/grub.cfg"
    fi

    if [ ! -d "$imgdir" ]; then
        if [ "$soft" = true ]; then
            warn "Build images dir not found (dry-run): $imgdir"
        else
            error "Build images dir not found: $imgdir"
            echo "Build first: bash $SCRIPT_DIR/build_jarvis_x86.sh   (or pass --skip-build with images present)"
            exit 1
        fi
    else
        if [ ! -f "$KERNEL_PATH" ] && [ "$soft" = false ]; then
            error "Kernel image not found: $KERNEL_PATH"
            exit 1
        fi
        # Derive the rootserver image name dynamically.
        if ROOTSERVER_IMAGE="$(derive_rootserver_image "$imgdir" 2>/dev/null)"; then
            ROOTSERVER_PATH="$imgdir/$ROOTSERVER_IMAGE"
            info "Rootserver image: $ROOTSERVER_IMAGE"
        else
            if [ "$soft" = true ]; then
                warn "Could not derive rootserver image (dry-run) from $imgdir"
            else
                derive_rootserver_image "$imgdir"   # re-run to surface the loud error
                exit 1
            fi
        fi
    fi

    if [ ! -f "$GRUB_CFG" ]; then
        if [ "$soft" = true ]; then
            warn "grub.cfg not found (dry-run): $GRUB_CFG"
        else
            error "grub.cfg not found: $GRUB_CFG"
            exit 1
        fi
    fi

    # USB device validation (only for the usb target, when we intend to write it).
    if [ "$TARGET" = "usb" ] && [ "$SKIP_USB" = false ]; then
        if [ -z "$USB_DEV" ]; then
            if [ "$soft" = true ]; then
                warn "No --usb device given (dry-run)."
            else
                error "No --usb device given (required unless --skip-usb)."
                exit 1
            fi
        fi
    fi

    # Model file presence (only when we intend to provision it).
    if [ "$SKIP_MODEL" = false ]; then
        if [ ! -f "$MODEL_PATH" ]; then
            if [ "$soft" = true ]; then
                warn "Model file not found (dry-run): $MODEL_PATH"
            else
                error "Model file not found: $MODEL_PATH"
                echo "Provide one with --model /path/to/model.gguf"
                exit 1
            fi
        fi
    fi

    info "Repo:        $REPO_PATH"
    info "Build dir:   $BUILD_DIR"
    if [ "$TARGET" = "esp" ]; then
        info "ESP (dual): ${ESP_PART:-<none>}  (target=$TARGET)"
    else
        info "Boot USB:    ${USB_DEV:-<none>} (target=$TARGET)"
    fi
    info "NVMe:        $NVME_DEV"
    info "Model:       $MODEL_PATH"
    info "Dry run:     $DRY_RUN"
    echo ""
}

# ---------------------------------------------------------------------------
# Device safety — runs even in --dry-run. Refuse NVMe / root-fs disk as a
# boot-USB target (does not apply to esp/disk, which exit earlier).
# ---------------------------------------------------------------------------
guard_usb_device() {
    [ "$SKIP_USB" = true ] && return 0
    [ -z "$USB_DEV" ] && return 0
    if jarvis_is_unsafe_usb_target "$USB_DEV"; then
        error "REFUSED: $USB_DEV is an NVMe or the root-filesystem disk — not a safe USB target."
        error "Use a removable USB stick (e.g. /dev/sdb)."
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# ESP dual-boot safety — runs even in --dry-run. The hard string checks (set?
# partition-not-whole-disk?) run always; the block-device / vfat reads and the
# operator double-confirm are real-run-only (CI/dry-run has no device or stdin).
# Unlike guard_usb_device, this DELIBERATELY accepts an NVMe path — but only a
# PARTITION, never a whole disk (we never repartition the internal drive).
# ---------------------------------------------------------------------------
guard_esp_device() {
    if [ -z "$ESP_PART" ]; then
        error "REFUSED: --target esp requires --esp <partition> (e.g. --esp /dev/nvme0n1p4)."
        exit 1
    fi

    # Must be a PARTITION path, never a whole disk — prevents any whole-disk op.
    if ! [[ "$ESP_PART" =~ ^/dev/(nvme[0-9]+n[0-9]+p[0-9]+|sd[a-z]+[0-9]+|mmcblk[0-9]+p[0-9]+)$ ]]; then
        error "REFUSED: --esp '$ESP_PART' is not a partition path (expected e.g. /dev/nvme0n1p4 or /dev/sdb1)."
        error "Pass the ESP PARTITION, never a whole disk — JARVIS never repartitions the internal drive."
        exit 1
    fi

    if [ "$DRY_RUN" = true ]; then
        dry_would "verify $ESP_PART is a vfat ESP (lsblk -no FSTYPE == vfat; best-effort esp-flag)"
        dry_would "DOUBLE-CONFIRM: operator re-types '$ESP_PART', then types 'ADD-JARVIS'"
        return 0
    fi

    [ -b "$ESP_PART" ] || { error "REFUSED: $ESP_PART is not a block device."; exit 1; }

    local fstype
    fstype="$(lsblk -no FSTYPE "$ESP_PART" 2>/dev/null | head -n1 | tr -d '[:space:]')"
    if [ "$fstype" != "vfat" ]; then
        error "REFUSED: $ESP_PART FSTYPE is '${fstype:-unknown}', not vfat — refusing to treat it as an ESP."
        exit 1
    fi
    # Best-effort esp/boot flag surfacing (PARTFLAGS is a raw GPT attr bitmask; the
    # hard gate is vfat + the operator confirm, since we never repartition).
    local pflags
    pflags="$(lsblk -no PARTFLAGS "$ESP_PART" 2>/dev/null | head -n1 || true)"
    [ -n "$pflags" ] && info "ESP $ESP_PART partition flags: $pflags"

    warn "About to ADD a JARVIS boot entry to the EXISTING ESP $ESP_PART."
    warn "This writes ONLY EFI/jarvis/* + one additive efibootmgr entry. It NEVER"
    warn "repartitions, NEVER formats, and NEVER touches EFI/BOOT or EFI/ubuntu. Ubuntu"
    warn "stays the default boot entry."
    local c1 c2
    read -r -p "Re-type the ESP partition to confirm ($ESP_PART): " c1
    [ "$c1" = "$ESP_PART" ] || { error "Confirmation mismatch — aborting."; exit 1; }
    read -r -p "Type ADD-JARVIS to proceed: " c2
    [ "$c2" = "ADD-JARVIS" ] || { error "Not confirmed — aborting."; exit 1; }
}

# ---------------------------------------------------------------------------
# Build step
# ---------------------------------------------------------------------------
run_build() {
    if [ "$DO_BUILD" = false ]; then
        info "Skipping build (--skip-build)."
        return 0
    fi
    step "Building seL4 x86-64 images"
    local build_sh="$SCRIPT_DIR/build_jarvis_x86.sh"
    if [ "$DRY_RUN" = true ]; then
        dry_would "run $build_sh $REPO_PATH"
        return 0
    fi
    if [ ! -x "$build_sh" ] && [ ! -f "$build_sh" ]; then
        error "build_jarvis_x86.sh not found: $build_sh"
        exit 1
    fi
    bash "$build_sh" "$REPO_PATH"
}

# ---------------------------------------------------------------------------
# stage_boot_payload ESP_MOUNT INSTALL_MODE
#   Copies the 2 seL4 images + grub.cfg to ESP_MOUNT/boot and runs
#   grub-install for UEFI. Shared by the USB path and (future) ESP path.
#   INSTALL_MODE is "usb" or "esp" (informational for grub-install target).
# ---------------------------------------------------------------------------
stage_boot_payload() {
    local esp_mount="$1" install_mode="$2" dev="$3"

    if [ "$DRY_RUN" = true ]; then
        dry_would "mkdir -p $esp_mount/boot/grub $esp_mount/EFI/BOOT"
        dry_would "cp $KERNEL_PATH -> $esp_mount/boot/$KERNEL_IMAGE"
        dry_would "cp $ROOTSERVER_PATH -> $esp_mount/boot/${ROOTSERVER_IMAGE:-<rootserver>}"
        dry_would "cp $GRUB_CFG -> $esp_mount/boot/grub/grub.cfg"
        dry_would "grub-install --target=x86_64-efi --efi-directory=$esp_mount --boot-directory=$esp_mount/boot --removable --recheck $dev"
        return 0
    fi

    mkdir -p "$esp_mount/boot/grub" "$esp_mount/EFI/BOOT"
    cp "$KERNEL_PATH" "$esp_mount/boot/$KERNEL_IMAGE"
    cp "$ROOTSERVER_PATH" "$esp_mount/boot/$ROOTSERVER_IMAGE"
    cp "$GRUB_CFG" "$esp_mount/boot/grub/grub.cfg"
    info "Staged: boot/$KERNEL_IMAGE, boot/$ROOTSERVER_IMAGE, boot/grub/grub.cfg ($install_mode)"

    grub-install \
        --target=x86_64-efi \
        --efi-directory="$esp_mount" \
        --boot-directory="$esp_mount/boot" \
        --removable \
        --recheck \
        "$dev" 2>&1 | sed 's/^/    /'
    info "GRUB (UEFI x86_64-efi) installed to $dev"
}

# ---------------------------------------------------------------------------
# Boot USB step — partition (GPT+ESP), format FAT32, stage payload.
# ---------------------------------------------------------------------------
USB_MNT=""
write_boot_usb() {
    if [ "$SKIP_USB" = true ]; then
        info "Skipping boot USB (--skip-usb)."
        return 0
    fi
    step "Writing boot USB to $USB_DEV"

    if [ -z "$USB_DEV" ]; then
        if [ "$DRY_RUN" = true ]; then
            warn "No --usb device (dry-run) — skipping boot-USB staging."
            return 0
        fi
        error "No --usb device specified."
        exit 1
    fi

    local part
    if [ "$DRY_RUN" = true ]; then
        dry_would "umount ${USB_DEV}* (any mounted partitions)"
        dry_would "parted -s $USB_DEV mklabel gpt"
        dry_would "parted -s $USB_DEV mkpart primary fat32 1MiB 100%"
        dry_would "parted -s $USB_DEV set 1 esp on"
        dry_would "mkfs.fat -F 32 -n JARVIS_BOOT ${USB_DEV}1"
        dry_would "mount ${USB_DEV}1 <tmp mount>"
        stage_boot_payload "<tmp mount>" "usb" "$USB_DEV"
        dry_would "sync && umount <tmp mount>"
        return 0
    fi

    # Real run.
    for p in $(lsblk -no PATH "$USB_DEV" 2>/dev/null | tail -n +2); do
        umount "$p" 2>/dev/null || true
    done
    parted -s "$USB_DEV" mklabel gpt
    parted -s "$USB_DEV" mkpart primary fat32 1MiB 100%
    parted -s "$USB_DEV" set 1 esp on
    partprobe "$USB_DEV" 2>/dev/null || true
    sleep 2

    part="${USB_DEV}1"
    [ -b "$part" ] || part="${USB_DEV}p1"
    if [ ! -b "$part" ]; then
        error "Boot partition did not appear: $part"
        lsblk "$USB_DEV"
        exit 1
    fi
    mkfs.fat -F 32 -n JARVIS_BOOT "$part"

    USB_MNT="$(mktemp -d)"
    mount "$part" "$USB_MNT"
    stage_boot_payload "$USB_MNT" "usb" "$USB_DEV"
    sync
    umount "$USB_MNT"
    rmdir "$USB_MNT"
    USB_MNT=""
    info "Boot USB written."
}

# ---------------------------------------------------------------------------
# add_jarvis_boot_entry DISK PARTNUM — create an ADDITIVE "JARVIS seL4" UEFI
# entry pointing at \EFI\jarvis\grubx64.efi, idempotently, then restore Ubuntu
# as BootOrder[0]. NEVER leaves JARVIS as the default. Real-run only.
# ---------------------------------------------------------------------------
JARVIS_BOOT_LABEL="JARVIS seL4"
add_jarvis_boot_entry() {
    local disk="$1" partnum="$2"

    # Idempotency: remove any existing JARVIS entry first.
    local id existing
    existing="$(efibootmgr 2>/dev/null | sed -n "s/^Boot\([0-9A-Fa-f]\{4\}\)\*\? .*${JARVIS_BOOT_LABEL}.*/\1/p")"
    local exarr=()
    if [ -n "$existing" ]; then read -ra exarr <<< "$existing"; fi
    for id in "${exarr[@]}"; do
        info "Removing existing boot entry Boot$id ($JARVIS_BOOT_LABEL)"
        efibootmgr -b "$id" -B >/dev/null 2>&1 || true
    done

    # Create (efibootmgr -c PREPENDS the new entry to BootOrder).
    if ! efibootmgr --create --disk "$disk" --part "$partnum" \
            --label "$JARVIS_BOOT_LABEL" --loader '\EFI\jarvis\grubx64.efi' >/dev/null 2>&1; then
        error "efibootmgr --create failed for '$JARVIS_BOOT_LABEL'"
        exit 1
    fi

    # Restore Ubuntu to the FRONT of BootOrder — JARVIS must never be the default.
    local ubuntu_id jarvis_id order rest neworder
    ubuntu_id="$(efibootmgr 2>/dev/null | sed -n 's/^Boot\([0-9A-Fa-f]\{4\}\)\*\? .*ubuntu.*/\1/p' | head -n1)"
    jarvis_id="$(efibootmgr 2>/dev/null | sed -n "s/^Boot\([0-9A-Fa-f]\{4\}\)\*\? .*${JARVIS_BOOT_LABEL}.*/\1/p" | head -n1)"
    order="$(efibootmgr 2>/dev/null | sed -n 's/^BootOrder: //p' | head -n1)"
    if [ -n "$ubuntu_id" ] && [ -n "$jarvis_id" ]; then
        rest="$(printf '%s' "$order" | tr ',' '\n' | grep -viE "^(${ubuntu_id}|${jarvis_id})$" | paste -sd, -)"
        neworder="${ubuntu_id},${jarvis_id}"
        [ -n "$rest" ] && neworder="${neworder},${rest}"
        efibootmgr -o "$neworder" >/dev/null 2>&1 || warn "Could not set BootOrder — check 'efibootmgr -v'."
        info "BootOrder = $neworder (Ubuntu first, JARVIS second — Ubuntu stays default)"
    else
        warn "Could not resolve ubuntu/JARVIS entry ids — BootOrder left as-is. VERIFY Ubuntu is still default ('efibootmgr -v')!"
    fi
}

# ---------------------------------------------------------------------------
# write_boot_esp — Option-A reversible DUAL-BOOT: add JARVIS to the EXISTING
# internal ESP. NO repartition, NO format, NO --removable, NO whole-disk
# grub-install. Writes ONLY EFI/jarvis/* + one additive efibootmgr entry;
# never touches EFI/BOOT or EFI/ubuntu (Ubuntu's shim/loaders).
# ---------------------------------------------------------------------------
ESP_MNT=""
write_boot_esp() {
    step "Adding JARVIS to the internal ESP $ESP_PART (dual-boot; Ubuntu untouched)"

    # Derive the whole disk + 1-based partition number from the ESP partition path.
    local esp_disk esp_partnum
    if [[ "$ESP_PART" =~ ^(/dev/nvme[0-9]+n[0-9]+)p([0-9]+)$ ]]; then
        esp_disk="${BASH_REMATCH[1]}"; esp_partnum="${BASH_REMATCH[2]}"
    elif [[ "$ESP_PART" =~ ^(/dev/mmcblk[0-9]+)p([0-9]+)$ ]]; then
        esp_disk="${BASH_REMATCH[1]}"; esp_partnum="${BASH_REMATCH[2]}"
    elif [[ "$ESP_PART" =~ ^(/dev/sd[a-z]+)([0-9]+)$ ]]; then
        esp_disk="${BASH_REMATCH[1]}"; esp_partnum="${BASH_REMATCH[2]}"
    else
        error "Could not derive disk/part number from $ESP_PART"; exit 1
    fi

    # The stale ESP-root model copy (a truncated 8.3 'GEMMA2B.GUF' that fills the ESP).
    # The REAL model lives on JARVIS_DATA (a DIFFERENT partition) and is never touched here.
    local stale_model="GEMMA2B.GUF"

    if [ "$DRY_RUN" = true ]; then
        dry_would "mount $ESP_PART <tmp> (EXISTING ESP — NO format, NO repartition)"
        dry_would "mkdir -p <esp>/EFI/jarvis/boot/grub"
        dry_would "cp $KERNEL_PATH -> <esp>/EFI/jarvis/boot/$KERNEL_IMAGE"
        dry_would "cp $ROOTSERVER_PATH -> <esp>/EFI/jarvis/boot/${ROOTSERVER_IMAGE:-<rootserver>}"
        dry_would "sed 's#/boot/#/EFI/jarvis/boot/#g' $GRUB_CFG -> <esp>/EFI/jarvis/boot/grub/grub.cfg"
        dry_would "grub-mkstandalone --format=x86_64-efi --output <esp>/EFI/jarvis/grubx64.efi (ONE JARVIS-owned file; never EFI/BOOT, never --removable, never whole-disk)"
        dry_would "if present, remove the STALE ESP-root copy <esp>/$stale_model (ESP copy ONLY — the real model on JARVIS_DATA is untouched)"
        dry_would "efibootmgr --create --disk $esp_disk --part $esp_partnum --label '$JARVIS_BOOT_LABEL' --loader '\\EFI\\jarvis\\grubx64.efi' (ADDITIVE)"
        dry_would "efibootmgr -o <ubuntu>,<jarvis>,<rest> — restore Ubuntu as BootOrder[0] (JARVIS never the default)"
        dry_would "sync && umount <tmp>"
        return 0
    fi

    # --- Real run ---
    ESP_MNT="$(mktemp -d)"
    mount "$ESP_PART" "$ESP_MNT"

    mkdir -p "$ESP_MNT/EFI/jarvis/boot/grub"
    cp "$KERNEL_PATH" "$ESP_MNT/EFI/jarvis/boot/$KERNEL_IMAGE"
    cp "$ROOTSERVER_PATH" "$ESP_MNT/EFI/jarvis/boot/$ROOTSERVER_IMAGE"
    # The committed grub.cfg uses ESP-root-relative /boot/... ; JARVIS lives under
    # /EFI/jarvis/boot/, so rewrite the image paths.
    sed 's#/boot/#/EFI/jarvis/boot/#g' "$GRUB_CFG" > "$ESP_MNT/EFI/jarvis/boot/grub/grub.cfg"
    info "Staged EFI/jarvis/boot/{$KERNEL_IMAGE, $ROOTSERVER_IMAGE, grub/grub.cfg}"

    # Stale ESP-root model cleanup (frees the otherwise-full ESP). ESP copy ONLY —
    # the real model on JARVIS_DATA (a different partition) is untouched.
    if [ -f "$ESP_MNT/$stale_model" ]; then
        warn "Removing the STALE ESP-root model copy /$stale_model ($(du -h "$ESP_MNT/$stale_model" 2>/dev/null | cut -f1)) — the real model on JARVIS_DATA is untouched."
        rm -f "$ESP_MNT/$stale_model"
    fi

    # Standalone JARVIS GRUB EFI — exactly ONE JARVIS-owned file. The embedded cfg
    # finds this ESP by the cfg file and chains to the real grub.cfg.
    local embcfg; embcfg="$(mktemp)"
    cat > "$embcfg" <<'EMBED'
search --no-floppy --file --set=root /EFI/jarvis/boot/grub/grub.cfg
configfile /EFI/jarvis/boot/grub/grub.cfg
EMBED
    grub-mkstandalone \
        --format=x86_64-efi \
        --output="$ESP_MNT/EFI/jarvis/grubx64.efi" \
        --modules="multiboot multiboot2 part_gpt fat normal echo all_video efi_gop efi_uga ls test configfile search search_fs_file" \
        "boot/grub/grub.cfg=$embcfg" 2>&1 | sed 's/^/    /'
    rm -f "$embcfg"
    info "Wrote EFI/jarvis/grubx64.efi (standalone; SHIMX64.EFI / EFI/ubuntu untouched)"

    sync
    umount "$ESP_MNT"; rmdir "$ESP_MNT"; ESP_MNT=""

    add_jarvis_boot_entry "$esp_disk" "$esp_partnum"
    info "JARVIS added to the ESP. Ubuntu remains the default boot entry."
}

# ---------------------------------------------------------------------------
# Model provisioning — delegate to setup_nvme_partition.sh (handles the
# detect-existing-JARVIS_DATA / sector-32768 / 8.3-name logic).
# ---------------------------------------------------------------------------
provision_model() {
    if [ "$SKIP_MODEL" = true ]; then
        info "Skipping model provisioning (--skip-model)."
        return 0
    fi
    step "Provisioning model onto NVMe ($NVME_DEV)"
    local setup_sh="$SCRIPT_DIR/setup_nvme_partition.sh"
    if [ "$DRY_RUN" = true ]; then
        dry_would "run $setup_sh --confirm --nvme $NVME_DEV --model $MODEL_PATH"
        dry_would "ensure JARVIS_DATA at sector 32768 holds GEMMA2B.GUF (8.3 = '$(jarvis_model_file)')"
        return 0
    fi
    if [ ! -f "$setup_sh" ]; then
        error "setup_nvme_partition.sh not found: $setup_sh"
        exit 1
    fi
    bash "$setup_sh" --confirm --nvme "$NVME_DEV" --model "$MODEL_PATH"
}

# ---------------------------------------------------------------------------
# Verify staged payload (best-effort).
# ---------------------------------------------------------------------------
verify() {
    step "Verifying"

    # --- ESP dual-boot verify ---
    if [ "$TARGET" = "esp" ]; then
        if [ "$DRY_RUN" = true ]; then
            dry_would "re-mount $ESP_PART read-only and confirm EFI/jarvis/grubx64.efi + boot images + grub.cfg"
            dry_would "confirm efibootmgr lists '$JARVIS_BOOT_LABEL' AND ubuntu is BootOrder[0]"
            return 0
        fi
        local vmnt f
        vmnt="$(mktemp -d)"
        if mount -o ro "$ESP_PART" "$vmnt" 2>/dev/null; then
            for f in "EFI/jarvis/grubx64.efi" "EFI/jarvis/boot/$KERNEL_IMAGE" "EFI/jarvis/boot/grub/grub.cfg"; do
                if [ -f "$vmnt/$f" ]; then info "OK: $f"; else error "MISSING: $f"; fi
            done
            if [ -n "$ROOTSERVER_IMAGE" ]; then
                if [ -f "$vmnt/EFI/jarvis/boot/$ROOTSERVER_IMAGE" ]; then
                    info "OK: EFI/jarvis/boot/$ROOTSERVER_IMAGE"
                else
                    error "MISSING: rootserver image on ESP"
                fi
            fi
            umount "$vmnt" 2>/dev/null || true
        fi
        rmdir "$vmnt" 2>/dev/null || true
        if command -v efibootmgr >/dev/null 2>&1; then
            local order0 uid
            order0="$(efibootmgr 2>/dev/null | sed -n 's/^BootOrder: //p' | head -n1 | cut -d, -f1)"
            uid="$(efibootmgr 2>/dev/null | sed -n 's/^Boot\([0-9A-Fa-f]\{4\}\)\*\? .*ubuntu.*/\1/p' | head -n1)"
            if [ -n "$uid" ] && [ "$order0" = "$uid" ]; then
                info "OK: ubuntu remains BootOrder[0] ($order0)"
            else
                warn "Ubuntu is NOT BootOrder[0] (first=$order0, ubuntu=$uid) — FIX before reboot!"
            fi
        fi
        return 0
    fi

    if [ "$DRY_RUN" = true ]; then
        dry_would "re-mount the boot USB read-only and confirm kernel + rootserver + grub.cfg present"
        dry_would "confirm JARVIS_DATA Start == 32768s via 'parted $NVME_DEV unit s print'"
        return 0
    fi
    local ok=true
    if [ "$SKIP_USB" = false ] && [ -n "$USB_DEV" ]; then
        local part="${USB_DEV}1"
        [ -b "$part" ] || part="${USB_DEV}p1"
        local vmnt
        vmnt="$(mktemp -d)"
        if mount -o ro "$part" "$vmnt" 2>/dev/null; then
            if [ -f "$vmnt/boot/$KERNEL_IMAGE" ]; then
                info "OK: boot/$KERNEL_IMAGE"
            else
                error "MISSING: boot/$KERNEL_IMAGE"; ok=false
            fi
            if [ -n "$ROOTSERVER_IMAGE" ] && [ -f "$vmnt/boot/$ROOTSERVER_IMAGE" ]; then
                info "OK: boot/$ROOTSERVER_IMAGE"
            else
                error "MISSING rootserver image on USB"; ok=false
            fi
            if [ -f "$vmnt/boot/grub/grub.cfg" ]; then
                info "OK: boot/grub/grub.cfg"
            else
                error "MISSING: boot/grub/grub.cfg"; ok=false
            fi
            umount "$vmnt" 2>/dev/null || true
        fi
        rmdir "$vmnt" 2>/dev/null || true
    fi
    if [ "$SKIP_MODEL" = false ]; then
        local ptext
        ptext="$(parted -s "$NVME_DEV" unit s print 2>/dev/null || true)"
        if jarvis_partition_start_ok "$ptext"; then
            info "OK: JARVIS_DATA starts at sector 32768"
        else
            warn "Could not confirm JARVIS_DATA Start == 32768s on $NVME_DEV"
        fi
    fi
    [ "$ok" = true ] || warn "One or more boot-USB checks failed — review above."
}

# ---------------------------------------------------------------------------
# BIOS + validation checklist
# ---------------------------------------------------------------------------
print_checklist() {
    echo ""
    echo -e "${GREEN}================================================${NC}"
    if [ "$DRY_RUN" = true ]; then
        echo -e "${GREEN}  DRY RUN COMPLETE${NC}"
    else
        echo -e "${GREEN}  JARVIS x86-64 INSTALL COMPLETE${NC}"
    fi
    echo -e "${GREEN}================================================${NC}"
    echo ""

    if [ "$TARGET" = "esp" ]; then
        echo -e "${BOLD}Dual-boot — JARVIS added to the internal ESP; Ubuntu kept as default:${NC}"
        echo "  1. Secure Boot -> 'Other OS' / non-enforcing (the JARVIS grubx64.efi is UNSIGNED)."
        echo "  2. Ubuntu remains BootOrder[0]; JARVIS was ADDED, never made the default."
        echo ""
        echo -e "${BOLD}Validate JARVIS once (does NOT change the default):${NC}"
        echo "  3. Find the entry id:   sudo efibootmgr | grep 'JARVIS seL4'"
        echo "  4. One-shot next boot:  sudo efibootmgr --bootnext <JARVIS_id> && sudo reboot"
        echo "     (boots JARVIS ONCE; the next reboot returns to Ubuntu automatically.)"
        echo "  5. Confirm boot + model load + queries from the NVMe telemetry log:"
        echo "       sudo dd if=${NVME_DEV} bs=512 skip=4000794624 count=2701 \\"
        echo "         | python3 phase3/scripts/parse_nvme_log.py"
        echo "  6. Confirm network telemetry on a LAN host:"
        echo "       python3 phase3/scripts/telemetry_receiver.py"
        echo "     (The model still loads from NVMe JARVIS_DATA @ sector 32768 — unchanged.)"
        echo ""
        echo -e "${BOLD}Rollback (removes JARVIS, leaves Ubuntu fully intact):${NC}"
        echo "       sudo efibootmgr -b <JARVIS_id> -B"
        echo "       sudo mount ${ESP_PART:-/dev/nvme0n1p4} /mnt && sudo rm -rf /mnt/EFI/jarvis && sudo umount /mnt"
        echo "       sudo efibootmgr -v | grep -i ubuntu     # ubuntu must remain BootOrder[0]"
        echo ""
        echo "See: phase3/docs/BARE_METAL_BOOT_GUIDE.md"
        echo ""
        return 0
    fi

    echo -e "${BOLD}BIOS / firmware setup (JARVIS PC):${NC}"
    echo "  1. Disable CSM (boot UEFI-only; CSM is sunset for v1.0)."
    echo "  2. Secure Boot -> set OS Type to 'Other OS' (disables Secure Boot enforcement)."
    echo "  3. Enable IOMMU / AMD-Vi (for safe device DMA isolation)."
    echo "  4. Set the 'UEFI: <USB stick>' entry as the first boot device."
    echo ""
    echo -e "${BOLD}Boot validation (headless-except-monitor — verify from the log):${NC}"
    echo "  5. Boot the USB; GRUB (multiboot2) loads the seL4 kernel + JARVIS rootserver."
    echo "  6. The rootserver loads the model from NVMe FAT32 JARVIS_DATA."
    echo "     The model partition MUST start at sector 32768 or it is silently not found."
    echo "  7. Read the NVMe telemetry log to confirm boot + model load + queries:"
    echo "       sudo dd if=${NVME_DEV} bs=512 skip=4000794624 count=2700 \\"
    echo "         | python3 phase3/scripts/parse_nvme_log.py"
    echo "  8. Confirm network telemetry (UDP broadcast) on a host on the LAN:"
    echo "       python3 phase3/scripts/telemetry_receiver.py"
    echo ""
    echo "See: phase3/docs/BARE_METAL_BOOT_GUIDE.md"
    echo ""
}

# ---------------------------------------------------------------------------
# Cleanup on exit
# ---------------------------------------------------------------------------
cleanup() {
    local m
    for m in "${USB_MNT:-}" "${ESP_MNT:-}"; do
        [ -n "$m" ] || continue
        if mountpoint -q "$m" 2>/dev/null; then umount "$m" 2>/dev/null || true; fi
        if [ -d "$m" ]; then rmdir "$m" 2>/dev/null || true; fi
    done
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    trap cleanup EXIT
    echo ""
    echo -e "${GREEN}================================================${NC}"
    echo -e "${GREEN}  JARVIS AI-OS x86-64 Installer v${VERSION}${NC}"
    echo -e "${GREEN}================================================${NC}"
    echo ""

    parse_args "$@"
    check_target          # gate target (disk still "not yet implemented") BEFORE device guards
    preflight
    if [ "$TARGET" = "esp" ]; then
        guard_esp_device  # ESP partition + ADD-JARVIS double-confirm (an NVMe PARTITION is allowed here)
        run_build
        write_boot_esp    # additive dual-boot on the EXISTING ESP; Ubuntu kept default
    else
        guard_usb_device  # device-safety refusals (runs in dry-run too)
        run_build
        write_boot_usb    # repartition + format USB, then stage_boot_payload()
    fi
    provision_model
    verify
    print_checklist
}

# Source-guard: allow the test to source helpers without executing main.
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
