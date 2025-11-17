# JARVIS AI-OS Phase 1 - CMake Build Configuration
#
# This CMakeLists.txt builds the JARVIS seL4 microkernel application
#
# Based on seL4 tutorials hello-world template
# Modified for JARVIS AI-OS Phase 1 Week 2

cmake_minimum_required(VERSION 3.7.2)

# Declare the JARVIS CMake project
project(jarvis-sel4 C ASM)

# Find required seL4 packages
find_package(seL4 REQUIRED)
find_package(elfloader-tool REQUIRED)
find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

# Import seL4 kernel and elfloader
sel4_import_kernel()
elfloader_import_project()

# Set up build environment with musllibc and runtime libraries
musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

# Define the executable and source files
# Week 10: Added decision cache and IPC components
add_executable(jarvis-sel4
    main.c
    ../cache/decision_cache.c
    ../cache/cache_patterns.c
    ../ipc/ring_buffer.c
    ../ipc/ipc_sel4.c
)

# Add include directories for headers
target_include_directories(jarvis-sel4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../cache
    ${CMAKE_CURRENT_SOURCE_DIR}/../ipc
)

# Link required libraries
target_link_libraries(jarvis-sel4
    sel4runtime
    sel4
    muslc
    utils
    sel4muslcsys
    sel4platsupport
    sel4utils
    sel4debug
)

# Declare this as the root server (main task)
include(rootserver)
DeclareRootserver(jarvis-sel4)

# Generate simulation script for QEMU
include(simulation)
GenerateSimulateScript()
