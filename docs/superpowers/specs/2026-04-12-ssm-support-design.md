# Qwen3.5 Hybrid Architecture Support — Gated DeltaNet + Full Attention

**Date:** 2026-04-12
**Status:** Approved for implementation

## Goal

Support Qwen3.5 hybrid architecture (4B and 9B models). 24 of 32 layers use
**Gated DeltaNet** (linear attention with recurrent state). 8 of 32 layers use
**gated full attention** (standard GQA with sigmoid output gate). Unlocks the
last 2 of 11 bench models.

## Architecture Overview

Qwen3.5 interleaves two layer types in a 3:1 pattern:
- **DeltaNet layers** (0,1,2, 4,5,6, 8,9,10, ...): recurrent linear attention
- **Full attention layers** (3, 7, 11, 15, 19, 23, 27, 31): standard GQA + gate

Detection: `full_attention_interval = 4` from GGUF metadata, or detect from
tensor presence (`ssm_conv1d` → DeltaNet, `attn_k` → full attention).

## Verified Dimensions (from GGUF metadata + tensor dump)

### GGUF Metadata Keys (Qwen3.5 4B)
```
qwen35.block_count = 32
qwen35.attention.head_count = 16          (n_heads for full attention)
qwen35.attention.head_count_kv = 4        (n_kv_heads for full attention)
qwen35.attention.key_length = 256         (head_dim for full attention)
qwen35.attention.value_length = 256       (head_dim for full attention)
qwen35.ssm.conv_kernel = 4               (d_conv)
qwen35.ssm.state_size = 128              (head_k_dim — Q/K dim per SSM head)
qwen35.ssm.group_count = 16              (num_k_heads — Q/K head count)
qwen35.ssm.time_step_rank = 32           (num_v_heads — V/output head count)
qwen35.ssm.inner_size = 4096             (d_inner = V_dim = num_v_heads * head_v_dim)
qwen35.full_attention_interval = 4
```

### Derived Dimensions
```
head_v_dim = d_inner / num_v_heads = 4096 / 32 = 128
Q_dim = num_k_heads * head_k_dim = 16 * 128 = 2048
K_dim = num_k_heads * head_k_dim = 16 * 128 = 2048
V_dim = num_v_heads * head_v_dim = 32 * 128 = 4096
QKV_dim = Q + K + V = 8192 (conv1d channel count)
```

### Tensor Inventory

**DeltaNet layer (e.g., layer 0):**
| GGUF tensor | Dims (4B) | Type | Role |
|-------------|-----------|------|------|
| `blk.0.attn_norm.weight` | (2560,) | F32 | Input RMS norm |
| `blk.0.attn_qkv.weight` | (2560, 8192) | Q5_K | Project → Q[2048]+K[2048]+V[4096] |
| `blk.0.attn_gate.weight` | (2560, 4096) | Q4_K | Z output gate projection |
| `blk.0.ssm_alpha.weight` | (2560, 32) | Q8_0 | Alpha gate projection (decay) |
| `blk.0.ssm_beta.weight` | (2560, 32) | Q8_0 | Beta gate projection (update strength) |
| `blk.0.ssm_conv1d.weight` | (4, 8192) | F32 | Depthwise conv1d kernel |
| `blk.0.ssm_a` | (32,) | F32 | A_log (log-space decay rates per head) |
| `blk.0.ssm_dt.bias` | (32,) | F32 | Timestep bias |
| `blk.0.ssm_norm.weight` | (128,) | F32 | Output RMS norm (per head_v_dim) |
| `blk.0.ssm_out.weight` | (4096, 2560) | Q5_K | Output projection |
| `blk.0.post_attention_norm.weight` | (2560,) | F32 | Post-attention sandwich norm |
| `blk.0.ffn_gate/up/down.weight` | standard | Q4/6_K | Standard SwiGLU FFN |

**Full attention layer (e.g., layer 3):**
| GGUF tensor | Dims (4B) | Type | Role |
|-------------|-----------|------|------|
| `blk.3.attn_norm.weight` | (2560,) | F32 | Input RMS norm |
| `blk.3.attn_q.weight` | (2560, 8192) | Q5_K | Q+Gate interleaved: 16 heads × 256 × 2 |
| `blk.3.attn_k.weight` | (2560, 1024) | Q5_K | K: 4 kv_heads × 256 |
| `blk.3.attn_v.weight` | (2560, 1024) | Q6_K | V: 4 kv_heads × 256 |
| `blk.3.attn_output.weight` | (4096, 2560) | Q5_K | Output projection |
| `blk.3.attn_q_norm.weight` | (256,) | F32 | Per-head Q RMS norm |
| `blk.3.attn_k_norm.weight` | (256,) | F32 | Per-head K RMS norm |
| `blk.3.post_attention_norm.weight` | (2560,) | F32 | Post-attention sandwich norm |
| `blk.3.ffn_gate/up/down.weight` | standard | Q4/6_K | Standard SwiGLU FFN |

## Part 1: Full Attention Layers (Gated Q)

**The problem:** `attn_q.weight` has 8192 rows = `n_heads * head_dim * 2`. The extra
factor of 2 is an interleaved output gate: `[Q_h0, Gate_h0, Q_h1, Gate_h1, ...]`.

**Solution:** Inference-time split. Do one `qmatmul_vec` producing [8192] output.
Split with stride: for head h, `q[h] = out[h*2*hd .. h*2*hd+hd-1]`,
`gate[h] = out[h*2*hd+hd .. h*2*hd+2*hd-1]`. After GQA attention produces
output[n_heads*head_dim], apply `output[i] *= sigmoid(gate[i])`.

Can't do load-time split because the interleaving is per-head, not contiguous blocks
— quantized data can't be deinterleaved without dequantizing.

**Existing code reuse:** K, V, Q/K norms, RoPE, GQA attention, post-norm — all
handled by existing `qmodel_forward()`. Only additions: larger Q buffer, stride
extraction, sigmoid gate.

## Part 2: DeltaNet Layers (Gated Delta Rule Recurrence)

### Forward Pass (single token, autoregressive)

```
Input: x[dim=2560]

1. xb = rms_norm(x, attn_norm)                          [2560]

2. qkv = attn_qkv @ xb                                  [8192]
   Split: Q[0..2047], K[2048..4095], V[4096..8191]

3. z = attn_gate @ xb                                    [4096]

4. alpha_raw = ssm_alpha @ xb                            [32]
   beta_raw  = ssm_beta @ xb                             [32]

5. Conv1d with state:
   - Shift conv_state left by 1, insert concat(Q,K,V)
   - Depthwise conv: out[c] = sum(conv_state[t,c] * kernel[t,c])
   - Apply SiLU activation
   - Split back: Q[2048], K[2048], V[4096]

6. L2-normalize Q and K per-head:
   - For each of 16 Q/K heads: normalize the 128-dim vector

7. Discretize gates:
   - gate[h] = exp(-softplus(alpha_raw[h] + dt_bias[h]) * exp(A_log[h]))
   - beta[h] = sigmoid(beta_raw[h])

8. Delta-rule recurrence (per V-head, 32 heads):
   For each head h with S[h] shape [128 × 128]:
     S[h] *= gate[h]                          (decay old state)
     pred = S[h]^T @ k[h_k]                   (what state predicts for this key)
     delta = beta[h] * (v[h] - pred)           (error correction)
     S[h] += k[h_k] outer delta               (write correction)
     out[h] = S[h]^T @ q[h_k]                 (read output)
   
   Note: num_v_heads=32 but num_k_heads=16, so the head mapping is
   h_k = h * num_k_heads / num_v_heads (GQA-style grouping).

9. out = rms_norm(out, ssm_norm) per group               [4096]
   out = out * silu(z)                                    [4096]

10. cur = ssm_out @ out                                   [2560]

11. Post-norm (sandwich norm) + residual add to x
```

### Conv1d Implementation

Depthwise 1D convolution with `d_conv=4` kernel on `8192` channels:

```c
/* conv_state shape: [d_conv-1][QKV_dim] = [3][8192] per layer */
/* Shift state left, insert new token */
memmove(conv_state, conv_state + QKV_dim, (d_conv-2) * QKV_dim * sizeof(float));
memcpy(conv_state + (d_conv-2) * QKV_dim, qkv, QKV_dim * sizeof(float));

/* Apply depthwise conv: each channel independently */
for (int c = 0; c < QKV_dim; c++) {
    float sum = 0;
    for (int t = 0; t < d_conv-1; t++)
        sum += conv_state[t * QKV_dim + c] * kernel[t * QKV_dim + c];
    sum += qkv[c] * kernel[(d_conv-1) * QKV_dim + c];  /* current token */
    qkv[c] = silu(sum);
}
```

Wait — the conv1d tensor is dims=(4, 8192). In GGUF, dims are stored
as [width, channels]. So `kernel[tap][channel]` with 4 taps and 8192 channels.

The conv_state stores the last `d_conv - 1 = 3` input vectors. For single-token
inference:
1. State has 3 stored vectors + 1 current = 4 values per channel
2. Convolve: `out[c] = sum_{t=0}^{3} state_extended[t][c] * kernel[t][c]`
3. Apply SiLU

### Delta Rule Recurrence

For each of 32 V-heads, with state matrix S[128][128]:

```c
int h_k = h * num_k_heads / num_v_heads;  /* GQA: map V-head to K-head */
float *s = ssm_state + h * head_k_dim * head_v_dim;  /* [128][128] */
float *q_h = q + h_k * head_k_dim;                    /* [128] */
float *k_h = k + h_k * head_k_dim;                    /* [128] */
float *v_h = v + h * head_v_dim;                       /* [128] */

/* Step 1: Decay */
for (int i = 0; i < head_k_dim * head_v_dim; i++)
    s[i] *= gate[h];

/* Step 2: Predict (S^T @ k) → [head_v_dim] */
float pred[128];
for (int j = 0; j < head_v_dim; j++) {
    pred[j] = 0;
    for (int i = 0; i < head_k_dim; i++)
        pred[j] += s[i * head_v_dim + j] * k_h[i];
}

/* Step 3: Delta correction */
float delta[128];
for (int j = 0; j < head_v_dim; j++)
    delta[j] = beta[h] * (v_h[j] - pred[j]);

/* Step 4: Write (k @ delta^T → outer product added to S) */
for (int i = 0; i < head_k_dim; i++)
    for (int j = 0; j < head_v_dim; j++)
        s[i * head_v_dim + j] += k_h[i] * delta[j];

/* Step 5: Read (S^T @ q) → [head_v_dim] */
float *out_h = out + h * head_v_dim;
for (int j = 0; j < head_v_dim; j++) {
    out_h[j] = 0;
    for (int i = 0; i < head_k_dim; i++)
        out_h[j] += s[i * head_v_dim + j] * q_h[i];
}
```

## State Management

### New buffers in llama_state_t
```c
float *conv_state;   /* [n_deltanet_layers × (d_conv-1) × QKV_dim] */
float *ssm_state;    /* [n_deltanet_layers × num_v_heads × head_k_dim × head_v_dim] */
float *qkv_scratch;  /* [max(QKV_dim + V_dim, n_heads * head_dim * 2)] scratch buffer */
```

### Memory Budget (Qwen3.5 4B, 24 DeltaNet layers)
| Buffer | Per layer | Total (24 layers) |
|--------|-----------|-------------------|
| conv_state: 3 × 8192 × 4B | 96 KB | 2.3 MB |
| ssm_state: 32 × 128 × 128 × 4B | 2 MB | 48 MB |
| **Total** | **2.1 MB** | **~50 MB** |

Fixed size — does NOT grow with context length.

### Reset
Zero `conv_state` and `ssm_state` before each new generation (same as KV cache reset).

## Config Extensions (llama_config_t)

```c
/* Qwen3.5 SSM/DeltaNet extensions */
int   ssm_d_conv;           /* conv kernel width (4) */
int   ssm_d_state;          /* head_k_dim (128) — Q/K per-head dim for SSM */
int   ssm_n_group;          /* num_k_heads (16) — Q/K head count for SSM */
int   ssm_dt_rank;          /* num_v_heads (32) — V/output head count */
int   ssm_d_inner;          /* d_inner (4096) — V_dim total */
int   full_attn_interval;   /* every Nth layer is full attention (4) */
bool *layer_is_deltanet;    /* [n_layers] — derived from tensor presence */
```

## qlayer_t Extensions

```c
/* DeltaNet tensors (data=NULL for full-attention layers) */
qtensor_t ssm_conv1d;   /* depthwise conv kernel [d_conv, QKV_dim] */
qtensor_t ssm_alpha;    /* alpha projection [dim, num_v_heads] */
qtensor_t ssm_beta;     /* beta projection [dim, num_v_heads] */
qtensor_t ssm_a;        /* A_log [num_v_heads] */
qtensor_t ssm_dt_bias;  /* dt bias [num_v_heads] */
qtensor_t ssm_norm;     /* output norm [head_v_dim] */
qtensor_t ssm_out;      /* output projection [d_inner, dim] */

/* Shared: attn_gate for DeltaNet Z gate, also usable for attention output gate */
qtensor_t attn_out_gate;  /* [dim, d_inner] or [dim, n_heads*head_dim] */
```

## Commit Strategy

1. **`feat: Qwen3.5 full-attention gated Q layers + layer type detection`**
   - Config: SSM fields, full_attn_interval, layer_is_deltanet
   - Loading: detect layer type from tensor presence
   - Forward: Q+gate inference-time split, sigmoid gate on attention output
   - DeltaNet layers: identity pass-through (load tensors but skip computation)
   - Test: model loads, full-attention layers produce output

2. **`feat: DeltaNet state allocation + tensor loading + conv1d`**
   - State: conv_state + ssm_state buffers
   - Tensors: load all ssm_* tensors
   - Conv1d: implement depthwise conv with state
   - Test: unit test for conv1d, model loads fully

3. **`feat: DeltaNet forward pass — delta-rule recurrence + output gating`**
   - Full DeltaNet layer computation
   - Unit tests for L2 norm, discretization, delta rule
   - Test: model generates tokens (expect coherent output)

4. **`bench: Qwen3.5 4B/9B results + CLAUDE.md updates`**

## Files Modified/Created

| File | Change |
|------|--------|
| `phase3/src/ai/llama_model.h` | SSM config fields + state buffers |
| `phase3/src/ai/llama_load.c` | SSM GGUF key reading + state alloc |
| `phase3/src/ai/llama_quant.h` | DeltaNet tensor fields in qlayer_t |
| `phase3/src/ai/llama_quant.c` | Tensor loading + forward dispatch |
| `phase3/src/ai/ssm.h` | SSM/DeltaNet forward pass API |
| `phase3/src/ai/ssm.c` | SSM/DeltaNet implementation (~500 LOC) |
| `phase3/src/ai/test_ssm.c` | Unit tests (~200 LOC) |
| `.github/workflows/ci.yml` | CI step for test_ssm |

## Risk Assessment

**Hardest part:** Getting the delta-rule recurrence math right. The extra
`S^T @ k` matmul per head per token is the delta correction that distinguishes
DeltaNet from standard Mamba2. One sign error or transposition mistake produces
garbage. Mitigation: validate against llama.cpp's output by running the same
prompt through both engines and comparing per-layer norms.

**Second risk:** Conv1d state management. The state must persist between tokens
and shift correctly. Off-by-one in the shift produces corrupted Q/K/V.
Mitigation: unit test with known input/output pairs.

**Q/K head mapping in GQA-style SSM:** num_v_heads=32 but num_k_heads=16.
Each pair of V-heads shares the same K-head. Must verify the exact mapping
against llama.cpp source. If wrong, the recurrence uses mismatched K vectors.
