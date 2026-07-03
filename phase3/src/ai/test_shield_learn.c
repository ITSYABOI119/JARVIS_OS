/*
 * Phase 5 #5/M0 — SHIELD failure-learning risk-map tests (host/CI).
 * Phase-1 parity: +0.1/failure, cap 0.5, monotonic-raise-only
 * (phase1/src/ai/shield_framework.py:525-559, FailureLearningSystem).
 * Build: gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *        phase3/src/ai/test_shield_learn.c phase3/src/ai/shield_learn.c -o /tmp/tsl && /tmp/tsl
 */

#include "shield_learn.h"
#include <stdio.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do {                                  \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); }       \
    else      { g_fail++; printf("  FAIL: %s\n", msg); }       \
} while (0)

#define NEAR(x, want) (fabsf((x) - (want)) < 1e-6f)

int main(void)
{
    printf("== test_shield_learn (Phase-1 parity: +0.1/failure, cap 0.5, monotonic) ==\n");

    /* T1: the parity ladder — exact +0.1 steps to the 0.5 cap; count keeps climbing past it. */
    {
        shield_learn_slot_t tbl[8] = {{0, 0.0f, 0}};
        CHECK(shield_learn_record_failure(tbl, 8, 100) == SHIELD_LEARN_STEP,
              "T1: one failure -> risk_adj == 0.1");
        CHECK(shield_learn_fail_count(tbl, 8, 100) == 1, "T1: count == 1 after one failure");
        CHECK(NEAR(shield_learn_record_failure(tbl, 8, 100), 0.2f), "T1: 2nd failure -> 0.2 (the criterion-2 RISE signal)");
        CHECK(NEAR(shield_learn_record_failure(tbl, 8, 100), 0.3f), "T1: 3rd -> 0.3");
        CHECK(NEAR(shield_learn_record_failure(tbl, 8, 100), 0.4f), "T1: 4th -> 0.4");
        CHECK(shield_learn_record_failure(tbl, 8, 100) == SHIELD_LEARN_CAP,
              "T1: 5th failure -> risk_adj == 0.5 (cap)");
        CHECK(shield_learn_fail_count(tbl, 8, 100) == 5, "T1: count == 5 at the cap");
        CHECK(shield_learn_record_failure(tbl, 8, 100) == SHIELD_LEARN_CAP,
              "T1: 6th failure -> stays 0.5 (capped)");
        CHECK(shield_learn_fail_count(tbl, 8, 100) == 6, "T1: count == 6 (keeps counting past the cap)");
        CHECK(shield_learn_adjustment(tbl, 8, 100) == SHIELD_LEARN_CAP,
              "T1: adjustment() reads the capped value");
    }

    /* T2: monotonic — the adjustment NEVER decreases across many failures. */
    {
        shield_learn_slot_t tbl[8] = {{0, 0.0f, 0}};
        float prev = 0.0f;
        int mono = 1;
        for (int i = 0; i < 12; i++) {
            float r = shield_learn_record_failure(tbl, 8, 7);
            if (r < prev) mono = 0;
            prev = r;
        }
        CHECK(mono, "T2: risk_adj is monotonic-raise-only over 12 failures");
        CHECK(prev == SHIELD_LEARN_CAP && shield_learn_fail_count(tbl, 8, 7) == 12,
              "T2: settles at the cap with the full count");
    }

    /* T3: absent-key defaults (empty table AND a populated table). */
    {
        shield_learn_slot_t tbl[8] = {{0, 0.0f, 0}};
        CHECK(shield_learn_adjustment(tbl, 8, 42) == 0.0f, "T3: absent key -> adjustment 0.0");
        CHECK(shield_learn_fail_count(tbl, 8, 42) == 0, "T3: absent key -> count 0");
        shield_learn_record_failure(tbl, 8, 1);
        CHECK(shield_learn_adjustment(tbl, 8, 42) == 0.0f && shield_learn_fail_count(tbl, 8, 42) == 0,
              "T3: still absent after another key was recorded");
    }

    /* T4: distinct keys learn independently. */
    {
        shield_learn_slot_t tbl[8] = {{0, 0.0f, 0}};
        shield_learn_record_failure(tbl, 8, 1);
        shield_learn_record_failure(tbl, 8, 1);
        shield_learn_record_failure(tbl, 8, 2);
        CHECK(NEAR(shield_learn_adjustment(tbl, 8, 1), 0.2f) && shield_learn_fail_count(tbl, 8, 1) == 2,
              "T4: key 1 -> 0.2 / count 2");
        CHECK(shield_learn_adjustment(tbl, 8, 2) == SHIELD_LEARN_STEP && shield_learn_fail_count(tbl, 8, 2) == 1,
              "T4: key 2 -> 0.1 / count 1 (independent)");
    }

    /* T5: collision — keys 3 and 11 share slot 3 % 8; linear probing keeps them separate. */
    {
        shield_learn_slot_t tbl[8] = {{0, 0.0f, 0}};
        CHECK(shield_learn_record_failure(tbl, 8, 3) == SHIELD_LEARN_STEP, "T5: first colliding key inserts");
        CHECK(shield_learn_record_failure(tbl, 8, 11) == SHIELD_LEARN_STEP, "T5: second colliding key probes to a new slot");
        shield_learn_record_failure(tbl, 8, 11);
        CHECK(shield_learn_fail_count(tbl, 8, 3) == 1 && shield_learn_fail_count(tbl, 8, 11) == 2,
              "T5: colliding keys hold independent counts");
        CHECK(shield_learn_adjustment(tbl, 8, 3) == SHIELD_LEARN_STEP && NEAR(shield_learn_adjustment(tbl, 8, 11), 0.2f),
              "T5: colliding keys hold independent adjustments");
    }

    /* T6: saturation — a full table returns -1.0f for an absent key; present keys still record. */
    {
        shield_learn_slot_t tbl[4] = {{0, 0.0f, 0}};
        CHECK(shield_learn_record_failure(tbl, 4, 0) == SHIELD_LEARN_STEP &&
              shield_learn_record_failure(tbl, 4, 1) == SHIELD_LEARN_STEP &&
              shield_learn_record_failure(tbl, 4, 2) == SHIELD_LEARN_STEP &&
              shield_learn_record_failure(tbl, 4, 3) == SHIELD_LEARN_STEP,
              "T6: fill cap=4 with 4 distinct keys");
        CHECK(shield_learn_record_failure(tbl, 4, 99) == -1.0f, "T6: full table, absent key -> -1.0f");
        CHECK(shield_learn_adjustment(tbl, 4, 99) == 0.0f && shield_learn_fail_count(tbl, 4, 99) == 0,
              "T6: full table, absent key -> 0.0 / 0");
        CHECK(NEAR(shield_learn_record_failure(tbl, 4, 2), 0.2f),
              "T6: already-present key still records when full");
    }

    /* T7: guards — NULL/invalid-cap never crash, return the absent defaults / error. */
    {
        shield_learn_slot_t tbl[4] = {{0, 0.0f, 0}};
        CHECK(shield_learn_record_failure(0, 4, 1) == -1.0f, "T7: NULL table -> -1.0f");
        CHECK(shield_learn_record_failure(tbl, 0, 1) == -1.0f, "T7: cap<=0 -> -1.0f");
        CHECK(shield_learn_adjustment(0, 4, 1) == 0.0f && shield_learn_fail_count(0, 4, 1) == 0,
              "T7: NULL reads -> 0.0 / 0");
    }

    printf("== %d PASS, %d FAIL ==\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
