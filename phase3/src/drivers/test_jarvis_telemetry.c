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
    CHECK(sizeof(telemetry_packet_t) == 262, "sizeof(telemetry_packet_t) == 262 (v12)");
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
    OFF(monitors_fired, 228);            /* P6 6-1/M3: v8 fields appended before crc32 */
    OFF(last_monitor_event, 230);
    OFF(mon_pad, 231);
    OFF(wakes_fired, 232);               /* P6 6-2/M3: v9 fields appended before crc32 */
    OFF(last_wake_event, 234);
    OFF(wake_pad, 235);
    OFF(behaviors_fired, 236);           /* P6 6-3/M3: v10 fields appended before crc32 */
    OFF(behaviors_mask, 238);
    OFF(last_behavior, 240);
    OFF(beh_pad, 241);
    OFF(control_in_answered, 242);       /* P6 6-5/M3-4a: v11 fields appended before crc32 */
    OFF(control_in_blocked, 244);
    OFF(control_in_dropped, 246);
    /* v12 (6-6/B/M2): the routing DECISION counts + the live-vs-gated indicator. */
    OFF(route_sysfacts, 250);
    OFF(route_decline, 252);
    OFF(route_infer, 254);
    OFF(route_inited, 256);
    OFF(route_pad, 257);
    OFF(crc32, 258);
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
    pkt.monitors_fired = 4;                 /* P6 6-1/M3 (v8): monitor NOTIFY activity — a neutral count */
    pkt.wakes_fired = 3;                    /* P6 6-2/M3 (v9): DISPATCHED wake consults */
    pkt.last_wake_event = 1;                /* err-rate */
    pkt.last_monitor_event = 3;             /* MON_EV_STORE_WRAP */
    pkt.behaviors_fired = 3;                /* P6 6-3/M3 (v10): registry behavior fires (interrupts) */
    pkt.behaviors_mask = 21;                /* 0b10101 = B1+B3+B5 have fired this boot */
    pkt.last_behavior = 5;                  /* BEHAVIOR_DEGRADED_ALERT */
    pkt.control_in_answered = 3;            /* P6 6-5/M3-4a (v11): control-IN routed+answered */
    pkt.control_in_blocked = 1;             /* a DEFINED-ABUSE-CLASS refuse count, NOT "injection blocked" */
    pkt.control_in_dropped = 5;             /* frames rejected pre-SHIELD (auth/replay/parse/ratelimit) */
    pkt.route_sysfacts = 2;                 /* v12: routing DECISIONS (NOT a breakdown of answered) */
    pkt.route_decline  = 1;
    pkt.route_infer    = 3;
    pkt.route_inited   = 1;                 /* routing is live in this build (no flag bit exists) */

    jarvis_tlm_finalize(&pkt);

    CHECK(pkt.magic == JARVIS_TLM_MAGIC, "finalize sets magic == JTEL");
    CHECK(pkt.version == JARVIS_TLM_VERSION, "finalize sets version == 12");
    CHECK(JARVIS_TLM_VERSION == 12, "wire version is 12 (v12)");
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
    CHECK(pkt.monitors_fired == 4u && pkt.last_monitor_event == 3u,
          "monitors_fired/last_monitor_event survive finalize (v8, CRC covers 228-231)");
    CHECK(TLM_F_MONITORS == 0x1000, "TLM_F_MONITORS == 0x1000 (flags is u16 — fits)");
    CHECK(pkt.wakes_fired == 3u && pkt.last_wake_event == 1u,
          "wakes_fired/last_wake_event survive finalize (v9, CRC covers 232-235)");
    CHECK(TLM_F_WAKE == 0x2000, "TLM_F_WAKE == 0x2000 (flags is u16 — fits)");
    CHECK(pkt.behaviors_fired == 3u && pkt.behaviors_mask == 21u && pkt.last_behavior == 5u,
          "behaviors_fired/behaviors_mask/last_behavior survive finalize (v10, CRC covers 236-241)");
    CHECK(TLM_F_PROACTIVE == 0x4000, "TLM_F_PROACTIVE == 0x4000 (flags is u16 — fits)");
    CHECK(pkt.control_in_answered == 3u && pkt.control_in_blocked == 1u && pkt.control_in_dropped == 5u,
          "control_in_answered/blocked/dropped survive finalize (v11, CRC covers 242-249)");


    /* v12 (6-6/B/M2): routing DECISION counts survive finalize. HONESTY — these are decisions at
     * classification time, NOT a breakdown of control_in_answered: 2+1+3 = 6 decisions here against
     * control_in_answered = 3, exactly because an INFER decision that later degrades or times out is
     * counted but never answered (box-proven at B/M1: 5 decisions vs 4 answered). */
    CHECK(pkt.route_sysfacts == 2u && pkt.route_decline == 1u && pkt.route_infer == 3u,
          "route_sysfacts/decline/infer survive finalize (2/1/3)");
    CHECK(pkt.route_inited == 1u, "route_inited survives finalize (live-vs-gated indicator)");
    CHECK((uint32_t)(pkt.route_sysfacts + pkt.route_decline + pkt.route_infer)
              != (uint32_t)pkt.control_in_answered,
          "route_* are DECISIONS, not a breakdown of control_in_answered");    CHECK(TLM_F_CONTROL_IN == 0x8000, "TLM_F_CONTROL_IN == 0x8000 (the LAST u16 flag bit — flags now exhausted)");

    /* The stored crc matches a recompute over the first 258 bytes (offsetof(crc32)). */
    uint32_t recomputed = jarvis_tlm_crc32(&pkt, offsetof(telemetry_packet_t, crc32));
    CHECK(pkt.crc32 == recomputed, "stored crc32 == recompute over first 258 B");
    CHECK(pkt.crc32 != 0u, "crc32 is non-zero for a populated packet");

    /* The magic bytes are "JTEL" little-endian: 4C 45 54 4A. */
    uint8_t *raw = (uint8_t *)&pkt;
    CHECK(raw[0] == 0x4C && raw[1] == 0x45 && raw[2] == 0x54 && raw[3] == 0x4A,
          "magic on wire (LE) == 4C 45 54 4A (\"JTEL\")");

    /* Flipping any byte in [0,258) must break the CRC check. */
    int detected_all = 1;
    for (uint32_t i = 0; i < offsetof(telemetry_packet_t, crc32); i++) {
        raw[i] ^= 0xFF;
        if (jarvis_tlm_crc32(&pkt, offsetof(telemetry_packet_t, crc32)) == pkt.crc32)
            detected_all = 0;   /* a flip went undetected */
        raw[i] ^= 0xFF;         /* restore */
    }
    CHECK(detected_all, "every single-byte flip in [0,258) breaks the CRC");

    /* CONTROL_IN=0 honest-deploy shape: the control-IN fields left 0 + TLM_F_CONTROL_IN NOT set is the
     * deployed CONTROL_IN=0 box's honest v11 shape ("channel gated off"). The struct is STILL v11/254 B
     * for everyone (no OFF-object-identity claim at v11 — the honest-deploy check is this zero-fill +
     * flag-clear, not byte-identity). The FILL itself is gated in main_x86.c. */
    telemetry_packet_t z = {0};
    z.flags = TLM_F_MODEL_LOADED | TLM_F_SELFTEST_PASS;   /* deploy flags, NO CONTROL_IN */
    jarvis_tlm_finalize(&z);
    CHECK(sizeof z == 262u, "honest-0: still v12/262 B for everyone (no OFF-identity claim at a size bump)");
    CHECK(z.control_in_answered == 0u && z.control_in_blocked == 0u && z.control_in_dropped == 0u,
          "honest-0: control_in fields all 0 (the CONTROL_IN=0 deploy fill)");


    /* The ROUTING=0 deploy emits v12 with the routing counts 0 AND route_inited 0 — the honest
     * "routing gated off" shape. route_inited==0 is what stops the console rendering a live-looking
     * zero; there is no flag bit to clear (the u16 flags word is exhausted). */
    CHECK(z.route_sysfacts == 0u && z.route_decline == 0u && z.route_infer == 0u,
          "honest-0: ROUTING=0 emits zero routing decisions");
    CHECK(z.route_inited == 0u, "honest-0: route_inited 0 == 'routing gated off' (no flag bit exists)");    CHECK((z.flags & TLM_F_CONTROL_IN) == 0, "honest-0: TLM_F_CONTROL_IN clear (channel gated off)");
    CHECK(z.version == JARVIS_TLM_VERSION, "honest-0: version stamped == 12");
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
