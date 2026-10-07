# JARVIS Daily Tech Briefing — Wednesday October 7, 2026

*Gap run: last briefing on file is Sep 30; "recent" = Sep 30 – Oct 7. De-duplicated against Sep 22–30.*

**Lead — [FASTDIAR: Frame-level Speaker Encoder for Streaming Diarization (arXiv:2610.02941, 2 Oct, ICASSP 2027)](https://arxiv.org/abs/2610.02941).** Turns a speaker-recognition architecture into a causal frame-level encoder: an embedding every 80 ms from a bounded 2 s window plus adaptive online clustering, distilled from an utterance-level teacher. Claims best streaming DER on low-overlap benchmarks at sub-second latency and ~5x real-time on one CPU thread. Fits the household case (low overlap) and the always-on-listening end of the roadmap. Passes: active Main PC (CPU, so it leaves the 2070 free for whisper). Not the box. Check whether the code and checkpoints are public and which teacher it distils from before treating it as an ECAPA replacement.

## Voice — verification, diarization, ASR
- **[Who Said What, and Will It Be Remembered? Persistent Speaker Attribution Across Meetings (arXiv:2609.39344, 1 Oct)](https://arxiv.org/abs/2609.39344)** — title-level only: the abstract page returned no text to me. It is the cross-session speaker-identity problem your people layer faces. Read it before designing the "inferred over days" logic.
- **[Teaching LLMs to Hear Who Spoke What (arXiv:2610.01695, 2 Oct)](https://arxiv.org/abs/2610.01695)** — metadata-supervised pretraining for speaker attribution in encoder-free speech-LLMs. Title-level only (abstract not retrievable). Research direction, not a drop-in for the Main PC.
- **[Training-free wake-word detection from pretrained ASR (arXiv:2610.01182, 2 Oct)](https://arxiv.org/abs/2610.01182)** — a possible gate for always-on listening without a separate KWS model. Title-level only.

## Memory & Extraction
- **[MemPilot (arXiv:2610.06830)](https://arxiv.org/abs/2610.06830)** — on-demand multimodal memory curation for agents; and **[schema-guided extraction (arXiv:2610.06322)](https://arxiv.org/abs/2610.06322)**, same shape as your typed-fact extractor in a materials domain. Both from the cs.CL/recent listing, titles only, unread. Skim-level.

## Local Inference & Quantization
- **Update: llama.cpp tip is `b11433` (5 Oct), up from `b11238`** — Hexagon pooling/DMA work; a `v0.6.0` tag adds an extended batch API for mixed token/embedding inputs ([freedom.tech](https://freedom.tech/project/llama-cpp/)). Extractor denominator: the mixed token/embedding batch API is worth a look for the Qwen3-Embedding lane. I did not read the per-build notes, so I can't say whether any grammar-path change landed.

## Privacy & Law
- **OAIC published AI-transparency guidance for privacy policies on 30 Sep** ([oaic.gov.au/news](https://www.oaic.gov.au/news)), and the surveillance-wearables post remains there ("giving serious consideration" — no action). Worth a read when you write the household consent/retention story; I only saw the headline.

## Nothing new (surface named, checked)
- **seL4:** [sel4.net/news/2026.html](https://sel4.net/news/2026.html) read directly; newest item is still ANU as Associate Member (18 Sep). Latest release remains 16.0.0 (22 Jul).
- **Model releases, both denominators:** [llm-stats.com/llm-updates](https://llm-stats.com/llm-updates) read directly; only GPT-6.1 Sol (29 Sep) and Claude Sonnet 5.5 (28 Sep) are newer than Sep 22 — proprietary, fail both denominators. The page's detail panel did not load, so open-weight ≤10B releases that the list omits are not ruled out.
- **Speaker verification proper:** [eess.AS/recent](https://arxiv.org/list/eess.AS/recent) and [cs.SD/recent](https://arxiv.org/list/cs.SD/recent) read directly; no new verification-embedding or score-normalisation paper beyond the items above.
- **Not checked this run (treat as unchanged, not confirmed):** wearable hardware, MTEB leaderboard / jina.ai, agent-memory vendor release pages (Mem0, Graphiti, Letta), constrained-decoding engine release pages, the AG privacy consultation page, Systems/OS news. Two web searches (memory benchmarks, small-model GGUFs) returned only older material.
