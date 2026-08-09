#
# Copyright 2019, Data61, CSIRO (ABN 41 687 119 230)
#
# SPDX-License-Identifier: BSD-2-Clause
#

# Define our top level settings.  Whilst they have doc strings for readability
# here, they are hidden in the cmake-gui as they cannot be reliably changed
# after the initial configuration.  Enterprising users can still change them if
# they know what they are doing through advanced mode.
#
# Users should initialize a build directory by doing something like:
#
# mkdir build_sabre
# cd build_sabre
#
# Then
#
# ../griddle --PLATFORM=sabre --SIMULATION
# ninja
#
set(SIMULATION OFF CACHE BOOL "Include only simulation compatible tests")
set(RELEASE OFF CACHE BOOL "Performance optimized build")
set(VERIFICATION OFF CACHE BOOL "Only verification friendly kernel features")
set(BAMBOO OFF CACHE BOOL "Enable machine parseable output")
set(DOMAINS OFF CACHE BOOL "Test multiple domains")
set(SMP OFF CACHE BOOL "(if supported) Test SMP kernel")
set(NUM_NODES "" CACHE STRING "(if SMP) the number of nodes (default 4)")
set(PLATFORM "x86_64" CACHE STRING "Platform to test")
# JARVIS: 22 bits = 4M slots. Split allocator creates O(N) intermediate untypeds
# when loading Process B's 771MB .rodata — 19 (524K) overflows, 22 is safe.
set(KernelRootCNodeSizeBits 22 CACHE INTERNAL "")
# JARVIS: Process B needs ~50MB heap for KV cache + tokenizer + activations.
# Global setting — Process A gets same size but only uses ~5MB.
set(LibSel4MuslcSysMorecoreBytes 134217728 CACHE STRING "" FORCE)  # 128 MB
set(ARM_HYP OFF CACHE BOOL "Hyp mode for ARM platforms")
set(MCS OFF CACHE BOOL "MCS kernel")
set(KernelSel4Arch "" CACHE STRING "aarch32, aarch64, arm_hyp, ia32, x86_64, riscv32, riscv64")
set(LibSel4TestPrinterRegex ".*" CACHE STRING "A POSIX regex pattern used to filter tests")
set(LibSel4TestPrinterHaltOnTestFailure OFF CACHE BOOL "Halt on the first test failure")
mark_as_advanced(CLEAR LibSel4TestPrinterRegex LibSel4TestPrinterHaltOnTestFailure)
