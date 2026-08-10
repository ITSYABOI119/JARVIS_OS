/*
 * test_ctrl_exit.c — Phase 6 A9/3: the control-IN exit verdict truth table.
 *
 * What is under test is AGREEMENT: what the user is TOLD (reply verdict),
 * what the STORE records (episodic outcome + tag), and what the AUDIT says
 * (JACT verdict + outcome) are decided together and must never drift apart —
 * plus the counters and the three §3 honesty guards.
 *
 * THE TABLE PINS SHIPPED BEHAVIOUR, NOT IDEALISED BEHAVIOUR. In particular
 * T6 pins the one disagreement edge the extraction FOUND (got && resp_len==0
 * -> verdict 0 "answered" to the user, EPI_OUT_ERROR in the store,
 * AUDIT_OUT_FAIL in the audit, and control_in_answered still counted), and
 * T9 proves that edge is the ONLY state family where the user hears
 * "answered" while the store says ERROR. A mutant that "fixes" the edge
 * FAILS T6 — behaviour-neutral extraction means the repair is a separate,
 * deliberate decision, not a silent side effect of a refactor.
 *
 * Compile (host):
 *   gcc -O2 -Wall -Werror -Iphase3/src/ai -Iphase3/src/ipc \
 *       phase3/src/ai/ctrl_exit.c phase3/src/ai/test_ctrl_exit.c -o test_ctrl_exit
 */

#include <stdio.h>
#include <string.h>

#include "ctrl_exit.h"
#include "episodic_store.h"
#include "action_audit.h"
#include "shmem_ipc.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do {                                     \
    if (cond) { g_pass++; }                                        \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static ctrl_exit_decision_t D(int refused, int sl, int dok, int got,
                              int faulted, int rlen, uint8_t stop)
{
    ctrl_exit_decision_t d;
    memset(&d, 0xAA, sizeof d);   /* prove full overwrite: poison first */
    ctrl_exit_decide(refused, sl, dok, got, faulted, rlen, stop, &d);
    return d;
}

int main(void)
{
    ctrl_exit_decision_t d;

    /* ── T1: refused (QS_REFUSE) — label-only audit, NO episodic record ── */
    d = D(1, 0, 1, 0, 0, 0, PB_STOP_UNKNOWN);
    CHECK(d.reply_verdict == 1,              "T1 refused -> verdict 1");
    CHECK(d.epi_write == 0,                  "T1 refused -> NO episodic record");
    CHECK(d.jact_verdict == AUDIT_BLOCKED,   "T1 refused -> JACT BLOCKED");
    CHECK(d.jact_outcome == AUDIT_OUT_NA,    "T1 refused -> JACT outcome NA");
    CHECK(d.count_blocked == 1,              "T1 refused -> blocked counted");
    CHECK(d.count_answered == 0,             "T1 refused -> never counted answered");
    CHECK(d.ack_pb == 0 && d.miss_pb == 0,   "T1 refused -> no PB ack/miss");
    CHECK(d.close_window == 0,               "T1 refused -> no window close");
    /* precedence: refused wins over every other flag combination */
    d = D(1, 1, 0, 1, 1, 99, PB_STOP_CAP);
    CHECK(d.reply_verdict == 1 && d.epi_write == 0 && d.count_blocked == 1,
          "T1b refused wins precedence over all other state");

    /* ── T2: the §5-F degraded gate (dead/model-bad PB, would-be dispatch) ── */
    d = D(0, 0, 0, 0, 0, 0, PB_STOP_UNKNOWN);
    CHECK(d.reply_verdict == 2,              "T2 degraded -> verdict 2");
    CHECK(d.epi_write == 1,                  "T2 degraded -> episodic written");
    CHECK(d.epi_outcome == EPI_OUT_ERROR,    "T2 degraded -> episodic ERROR");
    CHECK(d.epi_action == EPI_ACT_CONTROL_IN,"T2 degraded -> tag CONTROL_IN (3)");
    CHECK(d.jact_verdict == AUDIT_EXECUTED,  "T2 degraded -> JACT EXECUTED");
    CHECK(d.jact_outcome == AUDIT_OUT_FAIL,  "T2 degraded -> JACT FAIL");
    CHECK(d.count_answered == 0 && d.count_blocked == 0,
          "T2 degraded -> neither counter");
    CHECK(d.close_window == 0,               "T2 degraded -> placed before the fold: no window close");
    /* precedence: the gate sits BEFORE dispatch, so got cannot rescue it */
    d = D(0, 0, 0, 1, 0, 50, PB_STOP_MODEL_ENDED);
    CHECK(d.reply_verdict == 2,              "T2b degraded wins over got");

    /* ── T3: locally-served (SYSFACTS/DECLINE) — the §3 honesty guards ── */
    d = D(0, 1, 1, 1, 0, 20, PB_STOP_UNKNOWN);
    CHECK(d.reply_verdict == 0,              "T3 local -> verdict 0");
    CHECK(d.epi_outcome == EPI_OUT_OK,       "T3 local -> episodic OK");
    CHECK(d.epi_action == EPI_ACT_CONTROL_IN_LOCAL,
          "T3 local -> tag CTRLIN-LOCAL (4, never recallable)");
    CHECK(d.jact_outcome == AUDIT_OUT_OK,    "T3 local -> JACT OK");
    CHECK(d.count_answered == 1,             "T3 local -> counted answered");
    CHECK(d.ack_pb == 0,                     "T3 GUARD 1: local answer is NOT a PB ack");
    CHECK(d.close_window == 0,               "T3 GUARD 2: local answer never closes the duty window");
    /* T3b: the stale-latch guard — a locally-served answer can never be
     * truncated by PB (no generation ran; the latch belongs to some earlier
     * inference). This is the branch a missing !served_locally would break. */
    d = D(0, 1, 1, 1, 0, 20, PB_STOP_CAP);
    CHECK(d.reply_verdict == 0,              "T3b local + stale CAP latch -> verdict 0, NOT 4");
    /* T3c: defensive local-with-empty-text state (unreachable: PA always
     * renders text) — the tag guard keeps it off the recallable tag */
    d = D(0, 1, 1, 1, 0, 0, PB_STOP_UNKNOWN);
    CHECK(d.epi_action == EPI_ACT_CONTROL_IN,"T3c local empty -> tag falls back to CONTROL_IN");
    CHECK(d.epi_outcome == EPI_OUT_ERROR,    "T3c local empty -> episodic ERROR");
    /* T3d: a local answer must stay answerable while PB is down — the
     * degraded gate is skipped for served_locally (its condition requires
     * !served_locally), which is why "how long have you been up?" answered
     * on the boot-50 UNUSABLE box. */
    d = D(0, 1, 0, 1, 0, 20, PB_STOP_UNKNOWN);
    CHECK(d.reply_verdict == 0 && d.epi_action == EPI_ACT_CONTROL_IN_LOCAL,
          "T3d local + PB down -> still answered locally");

    /* ── T4: PB-generated answer, complete ── */
    d = D(0, 0, 1, 1, 0, 180, PB_STOP_MODEL_ENDED);
    CHECK(d.reply_verdict == 0,              "T4 answered -> verdict 0");
    CHECK(d.epi_outcome == EPI_OUT_OK,       "T4 answered -> episodic OK");
    CHECK(d.epi_action == EPI_ACT_CONTROL_IN,"T4 answered -> tag CONTROL_IN (recallable)");
    CHECK(d.jact_outcome == AUDIT_OUT_OK,    "T4 answered -> JACT OK");
    CHECK(d.count_answered == 1,             "T4 answered -> counted");
    CHECK(d.ack_pb == 1,                     "T4 answered -> genuine PB ack permitted");
    CHECK(d.close_window == 1,               "T4 answered -> window close permitted");
    CHECK(d.miss_pb == 0,                    "T4 answered -> no miss");
    /* T4b: stop latch UNKNOWN (stats message never arrived) -> never
     * fabricate a truncation claim */
    d = D(0, 0, 1, 1, 0, 180, PB_STOP_UNKNOWN);
    CHECK(d.reply_verdict == 0,              "T4b stop UNKNOWN -> verdict 0, never fabricated 4");

    /* ── T5: verdict 4 — answered-but-truncated (#9) ── */
    d = D(0, 0, 1, 1, 0, 1100, PB_STOP_CAP);
    CHECK(d.reply_verdict == 4,              "T5 stop=CAP -> verdict 4");
    CHECK(d.epi_outcome == EPI_OUT_OK,       "T5 verdict 4 is still ANSWERED: episodic OK");
    CHECK(d.jact_outcome == AUDIT_OUT_OK,    "T5 verdict 4 -> JACT OK (a marker, not an error)");
    CHECK(d.count_answered == 1,             "T5 verdict 4 -> counted answered");
    d = D(0, 0, 1, 1, 0, 900, PB_STOP_KV_FULL);
    CHECK(d.reply_verdict == 4,              "T5b stop=KV-FULL -> verdict 4");
    /* T5c: defined-but-unreachable (a CAP stop implies ~250 generated tokens,
     * so the text cannot be empty) — the mapping stays total and honest */
    d = D(0, 0, 1, 1, 0, 0, PB_STOP_CAP);
    CHECK(d.reply_verdict == 4 && d.epi_outcome == EPI_OUT_ERROR,
          "T5c truncated-empty edge: defined, ERROR stored");

    /* ── T6: THE SHIPPED DISAGREEMENT EDGE — pinned, not repaired ──
     * A seq-matched MSG_RESPONSE whose chunk carried 0 usable bytes: the user
     * hears "answered" (verdict 0, empty text), the store records ERROR, the
     * audit records FAIL, and control_in_answered still counts. This is what
     * boot-52-era pa_ctrl_gate DOES; a mutant that "fixes" any leg of it
     * fails here, which is the point — the repair is a separate decision. */
    d = D(0, 0, 1, 1, 0, 0, PB_STOP_MODEL_ENDED);
    CHECK(d.reply_verdict == 0,              "T6 EDGE: user is told answered (verdict 0)");
    CHECK(d.epi_outcome == EPI_OUT_ERROR,    "T6 EDGE: store records ERROR");
    CHECK(d.jact_outcome == AUDIT_OUT_FAIL,  "T6 EDGE: audit records FAIL");
    CHECK(d.count_answered == 1,             "T6 EDGE: still counted answered (shipped behaviour)");
    CHECK(d.ack_pb == 1,                     "T6 EDGE: PB ack still permitted (a response DID arrive)");

    /* ── T7: timeout ── */
    d = D(0, 0, 1, 0, 0, 0, PB_STOP_UNKNOWN);
    CHECK(d.reply_verdict == 3,              "T7 timeout -> verdict 3");
    CHECK(d.epi_outcome == EPI_OUT_ERROR,    "T7 timeout -> episodic ERROR");
    CHECK(d.epi_action == EPI_ACT_CONTROL_IN,"T7 timeout -> tag CONTROL_IN (stays tag 3)");
    CHECK(d.jact_verdict == AUDIT_EXECUTED,  "T7 timeout -> JACT EXECUTED");
    CHECK(d.jact_outcome == AUDIT_OUT_FAIL,  "T7 timeout -> JACT FAIL");
    CHECK(d.miss_pb == 1,                    "T7 timeout -> PB miss fed (KM2B_LANE_CTRL)");
    CHECK(d.ack_pb == 0,                     "T7 timeout -> no ack");
    CHECK(d.close_window == 1,               "T7 timeout -> window close permitted");
    CHECK(d.count_answered == 0,             "T7 timeout -> NOT counted answered");

    /* ── T8: fault-mid-route — the miss counter must NOT double-count ── */
    d = D(0, 0, 1, 0, 1, 0, PB_STOP_UNKNOWN);
    CHECK(d.reply_verdict == 3,              "T8 fault -> verdict 3");
    CHECK(d.miss_pb == 0,                    "T8 fault -> miss NOT fed (pa_fault_check funneled it)");
    CHECK(d.close_window == 1,               "T8 fault -> window close permitted");

    /* ── T9: exhaustive agreement sweep — every combination, every invariant.
     * 2^5 flag combos x 3 resp_len values x 4 stop values = 384 states. ── */
    {
        static const int  RL[]   = { 0, 7, 256 };
        static const uint8_t SP[] = { PB_STOP_UNKNOWN, PB_STOP_MODEL_ENDED,
                                      PB_STOP_CAP, PB_STOP_KV_FULL };
        int states = 0, edge_states = 0, sweep_bad = 0;
        for (int rf = 0; rf <= 1; rf++)
        for (int sl = 0; sl <= 1; sl++)
        for (int dk = 0; dk <= 1; dk++)
        for (int gt = 0; gt <= 1; gt++)
        for (int fa = 0; fa <= 1; fa++)
        for (unsigned ri = 0; ri < sizeof RL / sizeof RL[0]; ri++)
        for (unsigned si = 0; si < sizeof SP / sizeof SP[0]; si++) {
            int rl = RL[ri]; uint8_t sp = SP[si];
            d = D(rf, sl, dk, gt, fa, rl, sp);
            states++;

            int arm_degraded = !rf && !sl && !dk;
            int arm_answered = !rf && !arm_degraded && gt;
            int ok = 1;

            /* the decision is total and the verdict is always a defined value */
            ok &= (d.reply_verdict <= 4);
            /* verdict 1 <-> refused; refused never stores the query */
            ok &= ((d.reply_verdict == 1) == (rf != 0));
            ok &= ((d.epi_write == 0) == (rf != 0));
            ok &= ((d.count_blocked == 1) == (rf != 0));
            /* JACT verdict: BLOCKED for refused, EXECUTED everywhere else */
            ok &= (rf ? d.jact_verdict == AUDIT_BLOCKED
                      : d.jact_verdict == AUDIT_EXECUTED);
            /* answered counting is exactly the answered arm */
            ok &= ((d.count_answered == 1) == arm_answered);
            ok &= ((d.reply_verdict == 0 || d.reply_verdict == 4) == arm_answered);
            /* verdict 2/3 always mean ERROR stored + FAIL audited */
            if (d.reply_verdict == 2 || d.reply_verdict == 3)
                ok &= (d.epi_outcome == EPI_OUT_ERROR &&
                       d.jact_outcome == AUDIT_OUT_FAIL);
            /* store-OK, audit-OK and user-answered move together */
            if (d.epi_write)
                ok &= ((d.epi_outcome == EPI_OUT_OK) ==
                       (d.jact_outcome == AUDIT_OUT_OK));
            ok &= ((d.epi_write && d.epi_outcome == EPI_OUT_OK)
                   ? (d.reply_verdict == 0 || d.reply_verdict == 4) : 1);
            /* verdict 4 only for a PB-generated answer that hit CAP/KV-FULL */
            if (d.reply_verdict == 4)
                ok &= (!sl && gt &&
                       (sp == PB_STOP_CAP || sp == PB_STOP_KV_FULL));
            /* the LOCAL tag is exactly a locally-served non-empty answer */
            if (d.epi_write)
                ok &= ((d.epi_action == EPI_ACT_CONTROL_IN_LOCAL) ==
                       (arm_answered && sl && rl > 0));
            /* §3 guards: ack only on a genuine PB response; miss only on a
             * pure timeout; window close never for local / refused / degraded.
             * NOTE the `!sl` in the miss invariant pins the module's ONE
             * deliberate deviation, in an UNREACHABLE cell: main_x86.c:4248
             * has no served_locally test (served_locally forces got=1, so a
             * local answer cannot reach the timeout arm); the module adds it
             * as the fail-safe total-function extension. Every REACHABLE
             * state matches the shipped code exactly. */
            ok &= ((d.ack_pb == 1) == (arm_answered && !sl));
            ok &= ((d.miss_pb == 1) ==
                   (!rf && !arm_degraded && !gt && !fa && !sl));
            ok &= ((d.close_window == 1) == (!rf && !sl && dk));

            /* THE EDGE EXTENT: the user hears answered(0/4) while the store
             * says ERROR in EXACTLY the answered-arm states with empty text */
            int told_ok_stored_err =
                ((d.reply_verdict == 0 || d.reply_verdict == 4) &&
                 d.epi_write && d.epi_outcome == EPI_OUT_ERROR);
            ok &= (told_ok_stored_err == (arm_answered && rl == 0));
            if (told_ok_stored_err) edge_states++;

            if (!ok && sweep_bad < 5) {
                printf("SWEEP FAIL at rf=%d sl=%d dok=%d got=%d fa=%d rl=%d stop=%u "
                       "-> v=%u epi(w=%u,o=%u,a=%u) jact(%u,%u) cnt(a=%u,b=%u) "
                       "ack=%u miss=%u close=%u\n",
                       rf, sl, dk, gt, fa, rl, sp, d.reply_verdict, d.epi_write,
                       d.epi_outcome, d.epi_action, d.jact_verdict, d.jact_outcome,
                       d.count_answered, d.count_blocked, d.ack_pb, d.miss_pb,
                       d.close_window);
            }
            if (!ok) sweep_bad++;
        }
        CHECK(states == 384,   "T9 sweep covered all 384 states");
        CHECK(sweep_bad == 0,  "T9 agreement invariants hold across the sweep");
        /* answered arm with rl==0: rf=0, (sl,dk) in {(0,1),(1,0),(1,1)}, got=1,
         * fa in {0,1}, 4 stop values -> 3*2*4 = 24 poisoned states, no more */
        CHECK(edge_states == 24,
              "T9 the told-answered/stored-ERROR edge is EXACTLY the empty-text answered states");
    }

    /* ── T10: determinism / full overwrite (the poisoned-output D() helper
     * already proves no field survives; this pins call-to-call identity) ── */
    {
        ctrl_exit_decision_t a = D(0, 0, 1, 1, 0, 42, PB_STOP_CAP);
        ctrl_exit_decision_t b = D(0, 0, 1, 1, 0, 42, PB_STOP_CAP);
        CHECK(memcmp(&a, &b, sizeof a) == 0, "T10 deterministic");
    }

    printf("test_ctrl_exit: %d PASS, %d FAIL\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
