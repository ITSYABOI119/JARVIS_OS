# ggml Portability Notes for seL4 Bare Metal

**Date:** March 19, 2026
**Tested with:** llama.cpp latest (depth-1 clone), ggml core C files
**Target:** seL4 x86-64 userspace with musl libc (no Linux, no POSIX runtime)

---

## 1. Build Test Results

### Standard GCC (glibc) — ggml core C files

| File | Result | Notes |
|------|--------|-------|
| `ggml.c` | **FAIL** (2 errors, 3 warnings) | Missing `GGML_VERSION`/`GGML_COMMIT` defines (cmake-generated); `getline`, `posix_memalign`, `clock_gettime` implicit decl warnings |
| `ggml-alloc.c` | **PASS** | Clean compile |
| `ggml-quants.c` | **PASS** | Clean compile |

The `ggml.c` errors are trivially fixable: `-DGGML_VERSION=\"0.0\" -DGGML_COMMIT=\"none\"` resolves both. The warnings are POSIX function declarations that exist in glibc but need stubs for seL4.

### musl-gcc — Not tested (musl-tools not installed in WSL at time of test)

Expected: same results as gcc plus potential `execinfo.h` issue (already fixed upstream — ggml guards it with `#ifdef __linux__` and checks for backtrace support).

---

## 2. Source File Inventory

### Pure C files (seL4-portable candidates)

| File | LOC | Language | seL4 Viable? |
|------|-----|----------|-------------|
| `ggml/src/ggml.c` | ~5,000 | C11 | **Yes** — with stubs |
| `ggml/src/ggml-alloc.c` | ~800 | C11 | **Yes** — clean |
| `ggml/src/ggml-quants.c` | ~12,000 | C11 | **Yes** — clean, AVX2/NEON quantization |

### C++ files (require C++ runtime)

| File | Language | seL4 Viable? |
|------|----------|-------------|
| `ggml-backend.cpp` | C++17 | **Problem** — needs `<vector>`, `<algorithm>` |
| `ggml-threading.cpp` | C++17 | **Problem** — needs `<mutex>` |
| `ggml-backend-reg.cpp` | C++17 | **Problem** — needs STL |
| `ggml-opt.cpp` | C++17 | Not needed for inference |
| `gguf.cpp` | C++17 | Needed for model loading |

**Key finding:** ggml's core tensor math is pure C (`ggml.c`, `ggml-quants.c`, `ggml-alloc.c`). But the backend system and model loading are C++. A seL4 port either needs:
1. C++ support in seL4 userspace (muslc++ / libstdc++ — possible but heavy), OR
2. A C-only inference path (write custom model loader + backend dispatcher in C)

---

## 3. POSIX Dependencies Found

### In ggml.c (core tensor library)

| Dependency | Header | Usage | Required? | seL4 Stub Strategy |
|-----------|--------|-------|-----------|-------------------|
| `posix_memalign()` | `<stdlib.h>` | Aligned memory allocation for SIMD | **Required** | musl provides this; or stub with `aligned_alloc()` (C11) |
| `clock_gettime()` | `<time.h>` | Performance timing | **Required** | Map to x86 TSC/HPET timer driver |
| `getline()` | `<stdio.h>` | Backtrace printing (debug only) | Skippable | Only in `ggml_print_backtrace()`, guarded by `#ifdef` |
| `fopen()`/`fclose()` | `<stdio.h>` | Backtrace via `/proc/self/maps` | Skippable | Debug only |
| `fprintf()` | `<stdio.h>` | Debug/error output | **Required** | Route to serial console (`uart_16550_puts`) |
| `execinfo.h` | `<execinfo.h>` | `backtrace()` function | Skippable | Already guarded by `#ifndef __EMSCRIPTEN__` + cmake check |
| `signal.h` | `<signal.h>` | `SIGABRT` handler | Skippable | Not needed on seL4; stub as no-op |
| `sys/prctl.h` | `<sys/prctl.h>` | Thread naming (Linux) | Skippable | `#ifdef __linux__` guarded |
| `unistd.h` | `<unistd.h>` | `sysconf(_SC_PAGESIZE)` | **Required** | Return 4096 (x86 page size) |
| `dlfcn.h` | `<dlfcn.h>` | Dynamic library loading | Skippable | Backend loading; not needed for static build |
| `alloca.h` | `<alloca.h>` | Stack allocation | **Required** | musl provides this |

### In ggml-backend.cpp (C++ backend system)

| Dependency | Usage | Required? | Notes |
|-----------|-------|-----------|-------|
| `<vector>` | Backend registry | **Required** for C++ path | Need libstdc++ or libc++ |
| `<algorithm>` | Sorting | **Required** for C++ path | Need STL |
| `<mutex>` | Thread safety | **Required** for C++ path | Need pthreads or C++ threading |
| `sys/sysctl.h` | macOS physical memory | Skippable | `#ifdef __APPLE__` guarded |

### In ggml-threading.cpp

| Dependency | Usage | Required? | Notes |
|-----------|-------|-----------|-------|
| `<mutex>` | Thread synchronization | Optional | Single-threaded mode avoids this |
| pthreads | Thread pool | Optional | Can run single-threaded |

---

## 4. Platform Guards Already in ggml.c

ggml.c has 14+ platform `#ifdef` checks. Key ones:

```c
#ifdef __linux__        // Linux-specific (prctl, /proc/self/maps)
#ifdef _WIN32           // Windows-specific (VirtualAlloc, QueryPerformanceCounter)
#ifdef __APPLE__        // macOS-specific (mach_absolute_time, Accelerate.framework)
#ifdef __EMSCRIPTEN__   // WebAssembly (no threading, no backtrace)
```

**Strategy:** Define none of these on seL4 — ggml will fall through to the generic POSIX path, which needs the fewest stubs.

---

## 5. Recommended Stub Strategy for seL4

### Minimal stubs needed (phase3/src/ai/posix_stubs.c)

```c
// 1. posix_memalign — musl libc provides this, no stub needed
// 2. clock_gettime — map to x86 timer
int clock_gettime(clockid_t clk, struct timespec *ts) {
    uint64_t ticks = x86_timer_read();  // from x86_timer driver
    ts->tv_sec = ticks / 1000000;
    ts->tv_nsec = (ticks % 1000000) * 1000;
    return 0;
}

// 3. fprintf — route to serial
int fprintf(FILE *f, const char *fmt, ...) {
    // format to buffer, output via uart_16550_puts()
}

// 4. sysconf
long sysconf(int name) {
    if (name == _SC_PAGESIZE) return 4096;
    if (name == _SC_NPROCESSORS_ONLN) return 1;  // single-threaded
    return -1;
}

// 5. signal — no-op
void (*signal(int sig, void (*handler)(int)))(int) { return NULL; }
```

### Build flags to minimize dependencies

```
-DGGML_VERSION=\"0.1\"
-DGGML_COMMIT=\"sel4\"
-DNDEBUG                    # Disable debug asserts/backtrace
-DGGML_USE_LLAMAFILE=0      # Disable llamafile backend
-DGGML_OPENMP=0             # Disable OpenMP threading
```

---

## 6. The C++ Problem

The biggest portability challenge is NOT the C tensor math — it's the C++ backend system. `ggml-backend.cpp`, `ggml-threading.cpp`, and `gguf.cpp` (model file parser) are all C++ with STL dependencies.

### Options

| Option | Effort | Risk | Notes |
|--------|--------|------|-------|
| **A: Add C++ support to seL4 userspace** | Medium (2-3 weeks) | Medium | Link muslc++ or picolibc++; seL4 tutorials use C only but C++ is possible |
| **B: Write C-only model loader + backend** | High (3-4 weeks) | Low | Custom GGUF parser in C, bypass ggml-backend.cpp entirely |
| **C: Use an older ggml version (pre-C++ split)** | Low (research) | Medium | Older ggml was pure C; may lack optimizations |
| **D: Extract minimal C subset** | Medium (2-3 weeks) | Low | Take ggml.c + ggml-quants.c + custom C loader |

**Recommendation:** Start with **Option D** — the tensor math (ggml.c, ggml-quants.c, ggml-alloc.c) is pure C and compiles with trivial stubs. Write a minimal GGUF parser in C (~500-800 LOC) that loads weights into ggml tensors. Skip the full backend system. This avoids C++ entirely.

**UPDATE (March 19, 2026): Option D IMPLEMENTED.** C-only GGUF parser written and tested:
- `gguf_parser.h` — header with all GGUF types, ggml type enum, structs, API
- `gguf_parser.c` — ~400 LOC, parses header/KV/tensor info/data, fopen/fread only
- `test_gguf_parser.c` — 9/9 tests pass (embedded test data, no external files)
- Handles: strings, uint32, float32, arrays (skipped), tensor data read, alignment
- Ready for seL4 integration: replace fopen/fread with AHCI block reads

---

## 7. x86-Specific Optimizations Available

ggml automatically detects and uses:
- **AVX2** — 256-bit SIMD (Ryzen 5 5600 supports this)
- **FMA** — fused multiply-add
- **F16C** — half-precision float conversion

These are enabled via `<immintrin.h>` which works on any x86 compiler. No portability issue — the Ryzen will get full SIMD acceleration.

---

## 8. Summary

| Category | Count | Status |
|----------|-------|--------|
| Required POSIX stubs | 5 | Straightforward (clock, fprintf, sysconf, signal, alloca) |
| Skippable POSIX deps | 6 | Already `#ifdef` guarded or debug-only |
| C files that compile clean | 2/3 | ggml-alloc.c, ggml-quants.c pass; ggml.c needs 2 defines |
| C++ files (problematic) | 5 | Need either C++ runtime or C-only alternative |
| Build flags needed | 5 | GGML_VERSION, GGML_COMMIT, NDEBUG, no-OPENMP, no-LLAMAFILE |

**Bottom line:** The core tensor math ports to seL4 with ~5 trivial stubs. The real work is replacing the C++ model loading/backend system with a C-only equivalent. Estimated additional effort: 2-3 weeks for Option D (C-only GGUF loader).

---

## References

- [llama.cpp build docs](https://github.com/ggml-org/llama.cpp/blob/master/docs/build.md)
- [ggml musl/Alpine issue #8762](https://github.com/ggml-org/llama.cpp/issues/8762) — execinfo.h fix
- [ggml source](https://github.com/ggml-org/llama.cpp/tree/master/ggml/src)
