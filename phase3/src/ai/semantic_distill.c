/*
 * JARVIS AI-OS — Phase 5 Goal #4: Semantic Distill (deterministic — #7's core)
 *
 * Pure implementation of the D-b rule (PHASE_5_GOAL4_SEMANTIC_MEMORY.md):
 * frequency + consistency, nothing else. No I/O, no LLM, no embeddings.
 */

#include "semantic_distill.h"
#include <string.h>

int sd_record_usable(const epi_record_t *rec)
{
    if (!rec)
        return 0;
    return (rec->action == EPI_ACT_INFER &&
            rec->outcome == EPI_OUT_OK &&
            rec->resp_len > 0) ? 1 : 0;
}

/* Is key already emitted into out[0..nf)? (dedup-by-key) */
static int key_emitted(const semantic_fact_t *out, int nf, uint64_t key)
{
    for (int i = 0; i < nf; i++)
        if (out[i].key == key)
            return 1;
    return 0;
}

int sd_distill(const epi_record_t *recs, int n, int min_support,
               semantic_fact_t *out, int max)
{
    if (n < 0 || (n > 0 && !recs) || !out || max <= 0 || min_support < 1)
        return (n == 0 && min_support >= 1 && (out || max > 0)) ? 0 : -1;
    if (n == 0)
        return 0;

    int nf = 0;
    for (int i = 0; i < n; i++) {
        if (!sd_record_usable(&recs[i]))
            continue;
        uint64_t key = recs[i].query_key;
        if (key_emitted(out, nf, key))
            continue;

        /* Count support over ALL usable same-key records; track the newest
         * (recs[] is oldest->newest — episodic logical order — so the last
         * match is the newest). */
        int support = 0;
        int newest = -1;
        for (int j = 0; j < n; j++) {
            if (sd_record_usable(&recs[j]) && recs[j].query_key == key) {
                support++;
                newest = j;
            }
        }
        if (support < min_support)
            continue;
        if (nf >= max)
            return nf;   /* documented cap — the caller sees count == max */

        /* Consistency: how many usable same-key answers are byte-identical to
         * the chosen (newest) one. Compared by resp_len + bytes, never strlen. */
        const epi_record_t *chosen = &recs[newest];
        int consistent = 0;
        for (int j = 0; j < n; j++) {
            if (sd_record_usable(&recs[j]) && recs[j].query_key == key &&
                recs[j].resp_len == chosen->resp_len &&
                memcmp(recs[j].resp, chosen->resp, chosen->resp_len) == 0)
                consistent++;
        }

        semantic_fact_t *f = &out[nf];
        memset(f, 0, sizeof(*f));
        f->key             = key;
        f->fact_type       = SEM_FACT_QA;
        f->support_count   = (uint16_t)(support > 0xFFFF ? 0xFFFF : support);
        f->confidence_x100 = (uint16_t)((consistent * 100) / support);
        f->t_ms            = chosen->t_ms;
        /* boot_id/seq left 0 — sem_store stamps at write time. */

        uint16_t tl = chosen->resp_len;
        if (tl > SEM_FACT_TEXT_MAX)
            tl = SEM_FACT_TEXT_MAX;
        memcpy(f->text, chosen->resp, tl);
        f->text_len = tl;

        nf++;
    }
    return nf;
}
