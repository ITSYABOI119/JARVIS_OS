/*
 * JARVIS AI-OS — Phase 5 Goal #2 (Shared Context Pool), Milestone M0 — engine.
 *
 * Single-writer SEQLOCK over the system_state snapshot + two single-writer rings
 * (event_stream, recent_decisions). Concurrency is GCC __atomic_* only (the proven
 * shmem_ipc.c raw-atomics style): the seqlock makes the read torn-free, and reading
 * the snapshot fields via relaxed __atomic_load_n (rather than plain loads) keeps it
 * ThreadSanitizer-clean — TSan treats __atomic_* as synchronization, so the
 * intentionally-racy seqlock data path does not false-positive.
 *
 * No seL4 / device / allocator dependency (the cross-process page mapping is M1).
 * Design: phase5/docs/PHASE_5_GOAL2_SHARED_CONTEXT_POOL.md.
 */

#include "shared_context.h"
#include <string.h>

/* Torn-read detector stamp — a deterministic function of the 6 carried fields
 * (NOT of `consistency` itself). Must match test_shared_context.c::consistency_of. */
static uint64_t sctx_consistency(const sctx_system_state_t *s) {
    return (uint64_t)s->boot_id ^ s->uptime_ms ^ s->q_total ^ s->q_hits ^ s->q_infer ^ s->last_query_key;
}

void sctx_init(shared_context_t *c, uint32_t boot_id) {
    memset(c, 0, sizeof(*c));   /* seq=0 (even), heads=0, rings zeroed */
    sctx_system_state_t s;
    memset(&s, 0, sizeof s);
    s.boot_id = boot_id;
    sctx_publish_state(c, &s);  /* publish an initial consistent snapshot (seq 0->2) */
}

void sctx_publish_state(shared_context_t *c, const sctx_system_state_t *s) {
    /* Single writer. An ACQ_REL RMW to ODD brackets the field writes (the acquire half
     * keeps the field stores from leaking before the bump); the RELEASE store to EVEN
     * publishes them to a reader that ACQUIRE-loads `seq`. No standalone
     * atomic_thread_fence — that is unsupported under -fsanitize=thread (-Wtsan) and the
     * RMW/release pairing gives the same ordering while staying TSan-clean. */
    uint32_t odd = __atomic_add_fetch(&c->seq, 1u, __ATOMIC_ACQ_REL);   /* odd: write in progress */

    __atomic_store_n(&c->st.boot_id,        s->boot_id,        __ATOMIC_RELAXED);
    __atomic_store_n(&c->st.uptime_ms,      s->uptime_ms,      __ATOMIC_RELAXED);
    __atomic_store_n(&c->st.q_total,        s->q_total,        __ATOMIC_RELAXED);
    __atomic_store_n(&c->st.q_hits,         s->q_hits,         __ATOMIC_RELAXED);
    __atomic_store_n(&c->st.q_infer,        s->q_infer,        __ATOMIC_RELAXED);
    __atomic_store_n(&c->st.last_query_key, s->last_query_key, __ATOMIC_RELAXED);
    __atomic_store_n(&c->st.consistency,    sctx_consistency(s), __ATOMIC_RELAXED);

    __atomic_store_n(&c->seq, odd + 1u, __ATOMIC_RELEASE);   /* even: snapshot complete */
}

int sctx_read_state(const shared_context_t *c, sctx_system_state_t *out) {
    int retries = 0;
    for (;;) {
        uint32_t v1 = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
        if (v1 & 1u) { retries++; continue; }   /* writer mid-update — spin */

        out->boot_id        = __atomic_load_n(&c->st.boot_id,        __ATOMIC_RELAXED);
        out->_pad0          = 0;
        out->uptime_ms      = __atomic_load_n(&c->st.uptime_ms,      __ATOMIC_RELAXED);
        out->q_total        = __atomic_load_n(&c->st.q_total,        __ATOMIC_RELAXED);
        out->q_hits         = __atomic_load_n(&c->st.q_hits,         __ATOMIC_RELAXED);
        out->q_infer        = __atomic_load_n(&c->st.q_infer,        __ATOMIC_RELAXED);
        out->last_query_key = __atomic_load_n(&c->st.last_query_key, __ATOMIC_RELAXED);
        out->consistency    = __atomic_load_n(&c->st.consistency,    __ATOMIC_RELAXED);

        /* ACQUIRE re-load (not a standalone fence — TSan-clean): if `seq` is unchanged and
         * even, no publish overlapped the field reads, so the snapshot is consistent. */
        uint32_t v2 = __atomic_load_n(&c->seq, __ATOMIC_ACQUIRE);
        if (v1 == v2) return retries;            /* stable across the read — consistent */
        retries++;                                /* a write landed mid-read — retry */
    }
}

void sctx_push_event(shared_context_t *c, uint64_t t_ms, uint64_t query_key, uint16_t kind) {
    uint32_t h    = __atomic_load_n(&c->ev_head, __ATOMIC_RELAXED);  /* single writer */
    uint32_t slot = h % SCTX_EVENT_RING;
    c->events[slot].t_ms      = t_ms;
    c->events[slot].query_key = query_key;
    c->events[slot].kind      = kind;
    __atomic_store_n(&c->ev_head, h + 1u, __ATOMIC_RELEASE);          /* publish the entry */
}

void sctx_record_decision(shared_context_t *c, uint64_t query_key, uint16_t action,
                          uint8_t outcome, uint8_t committed) {
    uint32_t h    = __atomic_load_n(&c->dec_head, __ATOMIC_RELAXED);  /* single writer */
    uint32_t slot = h % SCTX_DECISION_RING;
    c->decisions[slot].query_key = query_key;
    c->decisions[slot].seq       = h;
    c->decisions[slot].action    = action;
    c->decisions[slot].outcome   = outcome;
    c->decisions[slot].committed = committed;
    __atomic_store_n(&c->dec_head, h + 1u, __ATOMIC_RELEASE);         /* publish the entry */
}

/* Number of decision slots currently populated (capped at the ring size). */
static uint32_t sctx_dec_count(const shared_context_t *c, uint32_t *head_out) {
    uint32_t h = __atomic_load_n(&c->dec_head, __ATOMIC_ACQUIRE);
    *head_out = h;
    return h < SCTX_DECISION_RING ? h : SCTX_DECISION_RING;
}

int sctx_lookup_key(const shared_context_t *c, uint64_t query_key, sctx_decision_t *out) {
    uint32_t h;
    uint32_t count = sctx_dec_count(c, &h);
    for (uint32_t i = 0; i < count; i++) {                 /* newest -> oldest */
        uint32_t slot = (h - 1u - i) % SCTX_DECISION_RING;
        if (c->decisions[slot].query_key == query_key) {
            *out = c->decisions[slot];
            return 0;
        }
    }
    return -1;
}

int sctx_recent(const shared_context_t *c, int n, sctx_decision_t *out, int max) {
    if (n <= 0 || max <= 0) return 0;
    uint32_t h;
    uint32_t count = sctx_dec_count(c, &h);
    int want = n;
    if (want > (int)count) want = (int)count;
    if (want > max) want = max;
    for (int i = 0; i < want; i++)                         /* newest -> oldest */
        out[i] = c->decisions[(h - 1u - (uint32_t)i) % SCTX_DECISION_RING];
    return want;
}
