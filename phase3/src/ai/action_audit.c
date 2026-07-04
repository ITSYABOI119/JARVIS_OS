/*
 * JARVIS AI-OS — Phase 6 K/M0: the Action-Audit Store
 *
 * Pure, device-independent (episodic_store.c clone — same circular logic,
 * XOR header checksum, boot_id bump on valid re-init, stale-cursor clamp,
 * flush-after-every-write). All I/O via the callbacks in act_audit_t.
 */

#include "action_audit.h"
#include <string.h>

static uint32_t compute_checksum(const act_audit_header_t *h)
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

int act_audit_init(act_audit_t *s, audit_read_fn rd, audit_write_fn wr,
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
        if (s->hdr.magic == ACT_AUDIT_MAGIC &&
            s->hdr.version == ACT_AUDIT_VERSION &&
            s->hdr.checksum == compute_checksum(&s->hdr)) {
            s->hdr.boot_id++;
            s->hdr.cursor %= max_entries;
            s->initialized = 1;
            return act_audit_flush(s);
        }
    }

    memset(&s->hdr, 0, sizeof(s->hdr));
    s->hdr.magic         = ACT_AUDIT_MAGIC;
    s->hdr.version       = ACT_AUDIT_VERSION;
    s->hdr.cursor        = 0;
    s->hdr.total_entries = 0;
    s->hdr.boot_id       = 1;
    s->initialized       = 1;
    return act_audit_flush(s);
}

int act_audit_flush(act_audit_t *s)
{
    if (!s || !s->initialized)
        return -1;
    s->hdr.checksum = compute_checksum(&s->hdr);
    uint8_t sec[512];
    memset(sec, 0, sizeof(sec));
    memcpy(sec, &s->hdr, sizeof(s->hdr));
    return s->write(s->base_lba, 1, sec);
}

int act_audit_append(act_audit_t *s, const action_audit_rec_t *rec)
{
    if (!s || !s->initialized || !rec)
        return -1;

    action_audit_rec_t r;
    memcpy(&r, rec, sizeof(r));
    r.boot_id = s->hdr.boot_id;
    r.seq     = s->hdr.total_entries;

    uint64_t lba = s->base_lba + 1 + s->hdr.cursor;
    int rc = s->write(lba, 1, &r);
    if (rc != 0)
        return rc;

    s->hdr.cursor = (s->hdr.cursor + 1) % s->max_entries;
    s->hdr.total_entries++;
    return act_audit_flush(s);
}

int act_audit_read(act_audit_t *s, uint32_t logical_index, action_audit_rec_t *rec)
{
    if (!s || !s->initialized || !rec)
        return -1;
    if (logical_index >= act_audit_count(s))
        return -1;

    uint32_t slot;
    if (s->hdr.total_entries < s->max_entries)
        slot = logical_index;
    else
        slot = (s->hdr.cursor + logical_index) % s->max_entries;

    uint64_t lba = s->base_lba + 1 + slot;
    return s->read(lba, 1, rec);
}

uint32_t act_audit_boot_id(const act_audit_t *s)
{
    return (s && s->initialized) ? s->hdr.boot_id : 0;
}

uint32_t act_audit_count(const act_audit_t *s)
{
    if (!s || !s->initialized)
        return 0;
    return s->hdr.total_entries < s->max_entries ? s->hdr.total_entries : s->max_entries;
}

void act_audit_fill(action_audit_rec_t *rec, uint32_t t_ms, uint16_t action_id,
                    uint16_t trust_level, uint16_t verdict, uint16_t risk_x100,
                    uint16_t outcome, const char *trigger, uint16_t trigger_len)
{
    if (!rec)
        return;
    memset(rec, 0, sizeof(*rec));
    rec->t_ms        = (uint64_t)t_ms;
    rec->action_id   = action_id;
    rec->trust_level = trust_level;
    rec->verdict     = verdict;
    rec->risk_x100   = risk_x100;
    rec->outcome     = outcome;
    /* boot_id/seq left 0 — act_audit_append stamps them at write time. */

    if (trigger && trigger_len > 0) {
        uint16_t tl = trigger_len;
        if (tl > ACT_TRIGGER_MAX)
            tl = ACT_TRIGGER_MAX;
        memcpy(rec->trigger_snapshot, trigger, tl);   /* by length, never strlen */
        rec->trigger_len = tl;
    }
}
