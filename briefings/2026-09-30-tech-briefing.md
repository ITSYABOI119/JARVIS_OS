# JARVIS Daily Tech Briefing — Wednesday September 30, 2026

**Lead — [Rethinking Automated Voice Similarity by Shifting from EER to Embedding Geometry (arXiv:2609.33999, 29 Sep)](https://arxiv.org/abs/2609.33999).** Argues EER is the wrong yardstick for voice similarity and proposes embedding-geometry metrics. Fits the ECAPA-hypersphere thread (2609.24688, 22 Sep) behind your single stored threshold: measure the cluster geometry, not just one operating point. Extractor/voice half only. (Abstract fetch was rate-limited, so this rests on the arXiv listing title and summary line, not the abstract.)

## Voice — verification, diarization, ASR
- **[Unified Target-Speaker ASR with Text and Enrollment Speech Cues (arXiv:2609.33853, 29 Sep)](https://arxiv.org/abs/2609.33853)** — conditions ASR on both a text cue and enrollment audio. Relevant to speaker attribution of short utterances once enrolled voices exist. Listing-level only (abstract fetch rate-limited).
- **[ProgDraft — speculative decoding for ASR (arXiv:2609.33245, 29 Sep)](https://arxiv.org/abs/2609.33245)** — 1.657x speedup on Qwen3-ASR-0.6B, 1.227x on 1.7B vs autoregressive; code public. Autoregressive-ASR path, not your faster-whisper CTranslate2 path; a marker that Qwen3-ASR 0.6B is a live small-ASR candidate.
- **[Pruned CTC for Memory-Efficient Large-Vocabulary ASR Training (arXiv:2609.33645)](https://arxiv.org/abs/2609.33645)** — training-side only; skip unless you fine-tune.

## Memory & Extraction
- **[MemoReason (arXiv:2609.35312)](https://arxiv.org/abs/2609.35312)** — how parametric memory affects contextual reasoning; loosely relevant to the extractor trusting its priors over the transcript. Skim-level, from the listing title only.
- **[Weak Task Specifications to Scientific Extraction Agents (arXiv:2609.34829)](https://arxiv.org/abs/2609.34829)** — schema-from-vague-spec extraction; same shape as your predicate-registry problem, different domain. Skim-level.

## Local Inference & Quantization
- **Update: llama.cpp tip is now `b11238` (28 Sep)** ([freedom.tech](https://freedom.tech/project/llama-cpp/)): multimodal input for `/v1/embeddings` (tested with Qwen3-VL-Embedding); 26 Sep CUDA SSM-scan support for Nemotron 3 Puzzle. Extractor denominator: neither touches the Gemma 4 / grammar path. Box: not applicable.

## Privacy & Law
- **OAIC: nothing bearing on household recording.** [oaic.gov.au/news](https://www.oaic.gov.au/news) newest items are 28 Sep (FOI/information integrity) and 25 Sep (Latitude breach action); the surveillance-wearables blog remains 7 Aug (already known).

## Systems / OS
- **Linux 7.3-rc5, stable targeted 18 Oct** ([Phoronix](https://www.phoronix.com/)); 7.4 queue: steal governor, FUSE large folios. Awareness only.

## Nothing new (surface named, checked)
- **Model releases, both denominators:** [llm-stats.com/llm-updates](https://llm-stats.com/llm-updates) read directly. New since the last briefing is only Claude Sonnet 5.5 (28 Sep, proprietary — fails both denominators); rest unchanged since 22 Sep (MiMo-V2.6 already assessed).
- **seL4:** [sel4.net/news/2026.html](https://sel4.net/news/2026.html) read directly; no new item since the ANU associate-member post; latest release still 16.0.0 (22 Jul).
- **Speaker verification/diarization proper:** [eess.AS/recent](https://arxiv.org/list/eess.AS/recent) and [cs.SD/recent](https://arxiv.org/list/cs.SD/recent) read directly; no diarization paper in the 29 Sep block, only the voice-similarity paper above.
- **Not checked this run:** wearable hardware, embeddings/MTEB, agent-memory vendor release pages, constrained-decoding release pages, Australian AG consultation page. Treat as unchanged, not confirmed. Two arXiv abstract fetches hit a proxy rate limit.
