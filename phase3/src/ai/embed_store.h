/*
 * JARVIS AI-OS — Phase C / C/M2a: Embedding Vector Store
 *
 * A raw-LBA circular store holding ONE embedding vector per control-IN episodic
 * record, so semantic recall does not have to re-embed the corpus at boot.
 *
 * WHY IT EXISTS (the arithmetic, not a preference): an embed costs ~190 M
 * cycles/token (C/M1b-2, box-measured) — roughly 2 s per short query. Re-embedding
 * a full 4096-record control-IN store at every boot is therefore HOURS. The vectors
 * must be durable.
 *
 * Cloned from the proven family: semantic_store.c/h, itself an episodic_store clone
 * — header sector at base_lba, fixed-size records after it, XOR checksum, boot_id,
 * circular cursor + monotonic total_entries, flush-after-every-write, and
 * device-independent read/write CALLBACKS (no nvme.h here; the host test passes
 * mocks, the box would pass main_x86.c's epi_nvme_read/epi_nvme_write).
 *
 * ---------------------------------------------------------------------------
 * STATUS: STORAGE ONLY. NOTHING READS OR WRITES THIS STORE.
 * ---------------------------------------------------------------------------
 * There is no init call site, no PA/PB wiring, and no device I/O anywhere in this
 * milestone — the same M0 shape semantic_store shipped in. `JARVIS_EMBED` is 0.
 * The producer (embed-on-write) and the consumer (g3_select_semantic over these
 * vectors) are both C/M2b.
 *
 *
 * ============================ THE LAYOUT ============================
 *
 * TWO SECTORS PER RECORD (1024 B), and the reason is a hard constraint rather
 * than a preference:
 *
 *   The measured dimension is 128 (C/M2, af00495 — cm2_floor_results.json), and
 *   128 x float32 is EXACTLY 512 B. A one-sector record is therefore entirely
 *   vector, with zero room for anything saying WHICH episodic record it belongs to.
 *
 *   Positional-only mapping (embed slot i <-> episodic slot i) is the tempting
 *   answer and it is the wrong one. Both stores hold 4096 slots, so it works until
 *   one store writes and the other does not — a failed write, or a boot that
 *   appends to one and not the other. From then on every vector is attributed to
 *   the wrong record, SILENTLY. That is a false recall carrying a confidently wrong
 *   answer, and it walks straight past the similarity floor, because the vector is
 *   perfectly valid — it is simply some other record's. The whole C/M2 floor
 *   measurement exists to bound false recall; an unfalsifiable positional
 *   assumption underneath it would make that bound meaningless.
 *
 *   So each record carries the owning record's `seq` and `query_key` (both already
 *   on epi_record_t, at episodic_store.h:127 and :129), and desync becomes
 *   DETECTABLE PER RECORD instead of assumed. The cost is 2 MiB of a 1.66 TiB gap.
 *
 * Alternatives considered and rejected, so they are not re-proposed:
 *   - One shared metadata sector per N vectors (halves the space): a torn write
 *     then orphans N records at once instead of one, and every vector write
 *     becomes two non-adjacent I/Os. Strictly worse at the exact property this
 *     layout exists to protect.
 *   - float16 vectors (256 B, so vector + metadata fit one sector): changes the
 *     numerics. The 0.55 floor was measured on float32; quantising perturbs the
 *     cosines and deployed behaviour would then differ from the measurement that
 *     justified the threshold.
 *   - Fewer dimensions to make room: 128 is MEASURED, not chosen. Truncating
 *     further invalidates the sweep that picked it.
 *
 *
 * ================== SECTOR ORDER: PUBLISH-LAST ==================
 *
 * The vector occupies sector +0 and the identifying metadata sector +1, so a
 * single write(lba, 2, rec) emits the VECTOR FIRST and the IDENTITY LAST.
 *
 * That ordering is load-bearing, and the reason is a property of the callbacks
 * rather than of this module: epi_nvme_write implements count>1 as a LOOP of
 * single-sector NVMe commands through one shared bounce buffer
 * (main_x86.c:332-343). A 2-sector record is therefore two commands, not one
 * atomic transfer, and there is a window between them. Writing the identity last
 * is the same release-store discipline embed_region.c uses for `ready` and
 * shared_context.c uses for the preamble: the thing that says "this is valid and
 * it belongs to record N" becomes visible only after the payload it describes.
 *
 * HONEST LIMIT, because this is a durability claim and overstating it would be
 * worse than not making it: command ORDER is guaranteed (the loop is sequential
 * and polled), but DURABILITY order across a cold power loss is not — nothing
 * flushes between the two sectors, and the drive's volatile write cache may
 * reorder them. That is why the checksum spans BOTH sectors (see below): a torn
 * pair is DETECTED, not prevented. Detection is sufficient here — an invalid
 * record is skipped, which degrades to "this record has no vector", which falls
 * back to g3_select_exact_only. The safe direction.
 *
 * AND IT DELIBERATELY DOES NOT nvme_flush. That primitive exists and is already
 * wired for the control-IN replay floor (6-5/M4c-fix), so using it here would be
 * a two-line change; it is left out on the following reasoning, recorded because
 * "why is there no flush" is the obvious question to ask of the paragraph above.
 *
 * The replay floor flushes because losing it reopens a SECURITY window — an
 * already-answered sequence number becomes replayable. Losing a vector costs a
 * RECALL: that record falls back to exact-key matching, which is exactly what the
 * whole JARVIS_EMBED=0 path does today and is therefore a state the system is
 * already correct in. Paying a synchronous cache flush on every vector write to
 * protect a value whose loss degrades into current behaviour is the wrong trade,
 * and it would be paid on the write path of a bulk backfill.
 *
 * The asymmetry is the point: flush what cannot be reconstructed and whose loss
 * changes the security posture; do not flush what can be recomputed and whose
 * loss costs quality. A lost vector can be re-embedded.
 *
 *
 * ==================== CHECKSUM SPANS BOTH HALVES ====================
 *
 * The family's stores checksum only their HEADER, because their records are one
 * sector and a record is therefore all-or-nothing. This one is not, so the
 * per-record checksum covers the vector bytes AND the metadata fields. A stale
 * vector half under fresh metadata (or the reverse) fails it. A checksum over the
 * metadata alone would call a torn record valid — precisely the failure this
 * layout was chosen to make visible.
 *
 *
 * ==================== REGION (verify, do not trust) ====================
 *
 *   base            21,150,000   (8-sector aligned: /8 = 2,643,750, family convention)
 *   size            1 header + 4096 x 2 = 8,193 sectors (4.00 MiB)
 *   last sector     21,158,192   inclusive
 *   below           control-IN episodic ends 21,144,096 -> 5,903 sectors unused between
 *   above           the 21,200,000 partition floor      -> 41,807 sectors unused between
 *
 * That floor is not advisory. CLAUDE.md records that the 1.66 TiB "free space" gap
 * on the box is OCCUPIED by every raw-LBA store, and that anything ever partitioned
 * there must start above 21,200,000. This store stays below it. Full map:
 *
 *   episodic  21,100,000 .. 21,108,192      JACT      21,120,000 .. 21,124,096
 *   semantic  21,110,000 .. 21,114,096      ctrl key  21,130,000 .. 21,130,003
 *   ctrl-epi  21,140,000 .. 21,144,096      EMBED     21,150,000 .. 21,158,192
 *
 * RESERVED like its siblings: a future installer or repartition must not overlap
 * it. Separation from the siblings is BY REGION, not by magic — the same property
 * test_episodic_store.c's T9 pins for the two episodic instances.
 *
 *
 * ============ 128 HERE vs 1024 IN THE TRANSPORT — BOTH CORRECT ============
 *
 * ipc/embed_region.h defines EMBED_DIM 1024. This header defines
 * EMBED_STORE_DIM 128. THEY MUST NOT BE RECONCILED TO ONE VALUE.
 *
 * The pipeline is: embed(1024) -> mean-project IN 1024 -> truncate to 128 -> L2
 * renormalise. The projection artifact `mu` is a frozen 1024-dimension vector
 * (phase3/scripts/embed/golden_vectors.npz), so projection MUST happen at 1024 —
 * therefore the PA<->PB transport carries 1024 and truncation is strictly
 * downstream of it. What is STORED and compared is the 128-dim result, because
 * that is the configuration the floor was measured in.
 *
 * The two headers are deliberately independent: the transport does not know what
 * the store keeps, and the store does not know how the vector was produced.
 *
 *
 * ================== HONEST CEILING (carried from C/M2) ==================
 *
 * The floor these vectors are compared at (0.55) was measured with ZERO false
 * recalls observed on a 36-query hand-authored set with an adversarial tail. That
 * is NO FAILURES OBSERVED, NOT A ZERO RATE — the 95% upper bound on 0/36 is about
 * 8%. Nothing here prevents, eliminates, or zeroes false recall. See
 * g3_retrieval.h, which carries the same caveat next to the floor itself.
 */

#ifndef EMBED_STORE_H
#define EMBED_STORE_H

#include <stdint.h>
#include <stddef.h>

/* Start of the embedding sub-region (8-sector-aligned: 21,150,000 / 8 = 2,643,750). */
#define EMBED_STORE_BASE_LBA     21150000ULL
#define EMBED_STORE_MAX_VECS     4096U
#define EMBED_STORE_MAGIC        0x4A564543U   /* "JVEC" — family style: J + 3 */
#define EMBED_STORE_VERSION      1U

/* Sectors per record. TWO — see "THE LAYOUT" above; this is not a tunable. */
#define EMBED_STORE_SECTORS_PER_REC  2U

/* The STORED dimension. 128 is MEASURED (cm2_floor_results.json), not chosen, and
 * it is NOT the transport's EMBED_DIM 1024 — see the header comment. */
#define EMBED_STORE_DIM          128

/* Record status. Distinct names from embed_region.h's EMBED_STATUS_* so the two
 * headers can be included together without either being mistaken for the other. */
#define EMBED_STORE_ST_UNSET     0U
#define EMBED_STORE_ST_VALID     1U

/* Device-independent I/O callbacks (episodic_store/semantic_store/fat32 style).
 * Return 0 on success, <0 on error. Each call transfers `count` 512-byte sectors
 * at absolute `lba`. THE CALLER DOES THE I/O — this module never touches a device. */
typedef int (*embed_read_fn)(uint64_t lba, uint32_t count, void *buf);
typedef int (*embed_write_fn)(uint64_t lba, uint32_t count, const void *buf);

/* Header sector (512 bytes, only the first 64 used) — mirrors sem_store_header_t,
 * with `dim` claiming one of its reserved words. See EMBED DIM GUARD below. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t cursor;         /* circular WRITE slot (0..MAX-1) */
    uint32_t total_entries;  /* monotonic lifetime count */
    uint32_t boot_id;
    uint32_t dim;            /* the dimension every stored vector was written at */
    uint32_t reserved[9];
    uint32_t checksum;       /* XOR of words 0..14 */
} embed_store_header_t;

_Static_assert(sizeof(embed_store_header_t) <= 512, "embed header must fit one sector");

/*
 * One stored vector: exactly 1024 B = 2 sectors.
 *
 *   sector +0  vec[128]                       written FIRST
 *   sector +1  identity + dim + status + cksum  written LAST (publish-last)
 *
 * DELIBERATELY NOT __attribute__((packed)), unlike its siblings. Their layouts
 * would have holes without it; this one has none by construction (every field
 * lands on its natural alignment), and packing a struct containing a float array
 * makes every `rec->vec` a potentially-misaligned member address, which
 * -Waddress-of-packed-member turns into an error under -Werror. The
 * _Static_asserts below pin size AND every offset, which is a STRONGER guarantee
 * than packed gives: packed promises no padding, these promise the fields are
 * exactly where the on-disk format says they are.
 */
typedef struct {
    float    vec[EMBED_STORE_DIM];  /* +0    : the 128-dim L2-normalised vector */
    uint64_t owner_key;             /* +512  : owning epi_record_t.query_key */
    uint32_t boot_id;               /* +520  : stamped at write */
    uint32_t seq;                   /* +524  : this store's own monotonic seq */
    uint32_t owner_seq;             /* +528  : owning epi_record_t.seq */
    uint32_t dim;                   /* +532  : dimension this vector was written at */
    uint16_t status;                /* +536  : EMBED_STORE_ST_* */
    uint16_t reserved16;            /* +538  */
    uint32_t checksum;              /* +540  : XOR over the whole record, this word excluded */
    uint8_t  pad[480];              /* +544  : to exactly 1024 */
} embed_rec_t;

_Static_assert(sizeof(embed_rec_t) == 1024, "embed record must be exactly two sectors");
_Static_assert(sizeof(float) == 4, "vector serialisation assumes 4-byte float");

/* THE ONE THAT MATTERS MOST. The Phase C plan contemplates re-truncating the
 * dimension (Matryoshka), and the record's whole layout depends on the vector
 * being EXACTLY one sector. A dimension change must break the BUILD, not silently
 * straddle a sector boundary and corrupt every record's metadata half. */
_Static_assert(EMBED_STORE_DIM * 4 == 512, "vector must be exactly one sector");

/* The vector occupies sector +0 in full, so the metadata starts exactly at +512.
 * Pin every offset: the struct is unpacked, so a reordered or inserted field would
 * otherwise change the on-disk format with no diagnostic. */
_Static_assert(offsetof(embed_rec_t, vec)        == 0,   "vec is sector +0");
_Static_assert(offsetof(embed_rec_t, owner_key)  == 512, "metadata starts at sector +1");
_Static_assert(offsetof(embed_rec_t, boot_id)    == 520, "layout drift");
_Static_assert(offsetof(embed_rec_t, seq)        == 524, "layout drift");
_Static_assert(offsetof(embed_rec_t, owner_seq)  == 528, "layout drift");
_Static_assert(offsetof(embed_rec_t, dim)        == 532, "layout drift");
_Static_assert(offsetof(embed_rec_t, status)     == 536, "layout drift");
_Static_assert(offsetof(embed_rec_t, checksum)   == 540, "layout drift");

/* Store handle — callbacks, region geometry, cached header. */
typedef struct {
    embed_read_fn         read;
    embed_write_fn        write;
    uint64_t              base_lba;
    uint32_t              max_entries;
    embed_store_header_t  hdr;
    int                   initialized;
} embed_store_t;

/* ---- Core API (semantics identical to epi_store_* / sem_store_*) ---- */

/*
 * Read the header via the read callback. If valid (magic + version + checksum +
 * DIM), bump boot_id, clamp a stale cursor, continue; otherwise create a fresh
 * header (boot_id 1). Flushes. Returns 0 on success, <0 on error.
 *
 * EMBED DIM GUARD — the reason `dim` is in the header at all. A stored header
 * whose dim != EMBED_STORE_DIM is treated as INVALID and the store is reset,
 * discarding every vector in it. That is deliberate and it is the safe direction:
 * cosine between a 128-dim vector and a differently-produced one is not merely
 * inaccurate, it is meaningless, and a store holding a MIX of the two would return
 * confident nonsense with nothing to detect it. A dimension or embedder change
 * must invalidate the corpus, not silently blend into it. Losing the vectors costs
 * a re-embed; keeping them would cost correctness.
 */
int embed_store_init(embed_store_t *s, embed_read_fn rd, embed_write_fn wr,
                     uint64_t base_lba, uint32_t max_entries);

/*
 * Append a record at the current cursor slot (2 sectors, ONE write call so the
 * vector lands before the identity), advance cursor %= max, total_entries++, flush
 * the header. Circular: never fills, overwrites oldest.
 *
 * STAMPS boot_id (= header boot_id), seq (= header total_entries), dim and status
 * at write time and RECOMPUTES the checksum afterwards, so a caller cannot hand in
 * a record whose checksum disagrees with its stamped identity.
 *
 * PLAIN APPEND — it does NOT scan for an existing vector. That is what makes a
 * bulk backfill O(N) instead of O(N^2); see embed_store_put.
 */
int embed_store_append(embed_store_t *s, const embed_rec_t *rec);

/* Read the logical_index-th STORED record in wrap order oldest->newest
 * (0 = oldest retained). Returns -1 if logical_index >= embed_store_count(). */
int embed_store_read(embed_store_t *s, uint32_t logical_index, embed_rec_t *rec);

/*
 * Find the stored record belonging to episodic record `owner_seq`.
 * Returns 1 = found (out params filled if non-NULL), 0 = not found, <0 = error.
 *
 * LINEAR SCAN over the store via the read callback — O(N) reads per call. Fine for
 * a one-shot check; NOT a per-query or per-append path. A caller backfilling many
 * records should build an in-RAM index ONCE and look up there instead, exactly as
 * epi_index_lookup exists for (episodic_store.h:198: "NEVER a per-query O(N) NVMe
 * scan"). Doing this per append is O(N^2) and would take hours on a full store —
 * the same order of cost this store was created to avoid.
 */
int embed_store_find_by_seq(embed_store_t *s, uint32_t owner_seq,
                            uint32_t *logical_index_out, embed_rec_t *rec_out);

/*
 * Idempotent insert: append unless a vector for this owner_seq already exists.
 * Returns 0 = inserted, 1 = already present (nothing written), <0 = error.
 * O(N) — it is find_by_seq followed by append. See that function's note.
 *
 * APPEND-OR-NO-OP, NOT UPSERT, and the difference is deliberate.
 *
 * sem_store_upsert updates in place because a fact's text legitimately changes as
 * evidence accumulates. A vector cannot: it is a deterministic function of the
 * record's query text under a frozen model and a frozen `mu`. Re-embedding the
 * same record must therefore produce the same bytes, and a re-scan at boot must be
 * a no-op rather than a duplicate.
 *
 * So if a recomputed vector ever DIFFERED from the stored one, that is not new
 * evidence — it means the model, the projection, or the dimension changed. Quietly
 * updating one record would leave the store MIXED: some vectors from the old
 * pipeline, some from the new, cosines between them meaningless and nothing
 * anywhere indicating it. Refusing is the safe answer; the correct response to a
 * pipeline change is the header dim guard resetting the whole store, not a
 * per-record update.
 */
int embed_store_put(embed_store_t *s, const embed_rec_t *rec);

/* Write the current header to disk (checksum stamped). */
int embed_store_flush(embed_store_t *s);

uint32_t embed_store_boot_id(const embed_store_t *s);
uint32_t embed_store_count(const embed_store_t *s);  /* stored, capped at max (rolling-full) */

/* ---- Record helpers (pure — no store, no I/O, host-testable in isolation) ---- */

/*
 * THE DESYNC PREDICATE — the entire reason the metadata half exists.
 *
 * Returns 1 if this record belongs to the episodic record identified by
 * (owner_seq, owner_key), 0 otherwise (including a NULL record).
 *
 * BOTH fields are compared. `seq` alone would miss a store rebuilt or re-indexed
 * such that a different question landed on the same slot number; `query_key` alone
 * would miss the same question asked in two different sessions. They fail
 * independently, so checking one is checking neither reliably.
 *
 * Takes the two FIELDS rather than an epi_record_t deliberately, so this header
 * stays decoupled from episodic_store.h — the same decoupling g3_retrieval.h keeps
 * by taking want_action/ok_outcome as codes. The caller writes
 * embed_rec_matches(&r, rec.seq, rec.query_key).
 *
 * This is IDENTITY only. Structural validity is embed_rec_is_valid.
 */
int embed_rec_matches(const embed_rec_t *r, uint32_t owner_seq, uint64_t owner_key);

/* Structural validity: status is VALID, dim is this build's dim, and the checksum
 * over BOTH sectors matches. Catches a torn 2-sector write, which the identity
 * predicate above cannot see (a torn record's metadata half may be perfectly
 * self-consistent while the vector half belongs to the slot's previous occupant).
 * Returns 1 if usable, 0 otherwise (including NULL). */
int embed_rec_is_valid(const embed_rec_t *r);

/* XOR of every uint32 word in the record except the checksum word itself.
 * Spans the vector sector AND the metadata sector — see the header comment. */
uint32_t embed_rec_checksum(const embed_rec_t *r);

/* Populate a record for `owner_seq`/`owner_key` from `vec` (dim must equal
 * EMBED_STORE_DIM). Zeroes the record, sets dim/status, copies the vector
 * bit-exactly. Leaves boot_id/seq 0 and the checksum unset — embed_store_append
 * stamps and recomputes both at write time. Returns 0 on success, <0 on bad args. */
int embed_rec_fill(embed_rec_t *r, uint32_t owner_seq, uint64_t owner_key,
                   const float *vec, int dim);

#endif /* EMBED_STORE_H */
