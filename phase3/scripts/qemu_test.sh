#!/bin/bash
# ============================================================================
# qemu_test.sh — THE canonical KVM boot for JARVIS seL4 x86-64.
#
# This script IS the invocation every recent box gate uses. It was previously
# NOT: it passed no -smp and attached no NVMe drive at all, handing the model to
# seL4 as an initrd module — a path CLAUDE.md records as ABANDONED ("seL4
# overwrites module memory during rootserver relocation"). A future session
# running it got a non-SMP, no-NVMe, no-model boot, saw green, and would have
# drawn a false conclusion. Fixed 2026-07-25.
#
# What matters, and why:
#   -smp N        The deployed build is SMP with CONFIG_MAX_NUM_NODES=6 and the
#                 M3 threadpool pins N-1 workers to distinct cores. Boot without
#                 -smp and numNodes==1: the worker pool never engages, so any
#                 conclusion about threading, respawn/rejoin or per-worker faults
#                 is meaningless. N is DERIVED from the built kernel config below.
#   NVMe drive    The model is loaded at RUNTIME from FAT32 over the NVMe driver.
#                 No drive => no model => Process B idles.
#   -snapshot     Writes go to a COW overlay, so a gate never mutates the shared
#                 image and two gates can run concurrently against it. Opt out
#                 with --no-snapshot when you deliberately want persistence
#                 (e.g. a 2-boot store-survival test).
#
# IMAGE SIZE IS LOAD-BEARING. The raw-LBA stores live far past the FAT32 volume:
#   episodic 21,100,000 (10.06 GiB) · semantic 21,110,000 · JACT 21,120,000
#   control-key 21,130,000 · control-IN 21,140,000 (10.08 GiB)
# An image smaller than ~10.1 GiB cannot hold them, and every store silently
# fails to init — which is exactly what the old qemu_test_nvme.sh produced, since
# it generated an image sized to the MODEL (~3 GB). This script refuses instead.
# NOTE the durable telemetry log lives at LBA 4,000,794,624 (1907 GiB) and is
# therefore NOT reachable in KVM at any practical image size — read it on the box.
#
# Usage:
#   bash phase3/scripts/qemu_test.sh                       # defaults, interactive
#   bash phase3/scripts/qemu_test.sh --log /tmp/run.log    # detached-friendly
#   bash phase3/scripts/qemu_test.sh --image ~/other.img --smp 2 --no-snapshot
#
# DETACHED (how long gates are actually run — a plain nohup/setsid dies with the ssh session):
#   sudo systemd-run --unit=jarvis-gate --collect --property=PrivateTmp=no \
#        --property=Environment=HOME=/home/jarvis \
#        /usr/bin/timeout 7200 /usr/bin/bash .../qemu_test.sh --log /home/jarvis/gate.log
# The Environment=HOME is REQUIRED: `sudo systemd-run` runs as root, so this script's default
# paths would otherwise resolve under /root. Pass it, or pass --image and SEL4_BUILD_DIR
# explicitly.
# ============================================================================
set -euo pipefail

# systemd-run (how detached gates are launched) starts services with NO $HOME, and `set -u` turns
# that into "HOME: unbound variable" before anything runs — i.e. the script would fail precisely in
# the mode --log exists for. Resolve it from passwd when the environment does not supply it.
: "${HOME:=$(getent passwd "$(id -u)" | cut -d: -f6)}"

BUILD="${SEL4_BUILD_DIR:-$HOME/sel4-x86/jbuild}"
KERNEL="$BUILD/images/kernel-x86_64-pc99"
ROOTSERVER="$BUILD/images/sel4test-driver-image-x86_64-pc99"
IMAGE="${JARVIS_QEMU_IMAGE:-$HOME/nvme_test.img}"
MEM="${JARVIS_QEMU_MEM:-8G}"
SNAPSHOT=1
LOGFILE=""
SMP=""

while [ $# -gt 0 ]; do
    case "$1" in
        --image)       IMAGE="$2"; shift 2 ;;
        --log)         LOGFILE="$2"; shift 2 ;;
        --smp)         SMP="$2"; shift 2 ;;
        --mem)         MEM="$2"; shift 2 ;;
        --no-snapshot) SNAPSHOT=0; shift ;;
        -h|--help)     sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "ERROR: unknown argument '$1' (see --help)"; exit 2 ;;
    esac
done

if [ ! -f "$KERNEL" ] || [ ! -f "$ROOTSERVER" ]; then
    echo "ERROR: build images not found in $BUILD/images/"
    echo "       run build_jarvis_x86.sh first."
    exit 1
fi

# --- SMP: derive from the BUILT kernel config, do not hardcode -------------
# The build script already asserts CONFIG_MAX_NUM_NODES == NUM_NODES per build;
# reading the same value here is what keeps the runner from drifting from the
# thing it runs. Fall back to 6 (the deployed default) only if unreadable.
if [ -z "$SMP" ]; then
    GENCFG="$BUILD/kernel/gen_config/kernel/gen_config.h"
    if [ -f "$GENCFG" ]; then
        SMP=$(grep -oE '#define[[:space:]]+CONFIG_MAX_NUM_NODES[[:space:]]+[0-9]+' "$GENCFG" \
              | grep -oE '[0-9]+$' || true)
    fi
    [ -n "$SMP" ] || SMP=6
fi

# --- image checks: exist, and big enough to CONTAIN the raw-LBA stores ------
if [ ! -f "$IMAGE" ]; then
    echo "ERROR: NVMe image not found: $IMAGE"
    echo "       Provide one with --image, or create a >=11 GiB FAT32 image and"
    echo "       copy the model onto it as GEMMA2B.GUF (8.3 name matters)."
    exit 1
fi
IMG_BYTES=$(stat -c%s "$IMAGE")
NEED_BYTES=$(( 21140000 * 512 + 4097 * 512 ))   # control-IN store end = the deepest KVM-reachable store
if [ "$IMG_BYTES" -lt "$NEED_BYTES" ]; then
    echo "ERROR: image is $((IMG_BYTES / 1048576)) MiB — too small for the raw-LBA stores."
    echo "       They start at LBA 21,100,000 (10.06 GiB); this image cannot hold them, so"
    echo "       episodic / semantic / JACT / control-key / control-IN would ALL silently fail"
    echo "       to init and any memory or audit result from this run would be meaningless."
    echo "       Need >= $((NEED_BYTES / 1048576)) MiB."
    exit 1
fi

KVM_OPTS=""
if [ -e /dev/kvm ]; then
    KVM_OPTS="-enable-kvm -cpu host"
else
    echo "WARNING: /dev/kvm absent — falling back to emulation (inference will be very slow)."
fi

if [ -n "$LOGFILE" ]; then
    SERIAL=(-serial "file:$LOGFILE")
else
    SERIAL=(-serial mon:stdio)
    echo "Exit: Ctrl-A then X"
fi
SNAP=(); [ "$SNAPSHOT" -eq 1 ] && SNAP=(-snapshot)

echo "Booting JARVIS seL4 x86-64 under QEMU"
echo "  kernel   : $KERNEL"
echo "  rootsrv  : $ROOTSERVER"
echo "  nvme     : $IMAGE ($((IMG_BYTES / 1048576)) MiB)"
echo "  smp      : $SMP $([ -f "${BUILD}/kernel/gen_config/kernel/gen_config.h" ] && echo '(from kernel gen_config)' || echo '(default)')"
echo "  mem      : $MEM"
echo "  snapshot : $([ "$SNAPSHOT" -eq 1 ] && echo 'on (image not modified)' || echo 'OFF (writes PERSIST)')"
[ -n "$LOGFILE" ] && echo "  serial   : $LOGFILE"
echo ""

exec qemu-system-x86_64 \
    ${KVM_OPTS:--cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce} \
    -smp "$SMP" -nographic "${SERIAL[@]}" -m "$MEM" "${SNAP[@]}" \
    -kernel "$KERNEL" \
    -initrd "$ROOTSERVER" \
    -drive file="$IMAGE",if=none,id=nvme0,format=raw \
    -device nvme,serial=QEMU_NVME,drive=nvme0
