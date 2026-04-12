/*
 * JARVIS AI-OS Phase 3 -- Quantized Inference Engine Benchmark
 *
 * Standalone speed benchmark for GGUF models using the zero-copy
 * quantized inference path (qmodel_forward). Single prompt, 128-token
 * generation — matches llama-bench pp/tg methodology for fair comparison.
 *
 * Compile:
 *   gcc -O2 -mavx2 -mfma -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *       -I phase3/src/ai \
 *       phase3/src/ai/bench_engine.c phase3/src/ai/llama_quant.c \
 *       phase3/src/ai/llama_load.c phase3/src/ai/llama_forward.c \
 *       phase3/src/ai/gguf_parser.c phase3/src/ai/gguf_vocab.c \
 *       phase3/src/ai/tokenizer.c phase3/src/ai/tensor_ops.c \
 *       phase3/src/ai/dequant.c phase3/src/ai/sampling.c \
 *       phase3/src/ai/inference.c \
 *       -lm -o /tmp/bench_engine
 *
 * Run:
 *   /tmp/bench_engine path/to/model.gguf [--tokens 128]
 *
 * Pure C11, no C++ dependencies.
 */

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "llama_model.h"
#include "llama_quant.h"
#include "gguf_parser.h"
#include "gguf_vocab.h"
#include "tokenizer.h"
#include "sampling.h"

/* ---- Helpers ---- */

static double time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

#ifdef __linux__
static long peak_rss_kb(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long vmhwm = -1;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmHWM:", 6) == 0) {
            vmhwm = atol(line + 6);
            break;
        }
    }
    fclose(f);
    return vmhwm;
}
#endif

/* ---- Gemma 4 chat template token IDs (from inference_server.c) ----
 *   <bos> <|turn> user \n {text} <turn|> \n <|turn> model \n <|think|>
 *   bos=2, <|turn>=105, user=2364, \n=107, <turn|>=106, model=4368, <|think|>=98
 *   Stop on <eos>=1 (NOT eos_id=106 which is <turn|>).
 */
#define GEMMA_TURN_OPEN   105
#define GEMMA_USER        2364
#define GEMMA_NEWLINE     107
#define GEMMA_TURN_CLOSE  106
#define GEMMA_MODEL       4368
#define GEMMA_THINK       98
#define GEMMA_EOS_STOP    1

static const char *bench_prompt = "The seL4 microkernel is";

/* Build prompt token sequence. Returns total token count.
 * Detects architecture from arch string and wraps with appropriate
 * chat template for instruct-tuned models. */
static int build_prompt(const tokenizer_t *tok, const llama_config_t *cfg,
                        const char *arch, const char *text, int *ids, int max_ids)
{
    int n = 0;

    /* Universal newline token — tokenizer_encode can't encode \n for GPT-2 BPE
     * models (byte 0x0A isn't in the merge table). Look up directly instead. */
    int nl_tok = tokenizer_find(tok, "\n");

    if (cfg->embed_scale) {
        /* Gemma architecture: chat template wrapping */
        ids[n++] = tok->bos_id;
        ids[n++] = GEMMA_TURN_OPEN;
        ids[n++] = GEMMA_USER;
        ids[n++] = GEMMA_NEWLINE;

        int enc = tokenizer_encode(tok, text, ids + n, max_ids - n - 6);
        n += enc;

        ids[n++] = GEMMA_TURN_CLOSE;
        ids[n++] = GEMMA_NEWLINE;
        ids[n++] = GEMMA_TURN_OPEN;
        ids[n++] = GEMMA_MODEL;
        ids[n++] = GEMMA_NEWLINE;
        ids[n++] = GEMMA_THINK;
    } else if (arch && strcmp(arch, "phi3") == 0) {
        /* Phi-3: <|user|>\n{text}<|end|>\n<|assistant|>\n */
        int user_tok = tokenizer_find(tok, "<|user|>");
        int end_tok  = tokenizer_find(tok, "<|end|>");
        int asst_tok = tokenizer_find(tok, "<|assistant|>");
        if (user_tok >= 0 && end_tok >= 0 && asst_tok >= 0) {
            ids[n++] = tok->bos_id;
            ids[n++] = user_tok;
            if (nl_tok >= 0) ids[n++] = nl_tok;
            n += tokenizer_encode(tok, text, ids + n, max_ids - n - 6);
            ids[n++] = end_tok;
            if (nl_tok >= 0) ids[n++] = nl_tok;
            ids[n++] = asst_tok;
            if (nl_tok >= 0) ids[n++] = nl_tok;
            printf("  [phi3 chat template: %d tokens]\n", n);
        } else {
            printf("  [phi3 special tokens not found, using raw prompt]\n");
            ids[n++] = tok->bos_id;
            n += tokenizer_encode(tok, text, ids + n, max_ids - n);
        }
    } else if (arch && (strcmp(arch, "qwen3") == 0 || strcmp(arch, "qwen35") == 0)) {
        /* Qwen: <|im_start|>user\n{text}<|im_end|>\n<|im_start|>assistant\n */
        int im_start = tokenizer_find(tok, "<|im_start|>");
        int im_end   = tokenizer_find(tok, "<|im_end|>");
        if (im_start >= 0 && im_end >= 0) {
            ids[n++] = im_start;
            n += tokenizer_encode(tok, "user", ids + n, max_ids - n - 10);
            if (nl_tok >= 0) ids[n++] = nl_tok;
            n += tokenizer_encode(tok, text, ids + n, max_ids - n - 6);
            ids[n++] = im_end;
            if (nl_tok >= 0) ids[n++] = nl_tok;
            ids[n++] = im_start;
            n += tokenizer_encode(tok, "assistant", ids + n, max_ids - n - 2);
            if (nl_tok >= 0) ids[n++] = nl_tok;
            printf("  [qwen chat template: %d tokens]\n", n);
        } else {
            printf("  [qwen special tokens not found, using raw prompt]\n");
            ids[n++] = tok->bos_id;
            n += tokenizer_encode(tok, text, ids + n, max_ids - n);
        }
    } else {
        /* Llama / generic: BOS + encoded text */
        ids[n++] = tok->bos_id;
        n += tokenizer_encode(tok, text, ids + n, max_ids - n);
    }
    return n;
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    /* Parse CLI */
    const char *model_path = NULL;
    int max_tokens = 128;
    int debug = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0 && i + 1 < argc) {
            max_tokens = atoi(argv[++i]);
            if (max_tokens < 1) max_tokens = 1;
            if (max_tokens > 512) max_tokens = 512;
        } else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug = 1;
        } else if (argv[i][0] != '-') {
            model_path = argv[i];
        }
    }

    if (!model_path) {
        fprintf(stderr, "Usage: bench_engine <model.gguf> [--tokens N] [--debug]\n");
        return 1;
    }

    /* ---- Read entire GGUF into memory ---- */
    FILE *f = fopen(model_path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open %s\n", model_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    printf("JARVIS Quantized Engine Benchmark\n");
    printf("==================================\n");
    printf("Model: %s (%ld MB)\n", model_path, file_size / (1024 * 1024));
    printf("Tokens: %d\n\n", max_tokens);
    fflush(stdout);

    void *gguf_data = malloc((size_t)file_size);
    if (!gguf_data) {
        fprintf(stderr, "ERROR: cannot allocate %ld bytes\n", file_size);
        fclose(f);
        return 1;
    }
    printf("  Reading %.2f GB...\n", file_size / (1024.0 * 1024 * 1024)); fflush(stdout);
    double t_read = time_ms();
    if (fread(gguf_data, 1, (size_t)file_size, f) != (size_t)file_size) {
        fprintf(stderr, "ERROR: read failed\n");
        free(gguf_data);
        fclose(f);
        return 1;
    }
    fclose(f);
    printf("  File read done (%.1fs)\n", (time_ms() - t_read) / 1000.0); fflush(stdout);

    /* ---- Parse GGUF ---- */
    gguf_ctx_t ctx;
    double t_load_start = time_ms();

    printf("  Parsing GGUF...\n"); fflush(stdout);
    int err = gguf_open_memory(&ctx, gguf_data, (size_t)file_size);
    if (err) {
        fprintf(stderr, "ERROR: gguf_open_memory failed (%s)\n", gguf_strerror(err));
        free(gguf_data);
        return 1;
    }

    /* ---- Load quantized model ---- */
    printf("  Loading quantized model...\n"); fflush(stdout);
    qmodel_t qm;
    err = qmodel_load(&qm, &ctx, gguf_data);
    if (err) {
        const char *arch = gguf_get_kv_string(&ctx, "general.architecture");
        fprintf(stderr, "ERROR: qmodel_load failed (err=%d)", err);
        if (arch)
            fprintf(stderr, " [arch=%s]", arch);
        fprintf(stderr, "\n");
        gguf_close(&ctx);
        free(gguf_data);
        return 1;
    }

    /* ---- Extract vocab + init tokenizer ---- */
    printf("  Extracting vocabulary...\n"); fflush(stdout);
    gguf_vocab_t vocab;
    err = gguf_vocab_extract(gguf_data, (size_t)file_size, &vocab);
    if (err) {
        fprintf(stderr, "ERROR: gguf_vocab_extract failed\n");
        qmodel_free(&qm);
        gguf_close(&ctx);
        free(gguf_data);
        return 1;
    }

    tokenizer_t tok;
    err = gguf_vocab_init_tokenizer(&vocab, &tok);
    if (err) {
        fprintf(stderr, "ERROR: gguf_vocab_init_tokenizer failed\n");
        gguf_vocab_free(&vocab);
        qmodel_free(&qm);
        gguf_close(&ctx);
        free(gguf_data);
        return 1;
    }

    /* ---- Allocate inference state ---- */
    llama_state_t state;
    err = llama_alloc_state(&state, &qm.config);
    if (err) {
        fprintf(stderr, "ERROR: llama_alloc_state failed\n");
        tokenizer_free(&tok);
        gguf_vocab_free(&vocab);
        qmodel_free(&qm);
        gguf_close(&ctx);
        free(gguf_data);
        return 1;
    }

    double load_ms = time_ms() - t_load_start;

    const char *arch_str = gguf_get_kv_string(&ctx, "general.architecture");
    const char *model_name = gguf_get_kv_string(&ctx, "general.name");

    printf("Loaded in %.1f ms\n", load_ms);
    printf("  Name:   %s\n", model_name ? model_name : "(unknown)");
    printf("  Arch:   %s\n", arch_str ? arch_str : "(unknown)");
    printf("  Layers: %d, Dim: %d, Heads: %d, KV Heads: %d\n",
           qm.config.n_layers, qm.config.dim,
           qm.config.n_heads, qm.config.n_kv_heads);
    printf("  Vocab:  %d, BOS=%d, EOS=%d\n",
           qm.config.vocab_size, tok.bos_id, tok.eos_id);
    printf("  Gemma:  %s\n", qm.config.embed_scale ? "yes" : "no");
#ifdef __linux__
    long rss_after_load = peak_rss_kb();
    if (rss_after_load > 0)
        printf("  RSS:    %ld MB\n", rss_after_load / 1024);
#endif
    printf("\n");

    /* Determine stop token */
    int eos_token = qm.config.embed_scale ? GEMMA_EOS_STOP : tok.eos_id;

    /* KV cache size for reset */
    size_t kv_bytes = (size_t)qm.config.n_layers * (size_t)state.max_seq_len *
                      (size_t)qm.config.n_kv_heads * (size_t)qm.config.head_dim *
                      sizeof(float);

    /* ---- Build prompt ---- */
    int prompt_ids[256];
    int n_prompt = build_prompt(&tok, &qm.config, arch_str, bench_prompt, prompt_ids, 256);

    printf("--- \"%s\" ---\n", bench_prompt);
    fflush(stdout);

    /* Reset KV cache */
    state.pos = 0;
    memset(state.key_cache, 0, kv_bytes);
    memset(state.value_cache, 0, kv_bytes);

    if (debug) {
        printf("  Prompt IDs (%d):", n_prompt);
        for (int i = 0; i < n_prompt; i++) printf(" %d", prompt_ids[i]);
        printf("\n"); fflush(stdout);
    }

    /* ---- Prefill (timed) ---- */
    printf("  Prefill (%d tokens)...", n_prompt); fflush(stdout);
    double t0 = time_ms();
    for (int i = 0; i < n_prompt; i++)
        qmodel_forward(&qm, &state, prompt_ids[i]);
    double prefill_ms = time_ms() - t0;
    double pp_tps = (prefill_ms > 0.0) ? n_prompt * 1000.0 / prefill_ms : 0.0;
    printf(" %.1f tok/s\n", pp_tps); fflush(stdout);

    if (debug) {
        printf("  [POST-PREFILL] logits[0..4]: %.4f %.4f %.4f %.4f %.4f\n",
               state.logits[0], state.logits[1], state.logits[2],
               state.logits[3], state.logits[4]);
        int top[5] = {0,0,0,0,0};
        for (int i = 1; i < qm.config.vocab_size; i++)
            for (int j = 0; j < 5; j++)
                if (state.logits[i] > state.logits[top[j]]) {
                    for (int k = 4; k > j; k--) top[k] = top[k-1];
                    top[j] = i; break;
                }
        printf("  [TOP5] ");
        for (int j = 0; j < 5; j++) printf("%d(%.1f) ", top[j], state.logits[top[j]]);
        printf("\n  eos_token=%d\n", eos_token);
        fflush(stdout);
    }

    /* ---- Generation (timed) — stream each token ---- */
    int n_gen = 0;
    printf("  > ");

    t0 = time_ms();
    for (int g = 0; g < max_tokens; g++) {
        int next = sample_greedy(state.logits, qm.config.vocab_size);
        if (debug) { printf("[tok=%d] ", next); fflush(stdout); }
        n_gen++;
        if (next == eos_token) break;

        /* Stream-decode this single token */
        char tok_text[64];
        int tok_len = tokenizer_decode(&tok, &next, 1, tok_text, (int)sizeof(tok_text));
        if (tok_len > 0) {
            printf("%s", tok_text);
            fflush(stdout);
        }

        qmodel_forward(&qm, &state, next);
    }
    double gen_ms = time_ms() - t0;
    double gn_tps = (gen_ms > 0.0) ? n_gen * 1000.0 / gen_ms : 0.0;
    printf("\n");

    /* ---- Summary ---- */
    printf("\n");
    printf("============================================\n");
    printf("   Benchmark Summary\n");
    printf("============================================\n");
    printf("  Model:    %s\n", model_name ? model_name : model_path);
    printf("  Arch:     %s\n", arch_str ? arch_str : "(unknown)");
    printf("  Layers:   %d\n", qm.config.n_layers);
    printf("  Dim:      %d\n", qm.config.dim);
    printf("  Vocab:    %d\n", qm.config.vocab_size);
    printf("  Load:     %.1f ms (%.2f s)\n", load_ms, load_ms / 1000.0);
    printf("  Prefill:  %d tokens, %.1f tok/s\n", n_prompt, pp_tps);
    printf("  Gen:      %d tokens, %.1f tok/s\n", n_gen, gn_tps);
#ifdef __linux__
    {
        long rss_final = peak_rss_kb();
        if (rss_final > 0)
            printf("  Peak RSS: %ld MB\n", rss_final / 1024);
    }
#endif
    printf("============================================\n");

    /* ---- Cleanup ---- */
    llama_free_state(&state);
    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    qmodel_free(&qm);
    gguf_close(&ctx);
    free(gguf_data);

    return 0;
}
