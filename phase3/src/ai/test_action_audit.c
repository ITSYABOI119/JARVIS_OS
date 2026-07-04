/*
 * JARVIS AI-OS — Action-Audit Store Unit Tests (Phase 6 K/M0)
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *       phase3/src/ai/test_action_audit.c phase3/src/ai/action_audit.c \
 *       -o /tmp/test_action_audit
 *
 * Device-independent: mock read/write callbacks over a RAM buffer (no NVMe —
 * box wiring is K/M1+). --dump <path> writes the FIXED fixture for the
 * C->Python round-trip (phase3/scripts/test_parse_action_audit.py) — it MUST
 * stay byte-identical to that script's FIXTURE table.
 */

#include "action_audit.h"
#include <stdio.h>
#include <string.h>

/* ---- Mock storage: header + all ACT_AUDIT_MAX_ENTRIES slots ---- */
#define MOCK_SECTORS (ACT_AUDIT_MAX_ENTRIES + 1)
static uint8_t mock_disk[MOCK_SECTORS * 512];

static int mock_read(uint64_t lba, uint32_t count, void *buf)
{
    uint64_t offset = (lba - ACT_AUDIT_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk)) return -1;
    memcpy(buf, mock_disk + offset, (size_t)count * 512);
    return 0;
}

static int mock_write(uint64_t lba, uint32_t count, const void *buf)
{
    uint64_t offset = (lba - ACT_AUDIT_BASE_LBA) * 512;
    if (offset + (uint64_t)count * 512 > sizeof(mock_disk)) return -1;
    memcpy(mock_disk + offset, buf, (size_t)count * 512);
    return 0;
}

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", msg, __LINE__); tests_failed++; return; } \
} while (0)

#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while (0)

static void test_fresh_init(void)
{
    _Static_assert(sizeof(action_audit_rec_t) == 512, "audit record must be one sector");
    memset(mock_disk, 0xFF, sizeof(mock_disk));   /* garbage disk */
    act_audit_t s;
    ASSERT(act_audit_init(&s, mock_read, mock_write, ACT_AUDIT_BASE_LBA, ACT_AUDIT_MAX_ENTRIES) == 0,
           "fresh init succeeds");
    ASSERT(act_audit_boot_id(&s) == 1, "fresh boot_id == 1");
    ASSERT(act_audit_count(&s) == 0, "fresh count == 0");
    ASSERT(s.hdr.magic == ACT_AUDIT_MAGIC, "JACT magic stamped");
    ASSERT(s.hdr.cursor == 0, "cursor == 0");
    PASS("T1 fresh init (JACT magic, boot_id 1, cursor/total 0)");
}

static void test_append_roundtrip(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    act_audit_t s;
    act_audit_init(&s, mock_read, mock_write, ACT_AUDIT_BASE_LBA, ACT_AUDIT_MAX_ENTRIES);

    /* Trigger with an EMBEDDED NUL — copied by trigger_len, never strlen'd. */
    static const char trig[] = "PB heartbeat\0age 12000 ms";
    action_audit_rec_t rec;
    act_audit_fill(&rec, 1234, 1 /*action_id*/, 1 /*TRUST_NOTIFY*/, AUDIT_EXECUTED,
                   10 /*risk*/, AUDIT_OUT_OK, trig, (uint16_t)(sizeof(trig) - 1));
    ASSERT(act_audit_append(&s, &rec) == 0, "append succeeds");
    ASSERT(act_audit_count(&s) == 1, "count == 1");

    action_audit_rec_t back;
    ASSERT(act_audit_read(&s, 0, &back) == 0, "read logical 0");
    ASSERT(back.boot_id == 1 && back.seq == 0, "boot_id/seq stamped at write");
    ASSERT(back.t_ms == 1234 && back.action_id == 1 && back.trust_level == 1,
           "t_ms/action_id/trust round-trip");
    ASSERT(back.verdict == AUDIT_EXECUTED && back.risk_x100 == 10 && back.outcome == AUDIT_OUT_OK,
           "verdict/risk/outcome round-trip");
    ASSERT(back.trigger_len == sizeof(trig) - 1, "trigger_len round-trips");
    ASSERT(memcmp(back.trigger_snapshot, trig, sizeof(trig) - 1) == 0,
           "trigger bytes exact incl. the embedded NUL (length-based, no strlen)");
    ASSERT(act_audit_read(&s, 1, &back) == -1, "read past count fails");
    PASS("T2 append + read-back round-trip (embedded-NUL trigger, stamps)");
}

static void test_boot_id_and_corruption(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    act_audit_t s;
    act_audit_init(&s, mock_read, mock_write, ACT_AUDIT_BASE_LBA, ACT_AUDIT_MAX_ENTRIES);
    action_audit_rec_t rec;
    act_audit_fill(&rec, 1, 2, 0, AUDIT_EXECUTED, 0, AUDIT_OUT_OK, "x", 1);
    act_audit_append(&s, &rec);

    act_audit_t s2;   /* "reboot" over the same disk */
    ASSERT(act_audit_init(&s2, mock_read, mock_write, ACT_AUDIT_BASE_LBA, ACT_AUDIT_MAX_ENTRIES) == 0,
           "re-init succeeds");
    ASSERT(act_audit_boot_id(&s2) == 2, "boot_id bumped to 2");
    ASSERT(act_audit_count(&s2) == 1, "record survives re-init");

    mock_disk[8] ^= 0xFF;   /* corrupt a header word -> XOR checksum mismatch */
    act_audit_t s3;
    ASSERT(act_audit_init(&s3, mock_read, mock_write, ACT_AUDIT_BASE_LBA, ACT_AUDIT_MAX_ENTRIES) == 0,
           "init over corrupt header succeeds");
    ASSERT(act_audit_boot_id(&s3) == 1 && act_audit_count(&s3) == 0,
           "corrupt header -> fresh store (boot_id 1, count 0)");
    PASS("T3 boot_id increments across re-init; XOR corruption -> fresh header");
}

static void test_wrap(void)
{
    memset(mock_disk, 0, sizeof(mock_disk));
    act_audit_t s;
    act_audit_init(&s, mock_read, mock_write, ACT_AUDIT_BASE_LBA, 4);   /* tiny store */
    action_audit_rec_t rec, back;
    for (uint32_t i = 1; i <= 6; i++) {
        act_audit_fill(&rec, i * 10, (uint16_t)i, 0, AUDIT_EXECUTED, 0, AUDIT_OUT_OK, NULL, 0);
        ASSERT(act_audit_append(&s, &rec) == 0, "wrap append succeeds");
    }
    ASSERT(act_audit_count(&s) == 4, "count capped at max (rolling-full)");
    ASSERT(s.hdr.total_entries == 6, "total_entries monotonic (6)");
    ASSERT(act_audit_read(&s, 0, &back) == 0 && back.action_id == 3,
           "oldest retained == record 3 (1-2 overwritten)");
    ASSERT(act_audit_read(&s, 3, &back) == 0 && back.action_id == 6, "newest == record 6");
    PASS("T4 circular wrap past capacity (oldest overwritten, total monotonic)");
}

/* ================================================================
 * --dump <path>: the FIXED fixture for the C->Python round-trip. MUST stay
 * byte-identical to test_parse_action_audit.py's FIXTURE table. Covers all
 * three verdicts, all three outcomes, three trust levels, a benign trigger,
 * an empty (NULL) trigger, and a nonzero t_ms per record.
 * ================================================================ */
static int dump_fixture(const char *path)
{
    memset(mock_disk, 0, sizeof(mock_disk));   /* fresh region -> boot_id = 1 */
    act_audit_t s;
    if (act_audit_init(&s, mock_read, mock_write, ACT_AUDIT_BASE_LBA, ACT_AUDIT_MAX_ENTRIES) != 0) {
        printf("dump: act_audit_init failed\n");
        return 1;
    }

    action_audit_rec_t rec;
    act_audit_fill(&rec, 100, 1, 1, AUDIT_EXECUTED, 10, AUDIT_OUT_OK,
                   "PB heartbeat age 12000 ms", 25);
    act_audit_append(&s, &rec);
    act_audit_fill(&rec, 200, 2, 0, AUDIT_EXECUTED, 0, AUDIT_OUT_OK,
                   "q_errors delta 3", 16);
    act_audit_append(&s, &rec);
    act_audit_fill(&rec, 300, 0xFFFE, 3, AUDIT_BLOCKED, 100, AUDIT_OUT_NA,
                   "induced block probe", 19);
    act_audit_append(&s, &rec);
    act_audit_fill(&rec, 400, 1, 2, AUDIT_PROPOSED, 60, AUDIT_OUT_FAIL, NULL, 0);
    act_audit_append(&s, &rec);

    uint32_t total = s.hdr.total_entries;
    size_t region = (size_t)(1 + total) * 512;
    FILE *f = fopen(path, "wb");
    if (!f) { printf("dump: cannot open %s\n", path); return 1; }
    size_t wrote = fwrite(mock_disk, 1, region, f);
    fclose(f);
    if (wrote != region) { printf("dump: short write\n"); return 1; }
    printf("dumped %u records to %s\n", total, path);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--dump") == 0)
        return dump_fixture(argv[2]);

    printf("=== JARVIS AI-OS: Action-Audit Store Tests (Phase 6 K/M0) ===\n\n");
    test_fresh_init();
    test_append_roundtrip();
    test_boot_id_and_corruption();
    test_wrap();
    printf("\n== %d PASS, %d FAIL ==\n", tests_passed, tests_failed);
    return tests_failed ? 1 : 0;
}
