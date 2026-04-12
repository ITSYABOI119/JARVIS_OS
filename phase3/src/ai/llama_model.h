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

#define LLAMA_MAX_LAYERS 128

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

    /* --- Gemma 4 extensions (all default to 0/false/NULL for Llama models) --- */
    bool  embed_scale;        /* true = multiply embedding by sqrt(dim) (all Gemma variants) */
    bool  use_gelu;           /* true = GeGLU activation (Gemma), false = SwiGLU (Llama) */
    bool  rope_neox;          /* true = NEOX/split RoPE (Gemma), false = interleaved (Llama) */
    float rms_norm_eps;       /* 0.0 = use default 1e-5 */
    float logit_softcap;      /* 0.0 = no softcapping */
    float rope_theta_swa;     /* 0.0 = no SWA layers */
    int   ple_dim;            /* 0   = no PLE */
    int   swa_window;         /* 0   = no sliding window */
    int   shared_kv_layers;   /* 0   = all layers compute own KV */
    int   head_dim_swa;       /* 0   = no SWA-specific head dim */

    /* --- Qwen3.5 SSM/DeltaNet extensions (all default to 0/NULL) --- */
    int   ssm_d_conv;          /* conv kernel width (4 for Qwen3.5) */
    int   ssm_d_state;         /* head_k_dim (128) — Q/K per-head dim for SSM */
    int   ssm_n_group;         /* num_k_heads (16) — Q/K head count for SSM */
    int   ssm_dt_rank;         /* num_v_heads (32) — V/output head count */
    int   ssm_d_inner;         /* d_inner (4096) — V_dim = num_v_heads * head_v_dim */
    int   full_attn_interval;  /* every Nth layer is full attention (4 for Qwen3.5) */

    /* Per-layer config (malloc'd, NULL for uniform/Llama models) */
    int  *layer_ffn_dim;      /* [n_layers] — per-layer FFN hidden dim */
    bool *layer_is_swa;       /* [n_layers] — true = sliding window attention */
    int  *kv_share_map;       /* [n_layers] — -1 = own KV, >=0 = index of source layer */
    bool *layer_is_deltanet;  /* [n_layers] — true = DeltaNet recurrent layer */
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
    float *rope_freqs;     /* Custom RoPE frequencies (head_dim/2 values), or NULL for standard */
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
    /* Gemma 4 PLE scratch buffers (heap — too large for seL4 Process B stack) */
    float *ple_all;        /* [n_layers * ple_dim] — per-token PLE inputs */
    float *ple_context;    /* [n_layers * ple_dim] — context-aware projection */
    /* Qwen3.5 DeltaNet state buffers (heap, fixed size, context-independent) */
    float *conv_state;     /* [n_deltanet × (d_conv-1) × qkv_dim] — conv1d sliding window */
    float *ssm_state;      /* [n_deltanet × num_v_heads × head_k_dim × head_v_dim] — recurrent state */
    float *qkv_scratch;    /* scratch for fused QKV + gate projections */
    int    n_deltanet;     /* number of DeltaNet layers (for state indexing) */
    int pos;
    int max_seq_len;
} llama_state_t;

/* Loading API */
int  llama_load_config(llama_config_t *config, const gguf_ctx_t *ctx);
void llama_free_config(llama_config_t *config);
int  llama_alloc_model(llama_model_t *model);
int  llama_load_weights(llama_model_t *model, gguf_ctx_t *ctx);
int  llama_load_model(llama_model_t *model, const char *gguf_path);
void llama_free_model(llama_model_t *model);
int  llama_alloc_state(llama_state_t *state, const llama_config_t *config);
void llama_free_state(llama_state_t *state);

/* Forward pass + generation (implemented in llama_forward.c) */
void llama_forward(const llama_model_t *model, llama_state_t *state, int token);
int  llama_generate(const llama_model_t *model, llama_state_t *state,
                    const int *prompt_tokens, int n_prompt,
                    int *output_tokens, int max_output,
                    int eos_token, float temperature, int top_k,
                    uint64_t seed);

#endif /* LLAMA_MODEL_H */
