/*
 * test_sha256.c — FIPS 180-4 known-answer vectors for sha256.c
 * Prints PASS/FAIL per case; exits nonzero if any vector mismatches.
 */
#include <stdio.h>
#include <string.h>
#include "sha256.h"

static int check(const char *name, const char *msg, const char *want_hex)
{
    uint8_t digest[32];
    char got_hex[65];
    int i;

    sha256((const uint8_t *)msg, strlen(msg), digest);
    for (i = 0; i < 32; ++i)
        sprintf(got_hex + i * 2, "%02x", digest[i]);
    got_hex[64] = '\0';

    if (strcmp(got_hex, want_hex) == 0) {
        printf("PASS %-6s %s\n", name, got_hex);
        return 0;
    }
    printf("FAIL %-6s\n  got:  %s\n  want: %s\n", name, got_hex, want_hex);
    return 1;
}

int main(void)
{
    int fails = 0;

    fails += check("empty", "",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    fails += check("abc", "abc",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    fails += check("56ch",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    if (fails) {
        printf("\n%d vector(s) FAILED\n", fails);
        return 1;
    }
    printf("\nALL 3 vectors PASS\n");
    return 0;
}
