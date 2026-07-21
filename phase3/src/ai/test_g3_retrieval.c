/* JARVIS AI-OS — HOST unit test for the Phase 5 G3/M0 retrieval scorer + preamble assembler.
 *
 * Pure-logic, no model/device: exact-key + recency selection and the delimited preamble
 * builder (per-field + total truncation, *_len-honoring, never-past-cap, empty path).
 *
 * Build (see .github/workflows/ci.yml):
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *     phase3/src/ai/test_g3_retrieval.c phase3/src/ai/g3_retrieval.c -o /tmp/tg3 && /tmp/tg3
 */
#include "g3_retrieval.h"
#include <stdio.h>
#include <string.h>

static int pass = 0, fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { pass++; } \
    else { fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

/* ================================================================
 * T1 — scorer: exact-key newest-wins + dedup, then recency fallback
 * ================================================================ */
static void test_scorer(void)
{
    /* 0xAA appears twice (seq 1 and 5) — the newest (seq 5) must win + dedup. */
    g3_candidate_t cands[] = {
        { .query_key = 0xAA, .seq = 1, .query = "a",  .query_len = 1, .resp = "ra",  .resp_len = 2 },
        { .query_key = 0xBB, .seq = 2, .query = "b",  .query_len = 1, .resp = "rb",  .resp_len = 2 },
        { .query_key = 0xAA, .seq = 5, .query = "a2", .query_len = 2, .resp = "ra2", .resp_len = 3 },
        { .query_key = 0xCC, .seq = 3, .query = "c",  .query_len = 1, .resp = "rc",  .resp_len = 2 },
    };
    g3_candidate_t out[3];

    /* Exact key 0xAA, max 3: out[0]=AA(seq5 newest exact), then recency CC(3), BB(2); dedup. */
    int n = g3_select(cands, 4, 0xAA, 3, out);
    CHECK(n == 3, "T1 select count == 3");
    CHECK(out[0].query_key == 0xAA && out[0].seq == 5, "T1 exact match first = newest seq (dedup)");
    CHECK(out[1].query_key == 0xCC && out[1].seq == 3, "T1 recency fill #1 = next-newest (CC seq3)");
    CHECK(out[2].query_key == 0xBB && out[2].seq == 2, "T1 recency fill #2 = BB seq2");
    /* dedup: no key twice */
    CHECK(!(out[0].query_key == out[1].query_key || out[1].query_key == out[2].query_key
            || out[0].query_key == out[2].query_key), "T1 deduped by key");

    /* No exact match (0xFF), max 2: pure recency, newest-first, deduped: AA(5), CC(3). */
    int m = g3_select(cands, 4, 0xFF, 2, out);
    CHECK(m == 2, "T1 no-exact: recency fills max=2");
    CHECK(out[0].seq == 5 && out[1].seq == 3, "T1 no-exact: newest-first (seq 5 then 3)");

    /* max caps the count. */
    CHECK(g3_select(cands, 4, 0xAA, 1, out) == 1 && out[0].seq == 5, "T1 max=1 => 1 (the exact)");
    /* empty input. */
    CHECK(g3_select(cands, 0, 0xAA, 3, out) == 0, "T1 n=0 => 0");
}

/* ================================================================
 * T2 — preamble byte-exact
 * ================================================================ */
static void test_preamble_exact(void)
{
    g3_candidate_t sel[2] = {
        { .query = "what time is it", .query_len = 15, .resp = "noon", .resp_len = 4 },
        { .query = "status",          .query_len = 6,  .resp = "ok",   .resp_len = 2 },
    };
    char buf[256];
    int len = g3_build_preamble(sel, 2, buf, (int)sizeof buf);
    const char *expect = "Known context:\n- what time is it: noon\n- status: ok\n";
    CHECK(strcmp(buf, expect) == 0, "T2 preamble byte-exact");
    CHECK(len == (int)strlen(expect), "T2 returned length == strlen(preamble)");
}

/* ================================================================
 * T3 — empty path: 0 selected => len 0, empty string
 * ================================================================ */
static void test_empty(void)
{
    char buf[64];
    memset(buf, 0xAA, sizeof buf);
    int len = g3_build_preamble(NULL, 0, buf, (int)sizeof buf);
    CHECK(len == 0, "T3 empty => len 0");
    CHECK(buf[0] == '\0', "T3 empty => out[0] == NUL (no-memory baseline)");
}

/* ================================================================
 * T4 — truncation: per-field + a small cap, with a 0xAA canary past cap
 * ================================================================ */
static void test_truncation(void)
{
    /* query way over G3_Q_MAX, resp over G3_R_MAX. */
    char bigq[200], bigr[300];
    memset(bigq, 'q', sizeof bigq);
    memset(bigr, 'r', sizeof bigr);
    g3_candidate_t sel[1] = {
        { .query = bigq, .query_len = (uint16_t)sizeof bigq, .resp = bigr, .resp_len = (uint16_t)sizeof bigr },
    };

    /* Per-field truncation into a generous buffer. */
    char buf[PREAMBLE_MAX_BYTES + 64];
    int len = g3_build_preamble(sel, 1, buf, (int)sizeof buf);
    CHECK(len == (int)strlen(buf), "T4 returned length == strlen (NUL-terminated)");
    /* header(15) + "- "(2) + 80 q + ": "(2) + 120 r + "\n"(1) = 220 */
    CHECK(len == 15 + 2 + G3_Q_MAX + 2 + G3_R_MAX + 1, "T4 per-field truncation to G3_Q_MAX/G3_R_MAX");

    /* Tiny cap + canary: never write at/after cap. */
    char small[40];
    memset(small, 0xAA, sizeof small);
    const int cap = 20;
    int len2 = g3_build_preamble(sel, 1, small, cap);
    CHECK(len2 <= cap - 1, "T4 length <= cap-1");
    CHECK(small[len2] == '\0', "T4 NUL at returned length");
    int canary_ok = 1;
    for (int i = cap; i < (int)sizeof small; i++)
        if ((unsigned char)small[i] != 0xAA) canary_ok = 0;
    CHECK(canary_ok, "T4 never writes at/past cap (0xAA canary intact)");
}

/* ================================================================
 * T5 — NUL-safety: query/resp are NOT NUL-terminated (copied by *_len)
 * ================================================================ */
static void test_nul_safety(void)
{
    /* "abcdefGARBAGE" — query points at it but query_len=6 => only "abcdef". */
    const char qbuf[] = "abcdefGARBAGE";
    const char rbuf[] = "okEXTRA";
    g3_candidate_t sel[1] = {
        { .query = qbuf, .query_len = 6, .resp = rbuf, .resp_len = 2 },
    };
    char buf[128];
    g3_build_preamble(sel, 1, buf, (int)sizeof buf);
    CHECK(strcmp(buf, "Known context:\n- abcdef: ok\n") == 0,
          "T5 assembled by *_len, not strlen (no GARBAGE/EXTRA leak)");
}

/* ================================================================
 * T6 — G3/M2 prompt budget: preamble token room, clamped to [0, cap]
 *      room = cap - n_prompt - suffix - query_floor, clamped to [0, G3_PREAMBLE_TOK_CAP]
 * ================================================================ */
static void test_budget(void)
{
    CHECK(g3_prompt_budget(0,   256, 48, 6) == 160, "T6 fresh: 256-0-6-48=202 -> clamp to cap 160");
    CHECK(g3_prompt_budget(4,   256, 48, 6) == 160, "T6 after prefix: 256-4-6-48=198 -> clamp to cap 160");
    CHECK(g3_prompt_budget(200, 256, 48, 6) == 2,   "T6 nearly full: 256-200-6-48 = 2");
    CHECK(g3_prompt_budget(250, 256, 48, 6) == 0,   "T6 query_floor wins: 256-250-6-48=-48 -> clamp 0");
    CHECK(g3_prompt_budget(300, 256, 48, 6) == 0,   "T6 n_prompt > cap -> clamp 0");
    CHECK(G3_PREAMBLE_TOK_CAP == 160, "T6 G3_PREAMBLE_TOK_CAP == 160 (deployed cap)");
}

/* ================================================================
 * T7 — G3/M3 usability predicate: only SUCCESSFUL inference records are retrievable.
 *      (cache-action records polluted the preamble -> <|channel> thought restatement,
 *       box-observed 2026-06-28.)  args: (action, outcome, resp_len, infer_action, ok_outcome)
 * ================================================================ */
static void test_usable(void)
{
    CHECK(g3_candidate_usable(2, 0, 50, 2, 0) == 1, "T7 INFER + OK + text -> usable");
    CHECK(g3_candidate_usable(1, 0, 50, 2, 0) == 0, "T7 CACHE action -> excluded");
    CHECK(g3_candidate_usable(2, 1, 50, 2, 0) == 0, "T7 INFER but ERROR outcome -> excluded");
    CHECK(g3_candidate_usable(2, 2, 50, 2, 0) == 0, "T7 INFER but BLOCKED outcome -> excluded");
    CHECK(g3_candidate_usable(2, 0,  0, 2, 0) == 0, "T7 INFER + OK but empty resp -> excluded");
}

/* ================================================================
 * T11 — 6-5/M5-recall TAG ISOLATION. Control-IN recall passes want_action=EPI_ACT_CONTROL_IN (3)
 *       so it sources ONLY prior control-IN turns; the workload path passes EPI_ACT_INFER (2).
 *       The two lanes must be mutually exclusive IN BOTH DIRECTIONS: a workload answer must never
 *       ground a control-IN reply, and a control-IN answer (attacker-influenced text, tag-isolated
 *       from cache-growth/retrieval/distill by O-Q12) must never leak into the workload preamble.
 *       Codes mirror episodic_store.h: EPI_ACT_CACHE 1 / EPI_ACT_INFER 2 / EPI_ACT_CONTROL_IN 3.
 * ================================================================ */
static void test_tag_isolation(void)
{
    /* want CONTROL_IN: only tag-3 is sourced */
    CHECK(g3_candidate_usable(3, 0, 50, 3, 0) == 1, "T11 CONTROL_IN record, want CONTROL_IN -> usable");
    CHECK(g3_candidate_usable(2, 0, 50, 3, 0) == 0, "T11 INFER record, want CONTROL_IN -> excluded");
    CHECK(g3_candidate_usable(1, 0, 50, 3, 0) == 0, "T11 CACHE record, want CONTROL_IN -> excluded");

    /* want INFER: tag-3 must NOT leak into the workload lane */
    CHECK(g3_candidate_usable(3, 0, 50, 2, 0) == 0, "T11 CONTROL_IN record, want INFER -> excluded");

    /* the outcome/emptiness filters still bite on the control-IN lane (a refused/timed-out or
     * empty-answer turn is not a memory) */
    CHECK(g3_candidate_usable(3, 1, 50, 3, 0) == 0, "T11 CONTROL_IN but ERROR outcome -> excluded");
    CHECK(g3_candidate_usable(3, 2, 50, 3, 0) == 0, "T11 CONTROL_IN but BLOCKED outcome -> excluded");
    CHECK(g3_candidate_usable(3, 0,  0, 3, 0) == 0, "T11 CONTROL_IN + OK but empty resp -> excluded");
}

/* ================================================================
 * T8 — G3/M6 exact-key-ONLY select: exact hit newest wins; NO recency fallback
 *      (fixes the A/B P6 leak where a newer, WRONG-key fact was injected).
 * ================================================================ */
static void test_exact_only(void)
{
    g3_candidate_t cands[] = {
        { .query_key = 0xAA, .seq = 1, .resp = "ra",  .resp_len = 2 },
        { .query_key = 0xBB, .seq = 9, .resp = "rb",  .resp_len = 2 },   /* newest OVERALL, wrong key */
        { .query_key = 0xAA, .seq = 5, .resp = "ra2", .resp_len = 3 },   /* newest with key 0xAA */
        { .query_key = 0xCC, .seq = 3, .resp = "rc",  .resp_len = 2 },
    };
    g3_candidate_t out[1];

    CHECK(g3_select_exact_only(cands, 4, 0xAA, out) == 1, "T8 exact hit -> 1");
    CHECK(out[0].query_key == 0xAA && out[0].seq == 5,
          "T8 exact hit = newest-seq exact (dup -> newest, NOT the newer wrong-key BB seq9)");
    CHECK(g3_select_exact_only(cands, 4, 0xFF, out) == 0,
          "T8 no exact key -> 0 (does NOT fall back to recency)");
    CHECK(g3_select_exact_only(cands, 4, 0xEE, out) == 0, "T8 recency-only candidate set -> 0");
    CHECK(g3_select_exact_only(cands, 0, 0xAA, out) == 0, "T8 n=0 -> 0");
}

/* ================================================================
 * T9 — G3/M6 fenced ANSWER-ONLY preamble: no prior-question text (anti-echo), answer present,
 *      cap-safe (0xAA over-run canary), empty path.
 * ================================================================ */
static void test_answer_only(void)
{
    const char *Q = "What is the seL4 microkernel";   /* the prior QUESTION — must NOT appear */
    const char *R = "seL4 is a formally verified microkernel";
    g3_candidate_t sel[1] = {
        { .query = Q, .query_len = (uint16_t)strlen(Q), .resp = R, .resp_len = (uint16_t)strlen(R) },
    };
    char buf[256];
    int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf);
    CHECK(len > 0, "T9 answer-only preamble built");
    CHECK(strstr(buf, "Notes from a previous answer") != NULL,
          "T9 build-on label present");
    CHECK(strstr(buf, R) != NULL, "T9 answer text present");
    CHECK(strstr(buf, Q) == NULL, "T9 anti-echo: prior QUESTION text NOT in preamble");
    CHECK(strstr(buf, "- ") == NULL, "T9 anti-echo: no '- <query>' pattern");

    /* cap-safe: a giant response into a tiny cap must NOT overrun (0xAA canary past cap intact). */
    char cbuf[64];
    memset(cbuf, (int)0xAA, sizeof cbuf);
    const int CAP = 32;
    char big[300];
    memset(big, 'Z', sizeof big);
    g3_candidate_t sbig[1] = { { .query = Q, .query_len = 5, .resp = big, .resp_len = 300 } };
    int l2 = g3_build_preamble_answer_only(sbig, 1, cbuf, CAP);
    CHECK(l2 <= CAP - 1 && cbuf[l2] == '\0', "T9 cap: len<=cap-1 + NUL-terminated");
    CHECK((unsigned char)cbuf[CAP] == 0xAA && (unsigned char)cbuf[63] == 0xAA,
          "T9 cap: 0xAA canary past cap intact (no overrun)");

    /* empty sel -> 0, out[0]=='\0'. */
    char ebuf[16] = "x";
    CHECK(g3_build_preamble_answer_only(NULL, 0, ebuf, (int)sizeof ebuf) == 0 && ebuf[0] == '\0',
          "T9 empty sel -> 0 + NUL");
}

/* ================================================================
 * T10 — G3/M6b clean-boundary truncation: word-boundary cut + strip a dangling "### W" markdown
 *       header fragment (the P7 self-echo glitch). Rides the same G3 CI step.
 * ================================================================ */
static void test_clean_truncation(void)
{
    /* Craft a response where a hard cut at G3_R_MAX lands inside a trailing markdown header
     * "### What ..." (=> the "### W" glitch). Clean-truncation must cut at a word boundary AND
     * drop the incomplete header line. */
    char resp[300];
    int p = 0;
    while (p < 111) { resp[p++] = 'a'; resp[p++] = 'b'; resp[p++] = ' '; }   /* 37x "ab " = 111 bytes */
    resp[p++] = '\n';                                                        /* header on its own line */
    static const char TAIL[] = "### What is important";
    for (int k = 0; k < (int)(sizeof TAIL - 1); k++) resp[p++] = TAIL[k];
    int resp_len = p;   /* 133 > G3_R_MAX(120): the hard cut lands inside "### What" */

    g3_candidate_t sel[1] = { { .resp = resp, .resp_len = (uint16_t)resp_len } };
    char buf[256];
    int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf);
    CHECK(len > 0, "T10 preamble built");
    CHECK(strstr(buf, "### W") == NULL, "T10 dangling '### W' fragment gone");
    CHECK(strstr(buf, "###") == NULL, "T10 trailing '#'-run stripped");
    CHECK(strstr(buf, "ab ab") != NULL, "T10 answer content still present");
    CHECK(strstr(buf, "Notes from a previous answer") != NULL, "T10 build-on label present");
    {
        int L = (int)strlen(buf);
        while (L > 0 && buf[L - 1] == '\n') L--;   /* trim trailing newline */
        CHECK(L == 0 || buf[L - 1] != '#', "T10 output does not end with a bare '#'");
        CHECK(L >= 2 && buf[L - 1] == 'b' && buf[L - 2] == 'a',
              "T10 truncation cut at a complete word boundary (ends '...ab')");
    }
    CHECK(len <= PREAMBLE_MAX_BYTES - 1, "T10 total within PREAMBLE_MAX_BYTES");

    /* cap canary: same crafted input into a tiny cap must not overrun. */
    char cbuf[80];
    memset(cbuf, (int)0xAA, sizeof cbuf);
    const int CAP = 48;
    int l2 = g3_build_preamble_answer_only(sel, 1, cbuf, CAP);
    CHECK(l2 <= CAP - 1 && cbuf[l2] == '\0', "T10 cap: len<=cap-1 + NUL");
    CHECK((unsigned char)cbuf[CAP] == 0xAA && (unsigned char)cbuf[79] == 0xAA, "T10 cap: 0xAA canary intact");
}

int main(void)
{
    printf("=== G3 Retrieval Tests (Phase 5 G3/M0 + M2 budget + M3 filter + M6 hygiene) ===\n");
    test_scorer();
    test_preamble_exact();
    test_empty();
    test_truncation();
    test_nul_safety();
    test_budget();
    test_usable();
    test_tag_isolation();
    test_exact_only();
    test_answer_only();
    test_clean_truncation();
    printf("\n== Results: %d PASS, %d FAIL ==\n", pass, fail);
    return fail ? 1 : 0;
}
