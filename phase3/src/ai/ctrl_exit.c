/*
 * JARVIS AI-OS — control-IN exit verdict (Phase 6 A9/3). See ctrl_exit.h.
 *
 * Every value below is a faithful transcription of pa_ctrl_gate's four exit
 * paths as shipped at boot 52 (main_x86.c), with ONE deliberate departure:
 * the disagreement edge (got && resp_len == 0) is REPAIRED here, not
 * reproduced — an empty response replies verdict 3 and is not counted
 * answered (see the arm-3 comment below and ctrl_exit.h's repair note).
 *
 * The history matters and is one sentence: at A9/3 that edge WAS
 * pinned-not-repaired, because a behaviour-neutral extraction that quietly
 * fixed it would have been a behaviour change hiding inside a refactor. It
 * was repaired on 2026-08-11 once measured reachable from shipped PB.
 */

#include "ctrl_exit.h"
#include "episodic_store.h"   /* EPI_OUT_*, EPI_ACT_* */
#include "action_audit.h"     /* AUDIT_*, AUDIT_OUT_* */
#include "shmem_ipc.h"        /* PB_STOP_* (resolved via -I src/ipc, like the callers) */

void ctrl_exit_decide(int refused, int served_locally, int dispatch_ok,
                      int got, int faulted, int resp_len, uint8_t stop_reason,
                      ctrl_exit_decision_t *out)
{
    /* Fully overwrite: no field of a previous decision can leak through. */
    out->reply_verdict  = 3;                   /* fail-safe default (arm 4) */
    out->epi_write      = 1;
    out->epi_outcome    = EPI_OUT_ERROR;
    out->epi_action     = EPI_ACT_CONTROL_IN;
    out->jact_verdict   = AUDIT_EXECUTED;
    out->jact_outcome   = AUDIT_OUT_FAIL;
    out->count_answered = 0;
    out->count_blocked  = 0;
    out->ack_pb         = 0;
    out->miss_pb        = 0;
    out->close_window   = 0;

    /* Arm 1 — QS_REFUSE. Exits before routing: no episodic record (the raw
     * query must never enter the store; the audit carries the LABEL only),
     * BLOCKED/NA, the blocked counter, and no PB state was touched. */
    if (refused) {
        out->reply_verdict  = 1;
        out->epi_write      = 0;
        out->jact_verdict   = AUDIT_BLOCKED;
        out->jact_outcome   = AUDIT_OUT_NA;
        out->count_blocked  = 1;
        return;
    }

    /* Arm 2 — the §5-F degraded gate. Only a would-be PB dispatch hits it
     * (SYSFACTS/DECLINE are served from PA state precisely so they stay
     * answerable while PB is down). Placed BEFORE the duty fold in the
     * caller, so no window opens and none may be closed. Honest FAIL,
     * audited EXECUTED/FAIL to mirror the wake lane's degraded semantics. */
    if (!served_locally && !dispatch_ok) {
        out->reply_verdict = 2;
        /* epi ERROR / tag CONTROL_IN / EXECUTED / FAIL: the defaults above. */
        return;
    }

    /* Arm 3 — the answered arm (served_locally forces got upstream). */
    if (got) {
        /* THE REPAIR (2026-08-11). A response arrived but carried NO usable
         * bytes, so there is no answer to report: verdict 3 (failed), and the
         * answered counter does NOT count it. The store already said ERROR and
         * the audit already said FAIL — this makes what the USER is told agree
         * with them, rather than handing over silence labelled "answered".
         *
         * REACHABLE, which is why it is repaired and no longer merely pinned:
         * inference_server.c's `if (text_len == 0)` branch sends an explicit
         * zero-length MSG_RESPONSE carrying the ORIGINAL seq, and n_gen == 0
         * occurs whenever the first sampled token is the terminator (the
         * 9d70f7f stop check breaks BEFORE storing). PA reads pk_len == 0 ->
         * copy = 0 -> got = 1, roff = 0.
         *
         * WHICH stop_reason ACCOMPANIES THE EMPTY RESPONSE — corrected
         * 2026-08-12, because the first version of this comment named the
         * wrong one and that is the kind of error that costs a later session
         * an afternoon:
         *
         *   REAL trigger      MODEL_ENDED + empty. With n_gen == 0 and the
         *                     deployed caps (PB_GEN_MAX_WORKLOAD 50 /
         *                     PB_GEN_MAX_CONTROL_IN 250) the test
         *                     `n_gen >= max_tokens` is FALSE, so stop_reason
         *                     stays PB_STOP_MODEL_ENDED. Host-proven by T6.
         *   PROBE/box evidence CAP + empty. Only PB_GEN_EMPTYPROBE_TOKENS == 0
         *                     makes 0 >= max_tokens true. That is what the KVM
         *                     leg produced, and it proves the ORDERING — not
         *                     the reachability. Host-pinned by T5c.
         *
         * THE MAPPING IS stop_reason-INDEPENDENT for an empty response: the
         * test below is on resp_len alone, so both shapes land on verdict 3.
         * The ordering still matters, for exactly one combination — with the
         * truncation test first, CAP + empty would report verdict 4 and the
         * repair would be defeated by its own probe. Nothing was cut off;
         * nothing was produced.
         *
         * (Commit 9155f3f's message carries the same misidentification and is
         * immutable history — a record is a snapshot; this is the correction.) */
        if (resp_len == 0) {
            out->reply_verdict  = 3;
            out->count_answered = 0;
        } else {
            /* #9: ONLY CAP and KV_FULL are truncation, and never for a locally-
             * served answer (no generation ran — the latch would belong to some
             * earlier inference). UNKNOWN reads "not truncated": a stats message
             * that never arrived must not become a fabricated claim. */
            if (!served_locally &&
                (stop_reason == PB_STOP_CAP || stop_reason == PB_STOP_KV_FULL))
                out->reply_verdict = 4;
            else
                out->reply_verdict = 0;
            out->count_answered = 1;
        }

        /* Unchanged by the repair — these were already failed-shaped for an
         * empty response, which is precisely what made the old verdict 0 a
         * disagreement rather than a consistent (if wrong) reading. */
        out->epi_outcome    = (resp_len > 0) ? EPI_OUT_OK : EPI_OUT_ERROR;
        out->epi_action     = (served_locally && resp_len > 0)
                              ? EPI_ACT_CONTROL_IN_LOCAL : EPI_ACT_CONTROL_IN;
        out->jact_outcome   = (resp_len > 0) ? AUDIT_OUT_OK : AUDIT_OUT_FAIL;

        /* §3 honesty guards: a locally-served answer is NOT evidence of PB
         * contact (no fake ACK for the K/M2c detector) and never opened a
         * duty window (closing one would fold an open WORKLOAD window). */
        out->ack_pb       = served_locally ? 0 : 1;
        out->close_window = served_locally ? 0 : 1;
        return;
    }

    /* Arm 4 — timeout, or fault-mid-route. Verdict 3, honest FAIL (the
     * defaults). A pure timeout feeds the PB miss-counter under
     * KM2B_LANE_CTRL; a fault was already funneled by pa_fault_check, so
     * feeding the miss counter too would double-count one failure. The
     * window permission mirrors the shared tail's !served_locally guard.
     *
     * THE ONE DELIBERATE DEVIATION, in an UNREACHABLE cell (adversarially
     * verified 2026-08-10): main_x86.c:4248 is a bare `if (!faulted)` — it
     * has no served_locally test, because served_locally forces got=1
     * upstream and this arm is unreachable for a local answer. The module
     * adds `!served_locally` as the fail-safe TOTAL-function extension: if
     * a future edit ever made a local answer reach this arm, feeding the
     * PB miss counter for a turn that never dispatched to PB would be a
     * FALSE wedge signal (three of those restart a healthy PB) — the exact
     * hazard §3 guard 1 exists for. Every REACHABLE state is identical to
     * the shipped code; T9 pins this version and says so. */
    out->miss_pb      = (!faulted && !served_locally) ? 1 : 0;
    out->close_window = served_locally ? 0 : 1;
}
