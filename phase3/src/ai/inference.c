/*
 * JARVIS AI-OS Phase 3 — Top-Level Inference API
 */

#include "inference.h"
#include "sampling.h"
#include <string.h>

int inference_init(inference_ctx_t *ctx, const char *model_path)
{
    memset(ctx, 0, sizeof(*ctx));

    int err = llama_load_model(&ctx->model, model_path);
    if (err) return err;

    err = llama_alloc_state(&ctx->state, &ctx->model.config);
    if (err) { llama_free_model(&ctx->model); return err; }

    /* TODO: Extract vocab from GGUF metadata for tokenizer init.
     * For now, tokenizer must be initialized separately by caller. */
    ctx->ready = true;
    return 0;
}

int inference_query(inference_ctx_t *ctx, const char *prompt,
                    char *response, int max_response_len,
                    int max_tokens, float temperature, int top_k)
{
    if (!ctx || !ctx->ready || !prompt || !response) return -1;
    if (max_response_len <= 0) return -1;
    response[0] = '\0';

    /* Encode prompt to tokens */
    int prompt_tokens[512];
    int n_prompt = tokenizer_encode(&ctx->tokenizer, prompt,
                                     prompt_tokens, 512);
    if (n_prompt <= 0) return -1;

    /* Generate output tokens */
    int output_tokens[512];
    if (max_tokens > 512) max_tokens = 512;
    uint64_t seed = 42;

    int n_output = llama_generate(&ctx->model, &ctx->state,
                                   prompt_tokens, n_prompt,
                                   output_tokens, max_tokens,
                                   ctx->tokenizer.eos_id,
                                   temperature, top_k, seed);

    /* Decode output tokens to text */
    int len = tokenizer_decode(&ctx->tokenizer, output_tokens, n_output,
                                response, max_response_len);
    return len;
}

void inference_free(inference_ctx_t *ctx)
{
    if (!ctx) return;
    tokenizer_free(&ctx->tokenizer);
    llama_free_state(&ctx->state);
    llama_free_model(&ctx->model);
    memset(ctx, 0, sizeof(*ctx));
}
