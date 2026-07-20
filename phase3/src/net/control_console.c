/*
 * control_console.c — Phase 6 6-5/M4a: the provisioned control-IN reply address (pure logic).
 * See control_console.h for the mechanism. No I/O here — the caller reads the sector.
 */
#include "control_console.h"
#include <string.h>
#include <stddef.h>

/* Non-cryptographic, position-sensitive checksum over the first `n` bytes (rotate + xor + add).
 * IDENTICAL to control_floor.c's fold, so the whole control-IN raw-LBA sub-region is
 * self-consistent. A single-bit flip anywhere in the covered region changes the result — enough
 * to catch a torn or garbled sector (integrity, NOT authentication: the console slot is
 * owner-provisioned on a PA-only region, never attacker-reachable). */
static uint32_t console_checksum(const uint8_t *p, size_t n)
{
    uint32_t c = 0x811C9DC5u;   /* FNV-1a offset basis, reused as a seed */
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        c = (c << 5) | (c >> 27);   /* rotl 5 — position-sensitive */
        c += 0x9E3779B9u;
    }
    return c;
}

void ctrl_console_build(uint8_t out[512], const uint8_t mac[6], uint32_t ip, uint16_t port)
{
    ctrl_console_slot_t s;
    memset(&s, 0, sizeof s);            /* zero the WHOLE sector incl. reserved + pad */
    s.magic   = CTRL_CONSOLE_MAGIC;
    s.version = (uint16_t)CTRL_CONSOLE_VERSION;
    if (mac) memcpy(s.mac, mac, sizeof s.mac);
    s.ip      = ip;
    s.port    = port;
    s.checksum = console_checksum((const uint8_t *)&s, offsetof(ctrl_console_slot_t, checksum));
    memcpy(out, &s, sizeof s);
}

int ctrl_console_parse(const uint8_t sector[512], uint8_t mac[6], uint32_t *ip, uint16_t *port)
{
    ctrl_console_slot_t s;
    memcpy(&s, sector, sizeof s);       /* bounded to exactly 512 B — never reads past */

    if (s.magic != CTRL_CONSOLE_MAGIC) return 0;   /* covers the all-zero (magic==0) case */
    if (s.version != (uint16_t)CTRL_CONSOLE_VERSION) return 0;

    uint32_t calc = console_checksum(sector, offsetof(ctrl_console_slot_t, checksum));
    if (calc != s.checksum) return 0;

    /* Only past every gate do the out-params move (no half-set state on reject). */
    if (mac)  memcpy(mac, s.mac, sizeof s.mac);
    if (ip)   *ip   = s.ip;
    if (port) *port = s.port;
    return 1;
}
