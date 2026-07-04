/*
 * JARVIS AI-OS — Phase 5 Goal #4: Semantic Fact Store
 *
 * Pure, device-independent implementation: all I/O goes through the read/write
 * callbacks in sem_store_t (the host test passes mock callbacks; M1 passes
 * main_x86.c NVMe wrappers). NO nvme.h, NO direct device calls here.
 *
 * The circular logic (header at base_lba, records at base_lba+1+slot, XOR header
 * checksum, flush-after-every-write, boot_id increment on valid re-init,
 * stale-cursor clamp, circular cursor + monotonic total_entries) is the proven
 * episodic_store.c/nvme_log.c clone. The #4-specific addition is
 * sem_store_upsert: a repeated subject UPDATES its fact (support raised
 * monotonically via max — idempotent across boot re-distills), never a dup.
 */

#include "semantic_store.h"
#include <string.h>

/* XOR of the first 15 uint32_t words (the checksum field at word[15] excluded).
 * memcpy-based to avoid packed-struct alignment warnings — same as episodic_store.c. */
static uint32_t compute_checksum(const sem_store_header_t *h)
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

int sem_store_init(sem_store_t *s, sem_read_fn rd, sem_write_fn wr,
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
        if (s->hdr.magic == SEM_STORE_MAGIC &&
            s->hdr.version == SEM_STORE_VERSION &&
            s->hdr.checksum == compute_checksum(&s->hdr)) {
            s->hdr.boot_id++;
            s->hdr.cursor %= max_entries;   /* clamp a stale pre-wrap cursor (== max) */
            s->initialized = 1;
            return sem_store_flush(s);
        }
    }

    /* Invalid or no header — create fresh. */
    memset(&s->hdr, 0, sizeof(s->hdr));
    s->hdr.magic         = SEM_STORE_MAGIC;
    s->hdr.version       = SEM_STORE_VERSION;
    s->hdr.cursor        = 0;
    s->hdr.total_entries = 0;
    s->hdr.boot_id       = 1;
    s->initialized       = 1;
    return sem_store_flush(s);
}

int sem_store_flush(sem_store_t *s)
{
    if (!s || !s->initialized)
        return -1;
    s->hdr.checksum = compute_checksum(&s->hdr);
    uint8_t sec[512];
    memset(sec, 0, sizeof(sec));
    memcpy(sec, &s->hdr, sizeof(s->hdr));
    return s->write(s->base_lba, 1, sec);
}

int sem_store_append(sem_store_t *s, const semantic_fact_t *fact)
{
    if (!s || !s->initialized || !fact)
        return -1;

    /* Stamp the fact's identity at write time (the distill leaves both 0). Copy
     * into a local so the caller's fact stays const. */
    semantic_fact_t f;
    memcpy(&f, fact, sizeof(f));   /* sizeof(semantic_fact_t) == 512 */
    f.boot_id = s->hdr.boot_id;
    f.seq     = s->hdr.total_entries;

    /* Fact sector (cursor + 1 because slot 0 of the region is the header). */
    uint64_t lba = s->base_lba + 1 + s->hdr.cursor;
    int rc = s->write(lba, 1, &f);
    if (rc != 0)
        return rc;

    s->hdr.cursor = (s->hdr.cursor + 1) % s->max_entries;
    s->hdr.total_entries++;
    return sem_store_flush(s);
}

int sem_store_read(sem_store_t *s, uint32_t logical_index, semantic_fact_t *fact)
{
    if (!s || !s->initialized || !fact)
        return -1;
    if (logical_index >= sem_store_count(s))
        return -1;

    /* Map oldest->newest logical index to a physical slot (episodic_store logic). */
    uint32_t slot;
    if (s->hdr.total_entries < s->max_entries)
        slot = logical_index;
    else
        slot = (s->hdr.cursor + logical_index) % s->max_entries;

    uint64_t lba = s->base_lba + 1 + slot;
    return s->read(lba, 1, fact);
}

int sem_store_upsert(sem_store_t *s, const semantic_fact_t *fact)
{
    if (!s || !s->initialized || !fact)
        return -1;

    /* Linear scan for an existing fact with this key (upsert keeps keys unique,
     * so the first match is THE fact). Low-cadence distill use only. */
    uint32_t count = sem_store_count(s);
    for (uint32_t li = 0; li < count; li++) {
        semantic_fact_t existing;
        int rc = sem_store_read(s, li, &existing);
        if (rc != 0)
            return rc;
        if (existing.key != fact->key)
            continue;

        /* UPDATE in place: the incoming (newest) fact wins for text/t_ms/type/
         * confidence; support is MONOTONIC — max(existing, incoming) — so a boot
         * re-distill over the same source records is idempotent (never inflates).
         * seq keeps the fact's identity; boot_id restamped to the current boot. */
        semantic_fact_t f;
        memcpy(&f, fact, sizeof(f));
        if (existing.support_count > f.support_count)
            f.support_count = existing.support_count;
        f.seq     = existing.seq;
        f.boot_id = s->hdr.boot_id;

        /* Recompute the physical slot exactly as sem_store_read did. */
        uint32_t slot;
        if (s->hdr.total_entries < s->max_entries)
            slot = li;
        else
            slot = (s->hdr.cursor + li) % s->max_entries;
        uint64_t lba = s->base_lba + 1 + slot;
        rc = s->write(lba, 1, &f);
        if (rc != 0)
            return rc;
        return 1;   /* updated — cursor/total unchanged, no header flush needed */
    }

    /* No fact with this key — insert. */
    int rc = sem_store_append(s, fact);
    return (rc == 0) ? 0 : rc;
}

uint32_t sem_store_boot_id(const sem_store_t *s)
{
    return (s && s->initialized) ? s->hdr.boot_id : 0;
}

uint32_t sem_store_count(const sem_store_t *s)
{
    if (!s || !s->initialized)
        return 0;
    return s->hdr.total_entries < s->max_entries ? s->hdr.total_entries : s->max_entries;
}
