/*
 * JARVIS AI-OS Phase 4 — Goal #1 / Milestone M2
 * SMP viability probe (compile-guarded by JARVIS_SMP_PROBE).
 *
 * PURPOSE: prove the kernel booted multicore (NUM_NODES=2 => BSP + 1 AP) and
 * observe how the existing two-process system places on cores under SMP — the
 * recon that feeds the M3 seL4-native threadpool design.
 *
 * The PRIMARY gate is in main_x86.c: Process A reads bootinfo->numNodes and
 * logs "SMP numNodes=N nodeID=M apic=K" to the NVMe log. numNodes==2 proves
 * both cores booted (the kernel sets bi->numNodes from the actual started CPU
 * count — boot.c populate_bi_frame); numNodes==1 means the AP did not start
 * (a clean finding, not a mystery — likely LAPIC AP-bringup).
 *
 * This header only provides the local-APIC-id read so each process can report
 * which core it ran on (E1: do PA and PB land on the same core by default, or
 * does the scheduler distribute them?). CPUID is unprivileged — leaf 1 EBX
 * [31:24] = initial local APIC id, readable at CPL3. No <immintrin.h>, no libc.
 *
 * DEFAULT OFF: JARVIS_SMP_PROBE defaults to 0 in jarvis_debug.h; the M2 spike
 * build sets it = 1. Code stays in place behind the guard (bare-metal debug
 * rule: compile-guard, do not delete). The deployed image rebuilds with it = 0.
 */
#ifndef JARVIS_SMP_PROBE_H
#define JARVIS_SMP_PROBE_H

#include <stdint.h>

/* Initial local-APIC id of the core this thread is currently running on.
 * CPUID leaf 1, EBX[31:24]. Unprivileged; valid in Ring 3. */
static inline uint32_t smp_apic_id(void)
{
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return (b >> 24) & 0xFFu;
}

#endif /* JARVIS_SMP_PROBE_H */
