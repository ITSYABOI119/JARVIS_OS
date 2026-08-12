/*
 * test_ctrl_exit.c — Phase 6 A9/3: the control-IN exit verdict truth table.
 *
 * What is under test is AGREEMENT: what the user is TOLD (reply verdict),
 * what the STORE records (episodic outcome + tag), and what the AUDIT says
 * (JACT verdict + outcome) are decided together and must never drift apart —
 * plus the counters and the three §3 honesty guards.
 *
 * THE TABLE PINNED THE SHIPPED DISAGREEMENT UNTIL IT WAS PROVEN REACHABLE,
 * AND NOW PINS THE REPAIR. A9/3's T6 recorded the edge the extraction found
 * (got && resp_len==0 -> verdict 0 "answered" to the user, EPI_OUT_ERROR in
 * the store, AUDIT_OUT_FAIL in the audit, control_in_answered counted) and
 * deliberately failed any mutant that "fixed" it, because a behaviour-neutral
 * extraction must not smuggle in a behaviour change. The closures sweep then
 * MEASURED the edge reachable from shipped PB, so 2026-08-11 repairs it:
 * verdict 3, not counted. THE MUTANT DIRECTION THEREFORE INVERTS — what used
 * to be "the idealised fix must FAIL" is now "reverting to verdict 0 must
 * FAIL" — and T9's edge assertion inverts with it, from "exactly 24 poisoned
 * states" to "none", so a reintroduction ANYWHERE in the mapping is caught,
 * not just at the repaired line.
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
/* T11 keyword-clean way 2: the REAL gate, not a re-implementation. Links
 * shield_action.c + action_allowlist.c + shield_learn.c, the same set
 * test_km2b_trigger.c uses for the same reason. */
#include "shield_action.h"
#include "action_allowlist.h"

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

/* The canonical blocklist, TRANSCRIBED from shield_action.c rather than
 * imported: a test that shared the list would still pass if the list itself
 * were emptied, which is exactly the vacuity this scan exists to rule out. */
static const char *const KW[] = {
    "delete", "remove", "kill", "destroy", "format", "rm -rf", "drop table",
    "shutdown", "halt",
};

/* Bounded, case-insensitive substring scan, independent of shield_action.c's
 * implementation — so agreement between the two means something. */
static int kw_clean(const char *s, size_t n)
{
    for (unsigned k = 0; k < sizeof KW / sizeof KW[0]; k++) {
        size_t m = strlen(KW[k]);
        if (m > n) continue;
        for (size_t i = 0; i + m <= n; i++) {
            size_t j = 0;
            while (j < m) {
                char a = s[i + j], b = KW[k][j];
                if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
                if (a != b) break;
                j++;
            }
            if (j == m) return 0;
        }
    }
    return 1;
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
    /* T5c: empty + CAP. Once "defined-but-unreachable"; the REACHABLE shape
     * since the repair's probe — PB sets stop=CAP for n_gen >= max_tokens,
     * which includes 0 >= 0, so the max_tokens=0 probe leg lands EXACTLY
     * here. Emptiness outranks truncation: nothing was cut off, nothing was
     * produced. If this ever reads 4 again, the two tests have been reordered
     * and the repair is defeated by its own probe. */
    d = D(0, 0, 1, 1, 0, 0, PB_STOP_CAP);
    CHECK(d.reply_verdict == 3,              "T5c empty+CAP -> verdict 3, NOT 4 (emptiness outranks truncation)");
    CHECK(d.epi_outcome == EPI_OUT_ERROR,    "T5c empty+CAP -> ERROR stored");
    CHECK(d.count_answered == 0,             "T5c empty+CAP -> not counted answered");

    /* ── T6: THE REPAIRED EDGE — now pinning AGREEMENT ──
     * A seq-matched MSG_RESPONSE whose chunk carried 0 usable bytes. Until
     * 2026-08-11 this replied verdict 0 "answered" with EMPTY text while the
     * store said ERROR, the audit said FAIL and control_in_answered counted
     * it. Measured REACHABLE (inference_server.c's `if (text_len == 0)` send;
     * n_gen == 0 when the first sampled token is the terminator), so it was
     * repaired rather than documented. What the user is TOLD now agrees with
     * what the store and the audit already said. A mutant that reverts the
     * verdict to 0, or that re-counts it as answered, fails HERE. */
    d = D(0, 0, 1, 1, 0, 0, PB_STOP_MODEL_ENDED);
    CHECK(d.reply_verdict == 3,              "T6 REPAIRED: empty response -> verdict 3 (failed), not 0");
    CHECK(d.epi_outcome == EPI_OUT_ERROR,    "T6 REPAIRED: store records ERROR (unchanged)");
    CHECK(d.jact_outcome == AUDIT_OUT_FAIL,  "T6 REPAIRED: audit records FAIL (unchanged)");
    CHECK(d.count_answered == 0,             "T6 REPAIRED: NOT counted answered");
    CHECK(d.ack_pb == 1,                     "T6 REPAIRED: PB ack still permitted (a response DID arrive)");
    /* T6b: the repair must not have moved a NON-empty answer. One byte is the
     * whole difference between the repaired arm and the untouched one. */
    d = D(0, 0, 1, 1, 0, 1, PB_STOP_MODEL_ENDED);
    CHECK(d.reply_verdict == 0 && d.count_answered == 1 && d.epi_outcome == EPI_OUT_OK,
          "T6b one byte of answer -> still verdict 0, counted, OK stored");

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
        int states = 0, edge_states = 0, sweep_bad = 0, trig_lie = 0;
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
            /* Since the 2026-08-11 repair the answered ARM and an answered
             * OUTCOME are no longer the same thing: an empty response takes
             * the arm but reports failure. Every invariant that used to key
             * off arm_answered alone now says which of the two it means. */
            int arm_answered_nonempty = arm_answered && rl > 0;
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
            /* answered counting is exactly the answered arm WITH TEXT */
            ok &= ((d.count_answered == 1) == arm_answered_nonempty);
            ok &= ((d.reply_verdict == 0 || d.reply_verdict == 4) == arm_answered_nonempty);
            /* the repaired arm: took the answered arm, produced nothing,
             * reports failure — verdict 3, exactly as the timeout arm does */
            ok &= ((arm_answered && rl == 0) ? (d.reply_verdict == 3) : 1);
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
            /* verdict 4 only for a PB-generated answer WITH TEXT that hit
             * CAP/KV-FULL — an empty CAP stop is verdict 3 (T5c) */
            if (d.reply_verdict == 4)
                ok &= (!sl && gt && rl > 0 &&
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

            /* THE EDGE IS GONE: there is no state left in which the user
             * hears answered(0/4) while the store says ERROR. This assertion
             * carried the edge's exact extent (24 states) before the
             * 2026-08-11 repair; it now demands ZERO, so any reintroduction
             * anywhere in the mapping — not just at the repaired line — fails
             * the sweep. */
            int told_ok_stored_err =
                ((d.reply_verdict == 0 || d.reply_verdict == 4) &&
                 d.epi_write && d.epi_outcome == EPI_OUT_ERROR);
            ok &= (told_ok_stored_err == 0);
            if (told_ok_stored_err) edge_states++;

            /* THE TRIGGER INVARIANT — the exact defect this commit fixed,
             * pinned so it cannot return. No state may write the answered
             * literal into the durable audit while reporting the turn as not
             * answered. Checked over the whole sweep rather than at one site,
             * because the old bug was a fixed literal on a SHARED tail: it was
             * invisible from any single arm. */
            if (d.jact_trigger && strcmp(d.jact_trigger, "control-in answered") == 0
                && d.count_answered == 0)
                trig_lie++;
            /* and every state must carry a non-NULL trigger whose carried
             * length is its ACTUAL length — computed here, never trusted */
            ok &= (d.jact_trigger != NULL);
            if (d.jact_trigger)
                ok &= (d.jact_trigger_len == (uint16_t)strlen(d.jact_trigger));

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
        CHECK(trig_lie == 0,
              "T9 NO state carries the answered trigger while not counted answered");
        CHECK(sweep_bad == 0,  "T9 agreement invariants hold across the sweep");
        /* Was 24 (rf=0, (sl,dk) in {(0,1),(1,0),(1,1)}, got=1, fa in {0,1},
         * 4 stop values = 3*2*4) — the exact extent of the shipped edge.
         * The repair eliminates the family, so the only honest number is 0. */
        CHECK(edge_states == 0,
              "T9 NO told-answered/stored-ERROR state survives the repair");
    }

    /* ── T10: determinism / full overwrite (the poisoned-output D() helper
     * already proves no field survives; this pins call-to-call identity) ── */
    {
        ctrl_exit_decision_t a = D(0, 0, 1, 1, 0, 42, PB_STOP_CAP);
        ctrl_exit_decision_t b = D(0, 0, 1, 1, 0, 42, PB_STOP_CAP);
        CHECK(memcmp(&a, &b, sizeof a) == 0, "T10 deterministic");
    }

    /* ── T11: the JACT trigger — one literal per exit, keyword-clean both ways.
     * THE DEFECT: pa_ctrl_gate's SHARED tail wrote the fixed literal
     * "control-in answered" for BOTH the answered arm and the timeout/fault
     * arm, so after the T6 repair a turn reported to the user as NOT answered
     * (verdict 3, empty) still produced EXECUTED / FAIL / "control-in
     * answered" in the durable audit — the field a human reads off the store
     * months later, and the evidence base the 7-day exit rests on. ── */
    {
        printf("\n--- T11: JACT trigger per exit path ---\n");
        struct { const char *name; ctrl_exit_decision_t d; const char *want; } C[6];
        C[0].name = "T11 refused -> refused literal";
        C[0].d = D(1, 0, 1, 0, 0, 0, PB_STOP_UNKNOWN);   C[0].want = "control-in refused";
        C[1].name = "T11 degraded -> degraded literal";
        C[1].d = D(0, 0, 0, 0, 0, 0, PB_STOP_UNKNOWN);   C[1].want = "control-in degraded";
        C[2].name = "T11 answered -> answered literal";
        C[2].d = D(0, 0, 1, 1, 0, 180, PB_STOP_MODEL_ENDED); C[2].want = "control-in answered";
        C[3].name = "T11 truncated(4) is an ANSWER -> answered literal";
        C[3].d = D(0, 0, 1, 1, 0, 900, PB_STOP_CAP);     C[3].want = "control-in answered";
        C[4].name = "T11 EMPTY -> empty literal, NOT answered";
        C[4].d = D(0, 0, 1, 1, 0, 0, PB_STOP_MODEL_ENDED); C[4].want = "control-in empty response";
        C[5].name = "T11 fault -> fault literal";
        C[5].d = D(0, 0, 1, 0, 1, 0, PB_STOP_UNKNOWN);   C[5].want = "control-in fault";

        for (unsigned i = 0; i < 6; i++) {
            const ctrl_exit_decision_t *p = &C[i].d;
            CHECK(p->jact_trigger != NULL, "T11 trigger is non-NULL");
            CHECK(p->jact_trigger && strcmp(p->jact_trigger, C[i].want) == 0, C[i].name);
            /* length computed HERE, never trusted: a field echoing a wrong
             * constant would otherwise prove nothing */
            CHECK(p->jact_trigger &&
                  p->jact_trigger_len == (uint16_t)strlen(p->jact_trigger),
                  "T11 carried length == actual length");
            CHECK(p->jact_trigger_len <= ACT_TRIGGER_MAX, "T11 within ACT_TRIGGER_MAX");
            CHECK(p->jact_trigger && kw_clean(p->jact_trigger, p->jact_trigger_len),
                  "T11 keyword-clean (independent substring scan)");
        }

        /* timeout is DISTINCT from fault — the reply already distinguishes
         * them, and "did PB fault or simply never answer" is the first
         * question a reader of this trail asks */
        ctrl_exit_decision_t t = D(0, 0, 1, 0, 0, 0, PB_STOP_UNKNOWN);
        CHECK(t.jact_trigger && strcmp(t.jact_trigger, "control-in timeout") == 0,
              "T11 timeout literal is distinct from fault");

        /* the historical string is BYTE-IDENTICAL: boot-49 and boot-30 records
         * read exactly this, and staying comparable across boots matters */
        CHECK(C[2].d.jact_trigger_len == 19, "T11 'control-in answered' is still 19 bytes");

        /* KEYWORD-CLEAN WAY 2 — EXECUTED through the REAL shield_assess, using
         * ACTION_NOTIFY_ANOMALY (allowlisted; the test_km2b_trigger precedent).
         * DELIBERATELY NOT ACTION_CONTROL_IN_QUERY — see the pin below. */
        for (unsigned i = 0; i < 6; i++) {
            action_ctx_t ctx;
            ctx.query_key = 0;
            ctx.trigger = C[i].d.jact_trigger;
            ctx.trigger_len = C[i].d.jact_trigger_len;
            shield_action_result_t r = shield_assess(ACTION_NOTIFY_ANOMALY, &ctx, NULL, 0);
            CHECK(r.verdict != SHIELD_VERDICT_BLOCKED,
                  "T11 literal survives the REAL shield_assess (cannot self-block)");
        }

        /* TEETH for way 2: without this, the loop above could pass because the
         * gate never blocks anything rather than because the literals are clean */
        {
            action_ctx_t ctx;
            const char *poison = "control-in delete";
            ctx.query_key = 0; ctx.trigger = poison;
            ctx.trigger_len = (uint16_t)strlen(poison);
            shield_action_result_t r = shield_assess(ACTION_NOTIFY_ANOMALY, &ctx, NULL, 0);
            CHECK(r.verdict == SHIELD_VERDICT_BLOCKED, "T11 TEETH: a poisoned trigger DOES block");
            CHECK(kw_clean(poison, ctx.trigger_len) == 0, "T11 TEETH: the substring scan sees it too");
        }

        /* PINNED FINDING (measured 2026-08-12): shield_assess on the id this
         * site actually audits under returns BLOCKED even for a known-CLEAN
         * literal, because ACTION_CONTROL_IN_QUERY is an AUDIT TAG with no
         * allowlist entry — action_lookup fails and it refuses before scoring.
         * So a keyword-clean check written against that id could never fail,
         * clean or poisoned. Pinned so nobody "restores" it as the check. */
        {
            action_ctx_t ctx;
            ctx.query_key = 0;
            ctx.trigger = C[2].d.jact_trigger;
            ctx.trigger_len = C[2].d.jact_trigger_len;
            shield_action_result_t r = shield_assess(ACTION_CONTROL_IN_QUERY, &ctx, NULL, 0);
            CHECK(r.verdict == SHIELD_VERDICT_BLOCKED,
                  "T11 ACTION_CONTROL_IN_QUERY is unallowlisted -> blocks even a clean literal");
        }
    }

    printf("test_ctrl_exit: %d PASS, %d FAIL\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
