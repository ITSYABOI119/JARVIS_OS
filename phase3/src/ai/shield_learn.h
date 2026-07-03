/*
 * JARVIS AI-OS — Phase 5 #5: SHIELD failure-learning risk-map (pure, host-testable)
 *
 * C port of the Phase-1 FailureLearningSystem (phase1/src/ai/shield_framework.py:525-559)
 * with EXACT Phase-1 parity arithmetic: +0.1 risk per failure, capped at +0.5,
 * MONOTONIC-RAISE-ONLY (nothing ever lowers a learned score). One deliberate mapping
 * change: keys are episodic `query_key`s (FNV-1a, decision-cache parity — the shared
 * key currency of #1/#3/#6) rather than Phase-1's action_type strings.
 *
 * MONITOR-ONLY (D-c): this module learns and reports; it NEVER blocks. SEC-039 (the
 * live SHIELD ALLOW-stub) is unchanged by this goal — live enforcement is Phase 6 #5.
 *
 * PURE: no seL4, no device, no allocator. The caller owns the slot table (zero-init =
 * all empty; `fail_count==0` is the empty sentinel — a recorded failure always has
 * count >= 1, so 0 is safe). Same open-addressed linear-probe shape as the #6
 * cg_freq table. Persistence is D-a: the scores DERIVE from the episodic log's
 * ERROR/BLOCKED records (boot-scan seed + batch fold at M1), not a second NVMe store.
 *
 * Design: phase5/docs/PHASE_5_GOAL5_SHIELD_LEARNING.md.
 */

#ifndef SHIELD_LEARN_H
#define SHIELD_LEARN_H

#include <stdint.h>

#define SHIELD_LEARN_STEP     0.1f   /* +10% risk per failure (Phase-1 parity) */
#define SHIELD_LEARN_CAP      0.5f   /* max learned adjustment (Phase-1 parity) */
#define SHIELD_LEARN_CAP_KEYS 256    /* open-addressed table capacity (M1 sizing) */

typedef struct {
    uint64_t key;        /* episodic query_key (FNV-1a) */
    float    risk_adj;   /* learned adjustment, 0.1..0.5 once recorded */
    uint32_t fail_count; /* failures recorded for key; 0 => empty slot */
} shield_learn_slot_t;

/* Record one failure for key: fail_count++, risk_adj raised by SHIELD_LEARN_STEP up to
 * SHIELD_LEARN_CAP — monotonic-raise-only. Returns the NEW risk_adj, or -1.0f if the
 * table is full and key is absent. Open-addressed linear probe from key % cap. Pure. */
float    shield_learn_record_failure(shield_learn_slot_t *tbl, int cap, uint64_t key);

/* Learned adjustment for key (0.0f if absent — the probe stops at the first empty slot). */
float    shield_learn_adjustment(const shield_learn_slot_t *tbl, int cap, uint64_t key);

/* Failure count for key (0 if absent). Pure. */
uint32_t shield_learn_fail_count(const shield_learn_slot_t *tbl, int cap, uint64_t key);

#endif /* SHIELD_LEARN_H */
