/*
 * JARVIS AI-OS — control-IN exit verdict (Phase 6 A9/3)
 *
 * The pure decision behind pa_ctrl_gate's FOUR exit paths: given the turn's
 * terminal state, decide the three things that must agree — what the USER is
 * told (the reply verdict), what the STORE records (episodic outcome + action
 * tag), and what the AUDIT says (JACT verdict + outcome) — plus the two
 * counters and the three §3 honesty guards (PB ack / PB miss / duty-window
 * close). Before this module those twelve outputs agreed only by the adjacency
 * of literals at 18 sites in main_x86.c, which is never host-compiled.
 *
 * THIS MODULE DECIDES; pa_ctrl_gate SEQUENCES. The shared exit is a recorded
 * 6-6 design property (ONE signed reply -> ONE JACT record -> ONE counter ->
 * ONE episodic write for all three routing classes), and the extraction
 * deliberately does NOT move any call or create a second exit — the call
 * order, the serial diagnostics, and the reply/trigger strings all stay in
 * main_x86.c. What moved is only the MAPPING from terminal state to verdicts.
 *
 * FAITHFUL, WITH ONE DELIBERATE REPAIR. A9/3 originally reproduced a shipped
 * disagreement — a seq-matched MSG_RESPONSE carrying 0 usable bytes (got &&
 * resp_len == 0) replied verdict 0 "answered" (empty text) while the store
 * recorded EPI_OUT_ERROR, the audit recorded AUDIT_OUT_FAIL, and
 * control_in_answered counted it — because a behaviour-neutral extraction
 * that quietly repaired it would have been a behaviour change hiding inside a
 * refactor. Its REACHABILITY was then measured (closures sweep 2026-08-11):
 * inference_server.c's `if (text_len == 0)` branch sends that zero-length
 * response with the ORIGINAL seq, and n_gen == 0 occurs when the first
 * sampled token is the terminator (the 9d70f7f check breaks BEFORE storing);
 * a negative tokenizer_decode, coerced to 0, reaches the same branch.
 *
 * REPAIRED 2026-08-11 (this module only): that state now replies verdict 3
 * (failed) and is NOT counted answered. The store/audit values are unchanged
 * — they were already failed-shaped, which is what made the old verdict a
 * disagreement rather than a consistent misreading. The empty test runs
 * BEFORE the CAP/KV-FULL truncation test on purpose: PB sets stop = CAP for
 * n_gen >= max_tokens, which includes 0 >= 0, so the reachable shape is
 * CAP with empty text and the other order would report it as verdict 4.
 * HONEST SCOPE: this fixes ONE agreement defect on ONE lane. The empty
 * generation itself is untouched and still possible; the user now learns the
 * turn FAILED instead of being handed silence labelled "answered".
 *
 * Host-pure: no seL4 dependency; the caller passes every input. The
 * km2b_miss / wake / pb_progress / ctrl_epi_index / pb_health precedent.
 */

#ifndef JARVIS_CTRL_EXIT_H
#define JARVIS_CTRL_EXIT_H

#include <stdint.h>

/* Everything a pa_ctrl_gate exit needs to know, in one place, so the reply /
 * store / audit can never drift apart silently. Fields are valid as a SET —
 * a caller must not mix decisions from two calls. */
typedef struct {
    uint8_t  reply_verdict;   /* 0 answered / 1 refused / 2 degraded / 3 failed
                               * / 4 answered-but-truncated (#9: a marker ON a
                               * genuine answer, never an error state) */
    uint8_t  epi_write;       /* 1 = write an episodic record. 0 ONLY for the
                               * refused exit — the raw query never enters the
                               * store (audit carries the reason LABEL alone) */
    uint8_t  epi_outcome;     /* EPI_OUT_OK / EPI_OUT_ERROR   (valid iff epi_write) */
    uint16_t epi_action;      /* EPI_ACT_CONTROL_IN / _LOCAL  (valid iff epi_write) */
    uint16_t jact_verdict;    /* AUDIT_BLOCKED / AUDIT_EXECUTED */
    uint16_t jact_outcome;    /* AUDIT_OUT_NA / AUDIT_OUT_OK / AUDIT_OUT_FAIL */
    uint8_t  count_answered;  /* 1 = bump g_ctrl_in_answered, on the answered
                               * arm with text. 0 for an EMPTY response since
                               * the 2026-08-11 repair — a turn that produced
                               * no text is not an answer (see the repair note
                               * at the top of this header) */
    uint8_t  count_blocked;   /* 1 = bump g_ctrl_in_blocked (refused only) */
    uint8_t  ack_pb;          /* 1 = km2b_miss_on_pb_ack is PERMITTED. Never for
                               * a locally-served answer — a fake ACK would mask
                               * a real PB wedge from the K/M2c detector (§3
                               * honesty guard 1) */
    uint8_t  miss_pb;         /* 1 = km2b_miss_on_pb_timeout is PERMITTED. Only
                               * a pure timeout — a fault was already funneled
                               * by pa_fault_check */
    uint8_t  close_window;    /* 1 = closing the inference duty window is
                               * PERMITTED. Never for a locally-served answer —
                               * it opened none, and closing here would fold an
                               * open WORKLOAD window (§3 honesty guard 2). The
                               * call site keeps its own g_infer_active check:
                               * this is the permission, not the state. */
    const char *jact_trigger; /* The JACT trigger_snapshot text for THIS exit.
                               * Always non-NULL and always a STATIC STRING
                               * LITERAL owned by ctrl_exit.c — see the trigger
                               * note below. */
    uint16_t jact_trigger_len;/* Its length, carried. NEVER strlen() it at the
                               * call site: the house rule that ACT_TRIGGER_MAX
                               * exists to enforce. Set from sizeof(lit)-1 at
                               * compile time. */
} ctrl_exit_decision_t;

/* THE JACT TRIGGER IS AN OUTPUT, NEVER AN INPUT — and this is stated because
 * the neighbouring behaviors.c deliberately forbids a char* PARAMETER for the
 * opposite reason, so a reader who knows that rule will otherwise wonder why
 * this one is allowed.
 *
 * behaviors.c's rule stops free text being SMUGGLED IN: behavior_build_digest
 * takes no char*, so a caller cannot inject attacker-controlled bytes into an
 * audit record. Here the direction is reversed. ctrl_exit_decide takes no
 * string at all; it SELECTS one of a fixed set of literals compiled into this
 * file, keyed only on the decision it has already made. No inbound byte can
 * reach the trigger, by construction rather than by discipline — the K-b
 * select-never-synthesize boundary, applied to a label instead of an action.
 *
 * THE LITERALS ARE KEYWORD-CLEAN against the canonical blocklist and the tests
 * pin that both ways. NOTE the honest scope, measured 2026-08-12 rather than
 * assumed: ctrl_in_jact does NOT route the trigger through shield_assess — it
 * fills and appends the record directly, deliberately bypassing spine_record
 * so a query-SHIELD decision never conflates into the v7 SHIELD-ACTION
 * counters. So keyword-cleanliness is NOT load-bearing at today's call site;
 * it is defence-in-depth for any future path that does re-audit through the
 * spine, and it is exactly the invariant ctrl_in_jact's own comment already
 * claims ("a re-audit can never self-block"). Cheap to keep, so kept. */

/*
 * ctrl_exit_decide — map a terminal state to the decision set.
 *
 * Inputs mirror pa_ctrl_gate's own flags, and PRECEDENCE mirrors its control
 * flow exactly:
 *   1. refused                          (QS_REFUSE — exits before routing)
 *   2. !served_locally && !dispatch_ok  (the §5-F degraded gate — exits before
 *                                        the duty fold / dispatch)
 *   3. got                              (the answered arm; served_locally
 *                                        forces got=1 upstream)
 *   4. otherwise                        (timeout, or fault when `faulted`)
 *
 * resp_len    — bytes accumulated in the reply text (roff). Drives the
 *               OK/ERROR + tag decisions; the shipped `served_locally &&
 *               resp_len > 0` guard on the LOCAL tag is reproduced verbatim.
 *               resp_len == 0 in the answered arm is the REPAIRED edge: it
 *               decides verdict 3 and does not count, ahead of any
 *               truncation test (see the repair note at the top).
 * stop_reason — the PB_STOP_* latch value AT THE EXIT (the caller clears it to
 *               PB_STOP_UNKNOWN before dispatch, so it belongs to THIS
 *               inference; UNKNOWN therefore reads "not truncated", never a
 *               fabricated claim). Only CAP / KV_FULL are truncation, and only
 *               for a PB-generated answer: a locally-served answer ran no
 *               generation, so reading the latch for it would report some
 *               earlier inference's stop reason.
 *
 * Unreachable-but-defined combinations (the function is total):
 *   served_locally && !got   — cannot occur (served_locally sets got=1);
 *                              falls to arm 4 and decides fail-safe (verdict 3).
 *   got && faulted           — mutually exclusive by the poll loop; got wins,
 *                              matching the loop's break order.
 *
 * Pure: no globals, no I/O, deterministic. Output is fully overwritten.
 */
void ctrl_exit_decide(int refused, int served_locally, int dispatch_ok,
                      int got, int faulted, int resp_len, uint8_t stop_reason,
                      ctrl_exit_decision_t *out);

#endif /* JARVIS_CTRL_EXIT_H */
