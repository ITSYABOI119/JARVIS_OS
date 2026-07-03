/*
 * JARVIS AI-OS — Phase 5 #5: SHIELD failure-learning risk-map implementation.
 * Phase-1 parity port (shield_framework.py:525-559). See shield_learn.h +
 * phase5/docs/PHASE_5_GOAL5_SHIELD_LEARNING.md. MONITOR-ONLY — never blocks.
 */

#include "shield_learn.h"

float shield_learn_record_failure(shield_learn_slot_t *tbl, int cap, uint64_t key)
{
    if (!tbl || cap <= 0) {
        return -1.0f;
    }

    int start = (int)(key % (uint64_t)cap);
    for (int probe = 0; probe < cap; probe++) {
        shield_learn_slot_t *s = &tbl[(start + probe) % cap];
        if (s->fail_count == 0) {            /* empty slot: first failure for key */
            s->key = key;
            s->fail_count = 1;
            s->risk_adj = SHIELD_LEARN_STEP;
            return s->risk_adj;
        }
        if (s->key == key) {
            s->fail_count++;
            /* Phase-1 parity: +STEP per failure, capped at CAP, MONOTONIC-raise-only.
             * Computed as min(CAP, STEP * count) — behaviorally identical to Python's
             * accumulate-then-clamp (`min(0.5, adj + 0.1)`), and exact in IEEE single
             * at every ladder step (0.1f*n rounds to the 0.1/0.2/../0.5 literals).
             * The raise-only guard makes monotonicity structural, not incidental. */
            float r = SHIELD_LEARN_STEP * (float)s->fail_count;
            if (r > SHIELD_LEARN_CAP) r = SHIELD_LEARN_CAP;
            if (r > s->risk_adj) s->risk_adj = r;
            return s->risk_adj;
        }
    }

    return -1.0f;   /* table full and key absent */
}

float shield_learn_adjustment(const shield_learn_slot_t *tbl, int cap, uint64_t key)
{
    if (!tbl || cap <= 0) {
        return 0.0f;
    }

    int start = (int)(key % (uint64_t)cap);
    for (int probe = 0; probe < cap; probe++) {
        const shield_learn_slot_t *s = &tbl[(start + probe) % cap];
        if (s->fail_count == 0) {            /* hit an empty slot: absent */
            return 0.0f;
        }
        if (s->key == key) {
            return s->risk_adj;
        }
    }

    return 0.0f;   /* full table, key absent */
}

uint32_t shield_learn_fail_count(const shield_learn_slot_t *tbl, int cap, uint64_t key)
{
    if (!tbl || cap <= 0) {
        return 0;
    }

    int start = (int)(key % (uint64_t)cap);
    for (int probe = 0; probe < cap; probe++) {
        const shield_learn_slot_t *s = &tbl[(start + probe) % cap];
        if (s->fail_count == 0) {
            return 0;
        }
        if (s->key == key) {
            return s->fail_count;
        }
    }

    return 0;
}
