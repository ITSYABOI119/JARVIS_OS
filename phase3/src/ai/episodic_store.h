/*
 * JARVIS AI-OS — Phase 5 Goal #1 (KEYSTONE): Episodic Memory Store
 *
 * A raw-LBA circular store cloning the proven `nvme_log` pattern (header at base
 * LBA + fixed 512-byte records at +1..+N, XOR header checksum, boot_id, circular
 * cursor + monotonic total_entries, flush-after-every-write), but STRUCT-based and
 * read/write-CALLBACK-driven (fat32 style) so it has NO device dependency:
 *   - the host test drives mock callbacks over a mock buffer;
 *   - M1 later passes main_x86.c wrappers that close over the NVMe controller
 *     (exactly like fat32_nvme_read);
 *   - the later semantic / SHIELD-state stores reuse this same core at their own
 *     base_lba (separate epi_store_t instances — no collision).
 *
 * Storage region (PHASE_5_PLAN.md §5, VERIFIED on-box 2026-06-26 — disk =
 * 4,000,797,360 sectors, Lexar NM790 2TB): the Phase-5 memory region is a carved
 * raw-LBA span in the ~1.66 TiB FREE GAP after JARVIS_DATA (p2 ends @ 21,094,399).
 * Episodic is the FIRST sub-region:
 *     header @ EPI_STORE_BASE_LBA, records @ +1 .. +EPI_STORE_MAX_ENTRIES
 *     = 8193 sectors (~4 MiB).
 * The rest of the reserved ~8 GiB is left for the later semantic / SHIELD-state /
 * consolidation sub-regions. The region is ~3.56 BILLION sectors clear of Ubuntu
 * (p3 @ 3,581,364,224) and the tail telemetry log (@ 4,000,794,624) — zero
 * collision risk. This region is RESERVED: a future installer/repartition must NOT
 * overlap it (see PHASE_5_PLAN.md §8). No device wiring happens until M1.
 */

#ifndef EPISODIC_STORE_H
#define EPISODIC_STORE_H

#include <stdint.h>
#include <stddef.h>   /* offsetof — the provenance-offset _Static_asserts below */
#include <stddef.h>

/* Start of the Phase-5 memory region (8-sector-aligned: 21,100,000 / 8 = 2,637,500). */
#define EPI_STORE_BASE_LBA     21100000ULL
/* header + 8192 record slots = 8193 sectors (~4 MiB). Tunable; leaves the rest of the
 * reserved ~8 GiB for the semantic / SHIELD / consolidation sub-regions. */
#define EPI_STORE_MAX_ENTRIES  8192U
#define EPI_STORE_MAGIC        0x4A455049U   /* "JEPI" */
#define EPI_STORE_VERSION      1U

/* ---- 6-5/M5-recall: the DEDICATED control-IN episodic store ----
 * A SECOND INSTANCE of this same engine at its own base_lba — no engine change, no new magic.
 *
 * WHY it exists: control-IN turns used to share the store above, which the synthetic workload
 * churns at ~225 q/s. At 8192 slots that wrap-evicts a control-IN turn in ~36 SECONDS — measured
 * on hardware, where the M5-recall mini-flip aborted with ZERO tag-3 records surviving (7.0
 * complete wraps). Cross-session recall cannot exist on that substrate. Control-IN is human-paced,
 * so its own region never churns and recall decouples from workload volume.
 *
 * SEPARATION IS BY REGION, NOT BY MAGIC: the magic is a fixed #define shared by every instance
 * (parameterizing it would change episodic_store.c's object code and break the OFF-identity
 * discipline). Two instances cannot collide because base_lba is carried per-handle on epi_store_t
 * and the regions do not overlap — episodic occupies 21,100,000..21,108,192 (8193 sectors) and
 * this store 21,140,000..21,144,096 (4097 sectors), clear of semantic (21,110,000) and JACT
 * (21,120,000). The non-collision property is host-pinned by test_episodic_store.c's T9.
 * RESERVED like its siblings: a future installer/repartition must NOT overlap it. */
#define CTRL_EPI_BASE_LBA      21140000ULL   /* dedicated control-IN episodic store */
#define CTRL_EPI_MAX_ENTRIES   4096U         /* header + 4096 slots = 4097 sectors; last @ 21,144,096 */

/* Device-independent I/O callbacks (fat32 style). Return 0 on success, <0 on error.
 * Each call transfers `count` 512-byte sectors at absolute `lba`. */
typedef int (*epi_read_fn)(uint64_t lba, uint32_t count, void *buf);
typedef int (*epi_write_fn)(uint64_t lba, uint32_t count, const void *buf);

/* Header sector (512 bytes, only first 64 used) — mirrors nvme_log_header_t exactly. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t cursor;         /* circular WRITE slot (0..MAX-1; record i at LBA base+1+i) */
    uint32_t total_entries;  /* monotonic lifetime count */
    uint32_t boot_id;
    uint32_t reserved[10];
    uint32_t checksum;       /* XOR of words 0..14 */
} epi_store_header_t;

_Static_assert(sizeof(epi_store_header_t) <= 512, "episodic header must fit one sector");

/* Action / route codes — the routes that produce a memory */
#define EPI_ACT_CACHE       1
#define EPI_ACT_INFER       2
/* THE RECALL PREDICATE IS "WHO PRODUCED THE ANSWER", and these two values are how it
 * is expressed. An answer the MODEL generated is a memory; an answer PA RENDERED --
 * from live box state (SYSFACTS) or a canned string (DECLINE) -- is not.
 *
 * Measured 2026-07-26 over the real control-IN store (62 records): 9 were the canned
 * "I don't track that." and ALL NINE recalled, because 19 bytes with a full stop clears
 * the complete-sentence rule trivially. Two of those nine are the 6-6 routing FALSE
 * POSITIVES from the aborted flip, so a variant of that question could have a
 * known-wrong answer injected as its remembered context. A further 12 were SYSFACTS
 * ("up 476 seconds") which decline only BY ACCIDENT -- nothing intends them to be
 * unrecallable, they simply lack a terminator. Phrase one "Uptime is 476 seconds." and
 * stale box state becomes recallable.
 *
 * Tag 4 is a PREREQUISITE for C/M2, not hygiene: once semantic matching drops the
 * exact-key requirement, a stale system fact can land on an unrelated question. */
#define EPI_ACT_CONTROL_IN  3   /* 6-5/M3-2a: an inbound control-IN Q&A. Excluded from cache-growth
                                 * promotion + retrieval-sourcing + semantic distill BY THE TAG VALUE
                                 * ALONE (all three filter == EPI_ACT_INFER) — the O-Q12 store-
                                 * contamination surface is closed by construction, no extra filter. */
#define EPI_ACT_CONTROL_IN_LOCAL 4  /* an inbound control-IN turn PA answered ITSELF — 6-6 SYSFACTS
                                     * (rendered from live box state) or the canned DECLINE. Written
                                     * and auditable like any other turn, so the corpus is preserved
                                     * and C/M2 can consult these later if it ever wants them; simply
                                     * NEVER RECALLABLE. Costs no record-format change: `action` is
                                     * already uint16 and the record stays exactly 512 B.
                                     *
                                     * It works AT THE BOOT SCAN, which is the hard half — the scan
                                     * rebuilds the index from stored bytes and cannot know what route
                                     * a turn took, but the tag is STORED, so it filters correctly
                                     * with no extra state.
                                     *
                                     * A FAILED turn (EPI_OUT_ERROR — degraded/timeout/fault) stays
                                     * tag 3 whatever its route: it has no answer at all, and tagging
                                     * it 4 would conflate "answered from its own state" with "failed
                                     * to answer". Different facts. */

/* Outcome codes */
#define EPI_OUT_OK       0
#define EPI_OUT_ERROR    1
#define EPI_OUT_BLOCKED  2

/* Episodic record (exactly 512 bytes = 1 sector), packed — the episodic schema. */
#define EPI_QUERY_MAX  200
#define EPI_RESP_MAX   256
typedef struct __attribute__((packed)) {
    uint32_t boot_id;
    uint32_t seq;            /* monotonic lifetime index (== total_entries at write) */
    uint64_t t_ms;           /* boot-relative TSC->ms timestamp (caller-filled; 0 on host) */
    uint64_t query_key;      /* cache_hash(cache_normalize_query(query)) — decision-cache parity */
    uint16_t action;         /* route/action code */
    uint8_t  outcome;        /* EPI_OUT_* */
    uint8_t  feedback;       /* optional user feedback byte (0 = none) */
    uint16_t query_len;      /* bytes used in query[] */
    uint16_t resp_len;       /* bytes used in resp[] */
    char     query[EPI_QUERY_MAX];   /* truncated source query (raw; not NUL-required) */
    char     resp[EPI_RESP_MAX];     /* response head (first <=256 B) */
    /* ---- RECALL PROVENANCE (2026-08-01) — carved out of the former pad[24] --------------
     * WHICH record supplied this turn's preamble, recorded ON THE TURN rather than in a log.
     *
     * WHY HERE AND NOT A LOG LINE: boot 48 recorded sem_recall_hits=4 and durable recall=4,
     * and WHICH FOUR was unrecoverable — [CTRL-RECALL]/[CTRL-SEM] are puts_serial and vanish
     * at the deployed JARVIS_DBG_BOOT_LOG=0. The answer text cannot settle it either: every
     * answer named the right concept, which the model would do with or without a hit. This
     * record is ALREADY written on every control-IN turn (no new writes), it is co-located
     * with the turn it describes, and this store is 4096 slots at human pace — years —
     * against the telemetry log's 2700-entry rolling buffer that would age the evidence out.
     *
     * BYTE ARITHMETIC: 488 B of fields precede these. 1 + 4 + 2 = 7 B, so pad 24 -> 17 and
     * the record stays EXACTLY 512 B — the _Static_assert below is UNCHANGED, deliberately:
     * it is the thing protecting the on-disk layout, so it must not move. Offsets are
     * recall_kind @488, recall_src_seq @489, recall_cos_x1000 @493, pad @495..511. The
     * struct is __attribute__((packed)), so recall_src_seq is deliberately unaligned and no
     * compiler padding is inserted; parse_episodic.py reads the same three offsets.
     *
     * BACKWARD COMPATIBILITY, STATED NOT PAPERED OVER: records written before this change
     * have pad all-zero, which decodes as recall_kind = 0. For those turns that is AMBIGUOUS
     * — it could mean "no recall" or "written before the field existed" — and boot 46
     * genuinely had recall=2, so some of those zeros ARE wrong. There is deliberately NO
     * version byte: boot_id identifies the era, and parse_episodic.py renders an all-zero
     * provenance block as UNKNOWN rather than asserting "none". Note the one thing that DOES
     * disambiguate going forward: a semantic MISS now stores a non-zero cosine, so a
     * non-zero value anywhere in this block proves the record is post-change. */
    uint8_t  recall_kind;      /* @488 — 0 = none/unknown · 1 = exact-key · 2 = semantic */
    uint32_t recall_src_seq;   /* @489 — seq of the record that supplied the preamble; 0 when none */
    uint16_t recall_cos_x1000; /* @493 — BEST cosine seen x1000, hit OR miss (0 on the exact-key path).
                                * Recorded even on a miss ON PURPOSE: it makes near-misses readable
                                * straight off the store. Boot 48's paraphrase scored 0.494 against a
                                * 0.55 floor and it took a bespoke probe to learn that. */
    uint8_t  pad[17];        /* pad to exactly 512 */
} epi_record_t;

_Static_assert(sizeof(epi_record_t) == 512, "episodic record must be exactly one sector");
/* Pin the provenance offsets explicitly. The sizeof assert above proves the record still fits a
 * sector, but it would ALSO be satisfied by silently reordering these fields — which would
 * misread every previously-written record. These asserts are what make the layout the contract. */
_Static_assert(offsetof(epi_record_t, recall_kind)      == 488, "recall_kind must be @488");
_Static_assert(offsetof(epi_record_t, recall_src_seq)   == 489, "recall_src_seq must be @489");
_Static_assert(offsetof(epi_record_t, recall_cos_x1000) == 493, "recall_cos_x1000 must be @493");

/* recall_kind values */
#define EPI_RECALL_NONE     0u   /* no preamble — or a pre-2026-08-01 record (ambiguous; see above) */
#define EPI_RECALL_EXACT    1u   /* exact-key hit supplied the preamble */
#define EPI_RECALL_SEMANTIC 2u   /* semantic (cosine) match supplied the preamble */

/* Store handle — holds the callbacks, region geometry, and the cached header. */
typedef struct {
    epi_read_fn        read;
    epi_write_fn       write;
    uint64_t           base_lba;
    uint32_t           max_entries;
    epi_store_header_t hdr;
    int                initialized;
} epi_store_t;

/* ---- Core API (clones nvme_log.c's circular logic; device-independent) ---- */

/* Read the header via the read callback. If valid (magic+version+checksum), bump
 * boot_id, clamp a stale cursor (%= max), continue; else create a fresh header
 * (boot_id = 1). Flushes the header. Returns 0 on success, <0 on error. */
int epi_store_init(epi_store_t *s, epi_read_fn rd, epi_write_fn wr,
                   uint64_t base_lba, uint32_t max_entries);

/* Write a 512-byte record at the current cursor slot, advance cursor %= max,
 * total_entries++, flush the header. Circular: never fills, overwrites oldest.
 * STAMPS the written record's boot_id (= header boot_id) and seq (= header
 * total_entries) at write time, so callers (e.g. episodic_fill) leave them 0. */
int epi_store_append(epi_store_t *s, const void *rec512);

/* Read the logical_index-th STORED record in wrap order oldest->newest
 * (0 = oldest retained). Returns -1 if logical_index >= epi_store_count(). */
int epi_store_read(epi_store_t *s, uint32_t logical_index, void *rec512);

/* Write the current header to disk (checksum stamped). */
int epi_store_flush(epi_store_t *s);

uint32_t epi_store_boot_id(const epi_store_t *s);
uint32_t epi_store_count(const epi_store_t *s);   /* entries stored, capped at max (rolling-full) */

/* ---- Episodic-typed helpers ---- */

/* Fill an epi_record_t from the given fields: zeroes it; query_key =
 * cache_hash(cache_normalize_query(query)); copies the truncated query (<=200) and
 * resp (<=256, may be NULL) with their *_len. Leaves boot_id/seq = 0 — epi_store_append
 * stamps those at write time. Lets a caller batch records in RAM, then commit the batch. */
void episodic_fill(epi_record_t *rec, uint32_t t_ms, const char *query, uint16_t action,
                   uint8_t outcome, uint8_t feedback, const char *resp);

/* Convenience: episodic_fill(...) into a local record, then epi_store_append(...). */
int episodic_log(epi_store_t *s, uint32_t t_ms, const char *query, uint16_t action,
                 uint8_t outcome, uint8_t feedback, const char *resp);

/* ---- G3/M5: in-RAM key→record index (post-reboot recall) ---- */

/* One index entry: a record's decision-cache key + its logical_index in the store. */
typedef struct { uint64_t key; uint32_t logical_index; } epi_index_entry_t;

/* Newest (highest logical_index) entry whose key == `key` → its logical_index; -1 on miss.
 * Pure in-RAM linear scan — host-testable, no device dependency. The caller builds `idx` once at
 * boot (one epi_store_read per stored record), then per query does an O(1)-ish lookup + ONE bounded
 * epi_store_read of the matched record — NEVER a per-query O(N) NVMe scan. */
int epi_index_lookup(const epi_index_entry_t *idx, int n, uint64_t key);

#endif /* EPISODIC_STORE_H */
