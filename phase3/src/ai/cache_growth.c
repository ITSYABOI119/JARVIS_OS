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
