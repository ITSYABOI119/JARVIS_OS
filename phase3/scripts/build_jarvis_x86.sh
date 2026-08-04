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
copy_file "$JARVIS_DIR/phase3/src/sel4/jarvis_ui_tokens.h" "$DEST/src/jarvis_ui_tokens.h"
copy_file "$JARVIS_DIR/phase3/src/sel4/avx2_probe.h" "$DEST/src/avx2_probe.h"
copy_file "$JARVIS_DIR/phase3/src/sel4/smp_probe.h" "$DEST/src/smp_probe.h"
# Phase C / C/M2b Leg A: the generated measured-pair table. PROBE-ONLY (the header
# self-guards on JARVIS_EMBED_PROBE), so a deployed build carries none of its strings.
copy_file "$JARVIS_DIR/phase3/src/sel4/embed_gate_pairs.h" "$DEST/src/embed_gate_pairs.h"
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
    "threadpool_sel4.c"
    "episodic_store.c"    "episodic_store.h"
    "shared_context.c"    "shared_context.h"
    "g3_retrieval.c"      "g3_retrieval.h"
    "cache_growth.c"      "cache_growth.h"
    "shield_learn.c"      "shield_learn.h"
    "semantic_store.c"    "semantic_store.h"
    "semantic_distill.c"  "semantic_distill.h"
    "action_allowlist.c"  "action_allowlist.h"
    "shield_action.c"     "shield_action.h"
    "action_audit.c"      "action_audit.h"
    "km2b_trigger.c"      "km2b_trigger.h"
    "km2b_miss.c"         "km2b_miss.h"
    "km2b_fault.c"        "km2b_fault.h"
    "monitors.c"          "monitors.h"
    "wake.c"              "wake.h"
    "behaviors.c"         "behaviors.h"
    "query_shield.c"      "query_shield.h"
    "hostile_queries.h"   "benign_queries.h"
    "route.c"             "route.h"
    # Phase C / C/M2a+C/M2b: the vector store, the frozen mu, and the projection.
    # embed_mu.h is GENERATED (phase3/scripts/embed/gen_embed_mu.py) — copied, never edited here.
    "embed_store.c"       "embed_store.h"
    "embed_project.c"     "embed_project.h"
    "embed_mu.h"
    # Phase C / C/M4: the measured routing veto. route_centroids.h is GENERATED
    # (phase3/scripts/embed/gen_route_centroids.py) and FROZEN — copied, never edited here;
    # regenerating it invalidates the parity evidence and the probe-leg expectations.
    "route_veto.c"        "route_veto.h"
    "route_centroids.h"
    # Phase 6 N2: the PB liveness-tick wait verdict (host-pure; call sites gated JARVIS_PB_TICK)
    "pb_progress.c"       "pb_progress.h"
    # Phase 6 A9/1: control-IN recall-index maintenance (host-pure, host-tested; storage stays in PA)
    "ctrl_epi_index.c"    "ctrl_epi_index.h"
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
IPC3_FILES=("shmem_ipc.c" "shmem_ipc.h" "ipc_transport.h"
            "embed_region.c" "embed_region.h")   # Phase C / C/M1b-3: embed transport region

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
copy_file "$DRV_SRC/framebuffer.c" "$DRV_DST/framebuffer.c"
copy_file "$DRV_SRC/framebuffer.h" "$DRV_DST/framebuffer.h"
# Step 2c-2b: framebuffer.c #includes jarvis_ui_tokens.h — make it resolvable in src/drivers/ too
copy_file "$JARVIS_DIR/phase3/src/sel4/jarvis_ui_tokens.h" "$DRV_DST/jarvis_ui_tokens.h"
# goal #2b N-a: Intel I211 NIC driver (TX first-light)
copy_file "$DRV_SRC/nic_i211.c" "$DRV_DST/nic_i211.c"
copy_file "$DRV_SRC/nic_i211.h" "$DRV_DST/nic_i211.h"
# goal #2b N-b: Eth/IPv4/UDP broadcast frame builder
copy_file "$DRV_SRC/net_udp.c" "$DRV_DST/net_udp.c"
copy_file "$DRV_SRC/net_udp.h" "$DRV_DST/net_udp.h"
# goal #2b N-c: binary telemetry packet (CRC-32 + finalize)
copy_file "$DRV_SRC/jarvis_telemetry.c" "$DRV_DST/jarvis_telemetry.c"
copy_file "$DRV_SRC/jarvis_telemetry.h" "$DRV_DST/jarvis_telemetry.h"

# ── Phase 6 6-5/M2a: control-IN security core (GATED — synced ONLY when the header
# flag JARVIS_CONTROL_IN=1; keeps the OFF deploy image clean of any dead crypto). The
# flag values are read once here and reused by the CMake patch in [5/5]. ──
DBG_HDR="$JARVIS_DIR/phase3/src/sel4/jarvis_debug.h"
CONTROL_IN=$(awk '/^#define JARVIS_CONTROL_IN /{print $3; exit}' "$DBG_HDR" 2>/dev/null)
CONTROL_IN_PROBE=$(awk '/^#define JARVIS_CONTROL_IN_PROBE /{print $3; exit}' "$DBG_HDR" 2>/dev/null)
[ -z "$CONTROL_IN" ] && CONTROL_IN=0
[ -z "$CONTROL_IN_PROBE" ] && CONTROL_IN_PROBE=0
if [ "$CONTROL_IN" = "1" ]; then
    echo -e "${GREEN}Control-IN security core (JARVIS_CONTROL_IN=1)${NC}"
    CRYPTO_SRC="$JARVIS_DIR/phase3/src/crypto"; CRYPTO_DST="$DEST/src/crypto"
    NET_SRC="$JARVIS_DIR/phase3/src/net";       NET_DST="$DEST/src/net"
    mkdir -p "$CRYPTO_DST" "$NET_DST" 2>/dev/null || true
    for f in sha256.c sha256.h hmac_sha256.c hmac_sha256.h; do
        copy_file "$CRYPTO_SRC/$f" "$CRYPTO_DST/$f"
    done
    for f in control_msg.h control_mailbox.h control_parser.c control_parser.h control_replay.c control_replay.h \
             control_ratelimit.c control_ratelimit.h control_verify.c control_verify.h control_key.h \
             control_floor.c control_floor.h control_console.c control_console.h \
             control_reply.c control_reply.h; do
        copy_file "$NET_SRC/$f" "$NET_DST/$f"
    done

    # ── 6-5/M2b-1: the SEC-014 jarvis-input parser process (a sibling CPIO app to
    # jarvis-inference). It compiles ONLY control_parser + control_ratelimit (parse +
    # ratelimit; zero NIC caps, no key). Created ONLY under the gate; the else-branch
    # rm's the whole app so an OFF build produces no jarvis-input ELF. ──
    INPUT_APP_DIR="$SEL4_DIR/projects/jarvis-x86/apps/jarvis-input"
    copy_file "$JARVIS_DIR/phase3/src/sel4/input_server.c" "$INPUT_APP_DIR/src/main.c"
    for f in control_msg.h control_mailbox.h control_parser.c control_parser.h \
             control_ratelimit.c control_ratelimit.h; do
        copy_file "$NET_SRC/$f" "$INPUT_APP_DIR/src/net/$f"
    done
    if [ "$DRY_RUN" -eq 0 ]; then
        cat > "$INPUT_APP_DIR/CMakeLists.txt" << 'JARVIS_INPUT_CMAKE'
#
# JARVIS AI-OS — Control-IN Input Parser Process (jarvis-input, SEC-014)
#
# Phase 6 goal 6-5 / M2b-1. Least-privileged third seL4 process: runs ONLY the
# untrusted inbound parse + rate-limit (control_parser + control_ratelimit) — zero
# NIC caps, no HMAC key, no request/response ring. Built as a standalone ELF, packed
# into the rootserver CPIO next to jarvis-inference. Included via add_subdirectory
# from the rootserver CMakeLists (seL4 packages are already imported there — do NOT
# find_package here). This file is generated by build_jarvis_x86.sh under the
# JARVIS_CONTROL_IN=1 gate; the OFF build rm's the whole apps/jarvis-input dir.
#
cmake_minimum_required(VERSION 3.16.0)
project(jarvis-input C)

add_executable(jarvis-input EXCLUDE_FROM_ALL
    src/main.c
    src/net/control_parser.c
    src/net/control_ratelimit.c
)

target_include_directories(jarvis-input PRIVATE
    "src"
    "src/net"
)

target_link_libraries(jarvis-input
    PUBLIC
        sel4_autoconf
        muslc
        sel4
        sel4runtime
        sel4allocman
        sel4vka
        sel4utils
        sel4platsupport
        sel4muslcsys
)

target_compile_options(jarvis-input PRIVATE -Wall -g -O2)
JARVIS_INPUT_CMAKE
        echo -e "  ${GREEN}CREATED${NC}  apps/jarvis-input/CMakeLists.txt (SEC-014 input process)"
    fi
    echo ""
else
    # TEARDOWN (symmetric gate): remove any copied control-IN sources a PRIOR ON build
    # left in this PERSISTENT out-of-tree $DEST, so an OFF build carries no crypto/net,
    # and delete the whole jarvis-input app so no input ELF is built/packed.
    rm -rf "$DEST/src/crypto" "$DEST/src/net" 2>/dev/null || true
    rm -rf "$SEL4_DIR/projects/jarvis-x86/apps/jarvis-input" 2>/dev/null || true
fi

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
# C/M1b-3: PB needs the embed region LAYOUT (the struct + magic + status/size macros) but NOT
# embed_region.c — PB only ever PUBLISHES into the region; embed_result_usable() is PA's call.
# Header only, so PB links no new object.
copy_file "$IPC3_SRC/embed_region.h" "$PROC_B_DIR/ai/embed_region.h"
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

    # Add src/drivers/framebuffer.c to source list if missing (goal #2 Step 2b)
    if ! grep -q "src/drivers/framebuffer.c" "$CMAKE_FILE"; then
        sed -i '/src\/drivers\/fat32.c/a\    src/drivers/framebuffer.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/drivers/framebuffer.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/framebuffer.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add framebuffer.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/framebuffer.c already in source list"
    fi

    # goal #2b N-a: add src/drivers/nic_i211.c to source list if missing
    if ! grep -q "src/drivers/nic_i211.c" "$CMAKE_FILE"; then
        sed -i '/src\/drivers\/framebuffer.c/a\    src/drivers/nic_i211.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/drivers/nic_i211.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/nic_i211.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add nic_i211.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/nic_i211.c already in source list"
    fi

    # goal #2b N-b: add src/drivers/net_udp.c to source list if missing
    if ! grep -q "src/drivers/net_udp.c" "$CMAKE_FILE"; then
        sed -i '/src\/drivers\/nic_i211.c/a\    src/drivers/net_udp.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/drivers/net_udp.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/net_udp.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add net_udp.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/net_udp.c already in source list"
    fi

    # goal #2b N-c: add src/drivers/jarvis_telemetry.c to source list if missing
    if ! grep -q "src/drivers/jarvis_telemetry.c" "$CMAKE_FILE"; then
        sed -i '/src\/drivers\/net_udp.c/a\    src/drivers/jarvis_telemetry.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/drivers/jarvis_telemetry.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/drivers/jarvis_telemetry.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add jarvis_telemetry.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/drivers/jarvis_telemetry.c already in source list"
    fi

    # Phase 5 G1/M1: add src/ai/episodic_store.c to the Process A source list if missing
    if ! grep -q "src/ai/episodic_store.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/decision_cache.c/a\    src/ai/episodic_store.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/episodic_store.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/episodic_store.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add episodic_store.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/episodic_store.c already in source list"
    fi

    # Phase 5 G2/M1: add src/ai/shared_context.c to the Process A source list if missing
    if ! grep -q "src/ai/shared_context.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/episodic_store.c/a\    src/ai/shared_context.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/shared_context.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/shared_context.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add shared_context.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/shared_context.c already in source list"
    fi

    # Phase 5 G3/M1: add src/ai/g3_retrieval.c to the Process A source list if missing
    # (PA-only: PA packs the retrieval preamble; PB injects at M2, so no PB patch yet.)
    if ! grep -q "src/ai/g3_retrieval.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/shared_context.c/a\    src/ai/g3_retrieval.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/g3_retrieval.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/g3_retrieval.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add g3_retrieval.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/g3_retrieval.c already in source list"
    fi

    # Phase 5 #6/M1: add src/ai/cache_growth.c to the Process A source list if missing
    # (PA-only: the promotion pass runs in PA's workload loop; PB never touches the cache.)
    if ! grep -q "src/ai/cache_growth.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/g3_retrieval.c/a\    src/ai/cache_growth.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/cache_growth.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/cache_growth.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add cache_growth.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/cache_growth.c already in source list"
    fi

    # Phase 5 #5/M1: add src/ai/shield_learn.c to the Process A source list if missing
    # (PA-only: the risk map derives from the episodic log in PA; MONITOR-ONLY, never blocks.)
    if ! grep -q "src/ai/shield_learn.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/cache_growth.c/a\    src/ai/shield_learn.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/shield_learn.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/shield_learn.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add shield_learn.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/shield_learn.c already in source list"
    fi

    # Phase 5 #4/M1: add src/ai/semantic_store.c + semantic_distill.c to the Process A source
    # list if missing (PA-only: the boot-scan distill runs in PA; WRITE-ONLY — PB never reads it.)
    if ! grep -q "src/ai/semantic_store.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/shield_learn.c/a\    src/ai/semantic_store.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/semantic_store.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/semantic_store.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add semantic_store.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/semantic_store.c already in source list"
    fi
    if ! grep -q "src/ai/semantic_distill.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/semantic_store.c/a\    src/ai/semantic_distill.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/semantic_distill.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/semantic_distill.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add semantic_distill.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/semantic_distill.c already in source list"
    fi

    # Phase 6 K/M1: add the it-acts decision core (action_allowlist.c + shield_action.c +
    # action_audit.c) to the Process A source list if missing (PA-only: PA is the actor;
    # all gated JARVIS_ACTIONS default-0 — the deployed image stays action-inert).
    if ! grep -q "src/ai/action_allowlist.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/semantic_distill.c/a\    src/ai/action_allowlist.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/action_allowlist.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/action_allowlist.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add action_allowlist.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/action_allowlist.c already in source list"
    fi
    if ! grep -q "src/ai/shield_action.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/action_allowlist.c/a\    src/ai/shield_action.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/shield_action.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/shield_action.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add shield_action.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/shield_action.c already in source list"
    fi
    if ! grep -q "src/ai/action_audit.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/shield_action.c/a\    src/ai/action_audit.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/action_audit.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/action_audit.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add action_audit.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/action_audit.c already in source list"
    fi
    if ! grep -q "src/ai/km2b_trigger.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/action_audit.c/a\    src/ai/km2b_trigger.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/km2b_trigger.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/km2b_trigger.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add km2b_trigger.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/km2b_trigger.c already in source list"
    fi
    if ! grep -q "src/ai/km2b_miss.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/km2b_trigger.c/a\    src/ai/km2b_miss.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/km2b_miss.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/km2b_miss.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add km2b_miss.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/km2b_miss.c already in source list"
    fi
    if ! grep -q "src/ai/km2b_fault.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/km2b_miss.c/a\    src/ai/km2b_fault.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/km2b_fault.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/km2b_fault.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add km2b_fault.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/km2b_fault.c already in source list"
    fi
    if ! grep -q "src/ai/monitors.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/km2b_fault.c/a\    src/ai/monitors.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/monitors.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/monitors.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add monitors.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/monitors.c already in source list"
    fi
    if ! grep -q "src/ai/wake.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/monitors.c/a\    src/ai/wake.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/wake.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/wake.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add wake.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/wake.c already in source list"
    fi
    if ! grep -q "src/ai/behaviors.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/wake.c/a\    src/ai/behaviors.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/behaviors.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/behaviors.c to source list"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add behaviors.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/behaviors.c already in source list"
    fi

    # Phase 6 6-6/B/M1: add src/ai/route.c to the Process A source list if missing.
    # UNCONDITIONAL by design (the episodic_store.c / behaviors.c precedent, NOT the gated
    # control-IN crypto/net one): route.c is host-pure with no external dependency, so linking it
    # is benign and inert until JARVIS_ROUTING calls it. Keeping the injection ungated means the
    # ROUTING=0 build links the same objects as the ROUTING=1 build, which is exactly what makes
    # the main.c.obj OFF-object-identity comparison meaningful rather than a link-set difference.
    if ! grep -q "src/ai/route.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/behaviors.c/a\    src/ai/route.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/route.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/route.c to source list (6-6/B/M1)"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add route.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/route.c already in source list"
    fi

    # Phase 6 A9/1: add src/ai/ctrl_epi_index.c to the Process A source list if missing.
    # UNCONDITIONAL, the route.c / episodic_store.c precedent: host-pure with no external
    # dependency (g3_candidate_usable is a static inline in the header), so linking it is benign.
    # PA owns the index STORAGE; this module owns the maintenance.
    if ! grep -q "src/ai/ctrl_epi_index.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/route.c/a\    src/ai/ctrl_epi_index.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ai/ctrl_epi_index.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/ctrl_epi_index.c to source list (A9/1)"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add ctrl_epi_index.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/ctrl_epi_index.c already in source list"
    fi

    # Phase C / C/M2a + C/M2b: add src/ai/embed_store.c and src/ai/embed_project.c to the
    # Process A source list. UNCONDITIONAL, the route.c / embed_region.c precedent — both are
    # host-pure with no external dependency, so linking them is benign and inert until
    # JARVIS_EMBED calls them, and an ungated injection means the EMBED=0 build links the SAME
    # objects as EMBED=1. That is what makes the G1 main.c.obj comparison an identity test
    # rather than a link-set difference.
    for _em in embed_store embed_project route_veto pb_progress; do
        if ! grep -q "src/ai/$_em.c" "$CMAKE_FILE"; then
            sed -i "/src\/ai\/route.c/a\\    src/ai/$_em.c" "$CMAKE_FILE" 2>/dev/null
            if grep -q "src/ai/$_em.c" "$CMAKE_FILE"; then
                echo -e "  ${GREEN}ADDED${NC}  src/ai/$_em.c to source list (Phase C)"
                PATCHED=1
            else
                echo -e "  ${RED}FAILED${NC}  Could not add $_em.c — edit CMakeLists.txt manually"
            fi
        else
            echo -e "  ${CYAN}OK${NC}  src/ai/$_em.c already in source list"
        fi
    done

    # Phase C / C/M1b-3: add src/ipc/embed_region.c to the Process A source list if missing.
    # UNCONDITIONAL, the route.c precedent: it is host-pure with no external dependency, so
    # linking it is benign and inert until JARVIS_EMBED calls it — and keeping the injection
    # ungated means the EMBED=0 build links the SAME objects as EMBED=1, which is what makes the
    # main.c.obj OFF-object-identity comparison meaningful rather than a link-set difference.
    if ! grep -q "src/ipc/embed_region.c" "$CMAKE_FILE"; then
        sed -i '/src\/ai\/route.c/a\    src/ipc/embed_region.c' "$CMAKE_FILE" 2>/dev/null
        if grep -q "src/ipc/embed_region.c" "$CMAKE_FILE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ipc/embed_region.c to source list (C/M1b-3)"
            PATCHED=1
        else
            echo -e "  ${RED}FAILED${NC}  Could not add embed_region.c — edit CMakeLists.txt manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ipc/embed_region.c already in source list"
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

    # ── Phase 6 6-5/M2a: control-IN sources + include dirs + compile-def (GATED) ──
    if [ "$CONTROL_IN" = "1" ]; then
        for cf in src/crypto/sha256.c src/crypto/hmac_sha256.c src/net/control_parser.c \
                  src/net/control_replay.c src/net/control_ratelimit.c src/net/control_verify.c \
                  src/net/control_floor.c src/net/control_console.c src/net/control_reply.c; do
            if ! grep -q "$cf" "$CMAKE_FILE"; then
                sed -i "/src\/ai\/behaviors.c/a\\    $cf" "$CMAKE_FILE" 2>/dev/null
                if grep -q "$cf" "$CMAKE_FILE"; then
                    echo -e "  ${GREEN}ADDED${NC}  $cf to source list"; PATCHED=1
                else
                    echo -e "  ${RED}FAILED${NC}  Could not add $cf — edit CMakeLists.txt manually"
                fi
            fi
        done
        # 6-5/M3-2a: the query SHIELD (src/ai, already an include dir) — gated so an OFF build
        # neither compiles nor links it (the teardown else-branch strips it).
        if ! grep -q "src/ai/query_shield.c" "$CMAKE_FILE"; then
            sed -i "/src\/ai\/behaviors.c/a\\    src/ai/query_shield.c" "$CMAKE_FILE" 2>/dev/null
            if grep -q "src/ai/query_shield.c" "$CMAKE_FILE"; then
                echo -e "  ${GREEN}ADDED${NC}  src/ai/query_shield.c (6-5/M3-2a)"; PATCHED=1
            else
                echo -e "  ${RED}FAILED${NC}  Could not add src/ai/query_shield.c — edit CMakeLists.txt manually"
            fi
        fi
        if ! grep -q '"src/net"' "$CMAKE_FILE"; then
            sed -i 's|"src/drivers"|"src/drivers" "src/crypto" "src/net"|' "$CMAKE_FILE"
            grep -q '"src/net"' "$CMAKE_FILE" && { echo -e "  ${GREEN}ADDED${NC}  src/crypto + src/net includes"; PATCHED=1; }
        fi
        if ! grep -q "JARVIS_CONTROL_IN=1" "$CMAKE_FILE"; then
            sed -i '/target_compile_options/a\target_compile_definitions(sel4test-driver PRIVATE JARVIS_CONTROL_IN=1)' "$CMAKE_FILE" 2>/dev/null
            grep -q "JARVIS_CONTROL_IN=1" "$CMAKE_FILE" && { echo -e "  ${GREEN}ADDED${NC}  JARVIS_CONTROL_IN=1 compile def"; PATCHED=1; }
        fi
        # 6-5/M3-2a-fix: JARVIS_CONTROL_IN_PROBE is consumed ONLY by main_x86.c, which includes
        # jarvis_debug.h — the header VALUE governs directly (no -D needed; contrast JARVIS_CONTROL_IN,
        # whose -D MUST stay because nic_i211.c does not include the header). Strip any -D injected by
        # an older build: a persistent -DJARVIS_CONTROL_IN_PROBE=1 defeats the header's #ifndef and
        # silently pins the MODE to 1, so a mode-2/3 gate would compile out and fail by absence.
        if grep -q "JARVIS_CONTROL_IN_PROBE=" "$CMAKE_FILE"; then
            sed -i '/target_compile_definitions(sel4test-driver PRIVATE JARVIS_CONTROL_IN_PROBE=/d' "$CMAKE_FILE"
            echo -e "  ${YELLOW}STRIPPED${NC}  stale JARVIS_CONTROL_IN_PROBE def (header is the source of truth)"; PATCHED=1
        fi
        # 6-5/M2b-1: register the SEC-014 jarvis-input app (add_subdirectory) + pack its
        # ELF into the rootserver CPIO (extend MakeCPIO). These two lines live in the base
        # CMakeLists (not the source-list sed's above), so patch them explicitly.
        if ! grep -q "add_subdirectory(../jarvis-input" "$CMAKE_FILE"; then
            sed -i '/add_subdirectory(..\/jarvis-inference jarvis-inference)/a\add_subdirectory(../jarvis-input jarvis-input)' "$CMAKE_FILE" 2>/dev/null
            grep -q "add_subdirectory(../jarvis-input" "$CMAKE_FILE" && { echo -e "  ${GREEN}ADDED${NC}  add_subdirectory(jarvis-input)"; PATCHED=1; }
        fi
        if ! grep -q 'TARGET_FILE:jarvis-input' "$CMAKE_FILE"; then
            sed -i 's|MakeCPIO(archive.o .*|MakeCPIO(archive.o "$<TARGET_FILE:jarvis-inference>;$<TARGET_FILE:jarvis-input>")|' "$CMAKE_FILE"
            grep -q 'TARGET_FILE:jarvis-input' "$CMAKE_FILE" && { echo -e "  ${GREEN}ADDED${NC}  jarvis-input packed into the rootserver CPIO"; PATCHED=1; }
        fi
    else
        # TEARDOWN (symmetric gate — the load-bearing OFF-is-really-OFF guarantee). The
        # -DJARVIS_CONTROL_IN=1 lives in this PERSISTENT CMakeLists and OVERRIDES the
        # header's #ifndef/#define 0, so flipping the header to 0 ALONE is NOT enough:
        # a prior ON build would otherwise leave the OFF/deploy image compiled with the
        # control-IN path ACTIVE (the §8 forbidden 'mostly-gated' state). Strip every
        # injected line so an OFF build is genuinely OFF + object-identical to pre-M2a.
        if grep -qE 'JARVIS_CONTROL_IN(_PROBE)?=|src/(crypto|net)/|src/ai/query_shield.c|jarvis-input' "$CMAKE_FILE"; then
            sed -i '/target_compile_definitions(sel4test-driver PRIVATE JARVIS_CONTROL_IN=1)/d' "$CMAKE_FILE"
            # 6-5/M3-2a-fix: value-agnostic (=, not =1) so a stale mode-2/3 -D is torn down too.
            sed -i '/target_compile_definitions(sel4test-driver PRIVATE JARVIS_CONTROL_IN_PROBE=/d' "$CMAKE_FILE"
            # NOTE (6-5/M4a, re-verified M4b): the src/net/*.c strip below is GENERIC — it already
            # removes the newly injected src/net/control_console.c AND src/net/control_reply.c (as it
            # does control_floor.c). Verified, so NO extra per-file line is added here; an
            # add-without-teardown is the M2a bug class this guards.
            sed -i '/^[[:space:]]*src\/crypto\//d; /^[[:space:]]*src\/net\/.*\.c/d' "$CMAKE_FILE"
            sed -i '/^[[:space:]]*src\/ai\/query_shield.c/d' "$CMAKE_FILE"   # 6-5/M3-2a: strip the gated query SHIELD
            sed -i 's| "src/crypto" "src/net"||' "$CMAKE_FILE"
            # 6-5/M2b-1: revert the jarvis-input registration so an OFF build packs no
            # input ELF (MakeCPIO back to jarvis-inference only; drop add_subdirectory).
            sed -i '/add_subdirectory(..\/jarvis-input jarvis-input)/d' "$CMAKE_FILE"
            sed -i 's|MakeCPIO(archive.o .*|MakeCPIO(archive.o "$<TARGET_FILE:jarvis-inference>")|' "$CMAKE_FILE"
            echo -e "  ${YELLOW}TORE DOWN${NC}  control-IN CMake state (OFF build is genuinely OFF)"
            PATCHED=1
        fi
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

# ── [6b/6] M3 seL4-native threadpool (Process B only) ────────────────
# threadpool_sel4.c implements jarvis_parallel_for with seL4 worker threads (created by
# Process A in PB's VSpace). Add it to the jarvis-inference source list + define
# JARVIS_SEL4_SMP on that target ONLY. PA never compiles it (it resolves the worker entry
# from PB's ELF by name). Idempotent (grep-guarded).
echo -e "${GREEN}[6b/6] M3 threadpool (Process B)${NC}"
PB_CMAKE="$SEL4_DIR/projects/jarvis-x86/apps/jarvis-inference/CMakeLists.txt"
if [ -f "$PB_CMAKE" ]; then
    if ! grep -q "src/ai/threadpool_sel4.c" "$PB_CMAKE"; then
        sed -i '/src\/ai\/qdot.c/a\    src/ai/threadpool_sel4.c' "$PB_CMAKE"
        if grep -q "src/ai/threadpool_sel4.c" "$PB_CMAKE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/threadpool_sel4.c to jarvis-inference sources"
        else
            echo -e "  ${RED}FAILED${NC}  could not add threadpool_sel4.c — edit PB CMakeLists manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/threadpool_sel4.c already in sources"
    fi
    # Phase 6 N2: pb_progress.c into the PB source list too — BOTH lists, unconditionally (the
    # route.c precedent). PB never calls pb_wait_decide (its half of the tick is the inline
    # RELEASE bump from shmem_ipc.h), so the object links inert; injecting it anyway keeps the
    # TICK=0 and TICK=1 builds linking the SAME object set on both targets, which is what makes
    # the G1 OFF-object-identity comparison an identity test rather than a link-set difference.
    if ! grep -q "src/ai/pb_progress.c" "$PB_CMAKE"; then
        sed -i '/src\/ai\/qdot.c/a\    src/ai/pb_progress.c' "$PB_CMAKE"
        if grep -q "src/ai/pb_progress.c" "$PB_CMAKE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/pb_progress.c to jarvis-inference sources (N2)"
        else
            echo -e "  ${RED}FAILED${NC}  could not add pb_progress.c — edit PB CMakeLists manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/pb_progress.c already in sources"
    fi
    if ! grep -q "JARVIS_SEL4_SMP" "$PB_CMAKE"; then
        sed -i '/target_compile_options(jarvis-inference/a\target_compile_definitions(jarvis-inference PRIVATE JARVIS_SEL4_SMP)' "$PB_CMAKE"
        if grep -q "JARVIS_SEL4_SMP" "$PB_CMAKE"; then
            echo -e "  ${GREEN}ADDED${NC}  JARVIS_SEL4_SMP define on jarvis-inference"
        else
            echo -e "  ${RED}FAILED${NC}  could not add JARVIS_SEL4_SMP — edit PB CMakeLists manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  JARVIS_SEL4_SMP already defined"
    fi
    # Phase C / C/M1b-2: llama_quant.c deliberately does NOT include jarvis_debug.h (it also builds
    # in the native/CI contexts and carries its own #ifndef fallbacks), so it cannot see
    # JARVIS_EMBED from the header — qmodel_embed_last would silently not be compiled and PB would
    # fail to LINK. Propagate the header's value as a -D on the PB target, with a symmetric
    # teardown so an EMBED=0 build cannot inherit stale ON state (the JARVIS_CONTROL_IN precedent).
    EMBED_ON=$(grep -oE '^#define[[:space:]]+JARVIS_EMBED[[:space:]]+[0-9]' \
                 "$JARVIS_DIR/phase3/src/sel4/jarvis_debug.h" | grep -oE '[0-9]$' || echo 0)
    if [ "$EMBED_ON" = "1" ]; then
        if ! grep -q "JARVIS_EMBED=1" "$PB_CMAKE"; then
            sed -i '/target_compile_options(jarvis-inference/a\target_compile_definitions(jarvis-inference PRIVATE JARVIS_EMBED=1)' "$PB_CMAKE"
            echo -e "  ${GREEN}ADDED${NC}  JARVIS_EMBED=1 define on jarvis-inference (C/M1b-2)"
        else
            echo -e "  ${CYAN}OK${NC}  JARVIS_EMBED=1 already defined"
        fi
    else
        if grep -q "JARVIS_EMBED=1" "$PB_CMAKE"; then
            sed -i '/JARVIS_EMBED=1/d' "$PB_CMAKE"
            echo -e "  ${GREEN}REMOVED${NC}  stale JARVIS_EMBED=1 define (header says 0)"
        fi
    fi
else
    echo -e "  ${YELLOW}SKIP${NC}  jarvis-inference CMakeLists not found ($PB_CMAKE)"
fi
echo ""

# ── [6c/6] G2/M1 shared context pool (Process B reads it) ────────────
# shared_context.c is used by BOTH Process A (sctx_init) and Process B (sctx_read_state
# smoke; M3 reads it in handle_query), so it must be in BOTH source lists. AI_FILES already
# copies it to both ai/ dirs; Process A's list is patched in [5/5] above — add it to the
# jarvis-inference source list here (anchored on qdot.c, like the M3 threadpool).
echo -e "${GREEN}[6c/6] Shared context pool (Process B)${NC}"
if [ -f "$PB_CMAKE" ]; then
    if ! grep -q "src/ai/shared_context.c" "$PB_CMAKE"; then
        sed -i '/src\/ai\/qdot.c/a\    src/ai/shared_context.c' "$PB_CMAKE"
        if grep -q "src/ai/shared_context.c" "$PB_CMAKE"; then
            echo -e "  ${GREEN}ADDED${NC}  src/ai/shared_context.c to jarvis-inference sources"
        else
            echo -e "  ${RED}FAILED${NC}  could not add shared_context.c — edit PB CMakeLists manually"
        fi
    else
        echo -e "  ${CYAN}OK${NC}  src/ai/shared_context.c already in jarvis-inference sources"
    fi
else
    echo -e "  ${YELLOW}SKIP${NC}  jarvis-inference CMakeLists not found ($PB_CMAKE)"
fi
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

# ── Kernel: multiboot2 framebuffer-request tag (Phase 4 goal #2 Step 2a) ─────
# seL4 already PARSES a multiboot2 framebuffer tag and FORWARDS it to the rootserver
# as SEL4_BOOTINFO_HEADER_X86_FRAMEBUFFER (id=4) — but the kernel's MB2 header never
# REQUESTS one, so GRUB sets none up. Insert a type-5 framebuffer-request tag
# (width/height/depth = 0/0/0 = no preference => keep the firmware GOP mode) before
# the MB2 end tag. Idempotent (grep-guarded); runs BEFORE ninja so the kernel rebuilds
# with it. Independent of CONFIG_MULTIBOOT_GRAPHICS_MODE_* (that only wires the MB1
# header), so no Kconfig change is needed. NOTE: this deliberately leaves the
# (repo-managed) kernel source multiboot.S modified; it is re-applied (guarded) every
# build, so a `repo sync`/`git checkout` revert self-heals on the next build.
# The awk match is the EXACT end-tag comment (so the inserted "MB2 terminator" line
# can never be a second match), and we gate on awk explicitly — `awk ... && mv` would
# be EXEMPT from `set -e` (left operand of &&), so a silent awk failure must be caught
# here, not relied on the grep guard below.
MB2_S="$SEL4_DIR/kernel/src/arch/x86/multiboot.S"
if [ -f "$MB2_S" ]; then
    if ! grep -q "JARVIS goal #2: framebuffer-request" "$MB2_S"; then
        if awk '
            /end tag - type, flags, size/ && !ins {
                print "    /* JARVIS goal #2: framebuffer-request tag (type 5). 0/0/0 = no preference =>"
                print "     * keep the firmware GOP mode. seL4 forwards the result as id=4 to the rootserver. */"
                print "    .align  8"
                print "    .word   5      /* type = framebuffer */"
                print "    .word   0      /* flags (0 = required) */"
                print "    .long   20     /* size = type(2)+flags(2)+size(4)+width(4)+height(4)+depth(4) */"
                print "    .long   0      /* width  (0 = no preference) */"
                print "    .long   0      /* height (0 = no preference) */"
                print "    .long   0      /* depth  (0 = no preference) */"
                print "    .align  8      /* pad to 8 before the MB2 terminator */"
                ins=1
            }
            { print }
        ' "$MB2_S" > "$MB2_S.jarvis.tmp"; then
            mv "$MB2_S.jarvis.tmp" "$MB2_S"
        else
            rm -f "$MB2_S.jarvis.tmp"
            echo -e "${RED}FAILED to patch multiboot.S (awk error). Aborting.${NC}"
            exit 1
        fi
        if grep -q "JARVIS goal #2: framebuffer-request" "$MB2_S"; then
            echo -e "${GREEN}Patched kernel multiboot.S — added MB2 framebuffer-request tag (goal #2 Step 2a)${NC}"
        else
            echo -e "${RED}FAILED to patch multiboot.S ('end tag - type, flags, size' marker not found). Aborting.${NC}"
            exit 1
        fi
    else
        echo -e "${CYAN}multiboot.S already has the MB2 framebuffer-request tag${NC}"
    fi
else
    echo -e "${YELLOW}WARN${NC}  kernel multiboot.S not found at $MB2_S — no FB-request tag added"
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
# M3: worker count = NUM_NODES (kernel SMP node count). Default 6 (the dual-channel DDR4
# bandwidth knee for the N-sweep); override per-build via JARVIS_NUM_NODES. Cap is 8.
NN="${JARVIS_NUM_NODES:-6}"
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
      -DNUM_NODES="${NN}" \
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
    check_cfg "#define CONFIG_MAX_NUM_NODES[[:space:]]+${NN}\b"  "SMP: CONFIG_MAX_NUM_NODES=${NN} (M3 NUM_NODES)   [M2/M3]"
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
    # shellcheck disable=SC2012  # `ls -lh` is the point: this prints the human-readable
    # build listing for the operator. `find` would change that operator-visible format
    # for no safety gain, and these are our own build artifacts with fixed names.
    ls -lh "$IMAGES_DIR"/ 2>/dev/null | while read -r line; do
        echo "  $line"
    done
    echo ""
fi

echo -e "${GREEN}================================================${NC}"
echo -e "${GREEN}  BUILD COMPLETE${NC}"
echo -e "${GREEN}================================================${NC}"
