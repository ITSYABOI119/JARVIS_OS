/*
 * JARVIS AI-OS — Phase 5 Goal #3 (Retrieval Before Inference), Milestone M0 — engine.
 *
 * Pure C: the retrieval scorer (exact-key + recency, deduped by key) and the delimited
 * preamble assembler (per-field + total truncation, *_len-honoring, never-past-cap). No
 * seL4 / device / model / I/O — fully host-testable. Design: phase5/docs/
 * PHASE_5_GOAL3_RETRIEVAL.md (§2 scorer; §3 preamble).
 */

#include "g3_retrieval.h"

/* Is query_key already present in out[0..count)? (dedup-by-key check) */
static int g3_key_selected(const g3_candidate_t *out, int count, uint64_t key)
{
    for (int i = 0; i < count; i++)
        if (out[i].query_key == key)
            return 1;
    return 0;
}

int g3_select(const g3_candidate_t *cands, int n, uint64_t query_key, int max, g3_candidate_t *out)
{
    if (!cands || !out || n <= 0 || max <= 0)
        return 0;

    int count = 0;

    /* (1) Exact query_key match first: newest seq among candidates with that key. */
    int best_exact = -1;
    for (int i = 0; i < n; i++) {
        if (cands[i].query_key == query_key &&
            (best_exact < 0 || cands[i].seq > cands[best_exact].seq))
            best_exact = i;
    }
    if (best_exact >= 0 && count < max)
        out[count++] = cands[best_exact];

    /* (2) Recency fallback: repeatedly take the newest-seq candidate whose key isn't already
     * selected (dedup by key). Strict '>' makes input order break seq ties (deterministic). */
    while (count < max) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (g3_key_selected(out, count, cands[i].query_key))
                continue;
            if (best < 0 || cands[i].seq > cands[best].seq)
                best = i;
        }
        if (best < 0)
            break;   /* no more distinct keys */
        out[count++] = cands[best];
    }

    return count;
}

/* Append up to slen bytes of s into out[*pos..limit) (by length — never strlen). */
static void g3_append(char *out, int *pos, int limit, const char *s, int slen)
{
    for (int w = 0; w < slen && *pos < limit; w++)
        out[(*pos)++] = s[w];
}

int g3_build_preamble(const g3_candidate_t *sel, int n, char *out, int cap)
{
    if (!out || cap <= 0)
        return 0;
    out[0] = '\0';
    if (!sel || n <= 0)
        return 0;   /* no relevant memory => no preamble */

    /* Reserve one byte for the NUL; also honor the total PREAMBLE_MAX_BYTES cap. */
    int limit = cap - 1;
    if (limit > PREAMBLE_MAX_BYTES - 1)
        limit = PREAMBLE_MAX_BYTES - 1;
    if (limit < 0)
        limit = 0;

    int pos = 0;
    static const char HDR[] = "Known context:\n";
    g3_append(out, &pos, limit, HDR, (int)(sizeof HDR - 1));

    int facts = n < G3_MAX_FACTS ? n : G3_MAX_FACTS;
    for (int i = 0; i < facts; i++) {
        int qn = sel[i].query_len < G3_Q_MAX ? sel[i].query_len : G3_Q_MAX;
        int rn = sel[i].resp_len  < G3_R_MAX ? sel[i].resp_len  : G3_R_MAX;
        g3_append(out, &pos, limit, "- ", 2);
        if (sel[i].query) g3_append(out, &pos, limit, sel[i].query, qn);
        g3_append(out, &pos, limit, ": ", 2);
        if (sel[i].resp)  g3_append(out, &pos, limit, sel[i].resp, rn);
        g3_append(out, &pos, limit, "\n", 1);
    }

    out[pos] = '\0';   /* pos <= limit <= cap-1 — never past cap */
    return pos;
}
