#!/usr/bin/env bash
#
# bench_sel4_inference.sh — CANONICAL seL4-build inference tok/s measurement tool
# =============================================================================
# This is THE reproduction tool for the Phase 4 goal-#1 inference benchmark
# (Gemma 4 E2B on the bare-metal seL4 build). It distills the throwaway F1/F2
# N-sweep logic from M3 into a permanent, parameterized, self-cleaning script.
#
# It measures DECODE tok/s the same way M1/M3 did: Process B brackets the
# 50-token generation loop with RDTSC and emits "M1 gen=50 cyc=<cycles>" via
# MSG_DEBUG (flag JARVIS_M1_MEASURE). tok/s = 50 * TSC_HZ / cyc, with the
# Ryzen 2700X invariant TSC = 3.6999970 GHz (KVM -cpu host passes RDTSC
# straight through, so QEMU-KVM and bare metal use the same constant).
#
# Two model contexts (do NOT conflate — see PHASE_4_GOAL1_BENCHMARK.md):
#   - qemu      : QEMU-KVM relative-scaling range-finder. Use Llama-1B renamed
#                 to the model file on the NVMe image (Gemma load over emulated
#                 NVMe is too slow). Proves the pool scales with NUM_NODES.
#   - baremetal : the REAL deployed Gemma 4 E2B number. The only honest source
#                 of the production tok/s. No SSH during a run -> NVMe log capture.
#
# SAFETY: an EXIT trap ALWAYS resets JARVIS_M1_MEASURE=0 and
# JARVIS_DBG_BOOT_LOG=0 in the source header, so the box never lingers in a
# measurement config. You MUST still rebuild the deploy image clean afterward
# (the measurement image in jbuild/images has the flags compiled ON).
#
# CI: N/A — this drives the JARVIS PC (a kernel build + QEMU/bare-metal boot),
# not a host unit test. It cannot run on a GitHub runner.
#
# Run under a LOGIN shell so cmake/ninja are on PATH, e.g. over ssh:
#   ssh jarvis 'bash -lc "cd ~/Desktop/JARVIS_OS && \
#       bash phase3/scripts/bench_sel4_inference.sh 6 qemu"'
#
# Usage:  bench_sel4_inference.sh [NUM_NODES] [MODE]
#           NUM_NODES   SMP node count to build + measure (default 6)
#           MODE        qemu | baremetal                  (default baremetal)
# Env overrides:
#           JARVIS_REPO      box repo clone   (default ~/Desktop/JARVIS_OS)
#           SEL4_DIR         out-of-tree tree (default ~/sel4-x86)
#           JARVIS_NVME_IMG  qemu NVMe image  (default ~/nvme_test.img)
#           QEMU_TIMEOUT     seconds to run QEMU before capture (default 240)
# =============================================================================

set -uo pipefail

NUM_NODES="${1:-6}"
MODE="${2:-baremetal}"

JARVIS_REPO="${JARVIS_REPO:-$HOME/Desktop/JARVIS_OS}"
SEL4_DIR="${SEL4_DIR:-$HOME/sel4-x86}"
BUILD_DIR="$SEL4_DIR/jbuild"
NVME_IMG="${JARVIS_NVME_IMG:-$HOME/nvme_test.img}"
QEMU_TIMEOUT="${QEMU_TIMEOUT:-240}"

DBG_HEADER="$JARVIS_REPO/phase3/src/sel4/jarvis_debug.h"
BUILD_SH="$JARVIS_REPO/phase3/scripts/build_jarvis_x86.sh"
KERNEL_IMG="$BUILD_DIR/images/kernel-x86_64-pc99"
ROOTSERVER_IMG="$BUILD_DIR/images/sel4test-driver-image-x86_64-pc99"

# Ryzen 2700X invariant TSC. tok/s = GEN_TOKENS * TSC_HZ / cyc.
TSC_HZ="3.6999970e9"
GEN_TOKENS=50

# ---- flag flip helpers + self-cleaning EXIT trap --------------------------
# Match "#define JARVIS_<FLAG><ws><0|1>" regardless of the exact spacing.
set_flag() {
    sed -i -E "s/^(#define[[:space:]]+$1[[:space:]]+)[01]([[:space:]]*)$/\1$2\2/" "$DBG_HEADER"
}
get_flag() {
    grep -E "^#define[[:space:]]+$1[[:space:]]+[01]" "$DBG_HEADER" | grep -oE '[01][[:space:]]*$' | tr -d '[:space:]'
}

reset_measure_flags() {
    set_flag JARVIS_M1_MEASURE   0
    set_flag JARVIS_DBG_BOOT_LOG 0
    echo
    echo "[bench] EXIT: reset JARVIS_M1_MEASURE=0 + JARVIS_DBG_BOOT_LOG=0 in source"
    echo "[bench]       (source is back in stability/deploy config)."
    echo "[bench] REMINDER: the image in jbuild/images was built with measurement ON."
    echo "[bench]           Rebuild the deploy image clean before shipping:"
    echo "[bench]           JARVIS_NUM_NODES=$NUM_NODES bash phase3/scripts/build_jarvis_x86.sh $JARVIS_REPO"
}
trap reset_measure_flags EXIT

# ---- sanity --------------------------------------------------------------
if [[ ! -f "$DBG_HEADER" ]]; then
    echo "[bench] FATAL: cannot find $DBG_HEADER (set JARVIS_REPO to the box repo clone)" >&2
    exit 1
fi
if [[ "$MODE" != "qemu" && "$MODE" != "baremetal" ]]; then
    echo "[bench] FATAL: MODE must be 'qemu' or 'baremetal' (got '$MODE')" >&2
    exit 1
fi
echo "[bench] ============================================================"
echo "[bench] seL4-build inference benchmark   NUM_NODES=$NUM_NODES  MODE=$MODE"
echo "[bench] repo=$JARVIS_REPO  sel4=$SEL4_DIR"
echo "[bench] TSC=$TSC_HZ Hz   gen=$GEN_TOKENS tokens   tok/s = $GEN_TOKENS*TSC/cyc"
echo "[bench] ============================================================"

# ---- 1. enable measurement + build at NUM_NODES --------------------------
echo "[bench] enabling JARVIS_M1_MEASURE=1$( [[ "$MODE" == baremetal ]] && echo ' + JARVIS_DBG_BOOT_LOG=1 (NVMe capture)' )"
set_flag JARVIS_M1_MEASURE 1
[[ "$MODE" == "baremetal" ]] && set_flag JARVIS_DBG_BOOT_LOG 1

echo "[bench] M1_MEASURE=$(get_flag JARVIS_M1_MEASURE)  BOOT_LOG=$(get_flag JARVIS_DBG_BOOT_LOG)"
echo "[bench] building seL4 image at NUM_NODES=$NUM_NODES (config gate runs inside build_jarvis_x86.sh)..."
if ! JARVIS_NUM_NODES="$NUM_NODES" bash "$BUILD_SH" "$JARVIS_REPO"; then
    echo "[bench] FATAL: build failed (or config-verification gate aborted)." >&2
    exit 1
fi
echo "[bench] build OK — config gate asserted CONFIG_MAX_NUM_NODES=$NUM_NODES + XSAVE/AVX/SMP invariants."

# ---- 2a. QEMU-KVM relative-scaling measurement ---------------------------
if [[ "$MODE" == "qemu" ]]; then
    if [[ ! -f "$NVME_IMG" ]]; then
        echo "[bench] FATAL: NVMe image $NVME_IMG missing. Create a FAT32 image whose" >&2
        echo "[bench]        model file (name = JARVIS_MODEL_FILE) is Llama-1B for fast load." >&2
        exit 1
    fi
    LOG="$(mktemp /tmp/jarvis_bench_qemu.XXXXXX.log)"
    echo "[bench] booting QEMU-KVM -smp $NUM_NODES (timeout ${QEMU_TIMEOUT}s) -> $LOG"
    echo "[bench] NOTE: qemu_test_nvme.sh ships WITHOUT -smp; this tool adds -smp $NUM_NODES so workers actually run."
    timeout "${QEMU_TIMEOUT}" qemu-system-x86_64 \
        -enable-kvm -cpu host -smp "$NUM_NODES" -m 8G \
        -kernel "$KERNEL_IMG" \
        -initrd "$ROOTSERVER_IMG" \
        -drive "file=$NVME_IMG,if=none,id=nvme0,format=raw" \
        -device "nvme,serial=QEMU_NVME,drive=nvme0" \
        -nographic -no-reboot > "$LOG" 2>&1 || true

    echo "[bench] QEMU exited; parsing 'M1 gen=$GEN_TOKENS cyc=' samples..."
    python3 - "$LOG" "$TSC_HZ" "$GEN_TOKENS" "$NUM_NODES" <<'PY'
import sys, re
log, hz, n, nn = sys.argv[1], float(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
txt = open(log, errors='replace').read()
cyc = [int(c) for c in re.findall(r'M1 gen=%d cyc=(\d+)' % n, txt)]
if not cyc:
    print("[bench] NO 'M1 gen=%d cyc=' samples found — check boot / model load / -smp." % n)
    sys.exit(2)
ts = [hz * n / c for c in cyc]
print("[bench] RESULT NN=%s  samples=%d  avg=%.2f  min=%.2f  max=%.2f tok/s"
      % (nn, len(ts), sum(ts)/len(ts), min(ts), max(ts)))
print("[bench] (QEMU = relative scaling only; the production number is MODE=baremetal with the real Gemma model.)")
PY
    rm -f "$LOG"
    exit 0
fi

# ---- 2b. bare-metal procedure (no SSH during a run -> NVMe log) -----------
cat <<EOF

[bench] ===================== BARE-METAL PROCEDURE =====================
[bench] The measurement image (NUM_NODES=$NUM_NODES, M1_MEASURE + BOOT_LOG ON)
[bench] is built at: $BUILD_DIR/images/
[bench]   kernel     = $KERNEL_IMG
[bench]   rootserver = $ROOTSERVER_IMG   (NUM_NODES-independent; reads numNodes at runtime)
[bench]
[bench] 1. Reflash the USB in place (mount /dev/sda1; copy kernel+rootserver to /boot/;
[bench]    verify the KERNEL hash changed — the rootserver hash does NOT change with NN):
[bench]      sha256sum $KERNEL_IMG
[bench] 2. Physically boot the box from USB (GRUB). VGA console only (X470-F has no serial).
[bench] 3. Let it run past at least one [STATS] line (q>=100), then reboot into Ubuntu.
[bench]    Multiple NN runs separate by boot_id in the log.
[bench] 4. Read back + recompute tok/s from the NVMe telemetry log:
[bench]      sudo dd if=/dev/nvme0n1 bs=512 skip=4000794624 count=2700 \\
[bench]        | python3 $JARVIS_REPO/phase3/scripts/parse_nvme_log.py
[bench]    Each inference logs an "M1 gen=$GEN_TOKENS cyc=<cycles>" line (LOG_INFER).
[bench]    tok/s = $GEN_TOKENS * $TSC_HZ / cyc   (avg/min/max over the lines for this boot_id).
[bench] 5. ARCHIVE the raw log + the [STATS] err= rate, THEN rebuild the DEPLOY image clean
[bench]    (this script already reset the source flags on exit):
[bench]      JARVIS_NUM_NODES=$NUM_NODES bash phase3/scripts/build_jarvis_x86.sh $JARVIS_REPO
[bench]    and reflash so the box ships in the stability config (M1_MEASURE=0, BOOT_LOG=0).
[bench] ================================================================
[bench] Recorded production result (2026-06-18): Gemma 4 E2B 5.46 tok/s @ NUM_NODES=6,
[bench] err=0 over 800 queries, parallel output byte-identical to serial. See
[bench] phase4/docs/PHASE_4_GOAL1_BENCHMARK.md.
EOF
exit 0
