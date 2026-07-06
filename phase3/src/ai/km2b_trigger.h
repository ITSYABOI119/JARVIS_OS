/*
 * JARVIS AI-OS — Phase 6 K/M2b-2: the keyword-clean restart trigger builder (§5-E)
 *
 * Builds the JACT trigger_snapshot for a PB respawn from SYSTEM FACTS ONLY —
 * fixed literals + decimal-rendered counters — NEVER the in-flight query text
 * (the shield workload lane's queries are all blocklist keywords, which would
 * make shield_assess BLOCK its own restart). The two live reasons are "fault"
 * and "hang"; both are keyword-clean by construction, and the host test
 * (test_km2b_trigger.c) pins that the BUILT trigger never contains a canonical
 * blocklist keyword and never self-blocks shield_assess(ACTION_RESTART_PB).
 *
 * Host-pure: no seL4 dependency (Process A passes seL4_Word == uint64_t).
 * Output is cap-bounded (writes at most cap-1 bytes + NUL) — `reason` is
 * caller-controlled, so the bound is enforced here, not assumed.
 */

#ifndef KM2B_TRIGGER_H
#define KM2B_TRIGGER_H

#include <stdint.h>

/* Render "respawn <reason> lane=<lane> miss=<missed> age=<age_ms>ms
 * flt=<flabel>/<fbadge>" into buf (NUL-terminated, at most cap-1 chars).
 * age_ms/flabel/fbadge render their low 32 bits (counters, not pointers —
 * fault IPs/addrs are printed separately in full hex, never stored here).
 * Returns the length written (excluding NUL); 0 if buf is NULL or cap < 1. */
int km2b_build_trigger(char *buf, int cap, const char *reason, uint16_t lane,
                       uint32_t missed, uint64_t age_ms, uint64_t flabel,
                       uint64_t fbadge);

#endif /* KM2B_TRIGGER_H */
