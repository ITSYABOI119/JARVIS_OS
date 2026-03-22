# ggml Portability Notes for seL4 Bare Metal

**Date:** March 19, 2026 (updated March 22, 2026)
**Tested with:** llama.cpp latest (depth-1 clone), ggml core C files
**Target:** seL4 x86-64 userspace with musl libc (no Linux, no POSIX runtime)

---

## 1. Build Test Results

### Standard GCC (glibc) -- ggml core C files

| File | Result | Notes |
|------|--------|-------|
| `ggml.c` | **PASS** (with flags) | Needs `-DGGML_VERSION -DGGML_COMMIT -D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE` |
| `ggml-alloc.c` | **PASS** | Clean compile |
| `ggml-quants.c` | **PASS** | Clean compile |

**Required build flags for ggml.c:**
```
-std=c11 -DNDEBUG -O2
-D_POSIX_C_SOURCE=199309L    # exposes clock_gettime, CLOCK_MONOTONIC
-D_GNU_SOURCE                # exposes getline (debug only)
-DGGML_VERSION=\"test\"      # cmake-generated, must be provided
-DGGML_COMMIT=\"test\"       # cmake-generated, must be provided
-DGGML_USE_CPU               # selects CPU backend path
```

Without `-D_POSIX_C_SOURCE=199309L`: 5 errors (CLOCK_MONOTONIC, M_PI undeclared, clock_gettime undeclared).
Without `-DGGML_VERSION`/`-DGGML_COMMIT`: 2 errors (undeclared identifiers).

### Integration Test (March 22, 2026)

**16/16 PASS** -- ggml core library links and runs with stub backend.

Tests verified:
- `ggml_init` (context creation with 16MB arena)
- `ggml_new_tensor_1d` (F32 x 4)
- `ggml_get_data_f32` (direct data pointer access)
- `ggml_add` (graph node creation)
- `ggml_nelements`, `ggml_nbytes`, `ggml_type_name`, `ggml_type_size`
- `ggml_free` (cleanup, no crash)

### Object File Sizes

| File | Object Size | Notes |
|------|------------|-------|
| `ggml.o` | 230,832 bytes | Core tensor library |
| `ggml-alloc.o` | 21,592 bytes | Allocator |
| `ggml-quants.o` | 181,608 bytes | Quantization (AVX2/NEON) |
| **Total** | **434,032 bytes** | ~424 KB for all core tensor math |

### musl-gcc -- Not tested (musl-tools not installed in WSL at time of test)

Expected: same results as gcc plus potential `execinfo.h` issue (already fixed upstream -- ggml guards it with `#ifdef __linux__` and checks for backtrace support).

---

## 2. Source File Inventory (Updated March 22, 2026)

### Pure C files (seL4-portable candidates)

| File | LOC | Language | seL4 Viable? |
|------|-----|----------|-------------|
| `ggml/src/ggml.c` | 7,736 | C11 | **Yes** -- compiles + links with stubs |
| `ggml/src/ggml-alloc.c` | 1,244 | C11 | **Yes** -- clean compile |
| `ggml/src/ggml-quants.c` | 5,416 | C11 | **Yes** -- clean, AVX2/NEON quantization |
| `ggml/src/ggml-cpu/ggml-cpu.c` | 3,750 | C11 | **Likely** -- needs investigation |
| `ggml/src/ggml-cpu/quants.c` | ? | C11 | **Likely** -- arch-specific quantization |

### C++ files (require C++ runtime)

| File | Language | seL4 Viable? |
|------|----------|-------------|
| `ggml/src/ggml-backend.cpp` | C++17 | **Problem** -- needs `<vector>`, `<algorithm>` |
| `ggml/src/ggml-threading.cpp` | C++17 | **Problem** -- needs `<mutex>` |
| `ggml/src/ggml-backend-reg.cpp` | C++17 | **Problem** -- needs STL |
| `ggml/src/ggml-backend-dl.cpp` | C++17 | Skippable (dynamic loading) |
| `ggml/src/ggml-opt.cpp` | C++17 | Skippable (training, not needed for inference) |
| `ggml/src/ggml.cpp` | C++17 | Wrapper/utility code |
| `ggml/src/gguf.cpp` | C++17 | Replaced by our `gguf_parser.c` |
| `ggml/src/ggml-cpu/ggml-cpu.cpp` | C++17 | CPU backend registration |
| `ggml/src/ggml-cpu/ops.cpp` | C++17 | Tensor operations implementation |
| `ggml/src/ggml-cpu/binary-ops.cpp` | C++17 | Binary ops (add, mul, etc.) |
| `ggml/src/ggml-cpu/unary-ops.cpp` | C++17 | Unary ops (relu, gelu, etc.) |
| `ggml/src/ggml-cpu/vec.cpp` | C++17 | Vector math |
| `ggml/src/ggml-cpu/traits.cpp` | C++17 | Type traits |
| `ggml/src/ggml-cpu/repack.cpp` | C++17 | Tensor repacking |
| `ggml/src/ggml-cpu/hbm.cpp` | C++17 | High-bandwidth memory |

**Key finding:** ggml's core tensor library is pure C (`ggml.c`, `ggml-quants.c`, `ggml-alloc.c`) and compiles + links + runs with trivial stubs. But the compute backend (ops, threading, graph execution) is now C++ in the CPU backend directory. A seL4 port either needs:
1. C++ support in seL4 userspace (muslc++ / libstdc++ -- possible but heavy), OR
2. A C-only compute path (write custom graph executor and tensor ops in C)

---

## 3. Symbol Dependencies (from nm -u, March 22, 2026)

### ggml.o external symbols -- categorized for seL4

**musl libc provides (no stub needed):**
```
malloc, calloc, free, realloc           -- memory allocation
memcpy, memcmp, memmove, memset         -- memory operations
strcmp, strncmp                          -- string comparison
fprintf, snprintf, vsnprintf, fputs     -- formatted output (route to UART)
fflush, stderr, stdout                  -- stdio streams
abort, _Exit                            -- process termination
qsort                                   -- sorting
ceilf, expf, logf, sqrtf, roundf       -- math (libm)
lroundf, ldexpf, log2f                  -- math (libm)
```

**posix_stubs.c provides:**
```
clock_gettime                           -- timer (stub returns 0; real: HPET/TSC)
sysconf                                 -- system config (_SC_NPROCESSORS_ONLN, _SC_PAGESIZE)
```

**ggml_backend_stubs.c provides (for linking without full backend):**
```
ggml_critical_section_start/end         -- threading mutex (no-op on single-threaded)
ggml_backend_tensor_memset              -- backend buffer memset
ggml_backend_tensor_set                 -- backend buffer copy
ggml_backend_buffer_*                   -- buffer management (14 functions)
ggml_backend_buft_*                     -- buffer type management (5 functions)
ggml_backend_tensor_alloc              -- tensor allocation in buffer
ggml_backend_view_init                 -- view initialization
ggml_backend_get_default_buffer_type   -- default buffer type
ggml_backend_multi_buffer_alloc_buffer -- multi-buffer allocation
```

**Debug-only (skippable with -DNDEBUG or on non-Linux):**
```
backtrace, backtrace_symbols_fd         -- execinfo.h (guarded)
fork, execlp, pipe, read, close, waitpid -- addr2line subprocess
fopen, fclose, getline                  -- /proc/self/maps parsing
prctl, getpid                           -- #ifdef __linux__ guarded
getenv                                  -- env variable lookup
```

**Compiler builtins:**
```
__stack_chk_fail                        -- stack protector (-fno-stack-protector to skip)
__fprintf_chk, __snprintf_chk, etc.    -- fortified IO (-U_FORTIFY_SOURCE to skip)
```

### ggml-alloc.o external symbols

Mostly `ggml_backend_*` functions (provided by backend stubs or real backend).
Plus: `ggml_get_first_tensor`, `ggml_get_next_tensor`, `ggml_get_no_alloc`, `ggml_hash_set_*`, `ggml_log_internal` (all from ggml.o).

### ggml-quants.o external symbols

Math functions (libm) plus: `ggml_abort`, `ggml_row_size`, `ggml_type_name`, `ggml_type_size` (all from ggml.o).

---

## 4. Platform Guards Already in ggml.c

ggml.c has 14+ platform `#ifdef` checks. Key ones:

```c
#ifdef __linux__        // Linux-specific (prctl, /proc/self/maps)
#ifdef _WIN32           // Windows-specific (VirtualAlloc, QueryPerformanceCounter)
#ifdef __APPLE__        // macOS-specific (mach_absolute_time, Accelerate.framework)
#ifdef __EMSCRIPTEN__   // WebAssembly (no threading, no backtrace)
#ifdef __gnu_linux__    // GNU-specific (syscall.h)
```

**Strategy:** Define none of these on seL4 -- ggml will fall through to the generic POSIX path, which needs the fewest stubs.

---

## 5. Recommended Stub Strategy for seL4

### Files created (March 22, 2026)

| File | Purpose | LOC |
|------|---------|-----|
| `posix_stubs.h` | POSIX type/constant stubs (timespec, CLOCK_MONOTONIC, _SC_*) | ~30 |
| `posix_stubs.c` | `clock_gettime()` + `sysconf()` implementations | ~25 |
| `ggml_backend_stubs.c` | No-op stubs for ggml-backend and ggml-threading | ~120 |
| `test_ggml_integration.c` | 16-test integration test | ~90 |

### Build flags for seL4 bare metal

```
-std=c11 -O2 -DNDEBUG
-DGGML_VERSION=\"0.3\"
-DGGML_COMMIT=\"sel4-phase3\"
-DGGML_USE_CPU
-DJARVIS_POSIX_STUBS             # Enable our POSIX stubs
-fno-stack-protector             # Skip __stack_chk_fail
-U_FORTIFY_SOURCE                # Skip __*_chk variants
-I phase3/src/ai                 # For posix_stubs.h
```

### Stubs needed on seL4 bare metal

```c
// posix_stubs.c -- 2 stubs needed
clock_gettime()   // Real impl: read x86 TSC/HPET
sysconf()         // Return 1 CPU, 4096 page size

// ggml_backend_stubs.c -- ~20 no-op stubs
// These become real implementations when we write the CPU backend
ggml_critical_section_start/end()    // No-op (single-threaded)
ggml_backend_tensor_memset/set()     // memset/memcpy on tensor->data
ggml_backend_buffer_*()              // Simple flat buffer management
```

### What musl libc handles (no stubs needed)

```
posix_memalign    -- musl provides this natively
alloca            -- compiler builtin / alloca.h
fprintf/snprintf  -- musl stdio (route to UART fd)
malloc/free       -- musl heap
signal            -- musl provides (no-op on seL4)
math functions    -- musl libm
```

---

## 6. The C++ Problem

The biggest portability challenge is NOT the C tensor math -- it's the C++ backend system. The compute operations (add, mul, matmul, etc.) are implemented in C++ files within `ggml/src/ggml-cpu/`.

### Current ggml architecture (as of March 2026)

```
ggml.c (C)              -- tensor library, graph building, type system
ggml-alloc.c (C)        -- memory allocation for graphs
ggml-quants.c (C)       -- quantization/dequantization
ggml-backend.cpp (C++)  -- backend abstraction layer
ggml-threading.cpp (C++) -- thread pool, mutexes
ggml-cpu/ops.cpp (C++)  -- tensor operation implementations (THE BIG ONE)
ggml-cpu/vec.cpp (C++)  -- vector math
ggml-cpu/*.cpp           -- other CPU backend components
```

### Options

| Option | Effort | Risk | Notes |
|--------|--------|------|-------|
| **A: Add C++ support to seL4 userspace** | Medium (2-3 weeks) | Medium | Link muslc++ or picolibc++; seL4 tutorials use C only but C++ is possible |
| **B: Write C-only model loader + backend** | High (3-4 weeks) | Low | Custom GGUF parser in C, bypass ggml-backend.cpp entirely |
| **C: Use an older ggml version (pre-C++ split)** | Low (research) | Medium | Older ggml was pure C; may lack optimizations |
| **D: Extract minimal C subset** | Medium (2-3 weeks) | Low | Take ggml.c + ggml-quants.c + custom C loader + custom C ops |

**Recommendation:** Continue with **Option D** -- the tensor math (ggml.c, ggml-quants.c, ggml-alloc.c) is proven to compile and run with trivial stubs. Write a minimal C compute backend that implements the ~10 ops needed for transformer inference (matmul, add, mul, softmax, rms_norm, rope, silu, gelu). This avoids C++ entirely.

**UPDATE (March 19, 2026): Option D IMPLEMENTED (GGUF parser).** C-only GGUF parser written and tested:
- `gguf_parser.h` -- header with all GGUF types, ggml type enum, structs, API
- `gguf_parser.c` -- ~400 LOC, parses header/KV/tensor info/data, fopen/fread only
- `test_gguf_parser.c` -- 9/9 tests pass (embedded test data, no external files)
- Handles: strings, uint32, float32, arrays (skipped), tensor data read, alignment
- Ready for seL4 integration: replace fopen/fread with AHCI block reads

**UPDATE (March 22, 2026): POSIX stubs created and tested.**
- `posix_stubs.h/c` -- clock_gettime + sysconf stubs
- `ggml_backend_stubs.c` -- ~20 no-op backend stubs
- `test_ggml_integration.c` -- 16/16 PASS
- ggml.c + ggml-alloc.c + ggml-quants.c compile as .o files: CONFIRMED
- Full link + run with stubs: CONFIRMED
- **Remaining gap: C++ ops (ops.cpp, vec.cpp, binary-ops.cpp, unary-ops.cpp)**

---

## 7. Remaining Gap Analysis

### What works now (March 22, 2026)

| Component | Status | Files |
|-----------|--------|-------|
| Tensor type system | WORKING | ggml.c |
| Context/arena allocation | WORKING | ggml.c |
| Tensor creation | WORKING | ggml.c |
| Graph building (ggml_add, etc.) | WORKING | ggml.c |
| Quantization/dequantization | COMPILED | ggml-quants.c |
| Memory allocator | COMPILED | ggml-alloc.c |
| GGUF model file parsing | WORKING | gguf_parser.c (our C-only impl) |
| POSIX stubs | WORKING | posix_stubs.c |

### What's missing for actual inference

| Component | Gap | Effort | Strategy |
|-----------|-----|--------|----------|
| `ggml_graph_compute()` | Needs CPU backend | 2-3 weeks | Write C-only graph executor |
| Tensor ops (matmul, add, softmax...) | In C++ ops.cpp | 2-3 weeks | Port ~10 critical ops to C |
| Thread pool | In C++ threading.cpp | 1 week | Single-threaded first, add later |
| AVX2 SIMD math | In C++ vec.cpp | 1 week | Extract SIMD intrinsics to C |

### Estimated total remaining: 4-6 weeks for a working C-only inference path

This is scheduled for Phase 3b (Weeks 19-24 in the implementation plan).

---

## 8. x86-Specific Optimizations Available

ggml automatically detects and uses:
- **AVX2** -- 256-bit SIMD (Ryzen 5 5600 supports this)
- **FMA** -- fused multiply-add
- **F16C** -- half-precision float conversion

These are enabled via `<immintrin.h>` which works on any x86 compiler. No portability issue -- the Ryzen will get full SIMD acceleration.

---

## 9. Summary

| Category | Count | Status |
|----------|-------|--------|
| Required POSIX stubs | 2 | DONE (clock_gettime, sysconf) |
| Backend stubs (for linking) | ~20 | DONE (ggml_backend_stubs.c) |
| Skippable POSIX deps | 10+ | Already `#ifdef` guarded or debug-only |
| C files that compile + link | 3/3 | ggml.c, ggml-alloc.c, ggml-quants.c -- ALL PASS |
| Integration test | 16/16 | PASS -- context, tensors, graph nodes, types, cleanup |
| C++ files (need replacement) | 10+ | ops.cpp is the big one -- Phase 3b work |
| Build flags needed | 7 | VERSION, COMMIT, NDEBUG, _POSIX_C_SOURCE, _GNU_SOURCE, USE_CPU, JARVIS_POSIX_STUBS |

**Bottom line:** The core ggml tensor library (ggml.c, ggml-alloc.c, ggml-quants.c) compiles, links, and runs on x86-64 with just 2 POSIX stubs + ~20 backend stubs. The remaining gap is the C++ compute backend (ops.cpp, vec.cpp) which needs to be rewritten in C for seL4. This is ~4-6 weeks of work, scheduled for Phase 3b.

---

## References

- [llama.cpp build docs](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md)
- [ggml musl/Alpine issue #8762](https://github.com/ggml-org/llama.cpp/issues/8762) -- execinfo.h fix
- [ggml source](https://github.com/ggml-org/llama.cpp/tree/master/ggml/src)
