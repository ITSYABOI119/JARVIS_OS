# Fused QKV Tensor Support — Load-Time Split

**Date:** 2026-04-12
**Status:** Approved for implementation

## Goal

Support GGUF models that store Q, K, V attention weights as a single fused
`blk.N.attn_qkv.weight` tensor instead of three separate tensors. Unlocks
Phi-3 and Qwen3.5 architectures (3 of 11 bench models).

## Approach: Load-Time Split

Detect fused tensor in `qmodel_load()`. Compute byte offsets for Q/K/V
sections based on config dimensions. Create three qtensor_t views (`wq`,
`wk`, `wv`) pointing into the fused data. Forward pass unchanged.

**Why not inference-time split?** Load-time split requires zero forward-pass
changes, zero temp buffers, zero state allocation changes. The three existing
`qmatmul_vec` calls work unmodified — they just point to slices of the same
GGUF data region.

## Fused Tensor Layout

Confirmed via llama.cpp `src/models/phi3.cpp`: vertical concatenation
`[Q_rows || K_rows || V_rows]`, NOT interleaved per-head.

```
Row 0 .. (q_rows - 1)                     = Q section  (n_heads * head_dim rows)
Row q_rows .. (q_rows + kv_rows - 1)      = K section  (n_kv_heads * head_dim rows)
Row (q_rows + kv_rows) .. (total - 1)     = V section  (n_kv_heads * head_dim rows)
```

### Concrete Dimensions

| Model | n_heads | n_kv_heads | head_dim | dim | Q rows | K rows | V rows | Total |
|-------|---------|------------|----------|-----|--------|--------|--------|-------|
| Phi-3 mini | 32 | 32 | 96 | 3072 | 3072 | 3072 | 3072 | 9216 |
| Qwen3.5 4B | 16 | 4 | 256 | 2560 | 4096 | 1024 | 1024 | 6144 |
| Qwen3.5 9B | 16 | 4 | 256 | 4096 | 4096 | 1024 | 1024 | 6144 |

## Files Modified

| File | Change |
|------|--------|
| `phase3/src/ai/llama_quant.c` | Fused QKV detection + split in `qmodel_load()` per-layer loop |
| `phase3/src/ai/llama_quant.h` | No changes — existing `wq/wk/wv` fields populated with views |
| `phase3/src/ai/llama_load.c` | May need config key reading for Phi-3/Qwen3.5 architecture |

## Implementation Detail

In the per-layer tensor loading loop of `qmodel_load()`:

1. Try `blk.N.attn_q.weight` first (existing path)
2. If not found, try `blk.N.attn_qkv.weight`
3. If found, compute split:
   - `q_rows = n_heads * head_dim`
   - `kv_rows = n_kv_heads * head_dim`
   - `row_bytes = (dim / block_size) * block_bytes` (from quant type)
   - Create three qtensor_t views with data pointers offset by row_bytes * row_count
4. Validate: `tqkv->n_elements == (q_rows + 2 * kv_rows) * dim`
5. Skip separate K/V tensor loading for this layer

## Quantization Block Alignment

Q4_K uses 256-element blocks. Row width is always `dim` which is divisible by
256 for all target models (3072, 2560, 4096). Splitting at row boundaries
means splitting at block boundaries. No alignment issues.

## Fused Gate-Up FFN (Phi-3 bonus)

Phi-3 also uses fused gate-up FFN: `ffn_up.weight dims=(3072, 16384)` where
`16384 = 2 * hidden_dim`. No separate `ffn_gate.weight`. Same load-time split
approach: detect missing `ffn_gate`, check if `ffn_up` has `2 * hidden_dim`
rows, split into gate (first half) and up (second half).

## Qwen3.5: Not Solvable with Just Fused QKV

Qwen3.5 `attn_qkv.weight dims=(2560, 8192)` has 8192 rows, not the expected
6144 (Q+K+V). The extra 2048 rows are SSM (state-space model) projections —
Qwen3.5 is a hybrid attention+SSM architecture. Also has `ssm_conv1d`,
`ssm_alpha`, `ssm_beta`, `ssm_dt`, `ssm_out` tensors. Supporting Qwen3.5
requires ~2000 LOC of SSM implementation (deferred per bench-off plan).

## Testing

1. Compile bench_engine.c, run on Phi-3 with `--tokens 8` — PASS (coherent output)
2. Qwen3.5: fused QKV mismatch detected and reported cleanly (architecture unsupported)
3. Unit tests: test_gemma4_config 11/11 PASS, test_gemma4_forward 33/33 PASS (regression clean)
