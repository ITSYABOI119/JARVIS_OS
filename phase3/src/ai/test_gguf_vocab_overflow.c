/*
 * Phase 3 SEC regression — gguf_vocab string readers must reject integer-overflow
 * bounds bypasses (buf_read_gguf_string_fixed + buf_skip). Under AddressSanitizer a
 * reverted fix would OOB-read here (memcpy ~out_size bytes past a near-EOF cursor) and
 * abort. We #include the .c to reach the static helpers directly.
 *
 * Build: gcc -Wall -Werror -O1 -std=c11 -fsanitize=address,undefined \
 *   -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
 *   phase3/src/ai/test_gguf_vocab_overflow.c \
 *   phase3/src/ai/tokenizer.c phase3/src/ai/gguf_parser.c -lm -o /tmp/t && /tmp/t
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "gguf_vocab.c"   /* reach the static buf_* helpers under test */

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do {                                  \
    if (cond) { g_pass++; printf("  PASS: %s\n", msg); }       \
    else      { g_fail++; printf("  FAIL: %s\n", msg); }       \
} while (0)

/* Write a little-endian u64 into buf[0..7] (host is x86/ARM LE). */
static void put_u64le(uint8_t *buf, uint64_t v)
{
    for (int i = 0; i < 8; i++) buf[i] = (uint8_t)(v >> (8 * i));
}

int main(void)
{
    printf("== test_gguf_vocab_overflow ==\n");

    char out[512];   /* mirrors the real caller's key[512] */

    /* T1: a wrapping u64 len near EOF must be rejected with NO OOB read.
     * Buffer is malloc'd tight (16 B) so a reverted fix's ~511 B memcpy from pos=8
     * lands in ASan's redzone and aborts. */
    {
        uint8_t *buf = (uint8_t *)malloc(16);
        put_u64le(buf, 0xFFFFFFFFFFFFFFFFULL);   /* len = 2^64 - 1 (r->pos + len wraps) */
        memset(buf + 8, 'A', 8);
        buf_reader_t r = { buf, 16, 0 };
        int rc = buf_read_gguf_string_fixed(&r, out, sizeof(out));
        CHECK(rc == -1, "T1: wrapping u64 len rejected (no OOB read)");
        free(buf);
    }

    /* T2: len exceeds the remaining bytes but does NOT wrap -> reject. */
    {
        uint8_t *buf = (uint8_t *)malloc(16);
        put_u64le(buf, 100);          /* claims 100 bytes; only 8 remain after the len field */
        memset(buf + 8, 'B', 8);
        buf_reader_t r = { buf, 16, 0 };
        int rc = buf_read_gguf_string_fixed(&r, out, sizeof(out));
        CHECK(rc == -1, "T2: len > remaining rejected");
        free(buf);
    }

    /* T3: len larger than out_size, but fully inside the buffer -> the size cap rejects it
     * (without the cap this would truncate-and-succeed = rc 0, so this test has teeth). */
    {
        size_t n = 8 + 600;
        uint8_t *buf = (uint8_t *)malloc(n);
        put_u64le(buf, 600);          /* 600 > out_size(512) */
        memset(buf + 8, 'D', 600);
        buf_reader_t r = { buf, n, 0 };
        int rc = buf_read_gguf_string_fixed(&r, out, sizeof(out));
        CHECK(rc == -1, "T3: len > out_size rejected by the size cap");
        free(buf);
    }

    /* T4: a normal in-bounds string reads correctly and is NUL-terminated. */
    {
        const char *s = "hello";
        uint8_t *buf = (uint8_t *)malloc(8 + 5);
        put_u64le(buf, 5);
        memcpy(buf + 8, s, 5);
        buf_reader_t r = { buf, 8 + 5, 0 };
        memset(out, 'X', sizeof(out));
        int rc = buf_read_gguf_string_fixed(&r, out, sizeof(out));
        CHECK(rc == 0, "T4: in-bounds string accepted");
        CHECK(rc == 0 && strcmp(out, "hello") == 0, "T4: content == 'hello' (NUL-terminated)");
        CHECK(r.pos == (size_t)(8 + 5), "T4: cursor advanced by 8 + len");
        free(buf);
    }

    /* T5: buf_skip rejects a wrapping n; a normal skip advances the cursor. */
    {
        uint8_t *buf = (uint8_t *)malloc(16);
        memset(buf, 0, 16);
        buf_reader_t r = { buf, 16, 8 };
        int rc = buf_skip(&r, (size_t)0xFFFFFFFFFFFFFFFFULL);   /* wrapping n */
        CHECK(rc == -1, "T5: buf_skip rejects wrapping n");
        CHECK(r.pos == 8, "T5: cursor unchanged after a rejected skip");
        int rc2 = buf_skip(&r, 4);
        CHECK(rc2 == 0 && r.pos == 12, "T5: normal skip advances the cursor");
        free(buf);
    }

    printf("== %d PASS, %d FAIL ==\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
