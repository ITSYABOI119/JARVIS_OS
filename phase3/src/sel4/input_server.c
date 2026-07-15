/*
 * input_server.c — jarvis-input: the least-privileged control-IN parser (SEC-014).
 *
 * Phase 6 goal 6-5 / M2b-1. Model 2 (goal-doc §5): a THIRD seL4 process, spawned by
 * Process A, that runs ONLY the untrusted inbound parse + rate-limit. It holds ZERO
 * NIC caps, NO HMAC key, NO request/response ring, NO model caps, NO privileged EP —
 * its whole CSpace is its own TCB/CNode/VSpace + IPC buffer + two mailbox frames +
 * two notifications (the cap-subtraction that IS SEC-014).
 *
 * PA copies each raw captured frame into the PA->input RAW mailbox and signals
 * `wake`; this process strips the Ethernet/IPv4/UDP envelope (control_parse_frame),
 * validates the JCTL message shape (control_parse_msg), rate-limits
 * (control_ratelimit), and returns the isolated JCTL bytes + a status hint in the
 * input->PA CAND mailbox, then signals `ack`. PA RE-PARSES the candidate and does
 * the HMAC + replay ITSELF (verify-in-PA) — it trusts nothing that crosses the
 * candidate mailbox.
 *
 * Containment: a code-exec compromise here can only (a) write garbage that PA
 * HMAC-drops, or (b) stop forwarding (self-DoS). It cannot forge an authenticated
 * query (no key), touch the NIC (no caps), or reach inference (no ring). Under
 * KernelIOMMU=OFF, holding zero device-MMIO caps is what makes that true.
 *
 *   argv[0] = wake  notification cap (PA signals when a raw frame is staged)
 *   argv[1] = ack   notification cap (this process signals when a candidate is ready)
 *
 * A full sel4utils process: sel4runtime's _start self-installs the IPC buffer, so
 * there is NO manual seL4_SetIPCBuffer (unlike the M3 bare worker TCBs). This file
 * exists only in the CONTROL_IN=1 build (the build script creates the jarvis-input
 * app under the gate), so it carries no #if guards of its own.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include <sel4/sel4.h>
#include <sel4runtime.h>

#include "control_mailbox.h"
#include "control_parser.h"
#include "control_ratelimit.h"

int main(int argc, char **argv)
{
    (void)argc;
    seL4_CPtr wake = (seL4_CPtr)atol(argv[0]);   /* PA -> input: a raw frame is staged */
    seL4_CPtr ack  = (seL4_CPtr)atol(argv[1]);   /* input -> PA: a candidate is ready   */

    ctrl_raw_mbx_t  *raw  = (ctrl_raw_mbx_t  *)CTRL_MBX_RAW_VADDR_IN;
    ctrl_cand_mbx_t *cand = (ctrl_cand_mbx_t *)CTRL_MBX_CAND_VADDR_IN;

    control_ratelimit_t rl;
    control_ratelimit_init(&rl, 0);

    uint32_t last = 0;   /* last consumed raw seq                                  */
    uint32_t out  = 0;   /* candidate seq we publish                               */
    uint32_t tick = 0;   /* M2b-1: monotonic tick feeding the token bucket (M2b-2  */
                         /*        replaces this with a real ms clock)             */

    for (;;) {
        seL4_Wait(wake, NULL);   /* blocks (deschedules) until PA stages a frame — cooperative */

        /* Drain every staged frame (a coalesced/spurious wake may batch a few). */
        for (;;) {
            uint32_t s = __atomic_load_n(&raw->seq, __ATOMIC_ACQUIRE);
            if (s == last)
                break;
            last = s;

            uint16_t len = raw->len;
            if (len > CTRL_MBX_MAX_FRAME)
                len = CTRL_MBX_MAX_FRAME;

            /* Snapshot into a private buffer: the copy-free parser aliases its input,
             * and PA (with backpressure) should not mutate the frame from under it. */
            static uint8_t local[CTRL_MBX_MAX_FRAME];
            memcpy(local, raw->bytes, len);

            const uint8_t *pl = NULL;
            size_t pll = 0;
            control_msg_t msg;
            uint16_t status = CTRL_CAND_DROP_PARSE;
            uint16_t n = 0;

            if (control_parse_frame(local, len, &pl, &pll) == CTRL_OK &&
                control_parse_msg(pl, pll, &msg) == CTRL_OK) {
                /* Rate-limit the WELL-FORMED path — those are the frames that would
                 * cost PA an HMAC pair. tick++ advances only when we get this far. */
                if (control_ratelimit_allow(&rl, tick++)) {
                    n = (uint16_t)(pll > CONTROL_MSG_MAX ? CONTROL_MSG_MAX : pll);
                    memcpy(cand->jctl, pl, n);   /* forward the isolated JCTL bytes; PA re-parses */
                    status = CTRL_CAND_OK;
                } else {
                    status = CTRL_CAND_DROP_RATELIMIT;
                }
            }

            cand->status   = status;
            cand->jctl_len = n;
            __atomic_store_n(&cand->seq, ++out, __ATOMIC_RELEASE);   /* publish AFTER the payload */
            seL4_Signal(ack);
        }
    }
    return 0;
}
