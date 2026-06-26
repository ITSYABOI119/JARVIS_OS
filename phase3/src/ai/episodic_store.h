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
#include <stddef.h>

/* Start of the Phase-5 memory region (8-sector-aligned: 21,100,000 / 8 = 2,637,500). */
#define EPI_STORE_BASE_LBA     21100000ULL
/* header + 8192 record slots = 8193 sectors (~4 MiB). Tunable; leaves the rest of the
 * reserved ~8 GiB for the semantic / SHIELD / consolidation sub-regions. */
#define EPI_STORE_MAX_ENTRIES  8192U
#define EPI_STORE_MAGIC        0x4A455049U   /* "JEPI" */
#define EPI_STORE_VERSION      1U

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

/* Outcome codes */
#define EPI_OUTCOME_OK       0
#define EPI_OUTCOME_ERROR    1
#define EPI_OUTCOME_BLOCKED  2

/* Episodic record (exactly 512 bytes = 1 sector), packed — the episodic schema. */
#define EPI_QUERY_MAX  200
#define EPI_RESP_MAX   256
typedef struct __attribute__((packed)) {
    uint32_t boot_id;
    uint32_t seq;            /* monotonic lifetime index (== total_entries at write) */
    uint64_t t_ms;           /* boot-relative TSC->ms timestamp (caller-filled; 0 on host) */
    uint64_t query_key;      /* cache_hash(cache_normalize_query(query)) — decision-cache parity */
    uint16_t action;         /* route/action code */
    uint8_t  outcome;        /* EPI_OUTCOME_* */
    uint8_t  feedback;       /* optional user feedback byte (0 = none) */
    uint16_t query_len;      /* bytes used in query[] */
    uint16_t resp_len;       /* bytes used in resp[] */
    char     query[EPI_QUERY_MAX];   /* truncated source query (raw; not NUL-required) */
    char     resp[EPI_RESP_MAX];     /* truncated response tail */
    uint8_t  pad[24];        /* pad to exactly 512 */
} epi_record_t;

_Static_assert(sizeof(epi_record_t) == 512, "episodic record must be exactly one sector");

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
 * total_entries++, flush the header. Circular: never fills, overwrites oldest. */
int epi_store_append(epi_store_t *s, const void *rec512);

/* Read the logical_index-th STORED record in wrap order oldest->newest
 * (0 = oldest retained). Returns -1 if logical_index >= epi_store_count(). */
int epi_store_read(epi_store_t *s, uint32_t logical_index, void *rec512);

/* Write the current header to disk (checksum stamped). */
int epi_store_flush(epi_store_t *s);

uint32_t epi_store_boot_id(const epi_store_t *s);
uint32_t epi_store_count(const epi_store_t *s);   /* entries stored, capped at max (rolling-full) */

/* ---- Episodic-typed helper: fills an epi_record_t (incl. the FNV query key) then appends ---- */
int episodic_log(epi_store_t *s, uint32_t boot_id, uint32_t t_ms,
                 const char *query, uint16_t action, uint8_t outcome, uint8_t feedback,
                 const char *resp);

#endif /* EPISODIC_STORE_H */
