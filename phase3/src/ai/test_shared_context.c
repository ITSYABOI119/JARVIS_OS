/* JARVIS AI-OS — HOST unit test for the Shared Context Pool engine (Phase 5 G2/M0).
 *
 * Proves the single-writer seqlock is torn-read-safe under ThreadSanitizer (writer +
 * 1/2/4/8 readers, every read must be internally consistent), plus exact-key lookup,
 * recency iteration, the event-ring wrap, and uncommitted-batch visibility (D4). Keys
 * are exercised via the real decision_cache FNV-1a (cache_hash(cache_normalize_query))
 * to prove key parity with #1/#6 — never a re-implemented hash.
 *
 * Build (see .github/workflows/ci.yml):
 *   gcc -D_POSIX_C_SOURCE=200809L -fsanitize=thread -O1 -g -pthread -I phase3/src/ai \
 *     phase3/src/ai/test_shared_context.c phase3/src/ai/shared_context.c \
 *     phase3/src/ai/decision_cache.c -o /tmp/tsctx && /tmp/tsctx
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "shared_context.h"
#include "decision_cache.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

/* The torn-read detector — MUST equal sctx_publish_state's stamp (over the same 6 fields). */
static uint64_t consistency_of(const sctx_system_state_t *s) {
    return (uint64_t)s->boot_id ^ s->uptime_ms ^ s->q_total ^ s->q_hits ^ s->q_infer ^ s->last_query_key;
}

/* ================================================================
 * T1 — torn-read safety under TSan (writer + K readers)
 * ================================================================ */
static shared_context_t g_ctx;
static atomic_int  g_stop;
static atomic_long g_torn;    /* must stay 0 — any torn read increments this */
static atomic_long g_reads;

static void *writer_fn(void *arg) {
    (void)arg;
    uint64_t i = 1;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        sctx_system_state_t s;
        s.boot_id        = (uint32_t)(i & 0xFFFFu) + 1u;
        s._pad0          = 0;
        s.uptime_ms      = i * 7u;
        s.q_total        = i;
        s.q_hits         = i / 2u;
        s.q_infer        = i / 3u;
        s.last_query_key = i * 0x9E3779B97F4A7C15ULL;
        s.consistency    = 0;   /* publish stamps the real value */
        sctx_publish_state(&g_ctx, &s);
        i++;
    }
    return NULL;
}

static void *reader_fn(void *arg) {
    (void)arg;
    while (!atomic_load_explicit(&g_stop, memory_order_relaxed)) {
        sctx_system_state_t out;
        sctx_read_state(&g_ctx, &out);
        if (out.consistency != consistency_of(&out))
            atomic_fetch_add_explicit(&g_torn, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_reads, 1, memory_order_relaxed);
    }
    return NULL;
}

static void test_torn_read(int k) {
    sctx_init(&g_ctx, 1);
    atomic_store(&g_stop, 0);
    atomic_store(&g_torn, 0);
    atomic_store(&g_reads, 0);

    pthread_t w, r[8];
    pthread_create(&w, NULL, writer_fn, NULL);
    for (int i = 0; i < k; i++) pthread_create(&r[i], NULL, reader_fn, NULL);

    struct timespec ts = { 0, 60 * 1000 * 1000 };  /* 60 ms of contention */
    nanosleep(&ts, NULL);
    atomic_store_explicit(&g_stop, 1, memory_order_relaxed);

    pthread_join(w, NULL);
    for (int i = 0; i < k; i++) pthread_join(r[i], NULL);

    char msg[80];
    snprintf(msg, sizeof msg, "T1 torn-read-safe K=%d (reads=%ld)", k, (long)atomic_load(&g_reads));
    CHECK(atomic_load(&g_torn) == 0, msg);
    CHECK(atomic_load(&g_reads) > 0, "T1 readers actually ran");
}

/* ================================================================
 * T2 — exact-key lookup returns the NEWEST decision per key; -1 on miss
 *      (incl. a key derived via decision-cache FNV-1a — key parity)
 * ================================================================ */
static void test_exact_key(void) {
    shared_context_t c;
    sctx_init(&c, 1);

    sctx_record_decision(&c, 0xAAAAu, 10, 0, 1);
    sctx_record_decision(&c, 0xBBBBu, 20, 0, 1);
    sctx_record_decision(&c, 0xAAAAu, 11, 1, 1);   /* newer for 0xAAAA */

    sctx_decision_t out;
    CHECK(sctx_lookup_key(&c, 0xAAAAu, &out) == 0 && out.action == 11, "T2 lookup returns NEWEST for key");
    CHECK(sctx_lookup_key(&c, 0xBBBBu, &out) == 0 && out.action == 20, "T2 lookup other key");
    CHECK(sctx_lookup_key(&c, 0xCCCCu, &out) == -1, "T2 miss returns -1");

    /* key parity: a key built from the decision-cache pipeline interoperates 1:1 */
    char norm[MAX_QUERY_LEN];
    CHECK(cache_normalize_query("  Hello   World  ", norm, sizeof norm), "T2 normalize ok");
    uint64_t kq = cache_hash(norm);
    sctx_record_decision(&c, kq, 42, 0, 1);
    CHECK(sctx_lookup_key(&c, kq, &out) == 0 && out.action == 42,
          "T2 key parity via cache_hash(cache_normalize_query)");
    CHECK(kq == cache_hash("hello world"), "T2 normalized key == cache_hash(\"hello world\")");
}

/* ================================================================
 * T3 — recency: newest->oldest, correct count, capped at max + respects n
 * ================================================================ */
static void test_recency(void) {
    shared_context_t c;
    sctx_init(&c, 1);
    for (int i = 0; i < 5; i++)
        sctx_record_decision(&c, (uint64_t)(1000 + i), (uint16_t)i, 0, 1);

    sctx_decision_t out[3];
    int n = sctx_recent(&c, 10, out, 3);   /* want 10, capped at max=3 */
    CHECK(n == 3, "T3 recent capped at max");
    CHECK(out[0].query_key == 1004 && out[1].query_key == 1003 && out[2].query_key == 1002,
          "T3 newest->oldest order");

    int n2 = sctx_recent(&c, 2, out, 3);   /* want 2 */
    CHECK(n2 == 2 && out[0].query_key == 1004 && out[1].query_key == 1003, "T3 respects n");
}

/* ================================================================
 * T4 — event ring: push > ring size; holds the latest-N in order (wrap correct)
 * ================================================================ */
static void test_event_ring(void) {
    shared_context_t c;
    sctx_init(&c, 1);

    const uint32_t N = SCTX_EVENT_RING;
    for (uint32_t i = 0; i < N + 5u; i++)
        sctx_push_event(&c, 100u + i, 9000u + i, (uint16_t)(i & 0xFFFFu));

    /* single-threaded after the pushes — plain white-box read of the ring */
    uint32_t h = c.ev_head;
    int ring_ok = 1;
    for (uint32_t i = 5u; i < N + 5u; i++)             /* i=0..4 were overwritten by the wrap */
        if (c.events[i % N].query_key != 9000u + i) ring_ok = 0;
    CHECK(h == N + 5u, "T4 ev_head monotonic past ring size");
    CHECK(ring_ok, "T4 event ring holds the latest-N in order (wrap correct)");
    CHECK(c.events[0].query_key == 9000u + N, "T4 slot 0 overwritten by the wrap (i=N)");
}

/* ================================================================
 * T5 — D4: an uncommitted (committed=0) decision is visible to lookup + recent
 * ================================================================ */
static void test_uncommitted_visible(void) {
    shared_context_t c;
    sctx_init(&c, 1);
    sctx_record_decision(&c, 0xDEADu, 7, 0, /*committed=*/0);

    sctx_decision_t out;
    CHECK(sctx_lookup_key(&c, 0xDEADu, &out) == 0 && out.committed == 0 && out.action == 7,
          "T5 uncommitted decision visible to lookup");
    sctx_decision_t r[1];
    CHECK(sctx_recent(&c, 1, r, 1) == 1 && r[0].query_key == 0xDEADu,
          "T5 uncommitted decision visible to recent (D4 — newest memories before flush)");
}

int main(void) {
    printf("=== Shared Context Pool Tests (Phase 5 G2/M0) ===\n");
    const int ks[] = { 1, 2, 4, 8 };
    for (int i = 0; i < 4; i++) test_torn_read(ks[i]);
    test_exact_key();
    test_recency();
    test_event_ring();
    test_uncommitted_visible();
    printf("\n== Results: %d PASS, %d FAIL ==\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
