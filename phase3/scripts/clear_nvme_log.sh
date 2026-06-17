#!/bin/bash
#
# JARVIS AI-OS — Reset the NVMe telemetry log region.
#
# This is the JARVIS NVMe telemetry log region (nvme_log.c/h). The log does NOT
# wrap, caps at NVME_LOG_MAX_ENTRIES, and its cursor + boot_id PERSIST across boots.
# Zeroing the header + entry sectors makes nvme_log_init() start fresh on the next
# bare-metal boot (boot_id=1, cursor=0). LBA + count are DERIVED from nvme_log.h so
# they cannot drift from the firmware's constants.
#
# ARCHIVE FIRST — this is destructive to the log region:
#   sudo dd if=/dev/nvme0n1 bs=512 skip=<base> count=<1+MAX> of=~/nvme_log_archive.bin
#   sudo dd if=/dev/nvme0n1 bs=512 skip=<base> count=<1+MAX> | \
#       python3 phase3/scripts/parse_nvme_log.py > ~/nvme_log_archive.txt
# (use the LBA range this script prints).
#
# Usage:
#   sudo bash clear_nvme_log.sh [/dev/nvme0n1]          # dry run (prints range)
#   sudo bash clear_nvme_log.sh [/dev/nvme0n1] --yes    # actually zero it
#
set -euo pipefail

DEV="/dev/nvme0n1"
CONFIRM=0
for a in "$@"; do
    case "$a" in
        --yes)   CONFIRM=1 ;;
        /dev/*)  DEV="$a" ;;
        *) echo "Unknown arg: $a"; exit 1 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HDR="$SCRIPT_DIR/../src/drivers/nvme_log.h"
[ -f "$HDR" ] || { echo "ERROR: nvme_log.h not found at $HDR"; exit 1; }

# Derive constants from the header (no magic numbers — can't drift from firmware).
BASE=$(grep -oE '#define[[:space:]]+NVME_LOG_BASE_LBA[[:space:]]+[0-9]+' "$HDR" | grep -oE '[0-9]+$')
MAXE=$(grep -oE '#define[[:space:]]+NVME_LOG_MAX_ENTRIES[[:space:]]+[0-9]+' "$HDR" | grep -oE '[0-9]+$')
[ -n "${BASE:-}" ] && [ -n "${MAXE:-}" ] || { echo "ERROR: could not parse NVME_LOG_BASE_LBA / NVME_LOG_MAX_ENTRIES from $HDR"; exit 1; }
COUNT=$((MAXE + 1))      # 1 header sector + MAX_ENTRIES entry sectors
END=$((BASE + COUNT))    # exclusive end LBA

[ -b "$DEV" ] || { echo "ERROR: $DEV is not a block device"; exit 1; }
DEVSZ=$(blockdev --getsz "$DEV")   # device size in 512-byte sectors

echo "JARVIS NVMe telemetry log reset"
echo "  device:        $DEV ($DEVSZ sectors)"
echo "  log base LBA:  $BASE        (nvme_log.h NVME_LOG_BASE_LBA)"
echo "  sectors:       $COUNT       (1 header + $MAXE entries)"
echo "  LBA range:     [$BASE .. $((END - 1))]  (exclusive end $END)"
echo "  device end:    $DEVSZ       (headroom after log: $((DEVSZ - END)) sectors)"

# Safety: the log region MUST lie entirely past the last partition, within the device.
if [ "$END" -gt "$DEVSZ" ]; then
    echo "ERROR: log end LBA ($END) exceeds device size ($DEVSZ) — refusing to write."
    exit 1
fi

if [ "$CONFIRM" -ne 1 ]; then
    echo
    echo "Dry run (no --yes). Re-run with --yes to ZERO the range above."
    exit 0
fi

echo
echo "Zeroing $COUNT sectors at LBA $BASE ..."
dd if=/dev/zero of="$DEV" bs=512 seek="$BASE" count="$COUNT" conv=fsync status=progress
sync
echo "Done. Next bare-metal boot starts a fresh telemetry log (boot_id=1, cursor=0)."
