/*
 * JARVIS AI-OS Phase 3 — Gated DeltaNet SSM Implementation
 *
 * Single-token autoregressive forward pass for the Gated DeltaNet linear
 * attention layer used in Qwen3.5 hybrid models.
 *
 * Reference: "Gated Delta Networks" (arXiv:2412.06464)
 * Implementation verified against llama.cpp qwen35.cpp source.
 *
 * Pure C11, no C++ dependencies.
 */

#include "ssm.h"
#include <math.h>
#include <string.h>

/* ---- Activation functions ---- */

static float silu_f(float x)
{
    return x / (1.0f + expf(-x));
}

static float sigmoid_f(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

static float softplus_f(float x)
{
    /* softplus(x) = log(1 + exp(x)), with overflow protection */
    if (x > 20.0f) return x;
    return logf(1.0f + expf(x));
}

/* ---- L2 normalize a vector in-place ---- */

static void l2_normalize(float *vec, int n, float eps)
{
    float sum_sq = 0.0f;
    for (int i = 0; i < n; i++)
        sum_sq += vec[i] * vec[i];
    float inv_norm = 1.0f / sqrtf(sum_sq + eps);
    for (int i = 0; i < n; i++)
        vec[i] *= inv_norm;
}

/* ---- Depthwise causal conv1d ---- */

void deltanet_conv1d(float *qkv, float *conv_state, const float *kernel,
                     int d_conv, int channels)
{
    int state_rows = d_conv - 1;  /* stored history rows */

    /* For each channel independently:
     *   1. Convolve: state[0..state_rows-1] + current token = d_conv values
     *   2. Apply SiLU
     *   3. Shift state left by 1, insert current token at end */

    for (int c = 0; c < channels; c++) {
        float sum = 0.0f;

        /* Convolution: kernel[c][t] * input[t] for t in [0, d_conv)
         * GGUF stores kernel as [channels][d_conv] (dim[0]=d_conv is inner/fastest).
         * State is stored as [state_rows][channels] (our own layout). */
        for (int t = 0; t < state_rows; t++)
            sum += conv_state[t * channels + c] * kernel[c * d_conv + t];
        sum += qkv[c] * kernel[c * d_conv + state_rows];

        /* Shift state: drop oldest (row 0), append current token */
        for (int t = 0; t < state_rows - 1; t++)
            conv_state[t * channels + c] = conv_state[(t + 1) * channels + c];
        conv_state[(state_rows - 1) * channels + c] = qkv[c];

        /* SiLU activation on convolution output */
        qkv[c] = silu_f(sum);
    }
}

/* ---- Gated DeltaNet forward pass ---- */

void deltanet_forward(const float *q, const float *k, const float *v,
                      const float *z,
                      const float *alpha_raw, const float *beta_raw,
                      const float *ssm_a, const float *dt_bias,
                      const float *norm_w, float norm_eps,
                      float *ssm_state, float *out,
                      int num_k_heads, int num_v_heads,
                      int head_k_dim, int head_v_dim)
{
    /* Working buffers on stack — safe since head dims are bounded (max 256) */
    float q_norm[256], k_norm[256];
    float pred[256], delta[256];

    /* Heads-per-group ratio (GQA-style: multiple V-heads share one K-head) */
    /* For Qwen3.5 4B: num_v_heads=32, num_k_heads=16, ratio=2 */
    int v_per_k = num_v_heads / num_k_heads;

    /* Step 1: Discretize gates
     *   gate[h] = exp(softplus(alpha_raw[h] + dt_bias[h]) * ssm_a[h])
     *   beta[h] = sigmoid(beta_raw[h])
     * ssm_a values are negative, so gate = exp(negative) ∈ (0, 1) */
    float gate[64], beta[64];  /* max num_v_heads */
    for (int h = 0; h < num_v_heads; h++) {
        float sp = softplus_f(alpha_raw[h] + dt_bias[h]);
        gate[h] = expf(sp * ssm_a[h]);  /* ssm_a < 0, so product is negative */
        beta[h] = sigmoid_f(beta_raw[h]);
    }

    /* Step 2: For each V-head, compute the delta-rule recurrence */
    for (int vh = 0; vh < num_v_heads; vh++) {
        /* Map V-head to K-head (GQA grouping) */
        int kh = vh / v_per_k;

        /* L2 normalize Q and K for this head (copy first — don't modify input) */
        memcpy(q_norm, q + kh * head_k_dim, (size_t)head_k_dim * sizeof(float));
        memcpy(k_norm, k + kh * head_k_dim, (size_t)head_k_dim * sizeof(float));
        l2_normalize(q_norm, head_k_dim, norm_eps);
        l2_normalize(k_norm, head_k_dim, norm_eps);

        float *S = ssm_state + (size_t)vh * head_k_dim * head_v_dim;
        const float *v_h = v + vh * head_v_dim;
        float *out_h = out + vh * head_v_dim;

        /* 2a. Decay: S *= gate[vh] */
        float g = gate[vh];
        for (int i = 0; i < head_k_dim * head_v_dim; i++)
            S[i] *= g;

        /* 2b. Predict: pred = S^T @ k  → [head_v_dim]
         *     S is [head_k_dim × head_v_dim], so S^T is [head_v_dim × head_k_dim]
         *     S^T @ k = sum over i of S[i][j] * k[i] for each j */
        for (int j = 0; j < head_v_dim; j++) {
            float dot = 0.0f;
            for (int i = 0; i < head_k_dim; i++)
                dot += S[i * head_v_dim + j] * k_norm[i];
            pred[j] = dot;
        }

        /* 2c. Delta correction: delta = beta[vh] * (v - pred)  → [head_v_dim] */
        float b = beta[vh];
        for (int j = 0; j < head_v_dim; j++)
            delta[j] = b * (v_h[j] - pred[j]);

        /* 2d. Write: S += k @ delta^T (outer product)
         *     S[i][j] += k[i] * delta[j] */
        for (int i = 0; i < head_k_dim; i++)
            for (int j = 0; j < head_v_dim; j++)
                S[i * head_v_dim + j] += k_norm[i] * delta[j];

        /* 2e. Read: out = S^T @ q  → [head_v_dim] */
        for (int j = 0; j < head_v_dim; j++) {
            float dot = 0.0f;
            for (int i = 0; i < head_k_dim; i++)
                dot += S[i * head_v_dim + j] * q_norm[i];
            out_h[j] = dot;
        }
    }

    /* Step 3: Per-group RMS norm on output
     * norm_w has [head_v_dim] weights — applied independently to each head's output.
     * (Same weight vector shared across all heads.) */
    for (int vh = 0; vh < num_v_heads; vh++) {
        float *out_h = out + vh * head_v_dim;
        float sum_sq = 0.0f;
        for (int j = 0; j < head_v_dim; j++)
            sum_sq += out_h[j] * out_h[j];
        float rms = sqrtf(sum_sq / head_v_dim + norm_eps);
        for (int j = 0; j < head_v_dim; j++)
            out_h[j] = (out_h[j] / rms) * norm_w[j];
    }

    /* Step 4: Output gating — out = out * silu(z)
     * z has [num_v_heads * head_v_dim] values (same layout as out) */
    int total_v = num_v_heads * head_v_dim;
    for (int i = 0; i < total_v; i++)
        out[i] *= silu_f(z[i]);
}
