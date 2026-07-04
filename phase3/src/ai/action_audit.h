/*
 * JARVIS AI-OS — Phase 6 K/M0: the Action-Audit Store
 *
 * A raw-LBA circular store of every action DECISION (executed / blocked /
 * proposed) — the durable evidence base for the it-acts spine and the 7-day
 * criterion ("SHIELD audit trail showing no Level 2+ actions taken without
 * approval" is answered by reading THIS store). Clones the proven
 * episodic_store / semantic_store pattern: header + fixed 512 B records, XOR
 * header checksum, boot_id, wrapping cursor + monotonic total, device-
 * independent read/write callbacks, flush-after-write.
 *
 * Storage region (PHASE_6_GOAL_K_IT_ACTS.md §5 — the THIRD sub-region of the
 * reserved Phase-5 memory gap, PHASE_5_PLAN.md §5):
 *     header @ ACT_AUDIT_BASE_LBA, records @ +1 .. +ACT_AUDIT_MAX_ENTRIES
 *     = 4097 sectors (~2 MiB), ending 21,124,096.
 * 8-sector-aligned (21,120,000 / 8 = 2,640,000); ~5,900 sectors clear of the
 * semantic store (ends ≈21,114,096). RESERVED — a future installer/repartition
 * must NOT overlap it. No device wiring until K/M1 (gated JARVIS_ACTIONS).
 *
 * Recovery:
 *   sudo dd if=/dev/nvme0n1 bs=512 skip=21120000 count=4097 \
 *     | python3 phase3/scripts/parse_action_audit.py
 */

#ifndef ACTION_AUDIT_H
#define ACTION_AUDIT_H

#include <stdint.h>
#include <stddef.h>

#define ACT_AUDIT_BASE_LBA     21120000ULL
#define ACT_AUDIT_MAX_ENTRIES  4096U
#define ACT_AUDIT_MAGIC        0x4A414354U   /* "JACT" */
#define ACT_AUDIT_VERSION      1U

/* Device-independent I/O callbacks (episodic_store style). 0 on success. */
typedef int (*audit_read_fn)(uint64_t lba, uint32_t count, void *buf);
typedef int (*audit_write_fn)(uint64_t lba, uint32_t count, const void *buf);

/* Header sector (512 bytes, only first 64 used) — mirrors epi_store_header_t. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t cursor;         /* circular WRITE slot (0..MAX-1) */
    uint32_t total_entries;  /* monotonic lifetime count */
    uint32_t boot_id;
    uint32_t reserved[10];
    uint32_t checksum;       /* XOR of words 0..14 */
} act_audit_header_t;

_Static_assert(sizeof(act_audit_header_t) <= 512, "audit header must fit one sector");

/* Verdicts (what the gate decided) */
#define AUDIT_EXECUTED  0
#define AUDIT_BLOCKED   1
#define AUDIT_PROPOSED  2   /* TRUST_REQUEST pre-control-IN: logged, never executed */

/* Outcomes (what happened AFTER an EXECUTED verdict; NA otherwise) */
#define AUDIT_OUT_OK    0
#define AUDIT_OUT_FAIL  1
#define AUDIT_OUT_NA    2

/* Audit record (exactly 512 bytes = 1 sector), packed.
 * trigger_snapshot is a bounded, length-carried text capture of the trigger
 * state — copied by trigger_len, never strlen'd, no NUL required. */
#define ACT_TRIGGER_MAX  456
typedef struct __attribute__((packed)) {
    uint32_t boot_id;        /* stamped at write */
    uint32_t seq;            /* stamped at write (== total_entries) */
    uint64_t t_ms;           /* boot-relative timestamp (caller-filled) */
    uint16_t action_id;
    uint16_t trust_level;    /* trust_level_t value (0..3) */
    uint16_t verdict;        /* AUDIT_* */
    uint16_t risk_x100;      /* shield_assess's score */
    uint16_t outcome;        /* AUDIT_OUT_* */
    uint16_t trigger_len;    /* bytes used in trigger_snapshot[] */
    char     trigger_snapshot[ACT_TRIGGER_MAX];
    uint8_t  pad[28];        /* pad to exactly 512 */
} action_audit_rec_t;

_Static_assert(sizeof(action_audit_rec_t) == 512, "audit record must be exactly one sector");

/* Store handle. */
typedef struct {
    audit_read_fn      read;
    audit_write_fn     write;
    uint64_t           base_lba;
    uint32_t           max_entries;
    act_audit_header_t hdr;
    int                initialized;
} act_audit_t;

/* Same semantics as epi_store_*: valid header -> boot_id++, stale-cursor clamp;
 * else fresh (boot_id 1). Flushes. 0 on success. */
int act_audit_init(act_audit_t *s, audit_read_fn rd, audit_write_fn wr,
                   uint64_t base_lba, uint32_t max_entries);

/* Append at cursor (stamps boot_id/seq at write time), advance, flush. */
int act_audit_append(act_audit_t *s, const action_audit_rec_t *rec);

/* Read the logical_index-th STORED record, oldest->newest. -1 past count. */
int act_audit_read(act_audit_t *s, uint32_t logical_index, action_audit_rec_t *rec);

int act_audit_flush(act_audit_t *s);
uint32_t act_audit_boot_id(const act_audit_t *s);
uint32_t act_audit_count(const act_audit_t *s);   /* stored, capped at max */

/* Fill a record (zeroes it; copies trigger by trigger_len — clamped to
 * ACT_TRIGGER_MAX; trigger may be NULL with len 0). boot_id/seq left 0 —
 * act_audit_append stamps them. */
void act_audit_fill(action_audit_rec_t *rec, uint32_t t_ms, uint16_t action_id,
                    uint16_t trust_level, uint16_t verdict, uint16_t risk_x100,
                    uint16_t outcome, const char *trigger, uint16_t trigger_len);

#endif /* ACTION_AUDIT_H */
