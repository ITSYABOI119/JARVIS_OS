/*
 * JARVIS AI-OS -- POSIX Stubs Implementation
 * Minimal stubs for ggml on seL4. Each stub documented with what
 * the real implementation needs on bare metal.
 *
 * Compile with: -DJARVIS_POSIX_STUBS
 *
 * === Compilation Test Results (March 22, 2026) ===
 *
 * TESTED: llama.cpp latest (ggml-org/llama.cpp, depth-1 clone)
 * COMPILER: gcc (Ubuntu 13.x) on WSL2, x86-64
 *
 * RESULT: SUCCESS -- ggml core compiles and links with stubs
 *
 * Build flags that make ggml.c compile cleanly:
 *   -std=c11 -DNDEBUG -D_POSIX_C_SOURCE=199309L -D_GNU_SOURCE
 *   -DGGML_VERSION=\"test\" -DGGML_COMMIT=\"test\"
 *   -DGGML_USE_CPU
 *
 * Object file compilation (all PASS):
 *   ggml.c       -> ggml.o       (230,832 bytes)
 *   ggml-alloc.c -> ggml-alloc.o  (21,592 bytes)
 *   ggml-quants.c-> ggml-quants.o (181,608 bytes)
 *
 * Integration test: 16/16 PASS
 *   - ggml_init, ggml_new_tensor_1d, ggml_get_data_f32, ggml_add
 *   - ggml_type_name, ggml_type_size, ggml_nelements, ggml_nbytes
 *   - ggml_free (no crash)
 *
 * POSIX symbols needed by ggml.o (from nm -u):
 *   REQUIRED by musl libc (no stub needed):
 *     malloc, calloc, free, realloc, memcpy, memcmp, memmove, memset
 *     strcmp, strncmp, fprintf, snprintf, vsnprintf, fputs, fflush
 *     abort, qsort, getenv, stderr, stdout
 *     ceilf, expf, logf, sqrtf, roundf, lroundf, ldexpf, log2f
 *
 *   REQUIRED (stub or real impl needed):
 *     clock_gettime  -- THIS FILE provides stub
 *     posix_memalign -- musl libc provides this
 *     sysconf        -- THIS FILE provides stub (only _SC_NPROCESSORS_ONLN, _SC_PAGESIZE)
 *
 *   DEBUG-ONLY (only in ggml_print_backtrace, guarded by __linux__):
 *     backtrace, backtrace_symbols_fd -- execinfo.h, skip on seL4
 *     fork, execlp, pipe, read, close, waitpid -- for addr2line, skip
 *     fopen, fclose, getline -- for /proc/self/maps, skip
 *     prctl, getpid -- Linux-specific, #ifdef guarded
 *
 *   COMPILER BUILTINS:
 *     __stack_chk_fail -- stack protector, -fno-stack-protector to skip
 *     __fprintf_chk etc -- fortified IO, -U_FORTIFY_SOURCE to skip
 */
#ifdef JARVIS_POSIX_STUBS

#include "posix_stubs.h"
#include <stdio.h>

/* clock_gettime: On seL4 x86-64, map to HPET or TSC timer driver.
 * The real implementation will read the hardware timer and convert
 * ticks to seconds + nanoseconds.
 * Stub returns 0 (all timings will be zero). */
int clock_gettime(int clk_id, struct timespec *tp)
{
    (void)clk_id;
    if (tp) { tp->tv_sec = 0; tp->tv_nsec = 0; }
    return 0;
}

/* sysconf: System configuration values.
 * On seL4 bare metal, we run single-core initially (Phase 3).
 * Multi-core support (SMP) is a Phase 4 enhancement. */
long sysconf(int name)
{
    switch (name) {
        case _SC_NPROCESSORS_ONLN: return 1;
        case _SC_PAGESIZE:         return 4096;
        default:                   return -1;
    }
}

#endif /* JARVIS_POSIX_STUBS */
