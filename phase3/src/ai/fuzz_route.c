/*
 * fuzz_route.c — ASan/UBSan fuzz target for the control-IN QUERY ROUTER (6-6/B M0).
 *
 * The validated query is untrusted, length-carried bytes. This harness proves
 * route_classify + sysfacts_answer are crash/UB-free for ANY input and STABLE
 * under match-preserving noise, in three ~100K-iter modes (no accuracy claim —
 * crash/UB-freedom + the result-struct invariant only):
 *
 *   (a) RAW              — random length 0..256 + random bytes into an EXACT-len
 *                          heap buffer (ASan redzone at data[len] — the M1
 *                          len==14 lesson: a fixed oversized buffer would hide a
 *                          read-past-len). Asserts no crash + the invariant
 *                          (INFER <=> field==SF_NONE & pattern==-1; SYSFACTS <=>
 *                          field in (0,SF__COUNT) & pattern>=0; DECLINE <=>
 *                          field==SF_NONE & pattern>=0).
 *
 *   (b) STRUCTURED       — mutate each suite entry with MATCH-PRESERVING
 *                          mutations ONLY (random per-letter case-fold, expand
 *                          each separator into a 1..3-byte non-alnum run,
 *                          surrounding non-printable). These leave the normalized
 *                          alnum word sequence identical, so the classification
 *                          MUST equal route.c's OWN verdict on the un-mutated
 *                          query (STABILITY — measured against route.c, not the
 *                          suite label, so a base-misroute doesn't false-fail).
 *
 *   (c) SYSFACTS         — random field (incl. out-of-range) + random facts into
 *                          a tiny EXACT-len out buffer (ASan redzone at out[cap]).
 *                          Asserts bounded (return < cap), NUL-terminated within
 *                          cap, no crash — for ANY cap 0..19.
 *
 * On ANY assertion failure: print the mode + iter + fixed seed and exit nonzero.
 * On success: "FUZZ OK <iters> iters".
 *
 * Build/run (ASan/UBSan, -O1):
 *   gcc -Wall -Werror -O1 -g -std=c11 -fsanitize=address,undefined -I phase3/src/ai \
 *       phase3/src/ai/fuzz_route.c phase3/src/ai/route.c -o /tmp/fuzz_route && /tmp/fuzz_route
 *
 * Host-pure (fuzz_query_shield.c / km2b_miss.c precedent); the DEPLOYED router is
 * malloc-free — this DRIVER allocates each input in an EXACT-len heap buffer so
 * ASan's redzone catches any read/write past the logical length.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "route.h"
#include "routing_suite.h"

/* ---- fixed seed (fuzz_query_shield.c / fuzz_control_in.c pattern) ---- */
#define FUZZ_SEED 0x524F5554455230ULL   /* "ROUTER0" */
static uint64_t rng_state = FUZZ_SEED;

static uint64_t xorshift64(void)
{
    uint64_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rng_state = x;
    return x;
}

static uint32_t rand_range(uint32_t max) /* [0, max) */
{
    if (max == 0) return 0;
    return (uint32_t)(xorshift64() % (uint64_t)max);
}

#define FUZZ_ASSERT(cond, mode, iter)                                          \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("\nFUZZ FAIL [%s] iter=%lld seed=0x%016llx cond=(%s)\n",     \
                   (mode), (long long)(iter),                                  \
                   (unsigned long long)FUZZ_SEED, #cond);                      \
            return 1;                                                          \
        }                                                                      \
    } while (0)

static int is_alnum_byte(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* The result-struct invariant (mirrors test_route.c). */
static int invariant_ok(const route_result_t *r)
{
    if (r->cls != ROUTE_INFER && r->cls != ROUTE_SYSFACTS && r->cls != ROUTE_DECLINE)
        return 0;
    if (r->cls == ROUTE_INFER)
        return r->field == SF_NONE && r->matched_pattern == -1;
    if (r->cls == ROUTE_SYSFACTS)
        return r->field > SF_NONE && r->field < SF__COUNT && r->matched_pattern >= 0;
    return r->field == SF_NONE && r->matched_pattern >= 0;   /* DECLINE */
}

/* Classify `len` bytes from an EXACT-len heap buffer so ASan redzones data[len]. */
static route_class_t classify_exact(const uint8_t *data, size_t len, route_result_t *out)
{
    uint8_t *buf = (uint8_t *)malloc(len ? len : 1u);
    if (!buf) return route_classify((const char *)data, len, out);
    if (len) memcpy(buf, data, len);
    route_class_t v = route_classify((const char *)buf, len, out);
    free(buf);
    return v;
}

/* Match-preserving mutation of a NUL-terminated corpus string into `out` (cap
 * bytes). Random per-letter case-fold; every source separator expands to a
 * 1..3-byte run of random NON-alnum separators; surrounding non-printable. The
 * normalized alnum word sequence is invariant, so the classification is
 * preserved. Returns the mutated length. */
static size_t mutate_preserving(const char *src, uint8_t *out, size_t cap)
{
    static const uint8_t SEP[] = {
        ' ', '\t', '.', ',', '!', ';', ':', '?', 0x01, 0x1F, 0x7F, 0xFF
    };
    const uint32_t NSEP = (uint32_t)(sizeof SEP / sizeof SEP[0]);
    size_t o = 0;

    int lead = (int)rand_range(7);
    for (int i = 0; i < lead && o + 1 < cap; i++)
        out[o++] = SEP[rand_range(NSEP)];

    for (size_t i = 0; src[i] && o + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (is_alnum_byte(c)) {
            if (c >= 'a' && c <= 'z') { if (rand_range(2)) c = (unsigned char)(c - 32); }
            else if (c >= 'A' && c <= 'Z') { if (rand_range(2)) c = (unsigned char)(c + 32); }
            out[o++] = c;                    /* alnum: preserved (case only) */
        } else {
            int run = 1 + (int)rand_range(3); /* separator: expand to 1..3 non-alnum */
            for (int k = 0; k < run && o + 1 < cap; k++)
                out[o++] = SEP[rand_range(NSEP)];
        }
    }

    int trail = (int)rand_range(7);
    for (int i = 0; i < trail && o + 1 < cap; i++)
        out[o++] = SEP[rand_range(NSEP)];

    return o;
}

/* ================================================================
 * (a) RAW — never crash / never OOB for ANY input.
 * ================================================================ */
static int run_raw(long iters, long *out_done)
{
    for (long i = 0; i < iters; i++) {
        size_t len = (size_t)rand_range(257); /* 0..256 */
        uint8_t *buf = (uint8_t *)malloc(len ? len : 1u);
        FUZZ_ASSERT(buf != NULL, "raw:oom", i);
        for (size_t j = 0; j < len; j++)
            buf[j] = (uint8_t)(xorshift64() & 0xFF);

        route_result_t r;
        route_classify((const char *)buf, len, &r);
        free(buf);
        FUZZ_ASSERT(invariant_ok(&r), "raw:invariant", i);
    }
    *out_done = iters;
    return 0;
}

/* ================================================================
 * (b) STRUCTURED — mutated suite entry classifies the SAME as the base.
 * ================================================================ */
static int run_structured(long iters, long *out_done)
{
    uint8_t mut[512];
    const size_t combined = ROUTING_DEV_N + ROUTING_HELDOUT_N;

    for (long i = 0; i < iters; i++) {
        size_t idx = (size_t)rand_range((uint32_t)combined);
        const char *q = (idx < ROUTING_DEV_N)
                        ? ROUTING_DEV[idx].query
                        : ROUTING_HELDOUT[idx - ROUTING_DEV_N].query;

        route_result_t base;
        route_classify(q, strlen(q), &base);

        size_t mlen = mutate_preserving(q, mut, sizeof mut);
        route_result_t r;
        route_class_t v = classify_exact(mut, mlen, &r);

        FUZZ_ASSERT(invariant_ok(&r), "structured:invariant", i);
        FUZZ_ASSERT(v == base.cls, "structured:cls-stable", i);
        FUZZ_ASSERT(r.field == base.field, "structured:field-stable", i);
    }
    *out_done = iters;
    return 0;
}

/* ================================================================
 * (c) SYSFACTS — bounded + NUL-terminated for any field / tiny cap.
 * ================================================================ */
static int run_sysfacts(long iters, long *out_done)
{
    for (long i = 0; i < iters; i++) {
        sysfact_field_t field = (sysfact_field_t)rand_range(8); /* 0..7, incl out-of-range 6/7 */

        sysfacts_t f;
        for (size_t j = 0; j < sizeof f; j++)
            ((uint8_t *)&f)[j] = (uint8_t)(xorshift64() & 0xFF); /* incl non-NUL-terminated model_name */

        size_t cap = (size_t)rand_range(20);   /* 0..19, exercises cap==0 + tiny */
        char *out = (char *)malloc(cap ? cap : 1u);
        FUZZ_ASSERT(out != NULL, "sysfacts:oom", i);

        int n = sysfacts_answer(field, &f, out, cap);

        if (cap == 0) {
            FUZZ_ASSERT(n == 0, "sysfacts:cap0-returns-0", i);
        } else {
            FUZZ_ASSERT(n >= 0 && (size_t)n < cap, "sysfacts:bounded", i);
            FUZZ_ASSERT(out[n] == '\0', "sysfacts:nul-terminated", i);
            FUZZ_ASSERT(strlen(out) == (size_t)n, "sysfacts:len-matches", i);
        }
        free(out);
    }
    *out_done = iters;
    return 0;
}

int main(void)
{
    long total = 0, done = 0;

    if (run_raw(100000, &done)) return 1;
    total += done;

    if (run_structured(100000, &done)) return 1;
    total += done;

    if (run_sysfacts(100000, &done)) return 1;
    total += done;

    printf("FUZZ OK %ld iters\n", total);
    return 0;
}
