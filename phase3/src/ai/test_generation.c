/*
 * JARVIS AI-OS Phase 3 — Comprehensive Generation Quality Test
 *
 * Tests that the Llama 3.2 1B model produces coherent, diverse output
 * across multiple prompts using the F32 inference path.
 *
 * Requires: phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf (771 MB)
 * RAM usage: ~6 GB (F32 dequantized model) + vocab + state
 * Runtime:  ~30-60s model load, ~5-15s per generation
 *
 * Build:
 *   gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
 *     phase3/src/ai/test_generation.c phase3/src/ai/llama_forward.c \
 *     phase3/src/ai/llama_load.c phase3/src/ai/tensor_ops.c \
 *     phase3/src/ai/dequant.c phase3/src/ai/sampling.c \
 *     phase3/src/ai/gguf_parser.c phase3/src/ai/tokenizer.c \
 *     phase3/src/ai/gguf_vocab.c \
 *     -lm -o /tmp/test_generation
 *   /tmp/test_generation
 */

#include "llama_model.h"
#include "gguf_parser.h"
#include "gguf_vocab.h"
#include "tokenizer.h"
#include "sampling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MODEL_PATH "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"
#define BOS_TOKEN  128000
#define EOS_TOKEN  128001
#define MAX_GEN    64
#define MAX_PROMPT 64

/* ---- Test counters ---- */

static int tests_passed = 0;
static int tests_failed = 0;
static int tests_total  = 0;

static void test_pass(const char *name)
{
    tests_passed++;
    tests_total++;
    printf("  PASS: %s\n", name);
}

static void test_fail(const char *name, const char *reason)
{
    tests_failed++;
    tests_total++;
    printf("  FAIL: %s — %s\n", name, reason);
}

/* ---- Helpers ---- */

/* Check if output is coherent English: mostly printable ASCII with spaces */
static int is_coherent(const char *text, int len)
{
    if (len < 5) return 0;
    int spaces = 0, printable = 0;
    for (int i = 0; i < len; i++) {
        if (text[i] == ' ') spaces++;
        if (text[i] >= 0x20 && text[i] < 0x7F) printable++;
    }
    return (printable > len * 7 / 10) && (spaces > 0);
}

/* Check no single token repeats more than max_repeat times in a row */
static int no_excessive_repeats(const int *tokens, int n, int max_repeat)
{
    int run = 1;
    for (int i = 1; i < n; i++) {
        if (tokens[i] == tokens[i - 1]) {
            run++;
            if (run > max_repeat) return 0;
        } else {
            run = 1;
        }
    }
    return 1;
}

/* Case-insensitive substring search */
static int contains_ci(const char *haystack, const char *needle)
{
    int hlen = (int)strlen(haystack);
    int nlen = (int)strlen(needle);
    if (nlen > hlen) return 0;
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            if (tolower((unsigned char)haystack[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

/* Load entire file into malloc'd buffer */
static void *load_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) { fclose(f); return NULL; }

    void *buf = malloc((size_t)len);
    if (!buf) { fclose(f); return NULL; }

    size_t rd = fread(buf, 1, (size_t)len, f);
    fclose(f);

    if (rd != (size_t)len) { free(buf); return NULL; }

    *out_len = (size_t)len;
    return buf;
}

/* Reset inference state (zero KV cache, reset pos) */
static void reset_state(llama_state_t *state, const llama_config_t *config)
{
    state->pos = 0;
    size_t kv_size = (size_t)config->n_layers * (size_t)state->max_seq_len *
                     (size_t)config->n_kv_heads * (size_t)config->head_dim *
                     sizeof(float);
    memset(state->key_cache, 0, kv_size);
    memset(state->value_cache, 0, kv_size);
}

/* Encode a prompt string to tokens, prepending BOS */
static int encode_prompt(const tokenizer_t *tok, const char *text,
                         int *tokens, int max_tokens)
{
    tokens[0] = BOS_TOKEN;
    int n = tokenizer_encode(tok, text, tokens + 1, max_tokens - 1);
    if (n < 0) return -1;
    return n + 1; /* +1 for BOS */
}

/* Generate from a prompt string, return number of output tokens */
static int generate_from_prompt(const llama_model_t *model,
                                llama_state_t *state,
                                const tokenizer_t *tok,
                                const char *prompt_text,
                                int max_output,
                                int *prompt_tokens, int *n_prompt_out,
                                int *output_tokens,
                                char *decoded_text, int decoded_max)
{
    /* Reset state for fresh generation */
    reset_state(state, &model->config);

    /* Encode prompt */
    int n_prompt = encode_prompt(tok, prompt_text, prompt_tokens, MAX_PROMPT);
    if (n_prompt < 0) return -1;
    *n_prompt_out = n_prompt;

    /* Generate */
    int n_gen = llama_generate(model, state, prompt_tokens, n_prompt,
                               output_tokens, max_output,
                               EOS_TOKEN, 0.0f, 1, 0);

    /* Decode output to text */
    if (n_gen > 0) {
        tokenizer_decode(tok, output_tokens, n_gen, decoded_text, decoded_max);
    } else {
        decoded_text[0] = '\0';
    }

    return n_gen;
}

/* ---- Main ---- */

int main(void)
{
    printf("=== JARVIS Generation Quality Test ===\n\n");

    /* ----------------------------------------------------------------
     * Step 0: Check model file exists (CI-compatible skip)
     * ---------------------------------------------------------------- */
    {
        FILE *f = fopen(MODEL_PATH, "rb");
        if (!f) {
            printf("SKIP: model not found (%s)\n", MODEL_PATH);
            return 0;
        }
        fclose(f);
    }

    /* ----------------------------------------------------------------
     * Step 1: Load F32 model (~30-60s)
     * ---------------------------------------------------------------- */
    printf("[1/4] Loading F32 model (this takes ~30-60 seconds)...\n");
    fflush(stdout);

    llama_model_t model;
    int err = llama_load_model(&model, MODEL_PATH);
    if (err) {
        fprintf(stderr, "FATAL: llama_load_model failed (%d)\n", err);
        return 1;
    }
    printf("       Model loaded: dim=%d, layers=%d, vocab=%d, %.1f GB\n",
           model.config.dim, model.config.n_layers,
           model.config.vocab_size,
           (double)model.total_bytes / (1024.0 * 1024.0 * 1024.0));
    fflush(stdout);

    /* ----------------------------------------------------------------
     * Step 2: Extract vocab and initialize tokenizer
     * ---------------------------------------------------------------- */
    printf("[2/4] Extracting vocab and initializing tokenizer...\n");
    fflush(stdout);

    size_t file_len = 0;
    void *file_buf = load_file(MODEL_PATH, &file_len);
    if (!file_buf) {
        fprintf(stderr, "FATAL: could not read model file for vocab extraction\n");
        llama_free_model(&model);
        return 1;
    }

    gguf_vocab_t vocab;
    err = gguf_vocab_extract(file_buf, file_len, &vocab);
    if (err) {
        fprintf(stderr, "FATAL: gguf_vocab_extract failed (%d)\n", err);
        free(file_buf);
        llama_free_model(&model);
        return 1;
    }
    printf("       Vocab: %d tokens, BOS=%d, EOS=%d\n",
           vocab.vocab_size, vocab.bos_id, vocab.eos_id);

    tokenizer_t tok;
    err = gguf_vocab_init_tokenizer(&vocab, &tok);
    if (err) {
        fprintf(stderr, "FATAL: gguf_vocab_init_tokenizer failed (%d)\n", err);
        gguf_vocab_free(&vocab);
        free(file_buf);
        llama_free_model(&model);
        return 1;
    }
    printf("       Tokenizer initialized.\n");
    fflush(stdout);

    /* Free file buffer -- no longer needed after vocab extraction */
    free(file_buf);
    file_buf = NULL;

    /* ----------------------------------------------------------------
     * Step 3: Allocate inference state
     * ---------------------------------------------------------------- */
    printf("[3/4] Allocating inference state...\n");
    fflush(stdout);

    llama_state_t state;
    err = llama_alloc_state(&state, &model.config);
    if (err) {
        fprintf(stderr, "FATAL: llama_alloc_state failed (%d)\n", err);
        tokenizer_free(&tok);
        gguf_vocab_free(&vocab);
        llama_free_model(&model);
        return 1;
    }

    /* Buffers for generation */
    int prompt_tokens[MAX_PROMPT];
    int output_tokens[MAX_GEN];
    char decoded[4096];
    int n_prompt, n_gen;

    /* ----------------------------------------------------------------
     * Step 4: Run 6 generation tests
     * ---------------------------------------------------------------- */
    printf("[4/4] Running 6 generation tests...\n\n");
    fflush(stdout);

    /* ---- Test 1: "The Linux kernel is" — coherent, >10 tokens ---- */
    {
        const char *prompt = "The Linux kernel is";
        printf("Test 1: \"%s\" — expect coherent output, >10 tokens\n", prompt);
        fflush(stdout);

        n_gen = generate_from_prompt(&model, &state, &tok, prompt, 30,
                                     prompt_tokens, &n_prompt,
                                     output_tokens, decoded, sizeof(decoded));

        printf("       Prompt tokens: %d, Generated: %d\n", n_prompt, n_gen);
        printf("       Output: \"%s\"\n", decoded);

        if (n_gen > 10 && is_coherent(decoded, (int)strlen(decoded))) {
            test_pass("Linux kernel — coherent, >10 tokens");
        } else if (n_gen <= 10) {
            test_fail("Linux kernel — coherent, >10 tokens",
                      "generated <= 10 tokens");
        } else {
            test_fail("Linux kernel — coherent, >10 tokens",
                      "output not coherent");
        }
        printf("\n");
        fflush(stdout);
    }

    /* ---- Test 2: "The capital of France is" — contains "Paris" ---- */
    {
        const char *prompt = "The capital of France is";
        printf("Test 2: \"%s\" — expect contains \"Paris\"\n", prompt);
        fflush(stdout);

        n_gen = generate_from_prompt(&model, &state, &tok, prompt, 30,
                                     prompt_tokens, &n_prompt,
                                     output_tokens, decoded, sizeof(decoded));

        printf("       Prompt tokens: %d, Generated: %d\n", n_prompt, n_gen);
        printf("       Output: \"%s\"\n", decoded);

        if (n_gen > 0 && contains_ci(decoded, "Paris")) {
            test_pass("Capital of France — contains Paris");
        } else {
            test_fail("Capital of France — contains Paris",
                      "output does not contain 'Paris'");
        }
        printf("\n");
        fflush(stdout);
    }

    /* ---- Test 3: "A function to sort" — coherent ---- */
    {
        const char *prompt = "A function to sort";
        printf("Test 3: \"%s\" — expect coherent output\n", prompt);
        fflush(stdout);

        n_gen = generate_from_prompt(&model, &state, &tok, prompt, 30,
                                     prompt_tokens, &n_prompt,
                                     output_tokens, decoded, sizeof(decoded));

        printf("       Prompt tokens: %d, Generated: %d\n", n_prompt, n_gen);
        printf("       Output: \"%s\"\n", decoded);

        if (n_gen > 0 && is_coherent(decoded, (int)strlen(decoded))) {
            test_pass("Function to sort — coherent");
        } else if (n_gen == 0) {
            test_fail("Function to sort — coherent", "generated 0 tokens");
        } else {
            test_fail("Function to sort — coherent", "output not coherent");
        }
        printf("\n");
        fflush(stdout);
    }

    /* ---- Test 4: "Once upon a time" (50 tokens) — no excessive repeats ---- */
    {
        const char *prompt = "Once upon a time";
        printf("Test 4: \"%s\" (50 tokens) — expect no token repeats >5x\n", prompt);
        fflush(stdout);

        n_gen = generate_from_prompt(&model, &state, &tok, prompt, 50,
                                     prompt_tokens, &n_prompt,
                                     output_tokens, decoded, sizeof(decoded));

        printf("       Prompt tokens: %d, Generated: %d\n", n_prompt, n_gen);
        printf("       Output: \"%s\"\n", decoded);

        if (n_gen > 0 && no_excessive_repeats(output_tokens, n_gen, 5)) {
            test_pass("Once upon a time — no excessive repeats");
        } else if (n_gen == 0) {
            test_fail("Once upon a time — no excessive repeats",
                      "generated 0 tokens");
        } else {
            test_fail("Once upon a time — no excessive repeats",
                      "single token repeated >5x consecutively");
        }
        printf("\n");
        fflush(stdout);
    }

    /* ---- Test 5: "Hello" — generates >0 tokens without crash ---- */
    {
        const char *prompt = "Hello";
        printf("Test 5: \"%s\" — expect >0 tokens without crash\n", prompt);
        fflush(stdout);

        n_gen = generate_from_prompt(&model, &state, &tok, prompt, 20,
                                     prompt_tokens, &n_prompt,
                                     output_tokens, decoded, sizeof(decoded));

        printf("       Prompt tokens: %d, Generated: %d\n", n_prompt, n_gen);
        printf("       Output: \"%s\"\n", decoded);

        if (n_gen > 0) {
            test_pass("Hello — generates >0 tokens");
        } else {
            test_fail("Hello — generates >0 tokens", "generated 0 tokens");
        }
        printf("\n");
        fflush(stdout);
    }

    /* ---- Test 6: BOS logit check — argmax after BOS token ---- */
    {
        printf("Test 6: BOS logit check — process token %d, verify argmax\n", BOS_TOKEN);
        fflush(stdout);

        reset_state(&state, &model.config);
        llama_forward(&model, &state, BOS_TOKEN);

        int am = sample_greedy(state.logits, model.config.vocab_size);
        printf("       Argmax after BOS: %d (logit=%.4f)\n",
               am, state.logits[am]);

        /* Print top-5 for diagnostic */
        printf("       Top-5 logits:");
        for (int rank = 0; rank < 5; rank++) {
            int best = -1;
            float best_val = -1e30f;
            for (int i = 0; i < model.config.vocab_size; i++) {
                /* Skip already-found tokens */
                int skip = 0;
                for (int r2 = 0; r2 < rank; r2++) {
                    /* Re-find the r2-th best to skip it */
                    (void)r2; /* we use a simpler approach below */
                }
                (void)skip;
                if (state.logits[i] > best_val) {
                    best_val = state.logits[i];
                    best = i;
                }
            }
            /* Zero out to find next */
            if (best >= 0) {
                printf(" [%d]=%.2f", best, best_val);
                state.logits[best] = -1e30f;
            }
        }
        printf("\n");

        /* Expected argmax candidates after BOS for Llama 3.2 1B Instruct:
         * Common first tokens: 16309, 2, 1757, 791, 1340, 475
         * (model-dependent, but should be one of these) */
        int expected[] = {16309, 2, 1757, 791, 1340, 475};
        int n_expected = (int)(sizeof(expected) / sizeof(expected[0]));
        int found = 0;
        for (int i = 0; i < n_expected; i++) {
            if (am == expected[i]) { found = 1; break; }
        }

        if (found) {
            test_pass("BOS logit check — argmax in expected set");
        } else {
            char reason[128];
            snprintf(reason, sizeof(reason),
                     "argmax=%d not in expected set {16309,2,1757,791,1340,475}",
                     am);
            test_fail("BOS logit check — argmax in expected set", reason);
        }
        printf("\n");
        fflush(stdout);
    }

    /* ----------------------------------------------------------------
     * Summary
     * ---------------------------------------------------------------- */
    printf("=== Generation Quality Test Summary ===\n");
    printf("  %d/%d tests passed", tests_passed, tests_total);
    if (tests_failed > 0) {
        printf(", %d FAILED", tests_failed);
    }
    printf("\n\n");

    /* Cleanup */
    llama_free_state(&state);
    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    llama_free_model(&model);

    return tests_failed > 0 ? 1 : 0;
}
