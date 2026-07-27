# Model bench-off, re-run on a fixed harness — 2026-07-27

**Status:** EVIDENCE. No model swapped, nothing deployed. The decision is the operator's.
**Context:** `THINKING_MODE_RESEARCH.md` §7 found the 2026-04 bench-off measured raw completion —
no chat template. This re-runs it under the deployed prompt shape.

---

## 0. Headline

**The harness fix alone changed the ranking.**

**Llama 3.1 8B scored 5.06/10 in 2026-04, placed #8 of 11, and was DISQUALIFIED for "training data
contamination". Under a correct harness it ranks FIRST.** Llama-3 is heavily instruct-tuned and
degrades badly without its chat template; the old harness gave it none. The "contamination" reading
was, at minimum, confounded with a prompt-format artifact.

**The incumbent Gemma 4 E2B ranks THIRD of four.**

---

## 1. Results

5 blind judges, 12 questions, anonymised, per-question shuffled order.

| rank | system | quality | judge range | tok/s | size | deployed-equiv tok/s |
|---|---|---|---|---|---|---|
| **1** | **Llama 3.1 8B Instruct** Q4_K_M | **7.7** | 7.0–8.0 | 7.3 | 4.58 GB | **~2.0** |
| **2** | **Gemma 4 E4B** Q4_K_M | **7.5** | 7.0–8.0 | 11.0 | 5.03 GB | **~2.9** |
| 3 | **Gemma 4 E2B — INCUMBENT**, greedy | 6.1 | 5.5–6.5 | 20.2 | 2.89 GB | **5.46** (measured) |
| 4 | Gemma 4 E2B — recommended sampler | 5.8 | 5.0–6.5 | 20.2 | 2.89 GB | 5.46 |

**Ranks 1 and 2 are INDISTINGUISHABLE** (0.2 apart, identical ranges; judges split 3–2). So are ranks
3 and 4 — four of five judges independently identified them as the same base model, which is correct.

**tok/s is llama.cpp on the box CPU, NOT our engine.** The deployed seL4 build gets 5.46 tok/s on the
same silicon where llama.cpp gets 20.2 — a ~3.7× engine gap. Only the RATIOS transfer; the
"deployed-equiv" column is those ratios applied to the measured 5.46, and is a projection.

---

## 2. The sampler A/B — reported separately, because it applies whatever model wins

The box generates greedy (temp 0). The Gemma 4 card recommends temp 1.0 / top_p 0.95 / top_k 64.
**Tested on the incumbent, both ways, everything else identical.**

| incumbent config | quality | truncated answers |
|---|---|---|
| greedy (deployed) | **6.1** | 3/12 |
| recommended sampler | **5.8** | 5/12 |

**There is no free win here. The recommended sampler measured slightly WORSE.** The ranges overlap
(5.5–6.5 vs 5.0–6.5), so the honest reading is *no significant difference, with the edge to greedy* —
not "greedy is better". **Recommendation: keep greedy.** It is also the reproducible choice, which the
workload lane requires anyway.

---

## 3. What actually discriminates — and it is NOT what the prompt predicted

**Every system complied with every explicit length instruction.** All four answered all 7
length-constrained questions (`in one line` ×5, `in one sentence`, `in three sentences`) correctly and
concisely. **Nobody returned three paragraphs to a one-line request.** The rubric's
concise-beats-thorough rule, which was expected to be the discriminator, separated nothing.

**The discriminator is ANSWER ORDER under our 250-token cap.** On the 5 open-ended questions:

- **Llama 3.1 8B answers first and elaborates second.** When the cap cuts it, only elaboration is lost.
- **All three Gemma configs open with `Here is a detailed breakdown` + `## 1. The Context`** and are
  cut *before the substance*. Judges counted 3 fatally-unanswered each: a *"difference between X and
  Y"* that never reaches Y, a *"step by step"* delivering 1 of 3 steps, and a requested C function
  with no body.

**This is a finding about the interaction of model style with OUR CAP, not purely about model
quality.** Raise the cap and the preamble-first models would complete their answers — the ranking
could plausibly invert. Anyone acting on this must hold the cap fixed or re-measure.

Measured answer lengths (mean / max bytes, and how many of 12 end mid-sentence):

| system | mean | max | >1426 B (MTU) | truncated |
|---|---|---|---|---|
| Gemma 4 E2B greedy | 562 | 1233 | **0** | 3/12 |
| Gemma 4 E2B recommended | 560 | 1256 | 0 | 5/12 |
| Gemma 4 E4B greedy | 570 | 1179 | 0 | 5/12 |
| Llama 3.1 8B greedy | 623 | 1313 | 0 | 4/12 |

**No answer hit the 1426 B MTU.** On this workload the binding constraint is the **250-token cap**
(250 × ~4.9 B/token ≈ 1225 B, just under the ceiling) — so M2 sized that cap correctly, and
multi-frame replies are NOT required to fix truncation here. Raising the token cap would be.

---

## 4. Correctness — outranks style, and the incumbent is not the worst offender

- **The seL4 capability question is the sharpest discriminator and 3 of 4 fail it.** Only **Gemma 4
  E4B** correctly says the capability system provides *"access rights through unforgeable tokens"*.
  The others substitute seL4's formal-verification property, which is a different thing. The
  incumbent's recommended-sampler config is worst: *"provides a formal verification foundation"*.
- **Llama 3.1 8B carries the set's only outright false claim** — that threads in a single-threaded
  program *"are still executed sequentially on a single core"*, which is self-contradictory. This caps
  it at 8 rather than 9, and is the strongest argument for E4B over it.
- **Llama 3.1 8B is the only system that delivered a complete, compiling C function.** The other
  three were cut mid-loop and produced no working function at all.

---

## 5. What changed in the harness (`phase3/scripts/bench_models.sh`)

| variable | 2026-04 | now |
|---|---|---|
| template | none (`-no-cnv` in the real Windows harness) | `--jinja`, each model's OWN template |
| thinking | **silently ON** for Gemma 4 today | `-rea off` — deploy-faithful |
| cap | `-n 100` | `-n 250` = deployed `PB_GEN_MAX_CONTROL_IN` |
| sampler | greedy only | `QUALITY_SAMPLER=greedy\|recommended`, recorded in the filename |
| workload | 10 generic prompts | 8 real control-IN questions + 4 originals for continuity |
| scriptable | — | `-st` (conversation mode is otherwise interactive) |
| **verification** | **none** | **asserts the built prompt: `template_applied` / `think_leak`** |

**The verification step is the point.** The 2026-04 result was misread for three months because
everyone trusted the flags instead of reading the built prompt. Every model now prints
`template: APPLIED` / `thinking: off` and the result file records `template_applied=1 think_leak=0`,
so a bad run is loud instead of silent.

**Two traps this run hit, recorded so the next one doesn't:**
1. **`-no-cnv` no longer works** — the current binary answers `--no-conversation is not supported by
   llama-cli` and proceeds *with* conversation mode. The old harness cannot simply be re-run.
2. **The spinner is BACKSPACE-driven (`0x08`)**, not plain characters. Two strips silently did nothing
   until the bytes were dumped with `od -c`. Chrome must be stripped or judges score llama-cli's
   banner alongside the answer.

---

## 6. A defect in MY blind protocol, found by a judge

**The first judging run was INVALID and was re-run.** The anonymiser assigned labels *by position*
after shuffling, so `[R1]` was a different model on every question and per-system aggregation was
meaningless. A judge caught it, noted their own "evidence of stability" didn't fit one question, and
flagged it rather than papering over it.

Fixed: each system now holds a **fixed** opaque label and only the presentation ORDER shuffles.
Verified before re-running (each label → exactly one system across all 12; 8 distinct orders), and
independently confirmed by the judges, who reported **no label drift** on the re-run.

**Blind means the judge cannot tell which model a label is — not that the label moves.**

---

## 7. Recommendation

**No model swap on this evidence alone.** What the numbers support:

- **The incumbent is NOT the quality leader on its own workload** — it ranks 3rd of 4, ~1.5 points
  behind two alternatives that are already local and already supported by the engine.
- **If a change is made, Gemma 4 E4B is the better trade than the nominal winner.** It is
  statistically tied with Llama 3.1 8B on quality (7.5 vs 7.7), is **45% faster** (11.0 vs 7.3 tok/s),
  is the only system correct on the seL4 question, carries no outright false claim, and is the **same
  family and architecture** as the incumbent — so no engine work and minimal deployment risk (§5's
  "cheapest possible upgrade" candidate).
- **The cost is speed, and it is the operator's call.** E4B ≈ 0.54× deployed speed (~2.9 tok/s;
  a 250-token answer goes ~46 s → ~85 s). Llama 3.1 8B ≈ 0.36× (~2.0 tok/s, ~125 s).
- **Keep greedy.** The recommended sampler is not a free win; it measured slightly worse.

**Before any swap**, two things should be settled because they could invert this result: the ranking
is driven by behaviour under the **250-token cap** (§3), and a bigger cap is a cheaper intervention
than a model change. And E4B at 5.03 GB has never been run through **our** engine — the quality here
is llama.cpp's, and only the seL4 build's output is the deployed product.

**Also worth stating plainly:** this bench-off is 4 systems on 12 questions. The 2026-04 one was 11
models on 10 prompts. Neither is large. The defensible claim is *"the incumbent is not clearly the
best choice and the old evidence was measured wrongly"* — not *"model X is better"*.

---

*Harness: `phase3/scripts/bench_models.sh` (quality phase). Raw outputs:
`models/quality_results_v2/`. Blind set: `models/quality_results_v2/BLIND_SET.md`.
Companion to `THINKING_MODE_RESEARCH.md` (why the re-bench was needed) and
`MODEL_OPTIONS_2026-07.md` (what a bigger model costs — §1's linear size↔speed projection is
CORRECTED by §1 above: E4B is larger on disk than Llama 3.1 8B yet 51% faster, because active
parameters per token, not file size, set the bandwidth cost).*
