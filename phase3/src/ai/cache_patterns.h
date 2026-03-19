/*
 * JARVIS AI-OS - Cache Patterns Header
 * Phase 3 x86 port
 *
 * Declarations for cache pattern loading functions.
 * These were originally declared via extern in test files;
 * this header provides proper declarations for the x86 build.
 */

#ifndef CACHE_PATTERNS_H
#define CACHE_PATTERNS_H

#include "decision_cache.h"

/**
 * Load initial patterns into the cache (50 patterns)
 * @param cache Pointer to decision cache
 * @return Number of patterns successfully loaded
 */
int cache_load_initial_patterns(decision_cache_t *cache);

/**
 * Load extended patterns into the cache (initial + extended = ~258 patterns)
 * @param cache Pointer to decision cache
 * @return Total number of patterns successfully loaded
 */
int cache_load_extended_patterns(decision_cache_t *cache);

/**
 * Get total number of available patterns
 * @return Total pattern count
 */
int cache_get_total_pattern_count(void);

/**
 * Print pattern statistics
 */
void cache_print_pattern_stats(void);

#endif /* CACHE_PATTERNS_H */
