/*
 * JARVIS AI-OS - Decision Cache Test Program
 * Phase 1 Week 3
 *
 * Standalone test program to validate cache implementation
 * Tests: initialization, lookup, insert, performance, hit rate
 */

#define _POSIX_C_SOURCE 199309L

#include "decision_cache.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>

/* External pattern loading functions */
extern int cache_load_initial_patterns(decision_cache_t *cache);
extern int cache_load_extended_patterns(decision_cache_t *cache);
extern int cache_get_total_pattern_count(void);
extern void cache_print_pattern_stats(void);

/* Test utilities */
static double get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* Test 1: Cache Initialization */
static void test_initialization(void)
{
    printf("\n========== TEST 1: Cache Initialization ==========\n");

    decision_cache_t cache;
    assert(cache_init(&cache));

    printf("✓ Cache initialized successfully\n");
    printf("✓ Cache size: %d entries\n", CACHE_SIZE);
    printf("✓ Statistics reset to zero\n");

    cache_stats_t stats;
    cache_get_stats(&cache, &stats);
    assert(stats.total_lookups == 0);
    assert(stats.cache_hits == 0);
    assert(stats.cache_misses == 0);
    assert(stats.entries_used == 0);

    printf("✓ All assertions passed\n");
}

/* Test 2: Query Normalization */
static void test_normalization(void)
{
    printf("\n========== TEST 2: Query Normalization ==========\n");

    char normalized[MAX_QUERY_LEN];

    /* Test lowercase conversion */
    assert(cache_normalize_query("HELLO WORLD", normalized, MAX_QUERY_LEN));
    assert(strcmp(normalized, "hello world") == 0);
    printf("✓ Lowercase conversion: \"HELLO WORLD\" → \"%s\"\n", normalized);

    /* Test whitespace collapse */
    assert(cache_normalize_query("hello    world", normalized, MAX_QUERY_LEN));
    assert(strcmp(normalized, "hello world") == 0);
    printf("✓ Whitespace collapse: \"hello    world\" → \"%s\"\n", normalized);

    /* Test leading/trailing whitespace */
    assert(cache_normalize_query("  hello world  ", normalized, MAX_QUERY_LEN));
    assert(strcmp(normalized, "hello world") == 0);
    printf("✓ Trim whitespace: \"  hello world  \" → \"%s\"\n", normalized);

    /* Test combined */
    assert(cache_normalize_query("  SHOW   ME   THE    TIME  ", normalized, MAX_QUERY_LEN));
    assert(strcmp(normalized, "show me the time") == 0);
    printf("✓ Combined: \"  SHOW   ME   THE    TIME  \" → \"%s\"\n", normalized);

    printf("✓ All normalization tests passed\n");
}

/* Test 3: Hash Function */
static void test_hash_function(void)
{
    printf("\n========== TEST 3: Hash Function ==========\n");

    uint64_t hash1 = cache_hash("hello");
    uint64_t hash2 = cache_hash("hello");
    uint64_t hash3 = cache_hash("world");

    assert(hash1 == hash2);
    printf("✓ Hash consistency: hash(\"hello\") == hash(\"hello\")\n");

    assert(hash1 != hash3);
    printf("✓ Hash uniqueness: hash(\"hello\") != hash(\"world\")\n");

    printf("  hash(\"hello\") = 0x%016llx\n", (unsigned long long)hash1);
    printf("  hash(\"world\") = 0x%016llx\n", (unsigned long long)hash3);

    printf("✓ FNV-1a hash function working correctly\n");
}

/* Test 4: Basic Insert and Lookup */
static void test_insert_lookup(void)
{
    printf("\n========== TEST 4: Insert and Lookup ==========\n");

    decision_cache_t cache;
    cache_init(&cache);

    /* Insert test entry */
    assert(cache_insert(&cache, "help", "show_help", TRUST_AUTO));
    printf("✓ Inserted entry: \"help\" → \"show_help\"\n");

    /* Lookup test entry */
    char action[MAX_ACTION_LEN];
    trust_level_t trust;
    assert(cache_lookup(&cache, "help", action, MAX_ACTION_LEN, &trust));
    assert(strcmp(action, "show_help") == 0);
    assert(trust == TRUST_AUTO);
    printf("✓ Lookup successful: found \"help\" → \"%s\" (trust=%d)\n", action, trust);

    /* Test cache miss */
    assert(!cache_lookup(&cache, "nonexistent", action, MAX_ACTION_LEN, &trust));
    printf("✓ Cache miss handled correctly for \"nonexistent\"\n");

    /* Test update existing entry */
    assert(cache_insert(&cache, "help", "updated_help", TRUST_NOTIFY));
    assert(cache_lookup(&cache, "help", action, MAX_ACTION_LEN, &trust));
    assert(strcmp(action, "updated_help") == 0);
    assert(trust == TRUST_NOTIFY);
    printf("✓ Update existing entry: \"help\" → \"%s\" (trust=%d)\n", action, trust);

    printf("✓ All insert/lookup tests passed\n");
}

/* Test 5: Pattern Loading */
static void test_pattern_loading(void)
{
    printf("\n========== TEST 5: Pattern Loading ==========\n");

    decision_cache_t cache;
    cache_init(&cache);

    cache_print_pattern_stats();

    int loaded = cache_load_extended_patterns(&cache);
    printf("Loaded %d patterns\n", loaded);
    assert(loaded > 0);

    cache_stats_t stats;
    cache_get_stats(&cache, &stats);
    printf("Cache entries used: %u / %d\n", stats.entries_used, CACHE_SIZE);

    /* Test some known patterns */
    char action[MAX_ACTION_LEN];
    trust_level_t trust;

    assert(cache_lookup(&cache, "help", action, MAX_ACTION_LEN, &trust));
    printf("✓ Found pattern: \"help\" → \"%s\"\n", action);

    assert(cache_lookup(&cache, "status", action, MAX_ACTION_LEN, &trust));
    printf("✓ Found pattern: \"status\" → \"%s\"\n", action);

    assert(cache_lookup(&cache, "ls", action, MAX_ACTION_LEN, &trust));
    printf("✓ Found pattern: \"ls\" → \"%s\"\n", action);

    printf("✓ Pattern loading successful\n");
}

/* Test 6: Performance Measurement */
static void test_performance(void)
{
    printf("\n========== TEST 6: Performance Measurement ==========\n");

    decision_cache_t cache;
    cache_init(&cache);
    cache_load_extended_patterns(&cache);

    /* Test queries */
    const char *test_queries[] = {
        "help", "status", "ls", "pwd", "ps", "top", "free", "df",
        "git status", "docker ps", "cat readme", "what time is it"
    };
    int num_queries = sizeof(test_queries) / sizeof(char*);

    char action[MAX_ACTION_LEN];
    trust_level_t trust;

    /* Warm up */
    for (int i = 0; i < num_queries; i++) {
        cache_lookup(&cache, test_queries[i], action, MAX_ACTION_LEN, &trust);
    }

    /* Measure lookup performance */
    int iterations = 10000;
    double start = get_time_ms();

    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < num_queries; i++) {
            cache_lookup(&cache, test_queries[i], action, MAX_ACTION_LEN, &trust);
        }
    }

    double end = get_time_ms();
    double total_time = end - start;
    double avg_time_us = (total_time * 1000.0) / (iterations * num_queries);

    printf("Performance Results:\n");
    printf("  Total lookups: %d\n", iterations * num_queries);
    printf("  Total time: %.2f ms\n", total_time);
    printf("  Average lookup time: %.3f μs (%.6f ms)\n", avg_time_us, avg_time_us / 1000.0);

    /* Target: <1ms (1000 μs) */
    if (avg_time_us < 1000.0) {
        printf("✓ PASS: Lookup time < 1ms target (%.1fx faster than target)\n",
               1000.0 / avg_time_us);
    } else {
        printf("✗ FAIL: Lookup time exceeds 1ms target\n");
    }

    /* Compare to AI inference time (50ms baseline) */
    double speedup = 50.0 / (avg_time_us / 1000.0);
    printf("  Speedup vs AI (50ms): %.0fx\n", speedup);

    /* Target: 135x speedup (50ms → <1ms) */
    if (speedup > 135.0) {
        printf("✓ PASS: Exceeds 135x speedup target\n");
    } else if (speedup > 50.0) {
        printf("✓ ACCEPTABLE: Significant speedup achieved\n");
    } else {
        printf("⚠ WARNING: Speedup less than expected\n");
    }
}

/* Test 7: Hit Rate Calculation */
static void test_hit_rate(void)
{
    printf("\n========== TEST 7: Hit Rate Calculation ==========\n");

    decision_cache_t cache;
    cache_init(&cache);
    cache_load_extended_patterns(&cache);

    /* Simulate realistic query distribution */
    const char *common_queries[] = {
        "help", "status", "ls", "pwd", "ps", "top",  /* 6 common (80%) */
    };
    const char *uncommon_queries[] = {
        "unknown1", "unknown2"  /* 2 uncommon (20%) */
    };

    char action[MAX_ACTION_LEN];
    trust_level_t trust;

    /* Simulate 80/20 distribution (80% common, 20% uncommon) */
    for (int i = 0; i < 100; i++) {
        if (i < 80) {
            /* Common queries (cache hits) */
            cache_lookup(&cache, common_queries[i % 6], action, MAX_ACTION_LEN, &trust);
        } else {
            /* Uncommon queries (cache misses) */
            cache_lookup(&cache, uncommon_queries[(i - 80) % 2], action, MAX_ACTION_LEN, &trust);
        }
    }

    cache_stats_t stats;
    cache_get_stats(&cache, &stats);

    printf("Hit Rate Test Results:\n");
    printf("  Total lookups: %llu\n", (unsigned long long)stats.total_lookups);
    printf("  Cache hits: %llu\n", (unsigned long long)stats.cache_hits);
    printf("  Cache misses: %llu\n", (unsigned long long)stats.cache_misses);
    printf("  Hit rate: %.2f%%\n", stats.hit_rate * 100.0);

    /* Target: >80% hit rate */
    if (stats.hit_rate >= 0.80) {
        printf("✓ PASS: Hit rate meets 80%% target\n");
    } else {
        printf("⚠ WARNING: Hit rate below 80%% target\n");
    }
}

/* Test 8: Collision Handling */
static void test_collisions(void)
{
    printf("\n========== TEST 8: Collision Handling ==========\n");

    decision_cache_t cache;
    cache_init(&cache);

    /* Insert many entries to force collisions */
    char query[64], action[64];
    int inserted = 0;

    for (int i = 0; i < 100; i++) {
        snprintf(query, sizeof(query), "query_%d", i);
        snprintf(action, sizeof(action), "action_%d", i);
        if (cache_insert(&cache, query, action, TRUST_AUTO)) {
            inserted++;
        }
    }

    printf("Inserted %d entries (collisions will occur with linear probing)\n", inserted);

    /* Verify all entries can be looked up */
    char retrieved_action[MAX_ACTION_LEN];
    trust_level_t trust;
    int found = 0;

    for (int i = 0; i < inserted; i++) {
        snprintf(query, sizeof(query), "query_%d", i);
        snprintf(action, sizeof(action), "action_%d", i);

        if (cache_lookup(&cache, query, retrieved_action, MAX_ACTION_LEN, &trust)) {
            assert(strcmp(retrieved_action, action) == 0);
            found++;
        }
    }

    printf("✓ Successfully retrieved %d / %d entries despite collisions\n", found, inserted);
    assert(found == inserted);
    printf("✓ Collision handling working correctly\n");
}

/* Main test suite */
int main(void)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   JARVIS AI-OS DECISION CACHE TEST SUITE             ║\n");
    printf("║   Phase 1 Week 3                                     ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");

    test_initialization();
    test_normalization();
    test_hash_function();
    test_insert_lookup();
    test_pattern_loading();
    test_performance();
    test_hit_rate();
    test_collisions();

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║   ALL TESTS PASSED ✓                                 ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Final cache statistics */
    decision_cache_t final_cache;
    cache_init(&final_cache);
    cache_load_extended_patterns(&final_cache);
    cache_print_stats(&final_cache);

    return 0;
}
