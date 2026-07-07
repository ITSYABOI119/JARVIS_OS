/*
 * JARVIS AI-OS — Phase 6 K/M4 pre-flip: fault-EP validity predicate (defense-in-depth).
 *
 * STEP-3 fixed the pa_poll_fault "phantom" (an empty-EP seL4_NBRecv leaves STALE register
 * garbage, NOT a zeroed MessageInfo) by gating on a valid fault-tag label (1..15; the kernel
 * DebugAsserts every seL4_Fault type fits in 4 bits: NullFault=0 .. VMFault=5). That single
 * gate rests on "empty-NBRecv garbage never lands in 1..15". Before the deploy-live
 * JARVIS_ACTIONS flip (K/M4) — when a phantom would restart a LIVE deployed PB — this adds a
 * BADGE cross-check: a phantom must now clear label in 1..15 AND badge in 0..n_workers to fire
 * a restart. badge is a SEPARATE stale-register class the label gate is blind to, so requiring
 * both is multiplicatively less likely to coincide (§10 carry-forward #2).
 *
 * Host-pure: NEVER include <sel4/sel4.h>; uint32/uint64 params only (mirrors km2b_miss.h /
 * km2b_trigger.h). Design: phase6/docs/PHASE_6_GOAL_K_SYSTEM_DESIGN.md §10.
 */

#ifndef KM2B_FAULT_H
#define KM2B_FAULT_H

#include <stdint.h>

/* VERDICT (the restart gate): 1 iff an NBRecv fault result is a GENUINE fault, not an
 * empty-EP phantom.  genuine == (label in 1..15) && (badge <= n_workers).
 *   - label 1..15: a real seL4_Fault tag (NullFault=0 => empty; > 15 => stale garbage).
 *   - badge 0..n_workers: 0 = PB-main (sel4utils' unbadged fault-EP copy), 1..n_workers = the
 *     per-worker badged fault caps. n_workers is the ACTUAL started worker count (0 on a serial
 *     degrade => only PB-main, badge 0, is valid).
 * ip/text_lo/text_hi are accepted for symmetry + future tightening but are NOT part of the
 * verdict — a genuine jump-to-garbage crash has ip outside .text yet MUST still restart. */
int km2b_fault_is_genuine(uint32_t label, uint64_t badge, uint64_t ip,
                          uint32_t n_workers, uint64_t text_lo, uint64_t text_hi);

/* ADVISORY only (log, NEVER a veto): 1 = the fault IP looks plausible. Returns 0 ONLY for a
 * VMFault (label==5, the sole fault whose MR0 is the faulting IP) whose ip falls outside PB's
 * known executable window [text_lo, text_hi). If the window is unknown (text_hi <= text_lo) it
 * degrades to always-plausible (1). Callers log a warning on 0 but MUST still restart — a real
 * jump-to-garbage crash legitimately faults outside .text. */
int km2b_fault_ip_plausible(uint32_t label, uint64_t ip,
                            uint64_t text_lo, uint64_t text_hi);

#endif /* KM2B_FAULT_H */
