#
# JARVIS AI-OS x86-64 — Top-level CMakeLists.txt
# Based on sel4test CMakeLists.txt, stripped of test infrastructure.
#

cmake_minimum_required(VERSION 3.16.0)

include(settings.cmake)

project(jarvis-x86 C ASM)

find_package(seL4 REQUIRED)
find_package(elfloader-tool REQUIRED)

# CNode size for rootserver capabilities
# 19 = 524288 slots (default 13 = 8192 overflows with 8GB QEMU + 771MB embedded model)
set(KernelRootCNodeSizeBits 22 CACHE INTERNAL "")

# Domain schedule
set(KernelDomainSchedule "${CMAKE_CURRENT_LIST_DIR}/domain_schedule.c" CACHE INTERNAL "")
sel4_import_kernel()

if((NOT Sel4testAllowSettingsOverride) AND (KernelArchARM OR KernelArchRiscV))
    ApplyData61ElfLoaderSettings(${KernelPlatform} ${KernelSel4Arch})
endif()
elfloader_import_project()

add_subdirectory(apps/sel4test-driver)

if(SIMULATION)
    include(simulation)
    if(KernelSel4ArchX86_64)
        SetSimulationScriptProperty(MEM_SIZE "3G")
    endif()
    GenerateSimulateScript()
endif()
