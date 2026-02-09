#
# JARVIS AI-OS - Phase 2 Week 33
# seL4 ARM64 (Raspberry Pi 4) Build Configuration
#
# Top-level CMakeLists.txt following sel4test pattern
#

cmake_minimum_required(VERSION 3.7.2)

# Include settings (must come before project())
include(settings.cmake)

project(jarvis-sel4 C ASM)

# Find and import seL4 kernel and elfloader
find_package(seL4 REQUIRED)
find_package(elfloader-tool REQUIRED)

# Set the roottask CNode size
set(KernelRootCNodeSizeBits 13 CACHE INTERNAL "")

# Import kernel and elfloader
sel4_import_kernel()

# Match sel4test elfloader settings (sets IMAGE_START_ADDR=0x10000000 for RPi4)
if(KernelArchARM OR KernelArchRiscV)
    ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})
endif()
elfloader_import_project()

# Find required libraries
find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)
find_package(sel4_projects_libs REQUIRED)

# Disable GC sections to prevent code stripping
set(UserLinkerGCSections OFF CACHE BOOL "" FORCE)

# Set up build environment with seL4 runtime and musllibc
musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
seL4_libs_import_libraries()
sel4_projects_libs_import_libraries()

# Define JARVIS executable with all sources
add_executable(jarvis-sel4
    # Main entry point
    src/main.c

    # Decision cache (Phase 1)
    src/cache/decision_cache.c
    src/cache/cache_patterns.c

    # IPC layer (Phase 1)
    src/ipc/ring_buffer.c
    src/ipc/ipc_sel4.c

    # Phase 2 bidirectional IPC
    src/ipc_phase2/dual_ring_buffer.c
    src/ipc_phase2/ipc_handler.c

    # ARM64 PL011 UART driver
    src/drivers/uart_pl011.c

    # SD/EMMC + Timer (Week 35-36)
    src/drivers/emmc_sdhci.c
    src/drivers/bcm2711_timer.c
    src/drivers/blk_dev.c

    # DMA allocator (Week 36 - proper physical address handling)
    src/drivers/dma_alloc.c

    # Shared slot allocator (Week 36 - prevents slot collisions)
    src/drivers/slot_alloc.c

    # BCM2711 GENET Ethernet (Week 37 - TX only)
    src/drivers/bcm_genet.c

    # Basic networking stack (Week 38 - ARP/ICMP)
    src/drivers/net_stack.c

    # Network shell commands (Week 39 - ping/ifconfig/netstat)
    src/drivers/net_cmd.c

    # USB HID keyboard driver (Week 40 - DWC2 host + HID boot protocol)
    src/drivers/usb_hid.c

    # GPIO + I2C drivers (Week 43)
    src/drivers/bcm_gpio.c
    src/drivers/bcm_i2c.c

    # Watchdog + Thermal monitoring (Week 44)
    src/drivers/bcm_watchdog.c
    src/drivers/bcm_thermal.c

    # FDT parser + embedded DTB (Week 45)
    src/boot/fdt_parser.c

    # Boot manager + Warm reboot + Power management (Week 46)
    src/boot/boot_manager.c
    src/boot/warm_reboot.c
    src/drivers/bcm_power.c
)

# Include directories
target_include_directories(jarvis-sel4 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cache
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ipc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ipc_phase2
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/boot
)

# Link seL4 libraries
target_link_libraries(jarvis-sel4
    PUBLIC
        sel4_autoconf      # Auto-generated config
        muslc              # C library
        sel4               # seL4 kernel API
        sel4runtime        # seL4 runtime support
        sel4allocman       # Allocator manager
        sel4vka            # Virtual kernel allocator
        sel4utils          # seL4 utilities
        sel4platsupport    # Platform support
        sel4muslcsys       # System call interface
        platsupport        # Platform support (for ps_io_map)
)

# Compile options
target_compile_options(jarvis-sel4 PRIVATE -Werror -g)

# Declare as bootable rootserver
include(rootserver)
DeclareRootserver(jarvis-sel4)
