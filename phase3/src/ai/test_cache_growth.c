/*
 * Phase 5 #6/M0 — cache-growth promotion selector tests (host/CI).
 * Build: gcc -Wall -Werror -O2 -std=c11 -I phase3/src/ai \
 *        phase3/src/ai/test_cache_growth.c phase3/src/ai/cache_growth.c -o /tmp/tcg && /tmp/tcg
 */

#include "cache_growth.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do {                                  \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); }       \
    else      { g_fail++; printf("  FAIL: %s\n", msg); }       \
} while (0)

/* Build a candidate over borrowed (non-owned) query/action strings. */
static cg_candidate_t mk(uint64_t key, const char *q, const char *a)
{
    cg_candidate_t c;
    c.query_key  = key;
    c.query      = q;
    c.query_len  = (uint16_t)strlen(q);
    c.action     = a;
    c.action_len = (uint16_t)strlen(a);
    return c;
}

int main(void)
{
    printf("== test_cache_growth ==\n");

    /* T1: a key seen >= threshold(2) is promoted; a key seen once is not; newest-wins. */
    {
        cg_candidate_t cands[] = {
            mk(100, "alpha", "A1"),
            mk(200, "beta",  "B1"),   /* freq 1 -> not promoted */
            mk(100, "alpha", "A2"),   /* freq 2 -> promoted, newest action = "A2" */
        };
        cg_promotion_t out[8];
        int m = cg_select_promotions(cands, 3, CG_PROMOTE_THRESHOLD, out, 8);
        CHECK(m == 1, "T1: one key crosses threshold");
        CHECK(m == 1 && out[0].query_key == 100, "T1: promoted key is 100");
        CHECK(m == 1 && out[0].action_len == 2 && memcmp(out[0].action, "A2", 2) == 0,
              "T1: newest-wins (action == latest 'A2', not 'A1')");
    }

    /* T2: mixed frequencies A(x3), B(x1), C(x2) -> promote A and C, not B; dedup; newest-wins. */
    {
        cg_candidate_t cands[] = {
            mk(1, "a", "a1"),
            mk(2, "b", "b1"),   /* freq 1 */
            mk(1, "a", "a2"),
            mk(3, "c", "c1"),
            mk(1, "a", "a3"),   /* A freq 3, newest a3 */
            mk(3, "c", "c2"),   /* C freq 2, newest c2 */
        };
        cg_promotion_t out[8];
        int m = cg_select_promotions(cands, 6, 2, out, 8);
        CHECK(m == 2, "T2: two keys promoted (A,C)");

        int a_seen = 0, b_seen = 0, c_seen = 0, a_ok = 0, c_ok = 0;
        for (int i = 0; i < m; i++) {
            if (out[i].query_key == 1) {
                a_seen++;
                if (out[i].action_len == 2 && memcmp(out[i].action, "a3", 2) == 0) a_ok = 1;
            }
            if (out[i].query_key == 2) b_seen++;
            if (out[i].query_key == 3) {
                c_seen++;
                if (out[i].action_len == 2 && memcmp(out[i].action, "c2", 2) == 0) c_ok = 1;
            }
        }
        CHECK(a_seen == 1 && c_seen == 1, "T2: A and C each promoted once (dedup)");
        CHECK(b_seen == 0, "T2: B (freq 1) not promoted");
        CHECK(a_ok && c_ok, "T2: newest-wins for both A('a3') and C('c2')");
    }

    /* T3: n == 0 -> 0 promotions. */
    {
        cg_candidate_t dummy = mk(1, "x", "y");
        cg_promotion_t out[4];
        int m = cg_select_promotions(&dummy, 0, 2, out, 4);
        CHECK(m == 0, "T3: n==0 returns 0");
    }

    /* T4: max cap honored — 3 promotable keys, max=1 -> returns 1. */
    {
        cg_candidate_t cands[] = {
            mk(10, "p", "p1"), mk(10, "p", "p2"),
            mk(20, "q", "q1"), mk(20, "q", "q2"),
            mk(30, "r", "r1"), mk(30, "r", "r2"),
        };
        cg_promotion_t out[1];
        int m = cg_select_promotions(cands, 6, 2, out, 1);
        CHECK(m == 1, "T4: max=1 cap honored (1 of 3 promotable)");
    }

    /* T5: threshold=3 -> a freq-2 key is NOT promoted. */
    {
        cg_candidate_t cands[] = {
            mk(7, "s", "s1"), mk(7, "s", "s2"),   /* freq 2 */
        };
        cg_promotion_t out[4];
        int m = cg_select_promotions(cands, 2, 3, out, 4);
        CHECK(m == 0, "T5: threshold=3, freq-2 key not promoted");
    }

    /* T6 (M1): freq aggregate — bump/get basics. */
    {
        cg_freq_slot_t tbl[8] = {{0, 0}};
        CHECK(cg_freq_bump(tbl, 8, 100) == 1, "T6: first bump of a new key -> 1");
        CHECK(cg_freq_bump(tbl, 8, 100) == 2, "T6: second bump of same key -> 2");
        CHECK(cg_freq_get(tbl, 8, 100) == 2, "T6: get returns the bumped count");
        CHECK(cg_freq_get(tbl, 8, 999) == 0, "T6: absent key -> 0");
    }

    /* T7 (M1): two distinct keys keep independent counts. */
    {
        cg_freq_slot_t tbl[8] = {{0, 0}};
        cg_freq_bump(tbl, 8, 1); cg_freq_bump(tbl, 8, 1); cg_freq_bump(tbl, 8, 2);
        CHECK(cg_freq_get(tbl, 8, 1) == 2 && cg_freq_get(tbl, 8, 2) == 1,
              "T7: distinct keys count independently");
    }

    /* T8 (M1): collision — keys 3 and 11 share slot 3 % 8; linear probing separates them. */
    {
        cg_freq_slot_t tbl[8] = {{0, 0}};
        CHECK(cg_freq_bump(tbl, 8, 3) == 1, "T8: first colliding key inserts");
        CHECK(cg_freq_bump(tbl, 8, 11) == 1, "T8: second colliding key probes to a new slot");
        cg_freq_bump(tbl, 8, 11);
        CHECK(cg_freq_get(tbl, 8, 3) == 1 && cg_freq_get(tbl, 8, 11) == 2,
              "T8: colliding keys hold correct independent counts");
    }

    /* T9 (M1): saturation — a full table returns 0 for an absent key; present keys still bump. */
    {
        cg_freq_slot_t tbl[4] = {{0, 0}};
        CHECK(cg_freq_bump(tbl, 4, 0) == 1 && cg_freq_bump(tbl, 4, 1) == 1 &&
              cg_freq_bump(tbl, 4, 2) == 1 && cg_freq_bump(tbl, 4, 3) == 1,
              "T9: fill cap=4 with 4 distinct keys");
        CHECK(cg_freq_bump(tbl, 4, 99) == 0, "T9: full table, absent-key bump -> 0");
        CHECK(cg_freq_get(tbl, 4, 99) == 0, "T9: full table, absent-key get -> 0");
        CHECK(cg_freq_bump(tbl, 4, 2) == 2, "T9: already-present key still bumps when full");
    }

    printf("== %d PASS, %d FAIL ==\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
