/*
 * Phase 6 N2 — host test for the PB liveness-tick decision (pb_progress.h).
 *
 * Pins the contract the control-IN poll loop will depend on: an exhausted CTRL-lane window
 * EXTENDS only when PA can SEE the generation is alive (the token counter moved) and the
 * extension budget is not spent; everything else takes exactly today's timeout path, so a
 * genuinely wedged PB is still caught in today's budget by the K/M2c miss counter.
 *
 * Two failure modes carry the weight, and each has its own group:
 *   - T3 WRAP: the counter is a uint32 and wraps once per 2^32 tokens. An ORDERING comparison
 *     (`now > start`) inverts exactly once per wrap and reports a live generation as a wedge.
 *   - T4 CAP: the budget check is what stops an extension loop, and its cap == 0 degenerate IS
 *     the KVM negative-control gate leg (behaviour identical to today).
 *
 * Build: gcc -Wall -Werror -O2 -std=c11 -I. test_pb_progress.c pb_progress.c -o test_pb_progress
 *        && ./test_pb_progress
 */

#include "pb_progress.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  PASS  %s\n", name); } \
    else      { g_fail++; printf("  FAIL  %s\n", name); } \
} while (0)

int main(void)
{
    printf("=== test_pb_progress (Phase 6 N2 — PB liveness tick) ===\n");

    /* ---------------------------------------------------------------------------------
     * T1: the counter MOVED and the budget is not spent -> EXTEND. The live-generation
     * case this whole mechanism exists to serve.
     * --------------------------------------------------------------------------------- */
    CHECK(pb_wait_decide(100u, 101u, 0u, 3u) == PB_WAIT_EXTEND,
          "T1a progress, first window (used 0 < cap 3) -> EXTEND");
    CHECK(pb_wait_decide(100u, 105u, 1u, 3u) == PB_WAIT_EXTEND,
          "T1b progress, middle window (used 1 < cap 3) -> EXTEND");
    CHECK(pb_wait_decide(100u, 101u, 2u, 3u) == PB_WAIT_EXTEND,
          "T1c progress, last grantable window (used 2 < cap 3) -> EXTEND");
    CHECK(pb_wait_decide(0u, 1u, 0u, 1u) == PB_WAIT_EXTEND,
          "T1d progress, minimum useful cap 1 -> EXTEND");
    CHECK(pb_wait_decide(7u, 4000u, 0u, 3u) == PB_WAIT_EXTEND,
          "T1e large forward jump (many tokens in one window) -> EXTEND");

    /* ---------------------------------------------------------------------------------
     * T2: the counter did NOT move -> TIMEOUT, whatever the budget says. The verdict is
     * progress-driven, not budget-driven: a huge cap must not buy a wedged PB any time.
     * --------------------------------------------------------------------------------- */
    CHECK(pb_wait_decide(100u, 100u, 0u, 3u) == PB_WAIT_TIMEOUT,
          "T2a no progress, used 0 -> TIMEOUT");
    CHECK(pb_wait_decide(100u, 100u, 1u, 3u) == PB_WAIT_TIMEOUT,
          "T2b no progress, used 1 -> TIMEOUT");
    CHECK(pb_wait_decide(100u, 100u, 0u, 0xFFFFFFFFu) == PB_WAIT_TIMEOUT,
          "T2c no progress, effectively unbounded cap -> TIMEOUT (progress-driven)");
    CHECK(pb_wait_decide(0u, 0u, 0u, 1000u) == PB_WAIT_TIMEOUT,
          "T2d no progress from a zero counter (nothing generated yet) -> TIMEOUT");
    CHECK(pb_wait_decide(0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 3u) == PB_WAIT_TIMEOUT,
          "T2e no progress AT the wrap value -> TIMEOUT");

    /* ---------------------------------------------------------------------------------
     * T3: WRAP — the pin. The counter is uint32 and wraps once per 2^32 generated tokens;
     * across that wrap the newest value is numerically the SMALLEST. Any implementation
     * that asks `now > start` returns TIMEOUT here and restarts a PB that is mid-answer.
     * --------------------------------------------------------------------------------- */
    CHECK(pb_wait_decide(0xFFFFFFFFu, 0u, 0u, 3u) == PB_WAIT_EXTEND,
          "T3a WRAP 0xFFFFFFFF -> 0 is progress -> EXTEND (ordering impl returns TIMEOUT)");
    CHECK(pb_wait_decide(0xFFFFFFFFu, 5u, 1u, 3u) == PB_WAIT_EXTEND,
          "T3b WRAP 0xFFFFFFFF -> 5 is progress -> EXTEND (ordering impl returns TIMEOUT)");
    CHECK(pb_wait_decide(0x7FFFFFFFu, 0x80000000u, 0u, 3u) == PB_WAIT_EXTEND,
          "T3c sign-bit crossing 0x7FFFFFFF -> 0x80000000 -> EXTEND (signed impl returns TIMEOUT)");
    CHECK(pb_wait_decide(0xFFFFFFFEu, 0xFFFFFFFFu, 0u, 3u) == PB_WAIT_EXTEND,
          "T3d adjacent below the wrap 0xFFFFFFFE -> 0xFFFFFFFF -> EXTEND");

    /* ---------------------------------------------------------------------------------
     * T4: CAP — the extension budget, and its cap == 0 degenerate. cap == 0 must be exactly
     * today's behaviour (ALWAYS timeout, progress or not): that is the KVM negative-control
     * gate leg, and it must hold at the wrap values too, where progress is most "obvious".
     * --------------------------------------------------------------------------------- */
    CHECK(pb_wait_decide(100u, 101u, 3u, 3u) == PB_WAIT_TIMEOUT,
          "T4a progress but used == cap (3 == 3) -> TIMEOUT (budget spent)");
    CHECK(pb_wait_decide(100u, 101u, 5u, 3u) == PB_WAIT_TIMEOUT,
          "T4b progress but used > cap (5 > 3) -> TIMEOUT");
    CHECK(pb_wait_decide(100u, 101u, 0u, 0u) == PB_WAIT_TIMEOUT,
          "T4c cap 0 with progress -> TIMEOUT (negative control: today's behaviour)");
    CHECK(pb_wait_decide(100u, 100u, 0u, 0u) == PB_WAIT_TIMEOUT,
          "T4d cap 0 without progress -> TIMEOUT");
    CHECK(pb_wait_decide(0xFFFFFFFFu, 0u, 0u, 0u) == PB_WAIT_TIMEOUT,
          "T4e cap 0 at the WRAP values -> TIMEOUT (degenerate holds across the wrap)");
    CHECK(pb_wait_decide(100u, 101u, 0xFFFFFFFFu, 0u) == PB_WAIT_TIMEOUT,
          "T4f cap 0 with a saturated used count -> TIMEOUT (no unsigned value is < 0)");

    /* ---------------------------------------------------------------------------------
     * T5: MUTATION TEETH (repo §6.10 discipline).
     *
     * The mutants below are NOT encoded as code — they are applied to pb_progress.c at build
     * time, run, and reverted, with the failing assertion names recorded. Documented here so
     * the teeth can be reproduced without re-deriving them:
     *
     *   MUTANT A — replace `progress_now != progress_start` with `progress_now > progress_start`
     *              (ordering instead of inequality).
     *              MUST FAIL: T3a, T3b. Across the wrap the newest counter value is the
     *              smallest, so `>` reads a live generation as no progress. T3c/T3d still pass
     *              (they do not cross the wrap), which is exactly why T3a/T3b must exist.
     *
     *   MUTANT B — delete the `windows_used < window_cap` check (always treat the budget as
     *              available).
     *              MUST FAIL: T4a, T4b, T4c, T4e, and the T5a sweep. Removing the budget check
     *              also removes the cap == 0 degenerate, because that degenerate falls out of
     *              this same unsigned comparison rather than a special case.
     *
     * T5a below is a small exhaustive re-derivation of the conjunction over used x cap, using
     * NON-wrapping counter values on purpose: it must discriminate mutant B without also
     * firing on mutant A, so each mutant's failures point at the condition it broke.
     * --------------------------------------------------------------------------------- */
    int sweep_ok = 1;
    for (uint32_t used = 0u; used <= 4u; used++) {
        for (uint32_t cap = 0u; cap <= 4u; cap++) {
            /* moved == 0 -> counter unchanged; moved == 1 -> counter advanced by one token. */
            for (uint32_t moved = 0u; moved <= 1u; moved++) {
                uint32_t start = 1000u;
                uint32_t now   = 1000u + moved;
                pb_wait_verdict_t want = (moved && used < cap) ? PB_WAIT_EXTEND : PB_WAIT_TIMEOUT;
                if (pb_wait_decide(start, now, used, cap) != want) {
                    sweep_ok = 0;
                    printf("        sweep mismatch: used=%u cap=%u moved=%u\n", used, cap, moved);
                }
            }
        }
    }
    CHECK(sweep_ok, "T5a sweep used 0..4 x cap 0..4 x moved 0..1 == (moved && used < cap)");

    /* Pure function of its four arguments: no state, so the same call twice is the same
     * verdict. Cheap, but it is the property that lets the caller re-ask at every window
     * boundary without carrying anything but the two loaded counter values. */
    CHECK(pb_wait_decide(0xFFFFFFFFu, 0u, 1u, 3u) == pb_wait_decide(0xFFFFFFFFu, 0u, 1u, 3u),
          "T5b same arguments -> same verdict (pure, stateless)");

    printf("=== Results: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
