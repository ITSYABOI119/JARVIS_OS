/*
 * JARVIS AI-OS — Embedding Vector Store Unit Tests (Phase C / C/M2a)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_embed_store.c phase3/src/ai/embed_store.c \
 *       -o /tmp/test_embed_store
 *
 * Device-independent: mock read/write callbacks over a mock buffer. No NVMe, no
 * box, no call site — C/M2a is storage only.
 *
 * The cases that are NOT inherited from test_semantic_store.c, and why each exists:
 *
 *   T7-T9   the DESYNC PREDICATE. The entire justification for spending a second
 *           sector per record is that a vector can be proven to belong to its
 *           episodic record. T8 is the case positional-only mapping is structurally
 *           blind to. T9 pins that BOTH fields are checked, not just one.
 *   T11     the checksum spans BOTH sectors. Corrupting the VECTOR half while
 *           leaving the metadata half perfectly self-consistent is exactly a torn
 *           2-sector write, and a metadata-only checksum would call it valid.
 *   T12     the header dim guard, with a POSITIVE CONTROL: the header checksum is
 *           repaired after patching dim, so the test cannot pass merely because the
 *           checksum caught the edit. Without that it would be vacuous.
 *   T13     region isolation, mirroring test_episodic_store.c's T9 — two instances
 *           over one device, deliberately ADJACENT so a one-sector overrun in the
 *           2-sectors-per-record arithmetic fails here, plus the real constants
 *           checked by arithmetic.
 */

#include "embed_store.h"
#include "episodic_store.h"   /* T13: real neighbouring region constants (headers, not literals) */
#include <stdio.h>
#include <string.h>

/* ---- Mock storage: header + all EMBED_STORE_MAX_VECS records (2 sectors each) ---- */
#define MOCK_SECTORS (1U + EMBED_STORE_MAX_VECS * EMBED_STORE_SECTORS_PER_REC)
static uint8_t mock_disk[MOCK_SECTORS * 512];

static int mock_read(uint64_t lba, uint32_t count, void *buf)
{
    uint64_t offset = (lba - EMBED_STORE_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk))
        return -1;
    memcpy(buf, mock_disk + offset, (size_t)count * 512);
    return 0;
}

static int mock_write(uint64_t lba, uint32_t count, const void *buf)
{
    uint64_t offset = (lba - EMBED_STORE_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk))
        return -1;
    memcpy(mock_disk + offset, buf, (size_t)count * 512);
    return 0;
}

/* ---- Test harness ---- */
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

/* A deterministic, distinguishable vector. No math.h — the store does not care
 * about the norm (unit-ness is g3_vec_is_unit's job in the selector, not here). */
static void make_vec(float *v, int tag)
{
    for (int i = 0; i < EMBED_STORE_DIM; i++)
        v[i] = (float)tag + (float)i * 0.001f;
}

static void make_rec(embed_rec_t *r, uint32_t owner_seq, uint64_t owner_key, int tag)
{
    float v[EMBED_STORE_DIM];
    make_vec(v, tag);
    embed_rec_fill(r, owner_seq, owner_key, v, EMBED_STORE_DIM);
}

/* The header's own 15-word XOR, duplicated here so T12 can repair it. */
static uint32_t hdr_checksum_of_mock(void)
{
    uint32_t cs = 0;
    for (int i = 0; i < 15; i++) {
        uint32_t w;
        memcpy(&w, mock_disk + i * 4, sizeof(w));
        cs ^= w;
    }
    return cs;
}

/* ================================================================
 * T1: layout + fresh init on a garbage disk
 * ================================================================ */
static void test_fresh_init(void)
{
    ASSERT(sizeof(embed_rec_t) == 1024, "embed_rec_t is exactly two sectors");
    ASSERT(sizeof(embed_store_header_t) <= 512, "header fits one sector");
    ASSERT(EMBED_STORE_DIM * 4 == 512, "vector is exactly one sector");
    ASSERT(offsetof(embed_rec_t, vec) == 0, "vector is sector +0 (written FIRST)");
    ASSERT(offsetof(embed_rec_t, owner_key) == 512, "metadata is sector +1 (written LAST)");
    ASSERT(EMBED_STORE_SECTORS_PER_REC == 2, "two sectors per record");

    memset(mock_disk, 0xFF, sizeof(mock_disk));   /* garbage disk */
    embed_store_t s;
    int rc = embed_store_init(&s, mock_read, mock_write,
                              EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS);
    ASSERT(rc == 0, "fresh init succeeds");
    ASSERT(embed_store_boot_id(&s) == 1, "fresh boot_id == 1");
    ASSERT(embed_store_count(&s) == 0, "fresh count == 0");
    ASSERT(s.hdr.dim == (uint32_t)EMBED_STORE_DIM, "fresh header records the dimension");
    PASS("T1 layout + fresh init (garbage disk -> fresh header, boot_id 1)");
}

/* ================================================================
 * T2: append + read-back; vector BIT-EXACT (memcmp, never a tolerance)
 * ================================================================ */
static void test_append_roundtrip(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    embed_store_t s;
    embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS);

    embed_rec_t r;
    make_rec(&r, 4242u, 0xABCDEF0123456789ULL, 7);
    ASSERT(embed_store_append(&s, &r) == 0, "append succeeds");
    ASSERT(embed_store_count(&s) == 1, "count == 1 after append");

    embed_rec_t back;
    ASSERT(embed_store_read(&s, 0, &back) == 0, "read logical 0 succeeds");
    /* Storage, not arithmetic: a float tolerance here would hide a serialisation bug. */
    ASSERT(memcmp(back.vec, r.vec, sizeof(r.vec)) == 0, "vector round-trips BIT-EXACTLY");
    ASSERT(back.owner_seq == 4242u, "owner_seq round-trips");
    ASSERT(back.owner_key == 0xABCDEF0123456789ULL, "owner_key round-trips");
    ASSERT(back.dim == (uint32_t)EMBED_STORE_DIM, "dim stamped");
    ASSERT(back.status == EMBED_STORE_ST_VALID, "status stamped");
    ASSERT(back.boot_id == 1, "boot_id stamped at write");
    ASSERT(back.seq == 0, "seq stamped (== total_entries at write)");
    ASSERT(embed_rec_is_valid(&back) == 1, "read-back record is structurally valid");
    ASSERT(embed_store_read(&s, 1, &back) == -1, "read past count fails");
    PASS("T2 append + read-back (vector bit-exact, identity + stamps preserved)");
}

/* ================================================================
 * T3: re-init bumps boot_id; vectors persist across a "reboot"
 * ================================================================ */
static void test_reinit_persists(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    embed_store_t s;
    embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS);

    embed_rec_t r;
    make_rec(&r, 10u, 111ULL, 1);
    embed_store_append(&s, &r);
    make_rec(&r, 20u, 222ULL, 2);
    embed_store_append(&s, &r);

    embed_store_t s2;   /* "reboot": a fresh handle over the same disk */
    ASSERT(embed_store_init(&s2, mock_read, mock_write,
                            EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS) == 0,
           "re-init succeeds");
    ASSERT(embed_store_boot_id(&s2) == 2, "boot_id bumped to 2");
    ASSERT(embed_store_count(&s2) == 2, "both vectors survive");

    embed_rec_t back;
    float expect[EMBED_STORE_DIM];
    make_vec(expect, 2);
    ASSERT(embed_store_read(&s2, 1, &back) == 0 && back.owner_seq == 20u,
           "newest vector intact");
    ASSERT(memcmp(back.vec, expect, sizeof(expect)) == 0,
           "persisted vector is still bit-exact");
    ASSERT(embed_rec_is_valid(&back) == 1, "persisted record still validates");

    make_rec(&r, 30u, 333ULL, 3);
    ASSERT(embed_store_append(&s2, &r) == 0 && embed_store_count(&s2) == 3,
           "append after re-init works");
    ASSERT(embed_store_read(&s2, 2, &back) == 0 && back.boot_id == 2,
           "post-reboot record stamped with the new boot_id");
    PASS("T3 re-init bumps boot_id, vectors persist bit-exactly");
}

/* ================================================================
 * T4: corrupt header checksum -> fresh header (no crash, boot_id 1)
 * ================================================================ */
static void test_corrupt_header(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    embed_store_t s;
    embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS);
    embed_rec_t r;
    make_rec(&r, 44u, 444ULL, 4);
    embed_store_append(&s, &r);

    mock_disk[8] ^= 0xFF;   /* corrupt the cursor word — checksum now wrong */

    embed_store_t s2;
    ASSERT(embed_store_init(&s2, mock_read, mock_write,
                            EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS) == 0,
           "init over a corrupt header succeeds");
    ASSERT(embed_store_boot_id(&s2) == 1, "corrupt header -> fresh boot_id 1");
    ASSERT(embed_store_count(&s2) == 0, "corrupt header -> count reset");
    PASS("T4 corrupt header checksum -> fresh header");
}

/* ================================================================
 * T5: circular wrap (small max) — oldest overwritten, total monotonic
 * ================================================================ */
static void test_wrap(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    embed_store_t s;
    embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, 4);   /* tiny store */

    embed_rec_t r;
    for (int i = 1; i <= 6; i++) {
        make_rec(&r, (uint32_t)i, (uint64_t)(i * 100), i);
        ASSERT(embed_store_append(&s, &r) == 0, "wrap append succeeds");
    }
    ASSERT(embed_store_count(&s) == 4, "count capped at max (rolling-full)");
    ASSERT(s.hdr.total_entries == 6, "total_entries MONOTONIC across the wrap");

    embed_rec_t back;
    ASSERT(embed_store_read(&s, 0, &back) == 0 && back.owner_seq == 3u,
           "oldest retained == record 3 (1-2 overwritten)");
    ASSERT(embed_store_read(&s, 3, &back) == 0 && back.owner_seq == 6u,
           "newest == record 6");
    /* A wrapped slot must hold the NEW record's vector, not a blend of both. */
    float expect[EMBED_STORE_DIM];
    make_vec(expect, 6);
    ASSERT(memcmp(back.vec, expect, sizeof(expect)) == 0,
           "the overwriting record's vector is intact (2-sector write did not straddle)");
    ASSERT(embed_rec_is_valid(&back) == 1, "wrapped-over record validates");
    PASS("T5 circular wrap (oldest overwritten, total monotonic, 2-sector slots aligned)");
}

/* ================================================================
 * T6: put is idempotent — re-putting the same owner_seq is a NO-OP, not a dup
 * ================================================================ */
static void test_put_idempotent(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    embed_store_t s;
    embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS);

    embed_rec_t r;
    make_rec(&r, 777u, 0xAAAAULL, 1);
    ASSERT(embed_store_put(&s, &r) == 0, "new owner_seq -> inserted (returns 0)");
    ASSERT(embed_store_count(&s) == 1, "count 1 after insert");

    /* Same record, recomputed — must be a no-op. A vector is a deterministic
     * function of its query under a frozen model, so a re-scan at boot must not
     * duplicate it. */
    make_rec(&r, 777u, 0xAAAAULL, 1);
    ASSERT(embed_store_put(&s, &r) == 1, "same owner_seq -> already present (returns 1)");
    ASSERT(embed_store_count(&s) == 1, "count STILL 1 (no duplicate)");

    /* A DIFFERENT vector for the same owner_seq is still refused — that means the
     * pipeline changed, and the answer is to reset the store (the header dim
     * guard), never to blend two generations of vector into one corpus. */
    make_rec(&r, 777u, 0xAAAAULL, 99);
    ASSERT(embed_store_put(&s, &r) == 1, "differing vector for a known seq is still a no-op");
    ASSERT(embed_store_count(&s) == 1, "count STILL 1");
    embed_rec_t back;
    float original[EMBED_STORE_DIM];
    make_vec(original, 1);
    ASSERT(embed_store_read(&s, 0, &back) == 0 &&
           memcmp(back.vec, original, sizeof(original)) == 0,
           "the ORIGINAL vector is retained, not silently replaced");

    make_rec(&r, 888u, 0xBBBBULL, 2);
    ASSERT(embed_store_put(&s, &r) == 0, "different owner_seq -> inserted");
    ASSERT(embed_store_count(&s) == 2, "count 2 (two distinct records)");

    uint32_t li = 0xFFFFFFFFu;
    ASSERT(embed_store_find_by_seq(&s, 888u, &li, NULL) == 1 && li == 1,
           "find_by_seq locates the second record");
    ASSERT(embed_store_find_by_seq(&s, 999u, NULL, NULL) == 0,
           "find_by_seq reports absence as 0, not an error");
    PASS("T6 put is idempotent (no dup, original retained, find_by_seq agrees)");
}

/* ================================================================
 * T7: the desync predicate says MATCH for a record's true owner
 * ================================================================ */
static void test_desync_match(void)
{
    embed_rec_t r;
    make_rec(&r, 1234u, 0xFEEDFACEULL, 5);
    ASSERT(embed_rec_matches(&r, 1234u, 0xFEEDFACEULL) == 1,
           "matching seq + key -> MATCH");
    PASS("T7 desync predicate: true owner -> MATCH");
}

/* ================================================================
 * T8: seq differs -> MISMATCH.
 *
 * THIS IS THE CASE POSITIONAL-ONLY MAPPING CANNOT SEE. Under slot-i <-> slot-i
 * the vector would be handed back as this record's own, and a perfectly valid
 * vector of the WRONG record walks straight past the similarity floor.
 * ================================================================ */
static void test_desync_seq_differs(void)
{
    embed_rec_t r;
    make_rec(&r, 1234u, 0xFEEDFACEULL, 5);
    ASSERT(embed_rec_matches(&r, 1235u, 0xFEEDFACEULL) == 0,
           "seq differs (key same) -> MISMATCH");
    ASSERT(embed_rec_matches(&r, 0u, 0xFEEDFACEULL) == 0,
           "seq 0 against a non-zero owner -> MISMATCH");
    PASS("T8 desync predicate: seq differs -> MISMATCH (the positional-only blind spot)");
}

/* ================================================================
 * T9: query_key differs while seq matches -> MISMATCH.
 * Pins that BOTH fields are compared: a predicate checking only seq passes a
 * suite that only ever varies the key, and vice versa.
 * ================================================================ */
static void test_desync_key_differs(void)
{
    embed_rec_t r;
    make_rec(&r, 1234u, 0xFEEDFACEULL, 5);
    ASSERT(embed_rec_matches(&r, 1234u, 0xDEADBEEFULL) == 0,
           "key differs (seq same) -> MISMATCH");
    ASSERT(embed_rec_matches(&r, 1234u, 0ULL) == 0,
           "key 0 against a non-zero owner -> MISMATCH");
    PASS("T9 desync predicate: key differs -> MISMATCH (both fields are checked)");
}

/* ================================================================
 * T10: NULL guards — no crash, no false positive
 * ================================================================ */
static void test_null_guards(void)
{
    embed_rec_t r;
    make_rec(&r, 1u, 2ULL, 3);
    float v[EMBED_STORE_DIM];
    make_vec(v, 1);

    ASSERT(embed_rec_matches(NULL, 1u, 2ULL) == 0, "matches(NULL) -> 0, never a match");
    ASSERT(embed_rec_is_valid(NULL) == 0, "is_valid(NULL) -> 0");
    ASSERT(embed_rec_checksum(NULL) == 0, "checksum(NULL) -> 0");
    ASSERT(embed_rec_fill(NULL, 1u, 2ULL, v, EMBED_STORE_DIM) < 0, "fill(NULL rec) refused");
    ASSERT(embed_rec_fill(&r, 1u, 2ULL, NULL, EMBED_STORE_DIM) < 0, "fill(NULL vec) refused");
    ASSERT(embed_rec_fill(&r, 1u, 2ULL, v, 64) < 0, "fill with the wrong dim refused");

    embed_store_t s;
    ASSERT(embed_store_init(NULL, mock_read, mock_write, EMBED_STORE_BASE_LBA, 4) < 0,
           "init(NULL store) refused");
    ASSERT(embed_store_init(&s, NULL, mock_write, EMBED_STORE_BASE_LBA, 4) < 0,
           "init(NULL read) refused");
    ASSERT(embed_store_init(&s, mock_read, NULL, EMBED_STORE_BASE_LBA, 4) < 0,
           "init(NULL write) refused");
    ASSERT(embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, 0) < 0,
           "init with max_entries 0 refused");

    /* An uninitialised handle must not be usable. */
    embed_store_t un;
    memset(&un, 0, sizeof(un));
    ASSERT(embed_store_append(&un, &r) < 0, "append on an uninitialised store refused");
    ASSERT(embed_store_read(&un, 0, &r) < 0, "read on an uninitialised store refused");
    ASSERT(embed_store_put(&un, &r) < 0, "put on an uninitialised store refused");
    ASSERT(embed_store_find_by_seq(&un, 0, NULL, NULL) < 0, "find on an uninitialised store refused");
    ASSERT(embed_store_flush(&un) < 0, "flush on an uninitialised store refused");
    ASSERT(embed_store_count(&un) == 0, "count on an uninitialised store == 0");
    ASSERT(embed_store_boot_id(&un) == 0, "boot_id on an uninitialised store == 0");
    ASSERT(embed_store_count(NULL) == 0, "count(NULL) == 0");
    ASSERT(embed_store_boot_id(NULL) == 0, "boot_id(NULL) == 0");

    memset(mock_disk, 0, sizeof(mock_disk));
    embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, 4);
    ASSERT(embed_store_append(&s, NULL) < 0, "append(NULL rec) refused");
    ASSERT(embed_store_read(&s, 0, NULL) < 0, "read(NULL out) refused");
    ASSERT(embed_store_put(&s, NULL) < 0, "put(NULL rec) refused");
    PASS("T10 NULL / uninitialised guards (no crash, no false positive)");
}

/* ================================================================
 * T11: THE CHECKSUM SPANS BOTH SECTORS.
 *
 * A torn 2-sector write leaves the metadata half self-consistent while the vector
 * half still belongs to the slot's previous occupant. A checksum over the metadata
 * alone would call that valid — which is the failure this layout exists to expose.
 * Corrupting the vector half on the mock disk reproduces exactly that shape.
 * ================================================================ */
static void test_checksum_spans_both_sectors(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    embed_store_t s;
    embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS);

    embed_rec_t r;
    make_rec(&r, 55u, 0x5555ULL, 8);
    ASSERT(embed_store_append(&s, &r) == 0, "append succeeds");

    embed_rec_t back;
    ASSERT(embed_store_read(&s, 0, &back) == 0, "read back");
    ASSERT(embed_rec_is_valid(&back) == 1, "positive control: an untouched record IS valid");

    /* Record 0 lives at base+1 (vector) and base+2 (metadata). Byte 0 of the
     * VECTOR sector — the metadata half is left completely intact. */
    const size_t vec_sector_off = 1u * 512u;
    mock_disk[vec_sector_off] ^= 0xFF;

    ASSERT(embed_store_read(&s, 0, &back) == 0, "re-read after vector-half corruption");
    ASSERT(back.owner_seq == 55u && back.owner_key == 0x5555ULL,
           "the METADATA half is untouched and still self-consistent");
    ASSERT(embed_rec_matches(&back, 55u, 0x5555ULL) == 1,
           "identity still MATCHES — identity alone cannot see a torn write");
    ASSERT(embed_rec_is_valid(&back) == 0,
           "TEETH: the record is INVALID because the checksum spans the vector sector too");

    mock_disk[vec_sector_off] ^= 0xFF;   /* restore */
    ASSERT(embed_store_read(&s, 0, &back) == 0 && embed_rec_is_valid(&back) == 1,
           "restoring the vector byte makes it valid again (the checksum, not luck)");

    /* And the converse half: corrupt a metadata byte, leave the vector intact. */
    const size_t meta_sector_off = 2u * 512u;
    mock_disk[meta_sector_off] ^= 0xFF;   /* inside owner_key */
    ASSERT(embed_store_read(&s, 0, &back) == 0, "re-read after metadata corruption");
    ASSERT(embed_rec_is_valid(&back) == 0, "metadata-half corruption also invalidates");
    PASS("T11 checksum spans BOTH sectors (torn-write detectable, identity alone is not enough)");
}

/* ================================================================
 * T12: the header DIM GUARD, with a positive control.
 *
 * The header checksum is REPAIRED after patching dim, so this cannot pass merely
 * because the checksum noticed the edit — without that repair the test would be
 * vacuous and would still pass with the dim check deleted.
 * ================================================================ */
static void test_dim_guard(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    embed_store_t s;
    embed_store_init(&s, mock_read, mock_write, EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS);
    embed_rec_t r;
    make_rec(&r, 66u, 0x6666ULL, 9);
    embed_store_append(&s, &r);

    /* Positive control: re-init with everything intact must RESUME, so the test
     * below is measuring the dim field and nothing else. */
    embed_store_t ctrl;
    ASSERT(embed_store_init(&ctrl, mock_read, mock_write,
                            EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS) == 0, "control init");
    ASSERT(embed_store_count(&ctrl) == 1 && embed_store_boot_id(&ctrl) == 2,
           "positive control: an intact header RESUMES (count kept, boot_id bumped)");

    /* Patch dim (header word 5) to a different dimension, then REPAIR the checksum. */
    uint32_t other_dim = 256u;
    memcpy(mock_disk + 5 * 4, &other_dim, sizeof(other_dim));
    uint32_t repaired = hdr_checksum_of_mock();
    memcpy(mock_disk + 15 * 4, &repaired, sizeof(repaired));

    embed_store_t s2;
    ASSERT(embed_store_init(&s2, mock_read, mock_write,
                            EMBED_STORE_BASE_LBA, EMBED_STORE_MAX_VECS) == 0,
           "init over a dim-mismatched header succeeds");
    ASSERT(embed_store_count(&s2) == 0,
           "TEETH: a store written at another dimension is DISCARDED, never blended");
    ASSERT(embed_store_boot_id(&s2) == 1, "dim mismatch -> fresh store, boot_id 1");
    ASSERT(s2.hdr.dim == (uint32_t)EMBED_STORE_DIM, "the fresh header records THIS build's dim");
    PASS("T12 header dim guard (checksum repaired first, so the test is not vacuous)");
}

/* ================================================================
 * T13: region isolation + the real constants, by arithmetic.
 *
 * Mirrors test_episodic_store.c's T9. The two scaled-down regions are deliberately
 * ADJACENT — B starts one sector past A's last — because adjacency is the TIGHTEST
 * case: a one-sector overrun in the 2-sectors-per-record arithmetic corrupts B's
 * header here, where far-apart real bases would happily pass.
 * ================================================================ */
#define T13_BASE_A   EMBED_STORE_BASE_LBA
#define T13_ENTRIES  4U
#define T13_BASE_B   (T13_BASE_A + 1U + T13_ENTRIES * EMBED_STORE_SECTORS_PER_REC)

static void test_region_isolation(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));   /* ONE shared device backs both */

    embed_store_t a, b;
    ASSERT(embed_store_init(&a, mock_read, mock_write, T13_BASE_A, T13_ENTRIES) == 0, "init A");
    ASSERT(embed_store_init(&b, mock_read, mock_write, T13_BASE_B, T13_ENTRIES) == 0, "init B");
    ASSERT(embed_store_boot_id(&a) == 1 && embed_store_boot_id(&b) == 1,
           "both fresh: B's init was not fooled by A's header");
    ASSERT(embed_store_count(&a) == 0 && embed_store_count(&b) == 0, "both fresh: count 0");

    /* Interleave so an aliasing bug cannot hide behind ordering. A fills to its
     * last slot, which is what would overrun into B's header if the
     * 2-sectors-per-record arithmetic were wrong. */
    embed_rec_t r;
    for (uint32_t i = 0; i < T13_ENTRIES; i++) {
        make_rec(&r, 100u + i, 0xA000ULL + i, (int)(10 + i));
        ASSERT(embed_store_append(&a, &r) == 0, "A append");
        make_rec(&r, 200u + i, 0xB000ULL + i, (int)(20 + i));
        ASSERT(embed_store_append(&b, &r) == 0, "B append");
    }

    ASSERT(embed_store_count(&a) == T13_ENTRIES, "A count == its own appends");
    ASSERT(embed_store_count(&b) == T13_ENTRIES, "B count == its own appends");

    embed_rec_t back;
    ASSERT(embed_store_read(&a, T13_ENTRIES - 1, &back) == 0 && back.owner_seq == 103u,
           "A's last record is A's, not B's");
    ASSERT(embed_rec_is_valid(&back) == 1, "A's last record survived B's writes");
    ASSERT(embed_store_read(&b, 0, &back) == 0 && back.owner_seq == 200u,
           "B's first record is B's, not overwritten by A's last");
    ASSERT(embed_rec_is_valid(&back) == 1, "B's first record survived A's writes");

    /* ---- The REAL constants, checked by arithmetic (nothing is written here) ---- */
    ASSERT(EMBED_STORE_BASE_LBA == 21150000ULL, "real base is 21,150,000");
    ASSERT(EMBED_STORE_BASE_LBA % 8ULL == 0ULL,
           "real base is 8-sector aligned (family convention)");
    ASSERT(1ULL + (uint64_t)EMBED_STORE_MAX_VECS * EMBED_STORE_SECTORS_PER_REC == 8193ULL,
           "real size is 8193 sectors (1 header + 4096 x 2)");
    ASSERT(EMBED_STORE_BASE_LBA + 8193ULL - 1ULL == 21158192ULL,
           "real last sector is 21,158,192 inclusive");
    /* Below: the control-IN episodic store's last sector, from ITS header. */
    ASSERT(CTRL_EPI_BASE_LBA + 1ULL + CTRL_EPI_MAX_ENTRIES - 1ULL == 21144096ULL,
           "control-IN store ends at 21,144,096 inclusive");
    ASSERT(EMBED_STORE_BASE_LBA > CTRL_EPI_BASE_LBA + 1ULL + CTRL_EPI_MAX_ENTRIES - 1ULL,
           "this store starts ABOVE the control-IN store (no overlap)");
    /* Above: the partition floor CLAUDE.md records for the occupied 1.66 TiB gap. */
    ASSERT(EMBED_STORE_BASE_LBA + 8193ULL - 1ULL < 21200000ULL,
           "the whole region stays BELOW the 21,200,000 partition floor");
    /* And clear of every earlier sibling. */
    ASSERT(EMBED_STORE_BASE_LBA > EPI_STORE_BASE_LBA + 1ULL + EPI_STORE_MAX_ENTRIES - 1ULL,
           "clear of the workload episodic store");
    PASS("T13 region isolation (adjacent instances) + real constants by arithmetic");
}

int main(void)
{
    printf("=== JARVIS AI-OS: Embedding Vector Store Tests (Phase C / C/M2a) ===\n\n");
    test_fresh_init();
    test_append_roundtrip();
    test_reinit_persists();
    test_corrupt_header();
    test_wrap();
    test_put_idempotent();
    test_desync_match();
    test_desync_seq_differs();
    test_desync_key_differs();
    test_null_guards();
    test_checksum_spans_both_sectors();
    test_dim_guard();
    test_region_isolation();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
