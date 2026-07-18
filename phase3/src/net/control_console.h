/*
 * control_console.h — Phase 6 6-5/M3-2b: the provisioned control-IN reply address.
 *
 * ── SCAFFOLDING (compile-const; M4 replaces this whole file) ─────────────────────────────
 *
 * The box has NO ARP and no IP stack: the control-IN reply destination must be PROVISIONED,
 * never resolved, and NEVER derived from a received frame's source (a spoofed source would
 * redirect the reply — the confidentiality hole this milestone closes). For M3-2b the console
 * address is a COMPILE CONST (box scaffolding). **M4 REPLACES this whole header** with a
 * fail-closed read from an NVMe console-addr slot (the control_key.h / JKEY precedent), so an
 * operator provisions the console address at install time rather than it being baked in.
 * Keeping it in its OWN file makes that swap a one-file change.
 *
 * The MAC below is the Main PC's 192.168.100.146 interface (Ethernet 3), determined from the
 * live host (getmac / Get-NetAdapter) — NOT guessed. A wrong MAC = the reply silently goes
 * nowhere. If the console host changes, this is the one place to update (until the M4 slot).
 */
#ifndef JARVIS_CONTROL_CONSOLE_H
#define JARVIS_CONTROL_CONSOLE_H

#include <stdint.h>

/* Provisioned console L2/L3 destination (the Main PC, 192.168.100.146 / Ethernet 3). */
#define CONTROL_CONSOLE_MAC { 0x9C, 0x6B, 0x00, 0xAE, 0x6A, 0xFF }
#define CONTROL_CONSOLE_IP  ((192u << 24) | (168u << 16) | (100u << 8) | 146u)  /* 192.168.100.146 */

/* box -> console reply UDP port (51000 = telemetry-OUT, 51001 = control-IN inbound). */
#ifndef CONTROL_REPLY_PORT
#define CONTROL_REPLY_PORT 51002u
#endif

#endif /* JARVIS_CONTROL_CONSOLE_H */
