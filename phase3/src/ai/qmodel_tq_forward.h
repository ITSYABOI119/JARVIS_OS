#ifndef QMODEL_TQ_FORWARD_H
#define QMODEL_TQ_FORWARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "llama_model.h"
#include "llama_quant.h"
#include "turboquant.h"

typedef struct {
    float *x, *xb, *xb2;
    float *q, *k, *v;
    float *att;
    float *hb, *hb2;
    float *logits;

    tq_ckey_t *key_cache;   /* n_layers * max_seq * n_kv_heads */
    tq_cval_t *val_cache;   /* n_layers * max_seq * n_kv_heads */

    tq_state_t *tq_states;  /* calloc(n_layers) -- NOT fixed array */

    int pos;
    int max_seq_len;
    int n_layers;
    int n_kv_heads;
    int key_bits;
    int val_bits;
} qmodel_tq_state_t;

int  qmodel_tq_alloc_state(qmodel_tq_state_t *state, const llama_config_t *config,
                            int key_bits, int val_bits, uint64_t base_seed);
void qmodel_tq_free_state(qmodel_tq_state_t *state);
void qmodel_tq_forward(const qmodel_t *qm, qmodel_tq_state_t *state, int token);
int  qmodel_tq_generate(const qmodel_t *qm, qmodel_tq_state_t *state,
                         const int *prompt_tokens, int n_prompt,
                         int *output_tokens, int max_output,
                         int eos_token, float temperature, int top_k, uint64_t seed);
void qmodel_tq_print_memory(const qmodel_tq_state_t *state, const llama_config_t *config);

#ifdef __cplusplus
}
#endif
#endif
