/*
 * JARVIS AI-OS — Phase 5 Goal #4: Semantic Fact Store
 *
 * A SEPARATE raw-LBA circular store for distilled semantic facts, cloning the
 * proven episodic_store pattern (header at base LBA + fixed 512-byte records at
 * +1..+N, XOR header checksum, boot_id, circular cursor + monotonic
 * total_entries, flush-after-every-write, device-independent read/write
 * callbacks). Distinct from #6's decision-cache promotion: the cache serves
 * repeats FAST (volatile, HWM-capped); this store keeps distilled facts DURABLE
 * as a future retrieval SOURCE (G3 reads "episodic + semantic" — a later slice).
 *
 * HONEST CEILING (PHASE_5_GOAL4_SEMANTIC_MEMORY.md §2): a fact here is "this
 * exact question was asked >= N times and its newest consistent answer was X" —
 * an OBSERVABLE PATTERN compacted from the episodic log by the deterministic
 * distill (semantic_distill.h). Nothing here "knows preferences" or
 * "understands"; no LLM, no embeddings (those are Phase 7).
 *
 * Storage region (PHASE_5_PLAN.md §5 — the reserved Phase-5 gap): the SECOND
 * sub-region, clear of episodic (EPI @ 21,100,000 + 8193 sectors -> ends
 * 21,108,192):
 *     header @ SEM_STORE_BASE_LBA, facts @ +1 .. +SEM_STORE_MAX_FACTS
 *     = 4097 sectors (~2 MiB), ending ~21,114,096.
 * RESERVED like the episodic region — a future installer/repartition must NOT
 * overlap it. No device wiring until M1 (gated JARVIS_SEMANTIC, default 0).
 */

#ifndef SEMANTIC_STORE_H
#define SEMANTIC_STORE_H

#include <stdint.h>
#include <stddef.h>

/* Start of the semantic sub-region (8-sector-aligned: 21,110,000 / 8 = 2,638,750). */
#define SEM_STORE_BASE_LBA   21110000ULL
#define SEM_STORE_MAX_FACTS  4096U
#define SEM_STORE_MAGIC      0x4A53454DU   /* "JSEM" */
#define SEM_STORE_VERSION    1U

/* Device-independent I/O callbacks (episodic_store/fat32 style). Return 0 on
 * success, <0 on error. Each call transfers `count` 512-byte sectors at `lba`. */
typedef int (*sem_read_fn)(uint64_t lba, uint32_t count, void *buf);
typedef int (*sem_write_fn)(uint64_t lba, uint32_t count, const void *buf);

/* Header sector (512 bytes, only first 64 used) — mirrors epi_store_header_t. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t cursor;         /* circular WRITE slot (0..MAX-1; fact i at LBA base+1+i) */
    uint32_t total_entries;  /* monotonic lifetime count */
    uint32_t boot_id;
    uint32_t reserved[10];
    uint32_t checksum;       /* XOR of words 0..14 */
} sem_store_header_t;

_Static_assert(sizeof(sem_store_header_t) <= 512, "semantic header must fit one sector");

/* Fact types */
#define SEM_FACT_QA  1   /* a recurring Q&A pattern (the M0 distill's only type) */

/* Semantic fact (exactly 512 bytes = 1 sector), packed. */
#define SEM_FACT_TEXT_MAX  440
typedef struct __attribute__((packed)) {
    uint32_t boot_id;            /* stamped at write (0 from the distill) */
    uint32_t seq;                /* stamped at write (== total_entries; kept on upsert) */
    uint64_t t_ms;               /* the chosen (newest) source record's timestamp */
    uint64_t key;                /* FNV-1a of the subject query — decision-cache parity */
    uint16_t fact_type;          /* SEM_FACT_* */
    uint16_t support_count;      /* usable source records seen (monotonic across upserts) */
    uint16_t confidence_x100;    /* % of usable same-key answers identical to the chosen one */
    uint16_t text_len;           /* bytes used in text[] */
    char     text[SEM_FACT_TEXT_MAX];  /* the distilled fact text (head-copied, *_len — no NUL required) */
    uint8_t  pad[40];            /* pad to exactly 512 */
} semantic_fact_t;

_Static_assert(sizeof(semantic_fact_t) == 512, "semantic fact must be exactly one sector");

/* Store handle — callbacks, region geometry, cached header. */
typedef struct {
    sem_read_fn        read;
    sem_write_fn       write;
    uint64_t           base_lba;
    uint32_t           max_entries;
    sem_store_header_t hdr;
    int                initialized;
} sem_store_t;

/* ---- Core API (identical semantics to epi_store_*; device-independent) ---- */

/* Read the header via the read callback. If valid (magic+version+checksum), bump
 * boot_id, clamp a stale cursor (%= max), continue; else create a fresh header
 * (boot_id = 1). Flushes the header. Returns 0 on success, <0 on error. */
int sem_store_init(sem_store_t *s, sem_read_fn rd, sem_write_fn wr,
                   uint64_t base_lba, uint32_t max_entries);

/* Write a 512-byte fact at the current cursor slot, advance cursor %= max,
 * total_entries++, flush the header. Circular: never fills, overwrites oldest.
 * STAMPS the written fact's boot_id (= header boot_id) and seq (= header
 * total_entries) at write time — the distill leaves them 0. */
int sem_store_append(sem_store_t *s, const semantic_fact_t *fact);

/* Read the logical_index-th STORED fact in wrap order oldest->newest
 * (0 = oldest retained). Returns -1 if logical_index >= sem_store_count(). */
int sem_store_read(sem_store_t *s, uint32_t logical_index, semantic_fact_t *fact);

/* Insert-or-raise-support: if a stored fact with fact->key exists, UPDATE it in
 * place (text/t_ms/fact_type/confidence follow the incoming — newest fact wins;
 * support_count = max(existing, incoming) — MONOTONIC and idempotent, so a boot
 * re-distill over the same records can never double-count; seq keeps the
 * existing fact's identity; boot_id restamped to the current boot). A repeated
 * subject updates its fact, never a dup. Otherwise append (stamps boot_id/seq).
 * Linear-scans the store via the read callback (low-cadence distill use ONLY —
 * never a hot path). Returns 0 = inserted, 1 = updated, <0 = error. */
int sem_store_upsert(sem_store_t *s, const semantic_fact_t *fact);

/* Write the current header to disk (checksum stamped). */
int sem_store_flush(sem_store_t *s);

uint32_t sem_store_boot_id(const sem_store_t *s);
uint32_t sem_store_count(const sem_store_t *s);   /* facts stored, capped at max (rolling-full) */

#endif /* SEMANTIC_STORE_H */
