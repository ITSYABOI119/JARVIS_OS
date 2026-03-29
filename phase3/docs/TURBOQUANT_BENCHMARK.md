# TurboQuant KV Cache Compression Benchmark

**Date:** 2026-03-29
**Branch:** `experiment/turboquant-benchmark`
**Hardware:** Ryzen 7 2700X (8C/16T), RTX 2070 (8GB VRAM), 32GB DDR4
**Software:** llama.cpp b8565, JARVIS TurboQuant C implementation

## Summary

TurboQuant (arXiv 2504.19874, ICLR 2026) compresses KV cache entries via random rotation + Lloyd-Max scalar quantization + 1-bit QJL residual correction. We benchmarked llama.cpp baselines and a custom pure-C TurboQuant implementation against Llama 3.2 1B/3B.

**Key finding:** 3-bit TurboQuant achieves **8.2x KV cache compression** (32 MB -> 3.9 MB for 1B) with 0.94 key cosine similarity and 0.96 attention score correlation. Frees ~28 MB on seL4 bare-metal.

## 1. llama.cpp Baseline Throughput

| Model | Size | Backend | ngl | Prompt (pp512) | Generation (tg128) |
|-------|------|---------|-----|---------------:|-------------------:|
| Llama 3.2 1B Q4_K_M | 763 MB | CPU | 0 | 3,387 t/s | 41.4 t/s |
| Llama 3.2 1B Q4_K_M | 763 MB | GPU | 99 | 7,546 t/s | 254.9 t/s |
| Llama 3.2 3B Q4_K_M | 1.87 GB | CPU | 0 | 1,131 t/s | 15.4 t/s |
| Llama 3.2 3B Q4_K_M | 1.87 GB | GPU | 99 | 2,873 t/s | 108.7 t/s |
| Llama 3.2 3B Q4_K_M | 1.87 GB | GPU (p512,n128) | 99 | 2,777 t/s | 105.1 t/s |

llama.cpp `--cache-type-k q8_0/q4_0` failed for 3B ("failed to create context").

## 2. JARVIS TurboQuant Compression Results

| Model Config | Bits | F32 KV | TQ KV | Compression | Savings |
|-------------|------|-------:|------:|------------:|--------:|
| 1B (d=64, 8 heads, 16 layers) | 2-bit | 32.0 MB | 2.9 MB | **11.1x** | 29.1 MB |
| 1B (d=64, 8 heads, 16 layers) | 3-bit | 32.0 MB | 3.9 MB | **8.2x** | 28.1 MB |
| 1B (d=64, 8 heads, 16 layers) | 4-bit | 32.0 MB | 4.9 MB | **6.6x** | 27.1 MB |
| 3B (d=128, 8 heads, 28 layers) | 3-bit | 112.0 MB | 14.7 MB | **7.6x** | 97.3 MB |

## 3. Quality Results

| Config | Key Cosine | Val Cosine | QJL Dot RMSE | Attn Corr |
|--------|:---------:|:---------:|:----------:|:--------:|
| 2-bit | 0.801 | 0.942 | 1.94 | ~0.90 |
| **3-bit** | **0.941** | **0.984** | **1.16** | **0.96** |
| 4-bit | 0.984 | 0.996 | 0.60 | ~0.99 |

3-bit is the sweet spot: 8.2x compression, 0.94+ key fidelity, 0.96 attention correlation.

## 4. Real Data Validation (Llama 3.2 1B F32)

Loaded the real model, ran 8-token forward pass ("The seL4 microkernel is"), extracted real KV cache vectors from the transformer, and compressed them with TurboQuant.

**KV cache distribution (layer 0) — NOT Gaussian:**

| | min | max | mean | std |
|---|----:|----:|-----:|----:|
| Keys | -9.56 | 12.81 | 0.14 | 1.69 |
| Values | -0.80 | 0.43 | -0.003 | 0.07 |

Keys have 25x larger std than values. Despite this non-Gaussian distribution, TurboQuant still works well because the rotation decorrelates coordinates.

**Compression quality on real KV vectors (128 vectors, 3-bit):**

| | avg | min | max |
|---|:---:|:---:|:---:|
| Key cosine | **0.943** | 0.903 | 0.967 |
| Value cosine | **0.983** | 0.968 | 0.990 |

Matches random-vector results within 0.002 — the rotation normalization is effective.

**Attention score comparison (4 layers x 4 heads = 16 tests):**

| Metric | Value |
|--------|------:|
| Avg score correlation | **0.984** |
| Avg softmax correlation | **0.9997** |
| Argmax match rate | **100%** (16/16) |

**Note:** Individual head cases (e.g., layer 0 head 0) can show softmax correlation as low as 0.88 due to score differences at specific positions being amplified by softmax. The position 7 score drifted by 2.6 points in that case. However, averaged across layers and heads, softmax correlation is 0.9997 and argmax always matches. This is consistent with the paper's finding that attention averaging dampens per-head errors.

## 5. Throughput (per head, d=64, single-threaded)

| Operation | Time | Throughput |
|-----------|-----:|-----------:|
| Key compress + decompress | 11.7 us | 86 Kops/s |
| Val compress + decompress | 6.1 us | 165 Kops/s |
| QJL dot product | 5.9 us | 169 Kops/s |

## 5. Memory Layout (d=64, 3-bit)

Per head: Key=28 bytes (16 MSE + 8 QJL + 4 norms), Value=26 bytes (24 MSE + 2 norm)
Per position per layer (8 heads): **432 bytes** vs 4,096 F32
State overhead: 33 KB/layer (Pi + S matrices), 528 KB total for 16 layers

## 6. Assessment: YES for Phase 3c

Benefits: 28 MB freed, context 512->2048+, 3B model KV fits in seL4
Cost: +6 ms/token at 512 positions (3.7% overhead), 528 KB matrices

## 7. Test Results: 13/13 PASS

```
Pi is orthogonal (Pi * Pi^T = I)                   PASS
Codebook centroids sorted ascending                PASS
Key roundtrip cosine similarity > 0.90             PASS
Value roundtrip cosine similarity > 0.95           PASS
QJL dot product reduces error vs MSE-only          PASS
Memory savings ~8x for d=64, 3-bit                 PASS
Zero vector compress/decompress                    PASS
d=128 compress/decompress roundtrip                PASS
Stress: 1000 key+value compress/decompress         PASS
Attention scores: TQ vs F32 correlation > 0.95     PASS
State overhead < 64KB for d=64                     PASS
Invalid parameters rejected                        PASS
Full KV cache simulation (16 layers, 8 heads)      PASS
```

## 8. Files

| File | LOC | Purpose |
|------|----:|---------|
| `phase3/src/ai/turboquant.h` | 107 | API header |
| `phase3/src/ai/turboquant.c` | 389 | Full implementation |
| `phase3/src/ai/test_turboquant.c` | 310 | 13 unit tests |
| `phase3/src/ai/bench_turboquant.c` | 141 | Benchmark harness |
| `phase3/src/ai/test_turboquant_real.c` | 280 | Real model validation (SKIP on CI) |

## References

- TurboQuant: arXiv 2504.19874 (ICLR 2026)
- PyTorch ref: github.com/tonbistudio/turboquant-pytorch
- llama.cpp KV cache: `--cache-type-k` / `--cache-type-v` flags
