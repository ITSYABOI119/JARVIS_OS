/**
 * jarvis_telemetry.h - JARVIS binary telemetry packet (goal #2b N-c)
 *
 * A versioned, CRC'd, fixed-276-byte (v14) binary packet the box emits over UDP
 * (255.255.255.255:51000, via net_udp.c + the I211) so a remote console can
 * render live, honest box state. Pure logic / host-testable (CRC + finalize);
 * the emit site is in main_x86.c.
 *
 * Wire format: little-endian (x86), packed, no padding. The CRC-32 is the
 * standard zlib/IEEE CRC (poly 0xEDB88320, init/xorout 0xFFFFFFFF) over the
 * first 272 bytes [0 .. offsetof(crc32)), so a Python receiver validates with
 * `zlib.crc32(pkt[:272]) == struct.unpack_from('<I', pkt, 272)[0]`.
 *
 * JARVIS AI-OS - Phase 4 (goal #2b Remote Telemetry Console)
 */

#ifndef JARVIS_TELEMETRY_H
#define JARVIS_TELEMETRY_H

#include <stdint.h>

#define JARVIS_TLM_MAGIC   0x4A54454Cu  /* "JTEL" (LE on wire: 4C 45 54 4A) */
#define JARVIS_TLM_VERSION 14

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
#define TLM_F_PROACTIVE     0x4000 /* Phase 6 6-3/M3: the behavior registry is live (g_proactive_inited) — named, bounded, rate-limited INFORM behaviors (behaviors_fired = interrupts the user saw; a B1/B2 consult bumps BOTH wakes_fired and behaviors_fired — two honest views of one event, never summed), never "decided on its own" */
#define TLM_F_CONTROL_IN    0x8000 /* Phase 6 6-5/M3-4a (v11): the control-IN two-way channel is UP (key + floor + RX ring live). control_in_blocked is a DEFINED-ABUSE-CLASS refuse count from the query SHIELD, NEVER "injection blocked"/"attacks blocked" (general injection is contained STRUCTURALLY, not detected). The LAST u16 flag bit — flags is now exhausted; a future flag needs a flags-width bump. Fill gated JARVIS_CONTROL_IN -> the CONTROL_IN=0 deploy emits v11 with the 3 counters 0 + this flag CLEAR (honest "channel gated off"). */

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
 * the flag-OFF deploy); v10 (P6 6-3/M3) appends behaviors_fired/behaviors_mask/
 * last_behavior/beh_pad -> 246 B, CRC@242 (the proactive-behavior INFORM
 * activity — behaviors_fired counts USER INTERRUPTS (the always-fires mark; a
 * cap-suppressed inform never counts), behaviors_mask bit (id-1) latches once
 * behavior id has fired this boot (the console's live per-row source),
 * last_behavior = the most recent behavior id; a B1/B2 consult bumps BOTH
 * wakes_fired AND behaviors_fired BY DESIGN — two honest views of one event,
 * documented, never summed; TLM_F_PROACTIVE set on g_proactive_inited; 0s +
 * flag clear in the flag-OFF deploy); v11 (P6 6-5/M3-4a) appends
 * control_in_answered/control_in_blocked/control_in_dropped -> 254 B, CRC@250,
 * +TLM_F_CONTROL_IN 0x8000 (the two-way control-IN channel — answered =
 * routed+answered queries, blocked = a DEFINED-ABUSE-CLASS refuse count from the
 * query SHIELD (NEVER "injection blocked"; general injection is contained
 * STRUCTURALLY, not detected), dropped = frames rejected pre-SHIELD by
 * auth/replay/parse/ratelimit; the fill is gated JARVIS_CONTROL_IN so the
 * CONTROL_IN=0 deploy emits v11 with all three 0 + the flag CLEAR — honest
 * "channel gated off"; the flag means the CHANNEL is up, not that a query
 * arrived); v12 (P6 6-6/B/M2) appends route_sysfacts/route_decline/route_infer/
 * route_inited/route_pad -> 262 B, CRC@258, with NO NEW FLAG BIT — the u16 flags
 * word is EXHAUSTED (TLM_F_CONTROL_IN 0x8000 is the last), so routing rides the
 * EXISTING TLM_F_CONTROL_IN (routing requires control-IN) and uses route_inited
 * as its live-vs-gated indicator; consequently routing gets NO Capabilities
 * auto-row and must be surfaced as field-derived console rows (goal doc §6g).
 * These are ROUTING DECISIONS counted at classification time — they are NOT a
 * breakdown of control_in_answered and must never be rendered as one (an INFER
 * decision that later degrades or times out counts in route_infer but never
 * reaches the answered exit; box-proven 5 decisions vs 4 answered). The fill is
 * gated JARVIS_ROUTING so the ROUTING=0 deploy emits v12 with the three counts 0
 * and route_inited 0 — honest "routing gated off". Note the STRUCT is v12/262 B
 * for EVERYONE (the v5..v11 pattern): there is no OFF-object-identity claim at a
 * size bump; the honest check is the zero-fill + route_inited==0.
 *
 * v13 (Phase C / C/M3a) appends sem_recall_hits/sem_recall_tried/
 * sem_recall_floor/sem_inited/sem_pad -> 270 B, CRC@266, again with NO NEW FLAG
 * BIT (the u16 flags word stayed EXHAUSTED at TLM_F_CONTROL_IN 0x8000), so
 * semantic recall rides the EXISTING TLM_F_CONTROL_IN — it is a control-IN lane —
 * and uses sem_inited as its live-vs-gated indicator, exactly as routing does.
 * Consequence, and it is by construction: semantic recall gets NO Capabilities
 * auto-row and must be surfaced as field-derived console rows.
 *
 * NOT THE SAME FEATURE AS semantic_fact_count (v6). That is the Phase-5 #4
 * DISTILL store — deterministic grouping of repeated Q&A into facts, no model
 * involved. These are the Phase-C EMBEDDING recall lane: a paraphrase of an
 * earlier question matched by cosine similarity against stored answer vectors.
 * Different mechanism, different store, different honesty ceiling; never merge,
 * sum or cross-label them.
 *
 * HONESTY, and the reason there are three counters rather than one: a hit count
 * alone lets a reader infer a fire rate that is not there. The MEASURED rate is
 * about half — 19 of 36 paraphrase pairs at the 0.55 floor — so the miss is the
 * MAJORITY case and hiding it would misrepresent the feature.
 *   sem_recall_tried  = the lane was entered at all. It is entered ONLY when the
 *                       turn was about to get no preamble (cplen==0), so it is
 *                       the honest denominator, not a count of every query.
 *   sem_recall_hits   = a semantic preamble was actually built and injected.
 *   sem_recall_floor  = candidates existed and NONE reached the 0.55 floor.
 * They deliberately do NOT sum: tried - hits - floor is a real residual (no
 * stored vector to compare against yet, or a match whose text held no complete
 * sentence to quote). Rendering the residual as zero, or floor as "tried-hits",
 * would attribute embed/backfill states to a similarity decision. A miss is NOT
 * an error — it degrades to EXACTLY the no-preamble path the box takes today.
 * The fill is gated JARVIS_EMBED so the EMBED=0 deploy emits v13 with all three
 * counts 0 and sem_inited 0 — honest "semantic recall gated off".
 *
 * v14 (Phase C / C/M4) appends route_veto_checked/route_vetoed/veto_inited/
 * veto_pad -> 276 B, CRC@272, again with NO NEW FLAG BIT (the u16 flags word
 * stayed EXHAUSTED at TLM_F_CONTROL_IN 0x8000), so the routing veto rides the
 * EXISTING TLM_F_CONTROL_IN — it runs only on the control-IN lane — with
 * veto_inited as its live-vs-gated indicator, exactly as routing (v12) and
 * semantic recall (v13) do. Consequence, by construction: the veto gets NO
 * Capabilities auto-row and is surfaced as field-derived console rows.
 *
 * HONESTY, and the reason there are TWO counters rather than one:
 *   route_veto_checked = comparisons actually RUN — the honest denominator.
 *                        vetoed=0 alone cannot distinguish "no conceptual
 *                        questions arrived" from "the veto is silently
 *                        skipping" (the sem_* three-counter lesson).
 *   route_vetoed       = SYSFACTS captures rerouted to INFER (fired).
 * The veto may ONLY reroute a SYSFACTS capture to the model, never the
 * reverse, and never touches DECLINE (fabrication asymmetry) — so when
 * JARVIS_ROUTE_VETO=1 the v12 route_* counters record the POST-veto FINAL
 * decision (a vetoed query counts in route_infer, not route_sysfacts; the
 * increment sites sit downstream of the veto, nothing moved). The honest
 * claim is "an 81% measured cut in ONE defect class" (32->6 FP at 1 FN on the
 * measured corpus), NEVER "routing is fixed" — the 6 surviving FPs and the
 * 1 FN are the ceiling. The fill is gated JARVIS_ROUTE_VETO so the VETO=0
 * deploy — which is what ships today — emits v14 with both counts 0 and
 * veto_inited 0, honest "veto gated off". */
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
    uint16_t behaviors_fired;    /* v10 (P6 6-3/M3) @236 — registry behavior fires (USER INTERRUPTS; cap-suppressed never counts) */ /* 2 */
    uint16_t behaviors_mask;     /* v10 @238 — bit (id-1) latched once behavior id fired this boot (per-row console source) */ /* 2 */
    uint8_t  last_behavior;      /* v10 @240 — most recent behavior id (0 = none yet) */ /* 1 */
    uint8_t  beh_pad;            /* v10 @241 — alignment pad, always 0 */ /* 1 */
    uint16_t control_in_answered; /* v11 (P6 6-5/M3-4a) @242 — control-IN queries routed + answered (g_ctrl_in_answered, sat 0xFFFF) */ /* 2 */
    uint16_t control_in_blocked;  /* v11 @244 — queries the QUERY SHIELD REFUSED = a DEFINED-ABUSE-CLASS refuse count, NEVER "injection blocked" (g_ctrl_in_blocked, sat 0xFFFF) */ /* 2 */
    uint32_t control_in_dropped;  /* v11 @246 — inbound frames dropped PRE-SHIELD (auth/replay/parse/ratelimit; g_ctrl_dropped) */ /* 4 */
    /* v12 (P6 6-6/B/M2) — control-IN ROUTING DECISIONS. Counted at classification time in
     * pa_ctrl_gate, so they are NOT a breakdown of control_in_answered and MUST NEVER be
     * presented as one: an INFER decision whose dispatch later DEGRADES or TIMES OUT counts in
     * route_infer but never reaches the answered exit (box-proven at B/M1 — sysfacts=2 decline=1
     * infer=2 = 5 decisions with answered=4). No new flag bit exists to carry these (the u16
     * flags word is EXHAUSTED at TLM_F_CONTROL_IN 0x8000), so routing rides that flag plus
     * route_inited as the live-vs-gated signal. */
    uint16_t route_sysfacts;      /* v12 @250 — SYSFACTS routing decisions (g_route_sysfacts, sat 0xFFFF) */ /* 2 */
    uint16_t route_decline;       /* v12 @252 — DECLINE routing decisions, i.e. no-source status queries answered honestly instead of fabricated (g_route_decline, sat 0xFFFF) */ /* 2 */
    uint16_t route_infer;         /* v12 @254 — INFER routing decisions dispatched to the model (g_route_infer, sat 0xFFFF) */ /* 2 */
    uint8_t  route_inited;        /* v12 @256 — 1 iff JARVIS_ROUTING is live in this build; the live-vs-gated indicator (routing has NO flag bit of its own) */ /* 1 */
    uint8_t  route_pad;           /* v12 @257 — alignment pad, always 0 (the mon_pad/wake_pad/beh_pad precedent) */ /* 1 */
    /* v13 (Phase C / C/M3a) — SEMANTIC RECALL on the control-IN path (the embedding lane), NOT
     * the v6 semantic_fact_count distill store. These three do NOT sum: the residual
     * (tried - hits - floor) is real and means "no stored vector to compare against yet, or a
     * match with no complete sentence to quote". Measured ceiling: about half of paraphrases
     * recall (19/36 at the 0.55 floor); a miss degrades to exactly today's no-preamble path. */
    uint16_t sem_recall_hits;     /* v13 @258 — semantic preambles BUILT + injected (g_embed_sem_hits, sat 0xFFFF) */ /* 2 */
    uint16_t sem_recall_tried;    /* v13 @260 — lane entries: turns that were about to get NO preamble (the honest denominator) */ /* 2 */
    uint16_t sem_recall_floor;    /* v13 @262 — candidates existed and NONE reached the 0.55 floor (a similarity decision, never an embed failure) */ /* 2 */
    uint8_t  sem_inited;          /* v13 @264 — 1 iff the semantic lane is LIVE (mu verified AND vector store ready); the live-vs-gated indicator (no flag bit of its own) */ /* 1 */
    uint8_t  sem_pad;             /* v13 @265 — alignment pad, always 0 (the mon_pad/wake_pad/beh_pad/route_pad precedent) */ /* 1 */
    /* v14 (Phase C / C/M4) — the ROUTING VETO on the control-IN path. checked is the honest
     * denominator (comparisons RUN — vetoed=0 alone cannot distinguish "no conceptual questions"
     * from "veto silently skipping"); vetoed counts SYSFACTS captures rerouted to INFER. The veto
     * can only fire in that ONE direction and never touches DECLINE; when it is live the v12
     * route_* counters above record the POST-veto final decision. */
    uint16_t route_veto_checked;  /* v14 @266 — veto comparisons actually run (the denominator; g_route_veto_checked, sat 0xFFFF) */ /* 2 */
    uint16_t route_vetoed;        /* v14 @268 — SYSFACTS captures rerouted to the model (g_route_vetoed, sat 0xFFFF) */ /* 2 */
    uint8_t  veto_inited;         /* v14 @270 — 1 iff the veto is ARMED this boot (centroids XOR-verified at init, JARVIS_ROUTE_VETO=1); the live-vs-gated indicator (no flag bit of its own) */ /* 1 */
    uint8_t  veto_pad;            /* v14 @271 — alignment pad, always 0 (the mon_pad/wake_pad/beh_pad/route_pad/sem_pad precedent) */ /* 1 */
    uint32_t crc32;          /* zlib CRC-32 over the first 272 bytes [0 .. offsetof(crc32)) */       /*  4 */
} telemetry_packet_t;

_Static_assert(sizeof(telemetry_packet_t) == 276, "telemetry packet must be 276 bytes (v14)");
/* The §6a offsets are RECOMPUTED here, not trusted: a silent re-layout (packing change, field
 * reorder) must break the BUILD, because the Python receiver unpacks these by absolute offset. */
_Static_assert(__builtin_offsetof(telemetry_packet_t, route_veto_checked) == 266, "route_veto_checked must sit at 266");
_Static_assert(__builtin_offsetof(telemetry_packet_t, route_vetoed)       == 268, "route_vetoed must sit at 268");
_Static_assert(__builtin_offsetof(telemetry_packet_t, veto_inited)        == 270, "veto_inited must sit at 270");
_Static_assert(__builtin_offsetof(telemetry_packet_t, veto_pad)           == 271, "veto_pad must sit at 271");
_Static_assert(__builtin_offsetof(telemetry_packet_t, crc32)              == 272, "crc32 must sit at 272 (the CRC region end)");

/* Standard zlib/IEEE CRC-32 (poly 0xEDB88320, init/xorout 0xFFFFFFFF) — equals Python zlib.crc32. */
uint32_t jarvis_tlm_crc32(const void *data, uint32_t len);

/* Stamp magic/version and compute+store crc32 over the first 272 bytes (v14).
 * offsetof-based, so a future append auto-extends the CRC region with NO .c change. */
void jarvis_tlm_finalize(telemetry_packet_t *pkt);

#endif /* JARVIS_TELEMETRY_H */
