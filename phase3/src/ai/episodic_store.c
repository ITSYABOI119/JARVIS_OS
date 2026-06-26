/*
 * JARVIS AI-OS — Phase 5 Goal #1 (KEYSTONE): Episodic Memory Store
 *
 * Pure, device-independent implementation: all I/O goes through the read/write
 * callbacks in epi_store_t (the host test passes mock callbacks; M1 passes
 * main_x86.c NVMe wrappers). NO nvme.h, NO direct device calls here.
 *
 * The circular logic (header at base_lba, records at base_lba+1+slot, XOR header
 * checksum, flush-after-every-write, boot_id increment on valid re-init, stale-cursor
 * clamp, circular cursor + monotonic total_entries) is cloned from nvme_log.c.
 *
 * The query key reuses the decision cache's normalization + FNV-1a hash
 * (cache_normalize_query + cache_hash) so Phase-5 #6 cache-growth can match episodic
 * patterns against cache entries without divergence — NOT a duplicated FNV.
 */

#include "episodic_store.h"
#include "decision_cache.h"   /* cache_normalize_query, cache_hash, MAX_QUERY_LEN */
#include <string.h>

/* XOR of the first 15 uint32_t words (the checksum field at word[15] is excluded).
 * memcpy-based to avoid packed-struct alignment warnings (GCC 13) — same as nvme_log.c. */
static uint32_t compute_checksum(const epi_store_header_t *h)
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

int epi_store_init(epi_store_t *s, epi_read_fn rd, epi_write_fn wr,
                   uint64_t base_lba, uint32_t max_entries)
{
    if (!s || !rd || !wr || max_entries == 0)
        return -1;

    memset(s, 0, sizeof(*s));
    s->read        = rd;
    s->write       = wr;
    s->base_lba    = base_lba;
    s->max_entries = max_entries;

    /* Read the header sector. If valid, increment boot_id and continue from cursor. */
    uint8_t sec[512];
    int rc = s->read(base_lba, 1, sec);
    if (rc == 0) {
        memcpy(&s->hdr, sec, sizeof(s->hdr));
        if (s->hdr.magic == EPI_STORE_MAGIC &&
            s->hdr.version == EPI_STORE_VERSION &&
            s->hdr.checksum == compute_checksum(&s->hdr)) {
            s->hdr.boot_id++;
            s->hdr.cursor %= max_entries;   /* clamp a stale pre-wrap cursor (== max) */
            s->initialized = 1;
            return epi_store_flush(s);
        }
    }

    /* Invalid or no header -- create fresh. */
    memset(&s->hdr, 0, sizeof(s->hdr));
    s->hdr.magic         = EPI_STORE_MAGIC;
    s->hdr.version       = EPI_STORE_VERSION;
    s->hdr.cursor        = 0;
    s->hdr.total_entries = 0;
    s->hdr.boot_id       = 1;
    s->initialized       = 1;
    return epi_store_flush(s);
}

int epi_store_flush(epi_store_t *s)
{
    if (!s || !s->initialized)
        return -1;
    s->hdr.checksum = compute_checksum(&s->hdr);
    uint8_t sec[512];
    memset(sec, 0, sizeof(sec));
    memcpy(sec, &s->hdr, sizeof(s->hdr));
    return s->write(s->base_lba, 1, sec);
}

int epi_store_append(epi_store_t *s, const void *rec512)
{
    if (!s || !s->initialized || !rec512)
        return -1;

    /* Stamp the record's identity at write time: boot_id = the current boot, seq = the
     * monotonic lifetime index. Copy into a local so the caller's record stays const
     * (lets a caller batch pre-filled records in RAM and append them later). */
    epi_record_t rec;
    memcpy(&rec, rec512, sizeof(rec));   /* sizeof(epi_record_t) == 512 */
    rec.boot_id = s->hdr.boot_id;
    rec.seq     = s->hdr.total_entries;

    /* Record sector (cursor + 1 because slot 0 of the region is the header). */
    uint64_t lba = s->base_lba + 1 + s->hdr.cursor;
    int rc = s->write(lba, 1, &rec);
    if (rc != 0)
        return rc;

    /* Circular: cursor is the write SLOT (0..max-1); total_entries is the monotonic
     * lifetime count. Once total >= max the buffer rolls (oldest overwritten). */
    s->hdr.cursor = (s->hdr.cursor + 1) % s->max_entries;
    s->hdr.total_entries++;

    /* Flush the header so cursor/total are durable after every write. */
    return epi_store_flush(s);
}

int epi_store_read(epi_store_t *s, uint32_t logical_index, void *rec512)
{
    if (!s || !s->initialized || !rec512)
        return -1;
    if (logical_index >= epi_store_count(s))
        return -1;

    /* Map oldest->newest logical index to a physical slot. Not rolled: oldest is
     * slot 0 (logical i == slot i). Rolled: the oldest retained record is the one
     * about to be overwritten = slot `cursor`. */
    uint32_t slot;
    if (s->hdr.total_entries < s->max_entries)
        slot = logical_index;
    else
        slot = (s->hdr.cursor + logical_index) % s->max_entries;

    uint64_t lba = s->base_lba + 1 + slot;
    return s->read(lba, 1, rec512);
}

uint32_t epi_store_boot_id(const epi_store_t *s)
{
    return (s && s->initialized) ? s->hdr.boot_id : 0;
}

uint32_t epi_store_count(const epi_store_t *s)
{
    if (!s || !s->initialized)
        return 0;
    return s->hdr.total_entries < s->max_entries ? s->hdr.total_entries : s->max_entries;
}

void episodic_fill(epi_record_t *rec, uint32_t t_ms, const char *query, uint16_t action,
                   uint8_t outcome, uint8_t feedback, const char *resp)
{
    if (!rec)
        return;

    memset(rec, 0, sizeof(*rec));
    rec->t_ms     = (uint64_t)t_ms;
    rec->action   = action;
    rec->outcome  = outcome;
    rec->feedback = feedback;
    /* boot_id and seq are left 0 — epi_store_append stamps them at write time. */

    if (query) {
        /* query_key = cache_hash(cache_normalize_query(query)) — identical normalization
         * to the decision cache so #6 cache-growth matches without divergence. */
        char norm[MAX_QUERY_LEN];
        if (cache_normalize_query(query, norm, sizeof norm))
            rec->query_key = cache_hash(norm);

        size_t ql = strlen(query);
        if (ql > EPI_QUERY_MAX) ql = EPI_QUERY_MAX;
        memcpy(rec->query, query, ql);
        rec->query_len = (uint16_t)ql;
    }
    if (resp) {
        size_t rl = strlen(resp);
        if (rl > EPI_RESP_MAX) rl = EPI_RESP_MAX;
        memcpy(rec->resp, resp, rl);
        rec->resp_len = (uint16_t)rl;
    }
}

int episodic_log(epi_store_t *s, uint32_t t_ms, const char *query, uint16_t action,
                 uint8_t outcome, uint8_t feedback, const char *resp)
{
    if (!s)
        return -1;
    epi_record_t rec;
    episodic_fill(&rec, t_ms, query, action, outcome, feedback, resp);
    return epi_store_append(s, &rec);
}
