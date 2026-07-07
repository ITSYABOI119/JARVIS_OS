/**
 * test_jarvis_telemetry.c - Host tests for the JARVIS telemetry packet
 *
 * Verifies the on-wire layout (sizeof + field offsets), the zlib-compatible
 * CRC-32 (canonical 0xCBF43926 check value -> proves Python zlib.crc32 compat),
 * and the finalize round-trip (CRC matches + any byte flip breaks it).
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -I phase3/src/drivers \
 *       phase3/src/drivers/test_jarvis_telemetry.c phase3/src/drivers/jarvis_telemetry.c \
 *       -o /tmp/test_jarvis_telemetry && /tmp/test_jarvis_telemetry
 *
 * JARVIS AI-OS - Phase 4 (goal #2b N-c)
 */

#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include "jarvis_telemetry.h"

static int pass = 0, fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { pass++; printf("  PASS: %s\n", msg); } \
    else      { fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

#define OFF(field, want) \
    CHECK(offsetof(telemetry_packet_t, field) == (want), \
          "offsetof(" #field ") == " #want)

static void test_layout(void)
{
    CHECK(sizeof(telemetry_packet_t) == 232, "sizeof(telemetry_packet_t) == 232 (v7)");
    OFF(magic, 0);
    OFF(flags, 6);
    OFF(boot_id, 8);
    OFF(seq, 12);
    OFF(uptime_ms, 16);
    OFF(infer_active, 20);     /* System fields packed into former reserved_t */
    OFF(infer_duty_pct, 21);
    OFF(log_cursor, 22);
    OFF(q_total, 24);
    OFF(q_errors, 64);
    OFF(num_nodes, 72);
    OFF(model_size_mb, 80);
    OFF(total_ram_mb, 84);
    OFF(last_text, 92);
    OFF(model_name, 148);
    OFF(nvme_total_mb, 188);   /* System field packed into former reserved2[0] */
    OFF(episodic_count, 192);  /* P5 G1/M4: renamed from reserved2 (same offset/size) */
    OFF(pool_events, 196);     /* P5 G2/M4: v2 fields appended before crc32 */
    OFF(pool_decisions, 200);
    OFF(retrieval_hits, 204);       /* P5 G3/M4: v3 fields appended before crc32 */
    OFF(retrieval_latency_us, 208);
    OFF(cache_growth_count, 90);    /* P5 #6/M2: renamed from reserved_i (same offset/size, no bump) */
    OFF(infer_last_tok_x100, 212);  /* v4: appended before crc32 */
    OFF(shield_learn_keys, 214);          /* P5 #5/M2: v5 fields appended before crc32 */
    OFF(shield_learn_max_risk_x100, 216);
    OFF(semantic_fact_count, 218);        /* P5 #4/M2: v6 field appended before crc32 */
    OFF(restart_count, 220);              /* P6 K/M3: v7 fields appended before crc32 */
    OFF(actions_fired, 224);
    OFF(actions_blocked, 226);
    OFF(crc32, 228);
}

static void test_crc_known_vector(void)
{
    /* Canonical zlib/IEEE CRC-32 check value: crc32("123456789") == 0xCBF43926.
     * Matching this proves wire-compat with Python zlib.crc32. */
    uint32_t crc = jarvis_tlm_crc32("123456789", 9);
    CHECK(crc == 0xCBF43926u, "crc32(\"123456789\") == 0xCBF43926 (zlib check value)");

    /* Empty input -> 0 (zlib convention). */
    CHECK(jarvis_tlm_crc32("", 0) == 0u, "crc32(\"\", 0) == 0");
}

static void test_finalize_roundtrip(void)
{
    telemetry_packet_t pkt;
    memset(&pkt, 0, sizeof pkt);
    pkt.kind = TLM_K_STATS;
    pkt.seq = 7;
    pkt.q_total = 12345;
    pkt.num_nodes = 6;
    pkt.model_size_mb = 2962;
    memcpy(pkt.model_name, "Gemma 4 E2B", 11);
    memcpy(pkt.last_text, "hello world", 11);
    /* System fields (real, in former reserved space) */
    pkt.infer_active = 1;
    pkt.infer_duty_pct = 42;
    pkt.log_cursor = 137;
    pkt.nvme_total_mb = 1953892;
    pkt.total_ram_mb = 30000;
    pkt.episodic_count = 1234;   /* P5 G1/M4: renamed from reserved2 */
    pkt.pool_events = 77;        /* P5 G2/M4: v2 fields */
    pkt.pool_decisions = 88;
    pkt.retrieval_hits = 3;          /* P5 G3/M4: v3 fields (CRC[:212] now covers 204-211) */
    pkt.retrieval_latency_us = 40;
    pkt.cache_growth_count = 12;     /* P5 #6/M2: former reserved_i (offset 90, inside CRC region) */
    pkt.infer_last_tok_x100 = 152;   /* v4: 1.52 tok/s (a real measured shape, never the 5.46 benchmark) */
    pkt.infer_gen_tokens = 50;       /* v4: now REAL (last-inference token count) */
    pkt.shield_learn_keys = 1;             /* P5 #5/M2: monitor-only failure-learning fields (v5) */
    pkt.shield_learn_max_risk_x100 = 20;   /* the D-d probe's 0.20 after the 2nd attempt */
    pkt.semantic_fact_count = 1;           /* P5 #4/M2 (v6): distilled-fact count — the honest ~1 box yield */
    pkt.restart_count = 3;                  /* P6 K/M3 (v7): self-heal/action-gate activity */
    pkt.actions_fired = 3;
    pkt.actions_blocked = 2;

    jarvis_tlm_finalize(&pkt);

    CHECK(pkt.magic == JARVIS_TLM_MAGIC, "finalize sets magic == JTEL");
    CHECK(pkt.version == JARVIS_TLM_VERSION, "finalize sets version == 7");
    CHECK(pkt.infer_active == 1 && pkt.infer_duty_pct == 42, "infer_active/infer_duty_pct survive finalize");
    CHECK(pkt.log_cursor == 137 && pkt.nvme_total_mb == 1953892u, "log_cursor/nvme_total_mb survive finalize");
    CHECK(pkt.total_ram_mb == 30000u, "total_ram_mb survives finalize");
    CHECK(pkt.episodic_count == 1234u, "episodic_count survives finalize");
    CHECK(pkt.pool_events == 77u && pkt.pool_decisions == 88u,
          "pool_events/pool_decisions survive finalize");
    CHECK(pkt.retrieval_hits == 3u && pkt.retrieval_latency_us == 40u,
          "retrieval_hits/retrieval_latency_us survive finalize (CRC covers 204-211)");
    CHECK(TLM_F_RETRIEVAL == 0x80, "TLM_F_RETRIEVAL == 0x80 (next free flag bit)");
    CHECK(pkt.cache_growth_count == 12u, "cache_growth_count survives finalize (P5 #6/M2)");
    CHECK(pkt.infer_last_tok_x100 == 152u && pkt.infer_gen_tokens == 50u,
          "infer_last_tok_x100/infer_gen_tokens survive finalize (v4, CRC covers 212-213)");
    CHECK(TLM_F_CACHE_GROWTH == 0x100, "TLM_F_CACHE_GROWTH == 0x100 (flags is u16 — fits)");
    CHECK(pkt.shield_learn_keys == 1u && pkt.shield_learn_max_risk_x100 == 20u,
          "shield_learn_keys/shield_learn_max_risk_x100 survive finalize (v5, CRC covers 214-217)");
    CHECK(TLM_F_SHIELD_LEARN == 0x200, "TLM_F_SHIELD_LEARN == 0x200 (flags is u16 — fits)");
    CHECK(pkt.semantic_fact_count == 1u,
          "semantic_fact_count survives finalize (v6, CRC covers 218-219)");
    CHECK(TLM_F_SEMANTIC == 0x400, "TLM_F_SEMANTIC == 0x400 (flags is u16 — fits)");
    CHECK(pkt.restart_count == 3u && pkt.actions_fired == 3u && pkt.actions_blocked == 2u,
          "restart_count/actions_fired/actions_blocked survive finalize (v7, CRC covers 220-227)");
    CHECK(TLM_F_ACTIONS == 0x800, "TLM_F_ACTIONS == 0x800 (flags is u16 — fits)");

    /* The stored crc matches a recompute over the first 228 bytes (offsetof(crc32)). */
    uint32_t recomputed = jarvis_tlm_crc32(&pkt, offsetof(telemetry_packet_t, crc32));
    CHECK(pkt.crc32 == recomputed, "stored crc32 == recompute over first 228 B");
    CHECK(pkt.crc32 != 0u, "crc32 is non-zero for a populated packet");

    /* The magic bytes are "JTEL" little-endian: 4C 45 54 4A. */
    uint8_t *raw = (uint8_t *)&pkt;
    CHECK(raw[0] == 0x4C && raw[1] == 0x45 && raw[2] == 0x54 && raw[3] == 0x4A,
          "magic on wire (LE) == 4C 45 54 4A (\"JTEL\")");

    /* Flipping any byte in [0,228) must break the CRC check. */
    int detected_all = 1;
    for (uint32_t i = 0; i < offsetof(telemetry_packet_t, crc32); i++) {
        raw[i] ^= 0xFF;
        if (jarvis_tlm_crc32(&pkt, offsetof(telemetry_packet_t, crc32)) == pkt.crc32)
            detected_all = 0;   /* a flip went undetected */
        raw[i] ^= 0xFF;         /* restore */
    }
    CHECK(detected_all, "every single-byte flip in [0,228) breaks the CRC");
}

int main(void)
{
    printf("=== JARVIS AI-OS: telemetry packet Tests ===\n\n");
    test_layout();
    test_crc_known_vector();
    test_finalize_roundtrip();
    printf("\n=== Results: %d PASS, %d FAIL ===\n", pass, fail);
    return fail ? 1 : 0;
}
