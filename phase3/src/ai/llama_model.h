/*
 * JARVIS AI-OS Phase 3 — Llama Model Architecture Header
 *
 * Defines the Llama transformer model structures: config, layer weights,
 * full model, and inference state. Used with GGUF parser for weight loading.
 *
 * Pure C11, no C++ dependencies.
 */

#ifndef LLAMA_MODEL_H
#define LLAMA_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "gguf_parser.h"

typedef struct {
    int dim;
    int n_layers;
    int n_heads;
    int n_kv_heads;
    int head_dim;
    int hidden_dim;
    int vocab_size;
    int max_seq_len;
    float rope_theta;
} llama_config_t;

typedef struct {
    float *attn_norm;
    float *wq;
    float *wk;
    float *wv;
    float *wo;
    float *ffn_norm;
    float *w_gate;
    float *w_up;
    float *w_down;
} llama_layer_t;

typedef struct {
    llama_config_t config;
    float *token_embed;
    float *output_norm;
    float *output_weight;
    llama_layer_t *layers;
    size_t total_bytes;
    bool loaded;
} llama_model_t;

#define LLAMA_MAX_SEQ_LEN 512

typedef struct {
    float *x;
    float *xb;
    float *xb2;
    float *q;
    float *k;
    float *v;
    float *att;
    float *hb;
    float *hb2;
    float *logits;
    float *key_cache;
    float *value_cache;
    int pos;
    int max_seq_len;
} llama_state_t;

/* Loading API */
int  llama_load_config(llama_config_t *config, const gguf_ctx_t *ctx);
int  llama_alloc_model(llama_model_t *model);
int  llama_load_weights(llama_model_t *model, gguf_ctx_t *ctx);
int  llama_load_model(llama_model_t *model, const char *gguf_path);
void llama_free_model(llama_model_t *model);
int  llama_alloc_state(llama_state_t *state, const llama_config_t *config);
void llama_free_state(llama_state_t *state);

#endif /* LLAMA_MODEL_H */
