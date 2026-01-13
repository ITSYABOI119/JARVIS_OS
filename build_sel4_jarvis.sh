#!/bin/bash
# Build seL4 with JARVIS Decision Cache Integration
set -e

echo "=========================================="
echo "Building seL4 + JARVIS with Decision Cache"
echo "=========================================="

cd ~/jarvis-phase1
echo "[1/6] Cleaning old build..."
rm -rf hello-world_jarvis_build
echo "✓ Clean complete"

echo "[2/6] Initializing seL4..."
./init --plat pc99 --tut hello-world
mv hello-world_build hello-world_jarvis_build
cd hello-world_jarvis_build
echo "✓ seL4 initialized"

echo "[3/6] Copying JARVIS sources..."
JARVIS_SRC="/mnt/c/Users/jluca/Documents/JARVIS_OS/phase1/src"
mkdir -p hello-world
cp "/cache/decision_cache.c" hello-world/
cp "/cache/decision_cache.h" hello-world/
cp "/cache/cache_patterns.c" hello-world/
cp "/ipc/ring_buffer.c" hello-world/
cp "/ipc/ring_buffer.h" hello-world/
cp "/ipc/ipc_sel4.c" hello-world/
cp "/ipc/ipc_sel4.h" hello-world/
cp "/sel4/main.c" hello-world/
echo "✓ Sources copied"

echo "[4/6] Configuring CMakeLists.txt..."
cat > hello-world/CMakeLists.txt << 'EOFCMAKE'
include(/settings.cmake)
sel4_tutorials_regenerate_tutorial()
include(rootserver)
include(simulation)

add_executable(hello-world
    main.c
    decision_cache.c
    cache_patterns.c
    ring_buffer.c
    ipc_sel4.c
)

target_include_directories(hello-world PRIVATE )
target_link_libraries(hello-world sel4 muslc utils sel4muslcsys sel4platsupport sel4utils sel4debug)
DeclareRootserver(hello-world)
EOFCMAKE
echo "✓ CMakeLists.txt configured"

echo "[5/6] Building (this takes 1-2 minutes)..."
ninja
echo "✓ Build complete"

echo "[6/6] Verifying..."
ls -lh images/hello-world-image-x86_64-pc99 simulate

echo ""
echo "✅ BUILD SUCCESSFUL"
echo ""
echo "To run: cd ~/jarvis-phase1/hello-world_jarvis_build && ./simulate"
