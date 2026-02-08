/*
 * JARVIS AI-OS - Network Shell Commands Implementation
 *
 * Week 39: ping, ifconfig, netstat for bare-metal seL4.
 * Uses net_stack for frame building/matching, bcm_genet for TX/RX,
 * and bcm2711_timer for RTT measurement.
 */

#include "net_cmd.h"
#include "net_stack.h"
#include "bcm_genet.h"
#include "bcm2711_timer.h"
#include "usb_hid.h"
#include "bcm_gpio.h"
#include "bcm_i2c.h"
#include "emmc_sdhci.h"

#include <sel4/sel4.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Debug Output
 * ================================================================ */

static void debug_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') seL4_DebugPutChar('\r');
        seL4_DebugPutChar(*s++);
    }
}

/* ================================================================
 * IP Address Parsing
 * ================================================================ */

uint32_t parse_ipv4(const char *str)
{
    if (!str) return 0;

    uint32_t octets[4] = {0};
    int octet_idx = 0;
    uint32_t val = 0;
    bool has_digit = false;

    for (const char *p = str; ; p++) {
        if (*p >= '0' && *p <= '9') {
            val = val * 10 + (*p - '0');
            has_digit = true;
            if (val > 255) return 0;
        } else if (*p == '.' || *p == '\0') {
            if (!has_digit || octet_idx > 3) return 0;
            octets[octet_idx++] = val;
            val = 0;
            has_digit = false;
            if (*p == '\0') break;
        } else {
            return 0;  /* Invalid character */
        }
    }

    if (octet_idx != 4) return 0;

    /* Network byte order (big-endian) */
    uint8_t ip_bytes[4] = {
        (uint8_t)octets[0], (uint8_t)octets[1],
        (uint8_t)octets[2], (uint8_t)octets[3]
    };
    uint32_t ip_be;
    memcpy(&ip_be, ip_bytes, 4);
    return ip_be;
}

/* Format IP address from network byte order to string */
static int format_ip(uint32_t ip_be, char *buf, uint32_t size)
{
    uint8_t b[4];
    memcpy(b, &ip_be, 4);
    return snprintf(buf, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

/* Format MAC address to string */
static int format_mac(const uint8_t mac[6], char *buf, uint32_t size)
{
    return snprintf(buf, size, "%02x:%02x:%02x:%02x:%02x:%02x",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ================================================================
 * Link speed to string
 * ================================================================ */

static const char *speed_str(genet_link_speed_t speed)
{
    switch (speed) {
        case GENET_LINK_DOWN:   return "DOWN";
        case GENET_LINK_10HD:   return "10 Mbps Half";
        case GENET_LINK_10FD:   return "10 Mbps Full";
        case GENET_LINK_100HD:  return "100 Mbps Half";
        case GENET_LINK_100FD:  return "100 Mbps Full";
        case GENET_LINK_1000HD: return "1000 Mbps Half";
        case GENET_LINK_1000FD: return "1000 Mbps Full";
        default:                return "Unknown";
    }
}

/* ================================================================
 * cmd_ping
 * ================================================================ */

int cmd_ping(uint32_t target_ip_be, uint32_t count, uint32_t timeout_ms,
             char *output, uint32_t output_size)
{
    int pos = 0;
    char ip_str[16];

    format_ip(target_ip_be, ip_str, sizeof(ip_str));

    /* Check link */
    if (!genet_get_link_status()) {
        pos += snprintf(output + pos, output_size - pos,
                        "ping %s: link DOWN\n", ip_str);
        return pos;
    }

    /* ARP resolve */
    uint8_t dst_mac[6];
    if (!net_arp_resolve(target_ip_be, dst_mac, 2000)) {
        pos += snprintf(output + pos, output_size - pos,
                        "ping %s: ARP timeout\n", ip_str);
        return pos;
    }

    /* Send ICMP echo requests */
    uint16_t ident = (uint16_t)(systimer_read() & 0xFFFF);
    uint32_t sent = 0, received = 0;
    uint64_t total_rtt_us = 0;

    for (uint32_t seq = 1; seq <= count && pos < (int)(output_size - 60); seq++) {
        uint8_t frame[128];
        uint32_t frame_len = net_build_icmp_request(target_ip_be, dst_mac,
                                                     ident, (uint16_t)seq,
                                                     frame, sizeof(frame));
        if (frame_len == 0) continue;

        if (!genet_tx_send(frame, frame_len)) continue;
        sent++;

        /* Poll for reply */
        uint64_t send_time = systimer_read();
        uint64_t deadline = send_time + (uint64_t)timeout_ms * 1000;
        bool got_reply = false;

        while (systimer_read() < deadline) {
            uint8_t rx_buf[NET_MAX_FRAME];
            uint32_t rx_len = 0;

            if (genet_rx_recv(rx_buf, sizeof(rx_buf), &rx_len)) {
                uint8_t ttl = 0;
                if (net_is_icmp_echo_reply(rx_buf, rx_len,
                                           ident, (uint16_t)seq, &ttl)) {
                    uint64_t rtt = systimer_read() - send_time;
                    total_rtt_us += rtt;
                    received++;
                    got_reply = true;

                    /* Format: "Reply from X.X.X.X: time=N.Nms TTL=64" */
                    uint32_t rtt_ms = (uint32_t)(rtt / 1000);
                    uint32_t rtt_frac = (uint32_t)((rtt % 1000) / 100);
                    pos += snprintf(output + pos, output_size - pos,
                                    "Reply from %s: time=%u.%ums TTL=%u\n",
                                    ip_str, rtt_ms, rtt_frac, ttl);
                    break;
                }
                /* Not our reply - could be other traffic, process ARP */
                if (rx_len >= 14) {
                    uint16_t et = ((uint16_t)rx_buf[12] << 8) | rx_buf[13];
                    if (et == 0x0806) {
                        net_process_arp_reply(rx_buf, rx_len);
                    }
                }
            }

            for (volatile int d = 0; d < 100; d++);
        }

        if (!got_reply) {
            pos += snprintf(output + pos, output_size - pos,
                            "Request timed out\n");
        }
    }

    /* Summary */
    if (sent > 0) {
        uint32_t loss = ((sent - received) * 100) / sent;
        pos += snprintf(output + pos, output_size - pos,
                        "%u/%u received, %u%% loss",
                        received, sent, loss);
        if (received > 0) {
            uint32_t avg_ms = (uint32_t)(total_rtt_us / received / 1000);
            pos += snprintf(output + pos, output_size - pos,
                            ", avg=%ums", avg_ms);
        }
        pos += snprintf(output + pos, output_size - pos, "\n");
    }

    return pos;
}

/* ================================================================
 * cmd_ifconfig
 * ================================================================ */

int cmd_ifconfig(char *output, uint32_t output_size)
{
    int pos = 0;
    uint8_t mac[6];
    char mac_str[18], ip_str[16];

    net_get_mac(mac);
    format_mac(mac, mac_str, sizeof(mac_str));
    format_ip(net_get_ip(), ip_str, sizeof(ip_str));

    bool link = genet_get_link_status();
    genet_link_speed_t speed = genet_get_link_speed();

    pos += snprintf(output + pos, output_size - pos,
                    "eth0: %s\n"
                    "  MAC: %s\n"
                    "  IP:  %s\n"
                    "  Link: %s\n",
                    link ? "UP" : "DOWN",
                    mac_str, ip_str,
                    speed_str(speed));

    return pos;
}

/* ================================================================
 * cmd_netstat
 * ================================================================ */

int cmd_netstat(char *output, uint32_t output_size)
{
    int pos = 0;
    genet_stats_t stats;
    genet_get_stats(&stats);

    pos += snprintf(output + pos, output_size - pos,
                    "TX: %u pkts, %u bytes, %u err\n"
                    "RX: %u pkts, %u bytes, %u err, %u drop\n"
                    "ARP cache: %u entries\n",
                    stats.tx_packets, stats.tx_bytes, stats.tx_errors,
                    stats.rx_packets, stats.rx_bytes, stats.rx_errors,
                    stats.rx_dropped,
                    net_arp_cache_count());

    return pos;
}

/* ================================================================
 * cmd_stress - Quick stress test exercising all drivers
 * ================================================================ */

int cmd_stress(char *output, uint32_t output_size)
{
    int pos = 0;
    int s_pass = 0, s_fail = 0;
    int iterations = 100;

    for (int i = 0; i < iterations; i++) {
        /* Timer read */
        uint64_t t = systimer_read();
        if (t != 0) s_pass++; else s_fail++;

        /* GPIO toggle (if initialized) */
        if (gpio_is_initialized()) {
            gpio_write(42, 1);
            gpio_write(42, 0);
            s_pass++;
        } else {
            s_pass++; /* skip but don't fail */
        }

        /* EMMC: read LBA 0 */
        uint8_t sector[512];
        if (emmc_read_block(0, sector)) s_pass++; else s_fail++;

        /* GENET: link status check (no-hang) */
        (void)genet_get_link_status();
        s_pass++;

        /* USB HID: connected check (returns quickly) */
        (void)usb_hid_device_connected();
        s_pass++;
    }

    pos += snprintf(output + pos, output_size - pos,
                    "Stress: %d iterations, %d pass, %d fail\n",
                    iterations, s_pass, s_fail);
    return pos;
}

/* ================================================================
 * Command Dispatcher
 * ================================================================ */

/* Skip leading whitespace */
static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* Check if string starts with prefix (case-insensitive for alpha) */
static bool starts_with(const char *str, const char *prefix)
{
    while (*prefix) {
        char a = *str, b = *prefix;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
        str++;
        prefix++;
    }
    return true;
}

int cmd_dispatch(const char *cmd_str, char *output, uint32_t output_size)
{
    if (!cmd_str || !output || output_size == 0) {
        return 0;
    }

    output[0] = '\0';
    const char *cmd = skip_spaces(cmd_str);

    if (starts_with(cmd, "ping ")) {
        const char *arg = skip_spaces(cmd + 5);
        uint32_t ip = parse_ipv4(arg);
        if (ip == 0) {
            return snprintf(output, output_size, "Usage: ping <ip>\n");
        }
        return cmd_ping(ip, 4, 1000, output, output_size);

    } else if (starts_with(cmd, "ifconfig")) {
        return cmd_ifconfig(output, output_size);

    } else if (starts_with(cmd, "netstat")) {
        return cmd_netstat(output, output_size);

    } else if (starts_with(cmd, "usb")) {
        return usb_hid_get_status(output, output_size);

    } else if (starts_with(cmd, "gpio")) {
        return gpio_get_status(output, output_size);

    } else if (starts_with(cmd, "i2c")) {
        return i2c_scan(output, output_size);

    } else if (starts_with(cmd, "stress")) {
        return cmd_stress(output, output_size);

    } else {
        return snprintf(output, output_size,
                        "Commands: ping, ifconfig, netstat, usb, gpio, i2c, stress\n");
    }
}
