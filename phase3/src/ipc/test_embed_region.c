/*
 * test_embed_region.c — Phase C / C/M1b-3 host test for the embed-region staleness predicate.
 *
 * Host-only, no seL4, no device. Covers the eight cases the C/M1b-3 plan requires, plus a teeth
 * check that the stale-seq rule is actually load-bearing.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "embed_region.h"

static int pass = 0, fail = 0;

static void check(int cond, const char *what)
{
    if (cond) { pass++; printf("  PASS: %s\n", what); }
    else      { fail++; printf("  FAIL: %s\n", what); }
}

/* A header that SHOULD be usable; each test perturbs exactly one field from this baseline, so a
 * failure names the field responsible rather than "something in the struct". */
static embed_ctrl_t good(uint32_t seq)
{
    embed_ctrl_t c;
    c.magic  = EMBED_REGION_MAGIC;
    c.seq    = seq;
    c.status = EMBED_STATUS_OK;
    c.len    = EMBED_VEC_BYTES;
    c.ready  = 1u;
    return c;
}

int main(void)
{
    printf("=== C/M1b-3: embed-region staleness predicate ===\n");

    /* T1 — fresh result for the request we made. */
    {
        embed_ctrl_t c = good(42u);
        check(embed_result_usable(&c, 42u, EMBED_VEC_BYTES) == 1,
              "T1 fresh: ready + magic + seq match + len match + status OK -> USABLE");
    }

    /* T2 — not ready. The producer has not published yet; every other field is indeterminate. */
    {
        embed_ctrl_t c = good(42u);
        c.ready = 0u;
        check(embed_result_usable(&c, 42u, EMBED_VEC_BYTES) == 0,
              "T2 ready==0 -> NOT usable (fields are indeterminate until the release store)");
    }

    /* T3 — THE CASE THIS MODULE EXISTS FOR: ready, well-formed, but answering the PREVIOUS request.
     * Without this check PA returns the previous question's embedding: a valid unit vector of the
     * wrong text, which no cosine or norm check can detect. */
    {
        embed_ctrl_t c = good(41u);          /* the previous request */
        check(embed_result_usable(&c, 42u, EMBED_VEC_BYTES) == 0,
              "T3 STALE: ready but seq is the PREVIOUS request -> NOT usable (the silent-failure case)");
    }

    /* T4 — length mismatch. A truncated vector still scores cosine ~1.0, so length is checked
     * explicitly rather than inferred from status. */
    {
        embed_ctrl_t c = good(42u);
        c.len = EMBED_VEC_BYTES - 4u;        /* one float short */
        check(embed_result_usable(&c, 42u, EMBED_VEC_BYTES) == 0,
              "T4 len one float SHORT -> NOT usable (a truncated vector still scores cosine ~1.0)");
        c.len = EMBED_VEC_BYTES + 4u;
        check(embed_result_usable(&c, 42u, EMBED_VEC_BYTES) == 0,
              "T4b len one float LONG -> NOT usable");
    }

    /* T5 — producer reported an error. */
    {
        embed_ctrl_t c = good(42u);
        c.status = EMBED_STATUS_ERR;
        check(embed_result_usable(&c, 42u, EMBED_VEC_BYTES) == 0,
              "T5 status ERR -> NOT usable");
    }

    /* T6 — wrong magic. An unmapped/garbage page must never pass as a legitimate result. */
    {
        embed_ctrl_t c = good(42u);
        c.magic = 0xDEADBEEFu;
        check(embed_result_usable(&c, 42u, EMBED_VEC_BYTES) == 0,
              "T6 wrong magic -> NOT usable");
        /* An all-zero page is the realistic form of this: never-written shared memory. */
        embed_ctrl_t z;
        memset(&z, 0, sizeof z);
        check(embed_result_usable(&z, 0u, EMBED_VEC_BYTES) == 0,
              "T6b all-ZERO page (never written) -> NOT usable even when expect_seq is 0");
    }

    /* T7 — the uint32 wrap. At UINT32_MAX -> 0 the NEWEST seq is numerically the SMALLEST, so any
     * >=/> comparison inverts exactly here. Equality has no wrap behaviour to get wrong. */
    {
        embed_ctrl_t c = good(UINT32_MAX);
        check(embed_result_usable(&c, UINT32_MAX, EMBED_VEC_BYTES) == 1,
              "T7 seq == UINT32_MAX matches itself -> USABLE");

        embed_ctrl_t w = good(0u);           /* wrapped: this is the NEWER request */
        check(embed_result_usable(&w, 0u, EMBED_VEC_BYTES) == 1,
              "T7b seq wrapped to 0 matches expect 0 -> USABLE (ordering would call this older)");
        check(embed_result_usable(&w, UINT32_MAX, EMBED_VEC_BYTES) == 0,
              "T7c wrapped result vs the PRE-wrap expect -> NOT usable");

        embed_ctrl_t s = good(UINT32_MAX);   /* stale across the wrap */
        check(embed_result_usable(&s, 0u, EMBED_VEC_BYTES) == 0,
              "T7d PRE-wrap result vs post-wrap expect -> NOT usable (an ordering test would PASS this)");
    }

    /* T8 — NULL guard: a caller that has not mapped the region gets 0, not a fault. */
    check(embed_result_usable(NULL, 42u, EMBED_VEC_BYTES) == 0,
          "T8 NULL ctrl -> NOT usable, no crash");

    /* TEETH — prove the stale-seq rule is load-bearing rather than incidentally satisfied. If the
     * seq check were deleted, T3/T7c/T7d would all still be "ready + OK + right len", so this
     * asserts the ONLY difference between a usable and an unusable header here is the seq. */
    {
        embed_ctrl_t a = good(100u);
        check(embed_result_usable(&a, 100u, EMBED_VEC_BYTES) == 1 &&
              embed_result_usable(&a,  99u, EMBED_VEC_BYTES) == 0 &&
              embed_result_usable(&a, 101u, EMBED_VEC_BYTES) == 0,
              "TEETH: identical header, ONLY expect_seq varies -> seq alone decides usability");
    }

    /* The page invariant is compile-time (_Static_assert in the header); assert the derived value
     * at runtime too so the test output records what was actually built. */
    check(EMBED_VEC_BYTES == 4096u, "page invariant: 1024 floats x 4 B == exactly one 4096 B page");

    printf("\n== Results: %d PASS, %d FAIL ==\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
