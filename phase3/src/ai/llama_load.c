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

/* ---- Config Loading (arch-aware: reads general.architecture, dispatches {arch}.* keys) ---- */

int llama_load_config(llama_config_t *config, const gguf_ctx_t *ctx)
{
    if (!config || !ctx) { printf("[config] FAIL: null arg\n"); return -1; }
    memset(config, 0, sizeof(*config));

    /* Determine architecture prefix */
    const char *arch = gguf_get_kv_string(ctx, "general.architecture");
    printf("[config] arch='%s' n_kv=%llu n_tensors=%llu\n",
           arch ? arch : "(null)",
           (unsigned long long)ctx->n_kv,
           (unsigned long long)ctx->n_tensors);
    if (!arch) arch = "llama";  /* backward compat */

    /* Gemma variants: scale embeddings by sqrt(dim), use GeGLU (not SwiGLU) */
    config->embed_scale = (strncmp(arch, "gemma", 5) == 0);
    config->use_gelu    = (strncmp(arch, "gemma", 5) == 0);

    char key[256];
    uint32_t u32_val;
    float f32_val;

    /* Required: embedding dimension */
    snprintf(key, sizeof(key), "%s.embedding_length", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->dim = (int)u32_val;
    else
        { printf("[config] FAIL: %s not found\n", key); return -1; }
    printf("[config] dim=%d\n", config->dim);

    /* Required: block count */
    snprintf(key, sizeof(key), "%s.block_count", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->n_layers = (int)u32_val;
    else
        { printf("[config] FAIL: %s not found\n", key); return -1; }
    if (config->n_layers <= 0 || config->n_layers > LLAMA_MAX_LAYERS)
        { printf("[config] FAIL: n_layers=%d out of range\n", config->n_layers); return -1; }

    printf("[config] n_layers=%d\n", config->n_layers);

    /* Required: attention head count */
    snprintf(key, sizeof(key), "%s.attention.head_count", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->n_heads = (int)u32_val;
    else
        { printf("[config] FAIL: %s not found\n", key); return -1; }

    /* Optional: KV head count (GQA) — defaults to n_heads */
    snprintf(key, sizeof(key), "%s.attention.head_count_kv", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->n_kv_heads = (int)u32_val;
    else
        config->n_kv_heads = config->n_heads;

    printf("[config] n_heads=%d n_kv_heads=%d\n", config->n_heads, config->n_kv_heads);

    /* Explicit head_dim if available (Gemma 4, Qwen3), else derive */
    snprintf(key, sizeof(key), "%s.attention.key_length", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->head_dim = (int)u32_val;
    else if (config->n_heads > 0)
        config->head_dim = config->dim / config->n_heads;
    else
        { printf("[config] FAIL: head_dim derivation (n_heads=0)\n"); return -1; }

    printf("[config] head_dim=%d\n", config->head_dim);

    /* Required: FFN hidden dimension (scalar — per-layer arrays handled in Task 2) */
    snprintf(key, sizeof(key), "%s.feed_forward_length", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->hidden_dim = (int)u32_val;
    else {
        /* feed_forward_length may be an array for Gemma 4 — gguf_get_kv_u32 returns false.
         * For Gemma 4, default to 0 and let per-layer loading (Task 2) fill it in.
         * For Llama models, hidden_dim is required. */
        if (strcmp(arch, "gemma4") != 0)
            { printf("[config] FAIL: %s not found (arch=%s)\n", key, arch); return -1; }
        printf("[config] feed_forward_length is array (gemma4), hidden_dim=0 for now\n");
    }

    /* Vocab size: try metadata, fall back to token_embd tensor shape */
    snprintf(key, sizeof(key), "%s.vocab_size", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val)) {
        config->vocab_size = (int)u32_val;
    } else {
        const gguf_tensor_info_t *embd = gguf_find_tensor(ctx, "token_embd.weight");
        if (embd && embd->n_dims == 2)
            config->vocab_size = (int)embd->dims[1];
        else
            { printf("[config] FAIL: vocab_size not found (no key, no tensor)\n"); return -1; }
    }
    printf("[config] vocab_size=%d hidden_dim=%d\n", config->vocab_size, config->hidden_dim);

    /* Context length */
    snprintf(key, sizeof(key), "%s.context_length", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->max_seq_len = (int)u32_val;
    else
        config->max_seq_len = LLAMA_MAX_SEQ_LEN;
    if (config->max_seq_len > LLAMA_MAX_SEQ_LEN)
        config->max_seq_len = LLAMA_MAX_SEQ_LEN;

    /* RoPE theta */
    snprintf(key, sizeof(key), "%s.rope.freq_base", arch);
    if (gguf_get_kv_f32(ctx, key, &f32_val))
        config->rope_theta = f32_val;
    else
        config->rope_theta = 500000.0f;

    /* --- Gemma 4 / extended fields (all optional, default 0) --- */

    snprintf(key, sizeof(key), "%s.attention.layer_norm_rms_epsilon", arch);
    gguf_get_kv_f32(ctx, key, &config->rms_norm_eps);

    snprintf(key, sizeof(key), "%s.final_logit_softcapping", arch);
    gguf_get_kv_f32(ctx, key, &config->logit_softcap);

    snprintf(key, sizeof(key), "%s.rope.freq_base_swa", arch);
    gguf_get_kv_f32(ctx, key, &config->rope_theta_swa);

    snprintf(key, sizeof(key), "%s.embedding_length_per_layer_input", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->ple_dim = (int)u32_val;

    snprintf(key, sizeof(key), "%s.attention.sliding_window", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->swa_window = (int)u32_val;

    snprintf(key, sizeof(key), "%s.attention.key_length_swa", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->head_dim_swa = (int)u32_val;

    snprintf(key, sizeof(key), "%s.attention.shared_kv_layers", arch);
    if (gguf_get_kv_u32(ctx, key, &u32_val))
        config->shared_kv_layers = (int)u32_val;

    printf("[config] extensions: ple=%d swa=%d swa_hd=%d shared_kv=%d softcap=%.1f eps=%g theta_swa=%.0f\n",
           config->ple_dim, config->swa_window, config->head_dim_swa,
           config->shared_kv_layers, config->logit_softcap,
           config->rms_norm_eps, config->rope_theta_swa);

    /* --- Build per-layer arrays --- */
    int nl = config->n_layers;
    /* nl range already validated above, but be safe */
    if (nl <= 0 || nl > LLAMA_MAX_LAYERS) return -1;

    /* SWA layer pattern: mark which layers use sliding-window vs global attention.
     * TODO: Read actual pattern from gemma4.attention.sliding_window_pattern
     *       GGUF array once the parser supports array element extraction.
     * Default pattern: every 5th layer (index % 5 == 4) is global, rest are SWA.
     * Gemma 4 E2B: layers 4,9,14,19,24,29,34 are global; 28 SWA + 7 global = 35. */
    if (config->swa_window > 0) {
        config->layer_is_swa = (bool *)calloc((size_t)nl, sizeof(bool));
        if (!config->layer_is_swa) return -1;
        for (int i = 0; i < nl; i++)
            config->layer_is_swa[i] = ((i % 5) != 4);
    }

    /* KV share map: first n_unique layers compute own KV, rest share.
     * TODO: Verify exact sharing pattern against llama.cpp (attention-type
     *       matching may differ from simple modular index). */
    if (config->shared_kv_layers > 0) {
        config->kv_share_map = (int *)malloc((size_t)nl * sizeof(int));
        if (!config->kv_share_map) return -1;
        int n_unique = nl - config->shared_kv_layers;
        if (n_unique <= 0) n_unique = 1; /* safety */
        for (int i = 0; i < nl; i++) {
            if (i < n_unique)
                config->kv_share_map[i] = -1;   /* compute own KV */
            else
                config->kv_share_map[i] = i % n_unique; /* share from earlier layer */
        }
    }

    /* Per-layer FFN dim: for models with variable FFN sizes per layer.
     * TODO: Read actual values from gemma4.feed_forward_length GGUF array
     *       once the parser supports array element extraction.
     * Heuristic: first n_unique layers use smaller FFN (6144),
     *            remaining layers use larger FFN (12288).
     * Also set hidden_dim to the max for buffer allocation purposes. */
    if (config->hidden_dim == 0 && config->shared_kv_layers > 0) {
        config->layer_ffn_dim = (int *)malloc((size_t)nl * sizeof(int));
        if (!config->layer_ffn_dim) return -1;
        int n_unique = nl - config->shared_kv_layers;
        if (n_unique <= 0) n_unique = 1;
        for (int i = 0; i < nl; i++) {
            if (i < n_unique)
                config->layer_ffn_dim[i] = 6144;   /* smaller FFN for early layers */
            else
                config->layer_ffn_dim[i] = 12288;  /* larger FFN for later layers */
        }
        /* Set hidden_dim to max for buffer allocation */
        config->hidden_dim = 12288;
    }

    printf("[config] OK: dim=%d layers=%d heads=%d/%d hd=%d/%d ffn=%d vocab=%d\n",
           config->dim, config->n_layers, config->n_heads, config->n_kv_heads,
           config->head_dim, config->head_dim_swa, config->hidden_dim, config->vocab_size);
    return 0;
}

/* ---- Config Cleanup ---- */

void llama_free_config(llama_config_t *config)
{
    if (!config) return;
    free(config->layer_ffn_dim);
    free(config->layer_is_swa);
    free(config->kv_share_map);
    config->layer_ffn_dim = NULL;
    config->layer_is_swa = NULL;
    config->kv_share_map = NULL;
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

    llama_free_config(&model->config);
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

    /* For Gemma 4: SWA and global layers have different head_dim.
     * Allocate buffers using the MAX head_dim to fit either layer type.
     * For Llama: head_dim_swa is 0, so max_head_dim == head_dim (no change). */
    int max_head_dim = head_dim;
    if (config->head_dim_swa > 0 && config->head_dim_swa > max_head_dim)
        max_head_dim = config->head_dim_swa;

    state->max_seq_len = max_seq;
    state->pos = 0;

    /* xb/xb2 must fit the largest attention output: n_heads * max_head_dim.
     * For Gemma 4: 8*512=4096 > dim=1536, so xb must be larger than dim.
     * For Llama: n_heads*head_dim == dim, so xb_size == dim (unchanged). */
    int xb_size = dim;
    if (n_heads * max_head_dim > xb_size)
        xb_size = n_heads * max_head_dim;

    /* Activation buffers */
    state->x      = (float *)calloc((size_t)dim, sizeof(float));
    state->xb     = (float *)calloc((size_t)xb_size, sizeof(float));
    state->xb2    = (float *)calloc((size_t)xb_size, sizeof(float));
    state->q      = (float *)calloc((size_t)n_heads * (size_t)max_head_dim, sizeof(float));
    state->k      = (float *)calloc((size_t)n_kv_heads * (size_t)max_head_dim, sizeof(float));
    state->v      = (float *)calloc((size_t)n_kv_heads * (size_t)max_head_dim, sizeof(float));
    state->att    = (float *)calloc((size_t)n_heads * (size_t)max_seq, sizeof(float));
    state->hb     = (float *)calloc((size_t)hidden_dim, sizeof(float));
    state->hb2    = (float *)calloc((size_t)hidden_dim, sizeof(float));
    state->logits = (float *)calloc((size_t)vocab_size, sizeof(float));

    /* KV cache: use max kv_dim per layer for uniform cache stride.
     * SWA layers (smaller kv_dim) only use a prefix of each slot.
     *
     * TODO: With shared KV (Gemma 4), only n_unique_kv layers need cache slots
     * (n_layers - shared_kv_layers). Shared layers reuse the source layer's
     * slot via kv_share_map indexing, so their allocated slots go unused.
     * Shrinking to n_unique_kv slots would save ~57% KV memory for Gemma 4 E2B,
     * but requires remapping kv_share_map indices to compacted slot indices.
     * Keeping full allocation for now — correctness first. */
    int max_kv_dim = n_kv_heads * max_head_dim;
    size_t kv_size = (size_t)config->n_layers * (size_t)max_seq *
                     (size_t)max_kv_dim;
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
