/**
 * jarvis_telemetry.h - JARVIS binary telemetry packet (goal #2b N-c)
 *
 * A versioned, CRC'd, fixed-208-byte (v2) binary packet the box emits over UDP
 * (255.255.255.255:51000, via net_udp.c + the I211) so a remote console can
 * render live, honest box state. Pure logic / host-testable (CRC + finalize);
 * the emit site is in main_x86.c.
 *
 * Wire format: little-endian (x86), packed, no padding. The CRC-32 is the
 * standard zlib/IEEE CRC (poly 0xEDB88320, init/xorout 0xFFFFFFFF) over the
 * first 204 bytes [0 .. offsetof(crc32)), so a Python receiver validates with
 * `zlib.crc32(pkt[:204]) == struct.unpack_from('<I', pkt, 204)[0]`.
 *
 * JARVIS AI-OS - Phase 4 (goal #2b Remote Telemetry Console)
 */

#ifndef JARVIS_TELEMETRY_H
#define JARVIS_TELEMETRY_H

#include <stdint.h>

#define JARVIS_TLM_MAGIC   0x4A54454Cu  /* "JTEL" (LE on wire: 4C 45 54 4A) */
#define JARVIS_TLM_VERSION 2

/* flags (bitfield) */
#define TLM_F_MODEL_LOADED  0x01
#define TLM_F_FB_DRAWABLE   0x02
#define TLM_F_FB_MAPPED     0x04
#define TLM_F_HAS_ERROR     0x08
#define TLM_F_SELFTEST_PASS 0x10
#define TLM_F_MEMORY        0x20   /* episodic memory store up (Phase 5 G1) */
#define TLM_F_CONTEXT       0x40   /* shared context pool live (Phase 5 G2) */

/* kind */
#define TLM_K_STATS 1
#define TLM_K_INFER 2
#define TLM_K_STATE 3

/* System-page fields (infer_active/infer_duty_pct/log_cursor/nvme_total_mb, and the
 * now-real total_ram_mb) carry ONLY values with a live box source. infer_duty_pct is a
 * WORKLOAD duty cycle (inference cycles / uptime), NOT a CPU-load gauge (the rootserver
 * busy-polls). They consume former reserved bytes (v1 stayed 200 B, CRC@196); v2
 * appends pool_events/pool_decisions -> 208 B, CRC@204. */
typedef struct __attribute__((packed)) {
    uint32_t magic; uint8_t version; uint8_t kind; uint16_t flags; uint32_t boot_id; uint32_t seq;  /* 16 */
    uint32_t uptime_ms;                                                                              /*  4 */
    uint8_t infer_active; uint8_t infer_duty_pct; uint16_t log_cursor;                               /*  4 */
    uint64_t q_total, q_hits, q_infer, q_heartbeat, q_shield, q_errors;                              /* 48 */
    uint8_t num_nodes; uint8_t model_load_pct; uint8_t fb_bpp; uint8_t selftest_score;
    uint16_t fb_w; uint16_t fb_h; uint32_t model_size_mb; uint32_t total_ram_mb;                     /* 16 */
    uint16_t infer_gen_tokens; uint16_t reserved_i; char last_text[56];                              /* 60 */
    char model_name[40];                                                                             /* 40 */
    uint32_t nvme_total_mb; uint32_t episodic_count; /* NVMe namespace MB + episodic record count (P5 G1/M4) */ /* 8 */
    uint32_t pool_events; uint32_t pool_decisions;   /* P5 G2/M4: live context-pool lifetime counts */ /* 8 */
    uint32_t crc32;          /* zlib CRC-32 over the first 204 bytes [0 .. offsetof(crc32)) */       /*  4 */
} telemetry_packet_t;

_Static_assert(sizeof(telemetry_packet_t) == 208, "telemetry packet must be 208 bytes (v2)");

/* Standard zlib/IEEE CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF) — equals Python zlib.crc32. */
uint32_t jarvis_tlm_crc32(const void *data, uint32_t len);

/* Stamp magic/version and compute+store crc32 over the first 204 bytes. */
void jarvis_tlm_finalize(telemetry_packet_t *pkt);

#endif /* JARVIS_TELEMETRY_H */
