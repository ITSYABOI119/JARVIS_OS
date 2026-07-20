/*
 * control_console.h — Phase 6 6-5/M4a: the PROVISIONED control-IN reply address (NVMe slot).
 *
 * WHY PROVISIONED: the box has NO ARP and no IP stack, so the control-IN reply destination
 * must be PROVISIONED — never resolved, and NEVER derived from a received frame's source
 * (a spoofed source would redirect the answer to the attacker; that confidentiality hole is
 * what the provisioned-address discipline closes). `pa_ctrl_gate`'s reply path builds its
 * unicast frame to THIS address only, with a fail-closed dst_ok assertion.
 *
 * WHAT CHANGED AT M4a: M3-2b carried the console address as COMPILE CONSTS in this file
 * (CONTROL_CONSOLE_MAC / CONTROL_CONSOLE_IP) and marked itself "M4 REPLACES this whole
 * header". **Those compile consts are GONE.** The address now lives in an owner-provisioned
 * 512 B raw-LBA slot (the control_key.h / JKEY and control_floor.h / JFLR precedent), read
 * ONCE at boot, FAIL-CLOSED: no slot / bad magic / bad version / bad checksum / all-zero
 * -> control-IN stays OFF and no reply is ever sent. An operator provisions the console
 * address at install time instead of it being baked into the binary.
 *
 * Host-pure (NO seL4 / device / allocator dependency — the control_floor.c precedent): the
 * CALLER does the NVMe I/O and passes a 512 B sector buffer in. Nothing here reads past the
 * 512 B sector.
 *
 * STORAGE MAP (control-IN raw-LBA sub-region, base = CTRL_KEY_BASE_LBA 21,130,000):
 *     key     @ +0      21,130,000   JKEY  (WRITE-ONCE FOREVER)
 *     floor   @ +1/+2   21,130,001/2 JFLR  (A/B double-buffered, M3-3)
 *     console @ +3      21,130,003   JCON  (this module, M4a)
 *
 * ENDIANNESS: the integer fields (magic / version / ip / port / checksum) are read by DIRECT
 * STRUCT ACCESS on a little-endian box, so the ON-DISK byte order is LITTLE-ENDIAN — exactly
 * like ctrl_floor_slot_t and ctrl_key_slot_t. A provisioner writes them with struct.pack('<...').
 * `ip` is a HOST-ORDER u32 (the JARVIS_BOX_IP convention: 192.168.100.146 == 0xC0A86492), so on
 * disk its bytes read 92 64 A8 C0. `mac` is a plain 6-byte array in WIRE order (no swapping).
 *
 * REFERENCE VALUES for the provisioner (the Main PC, Ethernet 3 — determined from the live host
 * via Get-NetAdapter, NOT guessed; these are documentation, NOT compile consts any more):
 *     mac  = 9c:6b:00:ae:6a:ff
 *     ip   = 192.168.100.146  (host-order u32 0xC0A86492)
 *     port = 51002            (= CONTROL_REPLY_PORT below)
 * If the console host changes, re-provision the slot — no rebuild.
 *
 * HONESTY: this addresses the reply; it does NOT authenticate it. The box -> console direction
 * is CRC'd, NOT HMAC'd (M4b). And only unicast ADDRESSING is proven — never claim third-host
 * delivery exclusivity (that is a switch property, not a property of this code).
 */
#ifndef JARVIS_CONTROL_CONSOLE_H
#define JARVIS_CONTROL_CONSOLE_H

#include <stdint.h>

/* control_key.h owns CTRL_KEY_BASE_LBA; it is a sibling module and may not have landed in
 * every build context — __has_include lets this compile standalone with an honest fallback
 * and adopt the real base once the header exists (the control_floor.h precedent). */
#if defined(__has_include)
#  if __has_include("control_key.h")
#    include "control_key.h"
#  endif
#endif
#ifndef CTRL_KEY_BASE_LBA
#define CTRL_KEY_BASE_LBA 21130000ULL
#endif

#define CTRL_CONSOLE_LBA     (CTRL_KEY_BASE_LBA + 3ULL)   /* 21,130,003 */
#define CTRL_CONSOLE_MAGIC   0x4A434F4Eu                  /* 'J','C','O','N' */
#define CTRL_CONSOLE_VERSION 1u

/* box -> console reply UDP dst port (51000 = telemetry-OUT, 51001 = control-IN inbound).
 * Kept as the provisioner's REFERENCE DEFAULT — the live value comes from the slot. */
#ifndef CONTROL_REPLY_PORT
#define CONTROL_REPLY_PORT 51002u
#endif

typedef struct __attribute__((packed)) {
    uint32_t magic;         /* CTRL_CONSOLE_MAGIC "JCON"                                   */
    uint16_t version;       /* CTRL_CONSOLE_VERSION                                        */
    uint8_t  reserved0[2];
    uint8_t  mac[6];        /* console L2 dst MAC, WIRE order                              */
    uint8_t  reserved1[2];
    uint32_t ip;            /* console L3 dst IP, HOST-ORDER u32 (JARVIS_BOX_IP convention) */
    uint16_t port;          /* box -> console reply UDP dst port (51002)                   */
    uint8_t  reserved2[2];
    uint32_t checksum;      /* over [0 .. offsetof(checksum)) = the first 24 bytes         */
    uint8_t  pad[512 - 28]; /* pad to exactly one 512 B sector                             */
} ctrl_console_slot_t;

_Static_assert(sizeof(ctrl_console_slot_t) == 512, "console slot must be exactly one 512B sector");

/* Serialize {mac, ip, port} + magic/version/checksum into a 512 B sector (zeroed first). */
void ctrl_console_build(uint8_t out[512], const uint8_t mac[6], uint32_t ip, uint16_t port);

/* Parse + validate ONE sector. Returns 1 (and ONLY then sets *mac,*ip,*port) iff
 * magic + version + checksum all validate; else 0 — leaving the out-params UNTOUCHED, so a
 * caller that ignores the return value can never act on half-set state. An ALL-ZERO sector
 * returns 0 (not-provisioned — deliberately indistinguishable in effect from invalid: both
 * mean control-IN stays OFF). Any out-param may be NULL. Reads nothing past the sector. */
int ctrl_console_parse(const uint8_t sector[512], uint8_t mac[6], uint32_t *ip, uint16_t *port);

#endif /* JARVIS_CONTROL_CONSOLE_H */
