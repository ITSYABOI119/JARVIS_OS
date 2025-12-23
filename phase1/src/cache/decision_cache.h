/*
 * JARVIS AI-OS - Decision Cache Header
 * Phase 1 Week 3
 *
 * Fast lookup cache for common AI decisions
 * Target: 135x speedup (50ms AI → <1ms cache lookup)
 * Goal: >80% hit rate on common operations
 */

#ifndef DECISION_CACHE_H
#define DECISION_CACHE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Configuration */
#define CACHE_SIZE 512          /* Increased from 256 to accommodate Week 24 storage patterns (258 total) */
#define MAX_QUERY_LEN 128       /* Max normalized query string */
#define MAX_ACTION_LEN 256      /* Max action/bytecode length */
#define CACHE_VERSION 1         /* For future compatibility */

/* Cache entry states */
typedef enum {
    ENTRY_EMPTY = 0,
    ENTRY_VALID = 1,
    ENTRY_TOMBSTONE = 2    /* Deleted entry (for open addressing) */
} cache_entry_state_t;

/* Trust levels (from SHIELD framework) */
typedef enum {
    TRUST_AUTO = 0,        /* Automatic execution */
    TRUST_NOTIFY = 1,      /* Execute + notify user */
    TRUST_REQUEST = 2,     /* Require permission */
    TRUST_REQUIRE = 3      /* Explicit approval needed */
} trust_level_t;

/* Cache entry structure */
typedef struct {
    cache_entry_state_t state;
    uint64_t hash;                          /* FNV-1a hash of normalized query */
    char query[MAX_QUERY_LEN];              /* Normalized query string */
    char action[MAX_ACTION_LEN];            /* Action/bytecode to execute */
    trust_level_t trust_level;              /* Safety level */
    uint32_t hit_count;                     /* Usage statistics */
    uint64_t last_access_time;              /* For LRU eviction */
} cache_entry_t;

/* Cache statistics */
typedef struct {
    uint64_t total_lookups;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint32_t entries_used;
    double hit_rate;                        /* Calculated: hits / total */
} cache_stats_t;

/* Main cache structure */
typedef struct {
    cache_entry_t entries[CACHE_SIZE];
    cache_stats_t stats;
    uint64_t time_counter;                  /* Monotonic counter for LRU */
} decision_cache_t;

/* Public API */

/**
 * Initialize the decision cache
 * @param cache Pointer to cache structure
 * @return true on success, false on failure
 */
bool cache_init(decision_cache_t *cache);

/**
 * Normalize a query string (lowercase, trim, etc.)
 * @param raw_query Original query
 * @param normalized Output buffer for normalized query
 * @param max_len Maximum length of output buffer
 * @return true on success, false if query too long
 */
bool cache_normalize_query(const char *raw_query, char *normalized, size_t max_len);

/**
 * Compute FNV-1a hash of a string
 * @param str String to hash
 * @return 64-bit hash value
 */
uint64_t cache_hash(const char *str);

/**
 * Look up a query in the cache
 * @param cache Pointer to cache structure
 * @param normalized_query Normalized query string
 * @param action Output buffer for action (if found)
 * @param max_action_len Maximum length of action buffer
 * @param trust_level Output for trust level (if found)
 * @return true if cache hit, false if cache miss
 */
bool cache_lookup(decision_cache_t *cache,
                  const char *normalized_query,
                  char *action,
                  size_t max_action_len,
                  trust_level_t *trust_level);

/**
 * Insert a new entry into the cache
 * @param cache Pointer to cache structure
 * @param normalized_query Normalized query string
 * @param action Action/bytecode to cache
 * @param trust_level Safety trust level
 * @return true on success, false if cache full or error
 */
bool cache_insert(decision_cache_t *cache,
                  const char *normalized_query,
                  const char *action,
                  trust_level_t trust_level);

/**
 * Remove an entry from the cache
 * @param cache Pointer to cache structure
 * @param normalized_query Query to remove
 * @return true if found and removed, false if not found
 */
bool cache_remove(decision_cache_t *cache, const char *normalized_query);

/**
 * Clear all entries from the cache
 * @param cache Pointer to cache structure
 */
void cache_clear(decision_cache_t *cache);

/**
 * Get cache statistics
 * @param cache Pointer to cache structure
 * @param stats Output buffer for statistics
 */
void cache_get_stats(const decision_cache_t *cache, cache_stats_t *stats);

/**
 * Print cache statistics (for debugging)
 * @param cache Pointer to cache structure
 */
void cache_print_stats(const decision_cache_t *cache);

/**
 * Dump entire cache contents (for debugging)
 * @param cache Pointer to cache structure
 */
void cache_dump(const decision_cache_t *cache);

#endif /* DECISION_CACHE_H */
