/*
 * JARVIS AI-OS — Phase 6 K/M4 pre-flip: fault-EP validity predicate (defense-in-depth).
 * Host-pure (no seL4 dependency). Contract + rationale in km2b_fault.h. Design: §10.
 */

#include "km2b_fault.h"

/* seL4 fault-tag labels: NullFault=0 (empty EP) .. VMFault=5; the kernel DebugAsserts every
 * seL4_Fault type fits in 4 bits, so a label in 1..15 is a real fault and > 15 is stale garbage. */
#define KM2B_FAULT_LABEL_MIN  1u
#define KM2B_FAULT_LABEL_MAX  15u
#define KM2B_FAULT_VMFAULT    5u

int km2b_fault_is_genuine(uint32_t label, uint64_t badge, uint64_t ip,
                          uint32_t n_workers, uint64_t text_lo, uint64_t text_hi)
{
    (void)ip; (void)text_lo; (void)text_hi;   /* symmetry / future tightening — not in the verdict */
    if (label < KM2B_FAULT_LABEL_MIN || label > KM2B_FAULT_LABEL_MAX)
        return 0;                              /* NullFault(0) or stale garbage => endpoint empty */
    if (badge > (uint64_t)n_workers)
        return 0;                              /* badge must name PB-main(0) or a started worker */
    return 1;
}

int km2b_fault_ip_plausible(uint32_t label, uint64_t ip,
                            uint64_t text_lo, uint64_t text_hi)
{
    if (label != KM2B_FAULT_VMFAULT) return 1;       /* only a VMFault's MR0 is the faulting IP */
    if (text_hi <= text_lo)          return 1;       /* window unknown => degrade to plausible */
    if (ip < text_lo || ip >= text_hi) return 0;     /* [text_lo, text_hi) half-open */
    return 1;
}
