# JARVIS Daily Tech Briefing — Tuesday September 29, 2026

**Lead — [Inference-time target speaker unlearning in LLM-based ASR (arXiv:2609.30439, announced Mon 28 Sep)](https://arxiv.org/abs/2609.30439).** Removing a chosen speaker's influence from an ASR model at inference, no retraining. Bears on your retention/deletion rule for household speakers ("forget this person") if ASR ever adapts per speaker. Denominator: extractor-side/voice pipeline, concept only. Gist from the cs.SD listing only; paper not opened.

## Voice — verification, diarization, ASR
- **[I-Parakeet: Integer-Only Conformer ASR on Mobile NPU (arXiv:2609.30846)](https://arxiv.org/abs/2609.30846)** — fully integer Parakeet-style Conformer. Relevant to the wearable/always-on end of the roadmap, not the 2070. Listing gist only.
- **[Does per-frame early exit pay? (arXiv:2609.29867)](https://arxiv.org/abs/2609.29867)** and **[LiSenNet redesign for embedded (arXiv:2609.29866)](https://arxiv.org/abs/2609.29866)** — embedded speech-enhancement cost studies; possible pre-VAD cleanup on the wearable. Low priority.
- Also announced Sep 28: [dialogue-based streaming audio-visual target speaker extraction (2609.30774)](https://arxiv.org/abs/2609.30774) and [Acoustic-to-Text KV compression for full-duplex speech models (2609.31224)](https://arxiv.org/abs/2609.31224). Neither verification/diarization; skip unless you go full-duplex.

## Memory & Extraction
- **[Probing Stability-Plasticity Tradeoffs in Agent Memory (arXiv:2609.30558, EMNLP 2026)](https://arxiv.org/abs/2609.30558)** — cognitive-paradigm probes of retention vs. updating; a test-design source for your knowledge-update/conflict cases. Listing gist only.
- **[PIA (arXiv:2609.31255)](https://arxiv.org/abs/2609.31255)** — health conversations → structured records; same dialogue→typed-fact shape as your extractor, different domain. Skim-level.

## Local Inference & Quantization
- **llama.cpp tip is now `b11222` (27 Sep, RANK-pooling batch splitting for causal LLM rerankers)** ([freedom.tech](https://freedom.tech/project/llama-cpp/)). Extractor denominator: touches the reranker path (e.g. the jina-reranker GGUF noted earlier), not the grammar or CPU/AVX2 kernels. The page summary named no CPU/grammar/Gemma change; per-build notes between b11153 and b11222 were not individually read.

## Systems / OS
- **Linux 7.3-rc5 (27 Sep); stable expected ~18 Oct** ([Phoronix](https://www.phoronix.com/)). Go is adding experimental portable SIMD API. Not JARVIS-relevant beyond awareness.

## Nothing new (surface named, checked)
- **seL4:** [sel4.net/news/2026.html](https://sel4.net/news/2026.html) read directly; newest still ANU associate member (18 Sep).
- **Model releases, both denominators:** [llm-stats.com/llm-updates](https://llm-stats.com/llm-updates) read directly; newest still 22 Sep (Opus 5.5, GPT-6 Luna/Sol, MiMo-V2.6-Flash/Pro), nothing ≤10B extractor-class or box-eligible new.
- **Speaker verification/diarization proper:** [cs.SD/recent](https://arxiv.org/list/cs.SD/recent) read directly (newest 28 Sep); no verification/diarization paper in it.
- **Not checked this run (search-only or skipped):** wearable hardware, embeddings/MTEB, agent-memory vendor releases, constrained-decoding release pages, Australian privacy sites. Treat as unchanged, not confirmed.
