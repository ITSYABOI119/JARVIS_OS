/*
 * control_floor.h — Phase 6 6-5/M3-3: cross-reboot persisted replay floor.
 *
 * Host-pure (NO seL4 / device / allocator dependency — the control_replay.c /
 * km2b_miss.c / wake.c precedent): the CALLER does the NVMe I/O and passes 512 B
 * sector buffers in. This module is the pure serialize / validate / select /
 * eager-reservation logic.
 *
 * MECHANISM (Option A, box-side only — no sender/epoch coordination): the control-IN
 * replay floor (control_replay_t.seq_floor) is persisted to NVMe so a frame captured
 * in boot N cannot replay in boot N+1. The persisted value is a RESERVATION that is
 * always AHEAD of the highest accepted seq (fail-high) -> a reboot resumes with
 * floor >= last-accepted -> ZERO cross-reboot replay window.
 *
 * STORAGE: a SEPARATE, DOUBLE-BUFFERED, checksummed floor-sector PAIR (A/B) — NOT
 * the JKEY key slot's reserved fields. The key sector (control_key.h) stays
 * WRITE-ONCE-FOREVER (zero torn-write risk on the crown-jewel key); its seq_floor /
 * boot_epoch reserved fields are LEFT UNUSED by design. The A/B pair is written
 * alternately (newest write_count wins), so a torn write to one leaves the other
 * intact.
 *
 * Epoch is UNCHANGED (still CONTROL_TEST_EPOCH — a per-boot epoch is Option B, not
 * chosen). This module persists only the sequence floor.
 */
#ifndef JARVIS_CONTROL_FLOOR_H
#define JARVIS_CONTROL_FLOOR_H

#include <stdint.h>

/* control_key.h owns CTRL_KEY_BASE_LBA; it is a sibling module and may not have
 * landed in every build context — __has_include lets this compile standalone with
 * an honest fallback and adopt the real base once the header exists (the
 * control_replay.h precedent). */
#if defined(__has_include)
#  if __has_include("control_key.h")
#    include "control_key.h"
#  endif
#endif
#ifndef CTRL_KEY_BASE_LBA
#define CTRL_KEY_BASE_LBA 21130000ULL
#endif

/* The floor sector pair — both in the control-IN raw-LBA sub-region, right after
 * the key sector (@ CTRL_KEY_BASE_LBA). */
#define CTRL_FLOOR_LBA_A   (CTRL_KEY_BASE_LBA + 1ULL)   /* 21,130,001 */
#define CTRL_FLOOR_LBA_B   (CTRL_KEY_BASE_LBA + 2ULL)   /* 21,130,002 */

#define CTRL_FLOOR_MAGIC   0x4A464C52u                  /* 'J','F','L','R' */
#define CTRL_FLOOR_VERSION 1u

/* Seqs reserved ahead of the highest accepted seq per persist (the fail-high
 * margin; also = the persist cadence: one NVMe write per ~RESERVE accepts).
 * Overridable at build time for the box gate (a small value crosses a reservation
 * boundary in a few frames). */
#ifndef CTRL_FLOOR_RESERVE
#define CTRL_FLOOR_RESERVE 256ULL
#endif

typedef struct __attribute__((packed)) {
    uint32_t magic;          /* CTRL_FLOOR_MAGIC                                       */
    uint32_t version;        /* CTRL_FLOOR_VERSION                                     */
    uint64_t reserved_floor; /* persisted high-water RESERVATION (>= any accepted seq) */
    uint32_t write_count;    /* monotonic; newest-valid wins across the A/B pair       */
    uint32_t checksum;       /* over [0 .. offsetof(checksum)) = the first 20 bytes    */
    uint8_t  pad[512 - 24];  /* pad to exactly one 512 B sector                        */
} ctrl_floor_slot_t;

_Static_assert(sizeof(ctrl_floor_slot_t) == 512, "ctrl_floor_slot must be exactly one 512B sector");

typedef enum {
    FLOOR_OK      = 0,  /* >=1 sector validates; newest write_count wins (*floor,*next_wc set) */
    FLOOR_FRESH   = 1,  /* BOTH sectors all-zero — an un-provisioned box (accept from seq 1)   */
    FLOOR_CORRUPT = 2   /* non-zero but NEITHER validates (torn/garbage) -> caller FAILS SAFE  */
} ctrl_floor_status_t;

/* Serialize {reserved_floor, write_count} + magic/version/checksum into a 512 B sector. */
void ctrl_floor_build(uint8_t out[512], uint64_t reserved_floor, uint32_t write_count);

/* Parse + validate ONE sector. Returns 1 (and sets *floor,*wc) iff magic+version+checksum OK;
 * else 0 (incl. an all-zero sector — not-provisioned, distinct from a valid floor=0 record,
 * which carries the magic). The floor and wc out-params may be NULL. */
int ctrl_floor_parse(const uint8_t sector[512], uint64_t *floor, uint32_t *wc);

/* Given the A and B sectors just read, pick the durable floor.
 *   FLOOR_OK      -> *floor = the winner's reservation; *next_wc = winner_wc + 1;
 *   FLOOR_FRESH   -> both all-zero; *floor = 0; *next_wc = 1;
 *   FLOOR_CORRUPT -> non-zero but neither validates (torn/garbage). */
ctrl_floor_status_t ctrl_floor_select(const uint8_t a[512], const uint8_t b[512],
                                      uint64_t *floor, uint32_t *next_wc);

/* Eager-reservation decision (pure). If the live highest-accepted seq has reached/crossed the
 * current persisted reservation, a persist is DUE: return 1 and *new_reservation =
 * highest_seq + CTRL_FLOOR_RESERVE (always > highest_seq, so the persisted value stays ahead).
 * Else return 0. */
int ctrl_floor_due(uint64_t highest_seq, uint64_t cur_reservation, uint64_t *new_reservation);

/* Which sector to write next (alternate A/B by write_count parity: even -> A, odd -> B), so the
 * live write never overwrites the sector the boot-read just selected. */
uint64_t ctrl_floor_next_lba(uint32_t next_wc);

#endif /* JARVIS_CONTROL_FLOOR_H */
