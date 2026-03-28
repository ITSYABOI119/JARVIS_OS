# Running JARVIS AI-OS

## Quick Run (if already built)

```bash
cd ~/sel4-x86/jbuild
timeout 600 qemu-system-x86_64 -enable-kvm -cpu host -nographic -serial mon:stdio -m 8G -kernel images/kernel-x86_64-pc99 -initrd images/sel4test-driver-image-x86_64-pc99
```

Exit QEMU: press **Ctrl+A** then **X**

Takes 2-3 minutes to boot (772MB image loading + model init).

---

## Full Build + Run (if build tree is clean or missing)

### Step 1: Install dependencies (first time only)

```bash
sudo apt install -y build-essential cmake ninja-build python3 python3-pip gcc-multilib g++-multilib qemu-system-x86 repo git libxml2-utils device-tree-compiler
pip3 install --break-system-packages protobuf grpcio-tools pyelftools lxml ply
```

### Step 2: Clone seL4 (first time only)

```bash
mkdir -p ~/sel4-x86 && cd ~/sel4-x86
repo init -u https://github.com/seL4/sel4test-manifest.git
repo sync -j4
```

### Step 3: Build

```bash
cd ~/sel4-x86
rm -rf jbuild && mkdir jbuild && cd jbuild
export PATH="$HOME/.local/bin:$HOME/.local/usr/bin:$PATH"
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake -DPLATFORM=x86_64 -DSIMULATION=1 -DSEL4_CACHE_DIR=$HOME/.sel4_cache -DJARVIS_EMBED_MODEL=$HOME/Desktop/JARVIS_OS/phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf -C ../projects/jarvis-x86/settings.cmake ../projects/jarvis-x86
ninja
```

Build takes ~3-5 minutes. The model (771MB) is embedded via objcopy.

### Step 4: Run

```bash
cd ~/sel4-x86/jbuild
timeout 600 qemu-system-x86_64 -enable-kvm -cpu host -nographic -serial mon:stdio -m 8G -kernel images/kernel-x86_64-pc99 -initrd images/sel4test-driver-image-x86_64-pc99
```

---

## What You'll See

```
==================================================
  JARVIS AI-OS v0.3.0-dev | seL4 x86-64 | PC99
==================================================

Boot: 107 untypeds

[JARVIS] Init decision cache...
[JARVIS] Loaded 308 patterns

--- Cache Queries ---
  [HIT]  check disk space -> show_disk_usage
  [MISS] unknown query test

--- SHIELD ---
  [SHIELD] delete all data -> BLOCKED
  [SHIELD] rm -rf / -> BLOCKED

=== JARVIS Self-Test Mode ===
[1/5] Decision Cache ... PASS
[2/5] SHIELD Safety ... PASS
[3/5] Tensor Operations ... 5/5 PASS
[4/5] Dequantization ... 3/3 PASS
[5/5] Tokenizer + Sampling ... 4/4 PASS
=== Self-Test: 5/5 PASS ===

[JARVIS] Spawning inference process...
[JARVIS] Inference process configured
[JARVIS] Shared memory mapped in Process A
[JARVIS] Shared memory mapped in Process B
[JARVIS] Process B started

[Process B] Inference server started
[Process B] Model: 770 MB
[Process B] Model loaded: 16 layers, 128256 vocab
[Process B] Tokenizer ready: 128256 tokens
[Process B] Ready for inference requests

[JARVIS] Process B ready (type=4 seq=0)
[JARVIS] Query: "The seL4 microkernel is"
[JARVIS] Waiting for inference response...
[JARVIS] Response: " a microkernel implementation of the L4 microkernel
architecture. It is designed to be a lightweight alternative..."

[JARVIS] System idle.
```

---

## Build Without Model (fast, for testing build system)

```bash
cd ~/sel4-x86
rm -rf jbuild && mkdir jbuild && cd jbuild
export PATH="$HOME/.local/bin:$HOME/.local/usr/bin:$PATH"
cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=../kernel/gcc.cmake -DPLATFORM=x86_64 -DSIMULATION=1 -DSEL4_CACHE_DIR=$HOME/.sel4_cache -C ../projects/jarvis-x86/settings.cmake ../projects/jarvis-x86
ninja
```

This builds in ~1 minute. Process B starts but goes to idle (no model).

---

## Run Host Tests (no QEMU needed)

```bash
cd ~/Desktop/JARVIS_OS

# Run all Phase 3 AI tests
for f in phase3/src/ai/test_*.c; do
  name=$(basename "$f" .c)
  echo "=== $name ==="
  gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
    "$f" phase3/src/ai/*.c -lm -o "/tmp/$name" 2>/dev/null && "/tmp/$name" 2>&1 | tail -1
done
```

---

## Key Info

- **QEMU RAM:** 8GB required (772MB image + process spawning overhead)
- **KVM:** Required for reasonable speed (-enable-kvm -cpu host)
- **CNode:** 22 bits (4M capability slots)
- **Morecore:** 128MB (shared by both processes)
- **Model:** Llama 3.2 1B Q4_K_M (771MB, embedded in Process B's .rodata)
- **Inference time:** ~2-3 minutes for 20 tokens in QEMU (much faster on real hardware)
