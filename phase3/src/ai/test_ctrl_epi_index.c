/*
 * Phase 6 A9/1 — host test for control-IN recall-index maintenance (ctrl_epi_index.h).
 *
 * This logic lives in main_x86.c today, which is seL4-only and is NEVER host-compiled, so it has
 * no test at all. Extracting it is behaviour-neutral; this suite is what makes the extraction
 * worth doing. Two groups carry the weight:
 *
 *   - T2 SHADOWING. epi_index_lookup is newest-wins, returns a SINGLE index, and has NO fallback
 *     to the next-newest same-key entry. So an entry the index accepts but the caller's fetch
 *     filter rejects does not degrade to "try the previous one" — it permanently SHADOWS the good
 *     prior answer under that key. This is the defect the module exists to make structural, and it
 *     was a LIVE one: the write path's predicate once omitted `action`, which was harmless only
 *     while every record in this store was tag 3.
 *
 *   - T5 POST-WRAP DRIFT. A logical index is a position, not a handle: once the region rolls,
 *     epi_store_read maps logical L to slot (cursor + L) % max and every existing entry silently
 *     starts pointing at a different record. T5 measures what the index does and does NOT promise,
 *     and pins that the CALLER's `query_key == ckey` re-check is what keeps drift safe.
 *
 * Build: gcc -Wall -Werror -O1 -g -fsanitize=address,undefined -std=c11 -I. \
 *          test_ctrl_epi_index.c ctrl_epi_index.c episodic_store.c decision_cache.c -o t && ./t
 */

#include "ctrl_epi_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  PASS  %s\n", name); } \
    else      { g_fail++; printf("  FAIL  %s\n", name); } \
} while (0)

/* ---------------------------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------------------------- */

/* Build a record directly rather than through episodic_fill: these tests need CONTROLLED keys
 * (T2 and T5 depend on exactly which records share one), and deriving them from query text would
 * make the interesting cases depend on FNV-1a values instead of stating them. */
static void mk_rec(epi_record_t *r, uint64_t key, uint16_t action, uint8_t outcome,
                   uint16_t resp_len, const char *tag)
{
    memset(r, 0, sizeof(*r));
    r->query_key = key;
    r->action    = action;
    r->outcome   = outcome;
    r->resp_len  = resp_len;
    if (tag) {
        size_t n = strlen(tag);
        if (n > sizeof(r->resp)) n = sizeof(r->resp);
        memcpy(r->resp, tag, n);
    }
}

/* A usable control-IN turn: the only shape that may enter the index. */
static void mk_good(epi_record_t *r, uint64_t key, const char *tag)
{
    mk_rec(r, key, EPI_ACT_CONTROL_IN, EPI_OUT_OK, 42, tag);
}

/*
 * Mock store, mirroring episodic_store.c's oldest->newest mapping EXACTLY (its lines 119-127):
 * not rolled, logical i == slot i; rolled, slot = (cursor + i) % max. Reproduced rather than
 * called because T5 needs to drive the store into a rolled state deterministically, and the real
 * store would need its NVMe callbacks and a header round-trip to get there.
 */
#define MOCK_MAX 4u
typedef struct {
    epi_record_t slot[MOCK_MAX];
    uint32_t     cursor;
    uint32_t     total;
} mock_store_t;

static uint32_t mock_count(const mock_store_t *m)
{
    return m->total < MOCK_MAX ? m->total : MOCK_MAX;
}

static void mock_append(mock_store_t *m, const epi_record_t *r)
{
    m->slot[m->cursor] = *r;
    m->slot[m->cursor].seq = m->total;   /* epi_store_append stamps seq from total_entries */
    m->cursor = (m->cursor + 1u) % MOCK_MAX;
    m->total++;
}

static int mock_read(void *ctx, uint32_t li, epi_record_t *out)
{
    mock_store_t *m = (mock_store_t *)ctx;
    if (li >= mock_count(m))
        return -1;
    uint32_t slot = (m->total < MOCK_MAX) ? li : (m->cursor + li) % MOCK_MAX;
    *out = m->slot[slot];
    return 0;
}

/* A flat array reader for T4, with an optional index whose read FAILS. */
typedef struct {
    const epi_record_t *recs;
    uint32_t            n;
    uint32_t            fail_at;   /* 0xFFFFFFFF = never fail */
} flat_ctx_t;

static int flat_read(void *ctx, uint32_t li, epi_record_t *out)
{
    flat_ctx_t *c = (flat_ctx_t *)ctx;
    if (li >= c->n || li == c->fail_at)
        return -1;
    *out = c->recs[li];
    return 0;
}

/* Reproduce today's write-path call shape in one place, so every test exercises the same
 * sequence the deployed caller will: append, then offer {key, count-1} to the index.
 * NOTE the `cnt > 0` term — the underflow guard the header says MUST stay at the call site. */
static int store_and_index(mock_store_t *m, ctrl_epi_index_t *ix, const epi_record_t *r)
{
    mock_append(m, r);
    uint32_t cnt = mock_count(m);
    if (cnt == 0u)
        return 0;
    return ctrl_index_add(ix, r, cnt - 1u);
}

int main(void)
{
    /* UNBUFFERED, deliberately. This suite is built with ASan and T3 exists precisely to make a
     * bounds violation abort — and when stdout is piped it becomes fully buffered, so an abort
     * discards every result line printed up to that point. That turns "which assertion caught the
     * mutant?" into an unanswerable question at exactly the moment it is being asked. Measured:
     * without this, a bounds-check mutant produced an ASan report and NOT ONE PASS/FAIL line. */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("=== test_ctrl_epi_index (Phase 6 A9/1 — control-IN index maintenance) ===\n");

    epi_index_entry_t store[64];
    ctrl_epi_index_t  ix;
    epi_record_t      rec, scratch;

    /* ---------------------------------------------------------------------------------
     * T1: ADD TRUTH TABLE. Exactly today's predicate — a usable control-IN turn is
     * indexable; a failed one, an empty-response one, and a locally-served tag-4 one are
     * not. The tag-4 case is the one that was a live defect: g3_candidate_usable keys on
     * `action`, and the write path's hand-written restatement did not.
     * --------------------------------------------------------------------------------- */
    ctrl_index_init(&ix, store, 64);
    CHECK(ix.n == 0 && ix.cap == 64 && ix.ent == store, "T1a init: empty, cap and storage set");

    mk_good(&rec, 0x1111u, "answered");
    CHECK(ctrl_index_add(&ix, &rec, 7u) == 1, "T1b usable control-IN turn -> 1 (added)");
    CHECK(ix.n == 1 && ix.ent[0].key == 0x1111u && ix.ent[0].logical_index == 7u,
          "T1c added entry carries the record's key and the given logical index");

    mk_rec(&rec, 0x2222u, EPI_ACT_CONTROL_IN, EPI_OUT_ERROR, 42u, "failed");
    CHECK(ctrl_index_add(&ix, &rec, 8u) == 0, "T1d failed turn (EPI_OUT_ERROR) -> 0 (not recallable)");

    mk_rec(&rec, 0x3333u, EPI_ACT_CONTROL_IN, EPI_OUT_OK, 0u, NULL);
    CHECK(ctrl_index_add(&ix, &rec, 9u) == 0, "T1e empty response (resp_len 0) -> 0 (not recallable)");

    mk_rec(&rec, 0x4444u, EPI_ACT_CONTROL_IN_LOCAL, EPI_OUT_OK, 42u, "up 476 seconds");
    CHECK(ctrl_index_add(&ix, &rec, 10u) == 0,
          "T1f locally-served tag-4 turn -> 0 (PA rendered it; not a memory)");

    mk_rec(&rec, 0x5555u, EPI_ACT_INFER, EPI_OUT_OK, 42u, "workload answer");
    CHECK(ctrl_index_add(&ix, &rec, 11u) == 0,
          "T1g a workload EPI_ACT_INFER record -> 0 (wrong store's tag)");

    mk_rec(&rec, 0x6666u, EPI_ACT_CONTROL_IN, EPI_OUT_BLOCKED, 42u, "blocked");
    CHECK(ctrl_index_add(&ix, &rec, 12u) == 0, "T1h blocked turn -> 0 (not recallable)");

    CHECK(ix.n == 1, "T1i none of the unusable records touched the index");

    /* Order is part of the contract: ctrl_sem_gather walks the array backwards for
     * newest-first, so adds must append. */
    mk_good(&rec, 0x7777u, "second");
    CHECK(ctrl_index_add(&ix, &rec, 13u) == 1 && ix.n == 2
          && ix.ent[1].key == 0x7777u && ix.ent[1].logical_index == 13u,
          "T1j a second add APPENDS (order is the ctrl_sem_gather contract)");

    /* NULL guards return 0, not -1: the caller derives `idxable` from (rc != 0), so an
     * invalid handle must never read as "recallable but full". */
    CHECK(ctrl_index_add(NULL, &rec, 0u) == 0, "T1k NULL index handle -> 0 (fail-safe, not -1)");
    CHECK(ctrl_index_add(&ix, NULL, 0u) == 0, "T1l NULL record -> 0 (fail-safe)");
    CHECK(ctrl_index_lookup(NULL, 0x1111u) == -1, "T1m lookup on a NULL handle -> -1");
    ctrl_index_reset(NULL);            /* must not crash */
    ctrl_index_init(NULL, store, 8);   /* must not crash */
    CHECK(ix.n == 2, "T1n NULL-arg calls changed nothing");

    /* A handle with no storage has no capacity, whatever cap was claimed. */
    ctrl_epi_index_t nullix;
    ctrl_index_init(&nullix, NULL, 64);
    mk_good(&rec, 0x8888u, "x");
    CHECK(nullix.cap == 0 && ctrl_index_add(&nullix, &rec, 0u) == -1,
          "T1o NULL storage forces cap 0 -> recallable record reports -1, no write");

    ctrl_index_reset(&ix);
    CHECK(ix.n == 0 && ix.cap == 64 && ix.ent == store,
          "T1p reset empties the index but keeps storage and cap");

    /* ---------------------------------------------------------------------------------
     * T2: THE SHADOWING INVARIANT — the defect this module exists for.
     *
     * lookup is newest-wins with NO fallback, so an unusable entry indexed under an
     * existing key would not merely be skipped, it would HIDE the good answer under that
     * key forever. Each unusable record below is given a HIGHER logical index than the
     * good one on purpose: if the predicate were dropped, it would win the lookup.
     * Cousin of test_episodic_store.c T10, pinned here at the add path.
     * --------------------------------------------------------------------------------- */
    ctrl_index_init(&ix, store, 64);
    const uint64_t K = 0xABCDEF0123456789ull;

    mk_good(&rec, K, "the good prior answer");
    (void)ctrl_index_add(&ix, &rec, 3u);

    mk_rec(&rec, K, EPI_ACT_CONTROL_IN, EPI_OUT_ERROR, 0u, NULL);   /* a timeout/degraded turn */
    CHECK(ctrl_index_add(&ix, &rec, 5u) == 0, "T2a a later FAILED turn under the same key is refused");
    CHECK(ctrl_index_lookup(&ix, K) == 3, "T2b the good answer is still what K resolves to");

    mk_rec(&rec, K, EPI_ACT_CONTROL_IN_LOCAL, EPI_OUT_OK, 42u, "up 12 seconds");
    CHECK(ctrl_index_add(&ix, &rec, 6u) == 0, "T2c a later tag-4 LOCAL turn under the same key is refused");
    CHECK(ctrl_index_lookup(&ix, K) == 3, "T2d K still resolves to the good answer, not the box fact");
    CHECK(ix.n == 1, "T2e neither unusable turn entered the index");

    /* Newest-wins among GOOD entries. This is the half a first-match-wins implementation
     * breaks: a second real answer to the same question must supersede the first. */
    mk_good(&rec, K, "a newer good answer");
    CHECK(ctrl_index_add(&ix, &rec, 9u) == 1, "T2f a later USABLE turn under the same key IS indexed");
    CHECK(ctrl_index_lookup(&ix, K) == 9, "T2g newest-wins: K now resolves to the newer answer");

    /* Newest is by logical_index VALUE, not array order — pin it by adding out of order. */
    mk_good(&rec, K, "an older answer added last");
    (void)ctrl_index_add(&ix, &rec, 4u);
    CHECK(ctrl_index_lookup(&ix, K) == 9,
          "T2h newest is by logical_index VALUE, not by array order (added-last does not win)");

    CHECK(ctrl_index_lookup(&ix, 0xDEADBEEFull) == -1, "T2i an unknown key misses");

    /* ---------------------------------------------------------------------------------
     * T3: SATURATION. A full index must refuse with -1 (recallable, no room) and write
     * NOTHING past the array. Checked twice on purpose:
     *   (a) a 0xAA canary slot past the declared capacity — a legal write that is caught
     *       by VALUE, so this sub-test still has teeth without a sanitizer;
     *   (b) an EXACT-LENGTH heap array — any write past it is a genuine heap overflow and
     *       ASan reports it, so the overflow is detected rather than inferred. Same
     *       exact-len-heap discipline the control-IN fuzz harness uses.
     * --------------------------------------------------------------------------------- */
    epi_index_entry_t guarded[5];
    epi_index_entry_t canary_ref;
    memset(guarded, 0xAA, sizeof(guarded));
    memset(&canary_ref, 0xAA, sizeof(canary_ref));
    ctrl_index_init(&ix, guarded, 4);          /* capacity 4; guarded[4] is the canary */

    int filled_ok = 1;
    for (uint32_t i = 0; i < 4u; i++) {
        mk_good(&rec, 0x100u + i, "fill");
        if (ctrl_index_add(&ix, &rec, i) != 1) filled_ok = 0;
    }
    CHECK(filled_ok && ix.n == 4, "T3a filled to capacity, every add reported 1");

    mk_good(&rec, 0x999u, "one too many");
    CHECK(ctrl_index_add(&ix, &rec, 99u) == -1,
          "T3b a usable record with no room -> -1 (recallable, NOT 0 — the embed/warn gate)");
    CHECK(ix.n == 4, "T3c the refused add did not grow n");
    CHECK(memcmp(&guarded[4], &canary_ref, sizeof(canary_ref)) == 0,
          "T3d CANARY past capacity is untouched (bounds check held)");
    CHECK(ctrl_index_lookup(&ix, 0x999u) == -1, "T3e the refused record is not findable");

    /* An UNUSABLE record on a full index still reports 0, not -1: the predicate is checked
     * first, so "not recallable" beats "no room". That ordering is what keeps a failed turn
     * from triggering the caller's embed and full-index warning. */
    mk_rec(&rec, 0x998u, EPI_ACT_CONTROL_IN, EPI_OUT_ERROR, 0u, NULL);
    CHECK(ctrl_index_add(&ix, &rec, 99u) == 0,
          "T3f unusable record on a FULL index -> 0 (predicate is checked before capacity)");

    epi_index_entry_t *exact = malloc(4 * sizeof(epi_index_entry_t));
    CHECK(exact != NULL, "T3g exact-length heap storage allocated");
    if (exact) {
        ctrl_index_init(&ix, exact, 4);
        for (uint32_t i = 0; i < 4u; i++) {
            mk_good(&rec, 0x200u + i, "fill");
            (void)ctrl_index_add(&ix, &rec, i);
        }
        mk_good(&rec, 0x299u, "overflow attempt");
        /* With the bounds check removed this write lands in the malloc redzone and ASan
         * aborts here — the overflow is REPORTED, not deduced from a survivor check. */
        CHECK(ctrl_index_add(&ix, &rec, 4u) == -1 && ix.n == 4,
              "T3h exact-len heap array: overflow refused (ASan would abort if it were not)");
        free(exact);
    }

    /* cap 0 is a degenerate handle, not a crash: everything usable reports "no room". */
    ctrl_index_init(&ix, store, 0);
    mk_good(&rec, 0x1u, "x");
    CHECK(ix.cap == 0 && ctrl_index_add(&ix, &rec, 0u) == -1 && ix.n == 0,
          "T3i cap 0 -> every usable record reports -1, nothing written");

    /* ---------------------------------------------------------------------------------
     * T4: BUILD — the boot-scan half. Reset-then-walk, oldest first, unusable records
     * skipped, unreadable records skipped, walk clamped to cap.
     * --------------------------------------------------------------------------------- */
    epi_record_t recs[8];
    mk_good(&recs[0], 0xA0u, "good 0");
    mk_rec (&recs[1], 0xA1u, EPI_ACT_CONTROL_IN, EPI_OUT_ERROR, 0u, NULL);      /* failed */
    mk_good(&recs[2], 0xA2u, "good 2");
    mk_rec (&recs[3], 0xA3u, EPI_ACT_CONTROL_IN_LOCAL, EPI_OUT_OK, 20u, "up 5s");/* tag 4 */
    mk_good(&recs[4], 0xA4u, "good 4");
    mk_good(&recs[5], 0xA5u, "good 5");
    mk_good(&recs[6], 0xA6u, "good 6");
    mk_good(&recs[7], 0xA7u, "good 7");

    flat_ctx_t fc = { recs, 8u, 0xFFFFFFFFu };
    ctrl_index_init(&ix, store, 64);
    CHECK(ctrl_index_build(&ix, 8u, flat_read, &fc, &scratch) == 6 && ix.n == 6,
          "T4a build indexes the 6 usable records and skips the failed + tag-4 ones");
    CHECK(ix.ent[0].logical_index == 0u && ix.ent[1].logical_index == 2u
          && ix.ent[2].logical_index == 4u && ix.ent[3].logical_index == 5u,
          "T4b entries carry the STORE's logical index, not the index array position");
    int ascending = 1;
    for (int i = 1; i < ix.n; i++)
        if (ix.ent[i].logical_index <= ix.ent[i - 1].logical_index) ascending = 0;
    CHECK(ascending, "T4c entries are oldest-first ascending (the ctrl_sem_gather contract)");
    CHECK(ctrl_index_lookup(&ix, 0xA1u) == -1 && ctrl_index_lookup(&ix, 0xA3u) == -1,
          "T4d the skipped records are not findable");

    /* A second build REPLACES the first. Without the reset the index would double. */
    CHECK(ctrl_index_build(&ix, 8u, flat_read, &fc, &scratch) == 6 && ix.n == 6,
          "T4e a second build replaces rather than appends (reset semantics)");

    /* An unreadable record is skipped, exactly as `epi_store_read(...) == 0` does today. */
    fc.fail_at = 4u;
    CHECK(ctrl_index_build(&ix, 8u, flat_read, &fc, &scratch) == 5
          && ctrl_index_lookup(&ix, 0xA4u) == -1,
          "T4f a record whose read fails is skipped, the walk continues");
    fc.fail_at = 0xFFFFFFFFu;

    /* The walk is clamped to cap: a store bigger than the index must not overrun it. cap 3
     * examines logical 0..2, of which 0 and 2 are usable and 1 is the failed record — so 2
     * entries, against the 6 an unclamped walk would produce. */
    ctrl_index_init(&ix, store, 3);
    CHECK(ctrl_index_build(&ix, 8u, flat_read, &fc, &scratch) == 2 && ix.n == 2,
          "T4g store_count > cap: the walk is CLAMPED to cap (2 usable of the 3 examined, not 6)");
    CHECK(ix.ent[1].logical_index == 2u,
          "T4h the clamped walk stopped at logical 2 (cap 3 records examined, 2 usable + 1 failed)");

    /* The same clamp where every examined record IS usable, so the walk fills the index to
     * exactly cap. This is the case that would overrun a build with no bounds check — today's
     * boot scan has none and is safe only because walk <= array size, which is the property
     * being pinned here. */
    epi_record_t allgood[8];
    for (uint32_t i = 0; i < 8u; i++)
        mk_good(&allgood[i], 0xD0u + i, "good");
    flat_ctx_t fg = { allgood, 8u, 0xFFFFFFFFu };
    ctrl_index_init(&ix, store, 3);
    CHECK(ctrl_index_build(&ix, 8u, flat_read, &fg, &scratch) == 3 && ix.n == 3,
          "T4h2 clamp at exact saturation: 8 usable records, cap 3 -> exactly 3, no overrun");
    CHECK(ix.ent[2].logical_index == 2u && ctrl_index_lookup(&ix, 0xD3u) == -1,
          "T4h3 ...and the records past the clamp were never examined");

    ctrl_index_init(&ix, store, 64);
    CHECK(ctrl_index_build(&ix, 0u, flat_read, &fc, &scratch) == 0 && ix.n == 0,
          "T4i an empty store builds an empty index");
    CHECK(ctrl_index_build(&ix, 8u, NULL, &fc, &scratch) == 0 && ix.n == 0,
          "T4j NULL reader -> empty index (and the reset still happened)");
    CHECK(ctrl_index_build(&ix, 8u, flat_read, &fc, NULL) == 0 && ix.n == 0,
          "T4k NULL scratch -> empty index");
    CHECK(ctrl_index_build(NULL, 8u, flat_read, &fc, &scratch) == 0, "T4l NULL handle -> 0");

    /* A build over a populated index with a broken reader must EMPTY it, not leave the old
     * one standing — a stale index is worse than none. */
    (void)ctrl_index_build(&ix, 8u, flat_read, &fc, &scratch);
    CHECK(ix.n == 6, "T4m index repopulated before the broken-reader case");
    CHECK(ctrl_index_build(&ix, 8u, NULL, &fc, &scratch) == 0 && ix.n == 0,
          "T4n a build that cannot read leaves the index EMPTY, not stale");

    /* ---------------------------------------------------------------------------------
     * T5: POST-WRAP DRIFT — what this module does NOT promise.
     *
     * A logical index is a position in oldest->newest order. When the region rolls,
     * epi_store_read maps logical L to slot (cursor + L) % max, so every entry recorded
     * before the roll starts resolving to a DIFFERENT record. The index cannot see this:
     * it holds no store state.
     *
     * The store below has 4 slots and the index is kept current on every append, exactly
     * as ctrl_epi_write does. After four appends the store is exactly full; the fifth is
     * the roll.
     * --------------------------------------------------------------------------------- */
    mock_store_t ms;
    memset(&ms, 0, sizeof(ms));
    ctrl_index_init(&ix, store, 64);

    /* Fill to exactly capacity: keys 0xE0..0xE3 at logical 0..3. */
    int all_added = 1;
    for (uint32_t i = 0; i < 4u; i++) {
        mk_good(&rec, 0xE0u + i, "answer");
        if (store_and_index(&ms, &ix, &rec) != 1) all_added = 0;
    }
    CHECK(all_added && ix.n == 4 && mock_count(&ms) == 4u,
          "T5a store filled to capacity, all four turns indexed");

    /* Pre-roll every entry resolves to its own record — the baseline the drift is measured
     * against, and without it a later mismatch could be a mapping bug rather than drift. */
    int pre_ok = 1;
    for (uint32_t i = 0; i < 4u; i++) {
        int li = ctrl_index_lookup(&ix, 0xE0u + i);
        if (li < 0 || mock_read(&ms, (uint32_t)li, &scratch) != 0
            || scratch.query_key != 0xE0u + i) pre_ok = 0;
    }
    CHECK(pre_ok, "T5b PRE-ROLL: every key resolves to a record carrying that same key");

    /* THE ROLL. One more turn overwrites the oldest slot and shifts the whole mapping by 1. */
    mk_good(&rec, 0xE4u, "the newest answer");
    CHECK(store_and_index(&ms, &ix, &rec) == 1 && ms.total == 5u,
          "T5c the store rolled (5 appends into 4 slots)");

    /* The oldest key now resolves to a record that is NOT its own. This is the drift, and
     * it is why the caller re-checks the key. */
    int li0 = ctrl_index_lookup(&ix, 0xE0u);
    CHECK(li0 == 0, "T5d the stale entry survives: key 0xE0 still resolves to logical 0");
    CHECK(mock_read(&ms, (uint32_t)li0, &scratch) == 0 && scratch.query_key != 0xE0u,
          "T5e DRIFT: logical 0 now holds a DIFFERENT record — the key MISMATCHES");
    CHECK(scratch.query_key == 0xE1u,
          "T5f the drifted read lands on the next record (slot = (cursor + L) mod max)");
    /* *** THE VERDICT ON THE SAFETY CLAIM. *** The index made no promise here and kept
     * none: it returned a stale index for a record it no longer describes. What prevents a
     * wrong recall is entirely the CALLER's `rec.query_key == ckey` re-check, which fails
     * on exactly this record and turns the hit into a clean miss. CONFIRMED. */
    CHECK(scratch.query_key != 0xE0u,
          "T5g CALLER'S RE-CHECK is what saves this: key mismatch -> clean MISS, not a wrong recall");

    /* Drift is not free, and this is its real cost: record 0xE3 is STILL IN THE STORE, but
     * its index entry now points elsewhere, so it has become unreachable. A silent loss of
     * recall — never a wrong answer, which is the right direction to fail in. */
    int li3 = ctrl_index_lookup(&ix, 0xE3u);
    CHECK(li3 >= 0 && mock_read(&ms, (uint32_t)li3, &scratch) == 0 && scratch.query_key != 0xE3u,
          "T5h COST OF DRIFT: 0xE3 is still stored, but its entry no longer resolves to it");

    /* The NEWEST entry is unaffected — drift damages old entries first, which is why a
     * repeat asked soon after its first answer still recalls. */
    int li4 = ctrl_index_lookup(&ix, 0xE4u);
    CHECK(li4 >= 0 && mock_read(&ms, (uint32_t)li4, &scratch) == 0 && scratch.query_key == 0xE4u,
          "T5i the newest entry still resolves correctly after the roll");

    /*
     * THE HONEST RESIDUAL — a drifted entry that lands on a record with a MATCHING key.
     *
     * Constructed with a deliberately small index (cap 2) so the store keeps accepting
     * turns after the index saturates, which is how two records for one key can exist with
     * only the older one indexed. After two rolls the stale entry resolves to the OTHER
     * record for the same key, so the caller's re-check PASSES.
     *
     * This does NOT falsify the safety claim. query_key is the hash of the normalized
     * query, so a matching key means the SAME question: the caller recalls a legitimate
     * stored answer to the question it asked, never another question's answer. What is
     * lost is identity/freshness — the record is not the one the entry described. So the
     * property that holds is "never a WRONG-QUESTION recall"; the looser phrasing "a stale
     * entry becomes a clean miss" is not exactly true, and this is the case that shows it.
     * (Key collision could in principle break the question-identity property too, but that
     * is a pre-existing property of FNV-1a keying shared by the whole exact-key path, not
     * something drift introduces.)
     */
    mock_store_t ms2;
    epi_index_entry_t small[2];
    ctrl_epi_index_t six;
    memset(&ms2, 0, sizeof(ms2));
    ctrl_index_init(&six, small, 2);
    const uint64_t KK = 0xF00Dull;

    mk_good(&rec, KK, "the OLD answer");
    CHECK(store_and_index(&ms2, &six, &rec) == 1, "T5j saturation case: OLD answer for KK indexed at 0");
    mk_good(&rec, 0xB1u, "filler");
    (void)store_and_index(&ms2, &six, &rec);                    /* index now FULL (cap 2) */
    mk_good(&rec, KK, "the NEW answer");
    CHECK(store_and_index(&ms2, &six, &rec) == -1,
          "T5k a NEW answer for KK is stored but the index is FULL -> -1 (recallable, no room)");
    mk_good(&rec, 0xB3u, "filler");
    (void)store_and_index(&ms2, &six, &rec);

    /* Two rolls: the stale {KK, 0} entry now resolves to logical 0 -> slot 2 = the NEW KK record. */
    mk_good(&rec, 0xC1u, "roll 1");
    (void)store_and_index(&ms2, &six, &rec);
    mk_good(&rec, 0xC2u, "roll 2");
    (void)store_and_index(&ms2, &six, &rec);

    int lik = ctrl_index_lookup(&six, KK);
    CHECK(lik == 0, "T5l the stale KK entry still points at logical 0");
    CHECK(mock_read(&ms2, (uint32_t)lik, &scratch) == 0 && scratch.query_key == KK,
          "T5m RESIDUAL: the drifted read lands on a MATCHING key — the re-check PASSES");
    CHECK(memcmp(scratch.resp, "the NEW answer", 14) == 0,
          "T5n ...and it is a DIFFERENT record for the SAME question (identity/freshness, "
          "never a wrong-question recall)");

    printf("=== Results: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/* ---------------------------------------------------------------------------------------------
 * T6 — MUTATION TEETH (repo §6.10 discipline). MEASURED, not predicted.
 *
 * The mutants are applied to ctrl_epi_index.c at build time, run, and reverted; they are not
 * encoded here. Every run below was preceded by an unmutated CONTROL run (64 PASS, 0 FAIL) and
 * followed by a restore verified with `diff -q`, so neither "the mutant did not apply" nor "the
 * restore did not happen" can masquerade as a result. Both failure modes were hit for real while
 * producing this list, which is why the verification is spelled out rather than assumed.
 *
 * Results are what the runs PRINTED. Two of the predictions written here first were wrong, and
 * are corrected below rather than quietly reworded.
 *
 *   MUTANT A — delete the g3_candidate_usable call from ctrl_index_add (index everything).
 *              MEASURED: 42 PASS, 22 FAIL — T1d, T1e, T1f, T1g, T1h, T1i, T1j, T1n, T2a, T2b,
 *              T2c, T2d, T2e, T3f, T4a, T4b, T4d, T4e, T4f, T4g, T4h, T4m.
 *              T2b and T2d are the load-bearing ones: they show a failed turn and a tag-4 turn
 *              SHADOWING the good answer under key K, which is the permanent, silent defect the
 *              predicate exists to prevent.
 *
 *   MUTANT B1 — delete the `ix->n >= ix->cap` check outright.
 *              MEASURED: ASan SEGV, write to NULL in ctrl_index_add, and ZERO named assertions —
 *              it dies at T1o, the NULL-storage handle, long before the capacity tests. That is
 *              a real consequence (the NULL-storage guard is IMPLEMENTED as cap == 0, so deleting
 *              the capacity test also removes it), but it means B1 does not demonstrate that the
 *              capacity teeth work. B2 exists for that reason.
 *
 *   MUTANT B2 — remove ONLY the capacity bound, keeping a `if (!ix->ent) return -1;` guard so the
 *              NULL-storage case still survives and execution reaches T3.
 *              MEASURED: FAIL T3b, T3c, T3d, T3e, then ASan heap-buffer-overflow, WRITE of size 8,
 *              in ctrl_index_add at T3h's exact-length heap array. Both halves of T3 do their job:
 *              the 0xAA canary catches the over-write BY VALUE (T3d, so the bound has teeth even
 *              without a sanitizer) and the exact-length heap array proves it is a genuine
 *              overflow rather than an inference from a survivor check.
 *
 *   MUTANT C — replace the epi_index_lookup delegation with a first-match-wins scan.
 *              MEASURED: 62 PASS, 2 FAIL — T2g AND T2h.
 *              The prediction that T2h would keep passing was WRONG. T2h adds {K,4} after {K,9},
 *              so a first-match scan returns 3 (the earliest K entry) rather than 9; both the
 *              "newer supersedes older" case and the "by value, not by array order" case detect
 *              this mutant. They still test different properties and both are kept.
 *
 * ONE MEASUREMENT DEFECT FOUND AND FIXED IN THIS FILE. The first B1/B2 runs reported an ASan
 * abort and NOT ONE PASS/FAIL line, because piped stdout is fully buffered and the abort
 * discarded the buffer — so "which assertion caught it?" was unanswerable. main() now calls
 * setvbuf(_IONBF); without it a suite whose whole purpose includes provoking an abort silently
 * loses its own results.
 * ------------------------------------------------------------------------------------------- */
