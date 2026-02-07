/*
 * JARVIS AI-OS - Network Shell Commands
 *
 * Week 39: ping, ifconfig, netstat for bare-metal seL4.
 * Commands callable from UART IPC or test code.
 */

#ifndef NET_CMD_H
#define NET_CMD_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum command output buffer size */
#define CMD_OUTPUT_MAX  240

/* Dispatch a command string to the appropriate handler.
 * Supported: "ping <ip>", "ifconfig", "netstat"
 * Output is written to output buffer (null-terminated).
 * Returns number of bytes written (excluding NUL). */
int cmd_dispatch(const char *cmd_str, char *output, uint32_t output_size);

/* ping: send ICMP echo requests, measure RTT */
int cmd_ping(uint32_t target_ip_be, uint32_t count, uint32_t timeout_ms,
             char *output, uint32_t output_size);

/* ifconfig: display MAC, IP, link status/speed */
int cmd_ifconfig(char *output, uint32_t output_size);

/* netstat: display TX/RX statistics */
int cmd_netstat(char *output, uint32_t output_size);

/* Parse "a.b.c.d" to network-byte-order uint32.
 * Returns 0 on parse error. */
uint32_t parse_ipv4(const char *str);

#endif /* NET_CMD_H */
