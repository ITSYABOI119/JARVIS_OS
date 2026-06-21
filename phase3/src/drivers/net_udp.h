/**
 * net_udp.h - Minimal dependency-free Ethernet/IPv4/UDP broadcast frame builder
 *
 * Wraps a valid IPv4/UDP broadcast frame around a raw L2 TX path (e.g. the I211
 * i211_send_phys). Pure logic, no dynamic allocation, host-testable — so the
 * framing + IP checksum can be unit-tested off the box. Goal #2b N-b
 * (telemetry-OUT step 2/3). The box has NO IP stack / DHCP on bare metal, so the
 * source IP is hardcoded and the destination is the LIMITED broadcast
 * (255.255.255.255 / FF:FF:FF:FF:FF:FF) so NO ARP is required.
 *
 * JARVIS AI-OS - Phase 4 (goal #2b Remote Telemetry Console)
 */

#ifndef JARVIS_NET_UDP_H
#define JARVIS_NET_UDP_H

#include <stdint.h>

/* ---- Deployment config (box static identity; no DHCP on bare metal) ---- */
#define JARVIS_BOX_IP         ((192u<<24)|(168u<<16)|(100u<<8)|143u)  /* 192.168.100.143 — box static src IP */
#define JARVIS_TELEMETRY_PORT 51000   /* UDP dst port the remote console will listen on */

/**
 * net_ip_checksum - RFC1071 ones-complement checksum over `len` bytes.
 * Used for the IPv4 header. Computed over the header with its checksum field
 * zeroed; re-running it over the completed header yields 0 (the verify property).
 */
uint16_t net_ip_checksum(const void *data, uint32_t len);

/**
 * net_build_udp_broadcast - Build Eth(bcast)+IPv4+UDP+payload into `out`.
 * @out:         output frame buffer
 * @out_cap:     capacity of `out` in bytes
 * @src_mac:     6-byte source MAC (the NIC's MAC)
 * @src_ip:      source IPv4 (host byte order; e.g. JARVIS_BOX_IP)
 * @src_port:    UDP source port (host byte order)
 * @dst_port:    UDP destination port (host byte order)
 * @payload:     payload bytes (may be NULL iff payload_len==0)
 * @payload_len: payload length in bytes
 *
 * All header multi-byte fields are written in NETWORK byte order (big-endian).
 * The IPv4 header checksum IS computed; the UDP checksum is 0 (legal/optional for
 * IPv4). Frames shorter than 60 bytes are zero-padded to 60 (min Ethernet; the
 * NIC's IFCS appends the 4-byte FCS). Destination = limited broadcast.
 *
 * Returns the total frame length (>= 60) on success, or -1 on bad args / too-large
 * payload / insufficient out_cap. No dynamic allocation.
 */
int net_build_udp_broadcast(uint8_t *out, uint32_t out_cap,
                            const uint8_t src_mac[6], uint32_t src_ip,
                            uint16_t src_port, uint16_t dst_port,
                            const void *payload, uint16_t payload_len);

#endif /* JARVIS_NET_UDP_H */
