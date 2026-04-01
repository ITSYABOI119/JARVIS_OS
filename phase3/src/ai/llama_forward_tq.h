/*
 * JARVIS AI-OS Phase 3 -- TurboQuant-Integrated Forward Pass (Algorithm 2)
 *
 * Stores compressed tq_ckey_t/tq_cval_t in KV cache.
 * Uses tq_dot_key() for attention scoring (QJL-corrected).
 * Decompresses values on-the-fly for weighted sum.
 * Everything else is identical to llama_forward.c.
 */

#ifndef LLAMA_FORWARD_TQ_H
#define LLAMA_FORWARD_TQ_H

#include "llama_model.h"
#include "turboquant.h"

#define TQ_MAX_LAYERS 128

/* TQ-integrated inference state.
 * Replaces F32 key_cache/value_cache with compressed representations.
 * Activation buffers (x, xb, q, k, v, etc.) remain F32. */
typedef struct {
    /* Activation buffers (identical to llama_state_t) */
    float *x, *xb, *xb2;
    float *q, *k, *v;
    float *att;
    float *hb, *hb2;
    float *logits;

    /* Compressed KV cache: indexed as [L * max_seq * n_kv_heads + pos * n_kv_heads + h] */
    tq_ckey_t *key_cache;   /* n_layers * max_seq * n_kv_heads entries */
    tq_cval_t *val_cache;   /* n_layers * max_seq * n_kv_heads entries */

    /* Per-layer TQ state (rotation matrix Pi + QJL matrix S + codebooks) */
    tq_state_t tq_states[TQ_MAX_LAYERS];

    int pos;
    int max_seq_len;
    int n_layers;
    int n_kv_heads;
} llama_tq_state_t;

/* Allocate TQ state. base_seed is used to derive per-layer seeds (base_seed + L). */
int  llama_tq_alloc_state(llama_tq_state_t *state, const llama_config_t *config,
                           int key_bits, int val_bits, uint64_t base_seed);
void llama_tq_free_state(llama_tq_state_t *state);

/* Forward pass: identical to llama_forward except KV cache is compressed. */
void llama_tq_forward(const llama_model_t *model, llama_tq_state_t *state, int token);

/* Generation: identical to llama_generate but uses llama_tq_forward. */
int  llama_tq_generate(const llama_model_t *model, llama_tq_state_t *state,
                        const int *prompt_tokens, int n_prompt,
                        int *output_tokens, int max_output,
                        int eos_token, float temperature, int top_k,
                        uint64_t seed);

#endif /* LLAMA_FORWARD_TQ_H */
