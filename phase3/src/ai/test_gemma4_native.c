/*
 * Native Gemma 4 inference test — runs on Linux/WSL, no seL4 needed.
 * Loads the real GGUF, tokenizes, runs forward pass, generates text.
 *
 * Compile:
 *   gcc -O2 -mavx2 -mfma -std=c11 -D_POSIX_C_SOURCE=200809L \
 *       -I phase3/src/ai \
 *       phase3/src/ai/test_gemma4_native.c \
 *       phase3/src/ai/llama_quant.c \
 *       phase3/src/ai/llama_forward.c \
 *       phase3/src/ai/llama_load.c \
 *       phase3/src/ai/gguf_parser.c \
 *       phase3/src/ai/gguf_vocab.c \
 *       phase3/src/ai/dequant.c \
 *       phase3/src/ai/tensor_ops.c \
 *       phase3/src/ai/tokenizer.c \
 *       phase3/src/ai/sampling.c \
 *       phase3/src/ai/inference.c \
 *       -lm -o /tmp/test_gemma4_native
 *
 * Run:
 *   /tmp/test_gemma4_native models/gemma-4-E2B-it-Q4_K_M.gguf
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gguf_parser.h"
#include "gguf_vocab.h"
#include "llama_quant.h"
#include "tokenizer.h"

int main(int argc, char **argv)
{
    const char *model_path = argc > 1 ? argv[1]
        : "models/gemma-4-E2B-it-Q4_K_M.gguf";

    /* 1. Load GGUF into memory */
    printf("=== JARVIS Native Gemma 4 Test ===\n\n");
    printf("Loading %s...\n", model_path);

    FILE *f = fopen(model_path, "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    printf("  Size: %.1f MB\n", sz / (1024.0 * 1024.0));

    void *data = malloc((size_t)sz);
    if (!data) { printf("  malloc failed (%ld bytes)\n", sz); return 1; }

    size_t total = 0;
    while (total < (size_t)sz) {
        size_t chunk = (size_t)sz - total;
        if (chunk > 64 * 1024 * 1024) chunk = 64 * 1024 * 1024;
        size_t r = fread((char *)data + total, 1, chunk, f);
        if (r == 0) { printf("  fread failed at %zu\n", total); return 1; }
        total += r;
        printf("  %zuMB read\r", total / (1024 * 1024));
        fflush(stdout);
    }
    fclose(f);
    printf("  %zuMB loaded into memory\n\n", total / (1024 * 1024));

    /* 2. Parse GGUF */
    gguf_ctx_t ctx;
    if (gguf_open_memory(&ctx, data, (size_t)sz) != 0) {
        printf("gguf_open_memory FAILED\n");
        return 1;
    }
    printf("GGUF: version=%u tensors=%llu kv=%llu\n\n",
           ctx.version, (unsigned long long)ctx.n_tensors,
           (unsigned long long)ctx.n_kv);

    /* 3. Load quantized model */
    qmodel_t qm;
    memset(&qm, 0, sizeof(qm));
    if (qmodel_load(&qm, &ctx, data) != 0) {
        printf("qmodel_load FAILED\n");
        return 1;
    }
    printf("\nModel: dim=%d layers=%d heads=%d/%d vocab=%d\n",
           qm.config.dim, qm.config.n_layers,
           qm.config.n_heads, qm.config.n_kv_heads,
           qm.config.vocab_size);
    printf("  embed_scale=%d ple=%d swa=%d softcap=%.1f\n\n",
           qm.config.embed_scale, qm.config.ple_dim,
           qm.config.swa_window, qm.config.logit_softcap);

    /* 4. Extract vocab + init tokenizer */
    gguf_vocab_t vocab;
    if (gguf_vocab_extract(data, (size_t)sz, &vocab) != 0) {
        printf("vocab extract FAILED\n");
        return 1;
    }

    tokenizer_t tok;
    if (gguf_vocab_init_tokenizer(&vocab, &tok) != 0) {
        printf("tokenizer init FAILED\n");
        return 1;
    }
    printf("Tokenizer: %d tokens, bos=%d, eos=%d, has_merges=%d\n\n",
           tok.vocab_size, tok.bos_id, tok.eos_id, vocab.has_merges);

    /* 5. Test tokenization — compare against llama.cpp reference */
    const char *test_prompts[] = {
        "The seL4 microkernel is",
        "What is a page fault",
        "Hello world",
    };
    int ref_ids[] = {2, 818, 636, 236798, 236812, 4719, 21424, 563};
    int ref_count = 8;

    for (int p = 0; p < 3; p++) {
        int ids[64];
        ids[0] = tok.bos_id;
        int n = 1 + tokenizer_encode(&tok, test_prompts[p], ids + 1, 63);
        printf("Tokenize \"%s\":\n  [%d tokens]:", test_prompts[p], n);
        for (int i = 0; i < n && i < 20; i++) printf(" %d", ids[i]);
        if (n > 20) printf(" ...");
        printf("\n");
        if (p == 0) {
            printf("  Reference: ");
            for (int i = 0; i < ref_count; i++) printf(" %d", ref_ids[i]);
            int match = (n == ref_count);
            if (match) {
                for (int i = 0; i < ref_count; i++)
                    if (ids[i] != ref_ids[i]) { match = 0; break; }
            }
            printf("\n  %s\n", match ? "*** MATCH ***" : "*** MISMATCH ***");
        }
        printf("\n");
    }

    /* 6. Allocate state */
    llama_state_t state;
    if (llama_alloc_state(&state, &qm.config) != 0) {
        printf("alloc_state FAILED\n");
        return 1;
    }

    /* === Print low-ID tokens to find control tokens === */
    printf("=== Low-ID tokens (control tokens) ===\n");
    for (int i = 0; i < 150; i++) {
        if (tok.tokens[i]) {
            /* Print only if it looks like a control token (contains < or |) or is short */
            const char *t = tok.tokens[i];
            int has_ctrl = (strchr(t, '<') || strchr(t, '|') || strchr(t, '>')) ? 1 : 0;
            if (has_ctrl || strlen(t) <= 4) {
                printf("  [%3d] \"%s\"\n", i, t);
            }
        }
    }
    printf("\n");

    /* Search for specific control token names */
    const char *control_names[] = {
        "<|turn>", "<turn|>", "<start_of_turn>", "<end_of_turn>",
        "<bos>", "<eos>", "<pad>", "user", "model",
        "<|turn>user", "<|turn>model", NULL
    };
    printf("=== Control token lookup ===\n");
    for (int i = 0; control_names[i]; i++) {
        int id = tokenizer_find(&tok, control_names[i]);
        printf("  tokenizer_find(\"%s\") = %d\n", control_names[i], id);
    }
    printf("\n");

    /* 7. Generate using Gemma 4 chat template with DIRECT token IDs.
     * Template: <bos><|turn>user\n{text}<turn|>\n<|turn>model\n<|think|>
     * Control tokens: <|turn>=105, <turn|>=106, \n=107, user=2364, model=4368, <|think|>=98
     * Note: Gemma 4 has a thinking mode that activates with <|think|>. */
    printf("=== Generation Test (chat template + think, direct IDs) ===\n\n");
    int prompt_ids[128];
    int n_prompt = 0;
    prompt_ids[n_prompt++] = tok.bos_id;   /* <bos> = 2 */
    prompt_ids[n_prompt++] = 105;          /* <|turn> */
    prompt_ids[n_prompt++] = 2364;         /* user */
    prompt_ids[n_prompt++] = 107;          /* \n */
    /* encode user text */
    n_prompt += tokenizer_encode(&tok, "What is the seL4 microkernel?",
                                  prompt_ids + n_prompt, 127 - n_prompt - 6);
    prompt_ids[n_prompt++] = 106;          /* <turn|> */
    prompt_ids[n_prompt++] = 107;          /* \n */
    prompt_ids[n_prompt++] = 105;          /* <|turn> */
    prompt_ids[n_prompt++] = 4368;         /* model */
    prompt_ids[n_prompt++] = 107;          /* \n */
    prompt_ids[n_prompt++] = 98;           /* <|think|> — enter thinking mode */

    printf("Prompt tokens (%d):", n_prompt);
    for (int i = 0; i < n_prompt; i++) printf(" %d", prompt_ids[i]);
    printf("\n");

    printf("Generating 30 tokens (greedy, temp=0)...\n");
    clock_t t0 = clock();

    int out_ids[64];
    /* Use 1 (<eos>) as stop token, NOT 106 (<turn|>) which model emits
     * as first token after chat template prompt. */
    int n_gen = qmodel_generate(&qm, &state, prompt_ids, n_prompt,
                                 out_ids, 30, 1 /* <eos> */, 0.0f, 1, 42);

    clock_t t1 = clock();
    double elapsed = (double)(t1 - t0) / CLOCKS_PER_SEC;

    char text[1024];
    int tlen = tokenizer_decode(&tok, out_ids, n_gen, text, sizeof(text));
    (void)tlen;

    printf("\nPrompt: \"The seL4 microkernel is\"\n");
    printf("Output: \"%s\"\n", text);
    printf("Tokens: %d in %.1fs (%.1f tok/s)\n", n_gen, elapsed,
           elapsed > 0 ? n_gen / elapsed : 0);
    printf("\nOutput IDs:");
    for (int i = 0; i < n_gen; i++) printf(" %d", out_ids[i]);
    printf("\n");

    /* 8. Second prompt */
    printf("\n--- Second prompt ---\n");
    state.pos = 0;
    size_t kv_bytes = (size_t)qm.config.n_layers * state.max_seq_len *
                      qm.config.n_kv_heads * qm.config.head_dim * sizeof(float);
    memset(state.key_cache, 0, kv_bytes);
    memset(state.value_cache, 0, kv_bytes);

    int p2[128];
    int n2 = 0;
    p2[n2++] = tok.bos_id;
    p2[n2++] = 105;          /* <|turn> */
    p2[n2++] = 2364;         /* user */
    p2[n2++] = 107;          /* \n */
    n2 += tokenizer_encode(&tok, "What is a page fault?",
                           p2 + n2, 127 - n2 - 6);
    p2[n2++] = 106;          /* <turn|> */
    p2[n2++] = 107;          /* \n */
    p2[n2++] = 105;          /* <|turn> */
    p2[n2++] = 4368;         /* model */
    p2[n2++] = 107;          /* \n */
    p2[n2++] = 98;           /* <|think|> */
    t0 = clock();
    n_gen = qmodel_generate(&qm, &state, p2, n2, out_ids, 30,
                             1 /* <eos> */, 0.0f, 1, 42);
    t1 = clock();
    elapsed = (double)(t1 - t0) / CLOCKS_PER_SEC;

    tlen = tokenizer_decode(&tok, out_ids, n_gen, text, sizeof(text));
    printf("Prompt: (chat template) \"What is a page fault?\"\n");
    printf("Output: \"%s\"\n", text);
    printf("Tokens: %d in %.1fs (%.1f tok/s)\n", n_gen, elapsed,
           elapsed > 0 ? n_gen / elapsed : 0);

    printf("\n=== DONE ===\n");
    return 0;
}
