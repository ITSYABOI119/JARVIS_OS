#!/bin/bash
set -e

JARVIS_ROOT="/mnt/c/Users/jluca/Documents/JARVIS_OS"
JARVIS_PROJECT="/home/itsme/sel4-workspace/projects/jarvis-sel4"
BUILD_DIR="/home/itsme/sel4-workspace/rpi4_jarvis"

echo "=== Syncing JARVIS sources ==="
mkdir -p "$JARVIS_PROJECT/src"
rm -rf "$JARVIS_PROJECT/src/cache" "$JARVIS_PROJECT/src/ipc" "$JARVIS_PROJECT/src/ipc_phase2" "$JARVIS_PROJECT/src/drivers" 2>/dev/null || true

cp -r "$JARVIS_ROOT/phase1/src/cache" "$JARVIS_PROJECT/src/"
cp -r "$JARVIS_ROOT/phase1/src/ipc" "$JARVIS_PROJECT/src/"
cp -r "$JARVIS_ROOT/phase2/src/drivers" "$JARVIS_PROJECT/src/"
cp -r "$JARVIS_ROOT/phase2/src/ipc" "$JARVIS_PROJECT/src/ipc_phase2"
cp "$JARVIS_ROOT/phase2/src/sel4/main_arm64.c" "$JARVIS_PROJECT/src/main.c"
cp "$JARVIS_ROOT/phase2/src/jarvis-sel4-cmake/CMakeLists.txt" "$JARVIS_PROJECT/"

echo "Sources synced. Checking version:"
grep "Phase 2 Week" "$JARVIS_PROJECT/src/main.c" | head -2

echo ""
echo "=== Building kernel ==="
cd "$BUILD_DIR"

# Clean and rebuild
ninja clean 2>/dev/null || true
ninja

echo ""
echo "=== Build complete ==="
ls -lh "$BUILD_DIR/images/"

echo ""
echo "=== Copying to firmware directory ==="
cp "$BUILD_DIR/images/jarvis-sel4-image-arm-bcm2711" "$JARVIS_ROOT/phase2/firmware/kernel8.img"
ls -lh "$JARVIS_ROOT/phase2/firmware/kernel8.img"

echo ""
echo "Done! Copy kernel8.img to D: drive"
