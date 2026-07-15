/*
 * control_key.h — the control-IN HMAC key slot (Phase 6 6-5/M2a).
 *
 * A single 512-byte raw-LBA sector holding the install-provisioned symmetric
 * HMAC-SHA256 key that authenticates inbound control-IN frames. The KEY LIVES
 * IN PROCESS A (never the future SEC-014 input process, never BAR0) — PA is the
 * only holder + the constant-time verifier (goal-doc §5, O-Q3c: verify-in-PA).
 *
 * Placed at LBA 21,130,000 — the next raw-LBA sub-region after the JACT
 * action-audit store (@ 21,120,000, +4097), keeping the 21,1X0,000 spacing of
 * the Phase-5/6 storage map (episodic @ 21,100,000 / semantic @ 21,110,000 /
 * JACT @ 21,120,000 / control-key @ 21,130,000). Owner-provisioned from Ubuntu
 * via `dd` before boot; read ONCE at boot, FAIL-CLOSED (no slot / bad magic /
 * all-zero key -> the RX poll never calls control_verify).
 *
 * seq_floor / boot_epoch are M3 (cross-reboot replay persistence) — IGNORED in
 * M2a, which uses a compile-time CONTROL_TEST_EPOCH and a per-boot floor of 0.
 */
#ifndef JARVIS_CONTROL_KEY_H
#define JARVIS_CONTROL_KEY_H

#include <stdint.h>

#define CTRL_KEY_BASE_LBA 21130000ULL
#define CTRL_KEY_MAGIC    0x4A4B4559u   /* "JKEY" */
#define CTRL_KEY_VERSION  1u
#define CTRL_KEY_LEN      32u           /* HMAC-SHA256 symmetric key length */

typedef struct __attribute__((packed)) {
    uint32_t magic;                 /* CTRL_KEY_MAGIC "JKEY"                     */
    uint16_t version;               /* CTRL_KEY_VERSION                          */
    uint8_t  reserved0[2];
    uint8_t  key[CTRL_KEY_LEN];      /* the 32-byte HMAC key                      */
    uint64_t seq_floor;             /* M3: persisted replay floor (IGNORED @ M2a) */
    uint32_t boot_epoch;            /* M3: real per-boot epoch    (IGNORED @ M2a) */
    uint8_t  reserved1[512 - 4 - 2 - 2 - CTRL_KEY_LEN - 8 - 4];  /* pad to 512 */
} ctrl_key_slot_t;

_Static_assert(sizeof(ctrl_key_slot_t) == 512, "control key slot must be exactly one 512B sector");

#endif /* JARVIS_CONTROL_KEY_H */
