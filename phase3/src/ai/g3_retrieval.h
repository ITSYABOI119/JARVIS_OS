/*
 * JARVIS AI-OS — Phase 5 Goal #3 (Retrieval Before Inference), Milestone M0
 *
 * The retrieval SCORER + preamble ASSEMBLER as pure, host-testable C — NO seL4 /
 * device / model dependency. This is the consumer LOGIC only: sourcing the
 * candidate records (pool key→record / NVMe text) is M1, and injecting the
 * preamble into Process B's prompt is M2. Design: phase5/docs/
 * PHASE_5_GOAL3_RETRIEVAL.md (§2 scorer = exact-key + recency; §3 preamble).
 *
 * MVP scorer (§2): exact query_key match first, then a recency fallback — exact-key
 * and recency are the only signals that exist (decision-cache FNV-1a parity); the
 * caller passes the already-hashed query_key (no cache_hash here). All text fields
 * are NOT NUL-terminated — always copy by *_len, never strlen.
 */

#ifndef G3_RETRIEVAL_H
#define G3_RETRIEVAL_H

#include <stdint.h>

/* Tunables (§3 / D-d). */
#define G3_MAX_FACTS        2     /* preamble holds at most this many facts */
#define G3_Q_MAX            80    /* per-field query truncation (bytes) */
#define G3_R_MAX            120   /* per-field response truncation (bytes) */
#define PREAMBLE_MAX_BYTES  512   /* total preamble cap (< ~600 so a prompt_ids[256] holds it) */

/* A retrieval candidate — decoupled from epi_record_t so this stays pure/host-testable.
 * query/resp are NOT NUL-terminated; always use query_len/resp_len. */
typedef struct {
    uint64_t    query_key;
    uint32_t    seq;          /* monotonic recency index (higher = newer) */
    uint16_t    action;
    uint8_t     outcome;
    const char *query;
    uint16_t    query_len;
    const char *resp;
    uint16_t    resp_len;
} g3_candidate_t;

/* Select up to `max` records into out[]: the EXACT query_key match first (newest seq wins,
 * deduped by key), then a RECENCY fallback (newest seq, deduped by key) for the remaining
 * slots. Deterministic + stable (input order breaks seq ties). Returns the count written
 * (0..max). out must hold `max` entries. */
int g3_select(const g3_candidate_t *cands, int n, uint64_t query_key, int max, g3_candidate_t *out);

/* Assemble a delimited preamble from the selected records into out[0..cap):
 *   "Known context:\n" then per fact "- " query(<=G3_Q_MAX) ": " resp(<=G3_R_MAX) "\n".
 * Per-field AND total (min(cap, PREAMBLE_MAX_BYTES)) truncation; copies text by *_len
 * (never strlen); always NUL-terminates; NEVER writes past cap. Returns the preamble length
 * (0 when n<=0 — the "no relevant memory => no preamble" path). */
int g3_build_preamble(const g3_candidate_t *sel, int n, char *out, int cap);

#endif /* G3_RETRIEVAL_H */
