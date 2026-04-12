# RotorQuant — KV Cache Compression Reference

**Date:** April 12, 2026
**Source:** https://github.com/scrya-com/rotorquant
**Paper:** arXiv — "RotorQuant: Clifford Algebra Vector Quantization for LLM KV Cache Compression"
**Relevance:** Week 34 TurboQuant evaluation — RotorQuant is a direct alternative

## What It Is

KV cache quantization framework using block-diagonal rotations (Givens rotations and quaternions) instead of dense orthogonal transforms like TurboQuant's Walsh-Hadamard. Targets 3-4 bit symmetric quantization of KV cache tensors at inference time.

## vs TurboQuant

| Metric | RotorQuant (IsoQuant) | TurboQuant |
|---|---|---|
| Decode speed | 28% faster | baseline |
| Prefill speed | 5.3x faster | baseline |
| Perplexity (10.3x compression) | 6.91 | 7.07 |
| Attention cosine similarity | 0.990 | 0.991 |
| Rotation parameters (d=128) | 128 (44x fewer) | 16,384 |
| Complexity | O(d) | O(d^2) |

## Three Variants

1. **PlanarQuant** — 2D Givens rotations (1 angle per pair)
2. **IsoQuant** — 4D quaternion rotations (**recommended**, 5.8x fastest)
3. **RotorQuant** — Clifford algebra rotors (3D blocks, research/legacy)

## llama.cpp Integration

Has a working branch: `feature/planarquant-kv-cache`
- CUDA and Metal backends via CMake
- Cache type flags: `--cache-type-k iso3 --cache-type-v iso3`
- Deferred quantization: K-cache stays FP16 during prefill (zero error accumulation)

## Implementation Status

- **Python + CUDA + Triton + Metal** — no pure C code
- Dependencies: torch>=2.0, scipy, transformers
- Would need to port rotation math to C for JARVIS engine

## Key Design Insight

V-cache dequantization requires EXPLICIT inverse rotation (unlike TurboQuant's self-inverse Hadamard). This single fix improved perplexity from 15.3 to 7.05.

## Relevance to JARVIS

When evaluating TurboQuant (Week 34), compare against RotorQuant:
- If we port TurboQuant's Walsh-Hadamard to C, RotorQuant's block rotations would be simpler and faster
- IsoQuant's quaternion rotation is ~20 LOC of math vs TurboQuant's butterfly network
- Both target KV cache (complementary to Q4_K_M weight quantization)
- RotorQuant's llama.cpp branch could be studied for integration patterns
- The "deferred K quantization" strategy (FP16 during prefill, quantize only for decode) is worth adopting regardless of which rotation we use
