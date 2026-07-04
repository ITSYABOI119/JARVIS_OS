/*
 * JARVIS AI-OS — Semantic Distill Unit Tests (Phase 5 Goal #4 / M0 — #7's core)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
 *       phase3/src/ai/test_semantic_distill.c phase3/src/ai/semantic_distill.c \
 *       phase3/src/ai/episodic_store.c phase3/src/ai/decision_cache.c \
 *       -o /tmp/test_semantic_distill
 *
 * Pure logic: sd_distill over synthetic epi_record_t[] — no device, no store.
 * episodic_store.c + decision_cache.c are linked so records can be built with the
 * REAL episodic_fill (proving fact.key FNV-1a parity with the decision cache).
 */

#include "semantic_distill.h"
#include "episodic_store.h"
#include "decision_cache.h"
#include <stdio.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while (0)

#define PASS(name) do { \
    printf("  PASS: %s\n", name); \
    tests_passed++; \
} while (0)

/* Usable INFER/OK record via the REAL episodic_fill (strlen-based — fine for
 * NUL-terminated test strings; T8 builds a non-NUL record by hand). */
static void rec_ok(epi_record_t *r, const char *query, const char *resp, uint32_t t_ms)
{
    episodic_fill(r, t_ms, query, EPI_ACT_INFER, EPI_OUT_OK, 0, resp);
}

/* ================================================================
 * T1: below min_support -> no fact
 * ================================================================ */
static void test_below_threshold(void)
{
    epi_record_t recs[2];
    rec_ok(&recs[0], "what is sel4", "a verified microkernel", 10);
    rec_ok(&recs[1], "what is sel4", "a verified microkernel", 20);

    semantic_fact_t out[4];
    int n = sd_distill(recs, 2, 3, out, 4);
    ASSERT(n == 0, "support 2 < min_support 3 -> 0 facts");
    PASS("T1 below-threshold excluded");
}

/* ================================================================
 * T2: support counted; fact emitted; key parity with the decision cache
 * ================================================================ */
static void test_support_and_key_parity(void)
{
    epi_record_t recs[4];
    /* Normalization = lowercase + whitespace-collapse (punctuation KEPT) — these
     * three are case/whitespace variants of the SAME key. */
    rec_ok(&recs[0], "What is seL4", "a verified microkernel", 10);
    rec_ok(&recs[1], "unrelated question", "unrelated answer", 15);
    rec_ok(&recs[2], "what is sel4",     "a verified microkernel", 20);
    rec_ok(&recs[3], "WHAT   IS   SEL4", "a verified microkernel", 30);

    semantic_fact_t out[4];
    int n = sd_distill(recs, 4, 3, out, 4);
    ASSERT(n == 1, "exactly 1 fact (the 3-support key; the 1-support key excluded)");
    ASSERT(out[0].support_count == 3, "support_count == 3");
    ASSERT(out[0].confidence_x100 == 100, "identical answers -> confidence 100");
    ASSERT(out[0].fact_type == SEM_FACT_QA, "fact_type == SEM_FACT_QA");
    ASSERT(out[0].text_len == strlen("a verified microkernel") &&
           memcmp(out[0].text, "a verified microkernel", out[0].text_len) == 0,
           "fact text == the answer");
    ASSERT(out[0].boot_id == 0 && out[0].seq == 0, "boot_id/seq left 0 (store stamps)");

    /* key parity: the fact's key must equal cache_hash(cache_normalize_query(query)) */
    char norm[MAX_QUERY_LEN];
    ASSERT(cache_normalize_query("What is seL4", norm, sizeof norm),
           "normalize works");
    ASSERT(out[0].key == cache_hash(norm), "fact.key == decision-cache FNV-1a (parity)");
    PASS("T2 support counted + key parity (normalization folds case/whitespace variants)");
}

/* ================================================================
 * T3: newest answer wins; disagreement lowers confidence honestly
 * ================================================================ */
static void test_newest_wins(void)
{
    epi_record_t recs[3];
    rec_ok(&recs[0], "what is sel4", "an OLD answer", 10);
    rec_ok(&recs[1], "what is sel4", "an OLD answer", 20);
    rec_ok(&recs[2], "what is sel4", "the NEWEST answer", 30);

    semantic_fact_t out[4];
    int n = sd_distill(recs, 3, 3, out, 4);
    ASSERT(n == 1, "1 fact emitted");
    ASSERT(out[0].text_len == strlen("the NEWEST answer") &&
           memcmp(out[0].text, "the NEWEST answer", out[0].text_len) == 0,
           "the NEWEST answer is chosen");
    ASSERT(out[0].support_count == 3, "support still counts all usable records");
    ASSERT(out[0].confidence_x100 == 33, "1-of-3 agree with the chosen answer -> 33");
    ASSERT(out[0].t_ms == 30, "fact t_ms == the chosen (newest) record's t_ms");
    PASS("T3 newest consistent answer chosen; disagreement -> honest confidence");
}

/* ================================================================
 * T4: dedup by key — one fact per subject
 * ================================================================ */
static void test_dedup_by_key(void)
{
    epi_record_t recs[6];
    rec_ok(&recs[0], "subject A", "answer A", 10);
    rec_ok(&recs[1], "subject B", "answer B", 11);
    rec_ok(&recs[2], "subject A", "answer A", 12);
    rec_ok(&recs[3], "subject B", "answer B", 13);
    rec_ok(&recs[4], "subject A", "answer A", 14);
    rec_ok(&recs[5], "subject B", "answer B", 15);

    semantic_fact_t out[8];
    int n = sd_distill(recs, 6, 3, out, 8);
    ASSERT(n == 2, "exactly 2 facts (one per key, never a dup)");
    ASSERT(out[0].key != out[1].key, "distinct keys");
    PASS("T4 dedup-by-key (one fact per subject)");
}

/* ================================================================
 * T5: n=0 / NULL guards
 * ================================================================ */
static void test_empty(void)
{
    semantic_fact_t out[2];
    epi_record_t rec;
    rec_ok(&rec, "q", "a", 1);
    ASSERT(sd_distill(NULL, 0, 3, out, 2) == 0, "NULL recs with n=0 -> 0 facts");
    ASSERT(sd_distill(&rec, 0, 3, out, 2) == 0, "n=0 -> 0 facts");
    ASSERT(sd_distill(NULL, 5, 3, out, 2) == -1, "NULL recs with n>0 -> -1");
    ASSERT(sd_distill(&rec, 1, 3, NULL, 2) == -1, "NULL out -> -1");
    ASSERT(sd_distill(&rec, 1, 0, out, 2) == -1, "min_support < 1 -> -1");
    PASS("T5 n=0 / NULL / bad-arg guards");
}

/* ================================================================
 * T6: max-cap — emits at most `max` facts, returns the cap
 * ================================================================ */
static void test_max_cap(void)
{
    epi_record_t recs[6];
    rec_ok(&recs[0], "subject A", "answer A", 10);
    rec_ok(&recs[1], "subject A", "answer A", 11);
    rec_ok(&recs[2], "subject A", "answer A", 12);
    rec_ok(&recs[3], "subject B", "answer B", 13);
    rec_ok(&recs[4], "subject B", "answer B", 14);
    rec_ok(&recs[5], "subject B", "answer B", 15);

    semantic_fact_t out[1];
    int n = sd_distill(recs, 6, 3, out, 1);
    ASSERT(n == 1, "2 qualifying keys but max=1 -> returns 1 (capped)");
    PASS("T6 max-cap honored");
}

/* ================================================================
 * T7: unusable records never count (cache echoes / errors / empty resp)
 * ================================================================ */
static void test_usable_filter(void)
{
    epi_record_t recs[5];
    rec_ok(&recs[0], "what is sel4", "a verified microkernel", 10);
    rec_ok(&recs[1], "what is sel4", "a verified microkernel", 20);
    /* same key, but NOT usable — must not count toward support: */
    episodic_fill(&recs[2], 30, "what is sel4", EPI_ACT_CACHE, EPI_OUT_OK, 0,
                  "ACTION_CANNED_ECHO");                                   /* cache echo */
    episodic_fill(&recs[3], 40, "what is sel4", EPI_ACT_INFER, EPI_OUT_ERROR, 0,
                  "failed answer");                                        /* error outcome */
    episodic_fill(&recs[4], 50, "what is sel4", EPI_ACT_INFER, EPI_OUT_OK, 0,
                  NULL);                                                   /* empty resp */

    semantic_fact_t out[4];
    int n = sd_distill(recs, 5, 3, out, 4);
    ASSERT(n == 0, "2 usable + 3 unusable < min_support 3 -> no fact");
    ASSERT(sd_record_usable(&recs[0]) == 1, "INFER/OK/resp -> usable");
    ASSERT(sd_record_usable(&recs[2]) == 0, "CACHE echo -> unusable");
    ASSERT(sd_record_usable(&recs[3]) == 0, "ERROR outcome -> unusable");
    ASSERT(sd_record_usable(&recs[4]) == 0, "empty resp -> unusable");
    PASS("T7 usable filter (cache echoes / errors / empty resp excluded)");
}

/* ================================================================
 * T8: text copied by resp_len, never strlen (non-NUL-terminated input)
 * ================================================================ */
static void test_length_not_strlen(void)
{
    epi_record_t recs[3];
    for (int i = 0; i < 3; i++) {
        memset(&recs[i], 0xAA, sizeof(recs[i]));   /* poison: NO NUL terminators anywhere */
        recs[i].action   = EPI_ACT_INFER;
        recs[i].outcome  = EPI_OUT_OK;
        recs[i].query_key = 0x1234567890ABCDEFULL;
        recs[i].t_ms     = (uint64_t)(10 * (i + 1));
        memcpy(recs[i].resp, "ABCDE", 5);          /* bytes 5.. stay 0xAA — no NUL */
        recs[i].resp_len = 5;
        recs[i].query_len = 0;
    }

    semantic_fact_t out[2];
    int n = sd_distill(recs, 3, 3, out, 2);
    ASSERT(n == 1, "1 fact from the hand-built records");
    ASSERT(out[0].text_len == 5, "text_len == resp_len (5), not a strlen run into 0xAA");
    ASSERT(memcmp(out[0].text, "ABCDE", 5) == 0, "exactly the 5 resp bytes copied");
    ASSERT(out[0].text[5] == 0, "fact text beyond text_len stays zeroed (no 0xAA bleed)");
    ASSERT(out[0].confidence_x100 == 100, "identical 5-byte answers -> confidence 100");
    PASS("T8 copy by resp_len (non-NUL-terminated input safe)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: Semantic Distill Tests (Phase 5 #4/M0 — #7 core) ===\n\n");
    test_below_threshold();
    test_support_and_key_parity();
    test_newest_wins();
    test_dedup_by_key();
    test_empty();
    test_max_cap();
    test_usable_filter();
    test_length_not_strlen();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
