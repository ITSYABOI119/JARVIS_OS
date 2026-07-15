/*
 * test_hmac_sha256.c — RFC 4231 test vectors + constant-time coverage.
 *
 * Pins HMAC-SHA-256 against RFC 4231 cases 1, 2, 4, 6 (case 6 has a
 * >64-byte key, exercising the key-hash branch), then proves the verify
 * compare reads all 32 tag bytes (flip each bit of each byte -> reject).
 *
 * Prints PASS/FAIL per check; exits nonzero on any failure.
 */
#include "hmac_sha256.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int g_fail = 0;

/* Parse a lowercase hex string into bytes. Returns byte count. */
static size_t hexbytes(const char *hex, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (hex[0] && hex[1] && n < cap) {
        unsigned v;
        if (sscanf(hex, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        hex += 2;
    }
    return n;
}

static void check_case(const char *name,
                       const uint8_t *key, size_t klen,
                       const uint8_t *msg, size_t mlen,
                       const char *expect_hex)
{
    uint8_t exp[32];
    uint8_t got[32];
    size_t explen = hexbytes(expect_hex, exp, sizeof(exp));

    hmac_sha256(key, klen, msg, mlen, got);

    if (explen == 32 && memcmp(got, exp, 32) == 0) {
        printf("PASS %-8s ", name);
        for (int i = 0; i < 32; i++) printf("%02x", got[i]);
        printf("\n");
    } else {
        g_fail = 1;
        printf("FAIL %-8s got=", name);
        for (int i = 0; i < 32; i++) printf("%02x", got[i]);
        printf(" want=%s\n", expect_hex);
    }
}

int main(void)
{
    /* ---- RFC 4231 Case 1 ---- */
    {
        uint8_t key[20];
        memset(key, 0x0b, sizeof(key));
        const char *data = "Hi There";
        check_case("case1", key, sizeof(key),
                   (const uint8_t *)data, strlen(data),
                   "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    }

    /* ---- RFC 4231 Case 2 ---- */
    {
        const char *key = "Jefe";
        const char *data = "what do ya want for nothing?";
        check_case("case2", (const uint8_t *)key, strlen(key),
                   (const uint8_t *)data, strlen(data),
                   "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    }

    /* ---- RFC 4231 Case 4 ---- key = 0x01..0x19 (25 bytes), data = 0xcd x50 */
    {
        uint8_t key[25];
        for (int i = 0; i < 25; i++) key[i] = (uint8_t)(i + 1);
        uint8_t data[50];
        memset(data, 0xcd, sizeof(data));
        check_case("case4", key, sizeof(key), data, sizeof(data),
                   "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    }

    /* ---- RFC 4231 Case 6 ---- key = 0xaa x131 (>64, key-hash branch) */
    {
        uint8_t key[131];
        memset(key, 0xaa, sizeof(key));
        const char *data =
            "Test Using Larger Than Block-Size Key - Hash Key First";
        check_case("case6", key, sizeof(key),
                   (const uint8_t *)data, strlen(data),
                   "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    }

    /* ---- Constant-time coverage: verify reads every one of the 32 bytes ---- */
    {
        uint8_t key[20];
        memset(key, 0x0b, sizeof(key));
        const char *data = "Hi There";
        uint8_t good[32];
        hmac_sha256(key, sizeof(key), (const uint8_t *)data, strlen(data), good);

        /* Untouched tag must verify. */
        if (hmac_sha256_verify(key, sizeof(key),
                               (const uint8_t *)data, strlen(data), good) == 1) {
            printf("PASS ct-good  untouched tag accepted\n");
        } else {
            g_fail = 1;
            printf("FAIL ct-good  untouched tag rejected\n");
        }

        /* Flip one bit in each byte position -> must reject (all 32 read). */
        int flips_ok = 0;
        for (int i = 0; i < 32; i++) {
            for (int b = 0; b < 8; b++) {
                uint8_t bad[32];
                memcpy(bad, good, 32);
                bad[i] ^= (uint8_t)(1u << b);
                if (hmac_sha256_verify(key, sizeof(key),
                                       (const uint8_t *)data, strlen(data),
                                       bad) == 0) {
                    flips_ok++;
                } else {
                    g_fail = 1;
                    printf("FAIL ct-flip  byte %d bit %d accepted a bad tag\n",
                           i, b);
                }
            }
        }
        /* 32 bytes covered — report one PASS per byte position that rejected. */
        int byte_cover = 0;
        for (int i = 0; i < 32; i++) {
            uint8_t bad[32];
            memcpy(bad, good, 32);
            bad[i] ^= 0xff;
            if (hmac_sha256_verify(key, sizeof(key),
                                   (const uint8_t *)data, strlen(data),
                                   bad) == 0) {
                byte_cover++;
            } else {
                g_fail = 1;
                printf("FAIL ct-cover byte %d not read by compare\n", i);
            }
        }
        if (byte_cover == 32 && flips_ok == 256) {
            printf("PASS ct-cover all 32 bytes read (256 bit-flips + 32 byte-flips rejected)\n");
        }
    }

    if (g_fail) {
        printf("\nHMAC-SHA-256: FAILURES PRESENT\n");
        return 1;
    }
    printf("\nHMAC-SHA-256: ALL RFC 4231 + constant-time checks PASS\n");
    return 0;
}
