#!/bin/bash
#
# JARVIS AI-OS — Sync Sources & Build seL4 x86-64
#
# Copies JARVIS source files from the repo into the seL4 build tree,
# then runs ninja to rebuild. Run on JARVIS PC (Ubuntu).
#
# Usage:
#   ./build_jarvis_x86.sh [path-to-JARVIS_OS]
#   ./build_jarvis_x86.sh --dry-run
#   ./build_jarvis_x86.sh ~/Desktop/JARVIS_OS --dry-run
#

set -euo pipefail

# ── Colors ──────────────────────────────────────────────────────────
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# ── Defaults ────────────────────────────────────────────────────────
DRY_RUN=0
JARVIS_DIR=""

# ── Parse arguments ─────────────────────────────────────────────────
for arg in "$@"; do
    case "$arg" in
        --dry-run)
            DRY_RUN=1
            ;;
        -h|--help)
            echo "Usage: $0 [path-to-JARVIS_OS] [--dry-run]"
            echo ""
            echo "Syncs JARVIS sources into the seL4 build tree and rebuilds."
            echo ""
            echo "Arguments:"
            echo "  path-to-JARVIS_OS   Repository root (default: \$HOME/Desktop/JARVIS_OS)"
            echo ""
            echo "Options:"
            echo "  --dry-run           Show what would be copied without copying"
            echo "  -h, --help          Show this help"
            exit 0
            ;;
        -*)
            echo -e "${RED}Unknown option: $arg${NC}"
            exit 1
            ;;
        *)
            if [ -z "$JARVIS_DIR" ]; then
                JARVIS_DIR="$arg"
            else
                echo -e "${RED}Unexpected argument: $arg${NC}"
                exit 1
            fi
            ;;
    esac
done

# Apply defaults
JARVIS_DIR="${JARVIS_DIR:-$HOME/Desktop/JARVIS_OS}"
SEL4_DIR="$HOME/sel4-x86"
DEST="$SEL4_DIR/projects/jarvis-x86/apps/sel4test-driver"

# ── Banner ──────────────────────────────────────────────────────────
echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  JARVIS AI-OS — seL4 x86-64 Build${NC}"
echo -e "${GREEN}================================================${NC}"
echo ""

if [ "$DRY_RUN" -eq 1 ]; then
    echo -e "${YELLOW}*** DRY RUN — no files will be copied or built ***${NC}"
    echo ""
fi

echo -e "${BOLD}JARVIS repo:${NC}  $JARVIS_DIR"
echo -e "${BOLD}seL4 tree:${NC}    $SEL4_DIR"
echo -e "${BOLD}Destination:${NC}  $DEST"
echo ""

# ── Verify directories ─────────────────────────────────────────────
if [ ! -d "$JARVIS_DIR" ]; then
    echo -e "${RED}Error: JARVIS repo not found: $JARVIS_DIR${NC}"
    echo "Provide the path as an argument:"
    echo "  $0 /path/to/JARVIS_OS"
    exit 1
fi

if [ ! -d "$SEL4_DIR" ]; then
    echo -e "${RED}Error: seL4 build tree not found: $SEL4_DIR${NC}"
    echo "See phase3/docs/SEL4_X86_QEMU_SETUP.md for initial setup."
    exit 1
fi

# ── Helper: copy a file, creating dest dir as needed ────────────────
COPY_COUNT=0

copy_file() {
    local src="$1"
    local dst="$2"

    if [ ! -f "$src" ]; then
        echo -e "  ${YELLOW}SKIP${NC}  $src  (not found)"
        return
    fi

    if [ "$DRY_RUN" -eq 1 ]; then
        echo -e "  ${CYAN}WOULD COPY${NC}  $src"
        echo -e "            -> $dst"
    else
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
        echo -e "  ${GREEN}COPIED${NC}  $(basename "$src")"
    fi
    COPY_COUNT=$((COPY_COUNT + 1))
}

# ── [1/4] Rootserver ────────────────────────────────────────────────
echo -e "${GREEN}[1/4] Rootserver${NC}"
copy_file "$JARVIS_DIR/phase3/src/sel4/main_x86.c" "$DEST/src/main.c"
copy_file "$JARVIS_DIR/phase3/src/sel4/jarvis_debug.h" "$DEST/src/jarvis_debug.h"
copy_file "$JARVIS_DIR/phase3/src/sel4/avx2_probe.h" "$DEST/src/avx2_probe.h"
copy_file "$JARVIS_DIR/phase3/src/sel4/smp_probe.h" "$DEST/src/smp_probe.h"
echo ""

# ── [2/4] AI modules ───────────────────────────────────────────────
echo -e "${GREEN}[2/4] AI modules${NC}"

AI_SRC="$JARVIS_DIR/phase3/src/ai"
AI_DST="$DEST/src/ai"

AI_FILES=(
    "decision_cache.c"    "decision_cache.h"
    "cache_patterns.c"    "cache_patterns.h"
    "tensor_ops.c"        "tensor_ops.h"
    "dequant.c"           "dequant.h"
    "tokenizer.c"         "tokenizer.h"
    "sampling.c"          "sampling.h"
    "gguf_parser.c"       "gguf_parser.h"
    "shield.c"            "shield.h"
    "llama_model.h"
    "llama_quant.c"       "llama_quant.h"
    "llama_load.c"
    "llama_forward.c"
    "inference.c"         "inference.h"
    "gguf_vocab.c"        "gguf_vocab.h"
    "ssm.c"               "ssm.h"
    "qdot.c"              "qdot.h"
    "threadpool.h"
)

for f in "${AI_FILES[@]}"; do
    copy_file "$AI_SRC/$f" "$AI_DST/$f"
done
echo ""

# ── [3/4] IPC modules ──────────────────────────────────────────────
echo -e "${GREEN}[3/4] IPC modules${NC}"

IPC_DST="$DEST/src/ipc"

# Phase 3 IPC
IPC3_SRC="$JARVIS_DIR/phase3/src/ipc"
IPC3_FILES=("shmem_ipc.c" "shmem_ipc.h" "ipc_transport.h")

for f in "${IPC3_FILES[@]}"; do
    copy_file "$IPC3_SRC/$f" "$IPC_DST/$f"
done

# Phase 1 IPC
IPC1_SRC="$JARVIS_DIR/phase1/src/ipc"
IPC1_FILES=("ring_buffer.c" "ring_buffer.h" "sel4_atomics.h")

for f in "${IPC1_FILES[@]}"; do
    copy_file "$IPC1_SRC/$f" "$IPC_DST/$f"
done

# Phase 2 IPC
IPC2_SRC="$JARVIS_DIR/phase2/src/ipc"
IPC2_FILES=("dual_ring_buffer.c" "dual_ring_buffer.h" "ipc_handler.c" "ipc_handler.h")

for f in "${IPC2_FILES[@]}"; do
    copy_file "$IPC2_SRC/$f" "$IPC_DST/$f"
done
echo ""

# ── [4/4] Drivers + inference server ───────────────────────────────
echo -e "${GREEN}[4/4] Drivers${NC}"

DRV_SRC="$JARVIS_DIR/phase3/src/drivers"
DRV_DST="$DEST/src/drivers"

copy_file "$DRV_SRC/vga_text.c" "$DRV_DST/vga_text.c"
copy_file "$DRV_SRC/vga_text.h" "$DRV_DST/vga_text.h"
copy_file "$DRV_SRC/pci.c" "$DRV_DST/pci.c"
copy_file "$DRV_SRC/pci.h" "$DRV_DST/pci.h"
copy_file "$DRV_SRC/nvme.c" "$DRV_DST/nvme.c"
copy_file "$DRV_SRC/nvme.h" "$DRV_DST/nvme.h"
copy_file "$DRV_SRC/nvme_log.c" "$DRV_DST/nvme_log.c"
copy_file "$DRV_SRC/nvme_log.h" "$DRV_DST/nvme_log.h"
copy_file "$DRV_SRC/fat32.c" "$DRV_DST/fat32.c"
copy_file "$DRV_SRC/fat32.h" "$DRV_DST/fat32.h"

# Inference server (Process B — lives in jarvis-inference app, NOT sel4test-driver)
PROC_B_DIR="$SEL4_DIR/projects/jarvis-x86/apps/jarvis-inference/src"
mkdir -p "$PROC_B_DIR/ai" 2>/dev/null || true
copy_file "$JARVIS_DIR/phase3/src/sel4/inference_server.c" "$PROC_B_DIR/main.c"
copy_file "$JARVIS_DIR/phase3/src/sel4/jarvis_debug.h" "$PROC_B_DIR/jarvis_debug.h"
copy_file "$JARVIS_DIR/phase3/src/sel4/avx2_probe.h" "$PROC_B_DIR/avx2_probe.h"
copy_file "$JARVIS_DIR/phase3/src/sel4/smp_probe.h" "$PROC_B_DIR/smp_probe.h"

# Process B needs its own copy of AI modules (separate compilation unit)
for f in "${AI_FILES[@]}"; do
    copy_file "$AI_SRC/$f" "$PROC_B_DIR/ai/$f"
done

# Process B also needs IPC headers
copy_file "$IPC3_SRC/shmem_ipc.h" "$PROC_B_DIR/ai/shmem_ipc.h"
copy_file "$IPC3_SRC/shmem_ipc.c" "$PROC_B_DIR/ai/shmem_ipc.c"
echo ""

# ── [5/5] Patch CMakeLists.txt ─────────────────────────────────────
echo -e "${GREEN}[5/5] Patching CMakeLists.txt${NC}"

CMAKE_FILE="$DEST/CMakeLists.txt"

if [ -f "$CMAKE_FILE" ]; then
    PATCHED=0

    # Add src/drivers/vga_text.c to source list if missing
    if ! grep -q "src/drivers/vga_text.c" "$CMAKE_FILE"; then
        # Insert after the last src/ipc line in add_executable
        sed -i '/src\/ipc\/.*\.c/{ $!{N; /src\/ipc\/.*\.c[^.]*$/{ s/$/\n    src\/drivers\/vga_text.c/; }} }' "$CMAKE_FILE" 2>/dev/null
        # Fallback: if sed didn't match, try simpler pattern
        if ! grep -q "src/drivers/vga_text.c" "$CMAKE_FILE"; then
            sed -i '/^)$/{ x; /PATCHED_VGA/!{ x; /add_executable/,/^)$/{ /^)$/i\    src/drivers/vga_text.c
            }; x; s/$/PATCHED_VGA/; x; }; x; }' "$CMAKE_FILE" 2>/dev/null
        fi
        # Final fallback: add before the closing paren of add_executable
        if ! grep -q "src/drivers/vga_text.c" "$CMAKE_FILE"; then
            sed -i '0,/^)/{s/^)/    src\/drivers\/vga_text.c\n)/}' "$CMAKE_FILE"
        fi
        if grep -q "src/drivers/vga_text.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/vga_text.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add vga_text.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/vga_text.c already in source list"
    fi

    # Add src/drivers/pci.c to source list if missing
    if ! grep -q "src/drivers/pci.c" "$CMAKE_FILE"; then
        sed -i '/src\/drivers\/vga_text.c/a\    src/drivers/pci.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/drivers/pci.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/pci.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add pci.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/pci.c already in source list"
    fi

    # Add src/drivers/nvme.c to source list if missing
    if ! grep -q "src/drivers/nvme.c" "$CMAKE_FILE"; then
        sed -i '/src\/drivers\/pci.c/a\    src/drivers/nvme.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/drivers/nvme.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/nvme.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add nvme.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/nvme.c already in source list"
    fi

    # Add src/drivers/nvme_log.c to source list if missing
    if ! grep -q "src/drivers/nvme_log.c" "$CMAKE_FILE"; then
        sed -i '/src\/drivers\/nvme.c/a\    src/drivers/nvme_log.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/drivers/nvme_log.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/nvme_log.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add nvme_log.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/nvme_log.c already in source list"
    fi

    # Add src/drivers/fat32.c to source list if missing
    if ! grep -q "src/drivers/fat32.c" "$CMAKE_FILE"; then
        sed -i '/src\/drivers\/nvme_log.c/a\    src/drivers/fat32.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/drivers/fat32.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/fat32.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add fat32.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/fat32.c already in source list"
    fi

    # Add JARVIS_SEL4 compile definition (needed for pci.c IOPort backend)
    if ! grep -q "JARVIS_SEL4" "$CMAKE_FILE"; then
        sed -i '/target_compile_options/a\target_compile_definitions(sel4test-driver PRIVATE JARVIS_SEL4)' "$CMAKE_FILE" 2>/dev/null
        if grep -q "JARVIS_SEL4" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  JARVIS_SEL4 compile definition"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add JARVIS_SEL4 — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  JARVIS_SEL4 already defined"
    fi

    # Add src/drivers to include directories if missing
    if ! grep -q '"src/drivers"' "$CMAKE_FILE"; then
        sed -i 's|"src/ipc"|"src/ipc" "src/drivers"|' "$CMAKE_FILE"
        if grep -q '"src/drivers"' "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers to include directories"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add include path — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers already in include directories"
    fi

    [ "$PATCHED" -eq 0 ] && echo -e "  ${CYAN}OK${NC}  CMakeLists.txt already up to date"
else
    echo -e "  ${RED}WARN${NC}  CMakeLists.txt not found at $CMAKE_FILE"
fi
echo ""

# ── [6/6] AVX2 app compile flags (Phase 4 goal #1, M1) ───────────────
# Enable -mavx2 -mfma -mf16c on BOTH app targets so the engine's #ifdef __AVX2__
# kernels (qdot.c, tensor_ops.c, llama_quant.c) light up automatically — no engine
# source change. Safe ONLY because M0 rebuilt the kernel to XSAVE (it context-switches
# the AVX YMM state); do NOT enable AVX2 on an FXSAVE kernel.
# NOTE: -mavx2 makes the engine #include <immintrin.h>, but the seL4 app build does NOT
# search gcc's intrinsics-header dir — so we add it via -isystem (musl still supplies
# libc headers). The dir is computed from gcc so it tracks the toolchain version.
# The sed normalizes the whole option list (idempotent + self-correcting if a prior
# run left bare -mavx2 without the -isystem path).
echo -e "${GREEN}[6/6] AVX2 compile flags${NC}"
AVX2_FLAGS="-mavx2 -mfma -mf16c"
GCC_AVX_INC="$(gcc -print-file-name=include 2>/dev/null)"
if [ -n "$GCC_AVX_INC" ] && [ -f "$GCC_AVX_INC/immintrin.h" ]; then
    AVX2_FLAGS="$AVX2_FLAGS -isystem $GCC_AVX_INC"
else
    echo -e "  ${YELLOW}WARN${NC}  gcc immintrin.h dir not found — AVX2 build will fail (need -isystem <gcc include>)"
fi
PB_CMAKE="$SEL4_DIR/projects/jarvis-x86/apps/jarvis-inference/CMakeLists.txt"
for pair in "sel4test-driver:$CMAKE_FILE" "jarvis-inference:$PB_CMAKE"; do
    app="${pair%%:*}"; cmf="${pair#*:}"
    if [ ! -f "$cmf" ]; then
        echo -e "  ${YELLOW}SKIP${NC}  $app CMakeLists not found ($cmf)"
    elif grep -qE "target_compile_options\($app PRIVATE -Wall -g -O2" "$cmf"; then
        sed -i "s|target_compile_options($app PRIVATE -Wall -g -O2[^)]*)|target_compile_options($app PRIVATE -Wall -g -O2 $AVX2_FLAGS)|" "$cmf"
        echo -e "  ${GREEN}SET${NC}  $app: -Wall -g -O2 $AVX2_FLAGS"
    else
        echo -e "  ${YELLOW}SKIP${NC}  $app — target_compile_options not in expected form; add $AVX2_FLAGS manually"
    fi
done
echo ""

# ── Summary ─────────────────────────────────────────────────────────
echo -e "${GREEN}────────────────────────────────────────────────${NC}"
echo -e "${BOLD}Files synced:${NC} $COPY_COUNT"
echo ""

if [ "$DRY_RUN" -eq 1 ]; then
    echo -e "${YELLOW}Dry run complete. No files were copied.${NC}"
    exit 0
fi

# ── Build ───────────────────────────────────────────────────────────
echo -e "${GREEN}Building...${NC}"
echo ""

BUILD_DIR="$SEL4_DIR/jbuild"
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${RED}Error: Build directory not found: $BUILD_DIR${NC}"
    echo "Run cmake first — see phase3/docs/SEL4_X86_QEMU_SETUP.md"
    exit 1
fi

# Force KernelIOMMU OFF (NVMe needs direct physical DMA; no VT-d). The jarvis-x86 project
# settings.cmake (inherited from sel4test) force-sets IOMMU ON for non-simulation x86 builds,
# which -DKernelIOMMU=OFF on the cmake line CANNOT override. Under SIMULATION=ON the sim block
# forced it OFF for us; with SIMULATION=OFF (needed for AVX/XSAVE) that force is gone, so we
# patch the project settings directly (idempotent).
JX_SETTINGS="$SEL4_DIR/projects/jarvis-x86/settings.cmake"
if [ -f "$JX_SETTINGS" ] && grep -q 'set(KernelIOMMU ON CACHE BOOL "" FORCE)' "$JX_SETTINGS"; then
    sed -i 's/set(KernelIOMMU ON CACHE BOOL "" FORCE)/set(KernelIOMMU OFF CACHE BOOL "" FORCE)  # JARVIS: NVMe needs direct DMA (no VT-d)/' "$JX_SETTINGS"
    echo -e "${GREEN}Patched KernelIOMMU ON->OFF in jarvis-x86/settings.cmake${NC}"
fi

# Kernel config — force-set every build (cmake reconfiguration can reset defaults):
#   - KernelIOMMU=OFF: NVMe DMA needs direct physical access.
#   - SMP (Phase 4 goal #1 M2): -DSMP=ON -DNUM_NODES=2 => CONFIG_ENABLE_SMP_SUPPORT
#     + CONFIG_MAX_NUM_NODES=2 (BSP + 1 AP). jarvis-x86/settings.cmake reads the SMP
#     and NUM_NODES cache vars and FORCE-sets KernelMaxNumNodes from them, so
#     -DKernelMaxNumNodes alone is overridden (drive it via SMP/NUM_NODES). This is
#     the multicore foundation for the M3 threadpool — NO inference speedup yet (PA
#     and PB still run on node 0 until M3 wires SetAffinity). Further departs the
#     verified X64 config (already unverified by design — KernelFastpath=ON; ADR
#     2026-06-16), so it is a maturity call, not a proof forfeit. Production N is
#     chosen at M3 after bandwidth testing — keep 2 here (minimal cross-core test).
#   - XSAVE/AVX (Phase 4 goal #1 M0): KernelFPU=XSAVE + feature-set 7 (FPU+SSE+AVX)
#     + size 832 so the kernel context-switches the AVX YMM state for Ring-3 code.
#     Without this the kernel is FXSAVE-only (512B, x87+SSE only) and AVX2 in
#     userspace corrupts YMM across preemption. XSAVEOPT is the variant Zen+ (2700X)
#     supports. NOTE: this is the kernel's save/restore capability only — it does NOT
#     turn on -mavx2 in the engine apps (that is M1); the M0 probe is target()-isolated.
cd "$BUILD_DIR"
# Pass 1: flip the kernel to XSAVE FPU mode (so it context-switches AVX YMM state).
# SIMULATION=OFF is REQUIRED — the seL4 cmake-tool's ApplyCommonSimulationSettings()
# force-sets KernelFPU=FXSAVE under SIMULATION ("cannot simulate some more recent
# features" — TCG QEMU has no AVX), which silently overrides any XSAVE request. JARVIS
# targets KVM -cpu host / bare metal (real AVX2), so simulation mode must be off.
cmake -DKernelIOMMU=OFF \
      -DSIMULATION=OFF \
      -DKernelFPU=XSAVE \
      -DKernelXSave=XSAVEOPT \
      -DKernelXSaveFeatureSet=7 \
      -DKernelXSaveSize=832 \
      -DSMP=ON \
      -DNUM_NODES=2 \
      . >/dev/null 2>&1
# Pass 2: KernelXSaveFeatureSet DEPENDS on KernelFPUXSave, which only flips ON during
# pass 1; on the pass where it first becomes selectable it takes its default (3 =
# FPU+SSE). Re-assert =7 (FPU+SSE+AVX) now that it is available, else AVX is excluded.
cmake -DKernelXSaveFeatureSet=7 -DKernelXSaveSize=832 . >/dev/null 2>&1
ninja

echo ""

# ── Verify kernel config (Phase 4 M2 SMP + preserved M0/M1) ─────────────
# A silent wrong-config build is the exact trap that bit IOMMU and XSAVE before
# (a -D that did not "take"). Abort loudly if SMP did not enable, or if an M0/M1
# invariant regressed. Source of truth = the generated kernel gen_config.h.
GEN_CFG="$BUILD_DIR/kernel/gen_config/kernel/gen_config.h"
if [ -f "$GEN_CFG" ]; then
    echo -e "${GREEN}Kernel config verification:${NC}"
    cfg_fail=0
    check_cfg() {  # check_cfg "<grep -E pattern>" "<label>"
        if grep -Eq "$1" "$GEN_CFG"; then
            echo -e "  ${GREEN}OK${NC}    $2"
        else
            echo -e "  ${RED}FAIL${NC}  $2"
            cfg_fail=1
        fi
    }
    check_cfg '#define CONFIG_MAX_NUM_NODES[[:space:]]+2'      "SMP: CONFIG_MAX_NUM_NODES=2 (BSP + 1 AP)   [M2]"
    check_cfg '#define CONFIG_ENABLE_SMP_SUPPORT[[:space:]]+1' "SMP: CONFIG_ENABLE_SMP_SUPPORT=1           [M2]"
    check_cfg '#define CONFIG_XSAVE[[:space:]]+1'              "FPU: CONFIG_XSAVE=1 (kernel saves AVX YMM) [M0]"
    check_cfg '#define CONFIG_XSAVE_FEATURE_SET[[:space:]]+7'  "FPU: XSAVE feature-set 7 (FPU+SSE+AVX)     [M0]"
    check_cfg '/\* disabled: CONFIG_FXSAVE \*/'                "FPU: FXSAVE disabled                       [M0]"
    check_cfg '/\* disabled: CONFIG_IOMMU \*/'                 "IOMMU disabled (NVMe direct DMA)           [M1]"
    check_cfg '#define CONFIG_FASTPATH[[:space:]]+1'           "KernelFastpath=ON (unverified by design)"
    if [ "$cfg_fail" -ne 0 ]; then
        echo -e "${RED}Kernel config verification FAILED — not the intended M2 config. Aborting.${NC}"
        exit 1
    fi
    echo -e "  ${GREEN}All kernel config invariants hold (M2 SMP enabled; M0/M1 preserved).${NC}"
else
    echo -e "  ${YELLOW}WARN${NC}  gen_config.h not found at $GEN_CFG — cannot verify kernel config"
fi
echo ""

# ── Image sizes ─────────────────────────────────────────────────────
IMAGES_DIR="$BUILD_DIR/images"
if [ -d "$IMAGES_DIR" ]; then
    echo -e "${GREEN}────────────────────────────────────────────────${NC}"
    echo -e "${BOLD}Build output:${NC}"
    ls -lh "$IMAGES_DIR"/ 2>/dev/null | while read -r line; do
        echo "  $line"
    done
    echo ""
fi

echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  BUILD COMPLETE${NC}"
echo -e "${GREEN}================================================${NC}"
