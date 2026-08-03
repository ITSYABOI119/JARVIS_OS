/*
 * A7 — the decision-cache suite for the SHIPPING copies.
 *
 * WHY THIS EXISTS. `phase1/src/cache/test_cache.c` compiles and attests PHASE1's
 * decision_cache.c + cache_patterns.c. The copies that LINK INTO THE DEPLOYED IMAGE are
 * phase3's (build_jarvis_x86.sh:139-140 syncs them; the CMake source list carries
 * src/ai/decision_cache.c). So CI green over there proved *a* file, not *the* file that
 * ships. This suite is that suite's assertions, compiled against the phase3 copies ONLY.
 *
 * WHAT THE SHIPPING COPY ACTUALLY DIVERGES BY — measured, not assumed:
 *   decision_cache.c   +26/-2 lines, ONE hunk, all of it SEC-024 LRU eviction (708aa15).
 *                      phase1: cache_insert() on a FULL cache returns false.
 *                      phase3: evicts the LRU entry and returns true.
 *   decision_cache.h   identical content (phase1 LF vs phase3 CRLF only)
 *   cache_patterns.c   identical content (line endings only) -- the audit's "~880 diff
 *                      lines" is the CRLF artifact, not divergence
 *   cache_patterns.h   phase3-only (phase1 declares the four loaders via extern)
 *
 * The divergent full-cache path is NOT re-tested here: test_decision_cache_lru.c already
 * owns it. Nothing below fills the cache past CACHE_SIZE, so cache_insert()'s return value
 * is identical in both copies for every case in this file.
 *
 * Build: gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *        phase3/src/ai/test_decision_cache.c phase3/src/ai/decision_cache.c \
 *        phase3/src/ai/cache_patterns.c -o /tmp/tdc3 && /tmp/tdc3
 */

#include "decision_cache.h"
#include "cache_patterns.h"     /* phase3-only header; phase1's suite used bare externs */
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do {                                  \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); }       \
    else      { g_fail++; printf("  FAIL: %s\n", msg); }       \
} while (0)

/* ~213 KB (512 * ~416 B). phase1's suite put this on the STACK in six functions; at a
 * 1 MB default that is close to the edge, so every cache here is static. */
static decision_cache_t cache;

/* ---- T1: initialization ------------------------------------------------------------ */
static void t_initialization(void)
{
    printf("\n-- T1 initialization --\n");
    CHECK(cache_init(&cache), "cache_init returns true");

    cache_stats_t st;
    cache_get_stats(&cache, &st);
    CHECK(st.total_lookups == 0, "fresh: total_lookups == 0");
    CHECK(st.cache_hits    == 0, "fresh: cache_hits == 0");
    CHECK(st.cache_misses  == 0, "fresh: cache_misses == 0");
    CHECK(st.entries_used  == 0, "fresh: entries_used == 0");

    /* Layout constants read from phase3/src/ai/decision_cache.h:18-20. */
    CHECK(CACHE_SIZE     == 512, "CACHE_SIZE == 512");
    CHECK(MAX_QUERY_LEN  == 128, "MAX_QUERY_LEN == 128");
    CHECK(MAX_ACTION_LEN == 256, "MAX_ACTION_LEN == 256");
}

/* ---- T2: query normalization ------------------------------------------------------- */
static void t_normalization(void)
{
    printf("\n-- T2 normalization --\n");
    char n[MAX_QUERY_LEN];

    CHECK(cache_normalize_query("HELLO WORLD", n, MAX_QUERY_LEN) &&
          strcmp(n, "hello world") == 0, "lowercase: HELLO WORLD -> hello world");

    CHECK(cache_normalize_query("hello    world", n, MAX_QUERY_LEN) &&
          strcmp(n, "hello world") == 0, "collapse runs of spaces");

    CHECK(cache_normalize_query("  hello world  ", n, MAX_QUERY_LEN) &&
          strcmp(n, "hello world") == 0, "trim leading/trailing space");

    CHECK(cache_normalize_query("  SHOW   ME   THE    TIME  ", n, MAX_QUERY_LEN) &&
          strcmp(n, "show me the time") == 0, "combined lowercase+collapse+trim");
}

/* ---- T3: hash ---------------------------------------------------------------------- */
static void t_hash(void)
{
    printf("\n-- T3 hash --\n");
    CHECK(cache_hash("hello") == cache_hash("hello"), "hash is deterministic");
    CHECK(cache_hash("hello") != cache_hash("world"), "distinct inputs -> distinct hashes");

    /* PINNED FNV-1a-64 values, computed INDEPENDENTLY in Python from the documented
     * constants (offset basis 0xcbf29ce484222325, prime 0x100000001b3), not read back
     * from this implementation. This seed is not local: query_key across the episodic
     * store and cache-growth is cache_hash(cache_normalize_query(q)), so a silent change
     * here would break key parity repo-wide while every module still "worked". */
    CHECK(cache_hash("help")  == 0x0a9918cc5fa26abaULL, "FNV-1a pin: hash(\"help\")");
    CHECK(cache_hash("world") == 0x4f59ff5e730c8af3ULL, "FNV-1a pin: hash(\"world\")");
    CHECK(cache_hash("hello") == 0xa430d84680aabd0bULL, "FNV-1a pin: hash(\"hello\")");
}

/* ---- T4: insert / lookup / miss / update ------------------------------------------- */
static void t_insert_lookup(void)
{
    printf("\n-- T4 insert/lookup --\n");
    cache_init(&cache);

    char action[MAX_ACTION_LEN];
    trust_level_t trust;

    CHECK(cache_insert(&cache, "help", "show_help", TRUST_AUTO), "insert help");
    CHECK(cache_lookup(&cache, "help", action, MAX_ACTION_LEN, &trust) &&
          strcmp(action, "show_help") == 0 && trust == TRUST_AUTO,
          "lookup help -> show_help / TRUST_AUTO");

    CHECK(!cache_lookup(&cache, "nonexistent", action, MAX_ACTION_LEN, &trust),
          "lookup of an absent key misses");

    CHECK(cache_insert(&cache, "help", "updated_help", TRUST_NOTIFY), "re-insert help");
    CHECK(cache_lookup(&cache, "help", action, MAX_ACTION_LEN, &trust) &&
          strcmp(action, "updated_help") == 0 && trust == TRUST_NOTIFY,
          "re-insert UPDATES in place (action and trust both replaced)");

    cache_stats_t st;
    cache_get_stats(&cache, &st);
    CHECK(st.entries_used == 1, "update did not add a second entry (entries_used == 1)");
}

/* ---- T5: pattern loading, with the REAL shipping counts ---------------------------- */
static void t_pattern_loading(void)
{
    printf("\n-- T5 pattern loading --\n");
    cache_init(&cache);

    /* Counts read from phase3/src/ai/cache_patterns.c: initial_patterns[] = 50,
     * extended_patterns[] = 208, TOTAL_PATTERNS = 258. */
    CHECK(cache_get_total_pattern_count() == 258, "cache_get_total_pattern_count() == 258");

    CHECK(cache_load_initial_patterns(&cache) == 50, "initial loader returns 50");

    cache_init(&cache);
    int loaded = cache_load_extended_patterns(&cache);
    CHECK(loaded == 258, "extended loader returns 258 (it loads initial first, then 208)");

    /* THE TRAP THIS PINS: 258 patterns are DEFINED but only 252 keys are UNIQUE, so the
     * cache holds 252. Asserting entries_used == 258 looks obvious and is wrong. This
     * exact number is also the tightest available guard on the duplicate set: adding a
     * 7th duplicate makes it 251, removing one makes it 253. */
    cache_stats_t st;
    cache_get_stats(&cache, &st);
    CHECK(st.entries_used == 252,
          "252 UNIQUE keys land in the cache (258 defined - 6 duplicate keys)");

    char action[MAX_ACTION_LEN];
    trust_level_t trust;
    CHECK(cache_lookup(&cache, "help",   action, MAX_ACTION_LEN, &trust), "pattern present: help");
    CHECK(cache_lookup(&cache, "status", action, MAX_ACTION_LEN, &trust), "pattern present: status");
    CHECK(cache_lookup(&cache, "ls",     action, MAX_ACTION_LEN, &trust), "pattern present: ls");
}

/* ---- T6: the phase1 perf/hit-rate queries, as a FUNCTIONAL check -------------------- */
static void t_known_queries_resolve(void)
{
    printf("\n-- T6 known queries resolve --\n");
    /* phase1 T6/T7 measured lookup latency and hit rate and asserted NOTHING (printf
     * only). Timing thresholds are flaky on shared CI runners, so the timing is dropped
     * and the functional core kept: every query those tests exercised must resolve. */
    cache_init(&cache);
    cache_load_extended_patterns(&cache);

    static const char *queries[] = {
        "help", "status", "ls", "pwd", "ps", "top", "free", "df",
        "git status", "docker ps", "cat readme", "what time is it"
    };
    char action[MAX_ACTION_LEN];
    trust_level_t trust;
    int hits = 0;
    for (size_t i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
        if (cache_lookup(&cache, queries[i], action, MAX_ACTION_LEN, &trust)) hits++;
        else printf("    (miss: %s)\n", queries[i]);
    }
    CHECK(hits == 12, "all 12 phase1 benchmark queries resolve against the shipping table");

    /* A miss must be counted as a miss. */
    cache_stats_t before, after;
    cache_get_stats(&cache, &before);
    (void)cache_lookup(&cache, "zzz no such pattern zzz", action, MAX_ACTION_LEN, &trust);
    cache_get_stats(&cache, &after);
    CHECK(after.cache_misses == before.cache_misses + 1, "a miss increments cache_misses");
    CHECK(after.total_lookups == before.total_lookups + 1, "a miss increments total_lookups");
}

/* ---- T7: many distinct keys -------------------------------------------------------- */
static void t_many_keys(void)
{
    printf("\n-- T7 many distinct keys --\n");
    cache_init(&cache);

    char q[32], a[32];
    int inserted = 0;
    for (int i = 0; i < 100; i++) {
        snprintf(q, sizeof(q), "query_%d", i);
        snprintf(a, sizeof(a), "action_%d", i);
        if (cache_insert(&cache, q, a, TRUST_AUTO)) inserted++;
    }
    CHECK(inserted == 100, "100 distinct keys insert");

    char action[MAX_ACTION_LEN];
    int found = 0;
    for (int i = 0; i < 100; i++) {
        snprintf(q, sizeof(q), "query_%d", i);
        snprintf(a, sizeof(a), "action_%d", i);
        if (cache_lookup(&cache, q, action, MAX_ACTION_LEN, NULL) &&
            strcmp(action, a) == 0) found++;
    }
    CHECK(found == 100, "all 100 retrieve their own action (open addressing probes correctly)");

    cache_stats_t st;
    cache_get_stats(&cache, &st);
    CHECK(st.entries_used == 100, "entries_used == 100");
}

/* ---- T8: pattern-table well-formedness (the shipping DATA file) --------------------- */
static void t_pattern_table_wellformed(void)
{
    printf("\n-- T8 pattern table well-formedness --\n");
    /* phase3/src/ai/cache_patterns.c ships in the image but was, until this suite, never
     * COMPILED by any CI step (it appears in ci.yml only as phase1 paths and in the A1
     * copy-drift gate). A diverged-and-uncompiled data table is exactly what a
     * well-formedness sweep is for.
     *
     * The arrays are file-static, so the table is probed THROUGH the cache rather than
     * by enumerating it -- which also keeps this test from needing any change to the
     * shipped source. */
    cache_init(&cache);
    int loaded = cache_load_extended_patterns(&cache);

    /* Bounded strings, behaviourally: cache_insert() rejects a query at or over
     * MAX_QUERY_LEN (decision_cache.c bounds check), so a full 258 return proves every
     * key and action in the table fits. Measured maxima are 26 and 35. */
    CHECK(loaded == 258, "no pattern was rejected for length -> all keys/actions bounded");

    cache_stats_t st;
    cache_get_stats(&cache, &st);
    CHECK(st.entries_used + 6 == (unsigned)loaded,
          "the defined-vs-unique gap is exactly 6 (the documented duplicate-key set)");

    /* DUPLICATE-KEY POLICY, pinned rather than assumed: the table defines 6 keys twice,
     * and cache_insert() takes the update path for the second, so LAST WRITER WINS.
     * That is benign TODAY for one specific reason worth pinning: both members of every
     * pair carry the SAME trust level, so which action wins cannot change the safety
     * posture. "delete file" is TRUST_REQUEST in both; "format disk" is TRUST_REQUIRE in
     * both. If a future edit adds a duplicate whose pair DISAGREES on trust, the resolved
     * trust becomes order-dependent -- these assertions are what would catch it. */
    struct { const char *key; const char *winning_action; trust_level_t trust; } dups[] = {
        { "netstat",                    "show_network",            TRUST_AUTO    },
        { "format disk",                "format_disk_interactive", TRUST_REQUIRE },
        { "shutdown now",               "system_shutdown",         TRUST_REQUIRE },
        { "check disk space",           "show_disk_usage",         TRUST_AUTO    },
        { "what processes are running", "show_processes",          TRUST_AUTO    },
        { "delete file",                "file_delete",             TRUST_REQUEST },
    };
    char action[MAX_ACTION_LEN];
    trust_level_t trust;
    int ok_action = 0, ok_trust = 0;
    for (size_t i = 0; i < sizeof(dups) / sizeof(dups[0]); i++) {
        if (cache_lookup(&cache, dups[i].key, action, MAX_ACTION_LEN, &trust)) {
            if (strcmp(action, dups[i].winning_action) == 0) ok_action++;
            if (trust == dups[i].trust) ok_trust++;
            else printf("    (trust drift on \"%s\": got %d, want %d)\n",
                        dups[i].key, (int)trust, (int)dups[i].trust);
        } else {
            printf("    (duplicate key vanished: %s)\n", dups[i].key);
        }
    }
    CHECK(ok_action == 6, "all 6 duplicate keys resolve LAST-WRITER-WINS");
    CHECK(ok_trust  == 6, "all 6 duplicate pairs agree on trust level (safety is order-independent)");

    /* The destructive keys keep their gate regardless of duplicate resolution. */
    CHECK(cache_lookup(&cache, "format disk", action, MAX_ACTION_LEN, &trust) &&
          trust == TRUST_REQUIRE, "\"format disk\" is TRUST_REQUIRE");
    CHECK(cache_lookup(&cache, "shutdown now", action, MAX_ACTION_LEN, &trust) &&
          trust == TRUST_REQUIRE, "\"shutdown now\" is TRUST_REQUIRE");
    CHECK(cache_lookup(&cache, "delete file", action, MAX_ACTION_LEN, &trust) &&
          trust == TRUST_REQUEST, "\"delete file\" is TRUST_REQUEST");

    /* The whole table fits with room to spare -- 252 of 512, so loading patterns alone
     * never reaches the SEC-024 eviction path (which test_decision_cache_lru.c owns). */
    CHECK(st.entries_used < CACHE_SIZE,
          "the pattern table fits inside CACHE_SIZE without evicting");
}

int main(void)
{
    printf("== test_decision_cache (the SHIPPING phase3 copies) ==\n");

    t_initialization();
    t_normalization();
    t_hash();
    t_insert_lookup();
    t_pattern_loading();
    t_known_queries_resolve();
    t_many_keys();
    t_pattern_table_wellformed();

    printf("\n== Results: %d PASS, %d FAIL ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
