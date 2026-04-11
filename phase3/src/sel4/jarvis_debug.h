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
#define JARVIS_DBG_PB      1

/* Ring health checks before send ([PB] ring @... magic=... w= r=) */
#define JARVIS_DBG_RING    0

/* Periodic stats every 100 queries ([STATS] q= hits= infer= ...) — keep on for stability */
#define JARVIS_DBG_STATS   1

/* Per-inference summary line (query + response snippet) */
#define JARVIS_DBG_INFER_SUMMARY  1

/* Per-forward-pass tracing in llama_quant.c ([L00@N], [FWD], [TOP5@N]).
 * NOTE: llama_quant.c does NOT include this header (it is built in both seL4
 * and standalone native-test contexts). It carries its own #ifndef fallback
 * that defaults to 0. To enable, compile llama_quant.c with -DJARVIS_DBG_FORWARD=1.
 * Documented here for discoverability. */

#endif /* JARVIS_DEBUG_H */
