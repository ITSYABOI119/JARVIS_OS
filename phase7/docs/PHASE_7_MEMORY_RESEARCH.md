# Phase 7 — Memory Research: what state-of-the-art agent memory does today, and what it means for JARVIS

**Status:** COMPLETE as a survey, 2026-09-06 (strategist-authored; the board row "Memory research first" in `phase7/docs/PHASE_7_PLAN.md` §0). **Verification status is per claim, see §0: 25 claims voted, 9 V / 12 R / 4 X — and in 24 of 25 the numbers were confirmed against the primary source; the R verdicts refute framing, not figures.** Written for the operator's decision on 2026-09-05/06 that the household memory store is to be state of the art, worth a whole phase, and is the one substrate goals 7.1 (associative memory), 7.5 (cross-session personality) and 7.8 (household voice learning) share.
**Method:** the six pre-registered questions of §1 were run through a five-angle search → 24 fetched sources → 120 falsifiable claims extracted with quotes → a 3-vote adversarial verification of 25 of them → synthesis by the strategist. **The first verification pass collapsed on a session limit (72 of 105 agents never ran, and the harness scored every unvoted claim as "killed", which is a harness artefact, not evidence); the run was resumed from cache and stopped by the operator at 73 of 75 votes; the strategist tallied those votes by hand (§0).** Every number below is the fetch agent's extraction from the named source, not the strategist's reading of the paper; treat each as **[U]** (unverified) unless §0 marks it **[V]** or **[R]**. Nothing here is a measurement of JARVIS.
**Sources:** §9, 24 URLs with the harness's quality tag (primary / secondary / blog / forum / unreliable). The harness's own artefacts: `scratchpad/memory_research_claims.json` (120 claims with quotes), `memory_research_digest.md`, run `wf_a6f79692-1b4`.

---

## 0. Verification ledger

**How the votes were obtained, and their limits.** The first pass cast no real votes (session limit). The resumed run re-extracted claims from the same 24 sources and adversarially verified 25 of that second extraction with three voters each; the operator stopped the run at 13:51 with 73 of 75 votes cached to save usage, and the strategist tallied those votes by hand from the journal (`scratchpad/memory_research_votes_union.json`). Two claims therefore have two votes and one has one. Each voter fetched the primary source itself (arXiv HTML or `pdftotext` on the PDF, the GitHub README, the vendor docs) and searched for a counter-source. **Status rule:** **V** = ≥ 2 supports and 0 refutes; **R** = ≥ 2 refutes; **X** = split or a single vote. The verified claims are the second extraction's wording; the S-numbers give the equivalent claim in this memo's own §2–§7.

**The headline of the ledger is not the V/R count. In 24 of 25 claims the voters confirmed every NUMBER against the primary source; what the R verdicts refute is the claim's FRAMING — an overreach, a stale present tense, a cherry-picked cell, or a causal story the source does not test.** Read each R row's last column before deciding what it means for JARVIS.

| # | memo | claim (short) | source | sup/ref | status | what the votes established |
|---|---|---|---|---|---|---|
| 1 | S10.2, S5.3, S1.1 | Full-context (~26k tokens) beats Mem0 and Mem0g on LoCoMo J: 72.90 vs 66.88 / 68.44; the "26 %" is vs OpenAI memory | Mem0 paper | 4/0 | **V** | all eight J scores verbatim; 4–6 points exact |
| 2 | S14.2 | HippoRAG 2 is evaluated on document-QA categories, never LoCoMo/LongMemEval/MemBench; README carries no numbers | HippoRAG repo + paper | 3/0 | **V** | two primary sources |
| 3 | S14.3 | The reference local deployment runs Llama-3.3-70B on 2 GPUs + NV-Embed-v2 7B | HippoRAG repo | 3/0 | **V** | "if anything understates the hardware" |
| 4 | S6.2 | Structure-augmented RAG regresses on factual recall (NQ R@5 44.4 vs 75.4); HippoRAG 2 is framed as the repair | HippoRAG 2 | 3/0 | **V** | abstract's pivot sentence, Tables 2–3 exact |
| 5 | S6.1, S14.1 | HippoRAG 2's hybrid architecture: OpenIE triples + passage nodes via `contains` edges + synonym edges (0.8) + top-5 recognition-memory filter + PPR (damping 0.5) | HippoRAG 2 | 3/0 | **V** | eight specifics verbatim |
| 6 | S14.1 | HippoRAG = OpenIE graph + PPR; two peer-reviewed papers (NeurIPS 2024, ICML 2025); non-parametric | HippoRAG repo | 2/0 | **V** | venues confirmed at proceedings |
| 7 | S19.2 | FadeMem's decay: stretched exponential, β 0.8 / 1.2, importance-modulated λ, auto-prune | FadeMem | 3/0 | **V** | 6/6 specifics verbatim |
| 8 | S23.3 | DMR: Zep 94.8 vs MemGPT 93.4, full-context 94.4 (98.0 vs 98.2 with gpt-4o-mini); authors call DMR inadequate | Zep paper | 3/0 | **V** | verified on the arXiv mirror, not only the vendor PDF |
| 9 | S23.2 | LongMemEval_S: Zep 71.2 vs 60.2 (gpt-4o), 63.8 vs 55.4 (mini); "18.5 %" is relative = +11.0 points | Zep paper | 3/0 | **V** | Table 2 exact in two renderings |
| 10 | S10.5 (first half) | Mem0g entity resolution = embedding-similarity threshold; superseded edges soft-invalidated by an LLM update resolver | Mem0 paper | 3/1 | X | the refuter: misattributes the deciding component and is stale for Mem0 v3 |
| 11 | S10.3 | Mem0's two-phase pipeline; an LLM picks ADD / UPDATE / DELETE / NOOP against the top-s memories | Mem0 paper | 2/1 | X | **true of the paper, STALE for the product: Mem0's own v2→v3 migration doc says the shipped algorithm is single-pass ADD-only with no UPDATE/DELETE and contradictions accumulate** (corroborates S7.3) |
| 12 | S14.5 | HippoRAG stores triples with three embedding levels in pluggable stores; the README describes no confidence / temporal / provenance / contradiction / forgetting | HippoRAG repo | 2/1 | X | positive half verified twice; two of five "absences" refuted (fact-to-passage linkage exists) |
| 13 | S1.2 | Mem0 understated Zep (65.99) through three harness errors; Zep re-measures 75.14 ± 0.17 | Zep blog | 0/1 | X | single vote: numbers exact, the re-run is the measured party's own, dispute unreconciled (S4.2) |
| 14 | S23.4 | Graphiti entity resolution: embedded names + full-text candidates, LLM adjudication, 4-message window, speaker always extracted | Zep paper | 2/2 | R | mechanism verified; the "no ontology / no types" clause is stale — custom entity and edge types exist since 2025-05 |
| 15 | S17.4 | The anti-distillation argument: compression discards detail; triples fragment events; keep self-contained event units | EMem | 0/3 | **R** | quotes exact; **the causal "non-compression pays off" is untested — no isolating ablation, and a 2026 controlled representation swap (arXiv 2601.00821) finds verbatim chunks beat event-unit extraction, i.e. event units are themselves lossy** |
| 16 | S6.3 | HippoRAG 2's seven datasets and averages (59.8 vs 57.0 F1, 78.2 vs 73.4 R@5) | HippoRAG 2 | 1/2 | R | averages exact; one sub-claim inverted (PopQA R@5 51.7 > 51.0) and 2Wiki's +13.9 recall omitted — the claim understated HippoRAG 2 |
| 17 | S23.1, S5.4 | Zep's bitemporal edges (four timestamps); contradiction closes validity, never deletes | Zep paper | 0/3 | R | **the four-timestamp model is CONFIRMED; the "never deleted / no provenance" negative is refuted — Graphiti ships `delete_entity_edge` / `delete_episode` and fact ratings; non-destructive history is the paper's model, not a system guarantee** |
| 18 | S23.5 | Zep regressed vs full-context on single-session-assistant (−17.7 % / −9.06 %) and knowledge-update (−3.36 %) | Zep paper | 1/2 | R | numbers exact; "unexplained / reproducible" overreaches — single run, and the same paper's LoCoMo 84 % was later corrected to 58.44 % |
| 19 | S17.1 | EMem beats the frameworks AND full-context on LoCoMo / LongMemEval_S (0.780 vs 0.744 / 0.723; 77.9 vs 55.0) | EMem | 0/3 | R | every number exact; **as a RANKING refuted: the abstract says only "match or surpass", baselines are inherited from the Nemori paper, the inherited Zep 0.585 sits below both sides of that dispute, and the same table's F1 / BLEU columns put full-context and Nemori ahead** |
| 20 | S17.2 | The graph is dispensable: EMem = EMem-G on LoCoMo, −1.9 on LongMemEval_S | EMem | 0/3 | R | **refuted by the same paper's cells: multi-hop 0.747 vs 0.702 (+4.5 for the graph), open-domain +6.5, LongMemEval_S +1.9 at both backbones; EMem shares the event graph and only removes propagation** |
| 21 | S17.3 | Event memory is worse at preferences (SSP 32.2 vs Nemori 46.7; 46.7–50 vs 86.7); cause = event abstraction drops style; pair with a separate profile model | EMem | 0/3 | R | **numbers exact, MECHANISM falsified: the no-abstraction full-context baseline scores WORST on the same questions (6.7 % / 16.7 %), SSP has n = 30, and Zep's structured KG raised SSP 30 → 53.3 %**. The authors' "separate profile model" is a suggestion, not a measured result |
| 22 | S10.5 (second half) | Graph memory helps only temporal / open-domain and hurts single- and multi-hop (Mem0 vs Mem0g per category) | Mem0 paper | 0/3 | R | per-category numbers exact; the generalisation is refuted — Mem0g wins overall by 1.56 and HippoRAG 2's graph gains ~7 F1 on multi-hop |
| 23 | S10.4 | Footprints 7k / 14k / >600k tokens; latencies (Zep p50 0.513, LangMem 17.99 s …) | Mem0 paper | 0/3 | R | transcription exact; **refuted as facts about the systems: Zep's latency came from sequential searches (corrected p95 0.632 vs 0.778), the 600k footprint from two named misconfigurations, measured by a competitor** |
| 24 | S19.4 | FadeMem's LoCoMo multi-hop F1 29.43 vs Mem0 28.37 — a ~1-point gain, no CIs | FadeMem | 0/2 | R | the first caveat (no CIs) verified; the second refuted — Zep never publishes LoCoMo figures, Mem0 publishes F1 and BLEU too |
| 25 | S19.1 | FadeMem: 45 % storage cut at 85.9 % factual consistency; 82.1 % critical-fact retention on LTI-Bench | FadeMem | 0/2 | R | figures real; **FCR is LLM-based fact checking with no formula, reported for FadeMem only, on an author-built synthetic benchmark with no released code or data** |

Rule for readers: a **[U]** claim may be quoted only as "the source states"; a **[V]** claim may be quoted as a finding; an **[R]** claim's numbers may be quoted, its framing may not. Design implications in §8 were written before the votes and re-read after them — §8a records what changed.

---

## 1. The six pre-registered questions

Q1 architectures and benchmark numbers with caveats · Q2 fact representation, temporal validity, provenance, confidence, contradiction, consolidation, forgetting · Q3 entity resolution and who-is-what-to-whom from conversational speech · Q4 fully-offline feasibility on an 8 GB GPU · Q5 learning style and preferences; surfacing "what it believes" and digests · Q6 novelty versus marketing, failure modes, reproducibility.

---

## 2. Q1 — Architectures and what they measure

**The landscape the sources describe.** Flat natural-language memories reconciled by an LLM (Mem0; S10.3, S10.4); a graph variant of the same (Mem0g; S10.5); a bi-temporal knowledge graph with LLM entity resolution (Zep/Graphiti; S23.1, S23.4); OpenIE triples + Personalized PageRank over a document corpus (HippoRAG 2; S6.1, S14.1); non-compressive event units with an LLM relevance filter, with or without a graph (EMem / EMem-G; S17.1, S17.2, S17.4); importance-modulated decaying memories (FadeMem; S19.1, S19.2); a typed, bitemporal, audit-preserving memory layer with the LLM judge removed from the write path (the eight-system audit's comparator; S24.1, S24.2); and an agent given tool-call control over flat text files (S9.3).

**The numbers, and why they do not compose into a leaderboard [all U]:**
- On LoCoMo, Mem0 66.88 % J, Mem0g 68.44 %, Zep 65.99 %, LangMem 58.10 %, OpenAI memory 52.90 %, A-Mem 48.38 % per the Mem0 paper (S10.1) — but **the same paper's full-context baseline scores ~72.9 %**, above both Mem0 variants (S10.2, S5.3, S1.1). Zep disputes its own 65.99 and reports 75.14 % ± 0.17 on a corrected configuration (S1.2, S4.1), while Mem0's re-run of Zep gives 58.44 % ± 0.20 — a ~17-point gap the two parties never reconciled (S4.2, S5.1).
- LoCoMo's own defects: conversations of only ~16–26k tokens, inside a modern context window (S1.3); ~99 wrong or ambiguous gold answers, so a ceiling near 93–94 % (S2.1); speaker mis-attribution in labels (S1.4); no knowledge-update questions at all (S1.5); prompt sensitivity that swings F1 by 34 points with retrieval fixed (S9.1); retrieval evaluated at top-k=50 over 19–32 sessions, i.e. never tested (S2.2). Zep, a top scorer, disavows the benchmark and points to LongMemEval (S4.4).
- LongMemEval_S (~115k-token histories): Zep 71.2 % vs 60.2 % full-context with gpt-4o (S23.2); EMem-G 77.9 % / EMem 76.0 % vs 55.0 % full-context and 64.2 % Nemori with gpt-4o-mini, **baselines copied from the Nemori paper, not re-run** (S17.1).
- HippoRAG 2 is measured on seven document-QA sets, not on any conversational-memory benchmark: average F1 59.8 vs 57.0 for a plain strong embedder — a ~2.8-point margin — with GraphRAG 49.6, RAPTOR 48.8, LightRAG collapsing to 6.6 (S6.3, S14.2).
- Scale is unmeasured by every headline figure: under evidence-preserving growth (400 irrelevant sessions added) HippoRAG loses 16–20 points of reliability inside its retrieval budget (S3.1, S3.2), and the failure regime differs by driver-model size (S3.3).
- Systems near the LoCoMo ceiling fall to 40–60 % on MemoryArena, a decision-coupled multi-session benchmark (S21.1). On MemoryAgentBench FactConsolidation, Zep/Graphiti scores 7.0 % single-hop, Mem0 18.0 %, HippoRAG-v2 54.0 %, and all 22 systems ≤ 7 % multi-hop (S22.1).
- MemFail finds **no architecture dominates**: graph-based StructMem is strong on causal/multi-hop and collapses on coexisting facts; Mem0 shows the inverse (S8.1). Raising k or the model size helps little and sometimes hurts (S8.2). A separate five-benchmark study finds every pre-built structured store beaten by plain long context on at least one benchmark (S9.2).

**Reading.** Vendor numbers on LoCoMo are not comparable with each other and several are below the trivial baseline. The only benchmarks that stress what JARVIS needs — temporal validity, conflict, growth, preference transfer — show every named system weak. The literature therefore does not hand us an architecture to copy; it hands us a list of failure modes to design against and a mandate to build our own evaluation.

---

## 3. Q2 — Representation, time, provenance, contradiction, forgetting

- **Two contradiction strategies.** Mem0: an LLM classifies each candidate against the top-s similar memories into ADD / UPDATE / DELETE / NOOP — destructive (S10.3); the current product page describes an ADD-only write path with contradiction deferred to retrieval-time reranking (S7.3). Zep/Graphiti: four timestamps per fact edge (two transaction-time, two valid-time); a contradiction closes the old edge's validity window and keeps it as history (S23.1, S5.4).
- **LLM-judged conflict resolution is the weakest component wherever it is measured.** FadeMem: 68.9 % macro accuracy over 4,075 conflicts, 53.4 % on overlapping information (S19.3). MemoryAgentBench FactConsolidation: the floor-level numbers above (S22.1). BeliefShift: up to 42 % of cross-session contradictions left unresolved across seven model families (S24.5). Two named LLM failure modes: prior-override (the model emits its training prior over the newer stored fact) and serial-comparison drift as the candidate pool grows (S22.3).
- **Deterministic freshness beats the LLM for current-value conflicts:** structured candidate extraction plus a Python max over version markers gains +10.8 points on FC-SH (67.2 → 78.0) with the gap widening at longer contexts (S22.2) — but the win **does not replicate** on LongMemEval knowledge-update (57.8 vs 64.4, a tie), and the authors scope it to current-value resolution (S22.4). That pipeline needs no vector DB, no graph DB and no embedder: BM25 over numbered facts, ~50 lines (S22.5).
- **Write-path anomalies are universal where an LLM judges writes.** The eight-system audit: mem0 v2/v3, Graphiti, Letta, Zep and MIRIX each admit at least one of replay inconsistency, belief-drift skew, audit erasure; audit erasure is universal among the five with a provenance path; only removing the judge from the write path avoided all three (S24.1). The four production heuristics — last-writer-wins, evidence-weighted merge, await-confirmation, per-rule policy — are typed as bitemporal operators with explicit isolation preconditions, over a dual-row schema where the losing fact is preserved as an audit row (S24.2). Their contradiction-resolution latency stays flat to 100k facts single-process (p50 ~4 ms, p99 < 18 ms) (S24.4).
- **Compression versus preservation.** EMem argues that distilling to summaries or facts before indexing irreversibly discards detail, and that triples fragment one event into ~5 pieces that lose temporal and participant constraints; its alternative is a self-contained natural-language event unit with normalised entities, inferred timestamps and source-turn attribution (S17.4). Build-time schema commitment is named as an irreversible failure mode elsewhere too (S9.4). Yet MemFail finds verbose stored memories can degrade the embedding space and hurt retrieval-bottlenecked tasks (S8.4).
- **Forgetting.** FadeMem: stretched-exponential decay modulated by an importance score, two layers with half-lives ≈11 and ≈5 days, auto-pruning (S19.2); its 45 % storage cut with higher critical-fact retention is measured on the authors' own synthetic 30-day benchmark (S19.1), and its accuracy edge over Mem0 is about one F1 point (S19.4). The survey states selective forgetting is measured by essentially one benchmark and is the competency systems most reliably fail (S21.4). Staleness without an explicit temporal-validity mechanism is the default failure of accumulating stores (S21.3).
- **Provenance and source weighting.** The survey prescribes temporal versioning, source attribution weighted so the user's own statement outranks the agent's inference, and explicit conflict flagging (S21.2), and names the self-reinforcing-error mode of reflective memory: a wrong inference written to memory suppresses the evidence that would refute it (S21.5). TSM recovers 12.2 points on LongMemEval and LoCoMo purely by separating dialogue time from occurrence time (S24.5).

---

## 4. Q3 — Who is who, and who is what to whom, from speech

- Dialogue relation extraction is **about the speakers**: 89.9 % of DialogRE triples are a speaker attribute or a speaker-to-speaker relation, 96.8 % of subjects are person names (S20.2). Facts are cross-turn: ~96 % span multiple sentences, 65.9 % have arguments that never share a turn (S20.4) — utterance-level chunking would miss most of them. Speaker-aware tokens help modestly (61.2 vs 58.5 F1) (S20.1). Absolute accuracy is low even supervised and in-domain on clean scripted transcripts: 61.2 F1 over 36 relation types (S20.3); directional confusion between inverse relations persists, and evidence-trigger identification is as hard as the extraction itself (S20.5).
- Resolving "my sister" to a person is the task with the weakest human ceiling: Fleiss' κ 0.434 for linking versus 0.864 for detecting the mention (S18.1); detection is a rule (0.903 F1) while linking reaches 0.589 F1 (S18.3); the system links to an earlier surface string, not a persistent person node (S18.2); document entity linkers degrade sharply on conversation (REL 0.231 vs CREL 0.597 F1) (S18.4); the supervised resource is a 73-example test set (S18.5).
- Production systems resolve entities with an LLM: Graphiti embeds names for cosine candidates plus full-text search, then an LLM adjudicates duplicates in episode context, always extracting the speaker as an entity (S23.4); Mem0g runs an entity extractor then a relationship generator and marks superseded relationships obsolete (S10.5); Mem0's product page describes entity linking as a retrieval-time signal (S7.4).
- A small encoder-based joint entity + relation extractor (GLiNER-relex, DeBERTa-v3-large + BiLSTM) runs ~70× faster than a frontier API model (0.9 s per document on an L4) with zero-shot types specified at inference (S15.1, S15.2), but every zero-shot benchmark score is under 41 % micro-F1, dataset-dependent against an LLM (S15.3), and precision degrades quadratically in entity-dense passages — the multi-speaker household regime (S15.4). It ships as an installable package (S15.5).

**Reading.** "Who she is to him" is inferable, but every measured system is wrong a large fraction of the time on exactly this. The design consequence is not "do not infer"; it is that relationship facts must carry a confidence, a source turn, and be recomputable from preserved transcripts as evidence accumulates — and that a multi-day accumulation of consistent evidence, not a single extraction, is what earns a high confidence.

---

## 5. Q4 — Offline on an 8 GB GPU

- The published graph systems are out of reach as configured: HippoRAG 2's measured 9.9 GB GPU memory, 99.5 min indexing and a 70B extractor with a 7B embedder (S6.4, S6.5, S14.3); a practitioner's offline Graphiti stack uses qwen2.5:14b at ~16 GB VRAM with ~25 s per episode, asynchronous, throttled to 3 concurrent episodes (S11.2, S11.3, S11.4).
- Graphiti's maintainers warn smaller local models fail at extraction and at the constrained JSON the pipeline requires (S12.1, S12.2); three model roles are needed and some paths still hard-require an OpenAI key (S12.3); a fully local path via Ollama's OpenAI-compatible API is nonetheless documented, with nomic-embed-text at 768 dims (S12.4, S11.5), but not through the default client path (S12.5). Zep's self-hosted path is deprecated in favour of the paid cloud (S5.5).
- MemFail's finding that neither k nor model size is the binding constraint (S8.2) and the deterministic-freshness pipeline's BM25-only footprint (S22.5) both argue that a small, well-structured store can compete on the axes that matter here. The encoder-based extractor is consumer-GPU sized (S15.1).
- Qwen3-Embedding-0.6B — **already the deployed JARVIS embedder, parity-proven on the box at Q8_0** — is claimed to run in ~640 MB at Q8 (S13.1); that page is tagged unreliable and internally inconsistent (S13.5), so JARVIS's own measured figures supersede it.

**Reading.** The 2070 rules out the 14B–70B extractors the reference stacks assume. The feasible shape is: our existing embedder; an encoder-style or small constrained-decoding extractor measured for JSON reliability before trust; deterministic rules wherever a rule exists; and a single-process store (SQLite with a vector extension, or an embedded graph such as Kuzu — a design-doc decision, measured, not assumed).

---

## 6. Q5 — Style, preferences, and showing what it believes

- Event-centric memory is measurably worse at preferences and style than compression-based memory: 32.2 % vs 46.7 % on LongMemEval single-session-preference questions with gpt-4o-mini, 46.7–50 % vs 86.7 % with gpt-4.1-mini; the authors recommend a **separate user-profile model** rather than one representation for both (S17.3).
- PersonaMem: frontier models given the whole history score only ~50–52 % on personalisation queries (S16.1); reasoning-tuned models do no better (S16.2); the failure is transfer — applying a stated preference to a new scenario — not recall (S16.3); the profile is modelled as static attributes plus dynamic, versioned preferences with the distance and interference of evidence annotated (S16.4).
- MemFail separates two storage failures that a preference profile must handle: refusing to overwrite an outdated fact, and refusing to admit valid coexisting facts ("Dan likes pizza" versus "Dan now hates pizza") (S8.3).
- No source in the harvest evaluates a "current beliefs" view or a periodic digest as a product surface; the survey's prescriptions (versioning, source weighting, conflict flagging) are the closest thing to a specification for one (S21.2).

**Reading.** The owner's two asks, "always learn my style and preferences" and "I want to know by looking at what it thinks", point at a profile layer that is separate from the event store, versioned, and rendered with its evidence. That is exactly where the literature is weakest, which is where JARVIS's own measurement will have to carry the claim.

---

## 7. Q6 — Novelty, failure modes, reproducibility

- **Benchmark integrity is the field's central problem.** Vendor-run, self-tuned numbers (S5.2); a 9-point swing from configuration alone (S1.2); an arithmetic error in a headline (S4.1); non-comparable tasks under one benchmark name (S2.3, S2.5); hand-coded patches inflating a dev score (S2.4); no standard prompt (S4.3); latency figures that are harness artefacts (S4.5); an incomplete evidentiary page from a vendor (S7.5).
- **What is reproducible:** HippoRAG's MIT code with shipped OpenIE results (S14.5); MemFail's open datasets with seeds and row-level traceability, though LLM-generated (S8.5); the deterministic-freshness pipeline (S22.5); the encoder extractor package (S15.5). **What is not:** FadeMem (cloud models, no code) (S19.5); EMem (hosted OpenAI models throughout, heavy ingest: 500–1,400 event nodes per conversation) (S17.5); PersonaMem's headline figure differs between its repo and its abstract (S16.1).
- **What is genuinely new, on this evidence:** bitemporal non-destructive facts (S23.1) and typed operators with audit rows and stated isolation (S24.2) — database ideas applied to memory; non-compressive event units as the retrieval object (S17.4); the demonstration that removing the LLM from the write path removes a whole class of anomalies (S24.1); importance-modulated decay as a mechanism, if not yet as a validated result (S19.2). **What is marketing:** any LoCoMo-derived "state of the art" claim.

---

## 8. Design implications for JARVIS (written to hold whether the votes land [V] or [U])

1. **No single architecture; route by fact type.** Three layers over one provenance spine: (a) an **event layer** of non-compressive, self-contained natural-language event units with normalised entities, inferred timestamps, speaker cluster and source-turn attribution — the transcript is kept, so nothing is distilled irreversibly; (b) a **profile layer** — typed, versioned, bitemporal facts (valid-time and transaction-time), each with source, stated-or-inferred, confidence, and a non-destructive supersession chain; (c) a **people layer** — persistent person nodes (the owner enrolled; others as speaker clusters that earn a name and a relationship over days), with relationship edges carrying confidence and the evidence turns behind them.
2. **The LLM stays off the write path wherever a rule exists.** Current-value conflicts are resolved by deterministic freshness over version markers; the LLM extracts candidates with constrained output and never adjudicates silently. Every write is an append; a losing fact becomes an audit row, never a deletion. This is the K-b allowlist instinct applied to memory, and the audit's evidence says it removes replay inconsistency, belief-drift skew and audit erasure by construction.
3. **Inference is welcome, but it can never feed itself.** The owner's rule is that inferred facts are used without confirmation. The store therefore must (a) rank owner-stated over inferred, (b) recompute inferences from the preserved event layer rather than from prior inferences, (c) hold confidence as evidence count and consistency over days, and (d) surface every inferred fact with its evidence on the console so a wrong belief is visible rather than silently load-bearing. The self-reinforcing-error mode is designed out, not confirmed away.
4. **A separate preference and style model.** Preferences are versioned (a new preference supersedes with a valid-time; coexisting preferences are admitted as distinct facts), and the profile renders both the current value and its history. Style is learned from the owner's own utterances only.
5. **Our own benchmark, pre-registered, from day one.** A household test set built from the owner's real transcripts plus synthetic conflicts: knowledge updates, coexisting facts, preference transfer to a new scenario, who-is-what-to-whom, and evidence-preserving growth (the same facts, months of unrelated transcript added). Retrieval measured at a realistic k; write-path cost measured; no vendor number ever quoted as our target.
6. **Feasible on the 2070, by construction.** Reuse the deployed Qwen3-Embedding-0.6B; a small constrained-output extractor measured for JSON reliability before it is trusted, with an encoder-style extractor as the cheap first candidate; BM25 over numbered facts as the always-available baseline lane; a single-process embedded store with flat p99 write latency, chosen in the design doc after measuring SQLite-plus-vector against an embedded graph on our own data.
7. **Forgetting is demotion, never erasure.** Decay lowers retrieval priority and can archive; the only true delete is the owner's manual purge of non-household speech. Provenance survives everything.
8. **The box receives a projection, not the store.** The Main PC holds events, profile and people; the seL4 appliance receives a compact "current beliefs" set of typed facts with confidence over control-IN, sized to its 4096 × 512 B semantic store — the distillation target, not the archive.
9. **Honest ceiling, in advance.** Every measured system is wrong a large fraction of the time at exactly the tasks JARVIS wants: conflict resolution, relationship inference, preference transfer. The state of the art is a set of mechanisms that make wrongness visible, reversible and measurable — not a system that is right. That is what the design doc will pre-register.

### 8a. What the verification votes changed (read after §0)

- **Implication 1 is strengthened and sharpened.** The anti-compression argument (ledger #15) is refuted as a causal claim: event units are themselves lossy and a controlled swap found verbatim chunks better on knowledge updates. So the event layer is an INDEX over the preserved transcript, never the ground truth; retrieval must be able to fall back to the verbatim transcript span, and every derived unit carries the span it came from. The graph is not dispensable either (#20, #22): graph propagation earns its multi-hop gains; the design keeps a lightweight graph over the people layer rather than pretending flat retrieval suffices.
- **Implication 2 is corroborated by the vendor that abandoned the alternative.** Mem0's shipped v3 dropped LLM-adjudicated UPDATE/DELETE on the write path for ADD-only (#11); Graphiti exposes deletes and ratings that its paper's model does not mention (#17). Keeping the LLM off the write path and making every loss an audit row is now a choice with two vendors' scars behind it, not a preference.
- **Implication 4 stands on weaker legs than the memo first implied.** The preference gap is real as a number and false as a mechanism (#21): nobody has shown WHY event memory misses preferences, and full context is worse. A separate preference model is still the right shape, but JARVIS's own benchmark must include a preference-transfer set from day one, because the literature will not settle it.
- **Implication 5 is confirmed from the other direction.** Every R verdict on a benchmark claim (#13, #16, #18, #19, #23, #24, #25) was a framing or comparability failure, never a fabricated number. The field's numbers are real and its comparisons are not; our benchmark must be ours.
- **Nothing in §8 was overturned.** Nine claims verified outright; no design implication rested on a claim whose numbers failed.

---

## 9. Sources (harness quality tag; S-numbers as cited above)

| S | quality | source |
|---|---|---|
| S1 | blog | https://blog.getzep.com/lies-damn-lies-statistics-is-mem0-really-sota-in-agent-memory/ |
| S2 | forum | https://github.com/MemPalace/mempalace/issues/29 |
| S3 | primary | https://arxiv.org/html/2606.01435v1 |
| S4 | primary | https://github.com/getzep/zep-papers/issues/5 |
| S5 | blog | https://theaiengineer.substack.com/p/cognee-vs-zep-vs-mem0-vs-letta |
| S6 | primary | https://arxiv.org/abs/2502.14802 (HippoRAG 2, ICML 2025) |
| S7 | blog | https://mem0.ai/research |
| S8 | primary | https://arxiv.org/html/2605.26667v1 (MemFail) |
| S9 | primary | https://arxiv.org/pdf/2605.10108 |
| S10 | primary | https://arxiv.org/pdf/2504.19413 (Mem0, ECAI 2025) |
| S11 | blog | https://github.com/Flo976/graphiti-mcp-ollama |
| S12 | primary | https://help.getzep.com/graphiti/configuration/llm-configuration |
| S13 | unreliable | https://www.madebyagents.com/models/qwen3-embedding-0-6b |
| S14 | primary | https://github.com/OSU-NLP-Group/HippoRAG |
| S15 | primary | https://arxiv.org/html/2605.07313 (GLiNER-relex) |
| S16 | primary | https://github.com/bowen-upenn/PersonaMem (COLM 2025) |
| S17 | primary | https://arxiv.org/pdf/2511.17208 (EMem) |
| S18 | primary | https://arxiv.org/abs/2206.07836 (ConEL-2, personal entity linking) |
| S19 | primary | https://arxiv.org/html/2601.18642 (FadeMem) |
| S20 | primary | https://aclanthology.org/2020.acl-main.444/ (DialogRE) |
| S21 | secondary | https://arxiv.org/html/2603.07670v1 (survey) |
| S22 | primary | https://arxiv.org/html/2606.04315 (deterministic conflict resolution) |
| S23 | primary | https://blog.getzep.com/content/files/2025/01/ZEP__USING_KNOWLEDGE_GRAPHS_TO_POWER_LLM_AGENT_MEMORY_2025011700.pdf |
| S24 | primary | https://arxiv.org/pdf/2606.06240 (eight-system audit; typed bitemporal memory layer) |

The S-number → URL mapping was reconstructed by matching each extract's publish date and content to the run's source list (the agent start records carry only a cache key); S13's quality tag is the harness's own. A 25th cached extract duplicated a blog source and is not used.

---

## 10. What this document is not, and what comes next

Not a design, not a measurement of JARVIS, not a claim that any cited number is true beyond what §0 says. Next: the design doc for the memory store — the three layers and the provenance spine of §8 as schemas, the deterministic write path as rules, the household benchmark as pre-registered outcomes, the store choice as a measurement plan on the Main PC — then a coder prompt for M0 of the store, and the board rows "Memory research" (this document, on commit) and "The memory store" (its first measured milestone).
