/*
 * JARVIS AI-OS Phase 3 — Top-Level Inference API
 * Connects tokenizer → model → generation → decode in one call.
 */

#ifndef INFERENCE_H
#define INFERENCE_H

#include "llama_model.h"
#include "tokenizer.h"

typedef struct {
    llama_model_t model;
    llama_state_t state;
    tokenizer_t   tokenizer;
    bool          ready;
} inference_ctx_t;

int  inference_init(inference_ctx_t *ctx, const char *model_path);
int  inference_query(inference_ctx_t *ctx, const char *prompt,
                     char *response, int max_response_len,
                     int max_tokens, float temperature, int top_k);
void inference_free(inference_ctx_t *ctx);

#endif
