/*
 * JARVIS AI-OS -- POSIX Stubs for seL4 Bare Metal
 * Provides minimal implementations of POSIX functions needed by ggml.
 * On seL4, these map to kernel/driver calls. On Linux, real POSIX is used.
 *
 * Compile with: -DJARVIS_POSIX_STUBS to enable these stubs.
 * Without the define, this header is a no-op (real POSIX used instead).
 */
#ifndef JARVIS_POSIX_STUBS_H
#define JARVIS_POSIX_STUBS_H

#ifdef JARVIS_POSIX_STUBS

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* Timer stub -- maps to seL4 system timer on bare metal */
#ifndef _POSIX_C_SOURCE
struct timespec { long tv_sec; long tv_nsec; };
#define CLOCK_MONOTONIC 1
#define CLOCK_REALTIME  0
int clock_gettime(int clk_id, struct timespec *tp);
#endif

/* System config -- single-core for Phase 3 bare metal */
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 84
#define _SC_PAGESIZE         30
#endif
long sysconf(int name);

#endif /* JARVIS_POSIX_STUBS */
#endif /* JARVIS_POSIX_STUBS_H */
