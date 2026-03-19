#
# JARVIS AI-OS Phase 3 — x86-64 Rootserver Build Configuration
#
# Based on sel4test-driver CMakeLists.txt pattern.
# Builds the JARVIS rootserver for seL4 x86-64 PC99.
#
# Integration steps:
#   1. Place this project under ~/sel4-x86/projects/jarvis-x86/
#   2. Add to the top-level CMakeLists.txt or create a custom manifest
#   3. Build: mkdir cbuild && cd cbuild && cmake -G Ninja \
#      -DPLATFORM=x86_64 -DSIMULATION=1 ../projects/jarvis-x86
#   4. Run: ./simulate (or qemu-system-x86_64 with -nographic)
#
# NOTE: This CMakeLists.txt is a TEMPLATE. Actual integration into
# the seL4 build system requires a manifest file or top-level
# CMakeLists.txt that includes the seL4 kernel, musllibc, and
# support libraries. See SEL4_X86_ROOTSERVER_NOTES.md for details.
#

cmake_minimum_required(VERSION 3.16.0)

project(jarvis-x86 C)

# Find seL4 build system packages
find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

# Set up build environment with musl C library and seL4 runtime
musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

# JARVIS rootserver executable
add_executable(jarvis-x86
    EXCLUDE_FROM_ALL
    src/main_x86.c
    # Portable modules from Phase 2
    src/ai/decision_cache.c
    src/ai/cache_patterns.c
    src/ipc/ring_buffer.c
    src/ipc/dual_ring_buffer.c
    src/ipc/shmem_ipc.c
)

target_include_directories(jarvis-x86
    PRIVATE
    src/ai
    src/ipc
    src/drivers
)

target_link_libraries(jarvis-x86
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

target_compile_options(jarvis-x86 PRIVATE -Werror -g -O2)

# Declare as rootserver (seL4 build system macro)
include(rootserver)
DeclareRootserver(jarvis-x86)
