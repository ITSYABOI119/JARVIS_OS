# JARVIS AI-OS - Phase 3 Portable Modules Build
# Compiles platform-independent C code from Phase 1/2 on x86-64
#
# Usage:
#   mkdir -p build && cd build
#   cmake .. && make
#
# This builds a static library (libjarvis_portable.a) containing:
#   - Decision cache (FNV-1a hash table, 258 patterns)
#   - Ring buffer (SPSC lock-free IPC)
#   - Dual ring buffer (bidirectional IPC)
#   - IPC handler (polling mode)
#   - Network stack (ARP/ICMP, portable frame processing)
#   - Network commands (portable dispatch, IP parsing)

cmake_minimum_required(VERSION 3.16)
project(jarvis_phase3_portable C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror -O2")

# ================================================================
# Portable library (no hardware dependencies)
# ================================================================

add_library(jarvis_portable STATIC
    ai/decision_cache.c
    ai/cache_patterns.c
    ipc/ring_buffer.c
    ipc/dual_ring_buffer.c
    ipc/ipc_handler.c
    drivers/net_stack.c
    drivers/net_cmd.c
)

target_include_directories(jarvis_portable PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc
    ${CMAKE_CURRENT_SOURCE_DIR}/drivers
)

# On x86, JARVIS_PLATFORM_PI4 is NOT defined, so platform-specific
# code (#ifdef JARVIS_PLATFORM_PI4) is excluded.
# To build for Pi 4, add: -DJARVIS_PLATFORM_PI4=1

# ================================================================
# Phase 3 shared memory IPC (new for x86)
# ================================================================

add_library(jarvis_shmem STATIC
    ipc/shmem_ipc.c
)

target_include_directories(jarvis_shmem PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ipc
)

target_compile_definitions(jarvis_shmem PUBLIC JARVIS_IPC_SHMEM)
