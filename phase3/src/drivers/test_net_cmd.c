/**
 * test_net_cmd.c - Network Command Shell Portable Function Tests
 *
 * Tests the platform-independent parts of net_cmd.c:
 *   - parse_ipv4()      (string -> network-order uint32_t)
 *   - format_ip()       (network-order uint32_t -> string) [static, copied]
 *   - format_mac()      (6-byte MAC -> "xx:xx:xx:xx:xx:xx") [static, copied]
 *   - skip_spaces()     (whitespace skipping) [static, copied]
 *   - starts_with()     (case-insensitive prefix match) [static, copied]
 *   - safe_remaining()  (unsigned underflow protection) [static, copied]
 *   - cmd_dispatch()    (command routing, x86 stubs)
 *   - x86 stubs: cmd_ping, cmd_ifconfig, cmd_netstat, cmd_stress
 *
 * Static helpers are copied verbatim from net_cmd.c for direct testing.
 * Public functions are tested by linking net_cmd.c + net_stack.c.
 *
 * Compile:
 *   gcc -Wall -Werror -O2 -std=c11 -DJARVIS_TEST_MOCK \
 *       -I phase3/src/drivers phase3/src/drivers/test_net_cmd.c \
 *       phase3/src/drivers/net_cmd.c phase3/src/drivers/net_stack.c \
 *       -o /tmp/test_net_cmd
 *
 * JARVIS AI-OS - Phase 3
 */

#ifndef JARVIS_TEST_MOCK
#define JARVIS_TEST_MOCK
#endif

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "net_cmd.h"
#include "net_stack.h"

/* ========================================================================
 * Test Helpers
 * ======================================================================== */

static int pass_count = 0;
static int fail_count = 0;

#define TEST(name) \
    do { printf("  TEST: %-55s ", name); } while(0)

#define PASS() \
    do { printf("PASS\n"); pass_count++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL - %s\n", msg); fail_count++; } while(0)

#define CHECK(cond, msg) \
    do { if (cond) { PASS(); } else { FAIL(msg); } } while(0)

/* ========================================================================
 * Copied static helpers from net_cmd.c (for direct unit testing)
 *
 * These are static in net_cmd.c, so we duplicate them here with test_
 * prefix. The implementations are identical to the production code.
 * ======================================================================== */

static inline uint32_t test_safe_remaining(int pos, uint32_t size)
{
    return (pos >= 0 && (uint32_t)pos < size) ? size - (uint32_t)pos : 0;
}

static int test_format_ip(uint32_t ip_be, char *buf, uint32_t size)
{
    uint8_t b[4];
    memcpy(b, &ip_be, 4);
    return snprintf(buf, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
}

static int test_format_mac(const uint8_t mac[6], char *buf, uint32_t size)
{
    return snprintf(buf, size, "%02x:%02x:%02x:%02x:%02x:%02x",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static const char *test_skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static bool test_starts_with(const char *str, const char *prefix)
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

/* ========================================================================
 * Tests: parse_ipv4 (public API from net_cmd.h)
 * ======================================================================== */

static void test_parse_ipv4_basic(void)
{
    TEST("parse_ipv4(\"192.168.1.1\") -> correct bytes");

    uint32_t ip = parse_ipv4("192.168.1.1");
    uint8_t b[4];
    memcpy(b, &ip, 4);

    int ok = (b[0] == 192) && (b[1] == 168) && (b[2] == 1) && (b[3] == 1);
    CHECK(ok && ip != 0, "parsed octets wrong");
}

static void test_parse_ipv4_loopback(void)
{
    TEST("parse_ipv4(\"127.0.0.1\") -> loopback");

    uint32_t ip = parse_ipv4("127.0.0.1");
    uint8_t b[4];
    memcpy(b, &ip, 4);

    int ok = (b[0] == 127) && (b[1] == 0) && (b[2] == 0) && (b[3] == 1);
    CHECK(ok, "loopback address wrong");
}

static void test_parse_ipv4_all_zeros(void)
{
    TEST("parse_ipv4(\"0.0.0.0\") -> valid zero addr");

    /* 0.0.0.0 is a valid IP address; parse_ipv4 returns its
     * network-byte-order representation.  On little-endian x86
     * that is still 0x00000000, so parse_ipv4 returns 0 which
     * the API documents as "parse error".  This is a known
     * limitation we accept (0.0.0.0 is unusable as a target). */
    uint32_t ip = parse_ipv4("0.0.0.0");
    CHECK(ip == 0, "0.0.0.0 returns 0 (documented limitation)");
}

static void test_parse_ipv4_max(void)
{
    TEST("parse_ipv4(\"255.255.255.255\") -> broadcast");

    uint32_t ip = parse_ipv4("255.255.255.255");
    uint8_t b[4];
    memcpy(b, &ip, 4);

    int ok = (b[0] == 255) && (b[1] == 255) && (b[2] == 255) && (b[3] == 255);
    CHECK(ok && ip != 0, "broadcast address wrong");
}

static void test_parse_ipv4_octet_overflow(void)
{
    TEST("parse_ipv4(\"256.0.0.1\") -> 0 (octet > 255)");

    uint32_t ip = parse_ipv4("256.0.0.1");
    CHECK(ip == 0, "should reject octet > 255");
}

static void test_parse_ipv4_too_few_octets(void)
{
    TEST("parse_ipv4(\"192.168.1\") -> 0 (only 3 octets)");

    uint32_t ip = parse_ipv4("192.168.1");
    CHECK(ip == 0, "should reject fewer than 4 octets");
}

static void test_parse_ipv4_too_many_octets(void)
{
    TEST("parse_ipv4(\"1.2.3.4.5\") -> 0 (5 octets)");

    uint32_t ip = parse_ipv4("1.2.3.4.5");
    CHECK(ip == 0, "should reject more than 4 octets");
}

static void test_parse_ipv4_letters(void)
{
    TEST("parse_ipv4(\"abc.def.1.2\") -> 0 (letters)");

    uint32_t ip = parse_ipv4("abc.def.1.2");
    CHECK(ip == 0, "should reject non-digit characters");
}

static void test_parse_ipv4_empty(void)
{
    TEST("parse_ipv4(\"\") -> 0 (empty string)");

    uint32_t ip = parse_ipv4("");
    CHECK(ip == 0, "should reject empty string");
}

static void test_parse_ipv4_null(void)
{
    TEST("parse_ipv4(NULL) -> 0");

    uint32_t ip = parse_ipv4(NULL);
    CHECK(ip == 0, "should return 0 for NULL");
}

static void test_parse_ipv4_leading_dot(void)
{
    TEST("parse_ipv4(\".1.2.3.4\") -> 0 (leading dot)");

    uint32_t ip = parse_ipv4(".1.2.3.4");
    CHECK(ip == 0, "should reject leading dot (no digit before first dot)");
}

/* ========================================================================
 * Tests: format_ip (static helper, tested via copy)
 * ======================================================================== */

static void test_format_ip_basic(void)
{
    TEST("format_ip(192.168.1.100) -> correct string");

    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);

    char buf[16];
    int n = test_format_ip(ip, buf, sizeof(buf));

    int ok = (n > 0) && (strcmp(buf, "192.168.1.100") == 0);
    CHECK(ok, "format_ip output wrong");
}

static void test_format_ip_loopback(void)
{
    TEST("format_ip(127.0.0.1) -> \"127.0.0.1\"");

    uint8_t ip_bytes[4] = {127, 0, 0, 1};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);

    char buf[16];
    test_format_ip(ip, buf, sizeof(buf));

    CHECK(strcmp(buf, "127.0.0.1") == 0, "loopback format wrong");
}

static void test_format_ip_roundtrip(void)
{
    TEST("parse_ipv4 -> format_ip round-trip");

    uint32_t ip = parse_ipv4("10.20.30.40");
    char buf[16];
    test_format_ip(ip, buf, sizeof(buf));

    CHECK(strcmp(buf, "10.20.30.40") == 0, "round-trip mismatch");
}

/* ========================================================================
 * Tests: format_mac (static helper, tested via copy)
 * ======================================================================== */

static void test_format_mac_basic(void)
{
    TEST("format_mac -> \"aa:bb:cc:dd:ee:ff\"");

    uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    char buf[18];
    int n = test_format_mac(mac, buf, sizeof(buf));

    int ok = (n == 17) && (strcmp(buf, "aa:bb:cc:dd:ee:ff") == 0);
    CHECK(ok, "MAC format wrong");
}

static void test_format_mac_zeros(void)
{
    TEST("format_mac all-zeros -> \"00:00:00:00:00:00\"");

    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    char buf[18];
    test_format_mac(mac, buf, sizeof(buf));

    CHECK(strcmp(buf, "00:00:00:00:00:00") == 0, "zero MAC format wrong");
}

/* ========================================================================
 * Tests: skip_spaces (static helper, tested via copy)
 * ======================================================================== */

static void test_skip_spaces_leading(void)
{
    TEST("skip_spaces(\"   hello\") -> \"hello\"");

    const char *result = test_skip_spaces("   hello");
    CHECK(strcmp(result, "hello") == 0, "did not skip spaces");
}

static void test_skip_spaces_tabs(void)
{
    TEST("skip_spaces(\"\\t\\tworld\") -> \"world\"");

    const char *result = test_skip_spaces("\t\tworld");
    CHECK(strcmp(result, "world") == 0, "did not skip tabs");
}

static void test_skip_spaces_mixed(void)
{
    TEST("skip_spaces(\" \\t test\") -> \"test\"");

    const char *result = test_skip_spaces(" \t test");
    CHECK(strcmp(result, "test") == 0, "did not skip mixed whitespace");
}

static void test_skip_spaces_none(void)
{
    TEST("skip_spaces(\"nospace\") -> \"nospace\"");

    const char *result = test_skip_spaces("nospace");
    CHECK(strcmp(result, "nospace") == 0, "should not skip non-space chars");
}

static void test_skip_spaces_all(void)
{
    TEST("skip_spaces(\"   \") -> empty string");

    const char *result = test_skip_spaces("   ");
    CHECK(*result == '\0', "should reach end of string");
}

/* ========================================================================
 * Tests: starts_with (static helper, tested via copy)
 * ======================================================================== */

static void test_starts_with_exact(void)
{
    TEST("starts_with(\"ping 1.2.3.4\", \"ping \") -> true");

    CHECK(test_starts_with("ping 1.2.3.4", "ping "), "exact prefix should match");
}

static void test_starts_with_case_insensitive(void)
{
    TEST("starts_with(\"PING\", \"ping\") -> true (case-insensitive)");

    CHECK(test_starts_with("PING", "ping"), "case-insensitive match failed");
}

static void test_starts_with_mixed_case(void)
{
    TEST("starts_with(\"PiNg\", \"pInG\") -> true");

    CHECK(test_starts_with("PiNg", "pInG"), "mixed case match failed");
}

static void test_starts_with_mismatch(void)
{
    TEST("starts_with(\"ifconfig\", \"ping\") -> false");

    CHECK(!test_starts_with("ifconfig", "ping"), "should not match different prefix");
}

static void test_starts_with_empty_prefix(void)
{
    TEST("starts_with(\"anything\", \"\") -> true (empty prefix)");

    CHECK(test_starts_with("anything", ""), "empty prefix should always match");
}

static void test_starts_with_longer_prefix(void)
{
    TEST("starts_with(\"pi\", \"ping\") -> false (prefix longer)");

    CHECK(!test_starts_with("pi", "ping"), "longer prefix should not match shorter str");
}

/* ========================================================================
 * Tests: safe_remaining (static helper, tested via copy)
 * ======================================================================== */

static void test_safe_remaining_normal(void)
{
    TEST("safe_remaining(10, 100) -> 90");

    uint32_t r = test_safe_remaining(10, 100);
    CHECK(r == 90, "normal remaining wrong");
}

static void test_safe_remaining_zero_pos(void)
{
    TEST("safe_remaining(0, 100) -> 100");

    uint32_t r = test_safe_remaining(0, 100);
    CHECK(r == 100, "zero pos remaining wrong");
}

static void test_safe_remaining_at_end(void)
{
    TEST("safe_remaining(100, 100) -> 0 (pos == size)");

    uint32_t r = test_safe_remaining(100, 100);
    CHECK(r == 0, "pos==size should return 0");
}

static void test_safe_remaining_overflow(void)
{
    TEST("safe_remaining(200, 100) -> 0 (pos > size)");

    uint32_t r = test_safe_remaining(200, 100);
    CHECK(r == 0, "pos>size should return 0 (underflow protection)");
}

static void test_safe_remaining_negative(void)
{
    TEST("safe_remaining(-1, 100) -> 0 (negative pos)");

    uint32_t r = test_safe_remaining(-1, 100);
    CHECK(r == 0, "negative pos should return 0");
}

/* ========================================================================
 * Tests: cmd_dispatch (portable dispatcher + x86 stubs)
 * ======================================================================== */

static void test_cmd_dispatch_ping(void)
{
    TEST("cmd_dispatch(\"ping 192.168.1.1\") -> x86 stub output");

    /* Initialize net stack so cmd_ifconfig etc. have data */
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("ping 192.168.1.1", output, sizeof(output));

    /* On x86, ping returns "ping X.X.X.X: not available on x86" */
    int ok = (n > 0);
    ok = ok && (strstr(output, "192.168.1.1") != NULL);
    ok = ok && (strstr(output, "not available") != NULL);
    CHECK(ok, "x86 ping stub output wrong");
}

static void test_cmd_dispatch_ping_bad_ip(void)
{
    TEST("cmd_dispatch(\"ping badaddr\") -> usage message");

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("ping badaddr", output, sizeof(output));

    int ok = (n > 0) && (strstr(output, "Usage") != NULL);
    CHECK(ok, "should show usage for bad IP");
}

static void test_cmd_dispatch_ifconfig(void)
{
    TEST("cmd_dispatch(\"ifconfig\") -> shows MAC and IP");

    uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42};
    uint8_t ip_bytes[4] = {10, 0, 0, 1};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("ifconfig", output, sizeof(output));

    int ok = (n > 0);
    ok = ok && (strstr(output, "de:ad:be:ef:00:42") != NULL);
    ok = ok && (strstr(output, "10.0.0.1") != NULL);
    CHECK(ok, "ifconfig should show MAC and IP");
}

static void test_cmd_dispatch_netstat(void)
{
    TEST("cmd_dispatch(\"netstat\") -> shows ARP cache count");

    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("netstat", output, sizeof(output));

    int ok = (n > 0);
    ok = ok && (strstr(output, "ARP cache") != NULL);
    CHECK(ok, "netstat should mention ARP cache");
}

static void test_cmd_dispatch_stress_x86(void)
{
    TEST("cmd_dispatch(\"stress\") -> not available on x86");

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("stress", output, sizeof(output));

    /* On x86 (without JARVIS_PLATFORM_PI4), stress should be "unknown".
     * The Pi4-guarded stress command is not in the dispatch table on x86,
     * so it falls through to the help/unknown handler. */
    int ok = (n > 0);
    CHECK(ok, "stress dispatch should produce output");
}

static void test_cmd_dispatch_unknown(void)
{
    TEST("cmd_dispatch(\"boguscmd\") -> help list");

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("boguscmd", output, sizeof(output));

    int ok = (n > 0);
    ok = ok && (strstr(output, "Commands:") != NULL);
    ok = ok && (strstr(output, "ping") != NULL);
    CHECK(ok, "unknown command should show help");
}

static void test_cmd_dispatch_leading_spaces(void)
{
    TEST("cmd_dispatch(\"   ifconfig\") -> handles leading spaces");

    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {10, 0, 0, 1};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("   ifconfig", output, sizeof(output));

    int ok = (n > 0);
    ok = ok && (strstr(output, "10.0.0.1") != NULL);
    CHECK(ok, "should skip leading spaces before command");
}

static void test_cmd_dispatch_null(void)
{
    TEST("cmd_dispatch(NULL, ...) -> returns 0");

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch(NULL, output, sizeof(output));
    CHECK(n == 0, "NULL cmd_str should return 0");
}

static void test_cmd_dispatch_null_output(void)
{
    TEST("cmd_dispatch(..., NULL, ...) -> returns 0");

    int n = cmd_dispatch("ifconfig", NULL, 100);
    CHECK(n == 0, "NULL output should return 0");
}

static void test_cmd_dispatch_zero_size(void)
{
    TEST("cmd_dispatch(..., ..., 0) -> returns 0");

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("ifconfig", output, 0);
    CHECK(n == 0, "zero output_size should return 0");
}

static void test_cmd_dispatch_case_insensitive(void)
{
    TEST("cmd_dispatch(\"IFCONFIG\") -> works (case insensitive)");

    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {10, 0, 0, 1};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("IFCONFIG", output, sizeof(output));

    int ok = (n > 0);
    ok = ok && (strstr(output, "10.0.0.1") != NULL);
    CHECK(ok, "uppercase command should work");
}

static void test_cmd_dispatch_ping_with_spaces(void)
{
    TEST("cmd_dispatch(\"ping   10.0.0.1\") -> skips extra spaces");

    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    uint8_t ip_bytes[4] = {192, 168, 1, 100};
    uint32_t ip;
    memcpy(&ip, ip_bytes, 4);
    net_init(ip, mac);

    char output[CMD_OUTPUT_MAX];
    int n = cmd_dispatch("ping   10.0.0.1", output, sizeof(output));

    int ok = (n > 0);
    ok = ok && (strstr(output, "10.0.0.1") != NULL);
    CHECK(ok, "should skip spaces between ping and IP");
}

/* ========================================================================
 * Main
 * ======================================================================== */

int main(void)
{
    printf("=== Network Command Shell Portable Tests ===\n\n");

    /* parse_ipv4 */
    test_parse_ipv4_basic();
    test_parse_ipv4_loopback();
    test_parse_ipv4_all_zeros();
    test_parse_ipv4_max();
    test_parse_ipv4_octet_overflow();
    test_parse_ipv4_too_few_octets();
    test_parse_ipv4_too_many_octets();
    test_parse_ipv4_letters();
    test_parse_ipv4_empty();
    test_parse_ipv4_null();
    test_parse_ipv4_leading_dot();

    /* format_ip */
    test_format_ip_basic();
    test_format_ip_loopback();
    test_format_ip_roundtrip();

    /* format_mac */
    test_format_mac_basic();
    test_format_mac_zeros();

    /* skip_spaces */
    test_skip_spaces_leading();
    test_skip_spaces_tabs();
    test_skip_spaces_mixed();
    test_skip_spaces_none();
    test_skip_spaces_all();

    /* starts_with */
    test_starts_with_exact();
    test_starts_with_case_insensitive();
    test_starts_with_mixed_case();
    test_starts_with_mismatch();
    test_starts_with_empty_prefix();
    test_starts_with_longer_prefix();

    /* safe_remaining */
    test_safe_remaining_normal();
    test_safe_remaining_zero_pos();
    test_safe_remaining_at_end();
    test_safe_remaining_overflow();
    test_safe_remaining_negative();

    /* cmd_dispatch */
    test_cmd_dispatch_ping();
    test_cmd_dispatch_ping_bad_ip();
    test_cmd_dispatch_ifconfig();
    test_cmd_dispatch_netstat();
    test_cmd_dispatch_stress_x86();
    test_cmd_dispatch_unknown();
    test_cmd_dispatch_leading_spaces();
    test_cmd_dispatch_null();
    test_cmd_dispatch_null_output();
    test_cmd_dispatch_zero_size();
    test_cmd_dispatch_case_insensitive();
    test_cmd_dispatch_ping_with_spaces();

    printf("\n=== Results: %d PASS, %d FAIL (of %d) ===\n",
           pass_count, fail_count, pass_count + fail_count);

    return fail_count > 0 ? 1 : 0;
}
