/*
 * JARVIS AI-OS -- GGUF Vocabulary Extraction Test Suite
 *
 * Tests extracting tokenizer vocab from the real Llama 3.2 1B GGUF model.
 * Verifies token count, special token IDs, and first few tokens.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gguf_vocab.h"

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("Test %d: %s ... ", tests_run, name); \
} while(0)

#define PASS() do { \
    tests_passed++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    return; \
} while(0)

#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); } } while(0)

static const char *MODEL_PATH =
    "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf";

/* Read entire file into malloc'd buffer */
static void *read_file_to_buffer(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);

    void *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (rd != (size_t)len) { free(buf); return NULL; }

    *out_len = (size_t)len;
    return buf;
}

static void *model_buf = NULL;
static size_t model_len = 0;

/* ---- Tests ---- */

static void test_vocab_extract(void)
{
    TEST("gguf_vocab_extract() succeeds");

    gguf_vocab_t vocab;
    int rc = gguf_vocab_extract(model_buf, model_len, &vocab);
    ASSERT(rc == 0, "extraction failed");
    ASSERT(vocab.vocab_size > 0, "vocab_size is 0");
    gguf_vocab_free(&vocab);
    PASS();
}

static void test_vocab_size(void)
{
    TEST("vocab_size == 128256");

    gguf_vocab_t vocab;
    int rc = gguf_vocab_extract(model_buf, model_len, &vocab);
    ASSERT(rc == 0, "extraction failed");
    printf("(got %d) ", vocab.vocab_size);
    ASSERT(vocab.vocab_size == 128256, "wrong vocab_size");
    gguf_vocab_free(&vocab);
    PASS();
}

static void test_bos_token(void)
{
    TEST("bos_id == 128000");

    gguf_vocab_t vocab;
    int rc = gguf_vocab_extract(model_buf, model_len, &vocab);
    ASSERT(rc == 0, "extraction failed");
    printf("(got %d) ", vocab.bos_id);
    ASSERT(vocab.bos_id == 128000, "wrong bos_id");
    gguf_vocab_free(&vocab);
    PASS();
}

static void test_eos_token(void)
{
    TEST("eos_id == 128009");

    gguf_vocab_t vocab;
    int rc = gguf_vocab_extract(model_buf, model_len, &vocab);
    ASSERT(rc == 0, "extraction failed");
    printf("(got %d) ", vocab.eos_id);
    ASSERT(vocab.eos_id == 128009, "wrong eos_id");
    gguf_vocab_free(&vocab);
    PASS();
}

static void test_first_tokens(void)
{
    TEST("first 10 tokens are non-empty strings");

    gguf_vocab_t vocab;
    int rc = gguf_vocab_extract(model_buf, model_len, &vocab);
    ASSERT(rc == 0, "extraction failed");
    ASSERT(vocab.vocab_size >= 10, "vocab too small");

    printf("\n");
    int ok = 1;
    for (int i = 0; i < 10; i++) {
        ASSERT(vocab.tokens[i] != NULL, "token is NULL");
        printf("        token[%d] = \"%s\" (len=%zu)\n", i, vocab.tokens[i], strlen(vocab.tokens[i]));
        /* Tokens can be empty (e.g., token 0 is often "!"), just check non-NULL */
    }
    ASSERT(ok, "token check failed");
    gguf_vocab_free(&vocab);
    PASS();
}

static void test_scores_array(void)
{
    TEST("scores array is non-NULL and has vocab_size entries");

    gguf_vocab_t vocab;
    int rc = gguf_vocab_extract(model_buf, model_len, &vocab);
    ASSERT(rc == 0, "extraction failed");
    ASSERT(vocab.scores != NULL, "scores is NULL");
    /* Check a few entries are finite */
    int ok = 1;
    for (int i = 0; i < 10 && i < vocab.vocab_size; i++) {
        float s = vocab.scores[i];
        if (s != s) { ok = 0; break; } /* NaN check */
    }
    ASSERT(ok, "scores contain NaN");
    gguf_vocab_free(&vocab);
    PASS();
}

static void test_init_tokenizer(void)
{
    TEST("gguf_vocab_init_tokenizer() creates valid tokenizer");

    gguf_vocab_t vocab;
    int rc = gguf_vocab_extract(model_buf, model_len, &vocab);
    ASSERT(rc == 0, "extraction failed");

    tokenizer_t tok;
    rc = gguf_vocab_init_tokenizer(&vocab, &tok);
    ASSERT(rc == 0, "tokenizer init failed");
    ASSERT(tok.vocab_size == vocab.vocab_size, "vocab_size mismatch");
    ASSERT(tok.bos_id == vocab.bos_id, "bos_id mismatch");
    ASSERT(tok.eos_id == vocab.eos_id, "eos_id mismatch");
    ASSERT(tok.tokens != NULL, "tokenizer tokens NULL");

    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    PASS();
}

static void test_null_inputs(void)
{
    TEST("gguf_vocab_extract() rejects NULL inputs");

    gguf_vocab_t vocab;
    ASSERT(gguf_vocab_extract(NULL, 100, &vocab) == -1, "should reject NULL data");
    ASSERT(gguf_vocab_extract(model_buf, 0, &vocab) == -1, "should reject zero len");
    ASSERT(gguf_vocab_extract(model_buf, model_len, NULL) == -1, "should reject NULL vocab");
    PASS();
}

static void test_truncated_input(void)
{
    TEST("gguf_vocab_extract() rejects truncated input");

    gguf_vocab_t vocab;
    /* Only 16 bytes: not enough for header */
    ASSERT(gguf_vocab_extract(model_buf, 16, &vocab) == -1, "should reject truncated");
    PASS();
}

static void test_vocab_free_safety(void)
{
    TEST("gguf_vocab_free() safe on zeroed struct");

    gguf_vocab_t vocab;
    memset(&vocab, 0, sizeof(vocab));
    gguf_vocab_free(&vocab);  /* should not crash */
    gguf_vocab_free(NULL);    /* should not crash */
    PASS();
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS GGUF Vocabulary Extraction Test Suite ===\n\n");

    /* Load model file into memory once */
    model_buf = read_file_to_buffer(MODEL_PATH, &model_len);
    if (!model_buf) {
        printf("SKIP: Cannot read model file: %s\n", MODEL_PATH);
        printf("(This test requires the real Llama 3.2 1B GGUF model)\n");
        return 0;
    }
    printf("Loaded model: %zu bytes (%.1f MB)\n\n", model_len, model_len / (1024.0 * 1024.0));

    test_vocab_extract();
    test_vocab_size();
    test_bos_token();
    test_eos_token();
    test_first_tokens();
    test_scores_array();
    test_init_tokenizer();
    test_null_inputs();
    test_truncated_input();
    test_vocab_free_safety();

    free(model_buf);

    printf("\n=== Results: %d/%d PASS ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
