# JARVIS AI-OS — GPU Benchmark Results (RTX 2070)

**Date:** March 24, 2026
**Platform:** Main PC (daily driver, Ubuntu 24.04 dual-boot)
**GPU:** NVIDIA GeForce RTX 2070 (8GB VRAM, TU106, SM 7.5)
**CPU:** AMD Ryzen 7 2700X (8C/16T, 3.7 GHz)
**RAM:** 32GB DDR4
**NVIDIA Driver:** 580.126.09
**CUDA:** 13.0
**llama.cpp:** build c9dc43333 (8503), compiled with GGML_CUDA=ON, CUDA arch 75
**Purpose:** Phase 3a validation — benchmark inference performance for JARVIS model selection

---

## Benchmark Results

### llama-bench (pp512 = prompt processing 512 tokens, tg128 = text generation 128 tokens)

| Model | Quant | Size | Backend | ngl | pp512 (t/s) | tg128 (t/s) |
|-------|-------|------|---------|-----|-------------|-------------|
| Llama 3.2 1B Instruct | Q4_K_M | 763 MB | **CUDA** | 99 | **7,586.70 ± 344.72** | **273.31 ± 1.29** |
| Llama 3.2 1B Instruct | Q4_K_M | 763 MB | CPU | 0 | 3,341.35 ± 53.91 | 43.94 ± 0.31 |
| Llama 3.2 3B Instruct | Q4_K_M | 1.87 GB | **CUDA** | 99 | **3,006.80 ± 34.72** | **112.45 ± 1.21** |
| Llama 3.2 3B Instruct | Q4_K_M | 1.87 GB | CPU | 0 | 1,170.45 ± 8.71 | 16.47 ± 0.38 |

### GPU vs CPU Speedup

| Model | Prompt Speedup | Generation Speedup |
|-------|---------------|-------------------|
| 1B Q4_K_M | 2.3x | **6.2x** |
| 3B Q4_K_M | 2.6x | **6.8x** |

### Interactive Test (1B, GPU)

Prompt: "What is seL4?"
- Prompt processing: 1,187 t/s
- Generation: 247 t/s
- Output: Coherent English, but factually inaccurate (1B model hallucinated seL4's purpose — expected for a 1B model with limited knowledge)

---

## VRAM Usage Analysis

| Model | Quant | Model Size | Est. VRAM (model + context) | Fits RTX 2070 (8GB)? | Fits RTX 3060 (12GB)? |
|-------|-------|-----------|----------------------------|----------------------|----------------------|
| Llama 3.2 1B | Q4_K_M | 763 MB | ~1.2 GB | Yes (plenty of room) | Yes |
| Llama 3.2 3B | Q4_K_M | 1.87 GB | ~2.8 GB | Yes (room for context) | Yes |
| Phi-3 Mini 3.8B | Q4_K_M | ~2.3 GB | ~3.5 GB | Yes (tight with large context) | Yes |
| Llama 7B | Q4_K_M | ~4.0 GB | ~5.5 GB | Yes (limited context) | Yes (comfortable) |
| Llama 13B | Q4_K_M | ~7.5 GB | ~9.0 GB | **No** (exceeds 8GB) | Tight (12GB) |

**RTX 2070 (8GB) can comfortably run:** 1B, 3B, 3.8B, and 7B Q4 models.
**RTX 3060 (12GB) advantage:** Larger context windows for 7B, and can fit 13B Q4.

---

## CPU-Only Performance (Relevant for Phase 3b Bare Metal)

Phase 3b targets **CPU-only inference in seL4 userspace** (no CUDA on bare metal). These CPU numbers indicate what to expect on the Ryzen 7 2700X (or the spare PC's Ryzen 5 5600, which should be ~10-15% faster per-core):

| Model | CPU Generation (t/s) | Time per Token | Usable for JARVIS? |
|-------|---------------------|----------------|-------------------|
| 1B Q4_K_M | 43.9 t/s | 22.8 ms | **Yes** — fast enough for real-time |
| 3B Q4_K_M | 16.5 t/s | 60.6 ms | **Yes** — acceptable for standard queries |
| 7B Q4_K_M (est.) | ~6-8 t/s | ~125-167 ms | Marginal — slow but cache handles 85% |

**Key insight:** The decision cache handles 85%+ of queries in <1ms. Only ~15% of operations hit inference. At 44 tok/s (1B) or 16 tok/s (3B), CPU-only inference is viable for the remaining 15%.

For a typical 50-token response:
- 1B CPU: ~1.1 seconds
- 3B CPU: ~3.0 seconds
- Combined with 85% cache hit rate: average query latency stays under 500ms

---

## Comparison with Phase 2

| Metric | Phase 2 (Host PC CPU) | Phase 3a RTX 2070 GPU | Phase 3b CPU Target |
|--------|----------------------|----------------------|-------------------|
| Model | Phi-3 Mini 3.8B | Llama 3.2 1B/3B | Llama 3.2 1B/3B |
| Inference | 558ms (Phi-3 CPU) | ~4ms (1B GPU, 50 tok) | ~1.1s (1B CPU, 50 tok) |
| Throughput | ~2 tok/s (est.) | 273 tok/s (1B GPU) | 44 tok/s (1B CPU) |
| Platform | Main PC (Python) | Main PC (CUDA) | seL4 bare metal (C) |
| IPC | 7ms UART | 7ms UART (to Pi 4) | <1us shared memory |

**Phase 3a GPU gives 136x speedup** over Phase 2 CPU inference for the 1B model.

---

## Recommendations

### Phase 3a (GPU Host — Main PC or Spare PC)

Use **Llama 3.2 3B Q4_K_M** as the primary model:
- 112 tok/s generation on RTX 2070 — more than sufficient
- Better reasoning than 1B (fewer hallucinations)
- 1.87 GB model fits easily in 8GB VRAM
- Fall back to 1B (273 tok/s) for latency-critical queries

### Phase 3b (Bare Metal CPU — seL4 Userspace)

Use **Llama 3.2 1B Q4_K_M** as the primary model:
- 44 tok/s on Ryzen 7 2700X (Ryzen 5 5600 should be ~50 tok/s)
- 763 MB model fits in 16GB RAM with room for everything else
- With decision cache (85% hit rate), most queries never touch inference
- Scale up to 3B (16 tok/s) for complex queries via dynamic model scaling

### Dynamic Model Scaling (Phase 3c)

| State | Model | Platform | Expected Performance |
|-------|-------|----------|---------------------|
| IDLE | 1B Q4_K_M (763 MB) | CPU | 44 tok/s |
| ACTIVE | 3B Q4_K_M (1.87 GB) | CPU | 16 tok/s |
| CRITICAL | 3B + validator | CPU | ~12 tok/s |
| EMERGENCY | Rules only | Cache | <1ms |

---

## Notes

- These benchmarks were run on the **main PC** (daily driver), not the spare PC (not yet assembled)
- The spare PC (Ryzen 5 5600, RTX 3060 12GB) should perform ~10-15% better on CPU and have 50% more VRAM
- Phase 3b bare-metal inference will use the custom C tensor ops and dequantization code in `phase3/src/ai/`, not llama.cpp — expect somewhat lower performance than llama.cpp's optimized BLAS/AVX2 backend
- All models downloaded from bartowski/Llama-3.2-*-Instruct-GGUF on HuggingFace
- GGUF files stored in `phase3/models/` (gitignored)

---

*Benchmark Date: March 24, 2026*
*llama.cpp build: c9dc43333 (8503)*
*CUDA Toolkit: 13.0*
*Driver: 580.126.09*
