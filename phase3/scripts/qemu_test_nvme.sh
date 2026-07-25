#!/bin/bash
# ============================================================================
# qemu_test_nvme.sh — DEPRECATED SHIM. Delegates to qemu_test.sh.
#
# This script used to build a throwaway FAT32 image sized to the MODEL (~3 GB)
# and boot it with no -smp. Both were traps:
#
#   1. NO -smp  => numNodes==1, so the M3 worker pool never engaged and any
#      conclusion about threading, respawn/rejoin or worker faults was void.
#   2. IMAGE TOO SMALL => the raw-LBA stores start at LBA 21,100,000 (10.06 GiB).
#      A ~3 GB image cannot contain them, so episodic / semantic / JACT /
#      control-key / control-IN ALL silently failed to init. Any memory or audit
#      behaviour "tested" on that image was testing nothing.
#
# It also defaulted to a Llama-1B path, not the deployed Gemma. Rather than keep
# a second, wrong, reachable implementation (see qemu_test.sh's header on why two
# implementations drift), this now forwards to the canonical script, which takes
# a PERSISTENT, correctly-sized image via --image.
#
# Migrate to:  bash phase3/scripts/qemu_test.sh [--image PATH] [--log FILE]
# ============================================================================
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "NOTE: qemu_test_nvme.sh is DEPRECATED — delegating to qemu_test.sh." >&2
echo "      (the old behaviour booted without -smp and built an image too small" >&2
echo "       to contain the raw-LBA stores; see this file's header)." >&2

# The old first positional argument was a MODEL path. It is no longer meaningful:
# the canonical script boots a persistent image that already carries the model as
# GEMMA2B.GUF. Refuse rather than silently ignore it.
if [ $# -gt 0 ] && [ "${1#-}" = "$1" ]; then
    echo "ERROR: this script no longer takes a model path ('$1')." >&2
    echo "       The model lives on the NVMe image as GEMMA2B.GUF; pass the image:" >&2
    echo "         bash phase3/scripts/qemu_test.sh --image /path/to/nvme.img" >&2
    exit 2
fi

exec bash "$HERE/qemu_test.sh" "$@"
