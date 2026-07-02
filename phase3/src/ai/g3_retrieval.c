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

/* ---- G3/M6: injection-hygiene variants (fix the A/B P6 cross-topic echo) ---- */

int g3_select_exact_only(const g3_candidate_t *cands, int n, uint64_t query_key, g3_candidate_t *out)
{
    if (!cands || !out || n <= 0)
        return 0;

    /* Newest-seq candidate whose key EXACTLY matches — and ONLY that. No recency fallback:
     * a newer-but-different-key fact must never be injected into an unrelated query (the A/B P6
     * leak). Strict '>' makes input order break seq ties (deterministic). */
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (cands[i].query_key == query_key &&
            (best < 0 || cands[i].seq > cands[best].seq))
            best = i;
    }
    if (best < 0)
        return 0;   /* no exact key => no injection */
    out[0] = cands[best];
    return 1;
}

/* Clean-boundary truncation length for the injected prior answer (G3/M6b — fix the P7 "### W"
 * dangling-header glitch): cap at G3_R_MAX; if we truncated, back up to the last COMPLETE word
 * (last space) so no partial word bleeds through; then drop a trailing INCOMPLETE markdown header
 * fragment — a last line that is a bare "#"-run with too little text after it (e.g. "### W",
 * "###"). All trims are gated on truncation, so a complete short answer is kept verbatim.
 * Length-only, no strlen; returns a length in [0, min(resp_len, G3_R_MAX)]. */
static int g3_clean_answer_len(const char *resp, int resp_len)
{
    if (!resp || resp_len <= 0)
        return 0;

    int truncated = resp_len > G3_R_MAX;
    int n = truncated ? G3_R_MAX : resp_len;

    if (truncated) {
        /* Back up to the last space so the last word is complete. (No space in the window =>
         * one long token — keep the hard cut.) */
        int cut = n;
        while (cut > 0 && resp[cut - 1] != ' ')
            cut--;
        if (cut > 0)
            n = cut;
        /* Strip trailing whitespace. */
        while (n > 0 && (resp[n - 1] == ' ' || resp[n - 1] == '\n' ||
                        resp[n - 1] == '\r' || resp[n - 1] == '\t'))
            n--;
        /* Drop a trailing incomplete markdown header ("#"-run with < 3 chars of text after it),
         * which sits at the start of the last line — so we never emit a dangling "### W". */
        int ls = n;
        while (ls > 0 && resp[ls - 1] != '\n')
            ls--;                        /* ls = start of the last line */
        int h = ls;
        while (h < n && resp[h] == '#')  /* count the leading '#' run on that line */
            h++;
        if (h > ls) {                    /* the last line is a header line */
            int t = h;
            if (t < n && resp[t] == ' ')
                t++;
            if (n - t < 3) {             /* too little header text => incomplete fragment */
                n = ls;                  /* drop the whole dangling header line */
                while (n > 0 && (resp[n - 1] == '\n' || resp[n - 1] == ' '))
                    n--;                 /* strip the newline/space before it */
            }
        }
    }
    return n;
}

int g3_build_preamble_answer_only(const g3_candidate_t *sel, int n, char *out, int cap)
{
    if (!out || cap <= 0)
        return 0;
    out[0] = '\0';
    if (!sel || n <= 0)
        return 0;   /* no relevant memory => no preamble */

    int limit = cap - 1;
    if (limit > PREAMBLE_MAX_BYTES - 1)
        limit = PREAMBLE_MAX_BYTES - 1;
    if (limit < 0)
        limit = 0;

    int pos = 0;
    /* G3/M6b: reframe the label so the context reads as material to BUILD ON, not repeat (a soft
     * nudge against the P7 verbatim self-restatement). Kept short (counts against the token cap). */
    static const char HDR[] = "Notes from a previous answer (use as reference; add new detail, do not repeat):\n";
    g3_append(out, &pos, limit, HDR, (int)(sizeof HDR - 1));

    /* ANSWER-ONLY: emit each selected record's RESPONSE (never its query). No "- " / question
     * text, so the prior QUESTION can't read like an instruction and get echoed verbatim.
     * g3_clean_answer_len cuts at a word boundary + strips a dangling markdown header (fix P7). */
    int facts = n < G3_MAX_FACTS ? n : G3_MAX_FACTS;
    for (int i = 0; i < facts; i++) {
        if (sel[i].resp) {
            int rn = g3_clean_answer_len(sel[i].resp, sel[i].resp_len);
            g3_append(out, &pos, limit, sel[i].resp, rn);
        }
        g3_append(out, &pos, limit, "\n", 1);
    }

    out[pos] = '\0';   /* pos <= limit <= cap-1 — never past cap */
    return pos;
}
