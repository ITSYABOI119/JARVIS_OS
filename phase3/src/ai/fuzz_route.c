/*
 * fuzz_route.c — ASan/UBSan fuzz target for the control-IN QUERY ROUTER (6-6/B M0).
 *
 * The validated query is untrusted, length-carried bytes. This harness proves
 * route_classify + sysfacts_answer are crash/UB-free for ANY input and STABLE
 * under match-preserving noise, in three RANDOM ~100K-iter modes plus one small
 * DETERMINISTIC directed mode (no accuracy claim — crash/UB-freedom + the
 * result-struct invariant only):
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
 *   (d) DIRECTED         — a fixed table of inputs shaped to REACH classifier
 *                          branches the random modes miss on STATISTICS rather
 *                          than on structure (the write bound; the short-circuit-
 *                          SHADOWED uptime clauses; the "day is" wall-clock
 *                          sibling). Each case runs ONCE — this mode adds CASES,
 *                          never iterations. It asserts the same invariant as (a)
 *                          and deliberately asserts NO expected class; see
 *                          run_directed(). It runs LAST and consumes no RNG, so
 *                          (a)/(b)/(c) draw the identical stream they drew before
 *                          it existed.
 *
 * On ANY assertion failure: print the mode + iter + fixed seed and exit nonzero.
 * On success: "FUZZ OK <n> iters (<random> random + <n> directed cases)".
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

/* ================================================================
 * (d) DIRECTED — a fixed table of inputs aimed at classifier branches the random
 *     modes miss on STATISTICS, not on structure. MEASURED against the coverage
 *     instrument (phase3/scripts/coverage_report.sh), which is how the three
 *     families below were identified.
 *
 *     ASSERTS THE HARNESS CONTRACT ONLY — no crash, plus the same result-struct
 *     invariant mode (a) asserts. It deliberately does NOT pin an expected class:
 *     a routing expectation belongs to test_route.c / routing_suite.h, and
 *     asserting one here would make the FUZZER fail on a legitimate, reviewed
 *     retune of the keyword tables. These cases exist to REACH code, and their
 *     claim is "still crash-free and still structurally consistent".
 *
 *     (i)   LONG ALNUM -> the rt_normalize WRITE BOUND (`o + 1 < cap`), the exact
 *           analogue of qs_normalize's. Mode (a) already feeds up to 256 bytes so
 *           it is structurally able to reach the 191-char bound; random bytes
 *           normalise to ~108 chars (only ~24% of byte values are alphanumeric
 *           and every separator run collapses to one space), so it never arrives.
 *
 *     (ii)  SHADOWED UPTIME CLAUSES -> rt_seq2("since","when") /
 *           rt_seq2("been","up") / rt_seq2("been","running"). These are live and
 *           reachable, but every phrasing in the corpus that would reach them
 *           ALSO contains "how long", and rt_seq2("how","long") sits EARLIER in
 *           the same || chain and short-circuits first. The cases below omit
 *           "how long" so each clause is the one that decides.
 *
 *     (iii) "day is" -> the wall-clock DECLINE sibling. Its "time is" neighbour
 *           on the same line is covered; this one needs a query with no "time"
 *           word, so the earlier disjunct is false and this one decides.
 *
 *     Each case is `fill` repeated `reps` times followed by `tail`, so one table
 *     expresses both short literals and the long runs (i) needs.
 * ================================================================ */
typedef struct {
    const char *fill;   /* repeated `reps` times (may be "") */
    int         reps;
    const char *tail;   /* appended once (may be "")         */
} rt_directed_t;

static const rt_directed_t RT_DIRECTED[] = {
    /* (i) write bound — each normalises to more than RT_NORM_MAX-1 (191) chars. */
    { "x",      240, ""                   },  /* one 240-char word              */
    { "ab ",     80, ""                   },  /* 240 B, ~80 short words         */
    { "Node7 ",  40, ""                   },  /* 280 B, mixed case + digits     */
    { "pad ",    50, "have you been up"   },  /* the clause is pushed PAST the
                                               * bound and truncated away       */
    /* (ii) the short-circuit-shadowed uptime clauses — note NO "how long". */
    { "", 0, "since when are you awake" },
    { "", 0, "tell me since when"       },
    { "", 0, "have you been up"         },
    { "", 0, "have you been running"    },
    { "", 0, "have you been on"      },  /* the LAST uptime clause; see the note below */
    /* (iii) the "day is" wall-clock sibling — note NO "time" word. */
    { "", 0, "what day is it"    },
    { "", 0, "which day is today" },
    { "", 0, "what date is it"   },      /* the "date is" sibling; see the note below */
};

/*
 * A MEASURED PROPERTY OF THE INSTRUMENT, not of this table — read this before
 * concluding that a clause here is "still uncovered".
 *
 * At -O0, gcov attributes each clause of a MULTI-LINE `||` chain to the line
 * BEFORE it. Measured on route.c's UPTIME chain by running these inputs in
 * isolation and counting: four `been running` inputs produced a true-count of
 * exactly 4 on the line holding `been up`, and the line holding `been running`
 * showed outcomes totalling exactly the 5 inputs that fell through to the NEXT
 * clause. The same shift applies to the wall-clock chain.
 *
 * The consequence is a trap: a coverage report naming "route.c:269
 * rt_seq2(been,running)" as uncovered is really naming `been on`, and one
 * naming ":296 (... day is ...)" is really naming `date is`. Aiming at the
 * quoted clause covers a DIFFERENT line and leaves the reported gap open,
 * which looks exactly like the directed case not working. The last case in
 * each family above is the one the report was actually asking for.
 */
#define RT_DIRECTED_N ((long)(sizeof RT_DIRECTED / sizeof RT_DIRECTED[0]))

static int run_directed(long *out_done)
{
    uint8_t buf[512];   /* > the longest case (280 B); the builder is bounded anyway */

    for (long i = 0; i < RT_DIRECTED_N; i++) {
        const rt_directed_t *d = &RT_DIRECTED[i];
        size_t n = 0;
        for (int k = 0; k < d->reps; k++)
            for (const char *p = d->fill; *p && n < sizeof buf; p++)
                buf[n++] = (uint8_t)*p;
        for (const char *p = d->tail; *p && n < sizeof buf; p++)
            buf[n++] = (uint8_t)*p;

        route_result_t r;
        classify_exact(buf, n, &r);     /* exact-len heap: ASan redzones buf[n] */
        FUZZ_ASSERT(invariant_ok(&r), "directed:invariant", i);
    }
    *out_done = RT_DIRECTED_N;
    return 0;
}

int main(void)
{
    long rand_iters = 0, directed = 0, done = 0;

    if (run_raw(100000, &done)) return 1;
    rand_iters += done;

    if (run_structured(100000, &done)) return 1;
    rand_iters += done;

    if (run_sysfacts(100000, &done)) return 1;
    rand_iters += done;

    /* LAST and RNG-free, so the three random modes above draw exactly the stream
     * they drew before this mode existed. */
    if (run_directed(&done)) return 1;
    directed += done;

    printf("FUZZ OK %ld iters (%ld random + %ld directed cases)\n",
           rand_iters + directed, rand_iters, directed);
    return 0;
}
