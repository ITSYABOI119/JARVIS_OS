/*
 * JARVIS AI-OS — Phase 5 #6: Cache Growth — promotion selector (pure, host-testable)
 *
 * Selects repeated query->action patterns from a scanned episodic window for
 * promotion into the decision cache. Frequency-based, deterministic, no model:
 * the cache learns FREQUENTLY-ASKED queries and serves them fast — it never
 * "understands" them.
 *
 * PURE: no seL4, no device, no decision_cache dependency. The caller passes
 * already-hashed query_keys (FNV-1a, decision-cache parity — an episodic key IS
 * a cache key) and owns the text lifetime (candidates carry pointers + lengths
 * into the caller's episodic buffer; text is compared/deduped by query_key only,
 * never strlen'd — episodic text is not NUL-terminated).
 *
 * Design: phase5/docs/PHASE_5_GOAL6_CACHE_GROWTH.md (D-b: frequency >= threshold).
 */

#ifndef CACHE_GROWTH_H
#define CACHE_GROWTH_H

#include <stdint.h>

/* D-b: a query_key seen >= this many times in the scan window is a "repeated pattern". */
#define CG_PROMOTE_THRESHOLD 2

/* One scanned episodic record. Text is borrowed (not owned, not NUL-terminated):
 * compare/dedup by query_key only — never strlen/strcmp the borrowed text. */
typedef struct {
    uint64_t     query_key;   /* cache_hash(cache_normalize_query(query)) — episodic/cache parity */
    const char  *query;       /* borrowed normalized query text (query_len bytes) */
    uint16_t     query_len;
    const char  *action;      /* borrowed logged response/action (action_len bytes) */
    uint16_t     action_len;
} cg_candidate_t;

typedef cg_candidate_t cg_promotion_t;

/*
 * Select query_keys whose frequency over cands[0..n) is >= threshold, emitting the
 * NEWEST candidate per promoted key (input order is oldest->newest; later wins) into
 * out[0..max). Deduplicated by query_key. Returns the promotion count (0..max).
 *
 * Pure: no allocation, no I/O. Text is carried by pointer + *_len (never copied here,
 * never strlen'd). A promoted key appears exactly once in out, holding its latest action.
 */
int cg_select_promotions(const cg_candidate_t *cands, int n, int threshold,
                         cg_promotion_t *out, int max);

#endif /* CACHE_GROWTH_H */
