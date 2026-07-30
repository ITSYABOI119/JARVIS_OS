/*
 * JARVIS AI-OS — Phase C / C/M2a: Embedding Vector Store
 *
 * Pure, device-independent implementation: every byte of I/O goes through the
 * read/write callbacks on embed_store_t (host tests pass mocks; C/M2b would pass
 * main_x86.c's NVMe wrappers). NO nvme.h, NO direct device calls, NO call site.
 *
 * The circular core — header at base_lba, records after it, XOR header checksum,
 * flush after every write, boot_id bump on a valid re-init, stale-cursor clamp,
 * monotonic total_entries — is the proven semantic_store.c / episodic_store.c
 * clone. Three things are specific to this store, all argued in embed_store.h:
 *
 *   1. records are TWO sectors (vector + identity), written in ONE call so the
 *      vector lands before the identity that publishes it;
 *   2. the per-record checksum spans BOTH sectors, so a torn pair is detectable;
 *   3. the header carries `dim`, and a mismatch invalidates the whole store.
 */

#include "embed_store.h"
#include <string.h>

/* ---- Header checksum: XOR of the first 15 uint32 words (word 15 is the field
 * itself). memcpy-based to avoid packed-struct alignment warnings — the same
 * approach episodic_store.c and semantic_store.c use. ---- */
static uint32_t compute_header_checksum(const embed_store_header_t *h)
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

/* Absolute LBA of a physical slot. Slot 0 of the region is the header, so records
 * begin at +1, and each consumes EMBED_STORE_SECTORS_PER_REC sectors. */
static uint64_t slot_lba(const embed_store_t *s, uint32_t slot)
{
    return s->base_lba + 1 + (uint64_t)slot * EMBED_STORE_SECTORS_PER_REC;
}

/* Map an oldest->newest logical index to a physical slot (episodic_store logic). */
static uint32_t logical_to_slot(const embed_store_t *s, uint32_t logical_index)
{
    if (s->hdr.total_entries < s->max_entries)
        return logical_index;
    return (s->hdr.cursor + logical_index) % s->max_entries;
}

uint32_t embed_rec_checksum(const embed_rec_t *r)
{
    if (!r)
        return 0;
    const uint8_t *raw = (const uint8_t *)r;
    const size_t words = sizeof(*r) / 4;              /* 1024 / 4 = 256 */
    const size_t skip  = offsetof(embed_rec_t, checksum) / 4;
    uint32_t cs = 0;
    for (size_t i = 0; i < words; i++) {
        if (i == skip)
            continue;                                 /* the checksum field itself */
        uint32_t w;
        memcpy(&w, raw + i * 4, sizeof(w));
        cs ^= w;
    }
    return cs;
}

int embed_rec_matches(const embed_rec_t *r, uint32_t owner_seq, uint64_t owner_key)
{
    if (!r)
        return 0;
    /* BOTH fields — they fail independently; see embed_store.h. */
    if (r->owner_seq != owner_seq)
        return 0;
    if (r->owner_key != owner_key)
        return 0;
    return 1;
}

int embed_rec_is_valid(const embed_rec_t *r)
{
    if (!r)
        return 0;
    if (r->status != EMBED_STORE_ST_VALID)
        return 0;
    if (r->dim != (uint32_t)EMBED_STORE_DIM)
        return 0;
    return r->checksum == embed_rec_checksum(r);
}

int embed_rec_fill(embed_rec_t *r, uint32_t owner_seq, uint64_t owner_key,
                   const float *vec, int dim)
{
    if (!r || !vec || dim != EMBED_STORE_DIM)
        return -1;

    memset(r, 0, sizeof(*r));
    /* Bit-exact copy: this is storage, not arithmetic. */
    memcpy(r->vec, vec, (size_t)EMBED_STORE_DIM * sizeof(float));
    r->owner_seq = owner_seq;
    r->owner_key = owner_key;
    r->dim       = (uint32_t)EMBED_STORE_DIM;
    r->status    = EMBED_STORE_ST_VALID;
    /* boot_id / seq / checksum are stamped by embed_store_append. */
    return 0;
}

int embed_store_init(embed_store_t *s, embed_read_fn rd, embed_write_fn wr,
                     uint64_t base_lba, uint32_t max_entries)
{
    if (!s || !rd || !wr || max_entries == 0)
        return -1;

    memset(s, 0, sizeof(*s));
    s->read        = rd;
    s->write       = wr;
    s->base_lba    = base_lba;
    s->max_entries = max_entries;

    uint8_t sec[512];
    int rc = s->read(base_lba, 1, sec);
    if (rc == 0) {
        memcpy(&s->hdr, sec, sizeof(s->hdr));
        if (s->hdr.magic == EMBED_STORE_MAGIC &&
            s->hdr.version == EMBED_STORE_VERSION &&
            s->hdr.dim == (uint32_t)EMBED_STORE_DIM &&
            s->hdr.checksum == compute_header_checksum(&s->hdr)) {
            s->hdr.boot_id++;
            s->hdr.cursor %= max_entries;   /* clamp a stale pre-wrap cursor */
            s->initialized = 1;
            return embed_store_flush(s);
        }
        /* Falls through on a dim mismatch as well as on corruption: a store whose
         * vectors were written at another dimension is not merely stale, it is
         * incomparable, and resetting it is the safe direction (embed_store.h). */
    }

    memset(&s->hdr, 0, sizeof(s->hdr));
    s->hdr.magic         = EMBED_STORE_MAGIC;
    s->hdr.version       = EMBED_STORE_VERSION;
    s->hdr.cursor        = 0;
    s->hdr.total_entries = 0;
    s->hdr.boot_id       = 1;
    s->hdr.dim           = (uint32_t)EMBED_STORE_DIM;
    s->initialized       = 1;
    return embed_store_flush(s);
}

int embed_store_flush(embed_store_t *s)
{
    if (!s || !s->initialized)
        return -1;
    s->hdr.checksum = compute_header_checksum(&s->hdr);
    uint8_t sec[512];
    memset(sec, 0, sizeof(sec));
    memcpy(sec, &s->hdr, sizeof(s->hdr));
    return s->write(s->base_lba, 1, sec);
}

int embed_store_append(embed_store_t *s, const embed_rec_t *rec)
{
    if (!s || !s->initialized || !rec)
        return -1;

    /* Copy so the caller's record stays const, then stamp identity and recompute
     * the checksum over the stamped bytes — a caller cannot supply a checksum that
     * disagrees with what is actually written. */
    embed_rec_t r;
    memcpy(&r, rec, sizeof(r));
    r.boot_id  = s->hdr.boot_id;
    r.seq      = s->hdr.total_entries;
    r.dim      = (uint32_t)EMBED_STORE_DIM;
    r.status   = EMBED_STORE_ST_VALID;
    r.checksum = embed_rec_checksum(&r);

    /* ONE call for both sectors. The vector is at +0 and the identity at +1, and
     * the callback transfers ascending, so the payload lands before the metadata
     * that publishes it (embed_store.h, "PUBLISH-LAST"). */
    int rc = s->write(slot_lba(s, s->hdr.cursor), EMBED_STORE_SECTORS_PER_REC, &r);
    if (rc != 0)
        return rc;

    s->hdr.cursor = (s->hdr.cursor + 1) % s->max_entries;
    s->hdr.total_entries++;
    return embed_store_flush(s);
}

int embed_store_read(embed_store_t *s, uint32_t logical_index, embed_rec_t *rec)
{
    if (!s || !s->initialized || !rec)
        return -1;
    if (logical_index >= embed_store_count(s))
        return -1;
    return s->read(slot_lba(s, logical_to_slot(s, logical_index)),
                   EMBED_STORE_SECTORS_PER_REC, rec);
}

int embed_store_find_by_seq(embed_store_t *s, uint32_t owner_seq,
                            uint32_t *logical_index_out, embed_rec_t *rec_out)
{
    if (!s || !s->initialized)
        return -1;

    uint32_t count = embed_store_count(s);
    for (uint32_t li = 0; li < count; li++) {
        embed_rec_t r;
        int rc = embed_store_read(s, li, &r);
        if (rc != 0)
            return rc;
        if (r.owner_seq != owner_seq)
            continue;
        if (logical_index_out)
            *logical_index_out = li;
        if (rec_out)
            memcpy(rec_out, &r, sizeof(r));
        return 1;
    }
    return 0;
}

int embed_store_put(embed_store_t *s, const embed_rec_t *rec)
{
    if (!s || !s->initialized || !rec)
        return -1;

    int found = embed_store_find_by_seq(s, rec->owner_seq, NULL, NULL);
    if (found < 0)
        return found;
    if (found == 1)
        return 1;   /* already present — a vector never legitimately changes */

    int rc = embed_store_append(s, rec);
    return (rc == 0) ? 0 : rc;
}

uint32_t embed_store_boot_id(const embed_store_t *s)
{
    return (s && s->initialized) ? s->hdr.boot_id : 0;
}

uint32_t embed_store_count(const embed_store_t *s)
{
    if (!s || !s->initialized)
        return 0;
    return s->hdr.total_entries < s->max_entries ? s->hdr.total_entries : s->max_entries;
}
