/*
 * JARVIS AI-OS — Phase 5 Goal #2 (Shared Context Pool), Milestone M0
 *
 * A host-testable, page-sized working-memory pool: a system_state snapshot guarded
 * by a single-writer SEQLOCK, plus an event-stream ring and a recent-decisions keyed
 * ring. Greenfield C — plain memory + GCC __atomic_* builtins, NO seL4/device
 * dependency (the cross-process page mapping is M1). Design: phase5/docs/
 * PHASE_5_GOAL2_SHARED_CONTEXT_POOL.md (§2 decisions, §3 Approach A, §6 M0).
 *
 * Single writer = Process A; readers = PB + Process A's own G3 retrieval path. G2
 * ships the container/index/plumbing; relevance scoring + preamble assembly +
 * INJECTION are G3 (kept unmerged — this module never injects).
 *
 * Concurrency: the seqlock counter `seq` and every shared field are accessed ONLY
 * via __atomic_* (the proven shmem_ipc.c raw-atomics style), so the lock-free
 * read-without-remove is correct AND ThreadSanitizer-clean (TSan understands
 * __atomic_* as synchronization — a plain-memory seqlock would false-positive).
 * Keys are decision-cache FNV-1a (cache_hash(cache_normalize_query)) — never
 * re-implemented here — for parity with the episodic store (#1) and cache growth (#6).
 */

#ifndef SHARED_CONTEXT_H
#define SHARED_CONTEXT_H

#include <stdint.h>
#include <stddef.h>

#define SCTX_EVENT_RING     64u   /* latest-N event ring */
#define SCTX_DECISION_RING  64u   /* recent-decisions keyed ring */
#define SCTX_PREAMBLE_MAX   1024  /* D6: preamble staging sub-region (PA packs; G3 reads) */

/* event_stream entry */
typedef struct {
    uint64_t t_ms;
    uint64_t query_key;
    uint16_t kind;
    uint8_t  _pad[6];
} sctx_event_t;

/* recent_decisions entry — keyed via cache_hash; `committed`=0 marks a record that
 * still lives in the uncommitted g_epi_batch (D4: visible before the next NVMe flush). */
typedef struct {
    uint64_t query_key;
    uint32_t seq;        /* monotonic write index (recency) */
    uint16_t action;
    uint8_t  outcome;
    uint8_t  committed;  /* 1 = persisted to the episodic store; 0 = uncommitted batch */
} sctx_decision_t;

/* system_state snapshot (D7 SPEC field-set). `consistency` is a deterministic
 * function of the other fields so a TORN read is detectable in the host test. */
typedef struct {
    uint32_t boot_id;
    uint32_t _pad0;
    uint64_t uptime_ms;
    uint64_t q_total;
    uint64_t q_hits;
    uint64_t q_infer;
    uint64_t last_query_key;
    uint64_t consistency;   /* = boot_id ^ uptime_ms ^ q_total ^ q_hits ^ q_infer ^ last_query_key */
} sctx_system_state_t;

/* The whole pool — one page. Maps PA<->PB at M1; pure RAM here. */
typedef struct {
    _Alignas(64) uint32_t seq;   /* seqlock version (cache-line-aligned; accessed only via __atomic_*) */
    uint8_t  _pad_seq[60];       /* fill the cache line — no false sharing with `st` */
    sctx_system_state_t st;

    uint32_t     ev_head;                    /* monotonic event count (slot = head % RING) */
    sctx_event_t events[SCTX_EVENT_RING];

    uint32_t        dec_head;                /* monotonic decision count */
    sctx_decision_t decisions[SCTX_DECISION_RING];

    uint32_t preamble_len;                   /* D6: staging buffer, packed by PA, read by G3 */
    char     preamble[SCTX_PREAMBLE_MAX];
} shared_context_t;

_Static_assert(sizeof(shared_context_t) <= 4096, "shared context pool must fit one page");

/* ---- API (design §6 / the M0 contract) ---- */

/* Zero the pool and publish an initial consistent state for `boot_id`. */
void sctx_init(shared_context_t *c, uint32_t boot_id);

/* Single-writer seqlock publish: stamps `consistency` and releases an even seq. */
void sctx_publish_state(shared_context_t *c, const sctx_system_state_t *s);

/* Reader snapshot-retry: returns a torn-free copy in *out; returns the retry count. */
int  sctx_read_state(const shared_context_t *c, sctx_system_state_t *out);

/* Append an event to the latest-N ring (single writer). */
void sctx_push_event(shared_context_t *c, uint64_t t_ms, uint64_t query_key, uint16_t kind);

/* Record a decision (keyed ring). committed=0 ⇒ an uncommitted-batch record (D4). */
void sctx_record_decision(shared_context_t *c, uint64_t query_key, uint16_t action,
                          uint8_t outcome, uint8_t committed);

/* Exact-key lookup, NEWEST matching decision → *out; returns 0 on hit, -1 on miss. */
int  sctx_lookup_key(const shared_context_t *c, uint64_t query_key, sctx_decision_t *out);

/* Copy up to min(n, max, stored) recent decisions newest→oldest into out[]; returns count. */
int  sctx_recent(const shared_context_t *c, int n, sctx_decision_t *out, int max);

#endif /* SHARED_CONTEXT_H */
