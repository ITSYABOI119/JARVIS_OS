/*
 * JARVIS AI-OS — Debug Configuration
 * Compile-time flags to control diagnostic output.
 * Set to 1 to enable, 0 to disable.
 *
 * For 30-day stability test: all 0 except JARVIS_DBG_STATS.
 * For debugging: enable as needed.
 */

#ifndef JARVIS_DEBUG_H
#define JARVIS_DEBUG_H

/* Per-query IPC tracing in Process A ([DBG] q=N slot=N, send/signal/wait) */
#define JARVIS_DBG_IPC     0

/* Process B inference tracing ([PB] handle_query, generating, decoded) */
#define JARVIS_DBG_PB      0

/* Ring health checks before send ([PB] ring @... magic=... w= r=) */
#define JARVIS_DBG_RING    0

/* Periodic stats every 100 queries ([STATS] q= hits= infer= ...) — keep on for stability */
#define JARVIS_DBG_STATS   1

/* Per-inference summary line (query + response snippet) */
#define JARVIS_DBG_INFER_SUMMARY  1

/* NVMe log writes at every boot stage (FAT32 init, model progress, spawn).
 * Useful for diagnosing stalls. Turn OFF for 30-day stability test (write wear). */
#define JARVIS_DBG_BOOT_LOG  0

/* Phase 4 goal #1 / M0: AVX2-under-preemption safety probe (avx2_probe.h).
 * When 1, Process B runs a target("avx2,fma")-isolated YMM reduction vs a scalar
 * reference interleaved with the live PA<->PB workload, and confirms XCR0.AVX is
 * set (kernel saves AVX state). Default 0 — the M0 gate run builds with it =1.
 * This is a compile guard ONLY; it does NOT enable global -mavx2 (that is M1). */
#define JARVIS_AVX2_PROBE  0

/* Phase 4 goal #1 / M1: decode tok/s measurement. When 1, Process B brackets the
 * generation loop with RDTSC and reports "M1 gen=<n> cyc=<cycles>" via MSG_DEBUG;
 * Process A writes it to the NVMe log (LOG_INFER) + serial. Convert offline:
 * tok/s = n * TSC_HZ / cyc (Ryzen 2700X invariant TSC = 3.7 GHz). Default 0 — set 1
 * only for the M1 bare-metal bench (one LOG_INFER entry per inference). */
#define JARVIS_M1_MEASURE  0

/* Serial [STATS] prints every 100 queries; NVMe LOG_IPC_STATS is written every
 * JARVIS_STATS_NVME_INTERVAL. Measured bare-metal rate is ~3k queries/day (single
 * core, scalar) -> interval=100 gives ~870 entries over 30 days, well under the
 * 2700-entry saturating log. (Task 3 used 1000 on a 16k/day estimate the bare-metal
 * run disproved; q=400 err=0 verified on real hardware 2026-06-15.) */
#define JARVIS_STATS_NVME_INTERVAL  100

/* Per-forward-pass tracing in llama_quant.c ([L00@N], [FWD], [TOP5@N]).
 * NOTE: llama_quant.c does NOT include this header (it is built in both seL4
 * and standalone native-test contexts). It carries its own #ifndef fallback
 * that defaults to 0. To enable, compile llama_quant.c with -DJARVIS_DBG_FORWARD=1.
 * Documented here for discoverability. */

#endif /* JARVIS_DEBUG_H */
