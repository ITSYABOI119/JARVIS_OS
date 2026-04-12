/*
 * JARVIS AI-OS Phase 3 — Quantized Llama Model (Zero-Copy)
 *
 * Stores raw pointers into embedded GGUF .rodata instead of dequantizing
 * all weights to F32. Dequantizes on-the-fly during matrix-vector multiply.
 *
 * Memory budget (Llama 3.2 1B Q4_K_M, 256 MB heap):
 *   - layers array:   16 * sizeof(qlayer_t) ~  2 KB
 *   - activation bufs:  ~600 KB
 *   - KV cache:          ~32 MB
 *   - Tokenizer vocab:   ~10 MB
 *   - Grand total:       ~50 MB  (vs 5.7 GB for full F32 dequant)
 *
 * The 771 MB GGUF model lives in .rodata — no heap needed for weights.
 *
 * Pure C11, no C++ dependencies.
 */

#ifndef LLAMA_QUANT_H
#define LLAMA_QUANT_H

#include "llama_model.h"   /* llama_config_t, llama_state_t, llama_alloc_state, etc. */
#include "gguf_parser.h"   /* gguf_ctx_t, ggml_type_t, gguf_find_tensor, etc. */

/* ---- Quantized tensor reference (zero-copy into GGUF .rodata) ---- */

typedef struct {
    const void *data;        /* Raw quantized data pointer (NOT malloc'd — points into .rodata) */
    uint32_t    type;        /* ggml_type_t (GGML_TYPE_Q4_K, GGML_TYPE_Q6_K, GGML_TYPE_F32, ...) */
    uint64_t    n_elements;  /* Total element count (product of dims) */
    uint64_t    n_bytes;     /* Raw byte count in quantized form */
} qtensor_t;

/* ---- Per-layer quantized weight references ---- */

typedef struct {
    qtensor_t attn_norm;     /* F32 — small (dim floats) */
    qtensor_t wq;            /* Q4_K or Q6_K — [dim x dim] */
    qtensor_t wk;            /* Q4_K or Q6_K — [kv_dim x dim] */
    qtensor_t wv;            /* Q4_K or Q6_K — [kv_dim x dim] */
    qtensor_t wo;            /* Q4_K or Q6_K — [dim x dim] */
    qtensor_t ffn_norm;      /* F32 — small (dim floats) */
    qtensor_t w_gate;        /* Q4_K or Q6_K — [hidden_dim x dim] */
    qtensor_t w_up;          /* Q4_K or Q6_K — [hidden_dim x dim] */
    qtensor_t w_down;        /* Q4_K or Q6_K — [dim x hidden_dim] */
    /* --- Gemma 4 extensions (data=NULL for Llama models) --- */
    qtensor_t q_norm;             /* per-head Q RMSNorm before RoPE (Gemma 4, Qwen3) */
    qtensor_t k_norm;             /* per-head K RMSNorm before RoPE (Gemma 4, Qwen3) */
    qtensor_t post_attn_norm;     /* post-attention RMSNorm (sandwich norm) */
    qtensor_t post_ffw_norm;      /* post-FFN RMSNorm (sandwich norm) */
    qtensor_t layer_output_scale; /* per-layer output scaling */
    /* --- PLE per-layer tensors (Gemma 4, data=NULL for Llama) --- */
    qtensor_t ple_gate;           /* blk.N.inp_gate.weight [dim] — GELU gated */
    qtensor_t ple_proj;           /* blk.N.proj.weight [dim x ple_dim] */
    qtensor_t post_norm;          /* blk.N.post_norm.weight — post-PLE norm */
    /* --- DeltaNet/SSM tensors (Qwen3.5, data=NULL for non-DeltaNet layers) --- */
    qtensor_t ssm_conv1d;     /* depthwise conv1d kernel [d_conv, qkv_dim] */
    qtensor_t ssm_alpha;      /* alpha gate projection [dim, num_v_heads] */
    qtensor_t ssm_beta;       /* beta gate projection [dim, num_v_heads] */
    qtensor_t ssm_a;          /* A_log decay rates [num_v_heads] */
    qtensor_t ssm_dt_bias;    /* timestep bias [num_v_heads] */
    qtensor_t ssm_norm;       /* output RMS norm [head_v_dim] */
    qtensor_t ssm_out;        /* output projection [d_inner, dim] */
    qtensor_t attn_out_gate;  /* output gate: DeltaNet Z or full-attn sigmoid gate */
} qlayer_t;

/* ---- Full quantized model (only layers array is malloc'd) ---- */

typedef struct {
    llama_config_t config;
    qtensor_t token_embed;     /* Q6_K — [vocab_size x dim], lookup by row */
    qtensor_t output_norm;     /* F32 — small (dim floats) */
    qtensor_t output_weight;   /* Q6_K — [vocab_size x dim], final projection */
    const float *rope_freqs;   /* Custom RoPE frequencies (pointer into .rodata), or NULL */
    qlayer_t *layers;          /* Array of n_layers (malloc'd, but each qlayer_t just holds pointers) */
    bool      loaded;
    /* --- PLE global tensors (Gemma 4, data=NULL for Llama) --- */
    qtensor_t ple_embed;       /* per_layer_token_embd.weight [vocab x ple_dim] */
    qtensor_t ple_proj;        /* per_layer_model_proj.weight [dim x ple_dim] */
    qtensor_t ple_norm;        /* per_layer_proj_norm.weight [ple_dim] */
} qmodel_t;

/* ---- API ---- */

/**
 * Load quantized model — stores pointers into GGUF .rodata, NO weight dequantization.
 *
 * @param qm        Output model struct (zeroed by this function)
 * @param ctx       Parsed GGUF context (for metadata and tensor info)
 * @param gguf_base Raw pointer to start of GGUF data (e.g., &_binary_model_gguf_start)
 * @return 0 on success, negative on error
 */
int qmodel_load(qmodel_t *qm, const gguf_ctx_t *ctx, const void *gguf_base);

/**
 * Free quantized model. Only frees the layers array — weight data is in .rodata.
 */
void qmodel_free(qmodel_t *qm);

/**
 * Quantized forward pass for one token. Dequantizes weights on-the-fly
 * during matrix-vector multiply (one block at a time, 256 floats on stack).
 *
 * @param qm    Loaded quantized model
 * @param state  Inference state (activation buffers + KV cache, same as F32 path)
 * @param token  Input token ID
 */
void qmodel_forward(const qmodel_t *qm, llama_state_t *state, int token);

/**
 * Quantized autoregressive generation loop.
 * Same interface as llama_generate() but uses qmodel_forward() internally.
 *
 * @return Number of tokens generated (written to output_tokens)
 */
int qmodel_generate(const qmodel_t *qm, llama_state_t *state,
                    const int *prompt_tokens, int n_prompt,
                    int *output_tokens, int max_output,
                    int eos_token, float temperature, int top_k,
                    uint64_t seed);

#endif /* LLAMA_QUANT_H */
