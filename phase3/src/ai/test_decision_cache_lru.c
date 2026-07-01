/*
 * Phase 5 #6/M0 — the dormant SEC-024 LRU eviction (decision_cache.c:214-238) goes LIVE + tested.
 *
 * Cache growth is the first path that ever fills the decision cache, so it is the first to
 * exercise the SEC-024 LRU eviction. This test fills the cache to CACHE_SIZE, controls the LRU
 * via access, inserts one more distinct key, and asserts the oldest-accessed entry is evicted
 * (entries_used stays 512, the new key is present, recently-accessed entries survive).
 *
 * Build: gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *        phase3/src/ai/test_decision_cache_lru.c phase3/src/ai/decision_cache.c -o /tmp/tlru && /tmp/tlru
 */

#include "decision_cache.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do {                                  \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); }       \
    else      { g_fail++; printf("  FAIL: %s\n", msg); }       \
} while (0)

int main(void)
{
    printf("== test_decision_cache_lru ==\n");

    static decision_cache_t cache;   /* ~213 KB — keep off the stack */
    cache_init(&cache);

    /* Fill the cache to CACHE_SIZE with distinct keys. Insertion stamps last_access_time
     * 0..511 in order, so q000 starts as the oldest (LRU). */
    char q[32], a[32];
    for (int i = 0; i < CACHE_SIZE; i++) {
        snprintf(q, sizeof(q), "q%03d", i);
        snprintf(a, sizeof(a), "a%03d", i);
        cache_insert(&cache, q, a, TRUST_AUTO);
    }

    cache_stats_t st;
    cache_get_stats(&cache, &st);
    CHECK(st.entries_used == CACHE_SIZE, "fill: entries_used == 512 (cache full, no empty slots)");

    /* Control the LRU: access q000 (was oldest) so it becomes the newest; now q001 is the
     * oldest -> the designated eviction victim. Access q005 too as a known survivor. */
    char buf[MAX_ACTION_LEN];
    CHECK(cache_lookup(&cache, "q000", buf, sizeof(buf), NULL), "setup: q000 present pre-eviction");
    CHECK(cache_lookup(&cache, "q005", buf, sizeof(buf), NULL), "setup: q005 present pre-eviction");

    /* Insert a NEW distinct key -> cache full (no empty slot) -> SEC-024 LRU eviction fires. */
    CHECK(cache_insert(&cache, "qNEW", "aNEW", TRUST_AUTO), "insert: 513th key succeeds via eviction");

    cache_get_stats(&cache, &st);
    CHECK(st.entries_used == CACHE_SIZE, "eviction: entries_used stays 512 (replaced, not added)");

    /* The LRU victim (q001, the oldest last_access_time) was evicted. */
    CHECK(!cache_lookup(&cache, "q001", buf, sizeof(buf), NULL), "eviction: LRU victim q001 evicted (miss)");

    /* The new key is present with its action. */
    int hit = cache_lookup(&cache, "qNEW", buf, sizeof(buf), NULL);
    CHECK(hit && strcmp(buf, "aNEW") == 0, "eviction: new key qNEW present (hit, action == aNEW)");

    /* Recently-accessed entries survive. */
    CHECK(cache_lookup(&cache, "q000", buf, sizeof(buf), NULL), "eviction: bumped q000 survives");
    CHECK(cache_lookup(&cache, "q005", buf, sizeof(buf), NULL), "eviction: bumped q005 survives");

    /* An untouched non-victim entry also survives (sanity). */
    CHECK(cache_lookup(&cache, "q511", buf, sizeof(buf), NULL), "eviction: untouched q511 survives");

    printf("== %d PASS, %d FAIL ==\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
