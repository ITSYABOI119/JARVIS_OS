/*
 * Phase 6 A9/2 — host test for the PB crash-loop window (pb_health.h).
 *
 * Pins the rule that decides whether the box declares itself permanently degraded: five restarts
 * IN A WINDOW mean PB is crash-looping, five restarts spread across a week of healthy service mean
 * nothing. Getting that wrong is silent — no crash, no error line, just a box that serves cache-only
 * from then on — so every group here exists to make one way of getting it wrong fail loudly.
 *
 * T2 carries the weight and is the reason the module exists. K/M2b-2 STEP-2 defined
 * KM2B_HEALTHY_RESET and zeroed the counter on every restart but NEVER INCREMENTED IT, silently
 * turning a windowed bound into a LIFETIME cap. That shipped and was found on the box, because
 * main_x86.c is seL4-only and is never host-compiled — no test could have caught it. T2 pins BOTH
 * directions of the distinction, and mutant A in T7 reproduces the original defect exactly.
 *
 * Build: gcc -Wall -Werror -O2 -std=c11 -I. test_pb_health.c pb_health.c -o test_pb_health
 *        && ./test_pb_health
 */

#include "pb_health.h"
#include <stdio.h>

/* The deployed compile constants (main_x86.c KM2B_CRASHLOOP_BOUND / KM2B_HEALTHY_RESET). Used
 * throughout so the groups exercise the shipped operating point rather than a convenient one. */
#define TEST_BOUND  5u
#define TEST_RESET  25u

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  PASS  %s\n", name); } \
    else      { g_fail++; printf("  FAIL  %s\n", name); } \
} while (0)

int main(void)
{
    printf("=== test_pb_health (Phase 6 A9/2 — PB crash-loop window) ===\n");

    /* ---------------------------------------------------------------------------------
     * T1: a window OPENS on a restart and CLOSES after enough healthy responses. The whole
     * sliding-window lifecycle, one turn at a time.
     * --------------------------------------------------------------------------------- */
    {
        pb_health_t h;
        pb_health_init(&h);
        CHECK(h.window == 0 && h.healthy == 0, "T1a init zeroes both counters");

        CHECK(pb_health_on_restart(&h, TEST_BOUND) == 0, "T1b first restart does not trip the bound");
        CHECK(h.window == 1 && h.healthy == 0, "T1c restart opens the window (window 1, healthy 0)");

        /* reset_at - 1 healthy responses: the window must stay OPEN and every call report 0. */
        int early_trip = 0;
        for (uint32_t i = 1u; i < TEST_RESET; i++) {
            if (pb_health_on_healthy(&h, TEST_RESET) != 0)
                early_trip = 1;
            if (h.window != 1 || h.healthy != i)
                early_trip = 1;
        }
        CHECK(!early_trip, "T1d reset_at-1 healthy responses: window stays open, all return 0");
        CHECK(h.window == 1 && h.healthy == TEST_RESET - 1u,
              "T1e counters after reset_at-1 healthy (window 1, healthy reset_at-1)");

        /* The value the caller PRINTS on the reset line is read after the increment and before the
         * zeroing, and is always exactly reset_at. Read it here the same way. */
        uint32_t at_trip = h.healthy + 1u;
        CHECK(pb_health_on_healthy(&h, TEST_RESET) == 1, "T1f the reset_at-th healthy response closes the window");
        CHECK(at_trip == TEST_RESET, "T1g the healthy count at trip time is exactly reset_at (what the caller prints)");
        CHECK(h.window == 0 && h.healthy == 0, "T1h a closed window zeroes both counters");

        /* Exactly once: further healthy responses have no window to close, so they report 0. */
        int extra_trip = 0;
        for (int i = 0; i < 3 * (int)TEST_RESET; i++) {
            if (pb_health_on_healthy(&h, TEST_RESET) != 0)
                extra_trip = 1;
        }
        CHECK(!extra_trip, "T1i the window closes exactly once (later healthy responses report 0)");
    }

    /* ---------------------------------------------------------------------------------
     * T2: THE PROVEN SHIPPED DEFECT — window vs lifetime. Both directions, because either one
     * alone is satisfied by a broken implementation:
     *   (a) alone is satisfied by a LIFETIME cap (the shipped defect: it also latches at 5).
     *   (b) alone is satisfied by an implementation that never latches at all.
     * Only the pair distinguishes a real sliding window. See the file header for the history.
     * --------------------------------------------------------------------------------- */
    {
        /* (a) No healthy responses ever delivered: the bound-th restart latches, and unrelated
         * activity in between changes nothing. The "unrelated events" are deliberately the calls a
         * real caller interleaves — the guard predicate, and work on a SECOND handle — so this also
         * shows the state is per-handle rather than hidden in a global. */
        pb_health_t h, other;
        pb_health_init(&h);
        pb_health_init(&other);

        int tripped_at = 0, spurious = 0;
        for (uint32_t r = 1u; r <= TEST_BOUND; r++) {
            (void)pb_health_may_restart(0, 0);
            (void)pb_health_on_restart(&other, TEST_BOUND);
            (void)pb_health_on_healthy(&other, TEST_RESET);

            int trip = pb_health_on_restart(&h, TEST_BOUND);
            if (trip && tripped_at == 0)
                tripped_at = (int)r;
            else if (trip)
                spurious = 1;
        }
        CHECK(tripped_at == (int)TEST_BOUND && !spurious,
              "T2a no healthy responses: the bound-th restart latches, interleaved activity is irrelevant");

        /* (b) reset_at healthy responses between every restart: the window closes each time, so a
         * true sliding window NEVER latches however long this runs. A lifetime cap latches on the
         * 5th restart here — which is exactly what the shipped defect did. */
        pb_health_t w;
        pb_health_init(&w);
        int latched = 0;
        const int rounds = 4 * (int)TEST_BOUND;   /* N = 4 -> 20 restarts, 4x the bound */
        for (int r = 0; r < rounds; r++) {
            if (pb_health_on_restart(&w, TEST_BOUND) != 0)
                latched = 1;
            for (uint32_t i = 0; i < TEST_RESET; i++)
                (void)pb_health_on_healthy(&w, TEST_RESET);
        }
        CHECK(!latched,
              "T2b bound*4 restarts with a full healthy window between each NEVER latch (window, not lifetime)");
        CHECK(w.window == 0, "T2c the window is closed after the last healthy run");
    }

    /* ---------------------------------------------------------------------------------
     * T3: the terminal latch. This module does NOT own the degraded flag — the caller latches it
     * on the return value — so what is testable here is the CALLER's contract: once latched, the
     * guard refuses every restart regardless of the counters, and nothing but a reboot (re-init)
     * clears the state. See the header for why the flag lives in the caller.
     * --------------------------------------------------------------------------------- */
    {
        CHECK(pb_health_may_restart(0, 0) == 1, "T3a idle and alive -> a restart may proceed");
        CHECK(pb_health_may_restart(1, 0) == 0, "T3b a restart already in progress -> refused");
        CHECK(pb_health_may_restart(0, 1) == 0, "T3c degraded -> refused");
        CHECK(pb_health_may_restart(1, 1) == 0, "T3d both flags -> refused");

        /* The latch is terminal: no amount of subsequent health changes the guard's answer, because
         * the guard does not consult the counters at all. Drive the counters through a full close
         * and re-check. */
        pb_health_t h;
        pb_health_init(&h);
        (void)pb_health_on_restart(&h, TEST_BOUND);
        int freed = 0;
        for (uint32_t i = 0; i < 4u * TEST_RESET; i++) {
            (void)pb_health_on_healthy(&h, TEST_RESET);
            if (pb_health_may_restart(0, 1) != 0)
                freed = 1;
        }
        CHECK(!freed, "T3e once the caller has latched, no sequence of healthy responses re-permits a restart");
        CHECK(h.window == 0 && h.healthy == 0, "T3f the counters still track normally under a latch (unaware of it)");

        /* Only re-init clears the counters — the reboot case. */
        (void)pb_health_on_restart(&h, TEST_BOUND);
        CHECK(h.window == 1, "T3g a restart re-opens the window even while the caller is latched");
        pb_health_init(&h);
        CHECK(h.window == 0 && h.healthy == 0, "T3h re-init (reboot) is what clears the counters");
    }

    /* ---------------------------------------------------------------------------------
     * T4: ORDERING — bump first, then test. The restart that trips the bound is still COUNTED as
     * executed, and the trip is reported on THAT call rather than the next. This is what makes the
     * deployed output read [RESTART] ... window=5 followed by [FATAL]; an implementation testing
     * before the bump would print window=4 and trip one restart late.
     * --------------------------------------------------------------------------------- */
    {
        pb_health_t h;
        pb_health_init(&h);
        int reported_on = 0, wrong_window = 0;
        for (uint32_t r = 1u; r <= TEST_BOUND; r++) {
            int trip = pb_health_on_restart(&h, TEST_BOUND);
            if (h.window != r)
                wrong_window = 1;            /* the window value the caller prints on this line */
            if (trip && reported_on == 0)
                reported_on = (int)r;
        }
        CHECK(reported_on == (int)TEST_BOUND, "T4a the trip is reported on the bound-th call, not the next");
        CHECK(h.window == TEST_BOUND, "T4b the tripping restart is still counted (window == bound after it)");
        CHECK(!wrong_window, "T4c every call leaves the window at its printable running value");

        /* A caller honouring the contract latches here, and its guard then refuses the next restart
         * — which is why the module never has to reason about calls beyond the trip. */
        CHECK(pb_health_may_restart(0, 1) == 0, "T4d after the caller latches, the guard refuses the next restart");
    }

    /* ---------------------------------------------------------------------------------
     * T5: the `window > 0` SHORT-CIRCUIT. The deployed site is one && chain, so with no window
     * open the increment DOES NOT RUN — and that is observable, not cosmetic. The state argument
     * for calling the guard redundant is correct (a restart zeroes the count anyway) and the OUTPUT
     * argument defeats it: without the guard the count climbs on a healthy box, reaches reset_at,
     * and prints "window reset (N healthy inferences)" every reset_at responses forever, with no
     * restart having ever happened.
     *
     * So this group asserts the COUNTER IS STILL ZERO afterwards, not merely that the return was 0.
     * A return-only assertion would pass against an implementation that increments and discards.
     * --------------------------------------------------------------------------------- */
    {
        pb_health_t h;
        pb_health_init(&h);
        int any_trip = 0;
        /* The count is deliberately NOT a multiple of reset_at. Measured: at 4*TEST_RESET an
         * implementation missing the guard increments freely, hits reset_at, zeroes itself, and
         * lands back on 0 — so T5b passed against that mutant by arithmetic coincidence and proved
         * nothing. +3 leaves a runaway counter mid-cycle, where the assertion can see it. */
        for (uint32_t i = 0; i < 4u * TEST_RESET + 3u; i++) {
            if (pb_health_on_healthy(&h, TEST_RESET) != 0)
                any_trip = 1;
        }
        CHECK(!any_trip, "T5a healthy responses with no window open never report a reset");
        CHECK(h.healthy == 0, "T5b ... and the healthy counter is STILL 0 (the increment never ran)");
        CHECK(h.window == 0, "T5c ... and no window was invented");

        /* The same guard after a window has been opened and closed again: back to a no-op. */
        (void)pb_health_on_restart(&h, TEST_BOUND);
        for (uint32_t i = 0; i < TEST_RESET; i++)
            (void)pb_health_on_healthy(&h, TEST_RESET);
        CHECK(h.window == 0 && h.healthy == 0, "T5d window closed again");
        (void)pb_health_on_healthy(&h, TEST_RESET);
        CHECK(h.healthy == 0, "T5e a healthy response after the window closed still does not increment");
    }

    /* ---------------------------------------------------------------------------------
     * T6: guards, the degenerate parameters, and the arithmetic bounds.
     * --------------------------------------------------------------------------------- */
    {
        /* NULL-safety on every function that takes a handle. 0 is the fail-safe answer for both:
         * a call that examined nothing must not tell the caller to degrade or to log a reset. */
        CHECK(pb_health_on_restart(NULL, TEST_BOUND) == 0, "T6a on_restart(NULL) -> 0 (never orders a degrade)");
        CHECK(pb_health_on_healthy(NULL, TEST_RESET) == 0, "T6b on_healthy(NULL) -> 0");
        pb_health_init(NULL);   /* must not fault */
        CHECK(1, "T6c init(NULL) is a no-op (did not fault)");

        /* bound == 0 degenerates to always-trip: every unsigned value is >= 0. Documented as
         * following pb_wait_decide's cap == 0 precedent; deployed bound is the constant 5. */
        pb_health_t z;
        pb_health_init(&z);
        CHECK(pb_health_on_restart(&z, 0u) == 1, "T6d bound 0 degenerates: the first restart trips");
        CHECK(z.window == 1, "T6e ... and the restart is still counted");

        /* reset_at == 0 degenerates to closing on the first healthy response — but only while a
         * window is open, so the short-circuit still governs. */
        pb_health_t r;
        pb_health_init(&r);
        CHECK(pb_health_on_healthy(&r, 0u) == 0, "T6f reset_at 0 with NO window open is still a no-op");
        CHECK(r.healthy == 0, "T6g ... and still does not increment");
        (void)pb_health_on_restart(&r, TEST_BOUND);
        CHECK(pb_health_on_healthy(&r, 0u) == 1, "T6h reset_at 0 with a window open closes it immediately");
        CHECK(r.window == 0 && r.healthy == 0, "T6i ... zeroing both counters");

        /* No uint32 wrap is reachable for a caller honouring the contract: the window stops at the
         * bound (the caller latches and its guard then refuses every later restart) and the healthy
         * count stops at reset_at (it is zeroed the moment it reaches it). Drive a long mixed run
         * and watch the maxima. This is a property of the CALLER's discipline as much as the
         * module's: nothing here prevents unbounded growth if a caller ignores the return value and
         * keeps restarting, which at ~4 billion restarts is not a reachable state on this box. */
        pb_health_t m;
        pb_health_init(&m);
        uint32_t max_window = 0, max_healthy = 0;
        int latched = 0;
        for (int step = 0; step < 4000 && !latched; step++) {
            if ((step % 7) == 0) {
                if (pb_health_may_restart(0, latched)) {
                    if (pb_health_on_restart(&m, TEST_BOUND) != 0)
                        latched = 1;        /* the caller latches, exactly as the contract requires */
                }
            } else {
                (void)pb_health_on_healthy(&m, TEST_RESET);
            }
            if (m.window  > max_window)  max_window  = m.window;
            if (m.healthy > max_healthy) max_healthy = m.healthy;
        }
        CHECK(max_window <= TEST_BOUND, "T6j the window never exceeds the bound (the caller's latch stops it)");
        CHECK(max_healthy <= TEST_RESET, "T6k the healthy count never exceeds reset_at (zeroed on reaching it)");
        CHECK(latched, "T6l the mixed run did reach the latch (the bounds above are not vacuous)");
    }

    /* ---------------------------------------------------------------------------------
     * T7: MUTATION TEETH (repo §6.10 discipline).
     *
     * Not encoded as code — applied to pb_health.c at build time, run, reverted. The failure sets
     * below are MEASURED, not predicted: two of the three predictions written before the run were
     * wrong, so they are recorded as observed. An UNMUTATED control run is shown green first, or a
     * mutant that failed to apply reads as a mutant that was caught.
     *
     *   MUTANT A — delete the `++` from `if (++h->healthy < reset_at)` in pb_health_on_healthy
     *              (test the counter without ever incrementing it).
     *              FAILS 10: T1d T1e T1f T1g T1h T2b T2c T3f T3g T5d.
     *              *** THIS IS LITERALLY THE SHIPPED DEFECT. *** K/M2b-2 STEP-2 defined
     *              KM2B_HEALTHY_RESET and zeroed the counter on restart but never incremented it,
     *              so the window never closed and the bound became a LIFETIME cap. T2b names the
     *              consequence: with this mutation applied, four times the bound in widely-spaced
     *              restarts latches the box into permanent degraded mode.
     *
     *   MUTANT B — test the bound BEFORE the bump in pb_health_on_restart.
     *              FAILS 3: T2a T4a T6j.
     *              *** THE MUTATION MUST REPLACE THE POST-BUMP `return`, NOT MERELY PREPEND A
     *              PRE-BUMP TEST. *** A first attempt did the latter, leaving both tests in place —
     *              which trips at exactly the same time as the real code, so the suite stayed green
     *              at 41 PASS. That was a mis-constructed mutant, not a gap in T4, and it is only
     *              distinguishable by reading the diff rather than trusting that "a mutation was
     *              applied" means "the intended behaviour changed".
     *              It does NOT fail T4b: the bump still happens, so the window still ends at the
     *              bound. Only the REPORT timing moves, which is why T4a exists separately.
     *
     *   MUTANT C — delete the `if (h->window == 0) return 0;` short-circuit in pb_health_on_healthy.
     *              FAILS 5: T1i T5a T5b T5e T6f.
     *              T5b is the one that matters and it nearly did not fire: with T5a's loop set to
     *              an exact multiple of reset_at, a runaway counter increments, hits reset_at,
     *              zeroes itself and lands back on 0 — so the assertion passed by arithmetic
     *              coincidence. The loop is now a non-multiple (+3) so a runaway counter is caught
     *              mid-cycle. It does NOT fail T6g, where reset_at == 0 makes the runaway counter
     *              zero itself immediately; T6f's return-value assertion is what catches it there.
     * --------------------------------------------------------------------------------- */

    printf("=== Results: %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
