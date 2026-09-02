/* test_control_console.c — Phase 6 6-5/M4a host test for control_console.c (pure logic). */
#include "control_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

static int pass = 0, fail = 0;
#define CHECK(c, msg) do { \
    if (c) { pass++; } \
    else   { fail++; printf("  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while (0)

/* The reference console address the box gate provisions (Main PC / Ethernet 3). */
static const uint8_t REF_MAC[6] = { 0x9C, 0x6B, 0x00, 0xAE, 0x6A, 0xFF };
#define REF_IP   ((uint32_t)((192u << 24) | (168u << 16) | (100u << 8) | 146u))  /* 0xC0A86492 */
#define REF_PORT ((uint16_t)51002u)

/* Independent re-implementation of the sector fold (control_floor.c's rotate-xor-add), so the
 * test can forge a VALID checksum over a hand-patched sector and thereby isolate the version
 * gate from the checksum gate. */
static uint32_t ref_checksum(const uint8_t *p, size_t n)
{
    uint32_t c = 0x811C9DC5u;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        c = (c << 5) | (c >> 27);
        c += 0x9E3779B9u;
    }
    return c;
}

/* Little-endian scalar readers — assert the ON-DISK byte order explicitly rather than
 * trusting the host struct layout. */
static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}


/* ---------------------------------------------------------------------------
 * Host CLI (appended 2026-09-03) — the slot PROVISIONER.
 *
 * The console-address slot was provisioned once at 6-5/M4a using a throwaway
 * Python MIRROR of ctrl_console_build kept on the box. The project rule is that
 * a sector the box will parse comes from the code the box parses it with, so the
 * provisioner lives here instead:
 *
 *   --build  writes a slot through the REAL ctrl_console_build, then re-reads the
 *            file and runs the REAL ctrl_console_parse over the bytes ON DISK --
 *            proving what the box will accept, not merely that an in-memory
 *            struct was right.
 *   --parse  reads one back.
 *
 * The no-argument suite below is untouched and so is its tally: main() dispatches
 * before it and returns.
 *
 * A file that fails to parse yields EXACTLY "INVALID" on stdout and nothing else.
 * That matters because the JKEY sector holding the control-IN HMAC key is three
 * sectors away from this one: if it is ever fed here by a slip of a `skip=`, this
 * tool must not become a way to print any part of it.
 * --------------------------------------------------------------------------- */

static int parse_mac_str(const char *s, uint8_t out[6])
{
    unsigned b[6]; char tail;
    if (strlen(s) != 17) return 0;          /* argv is NUL-terminated; the no-strlen rule is for FILE bytes */
    int n = sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x%c",
                   &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &tail);
    if (n != 6) return 0;                   /* n == 7 means trailing junk */
    for (int i = 0; i < 6; i++) {
        if (b[i] > 0xFFu) return 0;
        out[i] = (uint8_t)b[i];
    }
    return 1;
}

static int parse_ip_str(const char *s, uint32_t *out)
{
    unsigned a, b, c, d; char tail;
    int n = sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail);
    if (n != 4) return 0;
    if (a > 255u || b > 255u || c > 255u || d > 255u) return 0;
    *out = (a << 24) | (b << 16) | (c << 8) | d;   /* HOST-ORDER u32 -- the slot's convention */
    return 1;
}

static int parse_port_str(const char *s, uint16_t *out)
{
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || !end || *end != '\0' || v > 65535ul) return 0;
    *out = (uint16_t)v;
    return 1;
}

/* Exactly 512 bytes -- not "at least". A longer file is refused too, because a
 * multi-sector dump fed here would silently be judged on its first sector. */
static int read_sector_exact(const char *path, uint8_t buf[512])
{
    FILE *fh = fopen(path, "rb");
    if (!fh) return 0;
    size_t got = fread(buf, 1, 512, fh);
    int extra = (got == 512) ? (fgetc(fh) != EOF) : 0;
    fclose(fh);
    return (got == 512 && !extra);
}

static void print_addr(const char *lead, const uint8_t mac[6], uint32_t ip, uint16_t port)
{
    printf("%s mac=%02x:%02x:%02x:%02x:%02x:%02x ip=%u.%u.%u.%u (0x%08X) port=%u",
           lead, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
           (ip >> 24) & 0xFFu, (ip >> 16) & 0xFFu, (ip >> 8) & 0xFFu, ip & 0xFFu,
           ip, (unsigned)port);
}

static int cli_build(char **a)      /* a[0]=mac a[1]=ip a[2]=port a[3]=out */
{
    uint8_t mac[6]; uint32_t ip = 0; uint16_t port = 0;
    if (!parse_mac_str(a[0], mac))   { fprintf(stderr, "bad mac (want aa:bb:cc:dd:ee:ff)\n"); return 2; }
    if (!parse_ip_str(a[1], &ip))    { fprintf(stderr, "bad ip (want a.b.c.d, each 0-255)\n"); return 2; }
    if (!parse_port_str(a[2], &port)) { fprintf(stderr, "bad port (want 0-65535)\n"); return 2; }

    uint8_t buf[512];
    ctrl_console_build(buf, mac, ip, port);

    FILE *fh = fopen(a[3], "wb");
    if (!fh) { fprintf(stderr, "cannot open %s for writing\n", a[3]); return 2; }
    size_t w = fwrite(buf, 1, 512, fh);
    int cerr = fclose(fh);
    if (w != 512 || cerr != 0) { fprintf(stderr, "short write to %s\n", a[3]); return 2; }

    uint8_t back[512];
    if (!read_sector_exact(a[3], back)) { fprintf(stderr, "read-back of %s failed\n", a[3]); return 2; }
    uint8_t rmac[6]; uint32_t rip = 0; uint16_t rport = 0;
    int ok = ctrl_console_parse(back, rmac, &rip, &rport);
    if (ok != 1 || memcmp(rmac, mac, 6) != 0 || rip != ip || rport != port) {
        fprintf(stderr, "read-back disagrees with the inputs -- refusing to report success\n");
        return 1;
    }
    print_addr("built:", mac, ip, port);
    printf(" checksum_ok=%d bytes16..19=%02x%02x%02x%02x\n",
           ok, back[16], back[17], back[18], back[19]);
    return 0;
}

static int cli_parse(const char *path)
{
    uint8_t buf[512];
    if (!read_sector_exact(path, buf)) { fprintf(stderr, "need a file of exactly 512 bytes\n"); return 2; }
    uint8_t mac[6]; uint32_t ip = 0; uint16_t port = 0;
    if (ctrl_console_parse(buf, mac, &ip, &port) != 1) {
        printf("INVALID\n");     /* stdout, and NOTHING else -- see the header note */
        return 1;
    }
    print_addr("parsed:", mac, ip, port);
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--build") == 0) {
        if (argc != 6) { fprintf(stderr, "usage: %s --build <mac> <ip> <port> <out>\n", argv[0]); return 2; }
        return cli_build(&argv[2]);
    }
    if (argc >= 2 && strcmp(argv[1], "--parse") == 0) {
        if (argc != 3) { fprintf(stderr, "usage: %s --parse <file>\n", argv[0]); return 2; }
        return cli_parse(argv[2]);
    }
    if (argc != 1) { fprintf(stderr, "usage: %s [--build <mac> <ip> <port> <out> | --parse <file>]\n", argv[0]); return 2; }

    printf("== test_control_console ==\n");

    /* ---- T1: build -> parse round-trip, reference values EXACT ---- */
    {
        uint8_t buf[512];
        uint8_t mac[6]; uint32_t ip = 0; uint16_t port = 0;
        ctrl_console_build(buf, REF_MAC, REF_IP, REF_PORT);
        CHECK(ctrl_console_parse(buf, mac, &ip, &port) == 1, "round-trip parse ok");
        CHECK(memcmp(mac, REF_MAC, 6) == 0, "round-trip mac exact (9c:6b:00:ae:6a:ff)");
        CHECK(ip == REF_IP, "round-trip ip exact (192.168.100.146 host-order)");
        CHECK(ip == 0xC0A86492u, "ip host-order value pinned 0xC0A86492");
        CHECK(port == REF_PORT, "round-trip port exact (51002)");
        CHECK(port == CONTROL_REPLY_PORT, "port == CONTROL_REPLY_PORT reference default");
        /* NULL out-params are safe and still give the right verdict */
        CHECK(ctrl_console_parse(buf, NULL, NULL, NULL) == 1, "parse NULL out-params -> 1");
    }

    /* ---- T2: a DIFFERENT address round-trips (no reference-value hardcoding in the module) ---- */
    {
        uint8_t buf[512];
        const uint8_t other[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
        uint8_t mac[6]; uint32_t ip = 0; uint16_t port = 0;
        ctrl_console_build(buf, other, 0x0A000001u, 9999u);   /* 10.0.0.1 : 9999 */
        CHECK(ctrl_console_parse(buf, mac, &ip, &port) == 1, "alt-address parse ok");
        CHECK(memcmp(mac, other, 6) == 0, "alt mac exact");
        CHECK(ip == 0x0A000001u && port == 9999u, "alt ip+port exact");
        /* extreme values survive (no truncation / sign issues) */
        const uint8_t ff[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
        ctrl_console_build(buf, ff, 0xFFFFFFFFu, 0xFFFFu);
        CHECK(ctrl_console_parse(buf, mac, &ip, &port) == 1, "max-values parse ok");
        CHECK(memcmp(mac, ff, 6) == 0 && ip == 0xFFFFFFFFu && port == 0xFFFFu,
              "max mac/ip/port exact (no truncation)");
    }

    /* ---- T3: the ON-DISK LAYOUT is pinned by OFFSET (a struct reshuffle must fail here) ---- */
    {
        /* compile-layout offsets */
        CHECK(offsetof(ctrl_console_slot_t, magic)    == 0,  "offsetof magic == 0");
        CHECK(offsetof(ctrl_console_slot_t, version)  == 4,  "offsetof version == 4");
        CHECK(offsetof(ctrl_console_slot_t, mac)      == 8,  "offsetof mac == 8");
        CHECK(offsetof(ctrl_console_slot_t, ip)       == 16, "offsetof ip == 16");
        CHECK(offsetof(ctrl_console_slot_t, port)     == 20, "offsetof port == 20");
        CHECK(offsetof(ctrl_console_slot_t, checksum) == 24, "offsetof checksum == 24");
        CHECK(sizeof(ctrl_console_slot_t) == 512, "sizeof slot == 512 at runtime");

        /* the RAW BYTES of a built sector agree with those offsets */
        uint8_t buf[512];
        ctrl_console_build(buf, REF_MAC, REF_IP, REF_PORT);
        CHECK(rd_le32(&buf[0])  == CTRL_CONSOLE_MAGIC,   "raw: magic LE @0 == JCON");
        CHECK(rd_le16(&buf[4])  == CTRL_CONSOLE_VERSION, "raw: version LE @4 == 1");
        CHECK(memcmp(&buf[8], REF_MAC, 6) == 0,          "raw: mac wire-order @8..13");
        CHECK(rd_le32(&buf[16]) == REF_IP,               "raw: ip LE @16");
        CHECK(buf[16] == 0x92 && buf[17] == 0x64 && buf[18] == 0xA8 && buf[19] == 0xC0,
              "raw: ip on-disk bytes are LITTLE-ENDIAN (92 64 A8 C0)");
        CHECK(rd_le16(&buf[20]) == REF_PORT,             "raw: port LE @20");
        CHECK(rd_le32(&buf[24]) == ref_checksum(buf, 24),
              "raw: checksum LE @24 covers exactly the first 24 bytes");
        /* reserved gaps + pad are zeroed by build */
        CHECK(buf[6] == 0 && buf[7] == 0,   "raw: reserved0 @6..7 zeroed");
        CHECK(buf[14] == 0 && buf[15] == 0, "raw: reserved1 @14..15 zeroed");
        CHECK(buf[22] == 0 && buf[23] == 0, "raw: reserved2 @22..23 zeroed");
        int pad_zero = 1;
        for (int i = 28; i < 512; i++) if (buf[i] != 0) pad_zero = 0;
        CHECK(pad_zero == 1, "raw: pad @28..511 fully zeroed by build");
    }

    /* ---- T4: checksum teeth — a 1-bit flip in EACH region rejects ---- */
    {
        uint8_t buf[512];
        struct { int off; const char *what; } spots[] = {
            {  0, "magic"    },   /* also trips the magic gate — still must reject */
            {  4, "version"  },
            {  8, "mac[0]"   },
            { 13, "mac[5]"   },
            { 16, "ip"       },
            { 20, "port"     },
            { 24, "checksum" },   /* the checksum field ITSELF */
        };
        for (unsigned i = 0; i < sizeof spots / sizeof spots[0]; i++) {
            ctrl_console_build(buf, REF_MAC, REF_IP, REF_PORT);
            CHECK(ctrl_console_parse(buf, NULL, NULL, NULL) == 1, "pre-flip sector valid");
            buf[spots[i].off] ^= 0x01;
            if (ctrl_console_parse(buf, NULL, NULL, NULL) != 0) {
                fail++; printf("  FAIL: bit-flip in %s not caught (line %d)\n", spots[i].what, __LINE__);
            } else { pass++; }
            buf[spots[i].off] ^= 0x01;   /* restore */
            CHECK(ctrl_console_parse(buf, NULL, NULL, NULL) == 1, "restore -> valid again");
        }
        /* a flip out in the pad is ALSO caught? No — pad is outside the covered region by
         * design; pin that so the coverage boundary is explicit, not accidental. */
        ctrl_console_build(buf, REF_MAC, REF_IP, REF_PORT);
        buf[400] ^= 0x01;
        CHECK(ctrl_console_parse(buf, NULL, NULL, NULL) == 1,
              "pad flip is OUTSIDE the checksum region (documented boundary)");
    }

    /* ---- T5: all-zero / magic / version rejects ---- */
    {
        uint8_t z[512]; memset(z, 0, sizeof z);
        CHECK(ctrl_console_parse(z, NULL, NULL, NULL) == 0, "all-zero sector -> 0 (not provisioned)");

        uint8_t g[512]; memset(g, 0xAB, sizeof g);
        CHECK(ctrl_console_parse(g, NULL, NULL, NULL) == 0, "garbage sector -> 0");

        uint8_t buf[512];
        ctrl_console_build(buf, REF_MAC, REF_IP, REF_PORT);
        buf[0] ^= 0xFF;
        CHECK(ctrl_console_parse(buf, NULL, NULL, NULL) == 0, "wrong magic -> reject");

        /* ISOLATED version gate: patch version to 2 AND re-forge a VALID checksum, so the
         * ONLY reason to reject is the version mismatch. */
        ctrl_console_build(buf, REF_MAC, REF_IP, REF_PORT);
        buf[4] = 2; buf[5] = 0;
        uint32_t good = ref_checksum(buf, 24);
        buf[24] = (uint8_t)(good);        buf[25] = (uint8_t)(good >> 8);
        buf[26] = (uint8_t)(good >> 16);  buf[27] = (uint8_t)(good >> 24);
        CHECK(ctrl_console_parse(buf, NULL, NULL, NULL) == 0,
              "version 2 with a VALID checksum -> reject (version gate isolated)");
    }

    /* ---- T6: out-params are UNTOUCHED on every reject (no half-set state) ---- */
    {
        uint8_t mac[6]; uint32_t ip; uint16_t port;
        uint8_t bad[512];

        /* all-zero */
        memset(mac, 0xAA, sizeof mac); ip = 0xAAAAAAAAu; port = 0xAAAAu;
        memset(bad, 0, sizeof bad);
        CHECK(ctrl_console_parse(bad, mac, &ip, &port) == 0, "reject: all-zero");
        CHECK(mac[0] == 0xAA && mac[5] == 0xAA && ip == 0xAAAAAAAAu && port == 0xAAAAu,
              "out-params still poisoned after all-zero reject");

        /* bad magic */
        memset(mac, 0xAA, sizeof mac); ip = 0xAAAAAAAAu; port = 0xAAAAu;
        ctrl_console_build(bad, REF_MAC, REF_IP, REF_PORT);
        bad[0] ^= 0xFF;
        CHECK(ctrl_console_parse(bad, mac, &ip, &port) == 0, "reject: bad magic");
        CHECK(mac[0] == 0xAA && ip == 0xAAAAAAAAu && port == 0xAAAAu,
              "out-params still poisoned after magic reject");

        /* bad checksum (mac corrupted) */
        memset(mac, 0xAA, sizeof mac); ip = 0xAAAAAAAAu; port = 0xAAAAu;
        ctrl_console_build(bad, REF_MAC, REF_IP, REF_PORT);
        bad[10] ^= 0x40;
        CHECK(ctrl_console_parse(bad, mac, &ip, &port) == 0, "reject: bad checksum");
        CHECK(mac[0] == 0xAA && mac[2] == 0xAA && ip == 0xAAAAAAAAu && port == 0xAAAAu,
              "out-params still poisoned after checksum reject (the crux)");

        /* partial NULLs on a VALID sector: the non-NULL ones are set, no crash */
        ctrl_console_build(bad, REF_MAC, REF_IP, REF_PORT);
        ip = 0xAAAAAAAAu;
        CHECK(ctrl_console_parse(bad, NULL, &ip, NULL) == 1, "valid sector, partial NULL out-params");
        CHECK(ip == REF_IP, "partial-NULL: ip still delivered");
    }

    /* ---- T7: storage-map arithmetic pinned ---- */
    {
        CHECK(CTRL_CONSOLE_LBA == 21130003ULL, "CTRL_CONSOLE_LBA == 21,130,003");
        CHECK(CTRL_CONSOLE_LBA == CTRL_KEY_BASE_LBA + 3ULL, "console slot == base + 3");
        CHECK(CTRL_KEY_BASE_LBA == 21130000ULL, "control-IN sub-region base pinned");
        CHECK(CTRL_CONSOLE_MAGIC == 0x4A434F4Eu, "magic == 'JCON'");
        CHECK(CTRL_CONSOLE_VERSION == 1u, "version == 1");
        CHECK(CONTROL_REPLY_PORT == 51002u, "CONTROL_REPLY_PORT reference default == 51002");
    }

    printf("== %d PASS, %d FAIL ==\n", pass, fail);
    return fail ? 1 : 0;
}
