/*
 * control_parser.h — JARVIS control-IN hardened inbound parser (Phase 6 goal 6-5).
 *
 * Two-stage, COPY-FREE, NEVER-ALLOCATING parser for the untrusted inbound
 * control datagram (the FIRST inbound the box accepts):
 *
 *   1. control_parse_frame() — validates the Ethernet/IPv4/UDP envelope of a
 *      raw captured frame and, on success, points at the UDP payload. It does
 *      the cheapest checks first and NEVER reads past `len`. Fragments are
 *      rejected BEFORE any downstream (UDP/HMAC) work is attempted.
 *
 *   2. control_parse_msg() — validates the JCTL application message (magic /
 *      version / flags / length arithmetic) and fills a control_msg_t whose
 *      pointer fields ALIAS the payload buffer (no copies, no reassembly).
 *
 * Host-pure — no seL4 dep, no allocation, no libc beyond <stdint.h>/<stddef.h>.
 * Follows the precedent of km2b_miss.c / wake.c / monitors.c.
 *
 * The IPv4 header checksum used here is an inlined RFC1071 ones-complement sum;
 * it is bit-for-bit equivalent to phase3/src/drivers/net_udp.c's
 * net_ip_checksum() (deliberately NOT linked against it — this module keeps
 * zero link dependencies).
 */

#ifndef JARVIS_CONTROL_PARSER_H
#define JARVIS_CONTROL_PARSER_H

#include <stdint.h>
#include <stddef.h>

#include "control_msg.h"

typedef enum {
    CTRL_OK = 0,
    CTRL_TOO_SHORT,
    CTRL_BAD_ETHERTYPE,
    CTRL_NOT_IPV4,
    CTRL_IP_IHL_BAD,
    CTRL_IP_CKSUM_BAD,
    CTRL_IP_FRAGMENT,
    CTRL_IP_LEN_BAD,
    CTRL_NOT_UDP,
    CTRL_BAD_PORT,
    CTRL_UDP_LEN_BAD,
    CTRL_BAD_MAGIC,
    CTRL_BAD_VERSION,
    CTRL_BAD_FLAGS,
    CTRL_QUERY_TOO_LONG,
    CTRL_LEN_MISMATCH
} control_status_t;

/*
 * Stage 1 — validate the L2/L3/L4 envelope of a raw frame.
 *
 * On CTRL_OK: *udp_payload points at the first UDP payload byte (inside `data`)
 * and *udp_len is its length in bytes. On any error the outputs are left
 * unchanged. Never reads past data[len-1].
 */
control_status_t control_parse_frame(const uint8_t *data, size_t len,
                                     const uint8_t **udp_payload,
                                     size_t *udp_len);

/*
 * Stage 2 — validate + view the JCTL message. `out` fields alias `payload`.
 */
control_status_t control_parse_msg(const uint8_t *payload, size_t len,
                                   control_msg_t *out);

#endif /* JARVIS_CONTROL_PARSER_H */
