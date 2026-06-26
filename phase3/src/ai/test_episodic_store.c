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
        rc = episodic_log(&s, 1, (uint32_t)(1000 + i), queries[i], (uint16_t)(10 + i),
                          (uint8_t)(i % 3), (uint8_t)i, resps[i]);
        ASSERT(rc == 0, "episodic_log should succeed");
    }
    ASSERT(epi_store_count(&s) == 5, "count should be 5");

    for (int i = 0; i < 5; i++) {
        epi_record_t r;
        rc = epi_store_read(&s, (uint32_t)i, &r);   /* logical i = oldest->newest = write order */
        ASSERT(rc == 0, "epi_store_read should succeed");
        ASSERT(r.seq == (uint32_t)i, "seq matches write order");
        ASSERT(r.boot_id == 1, "boot_id should be 1");
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

    rc = episodic_log(&s, 1, 0, "boot 1", 1, EPI_OUTCOME_OK, 0, NULL);
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
        rc = episodic_log(&s, 1, 0, NULL, (uint16_t)(i & 0xFFFF), EPI_OUTCOME_OK, 0, NULL);
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

    rc = episodic_log(&s, 6, 0, "post-migration", 1, EPI_OUTCOME_OK, 0, NULL);
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
    rc = episodic_log(&s, 1, 0, "Hello   World", 1, EPI_OUTCOME_OK, 0, NULL);
    ASSERT(rc == 0, "log variant A");
    rc = episodic_log(&s, 1, 0, "  hello world  ", 1, EPI_OUTCOME_OK, 0, NULL);
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

int main(void)
{
    printf("=== Episodic Store Tests (Phase 5 G1/M0) ===\n");

    test_fresh_init();
    test_append_readback();
    test_boot_id_increment();
    test_checksum_corruption();
    test_wrap_and_migration();
    test_key_parity();

    printf("\nResults: %d PASS, %d FAIL\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
