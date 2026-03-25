/*
 * JARVIS AI-OS Phase 3 — Llama Model Weight Loading
 *
 * Loads Llama model config and weights from GGUF files. Reads metadata
 * for architecture params, allocates weight arrays, dequantizes tensors
 * to float32 for inference.
 *
 * Tensor name mapping (Llama GGUF convention):
 *   token_embd.weight             -> token_embed
 *   output_norm.weight            -> output_norm
 *   output.weight                 -> output_weight
 *   blk.N.attn_norm.weight        -> layers[N].attn_norm
 *   blk.N.attn_q.weight           -> layers[N].wq
 *   blk.N.attn_k.weight           -> layers[N].wk
 *   blk.N.attn_v.weight           -> layers[N].wv
 *   blk.N.attn_output.weight      -> layers[N].wo
 *   blk.N.ffn_norm.weight         -> layers[N].ffn_norm
 *   blk.N.ffn_gate.weight         -> layers[N].w_gate
 *   blk.N.ffn_up.weight           -> layers[N].w_up
 *   blk.N.ffn_down.weight         -> layers[N].w_down
 *
 * Pure C11, no C++ dependencies.
 */

#include "llama_model.h"
#include "gguf_parser.h"
#include "dequant.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Config Loading ---- */

int llama_load_config(llama_config_t *config, const gguf_ctx_t *ctx)
{
    if (!config || !ctx) return -1;

    memset(config, 0, sizeof(*config));

    uint32_t u32_val;
    float f32_val;

    /* Read embedding dimension */
    if (gguf_get_kv_u32(ctx, "llama.embedding_length", &u32_val))
        config->dim = (int)u32_val;
    else
        return -1;  /* dim is required */

    /* Read number of layers */
    if (gguf_get_kv_u32(ctx, "llama.block_count", &u32_val))
        config->n_layers = (int)u32_val;
    else
        return -1;  /* n_layers is required */

    /* Read attention head count */
    if (gguf_get_kv_u32(ctx, "llama.attention.head_count", &u32_val))
        config->n_heads = (int)u32_val;
    else
        return -1;  /* n_heads is required */

    /* Read KV head count (GQA) - default to n_heads if not present */
    if (gguf_get_kv_u32(ctx, "llama.attention.head_count_kv", &u32_val))
        config->n_kv_heads = (int)u32_val;
    else
        config->n_kv_heads = config->n_heads;

    /* Compute head dimension */
    if (config->n_heads <= 0) return -1;
    config->head_dim = config->dim / config->n_heads;

    /* Read FFN hidden dimension */
    if (gguf_get_kv_u32(ctx, "llama.feed_forward_length", &u32_val))
        config->hidden_dim = (int)u32_val;
    else
        return -1;  /* hidden_dim is required */

    /* Vocab size: try metadata first, fall back to token_embd tensor dims */
    if (gguf_get_kv_u32(ctx, "llama.vocab_size", &u32_val)) {
        config->vocab_size = (int)u32_val;
    } else {
        /* Extract from token_embd.weight tensor shape */
        const gguf_tensor_info_t *embd = gguf_find_tensor(ctx, "token_embd.weight");
        if (embd && embd->n_dims == 2) {
            config->vocab_size = (int)embd->dims[1];
        } else {
            return -1;  /* Cannot determine vocab_size */
        }
    }

    /* Context length / max sequence length */
    if (gguf_get_kv_u32(ctx, "llama.context_length", &u32_val))
        config->max_seq_len = (int)u32_val;
    else
        config->max_seq_len = LLAMA_MAX_SEQ_LEN;

    /* Cap at LLAMA_MAX_SEQ_LEN for memory safety */
    if (config->max_seq_len > LLAMA_MAX_SEQ_LEN)
        config->max_seq_len = LLAMA_MAX_SEQ_LEN;

    /* RoPE theta - default 500000.0 if not specified */
    if (gguf_get_kv_f32(ctx, "llama.rope.freq_base", &f32_val))
        config->rope_theta = f32_val;
    else
        config->rope_theta = 500000.0f;

    return 0;
}

/* ---- Model Allocation ---- */

int llama_alloc_model(llama_model_t *model)
{
    if (!model) return -1;

    const llama_config_t *c = &model->config;
    if (c->dim <= 0 || c->n_layers <= 0 || c->vocab_size <= 0) return -1;

    model->total_bytes = 0;
    model->loaded = false;

    int dim       = c->dim;
    int n_layers  = c->n_layers;
    int n_heads   = c->n_heads;
    int n_kv_heads = c->n_kv_heads;
    int head_dim  = c->head_dim;
    int hidden_dim = c->hidden_dim;
    int vocab_size = c->vocab_size;

    /* Token embedding: vocab_size * dim */
    size_t embed_sz = (size_t)vocab_size * (size_t)dim * sizeof(float);
    model->token_embed = (float *)malloc(embed_sz);
    if (!model->token_embed) goto fail;
    model->total_bytes += embed_sz;

    /* Output norm: dim */
    size_t norm_sz = (size_t)dim * sizeof(float);
    model->output_norm = (float *)malloc(norm_sz);
    if (!model->output_norm) goto fail;
    model->total_bytes += norm_sz;

    /* Output weight: vocab_size * dim */
    size_t out_sz = (size_t)vocab_size * (size_t)dim * sizeof(float);
    model->output_weight = (float *)malloc(out_sz);
    if (!model->output_weight) goto fail;
    model->total_bytes += out_sz;

    /* Allocate layers array */
    model->layers = (llama_layer_t *)calloc((size_t)n_layers, sizeof(llama_layer_t));
    if (!model->layers) goto fail;
    model->total_bytes += (size_t)n_layers * sizeof(llama_layer_t);

    /* Allocate per-layer weights */
    for (int i = 0; i < n_layers; i++) {
        llama_layer_t *L = &model->layers[i];

        /* attn_norm: dim */
        L->attn_norm = (float *)malloc(norm_sz);
        if (!L->attn_norm) goto fail;
        model->total_bytes += norm_sz;

        /* wq: n_heads * head_dim * dim = dim * dim */
        size_t wq_sz = (size_t)n_heads * (size_t)head_dim * (size_t)dim * sizeof(float);
        L->wq = (float *)malloc(wq_sz);
        if (!L->wq) goto fail;
        model->total_bytes += wq_sz;

        /* wk: n_kv_heads * head_dim * dim */
        size_t wk_sz = (size_t)n_kv_heads * (size_t)head_dim * (size_t)dim * sizeof(float);
        L->wk = (float *)malloc(wk_sz);
        if (!L->wk) goto fail;
        model->total_bytes += wk_sz;

        /* wv: n_kv_heads * head_dim * dim */
        size_t wv_sz = wk_sz;
        L->wv = (float *)malloc(wv_sz);
        if (!L->wv) goto fail;
        model->total_bytes += wv_sz;

        /* wo: dim * n_heads * head_dim = dim * dim */
        size_t wo_sz = (size_t)dim * (size_t)n_heads * (size_t)head_dim * sizeof(float);
        L->wo = (float *)malloc(wo_sz);
        if (!L->wo) goto fail;
        model->total_bytes += wo_sz;

        /* ffn_norm: dim */
        L->ffn_norm = (float *)malloc(norm_sz);
        if (!L->ffn_norm) goto fail;
        model->total_bytes += norm_sz;

        /* w_gate: hidden_dim * dim */
        size_t gate_sz = (size_t)hidden_dim * (size_t)dim * sizeof(float);
        L->w_gate = (float *)malloc(gate_sz);
        if (!L->w_gate) goto fail;
        model->total_bytes += gate_sz;

        /* w_up: hidden_dim * dim */
        size_t up_sz = gate_sz;
        L->w_up = (float *)malloc(up_sz);
        if (!L->w_up) goto fail;
        model->total_bytes += up_sz;

        /* w_down: dim * hidden_dim */
        size_t down_sz = (size_t)dim * (size_t)hidden_dim * sizeof(float);
        L->w_down = (float *)malloc(down_sz);
        if (!L->w_down) goto fail;
        model->total_bytes += down_sz;
    }

    return 0;

fail:
    llama_free_model(model);
    return -1;
}

/* ---- Helper: Load One Tensor ---- */

static int load_tensor_f32(gguf_ctx_t *ctx, const char *name, float *dst, int n_elements)
{
    const gguf_tensor_info_t *t = gguf_find_tensor(ctx, name);
    if (!t) {
        fprintf(stderr, "llama_load: tensor '%s' not found\n", name);
        return -1;
    }

    /* Check element count matches expectation */
    if ((int)t->n_elements != n_elements) {
        fprintf(stderr, "llama_load: tensor '%s' has %lu elements, expected %d\n",
                name, (unsigned long)t->n_elements, n_elements);
        return -1;
    }

    /* If already F32, read directly */
    if (t->type == GGML_TYPE_F32) {
        return gguf_read_tensor_data(ctx, t, dst);
    }

    /* Allocate temp buffer for quantized data */
    void *tmp = malloc((size_t)t->n_bytes);
    if (!tmp) return -1;

    int err = gguf_read_tensor_data(ctx, t, tmp);
    if (err) {
        free(tmp);
        return err;
    }

    /* Dequantize to float32 */
    err = dequant_row(tmp, dst, n_elements, (ggml_type_t)t->type);
    free(tmp);
    if (err != 0) {
        fprintf(stderr, "llama_load: dequant failed for '%s' (type %s, err %d)\n",
                name, gguf_type_name(t->type), err);
        return -1;
    }

    return 0;
}

/* ---- Weight Loading ---- */

int llama_load_weights(llama_model_t *model, gguf_ctx_t *ctx)
{
    if (!model || !ctx) return -1;

    const llama_config_t *c = &model->config;
    int dim        = c->dim;
    int n_layers   = c->n_layers;
    int n_heads    = c->n_heads;
    int n_kv_heads = c->n_kv_heads;
    int head_dim   = c->head_dim;
    int hidden_dim = c->hidden_dim;
    int vocab_size = c->vocab_size;
    int err;

    /* Token embedding */
    err = load_tensor_f32(ctx, "token_embd.weight", model->token_embed,
                          vocab_size * dim);
    if (err) return err;

    /* Output norm */
    err = load_tensor_f32(ctx, "output_norm.weight", model->output_norm, dim);
    if (err) return err;

    /* Output weight (LM head) — may not exist if model uses weight tying */
    {
        const gguf_tensor_info_t *out_t = gguf_find_tensor(ctx, "output.weight");
        if (out_t) {
            err = load_tensor_f32(ctx, "output.weight", model->output_weight,
                                  vocab_size * dim);
            if (err) return err;
        } else {
            /* Weight tying: output projection shares token embedding */
            memcpy(model->output_weight, model->token_embed,
                   (size_t)vocab_size * (size_t)dim * sizeof(float));
        }
    }

    /* RoPE frequencies (optional — custom freqs for extended context models) */
    {
        const gguf_tensor_info_t *rf = gguf_find_tensor(ctx, "rope_freqs.weight");
        if (rf && rf->type == GGML_TYPE_F32 && rf->n_elements == (uint64_t)(head_dim / 2)) {
            model->rope_freqs = (float *)malloc(rf->n_bytes);
            if (model->rope_freqs) {
                gguf_read_tensor_data(ctx, rf, model->rope_freqs);
                model->total_bytes += rf->n_bytes;
            }
        }
    }

    /* Per-layer weights */
    char name[128];
    for (int i = 0; i < n_layers; i++) {
        llama_layer_t *L = &model->layers[i];

        snprintf(name, sizeof(name), "blk.%d.attn_norm.weight", i);
        err = load_tensor_f32(ctx, name, L->attn_norm, dim);
        if (err) return err;

        snprintf(name, sizeof(name), "blk.%d.attn_q.weight", i);
        err = load_tensor_f32(ctx, name, L->wq, n_heads * head_dim * dim);
        if (err) return err;

        snprintf(name, sizeof(name), "blk.%d.attn_k.weight", i);
        err = load_tensor_f32(ctx, name, L->wk, n_kv_heads * head_dim * dim);
        if (err) return err;

        snprintf(name, sizeof(name), "blk.%d.attn_v.weight", i);
        err = load_tensor_f32(ctx, name, L->wv, n_kv_heads * head_dim * dim);
        if (err) return err;

        snprintf(name, sizeof(name), "blk.%d.attn_output.weight", i);
        err = load_tensor_f32(ctx, name, L->wo, dim * n_heads * head_dim);
        if (err) return err;

        snprintf(name, sizeof(name), "blk.%d.ffn_norm.weight", i);
        err = load_tensor_f32(ctx, name, L->ffn_norm, dim);
        if (err) return err;

        snprintf(name, sizeof(name), "blk.%d.ffn_gate.weight", i);
        err = load_tensor_f32(ctx, name, L->w_gate, hidden_dim * dim);
        if (err) return err;

        snprintf(name, sizeof(name), "blk.%d.ffn_up.weight", i);
        err = load_tensor_f32(ctx, name, L->w_up, hidden_dim * dim);
        if (err) return err;

        snprintf(name, sizeof(name), "blk.%d.ffn_down.weight", i);
        err = load_tensor_f32(ctx, name, L->w_down, dim * hidden_dim);
        if (err) return err;
    }

    model->loaded = true;
    return 0;
}

/* ---- Full Model Load (Open + Config + Alloc + Weights + Close) ---- */

int llama_load_model(llama_model_t *model, const char *gguf_path)
{
    if (!model || !gguf_path) return -1;

    memset(model, 0, sizeof(*model));

    gguf_ctx_t ctx;
    int err = gguf_open(&ctx, gguf_path);
    if (err) {
        fprintf(stderr, "llama_load_model: gguf_open failed: %s\n",
                gguf_strerror(err));
        return err;
    }

    err = llama_load_config(&model->config, &ctx);
    if (err) {
        fprintf(stderr, "llama_load_model: config load failed\n");
        gguf_close(&ctx);
        return err;
    }

    err = llama_alloc_model(model);
    if (err) {
        fprintf(stderr, "llama_load_model: alloc failed\n");
        gguf_close(&ctx);
        return err;
    }

    err = llama_load_weights(model, &ctx);
    if (err) {
        fprintf(stderr, "llama_load_model: weight load failed\n");
        llama_free_model(model);
        gguf_close(&ctx);
        return err;
    }

    gguf_close(&ctx);
    return 0;
}

/* ---- Free Model ---- */

void llama_free_model(llama_model_t *model)
{
    if (!model) return;

    if (model->layers) {
        for (int i = 0; i < model->config.n_layers; i++) {
            llama_layer_t *L = &model->layers[i];
            free(L->attn_norm);
            free(L->wq);
            free(L->wk);
            free(L->wv);
            free(L->wo);
            free(L->ffn_norm);
            free(L->w_gate);
            free(L->w_up);
            free(L->w_down);
        }
        free(model->layers);
    }

    free(model->token_embed);
    free(model->output_norm);
    free(model->output_weight);
    free(model->rope_freqs);

    memset(model, 0, sizeof(*model));
}

/* ---- State Allocation ---- */

int llama_alloc_state(llama_state_t *state, const llama_config_t *config)
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

    state->max_seq_len = max_seq;
    state->pos = 0;

    /* Activation buffers */
    state->x      = (float *)calloc((size_t)dim, sizeof(float));
    state->xb     = (float *)calloc((size_t)dim, sizeof(float));
    state->xb2    = (float *)calloc((size_t)dim, sizeof(float));
    state->q      = (float *)calloc((size_t)n_heads * (size_t)head_dim, sizeof(float));
    state->k      = (float *)calloc((size_t)n_kv_heads * (size_t)head_dim, sizeof(float));
    state->v      = (float *)calloc((size_t)n_kv_heads * (size_t)head_dim, sizeof(float));
    state->att    = (float *)calloc((size_t)n_heads * (size_t)max_seq, sizeof(float));
    state->hb     = (float *)calloc((size_t)hidden_dim, sizeof(float));
    state->hb2    = (float *)calloc((size_t)hidden_dim, sizeof(float));
    state->logits = (float *)calloc((size_t)vocab_size, sizeof(float));

    /* KV cache: n_layers * max_seq_len * n_kv_heads * head_dim */
    size_t kv_size = (size_t)config->n_layers * (size_t)max_seq *
                     (size_t)n_kv_heads * (size_t)head_dim;
    state->key_cache   = (float *)calloc(kv_size, sizeof(float));
    state->value_cache = (float *)calloc(kv_size, sizeof(float));

    /* Verify all allocations succeeded */
    if (!state->x || !state->xb || !state->xb2 ||
        !state->q || !state->k || !state->v ||
        !state->att || !state->hb || !state->hb2 ||
        !state->logits || !state->key_cache || !state->value_cache) {
        llama_free_state(state);
        return -1;
    }

    return 0;
}

/* ---- Free State ---- */

void llama_free_state(llama_state_t *state)
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
    free(state->value_cache);

    memset(state, 0, sizeof(*state));
}
