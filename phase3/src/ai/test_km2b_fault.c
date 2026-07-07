/*
 * Phase 6 K/M4 pre-flip — host test for the fault-EP validity predicate (§10 carry-forward #2).
 *
 * Pins the defense-in-depth pa_poll_fault depends on: a restart fires ONLY when label in 1..15
 * AND badge in 0..n_workers (the badge layer catches a label-passing phantom the STEP-3 gate
 * would have let through), and the VMFault-ip advisory flags an out-of-.text IP without ever
 * vetoing a genuine jump-to-garbage crash.
 *
 * Build: gcc -O2 -I. test_km2b_fault.c km2b_fault.c -o test_km2b_fault && ./test_km2b_fault
 */

#include "km2b_fault.h"
#include <stdio.h>

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  PASS  %s\n", name); } \
    else      { g_fail++; printf("  FAIL  %s\n", name); } \
} while (0)

/* A plausible PB .text window (illustrative; the real one is derived at spawn). */
#define TLO 0x401000ULL
#define THI 0x480000ULL

int main(void)
{
    printf("=== test_km2b_fault (Phase 6 K/M4 pre-flip §10) ===\n");

    /* --- label boundary (valid badge=0, n_workers=5, window unknown) --- */
    CHECK(!km2b_fault_is_genuine(0,      0, 0, 5, 0, 0), "L1 label 0 (NullFault) REJECT");
    CHECK( km2b_fault_is_genuine(1,      0, 0, 5, 0, 0), "L2 label 1 ACCEPT");
    CHECK( km2b_fault_is_genuine(5,      0, 0, 5, 0, 0), "L3 label 5 (VMFault) ACCEPT");
    CHECK( km2b_fault_is_genuine(15,     0, 0, 5, 0, 0), "L4 label 15 ACCEPT");
    CHECK(!km2b_fault_is_genuine(16,     0, 0, 5, 0, 0), "L5 label 16 REJECT");
    CHECK(!km2b_fault_is_genuine(165553, 0, 0, 5, 0, 0), "L6 label 165553 (documented phantom) REJECT");

    /* --- badge boundary, n_workers = 5 (label=5 valid) --- */
    CHECK( km2b_fault_is_genuine(5, 0,      0, 5, 0, 0), "B1 badge 0 (PB-main) ACCEPT");
    CHECK( km2b_fault_is_genuine(5, 5,      0, 5, 0, 0), "B2 badge 5 (last worker) ACCEPT");
    CHECK(!km2b_fault_is_genuine(5, 6,      0, 5, 0, 0), "B3 badge 6 (> n_workers) REJECT");
    CHECK(!km2b_fault_is_genuine(5, 165553, 0, 5, 0, 0), "B4 badge 165553 (stale) REJECT");

    /* --- serial degrade (n_workers = 0): only PB-main badge 0 is valid --- */
    CHECK( km2b_fault_is_genuine(5, 0, 0, 0, 0, 0), "S1 n_workers 0: badge 0 ACCEPT");
    CHECK(!km2b_fault_is_genuine(5, 1, 0, 0, 0, 0), "S2 n_workers 0: badge 1 REJECT");

    /* --- the two layers earn their keep --- */
    /* A phantom that PASSES the label gate but has a stale badge: the STEP-3 label-only gate
     * would have accepted this; the new badge layer rejects it. */
    CHECK(!km2b_fault_is_genuine(5, 165553, 0x286b0160ULL, 5, TLO, THI),
          "X1 label-passing phantom rejected via badge (new layer earns its keep)");
    /* A genuine worker VMFault with an in-.text ip: accepted. */
    CHECK( km2b_fault_is_genuine(5, 1, 0x401234ULL, 5, TLO, THI),
          "X2 genuine {label=5, badge=1, ip in .text} ACCEPT");
    /* THE crux invariant: a genuine jump-to-garbage VMFault (ip OUTSIDE .text) with a valid badge
     * MUST be ACCEPTED — ip is NOT part of the verdict. This pins the advisory/verdict separation:
     * if a future change folded km2b_fault_ip_plausible into the verdict, self-heal would silently
     * brick for the most common real crash, yet every OTHER assert would stay green. */
    CHECK( km2b_fault_is_genuine(5, 0, 0x286b0160ULL, 5, TLO, THI),
          "X3 genuine VMFault ip OUTSIDE .text, valid badge -> ACCEPT (ip is NOT a verdict veto)");

    /* --- ip_plausible advisory --- */
    CHECK(km2b_fault_ip_plausible(5, 0x286b0160ULL, TLO, THI) == 0,
          "P1 VMFault ip outside .text -> 0 (advisory warn)");
    CHECK(km2b_fault_ip_plausible(5, 0x401234ULL,   TLO, THI) == 1,
          "P2 VMFault ip inside .text -> 1");
    CHECK(km2b_fault_ip_plausible(1, 0xDEADBEEFULL, TLO, THI) == 1,
          "P3 non-VMFault ip always plausible (no IP MR)");
    CHECK(km2b_fault_ip_plausible(5, 0x999999ULL, 0, 0) == 1,
          "P4 unknown window (hi<=lo) -> degrade to plausible");
    CHECK(km2b_fault_ip_plausible(5, TLO, TLO, THI) == 1,
          "P5 ip == text_lo -> inside (half-open [lo,hi))");
    CHECK(km2b_fault_ip_plausible(5, THI, TLO, THI) == 0,
          "P6 ip == text_hi -> outside (half-open [lo,hi))");

    printf("=== %d PASS, %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
