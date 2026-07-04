/*
 * JARVIS AI-OS — Semantic Fact Store Unit Tests (Phase 5 Goal #4 / M0)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_semantic_store.c phase3/src/ai/semantic_store.c \
 *       -o /tmp/test_semantic_store
 *
 * Device-independent: mock read/write callbacks drive a mock buffer (no NVMe —
 * box wiring is M1). Keys are caller-provided u64s here; the FNV-1a parity with
 * the decision cache is proven in test_semantic_distill.c (which builds records
 * via the real episodic_fill).
 */

#include "semantic_store.h"
#include <stdio.h>
#include <string.h>

/* ---- Mock storage: header + all SEM_STORE_MAX_FACTS slots ---- */
#define MOCK_SECTORS (SEM_STORE_MAX_FACTS + 1)
static uint8_t mock_disk[MOCK_SECTORS * 512];

static int mock_read(uint64_t lba, uint32_t count, void *buf)
{
    uint64_t offset = (lba - SEM_STORE_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk))
        return -1;
    memcpy(buf, mock_disk + offset, (size_t)count * 512);
    return 0;
}

static int mock_write(uint64_t lba, uint32_t count, const void *buf)
{
    uint64_t offset = (lba - SEM_STORE_BASE_LBA) * 512;
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

/* Build a fact in-place (boot_id/seq left 0 — the store stamps at write). */
static void make_fact(semantic_fact_t *f, uint64_t key, uint16_t support,
                      const char *text)
{
    memset(f, 0, sizeof(*f));
    f->key           = key;
    f->fact_type     = SEM_FACT_QA;
    f->support_count = support;
    f->confidence_x100 = 100;
    if (text) {
        size_t tl = strlen(text);
        if (tl > SEM_FACT_TEXT_MAX) tl = SEM_FACT_TEXT_MAX;
        memcpy(f->text, text, tl);
        f->text_len = (uint16_t)tl;
    }
}

/* ================================================================
 * T1: layout + fresh init on a garbage disk
 * ================================================================ */
static void test_fresh_init(void)
{
    ASSERT(sizeof(semantic_fact_t) == 512, "semantic_fact_t is exactly 512 bytes");
    ASSERT(sizeof(sem_store_header_t) <= 512, "header fits one sector");

    memset(mock_disk, 0xFF, sizeof(mock_disk));   /* garbage disk */
    sem_store_t s;
    int rc = sem_store_init(&s, mock_read, mock_write, SEM_STORE_BASE_LBA, SEM_STORE_MAX_FACTS);
    ASSERT(rc == 0, "fresh init succeeds");
    ASSERT(sem_store_boot_id(&s) == 1, "fresh boot_id == 1");
    ASSERT(sem_store_count(&s) == 0, "fresh count == 0");
    PASS("T1 fresh init (layout + garbage disk -> fresh header, boot_id 1)");
}

/* ================================================================
 * T2: append + read-back round-trip; boot_id/seq stamped at write
 * ================================================================ */
static void test_append_roundtrip(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    sem_store_t s;
    sem_store_init(&s, mock_read, mock_write, SEM_STORE_BASE_LBA, SEM_STORE_MAX_FACTS);

    semantic_fact_t f;
    make_fact(&f, 0xABCDEF0123456789ULL, 3, "the sel4 microkernel is formally verified");
    ASSERT(sem_store_append(&s, &f) == 0, "append succeeds");
    ASSERT(sem_store_count(&s) == 1, "count == 1 after append");

    semantic_fact_t back;
    ASSERT(sem_store_read(&s, 0, &back) == 0, "read logical 0 succeeds");
    ASSERT(back.key == 0xABCDEF0123456789ULL, "key round-trips");
    ASSERT(back.support_count == 3, "support_count round-trips");
    ASSERT(back.fact_type == SEM_FACT_QA, "fact_type round-trips");
    ASSERT(back.text_len == f.text_len, "text_len round-trips");
    ASSERT(memcmp(back.text, f.text, back.text_len) == 0, "text round-trips");
    ASSERT(back.boot_id == 1, "boot_id stamped at write");
    ASSERT(back.seq == 0, "seq stamped (== total_entries at write)");
    ASSERT(sem_store_read(&s, 1, &back) == -1, "read past count fails");
    PASS("T2 append + read-back round-trip (boot_id/seq stamped)");
}

/* ================================================================
 * T3: re-init bumps boot_id; facts persist ("reboot" on the mock disk)
 * ================================================================ */
static void test_reinit_persists(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    sem_store_t s;
    sem_store_init(&s, mock_read, mock_write, SEM_STORE_BASE_LBA, SEM_STORE_MAX_FACTS);
    semantic_fact_t f;
    make_fact(&f, 111, 3, "fact one");
    sem_store_append(&s, &f);
    make_fact(&f, 222, 4, "fact two");
    sem_store_append(&s, &f);

    sem_store_t s2;   /* "reboot": fresh handle over the same disk */
    ASSERT(sem_store_init(&s2, mock_read, mock_write, SEM_STORE_BASE_LBA, SEM_STORE_MAX_FACTS) == 0,
           "re-init succeeds");
    ASSERT(sem_store_boot_id(&s2) == 2, "boot_id bumped to 2");
    ASSERT(sem_store_count(&s2) == 2, "both facts survive");
    semantic_fact_t back;
    ASSERT(sem_store_read(&s2, 1, &back) == 0 && back.key == 222, "newest fact intact");
    make_fact(&f, 333, 3, "fact three");
    ASSERT(sem_store_append(&s2, &f) == 0 && sem_store_count(&s2) == 3,
           "append after re-init works");
    ASSERT(sem_store_read(&s2, 2, &back) == 0 && back.boot_id == 2,
           "post-reboot fact stamped with the new boot_id");
    PASS("T3 re-init bumps boot_id, facts persist");
}

/* ================================================================
 * T4: corrupt header checksum -> fresh header (no crash, boot_id 1)
 * ================================================================ */
static void test_corrupt_header(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    sem_store_t s;
    sem_store_init(&s, mock_read, mock_write, SEM_STORE_BASE_LBA, SEM_STORE_MAX_FACTS);
    semantic_fact_t f;
    make_fact(&f, 444, 3, "doomed fact");
    sem_store_append(&s, &f);

    mock_disk[8] ^= 0xFF;   /* corrupt a header byte (cursor word) — checksum now wrong */

    sem_store_t s2;
    ASSERT(sem_store_init(&s2, mock_read, mock_write, SEM_STORE_BASE_LBA, SEM_STORE_MAX_FACTS) == 0,
           "init over a corrupt header succeeds");
    ASSERT(sem_store_boot_id(&s2) == 1, "corrupt header -> fresh boot_id 1");
    ASSERT(sem_store_count(&s2) == 0, "corrupt header -> count reset");
    PASS("T4 corrupt header checksum -> fresh header");
}

/* ================================================================
 * T5: circular wrap (small max) — oldest overwritten, count capped
 * ================================================================ */
static void test_wrap(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    sem_store_t s;
    sem_store_init(&s, mock_read, mock_write, SEM_STORE_BASE_LBA, 4);   /* tiny store */

    semantic_fact_t f;
    char txt[16];
    for (int i = 1; i <= 6; i++) {
        snprintf(txt, sizeof txt, "fact %d", i);
        make_fact(&f, (uint64_t)i, 3, txt);
        ASSERT(sem_store_append(&s, &f) == 0, "wrap append succeeds");
    }
    ASSERT(sem_store_count(&s) == 4, "count capped at max (rolling-full)");

    semantic_fact_t back;
    ASSERT(sem_store_read(&s, 0, &back) == 0 && back.key == 3,
           "oldest retained == fact 3 (facts 1-2 overwritten)");
    ASSERT(sem_store_read(&s, 3, &back) == 0 && back.key == 6, "newest == fact 6");
    PASS("T5 circular wrap (oldest overwritten, count capped)");
}

/* ================================================================
 * T6: upsert — insert-or-raise-support; a repeated subject is never a dup
 * ================================================================ */
static void test_upsert(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    sem_store_t s;
    sem_store_init(&s, mock_read, mock_write, SEM_STORE_BASE_LBA, SEM_STORE_MAX_FACTS);

    semantic_fact_t f, back;
    make_fact(&f, 777, 3, "answer v1");
    ASSERT(sem_store_upsert(&s, &f) == 0, "new key -> inserted (returns 0)");
    ASSERT(sem_store_count(&s) == 1, "count 1 after insert");

    make_fact(&f, 777, 5, "answer v2 (newer)");
    ASSERT(sem_store_upsert(&s, &f) == 1, "existing key -> updated (returns 1)");
    ASSERT(sem_store_count(&s) == 1, "count STILL 1 (no dup)");
    ASSERT(sem_store_read(&s, 0, &back) == 0, "read back updated fact");
    ASSERT(back.support_count == 5, "support raised to 5");
    ASSERT(back.text_len == strlen("answer v2 (newer)") &&
           memcmp(back.text, "answer v2 (newer)", back.text_len) == 0,
           "text updated to the newest answer");

    make_fact(&f, 777, 2, "answer v3");
    ASSERT(sem_store_upsert(&s, &f) == 1, "lower-support upsert still updates");
    sem_store_read(&s, 0, &back);
    ASSERT(back.support_count == 5, "support is MONOTONIC (max) — never lowered");
    ASSERT(memcmp(back.text, "answer v3", 9) == 0, "text still follows the newest answer");

    make_fact(&f, 888, 3, "different subject");
    ASSERT(sem_store_upsert(&s, &f) == 0, "different key -> inserted");
    ASSERT(sem_store_count(&s) == 2, "count 2 (two distinct subjects)");
    PASS("T6 upsert (insert-or-raise-support, monotonic, no dups)");
}

int main(void)
{
    printf("=== JARVIS AI-OS: Semantic Fact Store Tests (Phase 5 #4/M0) ===\n\n");
    test_fresh_init();
    test_append_roundtrip();
    test_reinit_persists();
    test_corrupt_header();
    test_wrap();
    test_upsert();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
