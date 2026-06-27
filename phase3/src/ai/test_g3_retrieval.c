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

int main(void)
{
    printf("=== G3 Retrieval Tests (Phase 5 G3/M0) ===\n");
    test_scorer();
    test_preamble_exact();
    test_empty();
    test_truncation();
    test_nul_safety();
    printf("\n== Results: %d PASS, %d FAIL ==\n", pass, fail);
    return fail ? 1 : 0;
}
