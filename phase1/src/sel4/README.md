# JARVIS seL4 Microkernel Application

This directory contains the custom seL4 application for JARVIS AI-OS Phase 1.

## Overview

**Week 2 Goal:** Minimal serial console with echo functionality (no AI yet)

This is a simple seL4 root task that:
- Boots the seL4 microkernel
- Initializes serial console I/O
- Prints JARVIS banner
- Provides a simple shell prompt (`jarvis> `)
- Echoes user input back to the console
- Supports basic commands (help, hello, exit message)

## Files

- `main.c` - Main entry point and simple shell implementation
- `CMakeLists.txt` - Build configuration
- `README.md` - This file

## Building

This project is built as part of the seL4 tutorials framework:

```bash
# From ~/jarvis-phase1 directory
mkdir build-jarvis
cd build-jarvis

# Initialize with our custom project
# (We'll create an init script for this)
cmake -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake \
    -DPLATFORM=x86_64 \
    ../phase1/src/sel4

ninja
```

## Running

```bash
# After building
./simulate

# Or manually with QEMU
qemu-system-x86_64 \
    -m 8G \
    -smp 4 \
    -serial stdio \
    -nographic \
    -kernel images/jarvis-sel4-image-x86_64-pc99
```

## Testing

Expected output:
```
========================================
  JARVIS AI-OS v0.1 - Phase 1
  Proof of Concept (Week 2)
========================================
  seL4 Microkernel + Serial Console
  Build: Nov 15 2025 20:00:00
========================================

System Information:
  Architecture: x86_64
  Platform: pc99 (QEMU)
  Kernel: seL4 microkernel

Starting shell (type 'help' for commands)...

jarvis>
```

Type commands and see them echoed back.

## Commands

- `help` - Show available commands
- `hello` - Print greeting
- `exit` - Print exit message (use Ctrl+A then X to actually exit QEMU)

## Architecture

- **Platform:** x86_64 (pc99 in QEMU)
- **Kernel:** seL4 microkernel
- **Userspace:** Single root task (this application)
- **I/O:** Serial console via stdio
- **Memory:** 8GB (QEMU VM)
- **CPUs:** 4 cores (2 for kernel, 2 reserved for future AI agent)

## Week 2 Status

- [x] Project structure created
- [x] CMakeLists.txt configured
- [x] main.c with shell implementation
- [ ] Successfully builds (pending test)
- [ ] Runs in QEMU (pending test)
- [ ] Echo functionality works (pending test)

## Next Steps (Week 3)

- Implement decision cache (hash table, 50 patterns)
- Add cache lookup functionality
- Prepare for AI agent integration

## Notes

- This is a minimal implementation to validate seL4 + serial I/O
- No AI functionality in Week 2 - just basic I/O
- stdio functions (printf, getchar) provided by musllibc
- seL4 handles interrupts and low-level hardware access
