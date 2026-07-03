/*
 * JARVIS AI-OS — Phase 5 #6: Cache Growth — promotion selector implementation.
 * See cache_growth.h + phase5/docs/PHASE_5_GOAL6_CACHE_GROWTH.md (D-b).
 */

#include "cache_growth.h"

int cg_select_promotions(const cg_candidate_t *cands, int n, int threshold,
                         cg_promotion_t *out, int max)
{
    if (!cands || !out || n <= 0 || max <= 0) {
        return 0;
    }

    int count = 0;

    for (int i = 0; i < n && count < max; i++) {
        uint64_t key = cands[i].query_key;

        /* Dedup: skip keys already promoted (compare by key only — text is borrowed). */
        int already = 0;
        for (int k = 0; k < count; k++) {
            if (out[k].query_key == key) { already = 1; break; }
        }
        if (already) {
            continue;
        }

        /* Count frequency across the whole window; track the newest (last) occurrence.
         * Input order is oldest->newest, so the last matching index is the newest record. */
        int freq = 0;
        int newest = i;
        for (int j = 0; j < n; j++) {
            if (cands[j].query_key == key) {
                freq++;
                newest = j;
            }
        }

        if (freq >= threshold) {
            out[count++] = cands[newest];   /* emit the newest candidate for this key */
        }
    }

    return count;
}

uint32_t cg_freq_bump(cg_freq_slot_t *tbl, int cap, uint64_t key)
{
    if (!tbl || cap <= 0) {
        return 0;
    }

    int start = (int)(key % (uint64_t)cap);
    for (int probe = 0; probe < cap; probe++) {
        cg_freq_slot_t *s = &tbl[(start + probe) % cap];
        if (s->count == 0) {            /* empty slot: insert */
            s->key = key;
            s->count = 1;
            return 1;
        }
        if (s->key == key) {            /* found: bump */
            return ++s->count;
        }
    }

    return 0;   /* table full and key absent */
}

uint32_t cg_freq_get(const cg_freq_slot_t *tbl, int cap, uint64_t key)
{
    if (!tbl || cap <= 0) {
        return 0;
    }

    int start = (int)(key % (uint64_t)cap);
    for (int probe = 0; probe < cap; probe++) {
        const cg_freq_slot_t *s = &tbl[(start + probe) % cap];
        if (s->count == 0) {            /* hit an empty slot: absent */
            return 0;
        }
        if (s->key == key) {
            return s->count;
        }
    }

    return 0;   /* full table, key absent */
}
