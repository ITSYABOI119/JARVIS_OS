/*
 * JARVIS AI-OS Phase 3 -- Quantized Model + TurboQuant KV Cache Forward Pass
 *
 * Combines quantized weight matmul (llama_quant.c) with TurboQuant compressed
 * KV cache (turboquant.c). The tq_states array is dynamically allocated via
 * calloc to avoid ~4MB fixed arrays on the stack.
 *
 * Pure C11, no C++ dependencies.
 */

#include "qmodel_tq_forward.h"
#include "qmatmul.h"
#include "dequant.h"
#include "tensor_ops.h"
#include "sampling.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

int qmodel_tq_alloc_state(qmodel_tq_state_t *state, const llama_config_t *config,
                           int key_bits, int val_bits, uint64_t base_seed)
{
    if (!state || !config) return -1;
    if (config->dim <= 0 || config->n_layers <= 0) return -1;

    memset(state, 0, sizeof(*state));

    int dim        = config->dim;
    int n_heads    = config->n_heads;
    int n_kv_heads = config->n_kv_heads;
    int head_dim   = config->head_dim;
    int hidden_dim = config->hidden_dim;
    int vocab_size = config->vocab_size;
    int max_seq    = config->max_seq_len;
    int n_layers   = config->n_layers;

    state->max_seq_len = max_seq;
    state->n_layers    = n_layers;
    state->n_kv_heads  = n_kv_heads;
    state->key_bits    = key_bits;
    state->val_bits    = val_bits;
    state->pos         = 0;

    /* 1. Activation buffers */
    state->x      = (float *)calloc((size_t)dim, sizeof(float));
    state->xb     = (float *)calloc((size_t)dim, sizeof(float));
    state->xb2    = (float *)calloc((size_t)dim, sizeof(float));
    state->q      = (float *)calloc((size_t)n_heads * head_dim, sizeof(float));
    state->k      = (float *)calloc((size_t)n_kv_heads * head_dim, sizeof(float));
    state->v      = (float *)calloc((size_t)n_kv_heads * head_dim, sizeof(float));
    state->att    = (float *)calloc((size_t)n_heads * max_seq, sizeof(float));
    state->hb     = (float *)calloc((size_t)hidden_dim, sizeof(float));
    state->hb2    = (float *)calloc((size_t)hidden_dim, sizeof(float));
    state->logits = (float *)calloc((size_t)vocab_size, sizeof(float));

    if (!state->x || !state->xb || !state->xb2 ||
        !state->q || !state->k || !state->v ||
        !state->att || !state->hb || !state->hb2 || !state->logits) {
        qmodel_tq_free_state(state);
        return -1;
    }

    /* 2. Compressed KV cache: n_layers * max_seq * n_kv_heads entries */
    size_t cache_entries = (size_t)n_layers * (size_t)max_seq * (size_t)n_kv_heads;
    state->key_cache = (tq_ckey_t *)calloc(cache_entries, sizeof(tq_ckey_t));
    state->val_cache = (tq_cval_t *)calloc(cache_entries, sizeof(tq_cval_t));

    if (!state->key_cache || !state->val_cache) {
        qmodel_tq_free_state(state);
        return -1;
    }

    /* 3. Per-layer TQ states (dynamically allocated, NOT a fixed array) */
    state->tq_states = (tq_state_t *)calloc((size_t)n_layers, sizeof(tq_state_t));
    if (!state->tq_states) {
        qmodel_tq_free_state(state);
        return -1;
    }

    /* 4. Initialize each tq_state */
    for (int L = 0; L < n_layers; L++) {
        if (tq_init(&state->tq_states[L], head_dim, key_bits, val_bits,
                    base_seed + (uint64_t)L) != 0) {
            /* tq_init may have partially initialized; free up to L-1 */
            /* Set n_layers to L so free only iterates initialized states */
            state->n_layers = L;
            qmodel_tq_free_state(state);
            return -1;
        }
    }

    /* Restore n_layers after partial-init safeguard */
    state->n_layers = n_layers;
    return 0;
}

void qmodel_tq_free_state(qmodel_tq_state_t *state)
{
    if (!state) return;

    free(state->x);
    free(state->xb);
    free(state->xb2);
    free(state->q);
    free(state->k);
    free(state->v);
    free(state->att);
    free(state->hb);
    free(state->hb2);
    free(state->logits);
    free(state->key_cache);
    free(state->val_cache);

    if (state->tq_states) {
        for (int L = 0; L < state->n_layers; L++)
            tq_free(&state->tq_states[L]);
        free(state->tq_states);
    }

    memset(state, 0, sizeof(*state));
}

void qmodel_tq_print_memory(const qmodel_tq_state_t *state, const llama_config_t *config)
{
    if (!state || !config) return;

    int n_layers   = config->n_layers;
    int n_kv_heads = config->n_kv_heads;
    int head_dim   = config->head_dim;
    int max_seq    = config->max_seq_len;

    size_t cache_entries = (size_t)n_layers * (size_t)max_seq * (size_t)n_kv_heads;

    /* F32 baseline: 2 * n_layers * max_seq * n_kv_heads * head_dim * 4 bytes */
    size_t f32_kv = cache_entries * (size_t)head_dim * 2 * sizeof(float);

    /* TQ compressed: actual struct sizes */
    size_t tq_kv = cache_entries * (sizeof(tq_ckey_t) + sizeof(tq_cval_t));

    /* TQ state overhead (rotation + projection matrices per layer) */
    size_t tq_state_overhead = 0;
    if (state->tq_states) {
        for (int L = 0; L < n_layers; L++)
            tq_state_overhead += tq_state_bytes(&state->tq_states[L]);
    }

    printf("qmodel_tq memory report:\n");
    printf("  F32 KV cache:       %zu MB (%zu bytes)\n",
           f32_kv / (1024 * 1024), f32_kv);
    printf("  TQ KV cache:        %zu KB (%zu bytes)\n",
           tq_kv / 1024, tq_kv);
    printf("  TQ state overhead:  %zu KB (%zu bytes)\n",
           tq_state_overhead / 1024, tq_state_overhead);
    printf("  Compression ratio:  %.1fx\n",
           f32_kv > 0 ? (double)f32_kv / (double)tq_kv : 0.0);
}

/* Forward pass stub -- implemented in Task 3 */
void qmodel_tq_forward(const qmodel_t *qm, qmodel_tq_state_t *state, int token)
{
    (void)qm;
    (void)state;
    (void)token;
}

/* Generate stub -- implemented in Task 3 */
int qmodel_tq_generate(const qmodel_t *qm, qmodel_tq_state_t *state,
                        const int *prompt_tokens, int n_prompt,
                        int *output_tokens, int max_output,
                        int eos_token, float temperature, int top_k, uint64_t seed)
{
    (void)qm;
    (void)state;
    (void)prompt_tokens;
    (void)n_prompt;
    (void)output_tokens;
    (void)max_output;
    (void)eos_token;
    (void)temperature;
    (void)top_k;
    (void)seed;
    return 0;
}
