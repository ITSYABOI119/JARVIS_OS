# seL4 x86-64 Native Linux Setup Guide

**Date:** March 24, 2026
**Platform:** x86-64 PC99
**Host:** Ubuntu 24.04.2 LTS (native boot, NOT WSL)
**Hardware:** Ryzen 7 2700X, 32GB DDR4, RTX 2070
**Result:** seL4 test suite 123 PASS, 0 FAIL, 57 disabled. JARVIS rootserver 5/5 self-test PASS.

---

## Prerequisites

### System Packages (require sudo)

```bash
sudo apt install -y build-essential cmake ninja-build python3 python3-pip \
  gcc-multilib g++-multilib qemu-system-x86 repo git libxml2-utils \
  device-tree-compiler
```

### Python Packages

```bash
pip3 install --break-system-packages protobuf grpcio-tools pyelftools lxml ply
```

**Critical:** The seL4 build requires `lxml` (for `syscall_stub_gen.py`) and `ply` (for `bitfield_gen.py`) in addition to `protobuf` and `grpcio-tools` (for nanopb). Without these, the build fails during code generation. The WSL setup doc omits `lxml` and `ply` because they were pre-installed on that system.

### Alternative: No-sudo Installation

If you cannot use `sudo` (e.g., shared machine, locked account), you can install most tools locally:

```bash
# 1. Bootstrap pip
curl -sS https://bootstrap.pypa.io/get-pip.py -o /tmp/get-pip.py
python3 /tmp/get-pip.py --user --break-system-packages

# 2. Install cmake, ninja, and Python deps via pip
~/.local/bin/pip3 install --user --break-system-packages \
  cmake ninja protobuf grpcio-tools pyelftools lxml ply

# 3. Install Google repo tool
mkdir -p ~/.local/bin
curl -sS https://storage.googleapis.com/git-repo-downloads/repo > ~/.local/bin/repo
chmod +x ~/.local/bin/repo

# 4. Download and extract system packages from debs
cd /tmp
apt download libxml2-utils device-tree-compiler
for deb in /tmp/*.deb; do dpkg -x "$deb" ~/.local; done

# 5. Add to PATH
export PATH="$HOME/.local/bin:$HOME/.local/usr/bin:$PATH"
```

**QEMU without sudo** is more involved. The `qemu-system-x86` package has many shared library dependencies and hardcodes its module directory at `/usr/lib/x86_64-linux-gnu/qemu`. The workaround used here:

```bash
# Download qemu and all its library dependencies
cd /tmp
for pkg in qemu-system-x86 qemu-system-common qemu-system-data seabios \
           ipxe-qemu vgabios libfdt1 libpmem1 libndctl6 libdaxctl1 \
           liburing2 libslirp0 libaio1t64 libbpf1 libnuma1; do
    apt download "$pkg"
done
for deb in /tmp/*.deb; do dpkg -x "$deb" ~/.local; done

# Link firmware files
ln -sf ~/.local/usr/share/seabios/bios-256k.bin ~/.local/usr/share/qemu/
ln -sf ~/.local/usr/share/seabios/bios.bin ~/.local/usr/share/qemu/
for rom in ~/.local/usr/lib/ipxe/qemu/*.rom; do ln -sf "$rom" ~/.local/usr/share/qemu/; done
for f in ~/.local/usr/share/vgabios/vgabios*.bin; do ln -sf "$f" ~/.local/usr/share/qemu/; done

# Patch QEMU binary to find modules in /tmp/q/m instead of /usr/lib/x86_64-linux-gnu/qemu
mkdir -p /tmp/q
ln -sf ~/.local/usr/lib/x86_64-linux-gnu/qemu /tmp/q/m
cp ~/.local/usr/bin/qemu-system-x86_64 ~/.local/usr/bin/qemu-system-x86_64-patched
python3 -c "
with open('$HOME/.local/usr/bin/qemu-system-x86_64-patched', 'rb') as f:
    data = f.read()
old = b'/usr/lib/x86_64-linux-gnu/qemu'
new = b'/tmp/q/m' + b'\x00' * (len(old) - 8)
data = data.replace(old, new)
with open('$HOME/.local/usr/bin/qemu-system-x86_64-patched', 'wb') as f:
    f.write(data)
"
chmod +x ~/.local/usr/bin/qemu-system-x86_64-patched

# Run with:
export LD_LIBRARY_PATH="$HOME/.local/usr/lib/x86_64-linux-gnu:$HOME/.local/usr/lib"
qemu-system-x86_64-patched -L ~/.local/usr/share/qemu [options...]
```

**Note:** `gcc-multilib` and `g++-multilib` cannot be installed without sudo. They are not required for x86-64 builds (only needed if targeting IA32). The seL4 x86-64 build succeeds without them.

---

## Differences from WSL Setup

| Aspect | WSL2 | Native Ubuntu |
|--------|------|---------------|
| Python packages | `protobuf`, `grpcio-tools`, `pyelftools` | Also needs `lxml`, `ply` (pre-installed in WSL) |
| QEMU performance | Slightly slower (Hyper-V overhead) | Native KVM available (faster) |
| QEMU binary | System-installed via apt | Can use patched local binary if no sudo |
| `gcc-multilib` | Needed for completeness | Not needed for x86-64 target |
| KVM acceleration | Not available in WSL2 | Available (`-enable-kvm` flag) |
| File paths | `/mnt/c/Users/jluca/Documents/JARVIS_OS/` | `~/Desktop/JARVIS_OS/` |
| Build time | ~3 minutes | ~2 minutes (native disk I/O faster) |

---

## Build Steps

### 1. Clone seL4 Test Manifest

```bash
mkdir -p ~/sel4-x86 && cd ~/sel4-x86
repo init -u https://github.com/seL4/sel4test-manifest.git
repo sync -j4
```

**Time:** ~5-10 minutes for initial sync.

### 2. Build seL4 Test Suite

```bash
cd ~/sel4-x86
mkdir -p cbuild && cd cbuild
../init-build.sh -DPLATFORM=x86_64 -DSIMULATION=1
ninja
```

**Time:** ~2 minutes (184 build steps).

**Output:**
- `images/kernel-x86_64-pc99` (1.2 MB) -- seL4 kernel
- `images/sel4test-driver-image-x86_64-pc99` (3.6 MB) -- test rootserver image

### 3. Run seL4 Test Suite

```bash
./simulate    # Ctrl-A, X to exit QEMU
```

Or manually:
```bash
qemu-system-x86_64 \
  -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
  -nographic -serial mon:stdio -m size=3G \
  -kernel images/kernel-x86_64-pc99 \
  -initrd images/sel4test-driver-image-x86_64-pc99
```

If using patched local QEMU (no-sudo install):
```bash
export LD_LIBRARY_PATH="$HOME/.local/usr/lib/x86_64-linux-gnu:$HOME/.local/usr/lib"
~/.local/usr/bin/qemu-system-x86_64-patched \
  -L ~/.local/usr/share/qemu \
  -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
  -nographic -serial mon:stdio -m size=3G \
  -kernel images/kernel-x86_64-pc99 \
  -initrd images/sel4test-driver-image-x86_64-pc99
```

### 4. Test Results

```
Test suite passed. 123 tests passed. 57 tests disabled.
All is well in the universe
```

Matches the WSL result exactly.

---

## Build JARVIS Custom Rootserver

### 5. Create JARVIS Project

Fork sel4test, replace driver with JARVIS sources:

```bash
cd ~/sel4-x86

# Fork sel4test project
cp -r projects/sel4test projects/jarvis-x86

# Remove test infrastructure
rm -rf projects/jarvis-x86/apps/sel4test-tests
rm -rf projects/jarvis-x86/libsel4testsupport
rm -rf projects/jarvis-x86/docs
rm -rf projects/jarvis-x86/apps/sel4test-driver/src/*

# Create source directories
mkdir -p projects/jarvis-x86/apps/sel4test-driver/src/ai
mkdir -p projects/jarvis-x86/apps/sel4test-driver/src/ipc

# Copy JARVIS sources
JARVIS=~/Desktop/JARVIS_OS
cp "$JARVIS/phase3/src/sel4/main_x86.c" projects/jarvis-x86/apps/sel4test-driver/src/main.c

# AI modules
for f in decision_cache.{c,h} cache_patterns.{c,h} tensor_ops.{c,h} \
         dequant.{c,h} tokenizer.{c,h} sampling.{c,h} gguf_parser.h; do
    cp "$JARVIS/phase3/src/ai/$f" projects/jarvis-x86/apps/sel4test-driver/src/ai/
done

# IPC modules
for f in ring_buffer.{c,h} dual_ring_buffer.{c,h} shmem_ipc.{c,h} sel4_atomics.h; do
    cp "$JARVIS/phase3/src/ipc/$f" projects/jarvis-x86/apps/sel4test-driver/src/ipc/
done
```

### 6. Rewrite CMakeLists.txt

**Top-level** (`projects/jarvis-x86/CMakeLists.txt`):
- Change `project(sel4test ...)` to `project(jarvis-x86 C ASM)`
- Remove `add_subdirectory(../sel4test-tests ...)` reference
- Keep `settings.cmake` and `domain_schedule.c` as-is

**Driver** (`projects/jarvis-x86/apps/sel4test-driver/CMakeLists.txt`):
Replace with minimal version listing JARVIS source files:

```cmake
cmake_minimum_required(VERSION 3.16.0)
project(sel4test-driver C)
set(configure_string "")

find_package(musllibc REQUIRED)
find_package(util_libs REQUIRED)
find_package(seL4_libs REQUIRED)

musllibc_setup_build_environment_with_sel4runtime()
sel4_import_libsel4()
util_libs_import_libraries()
sel4_libs_import_libraries()

add_config_library(sel4test-driver "${configure_string}")

add_executable(sel4test-driver EXCLUDE_FROM_ALL
    src/main.c
    src/ai/decision_cache.c
    src/ai/cache_patterns.c
    src/ai/tensor_ops.c
    src/ai/dequant.c
    src/ai/tokenizer.c
    src/ai/sampling.c
    src/ipc/ring_buffer.c
    src/ipc/dual_ring_buffer.c
    src/ipc/shmem_ipc.c
)

target_include_directories(sel4test-driver PRIVATE "src" "src/ai" "src/ipc")
target_link_libraries(sel4test-driver PUBLIC
    sel4_autoconf muslc sel4 sel4runtime sel4allocman sel4vka
    sel4utils sel4platsupport sel4muslcsys)
target_compile_options(sel4test-driver PRIVATE -Werror -g -O2)

include(rootserver)
DeclareRootserver(sel4test-driver)
```

**Key:** The executable name must remain `sel4test-driver` because internal CMake references (elfloader, CPIO, etc.) use this name. Renaming it breaks the build.

### 7. Build JARVIS Rootserver

```bash
cd ~/sel4-x86
mkdir -p jbuild && cd jbuild
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake \
  -DPLATFORM=x86_64 -DSIMULATION=1 \
  -DSEL4_CACHE_DIR=$HOME/.sel4_cache \
  -C ../projects/jarvis-x86/settings.cmake \
  ../projects/jarvis-x86
ninja    # ~197 build steps
```

**Output:**
- `images/kernel-x86_64-pc99` (1.2 MB)
- `images/sel4test-driver-image-x86_64-pc99` (417 KB)

### 8. Run JARVIS Rootserver

```bash
./simulate    # Uses generated simulate script
```

Or manually (including no-sudo QEMU variant):
```bash
# Standard install:
qemu-system-x86_64 \
  -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
  -nographic -serial mon:stdio -m size=3G \
  -kernel images/kernel-x86_64-pc99 \
  -initrd images/sel4test-driver-image-x86_64-pc99

# No-sudo patched QEMU:
export LD_LIBRARY_PATH="$HOME/.local/usr/lib/x86_64-linux-gnu:$HOME/.local/usr/lib"
~/.local/usr/bin/qemu-system-x86_64-patched \
  -L ~/.local/usr/share/qemu \
  -cpu Nehalem,-vme,+pdpe1gb,-xsave,-xsaveopt,-xsavec,-fsgsbase,-invpcid,+syscall,+lm,enforce \
  -nographic -serial mon:stdio -m size=3G \
  -kernel images/kernel-x86_64-pc99 \
  -initrd images/sel4test-driver-image-x86_64-pc99
```

Exit with `Ctrl-A, X`.

---

## JARVIS Rootserver Output (Captured)

```
==================================================
  JARVIS AI-OS v0.3.0-dev | seL4 x86-64 | PC99
==================================================

Boot: 91 untypeds

[JARVIS] Init decision cache...
Loaded 50 initial patterns into cache
Loaded 258 total patterns into cache
[JARVIS] Loaded 308 patterns

--- Cache Queries ---
  [MISS] what is the system status
  [HIT]  check disk space -> show_disk_usage
  [MISS] list running processes
  [MISS] show memory usage
  [MISS] restart the web server
  [MISS] unknown query test
  [MISS] show network interfaces
  [MISS] open text editor

--- SHIELD ---
  [SHIELD] check health -> ALLOWED
  [SHIELD] delete all data -> BLOCKED
  [SHIELD] rm -rf / -> BLOCKED

--- Stats ---
  Queries: 8
  Hits:    1
  Misses:  7
  Rate:    12%

=== JARVIS Self-Test Mode ===

[1/5] Decision Cache ... 1/8 hits ... PASS
[2/5] SHIELD Safety ... PASS
[3/5] Tensor Operations ... 5/5 PASS
[4/5] Dequantization ... 3/3 PASS
[5/5] Tokenizer + Sampling ... 4/4 PASS

=== Self-Test: 5/5 PASS ===

[JARVIS] System idle.
```

All 5 self-tests pass:
1. Decision Cache -- 308 patterns loaded, queries work
2. SHIELD Safety -- dangerous commands blocked, safe commands allowed
3. Tensor Operations -- add, matmul, softmax, rms_norm, silu (5/5)
4. Dequantization -- f16_to_f32, Q4_0 dequant, type metadata (3/3)
5. Tokenizer + Sampling -- BPE encode/decode, greedy sampling, RNG (4/4)

---

## Known Issues and Workarounds

### 1. Missing Python packages: `lxml` and `ply`

The WSL setup doc lists only `protobuf`, `grpcio-tools`, and `pyelftools`. On a fresh Ubuntu install, you also need `lxml` (XML parsing for seL4 syscall stub generation) and `ply` (parser for bitfield code generation). Without them, the build fails at the code generation stage.

### 2. QEMU without sudo: binary patching required

The `qemu-system-x86_64` binary hardcodes `/usr/lib/x86_64-linux-gnu/qemu` as its module directory. When installed via `dpkg -x` to a user directory, QEMU cannot find its TCG accelerator module. The workaround is to:
1. Create a short symlink `/tmp/q/m` pointing to the actual module directory
2. Patch the binary to replace the hardcoded path (same length, null-padded)
3. Also symlink SeaBIOS/iPXE/vgabios firmware into the QEMU data directory
4. Set `LD_LIBRARY_PATH` for shared libraries
5. Use `-L` flag to specify the firmware data directory

This is fragile and version-specific. If you have sudo access, use `apt install` instead.

### 3. AppArmor blocks unprivileged user namespaces

Ubuntu 24.04 sets `apparmor_restrict_unprivileged_userns=1` by default, blocking `unshare --user --mount --map-root-user`. This prevents using user namespaces as an alternative to binary patching for QEMU.

### 4. gcc-multilib not required for x86-64

The `gcc-multilib` and `g++-multilib` packages (which provide 32-bit compilation support) are not needed when building seL4 for x86-64. The build completes without them.

### 5. Linker warnings (harmless)

The build produces warnings about missing `.note.GNU-stack` sections and `RWX` LOAD segments. These are expected with the seL4 build system and do not affect functionality.

### 6. QEMU post-test noise

After the seL4 test suite finishes (or JARVIS rootserver enters idle), QEMU continues outputting kernel debug messages about null capability invocations. This is normal -- the rootserver yields in a loop, triggering debug output. Exit with `Ctrl-A, X`.

---

## Environment Summary

| Component | Version |
|-----------|---------|
| Ubuntu | 24.04.2 LTS (native boot) |
| Kernel | 6.17.0-19-generic |
| GCC | 13.3.0 |
| CMake | 4.3.0 (via pip) |
| Ninja | 1.13.0 (via pip) |
| QEMU | 8.2.2 (Debian package, patched) |
| Python | 3.12 |
| seL4 | Latest from sel4test-manifest |

---

*Setup verified: March 24, 2026*
*Host: Ubuntu 24.04.2 LTS native, Ryzen 7 2700X*
*seL4 test result: 123 PASS, 0 FAIL, 57 disabled*
*JARVIS rootserver: 5/5 self-test PASS*
*Build integration: Fork sel4test, replace driver sources*
