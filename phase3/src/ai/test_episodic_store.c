/*
 * JARVIS AI-OS — Episodic Store Unit Tests (Phase 5 Goal #1 / M0)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -D_POSIX_C_SOURCE=200809L \
 *       -I phase3/src/ai \
 *       phase3/src/ai/test_episodic_store.c phase3/src/ai/episodic_store.c \
 *       phase3/src/ai/decision_cache.c \
 *       -o /tmp/test_episodic_store
 *
 * Device-independent: mock read/write callbacks below drive a mock buffer (no NVMe,
 * no main_x86.c wiring — that is M1). decision_cache.c is linked for the FNV-1a query
 * key (cache_normalize_query + cache_hash); the store reuses it, never duplicates it.
 */

#include "episodic_store.h"
#include "decision_cache.h"
#include <stdio.h>
#include <string.h>

/* ---- Mock storage: header + all EPI_STORE_MAX_ENTRIES slots (so the wrap test fits) ---- */
#define MOCK_SECTORS (EPI_STORE_MAX_ENTRIES + 1)
static uint8_t mock_disk[MOCK_SECTORS * 512];

static int mock_read(uint64_t lba, uint32_t count, void *buf)
{
    uint64_t offset = (lba - EPI_STORE_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk))
        return -1;
    memcpy(buf, mock_disk + offset, (size_t)count * 512);
    return 0;
}

static int mock_write(uint64_t lba, uint32_t count, const void *buf)
{
    uint64_t offset = (lba - EPI_STORE_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk))
        return -1;
    memcpy(mock_disk + offset, buf, (size_t)count * 512);
    return 0;
}

/* ---- Test helpers ---- */
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

/* Same checksum algorithm as episodic_store.c (for crafting a stale header in T5). */
static uint32_t test_compute_checksum(const epi_store_header_t *h)
{
    const uint8_t *raw = (const uint8_t *)h;
    uint32_t cs = 0;
    for (int i = 0; i < 15; i++) {
        uint32_t w;
        memcpy(&w, raw + i * 4, sizeof(w));
        cs ^= w;
    }
    return cs;
}

/* ================================================================
 * T1: Fresh init creates a valid header
 * ================================================================ */
static void test_fresh_init(void)
{
    memset(mock_disk, 0xFF, sizeof(mock_disk));   /* garbage disk */

    epi_store_t s;
    int rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "epi_store_init should return 0");

    epi_store_header_t hdr;
    memcpy(&hdr, mock_disk, sizeof(hdr));
    ASSERT(hdr.magic == EPI_STORE_MAGIC, "header magic should be JEPI");
    ASSERT(hdr.version == EPI_STORE_VERSION, "header version should be 1");
    ASSERT(hdr.boot_id == 1, "first boot_id should be 1");
    ASSERT(hdr.cursor == 0, "cursor should be 0");
    ASSERT(hdr.total_entries == 0, "total_entries should be 0");
    ASSERT(hdr.checksum == test_compute_checksum(&hdr), "checksum should be valid");
    ASSERT(epi_store_boot_id(&s) == 1, "epi_store_boot_id should be 1");
    ASSERT(epi_store_count(&s) == 0, "epi_store_count should be 0");

    PASS("test_fresh_init");
}

/* ================================================================
 * T2: episodic_log() round-trips field-exact through epi_store_read(),
 *     and query_key == cache_hash(cache_normalize_query(query))
 * ================================================================ */
static void test_append_readback(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));

    epi_store_t s;
    int rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "init should succeed");

    const char *queries[5] = { "hello world", "Status?", "what time is it",
                               "DELETE everything", "ping" };
    const char *resps[5]   = { "hi there", "ok", "noon", "blocked", "pong" };

    for (int i = 0; i < 5; i++) {
        rc = episodic_log(&s, (uint32_t)(1000 + i), queries[i], (uint16_t)(10 + i),
                          (uint8_t)(i % 3), (uint8_t)i, resps[i]);
        ASSERT(rc == 0, "episodic_log should succeed");
    }
    ASSERT(epi_store_count(&s) == 5, "count should be 5");

    for (int i = 0; i < 5; i++) {
        epi_record_t r;
        rc = epi_store_read(&s, (uint32_t)i, &r);   /* logical i = oldest->newest = write order */
        ASSERT(rc == 0, "epi_store_read should succeed");
        ASSERT(r.seq == (uint32_t)i, "seq matches write order");
        ASSERT(r.boot_id == epi_store_boot_id(&s), "boot_id stamped == store boot_id");
        ASSERT(r.t_ms == (uint64_t)(1000 + i), "t_ms field-exact");
        ASSERT(r.action == (uint16_t)(10 + i), "action field-exact");
        ASSERT(r.outcome == (uint8_t)(i % 3), "outcome field-exact");
        ASSERT(r.feedback == (uint8_t)i, "feedback field-exact");
        ASSERT(r.query_len == (uint16_t)strlen(queries[i]), "query_len field-exact");
        ASSERT(memcmp(r.query, queries[i], strlen(queries[i])) == 0, "query bytes field-exact");
        ASSERT(r.resp_len == (uint16_t)strlen(resps[i]), "resp_len field-exact");
        ASSERT(memcmp(r.resp, resps[i], strlen(resps[i])) == 0, "resp bytes field-exact");

        char norm[MAX_QUERY_LEN];
        ASSERT(cache_normalize_query(queries[i], norm, sizeof norm), "normalize should succeed");
        ASSERT(r.query_key == cache_hash(norm), "query_key == cache_hash(normalize(query))");
    }

    PASS("test_append_readback");
}

/* ================================================================
 * T3: boot_id increments on re-init
 * ================================================================ */
static void test_boot_id_increment(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));

    epi_store_t s;
    int rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "first init should succeed");
    ASSERT(epi_store_boot_id(&s) == 1, "first boot_id should be 1");

    rc = episodic_log(&s, 0, "boot 1", 1, EPI_OUT_OK, 0, NULL);
    ASSERT(rc == 0, "write should succeed");

    rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "second init should succeed");
    ASSERT(epi_store_boot_id(&s) == 2, "second boot_id should be 2");
    ASSERT(epi_store_count(&s) == 1, "count survives re-init (1 prior entry)");

    rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "third init should succeed");
    ASSERT(epi_store_boot_id(&s) == 3, "third boot_id should be 3");

    PASS("test_boot_id_increment");
}

/* ================================================================
 * T4: corrupted checksum forces a fresh header
 * ================================================================ */
static void test_checksum_corruption(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));

    epi_store_t s;
    int rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "init should succeed");

    /* Corrupt boot_id on disk without updating the checksum. */
    epi_store_header_t hdr;
    memcpy(&hdr, mock_disk, sizeof(hdr));
    ASSERT(hdr.magic == EPI_STORE_MAGIC, "header valid before corruption");
    hdr.boot_id = 99;
    memcpy(mock_disk, &hdr, sizeof(hdr));

    rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "init after corruption should succeed");
    ASSERT(epi_store_boot_id(&s) == 1, "boot_id should reset to 1 after corruption");

    PASS("test_checksum_corruption");
}

/* ================================================================
 * T5: circular / rolling buffer (wrap) + read-back wrap-order + stale-cursor migration
 * ================================================================ */
static void test_wrap_and_migration(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));

    epi_store_t s;
    int rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "init should succeed");

    const uint32_t MAX = EPI_STORE_MAX_ENTRIES;
    const uint32_t K = 5;
    for (uint32_t i = 0; i < MAX + K; i++) {
        rc = episodic_log(&s, 0, NULL, (uint16_t)(i & 0xFFFF), EPI_OUT_OK, 0, NULL);
        ASSERT(rc == 0, "(a) circular write never fails / never 'full'");
    }

    epi_store_header_t hdr;
    memcpy(&hdr, mock_disk, sizeof(hdr));
    ASSERT(hdr.total_entries == MAX + K, "(c) total_entries monotonic past MAX");
    ASSERT(hdr.cursor < MAX, "(b) cursor stays in [0, MAX)");
    ASSERT(hdr.cursor == (MAX + K) % MAX, "(b) cursor == total mod MAX");
    ASSERT(epi_store_count(&s) == MAX, "(d) count caps at MAX (rolling-full)");

    /* (e) slot 0 (LBA base+1) was overwritten by the wrap writer at lifetime index MAX. */
    epi_record_t r;
    memcpy(&r, mock_disk + 512, sizeof(r));
    ASSERT(r.seq == MAX, "(e) slot 0 overwritten by the wrap writer (seq == MAX)");

    /* (f) read-back wrap-order: logical 0 = oldest retained = seq K; last = newest = MAX+K-1. */
    rc = epi_store_read(&s, 0, &r);
    ASSERT(rc == 0 && r.seq == K, "(f) read(0) is the oldest retained (seq == K)");
    rc = epi_store_read(&s, MAX - 1, &r);
    ASSERT(rc == 0 && r.seq == MAX + K - 1, "(f) read(count-1) is the newest (seq == MAX+K-1)");
    rc = epi_store_read(&s, MAX, &r);
    ASSERT(rc == -1, "(f) read past count returns -1");

    /* Migration: a stale pre-wrap header (cursor == MAX) is clamped to 0 on init. */
    epi_store_header_t stale;
    memset(&stale, 0, sizeof(stale));
    stale.magic = EPI_STORE_MAGIC;
    stale.version = EPI_STORE_VERSION;
    stale.cursor = MAX;                 /* old "full" cursor */
    stale.total_entries = MAX;
    stale.boot_id = 5;
    stale.checksum = test_compute_checksum(&stale);
    memset(mock_disk, 0, sizeof(mock_disk));
    memcpy(mock_disk, &stale, sizeof(stale));

    rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "init with a stale full header should succeed");
    memcpy(&hdr, mock_disk, sizeof(hdr));
    ASSERT(hdr.cursor == 0, "stale cursor == MAX clamped to 0 on init");
    ASSERT(epi_store_boot_id(&s) == 6, "boot_id incremented from stale 5 -> 6");

    rc = episodic_log(&s, 0, "post-migration", 1, EPI_OUT_OK, 0, NULL);
    ASSERT(rc == 0, "post-migration write succeeds (no 'full')");
    memcpy(&r, mock_disk + 512, sizeof(r));   /* slot 0 */
    ASSERT(r.query_len == 14 && memcmp(r.query, "post-migration", 14) == 0,
           "post-migration write lands in slot 0");

    PASS("test_wrap_and_migration");
}

/* ================================================================
 * T6: query-key parity — raw variants that normalize to the same string share a key
 * ================================================================ */
static void test_key_parity(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));

    epi_store_t s;
    int rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "init should succeed");

    /* "Hello   World" and "  hello world  " both normalize to "hello world". */
    rc = episodic_log(&s, 0, "Hello   World", 1, EPI_OUT_OK, 0, NULL);
    ASSERT(rc == 0, "log variant A");
    rc = episodic_log(&s, 0, "  hello world  ", 1, EPI_OUT_OK, 0, NULL);
    ASSERT(rc == 0, "log variant B");

    epi_record_t a, b;
    ASSERT(epi_store_read(&s, 0, &a) == 0, "read A");
    ASSERT(epi_store_read(&s, 1, &b) == 0, "read B");
    ASSERT(a.query_key != 0, "key is nonzero");
    ASSERT(a.query_key == b.query_key, "raw variants that normalize equal share a query_key");

    char norm[MAX_QUERY_LEN];
    ASSERT(cache_normalize_query("hello world", norm, sizeof norm), "normalize");
    ASSERT(a.query_key == cache_hash(norm), "key == cache_hash(normalize) — decision-cache parity");

    PASS("test_key_parity");
}

/* ================================================================
 * T7: batched fill — episodic_fill() into a RAM batch, then epi_store_append()
 *     each (the M1 commit path). append stamps seq (0,1,2) + boot_id at write time.
 * ================================================================ */
static void test_batched_fill(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));

    epi_store_t s;
    int rc = epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES);
    ASSERT(rc == 0, "init should succeed");

    const char *q[3]   = { "alpha query", "Beta   Query", "gamma" };
    const char *rsp[3] = { "a-resp", NULL, "g-resp" };

    /* Fill into a RAM batch — episodic_fill leaves boot_id/seq = 0 (stamped at append). */
    epi_record_t batch[3];
    for (int i = 0; i < 3; i++) {
        episodic_fill(&batch[i], (uint32_t)(100 + i), q[i], EPI_ACT_CACHE, EPI_OUT_OK, 0, rsp[i]);
        ASSERT(batch[i].boot_id == 0 && batch[i].seq == 0, "fill leaves boot_id/seq = 0");
    }

    /* Commit the batch. */
    for (int i = 0; i < 3; i++)
        ASSERT(epi_store_append(&s, &batch[i]) == 0, "append batch record");
    ASSERT(epi_store_count(&s) == 3, "count should be 3");

    for (int i = 0; i < 3; i++) {
        epi_record_t r;
        ASSERT(epi_store_read(&s, (uint32_t)i, &r) == 0, "read back");
        ASSERT(r.seq == (uint32_t)i, "append stamped seq 0,1,2");
        ASSERT(r.boot_id == epi_store_boot_id(&s), "append stamped boot_id == store boot_id");
        ASSERT(r.t_ms == (uint64_t)(100 + i), "t_ms field-exact");
        ASSERT(r.action == EPI_ACT_CACHE, "action field-exact");
        ASSERT(r.outcome == EPI_OUT_OK, "outcome field-exact");
        ASSERT(r.query_len == (uint16_t)strlen(q[i]), "query_len field-exact");
        ASSERT(memcmp(r.query, q[i], strlen(q[i])) == 0, "query bytes field-exact");
        if (rsp[i]) {
            ASSERT(r.resp_len == (uint16_t)strlen(rsp[i]), "resp_len field-exact");
            ASSERT(memcmp(r.resp, rsp[i], strlen(rsp[i])) == 0, "resp bytes field-exact");
        } else {
            ASSERT(r.resp_len == 0, "NULL resp -> resp_len 0");
        }
        char norm[MAX_QUERY_LEN];
        ASSERT(cache_normalize_query(q[i], norm, sizeof norm), "normalize");
        ASSERT(r.query_key == cache_hash(norm), "query_key parity (decision-cache FNV-1a)");
    }

    PASS("test_batched_fill");
}

/* ================================================================
 * --dump <path>: write a FIXED, documented fixture to a raw region file
 *   (header sector + record sectors) for the C->Python round-trip
 *   (phase3/scripts/test_parse_episodic.py). This MUST stay byte-identical
 *   to that script's FIXTURE table. Runs INSTEAD of the test suite.
 *
 *   Fixture (5 records) covers: both actions (CACHE + INFER), an OK and an
 *   ERROR outcome, a NULL resp (rec2) and non-NULL resps, a mixed-case +
 *   extra-spaces query (rec0) to exercise key normalization, and a known
 *   nonzero t_ms per record (100..500).
 * ================================================================ */
static int dump_fixture(const char *path)
{
    memset(mock_disk, 0, sizeof(mock_disk));   /* fresh region -> boot_id = 1 */

    epi_store_t s;
    if (epi_store_init(&s, mock_read, mock_write, EPI_STORE_BASE_LBA, EPI_STORE_MAX_ENTRIES) != 0) {
        printf("dump: epi_store_init failed\n");
        return 1;
    }

    episodic_log(&s, 100, "  Hello   World  ", EPI_ACT_CACHE, EPI_OUT_OK,    0, "hi there");
    episodic_log(&s, 200, "status check",      EPI_ACT_INFER, EPI_OUT_OK,    0, "all systems nominal");
    episodic_log(&s, 300, "DELETE everything", EPI_ACT_CACHE, EPI_OUT_ERROR, 1, NULL);
    episodic_log(&s, 400, "what time is it",   EPI_ACT_INFER, EPI_OUT_OK,    0, "noon");
    episodic_log(&s, 500, "PING",              EPI_ACT_CACHE, EPI_OUT_OK,    2, "pong");

    uint32_t total = s.hdr.total_entries;
    size_t region = (size_t)(1 + total) * 512;   /* header sector + `total` record sectors */

    FILE *f = fopen(path, "wb");
    if (!f) {
        printf("dump: cannot open %s\n", path);
        return 1;
    }
    size_t wrote = fwrite(mock_disk, 1, region, f);
    fclose(f);
    if (wrote != region) {
        printf("dump: short write (%zu of %zu)\n", wrote, region);
        return 1;
    }
    printf("dumped %u records to %s\n", total, path);
    return 0;
}

/* G3/M5a: the pure in-RAM key→logical_index lookup (newest-wins, miss/empty -> -1). */
static void test_index_lookup(void)
{
    epi_index_entry_t idx[5] = {
        { 0xAAAAu, 3u },
        { 0xBBBBu, 7u },
        { 0xAAAAu, 9u },   /* dup key 0xAAAA, HIGHER logical_index (newer) */
        { 0xCCCCu, 5u },
        { 0xAAAAu, 1u },   /* dup key 0xAAAA, lower logical_index (older) */
    };
    ASSERT(epi_index_lookup(idx, 0, 0xAAAAu) == -1, "empty index (n=0) -> -1");
    ASSERT(epi_index_lookup(NULL, 5, 0xAAAAu) == -1, "NULL index -> -1");
    ASSERT(epi_index_lookup(idx, 5, 0xDEADu) == -1, "miss -> -1");
    ASSERT(epi_index_lookup(idx, 5, 0xBBBBu) == 7, "single hit -> its logical_index");
    ASSERT(epi_index_lookup(idx, 5, 0xCCCCu) == 5, "single hit (other key) -> its logical_index");
    ASSERT(epi_index_lookup(idx, 5, 0xAAAAu) == 9, "DUP keys -> the HIGHER (newest) logical_index");
    PASS("epi_index_lookup: empty/-1, miss/-1, single hit, dup -> newest");
}

/* ================================================================
 * T9 (6-5/M5-recall): TWO INSTANCES AT DIFFERENT base_lba DO NOT COLLIDE.
 *
 * The control-IN store is a second epi_store_t at CTRL_EPI_BASE_LBA sharing the SAME
 * EPI_STORE_MAGIC — separation is by REGION, not by magic (parameterizing the magic
 * would change episodic_store.c's object code and break OFF-identity). This test is
 * what replaces a distinct magic: it pins that two handles over ONE shared device
 * keep independent headers, cursors and records.
 *
 * Deliberately uses ADJACENT scaled-down regions (B starts one sector past A's last)
 * rather than the real 40,000-sector gap: adjacency is the TIGHTEST case, so a
 * one-sector overrun in the base_lba+1+slot arithmetic FAILS here, where far-apart
 * real bases would happily pass. The real constants are checked separately below, by
 * arithmetic.
 * ================================================================ */
#define T9_BASE_A   EPI_STORE_BASE_LBA
#define T9_ENTRIES  4U
#define T9_BASE_B   (T9_BASE_A + 1U + T9_ENTRIES)   /* immediately after A's last record */

static void test_two_instances_no_collision(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));   /* ONE shared device backs both instances */

    epi_store_t a, b;
    ASSERT(epi_store_init(&a, mock_read, mock_write, T9_BASE_A, T9_ENTRIES) == 0, "init A");
    ASSERT(epi_store_init(&b, mock_read, mock_write, T9_BASE_B, T9_ENTRIES) == 0, "init B");

    /* Both are fresh, so both start at boot 1 / count 0 — B's init must not have been
     * fooled into "resuming" A's header (they share a magic; only base_lba differs). */
    ASSERT(epi_store_boot_id(&a) == 1 && epi_store_boot_id(&b) == 1, "both fresh: boot_id 1");
    ASSERT(epi_store_count(&a) == 0 && epi_store_count(&b) == 0, "both fresh: count 0");

    /* Interleave writes so an aliasing bug cannot hide behind ordering. */
    ASSERT(episodic_log(&a, 10, "alpha one", EPI_ACT_INFER, EPI_OUT_OK, 0, "A-1") == 0, "A append 1");
    ASSERT(episodic_log(&b, 20, "bravo one", EPI_ACT_CONTROL_IN, EPI_OUT_OK, 0, "B-1") == 0, "B append 1");
    ASSERT(episodic_log(&a, 30, "alpha two", EPI_ACT_INFER, EPI_OUT_OK, 0, "A-2") == 0, "A append 2");
    ASSERT(episodic_log(&b, 40, "bravo two", EPI_ACT_CONTROL_IN, EPI_OUT_OK, 0, "B-2") == 0, "B append 2");
    ASSERT(episodic_log(&b, 50, "bravo three", EPI_ACT_CONTROL_IN, EPI_OUT_OK, 0, "B-3") == 0, "B append 3");

    /* Headers advanced independently — neither store saw the other's writes. */
    ASSERT(epi_store_count(&a) == 2, "A count == its own 2 appends");
    ASSERT(epi_store_count(&b) == 3, "B count == its own 3 appends");
    ASSERT(a.hdr.cursor == 2 && b.hdr.cursor == 3, "cursors independent");

    /* Records read back are each store's OWN, in its own order, with its own seq lineage. */
    epi_record_t r;
    const char *a_exp[2] = { "A-1", "A-2" };
    for (uint32_t i = 0; i < 2; i++) {
        ASSERT(epi_store_read(&a, i, &r) == 0, "A read");
        ASSERT(r.seq == i, "A seq lineage is its own");
        ASSERT(r.action == EPI_ACT_INFER, "A record is A's tag");
        ASSERT(r.resp_len == 3 && memcmp(r.resp, a_exp[i], 3) == 0, "A record bytes intact");
    }
    const char *b_exp[3] = { "B-1", "B-2", "B-3" };
    for (uint32_t i = 0; i < 3; i++) {
        ASSERT(epi_store_read(&b, i, &r) == 0, "B read");
        ASSERT(r.seq == i, "B seq lineage is its own");
        ASSERT(r.action == EPI_ACT_CONTROL_IN, "B record is B's tag");
        ASSERT(r.resp_len == 3 && memcmp(r.resp, b_exp[i], 3) == 0, "B record bytes intact");
    }

    /* Wrapping A must not spill into B's region (A's last record sector is adjacent to
     * B's header — an off-by-one in the wrap would corrupt exactly that). */
    for (int i = 0; i < 6; i++)
        ASSERT(episodic_log(&a, 60, "alpha churn", EPI_ACT_INFER, EPI_OUT_OK, 0, "A-x") == 0, "A churn");
    ASSERT(a.hdr.total_entries == 8 && epi_store_count(&a) == T9_ENTRIES, "A rolled");
    ASSERT(epi_store_count(&b) == 3, "B count untouched by A's wrap");
    for (uint32_t i = 0; i < 3; i++) {
        ASSERT(epi_store_read(&b, i, &r) == 0, "B read after A wrap");
        ASSERT(r.resp_len == 3 && memcmp(r.resp, b_exp[i], 3) == 0, "B record survives A's wrap");
    }

    /* Re-init B off the shared device: it must resume ITS OWN lineage (boot 2, count 3),
     * not adopt A's header — the reboot path of the real two-store deployment. */
    ASSERT(epi_store_init(&b, mock_read, mock_write, T9_BASE_B, T9_ENTRIES) == 0, "re-init B");
    ASSERT(epi_store_boot_id(&b) == 2, "B resumes its own boot_id");
    ASSERT(epi_store_count(&b) == 3, "B resumes its own count");

    /* The REAL deployed constants must not overlap: episodic occupies
     * [EPI_STORE_BASE_LBA, +1+MAX_ENTRIES) and control-IN starts past it. */
    ASSERT(CTRL_EPI_BASE_LBA >= EPI_STORE_BASE_LBA + 1ULL + EPI_STORE_MAX_ENTRIES,
           "real regions: control-IN starts after episodic ends");
    ASSERT(CTRL_EPI_MAX_ENTRIES > 0, "real control-IN region is non-empty");

    PASS("two instances at different base_lba: no collision (shared magic, separate regions)");
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--dump") == 0)
        return dump_fixture(argv[2]);

    printf("=== Episodic Store Tests (Phase 5 G1/M1 + G3/M5a + 6-5/M5-recall) ===\n");

    test_fresh_init();
    test_append_readback();
    test_boot_id_increment();
    test_checksum_corruption();
    test_wrap_and_migration();
    test_key_parity();
    test_batched_fill();
    test_index_lookup();
    test_two_instances_no_collision();

    printf("\nResults: %d PASS, %d FAIL\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
