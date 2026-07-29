/*
 * embed_region.c — Phase C / C/M1b-3. The staleness predicate; see embed_region.h for why this is
 * a separate host-testable module rather than a few lines inside main_x86.c.
 */
#include "embed_region.h"

int embed_result_usable(const embed_ctrl_t *c, uint32_t expect_seq, uint32_t expect_len)
{
    /* NULL guard first: a caller that has not yet mapped the region must get 0, not a fault. */
    if (!c) return 0;

    /* Identity before anything else — an unmapped or zeroed page reads as all-zero, and a zeroed
     * page has ready==0 AND magic==0. Checking magic means a garbage page can never be mistaken for
     * a legitimate not-ready one, which keeps the two failure modes distinguishable to the caller. */
    if (c->magic != EMBED_REGION_MAGIC) return 0;

    /* The producer publishes this LAST with a release store. Until it is set, every other field in
     * this header is in an indeterminate state and must not be trusted. */
    if (c->ready == 0u) return 0;

    if (c->status != EMBED_STATUS_OK) return 0;

    /* THE STALENESS CHECK, and the reason this module exists.
     *
     * EQUALITY, never ordering. At the uint32 wrap the newest seq is numerically the smallest, so
     * any >= / > comparison silently inverts exactly once every 2^32 requests. Equality has no wrap
     * behaviour to get wrong.
     *
     * Without this, a ready-but-stale page hands the caller the PREVIOUS request's embedding — a
     * perfectly well-formed unit vector of the wrong text, which a cosine or norm check cannot
     * detect. */
    if (c->seq != expect_seq) return 0;

    /* A short/long vector is not a usable one. Checked explicitly rather than assumed from status,
     * because a truncated transport is the other silent-failure mode here: a vector missing its
     * last few floats still scores cosine ~1.0 against the right answer. */
    if (c->len != expect_len) return 0;

    return 1;
}
