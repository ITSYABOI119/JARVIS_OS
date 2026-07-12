/**
 * jarvis_telemetry.h - JARVIS binary telemetry packet (goal #2b N-c)
 *
 * A versioned, CRC'd, fixed-240-byte (v9) binary packet the box emits over UDP
 * (255.255.255.255:51000, via net_udp.c + the I211) so a remote console can
 * render live, honest box state. Pure logic / host-testable (CRC + finalize);
 * the emit site is in main_x86.c.
 *
 * Wire format: little-endian (x86), packed, no padding. The CRC-32 is the
 * standard zlib/IEEE CRC (poly 0xEDB88320, init/xorout 0xFFFFFFFF) over the
 * first 236 bytes [0 .. offsetof(crc32)), so a Python receiver validates with
 * `zlib.crc32(pkt[:236]) == struct.unpack_from('<I', pkt, 236)[0]`.
 *
 * JARVIS AI-OS - Phase 4 (goal #2b Remote Telemetry Console)
 */

#ifndef JARVIS_TELEMETRY_H
#define JARVIS_TELEMETRY_H

#include <stdint.h>

#define JARVIS_TLM_MAGIC   0x4A54454Cu  /* "JTEL" (LE on wire: 4C 45 54 4A) */
#define JARVIS_TLM_VERSION 9

/* flags (bitfield) */
#define TLM_F_MODEL_LOADED  0x01
#define TLM_F_FB_DRAWABLE   0x02
#define TLM_F_FB_MAPPED     0x04
#define TLM_F_HAS_ERROR     0x08
#define TLM_F_SELFTEST_PASS 0x10
#define TLM_F_MEMORY        0x20   /* episodic memory store up (Phase 5 G1) */
#define TLM_F_CONTEXT       0x40   /* shared context pool live (Phase 5 G2) */
#define TLM_F_RETRIEVAL     0x80   /* retrieval-before-inference has fired (Phase 5 G3) */
#define TLM_F_CACHE_GROWTH  0x100  /* cache-growth promotion has occurred (Phase 5 #6) — flags is u16 */
#define TLM_F_SHIELD_LEARN  0x200  /* SHIELD failure-learning has learned >=1 key (Phase 5 #5) — MONITOR-ONLY, never a block claim */
#define TLM_F_SEMANTIC      0x400  /* semantic fact store holds >=1 distilled fact (Phase 5 #4) — observable patterns, never "knows preferences" */
#define TLM_F_ACTIONS       0x800  /* Phase 6 K/M3: the it-acts action gate + audit store are live (g_action_audit_ready) — self-heal/action counts, NOT a query-SHIELD block */
#define TLM_F_MONITORS      0x1000 /* Phase 6 6-1/M3: the always-on monitors are live (g_mon_inited) — a NEUTRAL monitor-event count (a mix of degradation + benign liveness events), never "anomalies/problems detected" */
#define TLM_F_WAKE          0x2000 /* Phase 6 6-2/M3: the event-driven wake lane is live (g_wake_inited) — event-triggered CONSULTS (a fixed, human-reviewed question per monitor event; cache-served or one bounded inference), never "thinking"/"reasoning" */

/* kind */
#define TLM_K_STATS 1
#define TLM_K_INFER 2
#define TLM_K_STATE 3

/* System-page fields (infer_active/infer_duty_pct/log_cursor/nvme_total_mb, and the
 * now-real total_ram_mb) carry ONLY values with a live box source. infer_duty_pct is a
 * WORKLOAD duty cycle (inference cycles / uptime), NOT a CPU-load gauge (the rootserver
 * busy-polls). They consume former reserved bytes (v1 stayed 200 B, CRC@196); v2
 * appends pool_events/pool_decisions -> 208 B, CRC@204; v3 appends
 * retrieval_hits/retrieval_latency_us -> 216 B, CRC@212; v4 appends
 * infer_last_tok_x100 -> 218 B, CRC@214 (and infer_gen_tokens becomes REAL —
 * both from PB's RDTSC-measured generation loop via MSG_INFER_STATS; never a
 * benchmark constant, 0 until a boot's first inference); v5 (P5 #5/M2) appends
 * shield_learn_keys/shield_learn_max_risk_x100 -> 222 B, CRC@218 (the SHIELD
 * failure-learning monitor signal — learned-risk counts, NEVER a block count;
 * both 0 + flag clear in the flag-OFF deploy); v6 (P5 #4/M2) appends
 * semantic_fact_count -> 224 B, CRC@220 (distilled-fact count — observable
 * repeated Q&A patterns compacted by the deterministic distill, never "knows
 * preferences"; 0 + flag clear in the flag-OFF deploy); v7 (P6 K/M3) appends
 * restart_count/actions_fired/actions_blocked -> 232 B, CRC@228 (the self-heal/
 * action activity — lifetime PB restarts + allowlisted actions EXECUTED/BLOCKED
 * by the action gate; TLM_F_ACTIONS set on g_action_audit_ready; all 0 + flag
 * clear in the flag-OFF deploy — NOT a query-SHIELD block count); v8 (P6 6-1/M3)
 * appends monitors_fired/last_monitor_event/mon_pad -> 236 B, CRC@232 (the
 * always-on-monitor NOTIFY activity — a NEUTRAL debounced event count: a MIX of
 * degradation signals (error-rate, self-heal-rate) and benign liveness events
 * (uptime milestones, store wraps) — NEVER framed as "anomalies/problems
 * detected"; TLM_F_MONITORS set on g_mon_inited; 0s + flag clear in the
 * flag-OFF deploy); v9 (P6 6-2/M3) appends wakes_fired/last_wake_event/wake_pad
 * -> 240 B, CRC@236 (the event-driven-wake CONSULT activity — DISPATCHED
 * consults only, never suppressed/refused; a consult = a fixed, human-reviewed
 * question per monitor event, cache-served or one bounded inference — NEVER
 * "thinking"/"reasoning"; TLM_F_WAKE set on g_wake_inited; 0s + flag clear in
 * the flag-OFF deploy). */
typedef struct __attribute__((packed)) {
    uint32_t magic; uint8_t version; uint8_t kind; uint16_t flags; uint32_t boot_id; uint32_t seq;  /* 16 */
    uint32_t uptime_ms;                                                                              /*  4 */
    uint8_t infer_active; uint8_t infer_duty_pct; uint16_t log_cursor;                               /*  4 */
    uint64_t q_total, q_hits, q_infer, q_heartbeat, q_shield, q_errors;                              /* 48 */
    uint8_t num_nodes; uint8_t model_load_pct; uint8_t fb_bpp; uint8_t selftest_score;
    uint16_t fb_w; uint16_t fb_h; uint32_t model_size_mb; uint32_t total_ram_mb;                     /* 16 */
    uint16_t infer_gen_tokens; uint16_t cache_growth_count; char last_text[56];  /* P5 #6/M2: promoted-entry count (entries_used − baseline; former reserved_i — same offset/size, NO size bump) */ /* 60 */
    char model_name[40];                                                                             /* 40 */
    uint32_t nvme_total_mb; uint32_t episodic_count; /* NVMe namespace MB + episodic record count (P5 G1/M4) */ /* 8 */
    uint32_t pool_events; uint32_t pool_decisions;   /* P5 G2/M4: live context-pool lifetime counts */ /* 8 */
    uint32_t retrieval_hits;        /* P5 G3/M4: count of non-empty retrieval preambles packed */     /*  4 */
    uint32_t retrieval_latency_us;  /* P5 G3/M4: last in-RAM retrieval (select+assemble+pack) latency, µs */ /* 4 */
    uint16_t infer_last_tok_x100;   /* v4: LAST real inference tok/s * 100 (RDTSC-measured in PB; 0 until first inference) */ /* 2 */
    uint16_t shield_learn_keys;          /* v5 (P5 #5/M2): actions with learned failure-risk — monitor-only */ /* 2 */
    uint16_t shield_learn_max_risk_x100; /* v5: max learned risk adjustment * 100 (cap 50 = +0.5) — never a block count */ /* 2 */
    uint16_t semantic_fact_count;        /* v6 (P5 #4/M2): distilled semantic facts stored — observable patterns only */ /* 2 */
    uint32_t restart_count;    /* v7 (P6 K/M3) @220 — lifetime PB self-heal restarts (g_restart_count) */ /* 4 */
    uint16_t actions_fired;    /* v7 @224 — allowlisted actions EXECUTED (SHIELD-gated); 0 + flag clear in the flag-OFF deploy */ /* 2 */
    uint16_t actions_blocked;  /* v7 @226 — actions BLOCKED by the action gate (NOT the query-SHIELD path) */ /* 2 */
    uint16_t monitors_fired;     /* v8 (P6 6-1/M3) @228 — monitor NOTIFY events (debounced, fire-once-per-crossing; a NEUTRAL mix, NOT "problems") */ /* 2 */
    uint8_t  last_monitor_event; /* v8 @230 — monitor_event_type_t of the most recent event (0=none .. 6=degraded; 6 appended at 6-3/M0, emitted from M1) */ /* 1 */
    uint8_t  mon_pad;            /* v8 @231 — alignment pad, always 0 */ /* 1 */
    uint16_t wakes_fired;        /* v9 (P6 6-2/M3) @232 — DISPATCHED wake consults (never suppressed/refused) */ /* 2 */
    uint8_t  last_wake_event;    /* v9 @234 — monitor_event_type_t of the most recent dispatched wake (0=none yet) */ /* 1 */
    uint8_t  wake_pad;           /* v9 @235 — alignment pad, always 0 */ /* 1 */
    uint32_t crc32;          /* zlib CRC-32 over the first 236 bytes [0 .. offsetof(crc32)) */       /*  4 */
} telemetry_packet_t;

_Static_assert(sizeof(telemetry_packet_t) == 240, "telemetry packet must be 240 bytes (v9)");

/* Standard zlib/IEEE CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF) — equals Python zlib.crc32. */
uint32_t jarvis_tlm_crc32(const void *data, uint32_t len);

/* Stamp magic/version and compute+store crc32 over the first 236 bytes (v9). */
void jarvis_tlm_finalize(telemetry_packet_t *pkt);

#endif /* JARVIS_TELEMETRY_H */
