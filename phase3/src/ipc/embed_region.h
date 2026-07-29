/*
 * embed_region.h — Phase C / C/M1b-3: the PA<->PB embedding transport region.
 *
 * WHY THIS IS ITS OWN HOST-TESTABLE MODULE. The vector does NOT travel through the response ring:
 * a 1024-float embedding is 4096 B = 18 chunks into a 15-slot, 240 B-payload ring, which is
 * impossible regardless of how well the chunk loop handles a full ring. So it travels through a
 * DEDICATED 2-page shared region, and the ring carries only a small typed request/completion.
 *
 * That leaves exactly one place a subtle bug can hide — deciding whether the bytes sitting in the
 * shared page belong to the request you just made, or to the previous one. That decision is pure
 * logic with no seL4 dependency, so it lives here and is host-tested, following the unbroken
 * precedent of km2b_miss / km2b_trigger / wake / control_floor / control_console: main_x86.c and
 * inference_server.c are seL4-only and are NEVER host-compiled, so anything left in them cannot be
 * tested at all.
 *
 * *** THE CASE THIS EXISTS FOR: a STALE-but-READY page. *** If PA accepts a result whose seq
 * belongs to the PREVIOUS request, it silently returns the previous question's embedding. A cosine
 * check would NOT notice — the vector is a perfectly well-formed unit vector, just of the wrong
 * text. Same silent-failure class the C/M1a parity harness exists to catch, arriving by a different
 * door. Hence embed_result_usable() requires the seq to MATCH, and hence test case 3.
 *
 * PUBLICATION ORDER (the SCTX pool / control-IN mailbox discipline, x86-TSO):
 *   producer (PB): write vector page -> write ctrl fields -> RELEASE-store `ready` LAST
 *   consumer (PA): ACQUIRE-load `ready` FIRST -> then read ctrl fields + vector
 * Publishing `ready` last is what makes the rest of the header safe to read at all; this module
 * only decides USABILITY, it does not perform the loads (the caller owns the atomics, exactly as
 * shared_context.c does).
 */
#ifndef JARVIS_EMBED_REGION_H
#define JARVIS_EMBED_REGION_H

#include <stdint.h>

/* 'J','E','M','B' read in wire order — same convention as CONTROL_MAGIC ('JCTL') / 'JEPI' / 'JACT'. */
#define EMBED_REGION_MAGIC   0x4A454D42u

/* Embedding dimension of the deployed embedder (Qwen3-Embedding-0.6B, golden_meta.json: dim 1024). */
#define EMBED_DIM            1024u
#define EMBED_VEC_BYTES      (EMBED_DIM * 4u)     /* float32 */

/* Status codes carried in the control header. */
#define EMBED_STATUS_OK      0u
#define EMBED_STATUS_ERR     1u   /* PB could not produce a vector (not ready / forward failed) */

/*
 * THE PAGE INVARIANT, PINNED SO A DIMENSION CHANGE BREAKS THE BUILD.
 *
 * 1024 floats x 4 B is EXACTLY one 4096 B page with zero bytes spare, which is the entire reason
 * the control header gets a page of its own instead of being squeezed in beside the vector.
 *
 * The dimension is NOT permanent: the Phase C plan explicitly contemplates Matryoshka truncation
 * to 128. A future change must fail loudly HERE rather than silently corrupt a page boundary — the
 * vector would run into the next page, or the header would be overwritten, and the first symptom
 * would be a plausible-looking wrong vector. That is precisely the failure this module exists to
 * make impossible.
 */
_Static_assert(EMBED_VEC_BYTES == 4096u,
    "embed vector must be EXACTLY one 4096 B page: 1024 floats x 4 B. If the embedding dimension "
    "changed (e.g. Matryoshka 128), the 2-page region layout must be re-derived - do not just widen "
    "this assert.");

/*
 * Control header — page 0 of the region. PACKED so PA and PB agree on the layout by construction
 * (both are built from this tree, but the region is raw shared memory, not a struct passed by the
 * compiler). `ready` is deliberately the LAST field written by the producer, never the first.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;    /* EMBED_REGION_MAGIC — region identity */
    uint32_t seq;      /* the request this result answers (EQUALITY only — see below) */
    uint32_t status;   /* EMBED_STATUS_* */
    uint32_t len;      /* bytes valid in the vector page */
    uint32_t ready;    /* release-stored LAST by PB; acquire-loaded FIRST by PA */
} embed_ctrl_t;

_Static_assert(sizeof(embed_ctrl_t) == 20u, "embed_ctrl_t layout is shared memory - do not reorder");

/*
 * Is the result currently in the region USABLE as the answer to request `expect_seq`?
 *
 * Returns 1 only if ALL hold: c is non-NULL, magic matches, ready is set, status is OK, seq EQUALS
 * expect_seq, and len EQUALS expect_len. Anything else returns 0 — the caller then keeps waiting
 * or times out. There is deliberately no "partially usable".
 *
 * *** SEQ IS COMPARED BY EQUALITY, NEVER BY ORDERING. *** A `>=`/`>` comparison would be wrong at
 * the uint32 wrap (UINT32_MAX -> 0), where the newest seq is numerically the SMALLEST. Equality has
 * no wrap behaviour to get wrong, and "is this the request I asked for" is genuinely an equality
 * question, not an ordering one. Test case 7 pins this.
 *
 * Pure: touches no globals, performs no atomics, has no seL4 dependency. The caller is responsible
 * for having acquire-loaded `ready` before the other fields are meaningful.
 */
int embed_result_usable(const embed_ctrl_t *c, uint32_t expect_seq, uint32_t expect_len);

#endif /* JARVIS_EMBED_REGION_H */
