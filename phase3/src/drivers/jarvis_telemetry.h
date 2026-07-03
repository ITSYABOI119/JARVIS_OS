/**
 * jarvis_telemetry.h - JARVIS binary telemetry packet (goal #2b N-c)
 *
 * A versioned, CRC'd, fixed-222-byte (v5) binary packet the box emits over UDP
 * (255.255.255.255:51000, via net_udp.c + the I211) so a remote console can
 * render live, honest box state. Pure logic / host-testable (CRC + finalize);
 * the emit site is in main_x86.c.
 *
 * Wire format: little-endian (x86), packed, no padding. The CRC-32 is the
 * standard zlib/IEEE CRC (poly 0xEDB88320, init/xorout 0xFFFFFFFF) over the
 * first 218 bytes [0 .. offsetof(crc32)), so a Python receiver validates with
 * `zlib.crc32(pkt[:218]) == struct.unpack_from('<I', pkt, 218)[0]`.
 *
 * JARVIS AI-OS - Phase 4 (goal #2b Remote Telemetry Console)
 */

#ifndef JARVIS_TELEMETRY_H
#define JARVIS_TELEMETRY_H

#include <stdint.h>

#define JARVIS_TLM_MAGIC   0x4A54454Cu  /* "JTEL" (LE on wire: 4C 45 54 4A) */
#define JARVIS_TLM_VERSION 5

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
 * both 0 + flag clear in the flag-OFF deploy). */
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
    uint32_t crc32;          /* zlib CRC-32 over the first 218 bytes [0 .. offsetof(crc32)) */       /*  4 */
} telemetry_packet_t;

_Static_assert(sizeof(telemetry_packet_t) == 222, "telemetry packet must be 222 bytes (v5)");

/* Standard zlib/IEEE CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF) — equals Python zlib.crc32. */
uint32_t jarvis_tlm_crc32(const void *data, uint32_t len);

/* Stamp magic/version and compute+store crc32 over the first 218 bytes (v5). */
void jarvis_tlm_finalize(telemetry_packet_t *pkt);

#endif /* JARVIS_TELEMETRY_H */
