/*
 * control_floor.c — Phase 6 6-5/M3-3: cross-reboot persisted replay floor (pure logic).
 * See control_floor.h for the mechanism. No I/O here — the caller reads/writes the sectors.
 */
#include "control_floor.h"
#include <string.h>
#include <stddef.h>

/* Non-cryptographic, position-sensitive checksum over the first `n` bytes (rotate + xor + add).
 * A single-bit flip anywhere in the covered region changes the result — enough to catch a torn
 * or garbled sector (integrity, NOT authentication: the floor sector is owner-provisioned + on a
 * write-once/PA-only region, never attacker-reachable). */
static uint32_t floor_checksum(const uint8_t *p, size_t n)
{
    uint32_t c = 0x811C9DC5u;   /* FNV-1a offset basis, reused as a seed */
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        c = (c << 5) | (c >> 27);   /* rotl 5 — position-sensitive */
        c += 0x9E3779B9u;
    }
    return c;
}

static int is_all_zero(const uint8_t sector[512])
{
    for (int i = 0; i < 512; i++)
        if (sector[i] != 0) return 0;
    return 1;
}

/* RFC-1982-style serial comparison so the write_count wrap tie-break is correct:
 * a is NEWER than b iff (a-b) is a small positive delta mod 2^32 (so wc=0 is newer than
 * wc=0xFFFFFFFF). Equal -> not newer. */
static int wc_newer(uint32_t a, uint32_t b)
{
    uint32_t d = a - b;
    return d != 0u && d < 0x80000000u;
}

void ctrl_floor_build(uint8_t out[512], uint64_t reserved_floor, uint32_t write_count)
{
    ctrl_floor_slot_t s;
    memset(&s, 0, sizeof s);
    s.magic          = CTRL_FLOOR_MAGIC;
    s.version        = CTRL_FLOOR_VERSION;
    s.reserved_floor = reserved_floor;
    s.write_count    = write_count;
    s.checksum       = floor_checksum((const uint8_t *)&s, offsetof(ctrl_floor_slot_t, checksum));
    memcpy(out, &s, sizeof s);
}

int ctrl_floor_parse(const uint8_t sector[512], uint64_t *floor, uint32_t *wc)
{
    ctrl_floor_slot_t s;
    memcpy(&s, sector, sizeof s);
    if (s.magic != CTRL_FLOOR_MAGIC) return 0;          /* covers the all-zero (magic==0) case */
    if (s.version != CTRL_FLOOR_VERSION) return 0;
    uint32_t calc = floor_checksum(sector, offsetof(ctrl_floor_slot_t, checksum));
    if (calc != s.checksum) return 0;
    if (floor) *floor = s.reserved_floor;
    if (wc)    *wc    = s.write_count;
    return 1;
}

ctrl_floor_status_t ctrl_floor_select(const uint8_t a[512], const uint8_t b[512],
                                      uint64_t *floor, uint32_t *next_wc)
{
    uint64_t fa = 0, fb = 0;
    uint32_t wca = 0, wcb = 0;
    int va = ctrl_floor_parse(a, &fa, &wca);
    int vb = ctrl_floor_parse(b, &fb, &wcb);

    if (va && vb) {
        if (wc_newer(wca, wcb)) { if (floor) *floor = fa; if (next_wc) *next_wc = wca + 1u; }
        else                    { if (floor) *floor = fb; if (next_wc) *next_wc = wcb + 1u; }
        return FLOOR_OK;
    }
    if (va) { if (floor) *floor = fa; if (next_wc) *next_wc = wca + 1u; return FLOOR_OK; }
    if (vb) { if (floor) *floor = fb; if (next_wc) *next_wc = wcb + 1u; return FLOOR_OK; }

    /* neither validates */
    if (is_all_zero(a) && is_all_zero(b)) {
        if (floor)   *floor   = 0;
        if (next_wc) *next_wc = 1u;
        return FLOOR_FRESH;
    }
    return FLOOR_CORRUPT;   /* non-zero + neither valid -> torn/garbage -> caller fails safe */
}

int ctrl_floor_due(uint64_t highest_seq, uint64_t cur_reservation, uint64_t *new_reservation)
{
    if (highest_seq >= cur_reservation) {
        if (new_reservation) *new_reservation = highest_seq + CTRL_FLOOR_RESERVE;
        return 1;
    }
    return 0;
}

uint64_t ctrl_floor_next_lba(uint32_t next_wc)
{
    return (next_wc & 1u) ? CTRL_FLOOR_LBA_B : CTRL_FLOOR_LBA_A;
}
