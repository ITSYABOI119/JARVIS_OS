/*
 * JARVIS AI-OS Phase 3 — SSM / Gated DeltaNet Forward Pass
 *
 * Implements the recurrent linear attention layer used in Qwen3.5 hybrid models.
 * Single-token autoregressive mode only (no parallel scan needed).
 *
 * Pure C11, no C++ dependencies.
 */

#ifndef SSM_H
#define SSM_H

#include <stddef.h>

/*
 * Depthwise causal conv1d with sliding window state.
 *
 * For each of `channels` independent channels, convolves `d_conv` values
 * (d_conv-1 from stored state + 1 current) with the kernel, applies SiLU.
 *
 * @param qkv        [channels] — current token's QKV values (input AND output: modified in-place)
 * @param conv_state  [d_conv-1][channels] — sliding window state (updated in-place)
 * @param kernel      [d_conv][channels] — depthwise conv weights (F32)
 * @param d_conv      Convolution kernel width (typically 4)
 * @param channels    Number of independent channels (QKV_dim, e.g. 8192)
 */
void deltanet_conv1d(float *qkv, float *conv_state, const float *kernel,
                     int d_conv, int channels);

/*
 * Full Gated DeltaNet forward pass for one token.
 *
 * Implements the recurrence:
 *   S[h] = gate[h] * S[h] + beta[h] * (v[h] - S[h]^T @ k[h]) @ k[h]^T
 *   out[h] = S[h]^T @ q[h]
 *
 * With L2 normalization on Q/K, discretized gates, and SiLU output gating.
 *
 * @param q           [num_k_heads * head_k_dim] — query after conv1d
 * @param k           [num_k_heads * head_k_dim] — key after conv1d
 * @param v           [num_v_heads * head_v_dim] — value after conv1d
 * @param z           [num_v_heads * head_v_dim] — output gate (SiLU applied)
 * @param alpha_raw   [num_v_heads] — alpha projection output (before discretization)
 * @param beta_raw    [num_v_heads] — beta projection output (before sigmoid)
 * @param ssm_a       [num_v_heads] — A_log decay rate parameters (negative F32 values)
 * @param dt_bias     [num_v_heads] — timestep bias (F32)
 * @param norm_w      [head_v_dim] — RMS norm weights for output
 * @param norm_eps    RMS norm epsilon
 * @param ssm_state   [num_v_heads * head_k_dim * head_v_dim] — recurrent state (updated in-place)
 * @param out         [num_v_heads * head_v_dim] — output (written)
 * @param num_k_heads Number of Q/K heads (16 for Qwen3.5 4B)
 * @param num_v_heads Number of V/output heads (32 for Qwen3.5 4B)
 * @param head_k_dim  Q/K dimension per head (128)
 * @param head_v_dim  V/output dimension per head (128)
 */
void deltanet_forward(const float *q, const float *k, const float *v,
                      const float *z,
                      const float *alpha_raw, const float *beta_raw,
                      const float *ssm_a, const float *dt_bias,
                      const float *norm_w, float norm_eps,
                      float *ssm_state, float *out,
                      int num_k_heads, int num_v_heads,
                      int head_k_dim, int head_v_dim);

#endif /* SSM_H */
