/*
 * JARVIS AI-OS Phase 3 -- Inference Benchmark
 *
 * Standalone benchmark measuring tokens/second for the JARVIS custom
 * C inference engine (F32 dequant path). Not a test -- no CI step.
 *
 * Compile:
 *   gcc -Wall -O2 -std=c11 -D_POSIX_C_SOURCE=200809L -I phase3/src/ai \
 *     phase3/src/ai/bench_inference.c phase3/src/ai/llama_forward.c \
 *     phase3/src/ai/llama_load.c phase3/src/ai/tensor_ops.c \
 *     phase3/src/ai/dequant.c phase3/src/ai/sampling.c \
 *     phase3/src/ai/gguf_parser.c phase3/src/ai/tokenizer.c \
 *     phase3/src/ai/gguf_vocab.c \
 *     -lm -o /tmp/bench_inference
 *
 * Run:
 *   /tmp/bench_inference
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "llama_model.h"
#include "gguf_parser.h"
#include "gguf_vocab.h"
#include "tokenizer.h"
#include "sampling.h"

#define MODEL_PATH "phase3/models/Llama-3.2-1B-Instruct-Q4_K_M.gguf"
#define WARMUP_ITERS  3
#define GEN_TOKENS    50

static double time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(void)
{
    /* ---- Check model exists ---- */
    FILE *f = fopen(MODEL_PATH, "rb");
    if (!f) {
        printf("SKIP: model not found at %s\n", MODEL_PATH);
        return 0;
    }

    /* Read entire GGUF into memory for vocab extraction */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *gguf_data = malloc((size_t)file_size);
    if (!gguf_data) {
        fprintf(stderr, "ERROR: cannot allocate %ld bytes for GGUF data\n", file_size);
        fclose(f);
        return 1;
    }
    if (fread(gguf_data, 1, (size_t)file_size, f) != (size_t)file_size) {
        fprintf(stderr, "ERROR: failed to read model file\n");
        free(gguf_data);
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("JARVIS Inference Benchmark\n");
    printf("==========================\n");
    printf("Model file: %s (%ld MB)\n\n", MODEL_PATH, file_size / (1024 * 1024));

    /* ---- Load model (F32 dequant path) ---- */
    llama_model_t model;
    memset(&model, 0, sizeof(model));

    printf("Loading model (F32 dequant -- will allocate ~5-6 GB)...\n");
    double t0 = time_ms();
    int err = llama_load_model(&model, MODEL_PATH);
    double load_time = time_ms() - t0;

    if (err) {
        fprintf(stderr, "ERROR: llama_load_model failed (err=%d)\n", err);
        free(gguf_data);
        return 1;
    }
    printf("Model loaded in %.1f ms (%.2f GB allocated)\n",
           load_time, (double)model.total_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("Config: dim=%d, layers=%d, heads=%d, kv_heads=%d, vocab=%d, max_seq=%d\n\n",
           model.config.dim, model.config.n_layers,
           model.config.n_heads, model.config.n_kv_heads,
           model.config.vocab_size, model.config.max_seq_len);

    /* ---- Extract vocab and init tokenizer ---- */
    printf("Extracting vocabulary...\n");
    gguf_vocab_t vocab;
    err = gguf_vocab_extract(gguf_data, (size_t)file_size, &vocab);
    if (err) {
        fprintf(stderr, "ERROR: gguf_vocab_extract failed\n");
        llama_free_model(&model);
        free(gguf_data);
        return 1;
    }
    printf("Vocab: %d tokens, BOS=%d, EOS=%d\n", vocab.vocab_size, vocab.bos_id, vocab.eos_id);

    tokenizer_t tok;
    err = gguf_vocab_init_tokenizer(&vocab, &tok);
    if (err) {
        fprintf(stderr, "ERROR: gguf_vocab_init_tokenizer failed\n");
        gguf_vocab_free(&vocab);
        llama_free_model(&model);
        free(gguf_data);
        return 1;
    }
    printf("Tokenizer initialized\n\n");

    /* Done with raw GGUF data */
    free(gguf_data);
    gguf_data = NULL;

    /* ---- Allocate inference state ---- */
    llama_state_t state;
    err = llama_alloc_state(&state, &model.config);
    if (err) {
        fprintf(stderr, "ERROR: llama_alloc_state failed\n");
        tokenizer_free(&tok);
        gguf_vocab_free(&vocab);
        llama_free_model(&model);
        return 1;
    }

    /* ---- Prepare prompt ---- */
    const char *prompt_text = "The seL4 microkernel is";
    int prompt_ids[64];
    prompt_ids[0] = vocab.bos_id;
    int n_enc = tokenizer_encode(&tok, prompt_text, prompt_ids + 1, 63);
    int n_prompt = 1 + n_enc;
    printf("Prompt: BOS + \"%s\" = %d tokens [", prompt_text, n_prompt);
    for (int i = 0; i < n_prompt; i++) {
        printf("%d%s", prompt_ids[i], i < n_prompt - 1 ? ", " : "");
    }
    printf("]\n\n");

    /* KV cache sizes for clearing */
    size_t kv_size = (size_t)model.config.n_layers * (size_t)state.max_seq_len *
                     (size_t)model.config.n_kv_heads * (size_t)model.config.head_dim *
                     sizeof(float);

    /* ---- Warm-up: 3 forward passes ---- */
    printf("Warming up (%d forward passes)...\n", WARMUP_ITERS);
    for (int w = 0; w < WARMUP_ITERS; w++) {
        state.pos = 0;
        memset(state.key_cache, 0, kv_size);
        memset(state.value_cache, 0, kv_size);
        llama_forward(&model, &state, prompt_ids[0]);
        printf("  warm-up %d/%d done\n", w + 1, WARMUP_ITERS);
    }
    printf("Warm-up complete\n\n");

    /* ---- Benchmark: Prefill (process all prompt tokens) ---- */
    printf("Benchmarking prefill (%d tokens)...\n", n_prompt);
    state.pos = 0;
    memset(state.key_cache, 0, kv_size);
    memset(state.value_cache, 0, kv_size);

    t0 = time_ms();
    for (int i = 0; i < n_prompt; i++) {
        llama_forward(&model, &state, prompt_ids[i]);
    }
    double prefill_ms = time_ms() - t0;
    printf("Prefill done: %d tokens in %.1f ms\n\n", n_prompt, prefill_ms);

    /* ---- Benchmark: Generation (50 tokens) ---- */
    printf("Benchmarking generation (%d tokens)...\n", GEN_TOKENS);
    int gen_tokens[GEN_TOKENS];

    t0 = time_ms();
    for (int g = 0; g < GEN_TOKENS; g++) {
        int next = sample_greedy(state.logits, model.config.vocab_size);
        gen_tokens[g] = next;
        if (next == vocab.eos_id) {
            /* EOS hit early -- adjust count */
            printf("  EOS at token %d\n", g + 1);
            /* Still measure remaining to fill GEN_TOKENS worth of time */
        }
        llama_forward(&model, &state, next);
        if ((g + 1) % 10 == 0) {
            double elapsed = time_ms() - t0;
            printf("  %d/%d tokens (%.1f ms elapsed, ~%.2f tok/s so far)\n",
                   g + 1, GEN_TOKENS, elapsed, (g + 1) * 1000.0 / elapsed);
        }
    }
    double gen_ms = time_ms() - t0;

    /* ---- Decode generated text ---- */
    char text[2048];
    tokenizer_decode(&tok, gen_tokens, GEN_TOKENS, text, sizeof(text));

    /* ---- Results ---- */
    double prefill_tps = n_prompt * 1000.0 / prefill_ms;
    double gen_tps = GEN_TOKENS * 1000.0 / gen_ms;

    printf("\n");
    printf("============================================\n");
    printf("   JARVIS Inference Benchmark Results\n");
    printf("============================================\n");
    printf("\n");
    printf("Model:     Llama 3.2 1B Instruct Q4_K_M (F32 dequant)\n");
    printf("Platform:  x86-64 host (Ryzen 7 2700X, 32GB DDR4)\n");
    printf("Engine:    JARVIS custom C inference (naive matmul, no SIMD)\n");
    printf("Prompt:    BOS + '%s' (%d tokens)\n", prompt_text, n_prompt);
    printf("\n");
    printf("Load time:   %.1f ms (%.1f s)\n", load_time, load_time / 1000.0);
    printf("Memory:      %.2f GB (F32 dequantized weights)\n",
           (double)model.total_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("\n");
    printf("Prefill:     %d tokens in %.1f ms  =>  %.2f tok/s\n",
           n_prompt, prefill_ms, prefill_tps);
    printf("Generation:  %d tokens in %.1f ms  =>  %.2f tok/s\n",
           GEN_TOKENS, gen_ms, gen_tps);
    printf("Per-token:   %.1f ms/tok (generation)\n", gen_ms / GEN_TOKENS);
    printf("\n");
    printf("Output text:\n  \"%s\"\n", text);
    printf("\n");
    printf("============================================\n");

    /* ---- Comparison ---- */
    printf("\nComparison with llama.cpp (same model, same CPU):\n");
    printf("  llama.cpp CPU:    44.0 tok/s generation (AVX2 + quantized matmul)\n");
    printf("  llama.cpp GPU:   273.3 tok/s generation (RTX 2070 CUDA)\n");
    printf("  JARVIS custom:   %.2f tok/s generation (naive F32, no SIMD)\n", gen_tps);
    printf("  Ratio vs CPU:    %.1fx slower than llama.cpp CPU\n", 44.0 / gen_tps);
    printf("  Ratio vs GPU:    %.1fx slower than llama.cpp GPU\n", 273.3 / gen_tps);
    printf("\n");

    /* ---- Cleanup ---- */
    llama_free_state(&state);
    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    llama_free_model(&model);

    printf("Benchmark complete.\n");
    return 0;
}
