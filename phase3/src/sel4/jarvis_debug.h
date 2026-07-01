/*
 * JARVIS AI-OS — Debug Configuration
 * Compile-time flags to control diagnostic output.
 * Set to 1 to enable, 0 to disable.
 *
 * For the deployed/stability config: diagnostic flags are 0 except JARVIS_DBG_STATS and
 * JARVIS_DBG_INFER_SUMMARY; feature flags follow their own notes (JARVIS_G3_RETRIEVAL is OFF /
 * opt-in — the M6 flip-to-default-ON is pending the offline A/B). Enable diagnostics as needed.
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

/* Phase 4 goal #1 / M2: SMP viability probe (smp_probe.h). When 1, Process A
 * reads bootinfo->numNodes and logs "SMP numNodes=N nodeID=M apic=K" to the NVMe
 * log + serial (the primary M2 gate — numNodes==2 proves BSP + 1 AP both booted),
 * and Process B prints its local-APIC id to serial at startup (E1: do PA and PB
 * land on different cores by default?). Compile guard ONLY; does NOT enable SMP
 * (that is the kernel build: -DSMP=ON -DNUM_NODES=2). Default 0 — the M2 spike
 * build sets it = 1; the deployed image rebuilds with it = 0. */
#define JARVIS_SMP_PROBE  0

/* Phase 5 G3 (Retrieval Before Inference): master switch for the retrieval path.
 * DEFAULT OFF / opt-in. When 1, Process A — on the inference route,
 * BEFORE MSG_QUERY — scores the recent episodic batch (g3_select), assembles a preamble, and
 * PACKS it into the shared context pool (sctx_pack_preamble), logging one [RETR] line per
 * inference; Process B then INJECTS that preamble into the Gemma prompt between the user turn's
 * `\n` and the question (bounded by g3_prompt_budget, prompt_ids[256], KV stays 512). Box-
 * verified M0–M5: inject + coherent prose (g3_candidate_usable filter) + <50 ms on a cache miss
 * + NVMe-backed post-reboot recall. When 0, the whole PA block + PB injection + their
 * g3_retrieval.h include compile out — deploy byte-identical, no new dependency (OFF==baseline).
 * PA links g3_retrieval.c (build_jarvis_x86.sh); PB uses only the header's static-inline
 * g3_prompt_budget.
 * M6 (flip-to-default-ON) is PENDING the offline OFF-vs-ON A/B: retrieval WORKS + is USED
 * ([PROBE] hit=1), but M2/M3 showed it broadens/meta-izes answers, so whether it HELPS is
 * unproven. Flip to 1 only after the A/B shows it helps, not just works. */
#define JARVIS_G3_RETRIEVAL  0

/* Phase 5 G3/M4c: synthetic-fact PROBE harness (box-only). When 1 (alongside JARVIS_G3_RETRIEVAL=1),
 * Process A seeds ONE known INFER fact (marker SYNTHPROBEZX9Q7) whose query matches an inference
 * query, then self-checks (case-insensitive contains) whether the model echoes the marker after
 * retrieval injects it — proving the injected memory is PRESENT-AND-USABLE (NOT "memory helped",
 * which stays an offline A/B question). Default 0 → the seed + self-check compile out, deploy
 * byte-identical. The operator confirms `[PROBE] … hit=1` (+ `[RETR] lat_us=` < 50 ms) in a
 * flag-ON QEMU smoke. */
#define JARVIS_G3_PROBE  0

/* Phase 5 #6 cache-growth — canon = promote repeated query→action patterns from the EPISODIC LOG
 * into the decision cache (see phase5/docs/PHASE_5_GOAL6_CACHE_GROWTH.md). The route-through-cache
 * impl (commit 7e8c30f) was REVERTED (wrong design vs canon); this flag is RETAINED — the canon
 * promotion pass reuses it (default 0 → compiles out, deploy byte-identical; box-only). */
#define JARVIS_CACHE_GROWTH 0

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
