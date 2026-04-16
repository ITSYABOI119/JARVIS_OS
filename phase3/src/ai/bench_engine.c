/*
 * JARVIS AI-OS Phase 3 -- Quantized Inference Engine Benchmark
 *
 * Standalone speed benchmark for GGUF models using the zero-copy
 * quantized inference path (qmodel_forward). Single prompt, 128-token
 * generation — matches llama-bench pp/tg methodology for fair comparison.
 *
 * Output format matches llama-bench table layout for side-by-side comparison.
 *
 * Compile:
 *   gcc -O2 -mavx2 -mfma -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE \
 *       -I phase3/src/ai \
 *       phase3/src/ai/bench_engine.c phase3/src/ai/llama_quant.c \
 *       phase3/src/ai/llama_load.c phase3/src/ai/llama_forward.c \
 *       phase3/src/ai/gguf_parser.c phase3/src/ai/gguf_vocab.c \
 *       phase3/src/ai/tokenizer.c phase3/src/ai/tensor_ops.c \
 *       phase3/src/ai/dequant.c phase3/src/ai/sampling.c \
 *       phase3/src/ai/inference.c phase3/src/ai/ssm.c \
 *       -lm -o /tmp/bench_engine
 *
 * Run:
 *   /tmp/bench_engine path/to/model.gguf [--tokens 128] [--threads N] [--debug]
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
#include <time.h> /* clock_gettime */
#ifdef __linux__
#include <unistd.h>
#endif

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

/* ---- Gemma 4 chat template token IDs (from inference_server.c) ---- */
#define GEMMA_TURN_OPEN   105
#define GEMMA_USER        2364
#define GEMMA_NEWLINE     107
#define GEMMA_TURN_CLOSE  106
#define GEMMA_MODEL       4368
#define GEMMA_THINK       98
#define GEMMA_EOS_STOP    1

static const char *bench_prompt = "The seL4 microkernel is";

/* Build prompt token sequence. Returns total token count. */
static int build_prompt(const tokenizer_t *tok, const llama_config_t *cfg,
                        const char *arch, const char *text, int *ids, int max_ids)
{
    int n = 0;
    int nl_tok = tokenizer_find(tok, "\n");

    if (cfg->embed_scale) {
        ids[n++] = tok->bos_id;
        ids[n++] = GEMMA_TURN_OPEN;
        ids[n++] = GEMMA_USER;
        ids[n++] = GEMMA_NEWLINE;
        n += tokenizer_encode(tok, text, ids + n, max_ids - n - 6);
        ids[n++] = GEMMA_TURN_CLOSE;
        ids[n++] = GEMMA_NEWLINE;
        ids[n++] = GEMMA_TURN_OPEN;
        ids[n++] = GEMMA_MODEL;
        ids[n++] = GEMMA_NEWLINE;
        ids[n++] = GEMMA_THINK;
    } else if (arch && strcmp(arch, "phi3") == 0) {
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
        } else {
            ids[n++] = tok->bos_id;
            n += tokenizer_encode(tok, text, ids + n, max_ids - n);
        }
    } else if (arch && (strcmp(arch, "qwen3") == 0 || strcmp(arch, "qwen35") == 0)) {
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
        } else {
            ids[n++] = tok->bos_id;
            n += tokenizer_encode(tok, text, ids + n, max_ids - n);
        }
    } else {
        ids[n++] = tok->bos_id;
        n += tokenizer_encode(tok, text, ids + n, max_ids - n);
    }
    return n;
}

/* Estimate parameter count from config (billions) */
static double estimate_params_b(const llama_config_t *c)
{
    double embed = (double)c->vocab_size * c->dim;
    double per_layer = (double)c->dim * c->dim * 4.0 + /* QKV + O */
                       (double)c->dim * c->hidden_dim * 3.0; /* gate + up + down */
    return (embed + per_layer * c->n_layers) / 1e9;
}

/* ---- Main ---- */

int main(int argc, char **argv)
{
    const char *model_path = NULL;
    int max_tokens = 128;
    int debug = 0;
    int threads = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--tokens") == 0 && i + 1 < argc) {
            max_tokens = atoi(argv[++i]);
            if (max_tokens < 1) max_tokens = 1;
            if (max_tokens > 512) max_tokens = 512;
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            threads = atoi(argv[++i]);
            if (threads < 1) threads = 1;
#ifndef JARVIS_PTHREAD
            fprintf(stderr, "Warning: --threads ignored (compile with -DJARVIS_PTHREAD=1 -pthread to enable)\n");
#endif
        } else if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
            debug = 1;
        } else if (argv[i][0] != '-') {
            model_path = argv[i];
        }
    }

    if (!model_path) {
        fprintf(stderr, "Usage: bench_engine <model.gguf> [--tokens N] [--threads N] [--debug]\n");
        fprintf(stderr, "Notes: threads default = all available CPUs; override via --threads or env JARVIS_THREADS.\n");
        return 1;
    }

    /* Optional override for pthread builds (no-op for single-thread builds) */
#ifdef __linux__
    if (threads > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", threads);
        setenv("JARVIS_THREADS", buf, 1);
    }
#endif

    /* ---- Read entire GGUF into memory ---- */
    FILE *f = fopen(model_path, "rb");
    if (!f) {
        fprintf(stderr, "ERROR: cannot open %s\n", model_path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    fprintf(stderr, "  Loading %s (%.2f GiB)...\n", model_path,
            file_size / (1024.0 * 1024 * 1024));
    fflush(stderr);

    void *gguf_data = malloc((size_t)file_size);
    if (!gguf_data) {
        fprintf(stderr, "ERROR: cannot allocate %ld bytes\n", file_size);
        fclose(f);
        return 1;
    }
    if (fread(gguf_data, 1, (size_t)file_size, f) != (size_t)file_size) {
        fprintf(stderr, "ERROR: read failed\n");
        free(gguf_data);
        fclose(f);
        return 1;
    }
    fclose(f);

    /* ---- Parse GGUF + load model ---- */
    gguf_ctx_t ctx;
    double t_load_start = time_ms();

    int err = gguf_open_memory(&ctx, gguf_data, (size_t)file_size);
    if (err) {
        fprintf(stderr, "ERROR: gguf_open_memory failed\n");
        free(gguf_data);
        return 1;
    }

    qmodel_t qm;
    err = qmodel_load(&qm, &ctx, gguf_data);
    if (err) {
        const char *arch = gguf_get_kv_string(&ctx, "general.architecture");
        fprintf(stderr, "ERROR: qmodel_load failed [arch=%s]\n", arch ? arch : "?");
        gguf_close(&ctx);
        free(gguf_data);
        return 1;
    }

    gguf_vocab_t vocab;
    err = gguf_vocab_extract(gguf_data, (size_t)file_size, &vocab);
    if (err) {
        fprintf(stderr, "ERROR: vocab extract failed\n");
        qmodel_free(&qm);
        gguf_close(&ctx);
        free(gguf_data);
        return 1;
    }

    tokenizer_t tok;
    err = gguf_vocab_init_tokenizer(&vocab, &tok);
    if (err) {
        fprintf(stderr, "ERROR: tokenizer init failed\n");
        gguf_vocab_free(&vocab);
        qmodel_free(&qm);
        gguf_close(&ctx);
        free(gguf_data);
        return 1;
    }

    llama_state_t state;
    err = llama_alloc_state(&state, &qm.config);
    if (err) {
        fprintf(stderr, "ERROR: state alloc failed\n");
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
    int eos_token = qm.config.embed_scale ? GEMMA_EOS_STOP : tok.eos_id;
    double params_b = estimate_params_b(&qm.config);

    fprintf(stderr, "  Loaded in %.1fs (%s, %d layers, %d dim)\n",
            load_ms / 1000.0, arch_str ? arch_str : "?",
            qm.config.n_layers, qm.config.dim);
    fflush(stderr);

    /* KV cache size for reset */
    size_t kv_bytes = (size_t)qm.config.n_layers * (size_t)state.max_seq_len *
                      (size_t)qm.config.n_kv_heads * (size_t)qm.config.head_dim *
                      sizeof(float);

    /* ---- Build prompt ---- */
    int prompt_ids[256];
    int n_prompt = build_prompt(&tok, &qm.config, arch_str, bench_prompt, prompt_ids, 256);

    if (debug) {
        fprintf(stderr, "  Prompt IDs (%d):", n_prompt);
        for (int i = 0; i < n_prompt; i++) fprintf(stderr, " %d", prompt_ids[i]);
        fprintf(stderr, "\n");
    }

    /* Reset KV cache */
    state.pos = 0;
    memset(state.key_cache, 0, kv_bytes);
    memset(state.value_cache, 0, kv_bytes);

    /* ---- Prefill ---- */
    double t0 = time_ms();
    for (int i = 0; i < n_prompt; i++)
        qmodel_forward(&qm, &state, prompt_ids[i]);
    double prefill_ms = time_ms() - t0;
    (void)prefill_ms; /* prefill speed not in table output — gen speed is the metric */

    /* ---- Generation ---- */
    int n_gen = 0;
    t0 = time_ms();
    for (int g = 0; g < max_tokens; g++) {
        int next = sample_greedy(state.logits, qm.config.vocab_size);
        n_gen++;
        if (next == eos_token) break;
        qmodel_forward(&qm, &state, next);
    }
    double gen_ms = time_ms() - t0;
    double gn_tps = (gen_ms > 0.0) ? n_gen * 1000.0 / gen_ms : 0.0;

    /* ---- Output: llama-bench table row ---- */
    char test_label[32];
    snprintf(test_label, sizeof(test_label), "tg%d", max_tokens);

    printf("| %-30s | %8.2f GiB | %8.2f B | %-10s | %7d | %15s | %17.2f ± 0.00 |\n",
           model_name ? model_name : "(unknown)",
           file_size / (1024.0 * 1024 * 1024),
           params_b,
           "JARVIS", 1, test_label, gn_tps);

#ifdef __linux__
    {
        long rss = peak_rss_kb();
        if (rss > 0)
            fprintf(stderr, "  Peak RSS: %ld MB\n", rss / 1024);
    }
#endif

    /* ---- Cleanup ---- */
    llama_free_state(&state);
    tokenizer_free(&tok);
    gguf_vocab_free(&vocab);
    qmodel_free(&qm);
    gguf_close(&ctx);
    free(gguf_data);

    return 0;
}
