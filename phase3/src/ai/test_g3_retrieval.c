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
#include "episodic_store.h"   /* for the EPI_RESP_MAX pin below */
#include <stdio.h>
#include <string.h>

/* 256 is not a tuned number: it is the WHOLE stored answer. If EPI_RESP_MAX ever
 * shrinks, the control-IN window would silently start promising bytes the store
 * cannot hold, so pin them together at compile time. */
_Static_assert(G3_R_MAX_CONTROL_IN <= EPI_RESP_MAX,
               "control-IN recall window must not exceed the stored response cap");
_Static_assert(G3_R_MAX <= G3_R_MAX_CONTROL_IN,
               "the workload window must not exceed the control-IN window");

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
    /* T12 note: the response is a COMPLETE sentence. It gained its terminating '.' when the
     * complete-sentence rule landed — an unterminated answer now yields NO preamble by design,
     * which would make every assertion below vacuous. The anti-echo intent is unchanged. */
    const char *R = "seL4 is a formally verified microkernel.";
    g3_candidate_t sel[1] = {
        { .query = Q, .query_len = (uint16_t)strlen(Q), .resp = R, .resp_len = (uint16_t)strlen(R) },
    };
    char buf[256];
    int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
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
    /* Sentence-terminated filler, NOT a flat run of 'Z': under the complete-sentence rule an
     * unterminated blob cleans to nothing, the builder writes nothing, and this cap-overrun
     * canary would pass vacuously without ever exercising truncation. */
    for (int k = 0; k < 300; k++) big[k] = ((k % 10) == 8) ? '.' : (((k % 10) == 9) ? ' ' : 'Z');
    g3_candidate_t sbig[1] = { { .query = Q, .query_len = 5, .resp = big, .resp_len = 300 } };
    int l2 = g3_build_preamble_answer_only(sbig, 1, cbuf, CAP, G3_R_MAX);
    CHECK(l2 <= CAP - 1 && cbuf[l2] == '\0', "T9 cap: len<=cap-1 + NUL-terminated");
    CHECK((unsigned char)cbuf[CAP] == 0xAA && (unsigned char)cbuf[63] == 0xAA,
          "T9 cap: 0xAA canary past cap intact (no overrun)");

    /* empty sel -> 0, out[0]=='\0'. */
    char ebuf[16] = "x";
    CHECK(g3_build_preamble_answer_only(NULL, 0, ebuf, (int)sizeof ebuf, G3_R_MAX) == 0 && ebuf[0] == '\0',
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
    /* T12 note: the filler is now SENTENCE-TERMINATED ("ab ab ab. " x11 = 110 B). It used to be a
     * flat "ab " run, which under the complete-sentence rule cleans to nothing — the dangling-header
     * assertions below would then pass vacuously against an empty preamble, testing nothing. The
     * P7 intent (a "### W" fragment must never be injected) is unchanged and still exercised: the
     * hard G3_R_MAX cut still lands inside the trailing header line. */
    while (p < 110) {
        static const char UNIT[] = "ab ab ab. ";
        for (int k = 0; k < (int)(sizeof UNIT - 1); k++) resp[p++] = UNIT[k];
    }
    resp[p++] = '\n';                                                        /* header on its own line */
    static const char TAIL[] = "### What is important";
    for (int k = 0; k < (int)(sizeof TAIL - 1); k++) resp[p++] = TAIL[k];
    int resp_len = p;   /* 133 > G3_R_MAX(120): the hard cut lands inside "### What" */

    g3_candidate_t sel[1] = { { .resp = resp, .resp_len = (uint16_t)resp_len } };
    char buf[256];
    int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
    CHECK(len > 0, "T10 preamble built");
    CHECK(strstr(buf, "### W") == NULL, "T10 dangling '### W' fragment gone");
    CHECK(strstr(buf, "###") == NULL, "T10 trailing '#'-run stripped");
    CHECK(strstr(buf, "ab ab") != NULL, "T10 answer content still present");
    CHECK(strstr(buf, "Notes from a previous answer") != NULL, "T10 build-on label present");
    {
        int L = (int)strlen(buf);
        while (L > 0 && buf[L - 1] == '\n') L--;   /* trim trailing newline */
        CHECK(L == 0 || buf[L - 1] != '#', "T10 output does not end with a bare '#'");
        /* T12 note: this assertion used to require the cut land on a complete WORD (ends "...ab").
         * A complete word is no longer sufficient — a word-boundary cut still ends mid-clause, which
         * is the measured trigger for the thinking-channel garbage. The stronger property replaces
         * it: the cut lands on a complete SENTENCE. */
        CHECK(L >= 1 && buf[L - 1] == '.',
              "T10 truncation cut at a complete SENTENCE boundary (ends '.')");
    }
    CHECK(len <= PREAMBLE_MAX_BYTES - 1, "T10 total within PREAMBLE_MAX_BYTES");

    /* cap canary: same crafted input into a tiny cap must not overrun. */
    char cbuf[80];
    memset(cbuf, (int)0xAA, sizeof cbuf);
    const int CAP = 48;
    int l2 = g3_build_preamble_answer_only(sel, 1, cbuf, CAP, G3_R_MAX);
    CHECK(l2 <= CAP - 1 && cbuf[l2] == '\0', "T10 cap: len<=cap-1 + NUL");
    CHECK((unsigned char)cbuf[CAP] == 0xAA && (unsigned char)cbuf[79] == 0xAA, "T10 cap: 0xAA canary intact");

    /* The ORIGINAL T10 input — a flat "ab " run with a dangling header and NO sentence anywhere —
     * kept here so the contract change is pinned rather than merely edited away: it used to yield a
     * word-boundary-cut preamble, and now correctly yields NO preamble at all. */
    {
        char noterm[300];
        int q = 0;
        while (q < 111) { noterm[q++] = 'a'; noterm[q++] = 'b'; noterm[q++] = ' '; }
        noterm[q++] = '\n';
        for (int k = 0; k < (int)(sizeof TAIL - 1); k++) noterm[q++] = TAIL[k];
        g3_candidate_t s2[1] = { { .resp = noterm, .resp_len = (uint16_t)q } };
        char b2[256];
        memset(b2, (int)0xAA, sizeof b2);
        int l3 = g3_build_preamble_answer_only(s2, 1, b2, (int)sizeof b2, G3_R_MAX);
        CHECK(l3 == 0, "T10 sentence-less answer => NO preamble (was: word-boundary fragment)");
        CHECK(b2[0] == '\0', "T10 sentence-less: NUL-terminated empty output");
    }
}

/* T12 — SENTENCE-BOUNDARY injection (the boot-40 cross-session-recall defect).
 *
 * Both inputs below are the REAL bytes read off the box's control-IN store, so this test
 * pins the actual defect rather than a reconstruction of it:
 *
 *   REAL_FAILING  = record [8:00042]. Its first G3_R_MAX(120) bytes contain NO sentence
 *                   terminator, so the old word-boundary cut injected "...its single
 *                   execution thread," -- a mid-sentence fragment. MEASURED on the real
 *                   engine: that preamble makes Gemma emit "<|channel>thought ..." instead
 *                   of an answer, byte-identical to the garbage the box stored at [9:00045].
 *   REAL_WORKED   = record [1:00000], the marker answer the M5-recall flip gate used. Its
 *                   120-byte window DOES contain a completed sentence, which is why that
 *                   gate passed and never exercised this path.
 *
 * Contract: inject only COMPLETE sentences. No completed sentence in the window => no
 * preamble at all (fall back to the clean no-preamble behaviour), never a fragment. */
static const char REAL_FAILING[] =
    "The short answer is: **A single-threaded program is fundamentally limited by the speed "
    "of its single execution thread, regardless of how many CPU cores are available.**\n\n"
    "Here is a detailed breakdown of why adding more CPU cores doesn't speed up a";
static const char REAL_WORKED[] =
    "Unfortunately, I do not have access to a specific, universally defined "
    "\"JARVIS-MINIFLIP-7X reference value.\" Please provide more context about what this "
    "reference value pertains to so I can try to assist you.<turn|><turn|><turn|><eos>";

static void test_sentence_boundary(void)
{
    char buf[512];

    /* A: the real failing record -- no completed sentence within the window => NO preamble.
     * This is the case that shipped garbage to a live, default-ON feature. */
    {
        g3_candidate_t sel[1] = { { .resp = REAL_FAILING,
                                    .resp_len = (uint16_t)(sizeof REAL_FAILING - 1) } };
        memset(buf, (int)0xAA, sizeof buf);
        int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
        CHECK(len == 0, "T12A real failing record => NO preamble (no complete sentence in window)");
        CHECK(buf[0] == '\0', "T12A empty preamble is NUL-terminated");
        CHECK(strstr(buf, "execution thread,") == NULL, "T12A mid-sentence fragment never injected");
    }

    /* B: the real marker record -- keeps the completed sentence, drops the dangling tail.
     * The flip gate must still pass: short facts still recall. */
    {
        g3_candidate_t sel[1] = { { .resp = REAL_WORKED,
                                    .resp_len = (uint16_t)(sizeof REAL_WORKED - 1) } };
        int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
        CHECK(len > 0, "T12B real marker record still recalls");
        CHECK(strstr(buf, "reference value.\"") != NULL, "T12B completed sentence kept (incl. closing quote)");
        CHECK(strstr(buf, "Please") == NULL, "T12B dangling partial sentence dropped");
    }

    /* C: a SHORT complete answer is passed through untouched -- the recall use case
     * ("state a fact, ask about it later") must keep working. */
    {
        static const char FACT[] = "A TLB caches virtual-to-physical address translations.";
        g3_candidate_t sel[1] = { { .resp = FACT, .resp_len = (uint16_t)(sizeof FACT - 1) } };
        int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
        CHECK(len > 0, "T12C short complete answer recalls");
        CHECK(strstr(buf, FACT) != NULL, "T12C short complete answer preserved verbatim");
    }

    /* D: an answer with NO terminator at all (e.g. a system-facts string) => no preamble. */
    {
        static const char NOTERM[] = "up 255 seconds";
        g3_candidate_t sel[1] = { { .resp = NOTERM, .resp_len = (uint16_t)(sizeof NOTERM - 1) } };
        int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
        CHECK(len == 0, "T12D answer with no sentence terminator => NO preamble");
    }

    /* E: the general invariant -- a NON-EMPTY preamble always ends at a sentence terminator
     * (optionally followed by closing punctuation), never mid-clause. */
    {
        g3_candidate_t sel[1] = { { .resp = REAL_WORKED,
                                    .resp_len = (uint16_t)(sizeof REAL_WORKED - 1) } };
        int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
        int L = len;
        while (L > 0 && (buf[L - 1] == '\n' || buf[L - 1] == ' ')) L--;
        while (L > 0 && (buf[L - 1] == '"' || buf[L - 1] == '\'' || buf[L - 1] == ')' ||
                         buf[L - 1] == ']' || buf[L - 1] == '*' || buf[L - 1] == '`')) L--;
        CHECK(L > 0 && (buf[L - 1] == '.' || buf[L - 1] == '?' || buf[L - 1] == '!'),
              "T12E non-empty preamble ends at a sentence terminator");
    }
}

/* T13 — thought scratch is NOT recallable, and enumeration is not a sentence.
 *
 * REAL_THOUGHT is the actual stored text of control-IN record [9:00045] read off the box: the
 * garbage answer the pre-fix box produced, which then got RECALLED on boot 41 (recall=2 instead
 * of the predicted 1) because "1." parsed as a completed sentence. Both halves are pinned. */
static const char REAL_THOUGHT[] =
    "<|channel>thought\nHere's a thinking process that leads to the suggested answer:\n\n"
    "1.  **Analyze the Request:**\n    *   **Core Topic:** Why adding more CPU cores doesn't "
    "speed up a *single-threaded* program";

static void test_thought_filter(void)
{
    char buf[512];

    /* A: the marker predicate itself, both ways. */
    CHECK(g3_text_has_thought_marker(REAL_THOUGHT, (int)(sizeof REAL_THOUGHT - 1)) == 1,
          "T13A real stored thought record detected");
    {
        static const char CH_CLOSE[] = "answer text <channel|> more";
        static const char THINK[]    = "a <|think|> b";
        static const char CLEAN[]    = "A page fault is an exception raised by the MMU.";
        CHECK(g3_text_has_thought_marker(CH_CLOSE, (int)(sizeof CH_CLOSE - 1)) == 1,
              "T13A closing channel marker detected");
        CHECK(g3_text_has_thought_marker(THINK, (int)(sizeof THINK - 1)) == 1,
              "T13A think marker detected");
        CHECK(g3_text_has_thought_marker(CLEAN, (int)(sizeof CLEAN - 1)) == 0,
              "T13A clean prose is NOT flagged (no false positive)");
        CHECK(g3_text_has_thought_marker(NULL, 10) == 0, "T13A NULL safe");
        CHECK(g3_text_has_thought_marker(CLEAN, 0) == 0, "T13A len 0 safe");
    }

    /* B: the real record must produce NO preamble — this is the boot-41 defect. */
    {
        g3_candidate_t sel[1] = { { .resp = REAL_THOUGHT,
                                    .resp_len = (uint16_t)(sizeof REAL_THOUGHT - 1) } };
        memset(buf, (int)0xAA, sizeof buf);
        int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
        CHECK(len == 0, "T13B thought record => NO preamble (was: 164 bytes of scratch)");
        CHECK(strstr(buf, "<|channel>") == NULL, "T13B marker never reaches the prompt");
    }

    /* C: enumeration is not a sentence end. Without the list-marker rule the cut lands on "1."
     * and injects a fragment; with it there is no completed sentence, so nothing is injected. */
    {
        static const char ENUM[] =
            "Here's a thinking process that leads to the suggested answer:\n\n1.  Analyze";
        g3_candidate_t sel[1] = { { .resp = ENUM, .resp_len = (uint16_t)(sizeof ENUM - 1) } };
        int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
        CHECK(len == 0, "T13C line-leading \"1.\" is not a sentence end => no preamble");
    }

    /* D: but a genuine sentence ending in a NUMBER still counts — the rule must not over-reach. */
    {
        static const char NUMEND[] = "The image shipped in 2026. It replaced the prior build.";
        g3_candidate_t sel[1] = { { .resp = NUMEND, .resp_len = (uint16_t)(sizeof NUMEND - 1) } };
        int len = g3_build_preamble_answer_only(sel, 1, buf, (int)sizeof buf, G3_R_MAX);
        CHECK(len > 0, "T13D sentence ending in a digit still recalls");
        CHECK(strstr(buf, "shipped in 2026.") != NULL, "T13D that sentence is preserved");
    }
}


/* ---- T14: PER-LANE recall window (workload 120 / control-IN 256) ----------
 *
 * WHY THIS GROUP EXISTS. Measured 2026-07-26 over the REAL control-IN store
 * (@21140000, 62 usable records) using the SHIPPED g3_build_preamble_answer_only
 * -- not a replication:
 *
 *     G3_R_MAX=120 (workload)   13/62 yield   median preamble  19 B
 *     G3_R_MAX=160              19/62 yield   median preamble  80 B
 *     G3_R_MAX=200              20/62 yield   median preamble 108 B
 *     G3_R_MAX_CONTROL_IN=256   22/62 yield   median preamble 129 B
 *
 * The 19-byte median is the finding, not the yield: that string is
 * route_decline_text()'s canned "I don't track that." -- the most recallable
 * content in the store, purely because it is short enough to fit inside a
 * 120-byte window WITH its full stop. Every informative answer was declined.
 *
 * SCOPE, stated so it is not restated as something stronger: 22/62 counts
 * RECORDS, not queries. epi_index_lookup is newest-wins and consults exactly ONE
 * record, so it is an upper bound on the corpus, never a per-query hit rate.
 *
 * Every literal below is the REAL stored answer, byte-for-byte, and every
 * expected value was measured with the shipped C before being written here. */
static const char AO_HDR_MIRROR[] =
    "Notes from a previous answer (use as reference; add new detail, do not repeat):\n";
#define AO_HDRLEN ((int)(sizeof AO_HDR_MIRROR - 1))

/* Returns the CLEANED ANSWER length carried by a preamble (or 0 for "declined").
 * Also validates the mirrored header against the shipped output, so this helper
 * cannot silently drift if the real header text ever changes. */
static int ao_clean(const char *resp, int resp_len, int r_max)
{
    g3_candidate_t c;
    memset(&c, 0, sizeof c);
    c.resp = resp; c.resp_len = (uint16_t)resp_len;
    static char buf[1024];
    int n = g3_build_preamble_answer_only(&c, 1, buf, (int)sizeof buf, r_max);
    if (n <= 0) return 0;
    if (n < AO_HDRLEN || memcmp(buf, AO_HDR_MIRROR, (size_t)AO_HDRLEN) != 0)
        return -1;            /* mirror drifted from the shipped header */
    return n - AO_HDRLEN - 1; /* minus the trailing newline */
}

/* --- the real stored answers (exact bytes, pulled from the box store) --- */
/* [12:00056] "explain paging in one line" -- 160 B, one markdown-bolded sentence.
 * Its trailing "**" is why the closer set in g3_clean_answer_len must include '*'. */
static const char T14_PAGING[] =
    "**Paging is a memory management technique that divides physical memory and logical memory "
    "into fixed-size blocks to allow efficient allocation and protection.**";
/* [14:00059] "what is a page fault?" -- 256 B (at the store cap), first sentence ends at 196. */
static const char T14_PAGEFAULT[] =
    "A **page fault** is a type of interrupt that occurs in a **virtual memory system** when a "
    "program tries to access a page of memory that is **not currently loaded into physical RAM "
    "(main memory)**.\n\nIn simpler terms, it's the operating system's way of handl";
/* [8:00042] the good CPU-cores answer, unrecallable since boot 8 -- first sentence ends at 168. */
static const char T14_CPU[] =
    "The short answer is: **A single-threaded program is fundamentally limited by the speed of "
    "its single execution thread, regardless of how many CPU cores are available.**\n\nHere is a "
    "detailed breakdown of why adding more CPU cores doesn't speed up a";
/* route_decline_text() -- the string the old window recalled best. */
static const char T14_DECLINE[] = "I don't track that.";
/* [4:00010] a real thought-scratch record. */
static const char T14_THOUGHT[] =
    "<|channel>thought\nThinking Process:\n\n1.  **Analyze the Request:** The user is asking for "
    "the \"JARVIS-MINIFLIP-7X reference value,\" based on \"notes from a previous answer.\"";
/* [2:00002]-shape: pre-9d70f7f stop-token junk, and the ONLY sentence end sits
 * immediately before it, so the '.' is followed by '"' then '<' -- not whitespace. */
static const char T14_TURNTAIL_BARE[] =
    "I do not have access to a specific, universally defined \"JARVIS-MINIFLIP-7X reference "
    "value.\"<turn|><turn|><turn|><turn|><eos>";
/* [1:00000]: same junk tail, but an EARLIER sentence qualifies, so the tail costs
 * only the final sentence rather than the whole record. */
static const char T14_TURNTAIL_SAVED[] =
    "Unfortunately, I do not have access to a specific, universally defined \"JARVIS-MINIFLIP-7X "
    "reference value.\" Please provide more context about what this reference value pertains to "
    "so I can try to assist you.<turn|><turn|><turn|><eos>";

static void test_per_lane_window(void)
{
    /* 1. THE WORKLOAD LANE DID NOT MOVE. The real 160-byte paging answer, whose
     *    only sentence ends exactly at 160, still yields NOTHING at 120. This is
     *    the assertion that pins the deployed baseline. */
    CHECK(ao_clean(T14_PAGING, (int)sizeof T14_PAGING - 1, G3_R_MAX) == 0,
          "T14.1 workload window (120) still DECLINES the 160 B paging answer");

    /* 2. The same input, control-IN window: the whole sentence. */
    CHECK(ao_clean(T14_PAGING, (int)sizeof T14_PAGING - 1, G3_R_MAX_CONTROL_IN) == 160,
          "T14.2 control-IN window (256) recalls the paging answer at 160");

    /* 3. The measured corpus cases, at both windows. */
    CHECK(ao_clean(T14_PAGEFAULT, (int)sizeof T14_PAGEFAULT - 1, G3_R_MAX) == 0,
          "T14.3a page-fault answer declined at 120");
    CHECK(ao_clean(T14_PAGEFAULT, (int)sizeof T14_PAGEFAULT - 1, G3_R_MAX_CONTROL_IN) == 196,
          "T14.3b page-fault answer recalls at 196");
    CHECK(ao_clean(T14_CPU, (int)sizeof T14_CPU - 1, G3_R_MAX) == 0,
          "T14.3c CPU-cores answer declined at 120");
    CHECK(ao_clean(T14_CPU, (int)sizeof T14_CPU - 1, G3_R_MAX_CONTROL_IN) == 168,
          "T14.3d CPU-cores answer recalls at 168");

    /* 4. THE CONTROL. The canned decline is short, so widening must not change it.
     *    Identical in both lanes => the change is ADDITIVE, not a rewrite. */
    CHECK(ao_clean(T14_DECLINE, (int)sizeof T14_DECLINE - 1, G3_R_MAX) == 19,
          "T14.4a canned decline still yields 19 at the workload window");
    CHECK(ao_clean(T14_DECLINE, (int)sizeof T14_DECLINE - 1, G3_R_MAX_CONTROL_IN) == 19,
          "T14.4b canned decline unchanged at the control-IN window");

    /* 5. Widening must not open a path around the thought filter. */
    CHECK(ao_clean(T14_THOUGHT, (int)sizeof T14_THOUGHT - 1, G3_R_MAX) == 0,
          "T14.5a thought scratch rejected at 120");
    CHECK(ao_clean(T14_THOUGHT, (int)sizeof T14_THOUGHT - 1, G3_R_MAX_CONTROL_IN) == 0,
          "T14.5b thought scratch STILL rejected at 256 (filter runs before truncation)");

    /* 6. The <turn|> interaction, pinned as a DECISION rather than an accident.
     *    Pre-9d70f7f records carry stop-token junk; a terminator followed by '<'
     *    is not a sentence end. Such records exist in the store today and age out
     *    only over YEARS (4096 slots, human-paced channel), so this is live
     *    behaviour, not history. */
    CHECK(ao_clean(T14_TURNTAIL_BARE, (int)sizeof T14_TURNTAIL_BARE - 1, G3_R_MAX_CONTROL_IN) == 0,
          "T14.6a stop-token tail with no earlier sentence yields 0 even at 256");
    /* ...but an earlier complete sentence rescues the record, costing only the
     * final one. Measured on the real [1:00000]; pinned so the two cases are not
     * confused for each other. */
    CHECK(ao_clean(T14_TURNTAIL_SAVED, (int)sizeof T14_TURNTAIL_SAVED - 1, G3_R_MAX_CONTROL_IN) == 108,
          "T14.6b stop-token tail with an earlier sentence still recalls that sentence (108)");

    /* 7. The per-lane parameter must actually be USED. If g3_build_preamble_answer_only
     *    ignored r_max and read the constant directly, every assertion above that
     *    depends on 256 would fail -- but make it explicit with a value the
     *    constant cannot produce: an intermediate window yields an intermediate
     *    result, so the parameter is demonstrably live rather than incidental. */
    CHECK(ao_clean(T14_CPU, (int)sizeof T14_CPU - 1, 200) == 168,
          "T14.7a r_max is honoured at an arbitrary window (200 -> 168)");
    CHECK(ao_clean(T14_PAGEFAULT, (int)sizeof T14_PAGEFAULT - 1, 150) == 0,
          "T14.7b r_max is honoured at an arbitrary window (150 -> declined)");
    CHECK(ao_clean(T14_PAGING, (int)sizeof T14_PAGING - 1, 0) == 0,
          "T14.7c r_max <= 0 is refused, never treated as unlimited");
}

int main(void)
{
    printf("=== G3 Retrieval Tests (Phase 5 G3/M0 + M2 budget + M3 filter + M6 hygiene) ===\n");
    test_sentence_boundary();
    test_thought_filter();
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
    test_per_lane_window();
    printf("\n== Results: %d PASS, %d FAIL ==\n", pass, fail);
    return fail ? 1 : 0;
}
