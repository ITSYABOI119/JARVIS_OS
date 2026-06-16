/*
 * JARVIS AI-OS Phase 4 — Goal #1 / Milestone M0
 * AVX2-under-preemption safety probe (compile-guarded by JARVIS_AVX2_PROBE).
 *
 * PURPOSE: prove the seL4 kernel context-switches AVX (YMM/XSAVE) state for a
 * Ring-3 process. The deployed kernel was FXSAVE-only (saves x87+SSE, NOT the
 * YMM upper-128) — M0 rebuilds it KernelFPU=XSAVE / XSaveFeatureSet=7 so YMM is
 * saved. This probe is the empirical gate that the rebuild actually works.
 *
 * ISOLATION (critical): the AVX2 code is reachable ONLY through a function with a
 * function-level __attribute__((target("avx2,fma"))). The rest of the build stays
 * scalar — there is NO global -mavx2 here (that is M1). Uses gcc vector extensions
 * (256-bit YMM), NOT <immintrin.h>, so it compiles under the seL4 -nostdinc build.
 *
 * DEFAULT OFF: JARVIS_AVX2_PROBE defaults to 0 in jarvis_debug.h; the M0 run builds
 * with it = 1. Code is left in place behind the guard (bare-metal debug rule:
 * compile-guard, do not delete).
 *
 * Logging is via seL4_DebugPutChar (available in both Process A and Process B).
 */
#ifndef JARVIS_AVX2_PROBE_H
#define JARVIS_AVX2_PROBE_H

#include <stdint.h>
#include <sel4/sel4.h>

/* 4 x f64 = 256-bit => forces YMM registers when AVX is enabled */
typedef double avx2_v4df __attribute__((vector_size(32)));

#define AVX2_PROBE_LEN   64        /* elements per pass (multiple of 4) */
#define AVX2_PROBE_ITER  800000u   /* outer reps: long enough (~ms) to be preempted mid-YMM-accumulate */
#define AVX2_TOUCH_ITER  40000u    /* lighter YMM-dirty for the PA workload loop */
#define AVX2_LOG_EVERY   64        /* log a running summary every N probe steps */

/* ---- tiny serial helpers (independent of each TU's own printers) ---- */
static inline void avx2_puts(const char *s) { while (*s) seL4_DebugPutChar(*s++); }
static inline void avx2_put_u64(uint64_t v) {
    char b[21]; int i = 0;
    if (v == 0) { seL4_DebugPutChar('0'); return; }
    while (v) { b[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (--i >= 0) seL4_DebugPutChar(b[i]);
}
static inline void avx2_put_hex(uint64_t v) {
    avx2_puts("0x");
    for (int s = 60; s >= 0; s -= 4) {
        uint32_t nib = (uint32_t)((v >> s) & 0xF);
        seL4_DebugPutChar((char)(nib < 10 ? '0' + nib : 'a' + nib - 10));
    }
}

/* ---- raw CPUID / XGETBV (no libc, no __builtin_cpu_supports crt dependency) ---- */
static inline void avx2_cpuid(uint32_t leaf, uint32_t sub,
                              uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf), "c"(sub));
}
static inline uint64_t avx2_xgetbv0(void) {
    uint32_t eax, edx;
    __asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));   /* legal at CPL3 iff CR4.OSXSAVE=1 */
    return ((uint64_t)edx << 32) | (uint64_t)eax;
}

/* ---- deterministic data fill (xorshift; no rand()) ---- */
static inline void avx2_fill(double *v, int n, uint64_t seed) {
    uint64_t s = seed ? seed : 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < n; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        v[i] = (double)((int32_t)(s & 0xFFFFu) - 32768) * (1.0 / 4096.0);
    }
}

/* Single-pass exact reference (cheap): full dot of the 64-element pattern. */
static inline double avx2_single_ref(const double *a, const double *b, int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

/*
 * Long AVX2/FMA reduction: accumulate the same dot ITER times in a LIVE YMM
 * accumulator. The asm barrier on `acc` defeats loop-invariant strength reduction
 * (so the YMM accumulator genuinely lives across the whole loop) — meaning a timer
 * preemption that lands inside the loop leaves YMM state live. If the kernel fails
 * to save/restore the YMM upper-128 across that switch (e.g. FXSAVE kernel, or
 * XSAVE without AVX in the feature mask), `acc` is corrupted and the result
 * diverges from the exact reference (iter * single). Isolated target attribute.
 */
__attribute__((target("avx2,fma")))
static double avx2_long_reduce(const double *a, const double *b, int n, unsigned iter) {
    avx2_v4df acc = {0, 0, 0, 0};
    for (unsigned t = 0; t < iter; t++) {
        for (int i = 0; i + 4 <= n; i += 4) {
            avx2_v4df va = *(const avx2_v4df *)(a + i);
            avx2_v4df vb = *(const avx2_v4df *)(b + i);
            acc += va * vb;
        }
        __asm__ volatile("" : "+x"(acc));  /* keep acc live in YMM; block hoisting */
    }
    return acc[0] + acc[1] + acc[2] + acc[3];
}

/* ---- per-translation-unit probe state ---- */
static int      g_avx2_inited = 0;
static int      g_avx2_enabled = 0;     /* 1 iff OSXSAVE && XCR0.AVX (and AVX2 in CPUID) */
static uint64_t g_avx2_xcr0 = 0;
static uint64_t g_avx2_probes = 0;
static uint64_t g_avx2_mismatch = 0;
static volatile double g_avx2_sink = 0; /* keep PA's touch from being optimized away */

/* One-time init: confirm the kernel enabled AVX state-saving (XCR0.AVX). */
static void avx2_probe_init(const char *who) {
    if (g_avx2_inited) return;
    g_avx2_inited = 1;
    uint32_t a, b, c, d;
    avx2_cpuid(1, 0, &a, &b, &c, &d);
    int osxsave = (c >> 27) & 1;     /* ECX.27 — OS enabled XSAVE (CR4.OSXSAVE) */
    int avx     = (c >> 28) & 1;     /* ECX.28 — AVX */
    avx2_cpuid(7, 0, &a, &b, &c, &d);
    int avx2    = (b >> 5) & 1;      /* leaf7 EBX.5 — AVX2 */

    avx2_puts("[AVX2] "); avx2_puts(who);
    avx2_puts(" init: osxsave="); avx2_put_u64((uint64_t)osxsave);
    avx2_puts(" avx="); avx2_put_u64((uint64_t)avx);
    avx2_puts(" avx2="); avx2_put_u64((uint64_t)avx2);

    if (osxsave) {
        g_avx2_xcr0 = avx2_xgetbv0();
        avx2_puts(" xcr0="); avx2_put_hex(g_avx2_xcr0);
        int xcr0_sse = (g_avx2_xcr0 >> 1) & 1;
        int xcr0_avx = (g_avx2_xcr0 >> 2) & 1;
        avx2_puts(" xcr0.sse="); avx2_put_u64((uint64_t)xcr0_sse);
        avx2_puts(" xcr0.avx="); avx2_put_u64((uint64_t)xcr0_avx);
        g_avx2_enabled = (xcr0_sse && xcr0_avx && avx2) ? 1 : 0;
    } else {
        avx2_puts(" xcr0=UNAVAILABLE(OSXSAVE=0 -> kernel did NOT enable XSAVE)");
        g_avx2_enabled = 0;
    }
    avx2_puts(g_avx2_enabled ? " => AVX-ENABLED\n" : " => AVX-DISABLED (probe inert)\n");
}

/* One probe step (Process B / the gate): compares the long YMM reduction against
 * the exact reference; counts + logs mismatches and periodic summaries. */
static void avx2_probe_run(const char *who, uint64_t seed) {
    if (!g_avx2_inited) avx2_probe_init(who);
    if (!g_avx2_enabled) return;

    double pa[AVX2_PROBE_LEN] __attribute__((aligned(32)));
    double pb[AVX2_PROBE_LEN] __attribute__((aligned(32)));
    avx2_fill(pa, AVX2_PROBE_LEN, seed);
    avx2_fill(pb, AVX2_PROBE_LEN, seed ^ 0xD1B54A32D192ED03ULL);

    double single = avx2_single_ref(pa, pb, AVX2_PROBE_LEN);
    double ref    = single * (double)AVX2_PROBE_ITER;
    double got    = avx2_long_reduce(pa, pb, AVX2_PROBE_LEN, AVX2_PROBE_ITER);

    g_avx2_probes++;
    double diff = got - ref; if (diff < 0) diff = -diff;
    double mag  = ref < 0 ? -ref : ref;
    double tol  = 1e-9 * mag + 1e-6;   /* double accumulation drift << this; YMM corruption >> this */

    if (diff > tol) {
        g_avx2_mismatch++;
        avx2_puts("[AVX2] MISMATCH "); avx2_puts(who);
        avx2_puts(" seed="); avx2_put_hex(seed);
        avx2_puts(" probes="); avx2_put_u64(g_avx2_probes);
        avx2_puts(" mism="); avx2_put_u64(g_avx2_mismatch);
        avx2_puts("\n");
    }
    if ((g_avx2_probes % AVX2_LOG_EVERY) == 0) {
        avx2_puts("[AVX2] "); avx2_puts(who);
        avx2_puts(" probes="); avx2_put_u64(g_avx2_probes);
        avx2_puts(" mismatch="); avx2_put_u64(g_avx2_mismatch);
        avx2_puts(" xcr0.avx=1\n");
    }
}

/* Lighter YMM "touch" (Process A): dirties YMM each workload iteration so the
 * kernel's lazy per-TCB FPU path actually exercises cross-thread YMM save/restore
 * (PA becomes an FPU owner competing with PB). No comparison — PB is the gate. */
static void avx2_probe_touch(uint64_t seed) {
    if (!g_avx2_inited) avx2_probe_init("PA");
    if (!g_avx2_enabled) return;
    double pa[AVX2_PROBE_LEN] __attribute__((aligned(32)));
    double pb[AVX2_PROBE_LEN] __attribute__((aligned(32)));
    avx2_fill(pa, AVX2_PROBE_LEN, seed | 1u);
    avx2_fill(pb, AVX2_PROBE_LEN, (seed << 1) ^ 0xABCD1234u);
    g_avx2_sink += avx2_long_reduce(pa, pb, AVX2_PROBE_LEN, AVX2_TOUCH_ITER);
}

#endif /* JARVIS_AVX2_PROBE_H */
