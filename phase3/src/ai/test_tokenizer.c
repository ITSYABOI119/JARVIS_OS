#include "tokenizer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)
#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)
#define PASS(name) do { printf("  PASS: %s\n", name); tests_passed++; } while(0)

/* ---- Test vocabulary (17 tokens) ---- */
static const char *test_vocab[] = {
    "h", "e", "l", "o", " ", "w", "r", "d",  /* 0-7  single chars */
    "he",     /* 8  */
    "wo",     /* 9  */
    "ll",     /* 10 */
    "lo",     /* 11 */
    "hell",   /* 12 */
    "wor",    /* 13 */
    "hello",  /* 14 */
    "worl",   /* 15 */
    "world",  /* 16 */
};
#define TEST_VOCAB_SIZE 17

/* Scores: higher (less negative) = merge first.
 * Single chars at 0, bigrams at -10..-30, longer at -40..-60.
 * BPE merges highest-score pair first, so "he" (-10) merges before
 * "wo" (-15), then "hell" (-40) from "he"+"ll", etc.
 */
static const float test_scores[] = {
    0, 0, 0, 0, 0, 0, 0, 0,       /* single chars: score 0 */
    -10, -15, -20, -30,            /* bigrams */
    -40, -45, -50, -55, -60        /* longer tokens */
};

static tokenizer_t tok;

static void setup(void)
{
    int rc = tokenizer_init(&tok, test_vocab, test_scores,
                            TEST_VOCAB_SIZE, 0, 1);
    if (rc != 0) {
        printf("FATAL: tokenizer_init failed\n");
        exit(1);
    }
}

static void teardown(void)
{
    tokenizer_free(&tok);
}

/* ---- Test 1: encode single char "h" ---- */
TEST(test_encode_single_char)
{
    int ids[16];
    int n = tokenizer_encode(&tok, "h", ids, 16);
    ASSERT(n == 1, "expected 1 token");
    ASSERT(ids[0] == 0, "expected token id 0 for 'h'");
    PASS("encode 'h' -> [0]");
}

/* ---- Test 2: encode "he" -> [8] ---- */
TEST(test_encode_he)
{
    int ids[16];
    int n = tokenizer_encode(&tok, "he", ids, 16);
    ASSERT(n == 1, "expected 1 token");
    ASSERT(ids[0] == 8, "expected token id 8 for 'he'");
    PASS("encode 'he' -> [8]");
}

/* ---- Test 3: encode "hello" -> [14] ---- */
TEST(test_encode_hello)
{
    int ids[16];
    int n = tokenizer_encode(&tok, "hello", ids, 16);
    ASSERT(n == 1, "expected 1 token");
    ASSERT(ids[0] == 14, "expected token id 14 for 'hello'");
    PASS("encode 'hello' -> [14]");
}

/* ---- Test 4: encode "world" -> [16] ---- */
TEST(test_encode_world)
{
    int ids[16];
    int n = tokenizer_encode(&tok, "world", ids, 16);
    ASSERT(n == 1, "expected 1 token");
    ASSERT(ids[0] == 16, "expected token id 16 for 'world'");
    PASS("encode 'world' -> [16]");
}

/* ---- Test 5: encode "hello world" -> [14, 4, 16] ---- */
TEST(test_encode_hello_world)
{
    int ids[16];
    int n = tokenizer_encode(&tok, "hello world", ids, 16);
    ASSERT(n == 3, "expected 3 tokens");
    ASSERT(ids[0] == 14, "expected 'hello' = 14");
    ASSERT(ids[1] == 4, "expected ' ' = 4");
    ASSERT(ids[2] == 16, "expected 'world' = 16");
    PASS("encode 'hello world' -> [14, 4, 16]");
}

/* ---- Test 6: encode "hell" -> [12] ---- */
TEST(test_encode_hell)
{
    int ids[16];
    int n = tokenizer_encode(&tok, "hell", ids, 16);
    ASSERT(n == 1, "expected 1 token");
    ASSERT(ids[0] == 12, "expected token id 12 for 'hell'");
    PASS("encode 'hell' -> [12]");
}

/* ---- Test 7: unknown char "x" doesn't crash ---- */
TEST(test_encode_unknown_char)
{
    int ids[16];
    int n = tokenizer_encode(&tok, "x", ids, 16);
    ASSERT(n == 1, "expected 1 token for unknown char");
    ASSERT(ids[0] == -1, "expected token_id -1 for unknown 'x'");
    PASS("encode 'x' -> [-1] (no crash)");
}

/* ---- Test 8: decode [14] -> "hello" ---- */
TEST(test_decode_hello)
{
    int ids[] = { 14 };
    char buf[64];
    int len = tokenizer_decode(&tok, ids, 1, buf, 64);
    ASSERT(len == 5, "expected length 5");
    ASSERT(strcmp(buf, "hello") == 0, "expected 'hello'");
    PASS("decode [14] -> 'hello'");
}

/* ---- Test 9: decode [14, 4, 16] -> "hello world" ---- */
TEST(test_decode_hello_world)
{
    int ids[] = { 14, 4, 16 };
    char buf[64];
    int len = tokenizer_decode(&tok, ids, 3, buf, 64);
    ASSERT(len == 11, "expected length 11");
    ASSERT(strcmp(buf, "hello world") == 0, "expected 'hello world'");
    PASS("decode [14, 4, 16] -> 'hello world'");
}

/* ---- Test 10: decode empty -> "" ---- */
TEST(test_decode_empty)
{
    char buf[64];
    int len = tokenizer_decode(&tok, NULL, 0, buf, 64);
    ASSERT(len == 0, "expected length 0");
    ASSERT(strcmp(buf, "") == 0, "expected empty string");
    PASS("decode [] -> ''");
}

/* ---- Test 11: find "hello" -> 14, find "xyz" -> -1 ---- */
TEST(test_find)
{
    ASSERT(tokenizer_find(&tok, "hello") == 14, "expected 14 for 'hello'");
    ASSERT(tokenizer_find(&tok, "xyz") == -1, "expected -1 for 'xyz'");
    PASS("find 'hello' -> 14, find 'xyz' -> -1");
}

/* ---- Test 12: encode with max_tokens=2 -> truncated ---- */
TEST(test_encode_truncated)
{
    int ids[2];
    int n = tokenizer_encode(&tok, "hello world", ids, 2);
    ASSERT(n == 2, "expected 2 tokens (truncated)");
    ASSERT(ids[0] == 14, "expected 'hello' = 14");
    ASSERT(ids[1] == 4, "expected ' ' = 4");
    PASS("encode 'hello world' max_tokens=2 -> [14, 4] (truncated)");
}

/* ---- T-N1..T-N4: tokenizer_encode_n carries the length; an interior NUL is DATA ----
 * The one break in the project's copy-by-length discipline was this function's strlen. The two live
 * PB callers hold a length (the retrieval preamble's, and the control-IN query's from the IPC) and
 * used to drop it, so a NUL inside a stored answer would have truncated the injected preamble
 * mid-fact and logged nothing. */
TEST(test_encode_n_prefix)
{
    int a[16], b[16];
    int na = tokenizer_encode_n(&tok, "hello world", 5, a, 16);
    int nb = tokenizer_encode(&tok, "hello", b, 16);
    ASSERT(na == nb && na > 0, "_n(text,5) must equal encode(\"hello\")");
    for (int i = 0; i < na; i++)
        ASSERT(a[i] == b[i], "_n(text,5) ids must equal encode(\"hello\") ids");
    PASS("T-N1 _n encodes exactly text_len bytes");
}

TEST(test_encode_n_wrapper_equivalence)
{
    static const char *cases[] = { "hello", "hello world", "he" };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        int a[32], b[32];
        int na = tokenizer_encode(&tok, cases[c], a, 32);
        int nb = tokenizer_encode_n(&tok, cases[c], (int)strlen(cases[c]), b, 32);
        ASSERT(na == nb, "the wrapper must equal _n(text, strlen(text))");
        for (int i = 0; i < na; i++)
            ASSERT(a[i] == b[i], "the wrapper's ids must equal _n's");
    }
    PASS("T-N2 tokenizer_encode == _n(text, strlen(text))");
}

TEST(test_encode_n_interior_nul)
{
    /* the bytes a, b, NUL, c, d with text_len 5: the NUL is a byte to encode, not a terminator. */
    static const char buf[6] = { 'a', 'b', 0, 'c', 'd', 0 };
    int full[32], head[32];
    int nf = tokenizer_encode_n(&tok, buf, 5, full, 32);
    int nh = tokenizer_encode_n(&tok, buf, 2, head, 32);
    ASSERT(nf > nh, "encoding 5 bytes must yield MORE tokens than stopping at the NUL");
    ASSERT(nh > 0, "the leading 'ab' must encode");
    for (int i = 0; i < nh; i++)
        ASSERT(full[i] == head[i], "the leading 'ab' ids must be unchanged by what follows");
    /* And the wrapper, which strlens, sees only the first two bytes - the truncation this exists
     * to remove, demonstrated rather than asserted away. */
    int wrapped[32];
    int nw = tokenizer_encode(&tok, buf, wrapped, 32);
    ASSERT(nw == nh, "the strlen wrapper stops at the interior NUL, by design");
    PASS("T-N3 an interior NUL is data under _n and a terminator under the wrapper");
}

TEST(test_encode_n_zero_len)
{
    int ids[8];
    ASSERT(tokenizer_encode_n(&tok, "hello", 0, ids, 8) == 0, "text_len 0 must return 0");
    ASSERT(tokenizer_encode_n(&tok, "hello", -1, ids, 8) == 0, "a negative text_len must return 0");
    ASSERT(tokenizer_encode_n(&tok, "hello", 5, ids, 0) == 0, "max_tokens 0 must return 0");
    PASS("T-N4 zero and negative lengths return 0");
}

int main(void)
{
    printf("=== BPE Tokenizer Tests ===\n");
    setup();

    test_encode_single_char();
    test_encode_he();
    test_encode_hello();
    test_encode_world();
    test_encode_hello_world();
    test_encode_hell();
    test_encode_unknown_char();
    test_decode_hello();
    test_decode_hello_world();
    test_decode_empty();
    test_find();
    test_encode_truncated();
    test_encode_n_prefix();
    test_encode_n_wrapper_equivalence();
    test_encode_n_interior_nul();
    test_encode_n_zero_len();

    teardown();

    printf("\n=== Results: %d PASS, %d FAIL out of %d ===\n",
           tests_passed, tests_failed, tests_passed + tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
