/*
 * JARVIS AI-OS - Decision Cache Implementation
 * Phase 1 Week 3
 *
 * Hash table with open addressing (linear probing)
 * FNV-1a hash function for fast, good distribution
 */

#include "decision_cache.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* FNV-1a constants */
#define FNV_OFFSET_BASIS 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

/* Public API Implementation */

bool cache_init(decision_cache_t *cache)
{
    if (!cache) {
        return false;
    }

    /* Clear all entries */
    memset(cache->entries, 0, sizeof(cache->entries));
    for (size_t i = 0; i < CACHE_SIZE; i++) {
        cache->entries[i].state = ENTRY_EMPTY;
    }

    /* Reset statistics */
    memset(&cache->stats, 0, sizeof(cache->stats));
    cache->time_counter = 0;

    return true;
}

bool cache_normalize_query(const char *raw_query, char *normalized, size_t max_len)
{
    if (!raw_query || !normalized || max_len == 0) {
        return false;
    }

    size_t i = 0;
    size_t j = 0;

    /* Skip leading whitespace */
    while (raw_query[i] && isspace((unsigned char)raw_query[i])) {
        i++;
    }

    /* Convert to lowercase, collapse whitespace */
    bool prev_was_space = false;
    while (raw_query[i] && j < max_len - 1) {
        if (isspace((unsigned char)raw_query[i])) {
            if (!prev_was_space && j > 0) {
                normalized[j++] = ' ';
                prev_was_space = true;
            }
        } else {
            normalized[j++] = tolower((unsigned char)raw_query[i]);
            prev_was_space = false;
        }
        i++;
    }

    /* Remove trailing space */
    if (j > 0 && normalized[j-1] == ' ') {
        j--;
    }

    normalized[j] = '\0';

    return (raw_query[i] == '\0');  /* True if we processed entire string */
}

uint64_t cache_hash(const char *str)
{
    uint64_t hash = FNV_OFFSET_BASIS;

    while (*str) {
        hash ^= (uint64_t)(unsigned char)(*str);
        hash *= FNV_PRIME;
        str++;
    }

    return hash;
}

bool cache_lookup(decision_cache_t *cache,
                  const char *normalized_query,
                  char *action,
                  size_t max_action_len,
                  trust_level_t *trust_level)
{
    if (!cache || !normalized_query || !action) {
        return false;
    }

    cache->stats.total_lookups++;

    uint64_t hash = cache_hash(normalized_query);
    size_t index = hash % CACHE_SIZE;
    size_t original_index = index;

    /* Linear probing */
    do {
        cache_entry_t *entry = &cache->entries[index];

        if (entry->state == ENTRY_EMPTY) {
            /* Not found - reached empty slot */
            cache->stats.cache_misses++;
            return false;
        }

        if (entry->state == ENTRY_VALID &&
            entry->hash == hash &&
            strcmp(entry->query, normalized_query) == 0) {
            /* Cache hit! */
            cache->stats.cache_hits++;
            cache->stats.hit_rate = (double)cache->stats.cache_hits / cache->stats.total_lookups;

            /* Copy action to output */
            strncpy(action, entry->action, max_action_len - 1);
            action[max_action_len - 1] = '\0';

            if (trust_level) {
                *trust_level = entry->trust_level;
            }

            /* Update LRU */
            entry->hit_count++;
            entry->last_access_time = cache->time_counter++;

            return true;
        }

        /* Continue probing */
        index = (index + 1) % CACHE_SIZE;
    } while (index != original_index);

    /* Checked entire cache, not found */
    cache->stats.cache_misses++;
    cache->stats.hit_rate = (double)cache->stats.cache_hits / cache->stats.total_lookups;
    return false;
}

bool cache_insert(decision_cache_t *cache,
                  const char *normalized_query,
                  const char *action,
                  trust_level_t trust_level)
{
    if (!cache || !normalized_query || !action) {
        return false;
    }

    /* Check lengths */
    if (strlen(normalized_query) >= MAX_QUERY_LEN ||
        strlen(action) >= MAX_ACTION_LEN) {
        return false;
    }

    uint64_t hash = cache_hash(normalized_query);
    size_t index = hash % CACHE_SIZE;
    size_t original_index = index;
    size_t first_tombstone = CACHE_SIZE;  /* Invalid index */

    /* Linear probing to find insertion point */
    do {
        cache_entry_t *entry = &cache->entries[index];

        /* Found empty slot or tombstone */
        if (entry->state == ENTRY_EMPTY) {
            /* Use tombstone if we found one, otherwise use this slot */
            size_t insert_index = (first_tombstone < CACHE_SIZE) ? first_tombstone : index;
            cache_entry_t *insert_entry = &cache->entries[insert_index];

            /* Insert new entry */
            insert_entry->state = ENTRY_VALID;
            insert_entry->hash = hash;
            strncpy(insert_entry->query, normalized_query, MAX_QUERY_LEN - 1);
            insert_entry->query[MAX_QUERY_LEN - 1] = '\0';
            strncpy(insert_entry->action, action, MAX_ACTION_LEN - 1);
            insert_entry->action[MAX_ACTION_LEN - 1] = '\0';
            insert_entry->trust_level = trust_level;
            insert_entry->hit_count = 0;
            insert_entry->last_access_time = cache->time_counter++;

            cache->stats.entries_used++;
            return true;
        }

        if (entry->state == ENTRY_TOMBSTONE && first_tombstone == CACHE_SIZE) {
            /* Remember first tombstone for reuse */
            first_tombstone = index;
        }

        if (entry->state == ENTRY_VALID &&
            entry->hash == hash &&
            strcmp(entry->query, normalized_query) == 0) {
            /* Entry already exists - update it */
            strncpy(entry->action, action, MAX_ACTION_LEN - 1);
            entry->action[MAX_ACTION_LEN - 1] = '\0';
            entry->trust_level = trust_level;
            entry->last_access_time = cache->time_counter++;
            return true;
        }

        /* Continue probing */
        index = (index + 1) % CACHE_SIZE;
    } while (index != original_index);

    /* Cache is full */
    return false;
}

bool cache_remove(decision_cache_t *cache, const char *normalized_query)
{
    if (!cache || !normalized_query) {
        return false;
    }

    uint64_t hash = cache_hash(normalized_query);
    size_t index = hash % CACHE_SIZE;
    size_t original_index = index;

    /* Linear probing to find entry */
    do {
        cache_entry_t *entry = &cache->entries[index];

        if (entry->state == ENTRY_EMPTY) {
            /* Not found */
            return false;
        }

        if (entry->state == ENTRY_VALID &&
            entry->hash == hash &&
            strcmp(entry->query, normalized_query) == 0) {
            /* Found - mark as tombstone */
            entry->state = ENTRY_TOMBSTONE;
            cache->stats.entries_used--;
            return true;
        }

        /* Continue probing */
        index = (index + 1) % CACHE_SIZE;
    } while (index != original_index);

    return false;
}

void cache_clear(decision_cache_t *cache)
{
    if (!cache) {
        return;
    }

    for (size_t i = 0; i < CACHE_SIZE; i++) {
        cache->entries[i].state = ENTRY_EMPTY;
    }

    cache->stats.entries_used = 0;
    /* Preserve lookup statistics */
}

void cache_get_stats(const decision_cache_t *cache, cache_stats_t *stats)
{
    if (!cache || !stats) {
        return;
    }

    memcpy(stats, &cache->stats, sizeof(cache_stats_t));
}

void cache_print_stats(const decision_cache_t *cache)
{
    if (!cache) {
        return;
    }

    printf("\n========== DECISION CACHE STATISTICS ==========\n");
    printf("Total lookups:     %llu\n", (unsigned long long)cache->stats.total_lookups);
    printf("Cache hits:        %llu\n", (unsigned long long)cache->stats.cache_hits);
    printf("Cache misses:      %llu\n", (unsigned long long)cache->stats.cache_misses);
    printf("Hit rate:          %.2f%%\n", cache->stats.hit_rate * 100.0);
    printf("Entries used:      %u / %d\n", cache->stats.entries_used, CACHE_SIZE);
    printf("Cache occupancy:   %.2f%%\n", (cache->stats.entries_used * 100.0) / CACHE_SIZE);
    printf("==============================================\n\n");
}

void cache_dump(const decision_cache_t *cache)
{
    if (!cache) {
        return;
    }

    printf("\n========== DECISION CACHE DUMP ==========\n");
    printf("Showing all VALID entries:\n\n");

    int count = 0;
    for (size_t i = 0; i < CACHE_SIZE; i++) {
        const cache_entry_t *entry = &cache->entries[i];
        if (entry->state == ENTRY_VALID) {
            count++;
            printf("[%zu] Query: \"%s\"\n", i, entry->query);
            printf("     Action: \"%s\"\n", entry->action);
            printf("     Trust: %d | Hits: %u | Last: %llu\n",
                   entry->trust_level,
                   entry->hit_count,
                   (unsigned long long)entry->last_access_time);
            printf("\n");
        }
    }

    printf("Total valid entries: %d\n", count);
    printf("=========================================\n\n");
}
