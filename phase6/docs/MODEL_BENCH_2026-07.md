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

## 8. FRONT-LOAD TEST (2026-07-28) — the mechanism is REAL, the fix is NET-HARMFUL

§3 found the discriminator was **answer order**, not quality. If so the incumbent's deficit is
structural, and structure is a prompt instruction — testable for one sentence, against a cap raise
that **cannot work**: max observed answer 1313 B against a 1426 B ceiling is **113 B ≈ 23 tokens, 9%**,
while reaching Gemma's substance needs ~400 tokens ≈ 1960 B, **1.37× over**. A meaningful cap raise
needs multi-frame (#14).

**ONE VARIABLE.** Cap 250, greedy, same 12 questions, same fixed-label blind protocol.
Instruction (**13 tokens**, measured with `llama-tokenize` less BOS = 5.2% of the budget):

> `Answer the question directly in your first sentence, then elaborate.`

Placed **inside the user turn, before the question** — the exact slot `g3_build_preamble_answer_only`
already injects into (`inference_server.c:373-390`), so it is deployable by construction. Asserted
from the BUILT prompt, not from a flag:

```
<|turn>user
Answer the question directly in your first sentence, then elaborate. ping<turn|>
<|turn>model
```

### 8.1 The mechanism WORKED — substance delivery, measured objectively

Per-question substance tests (does a "difference between X and Y" reach Y; does the handshake reach
all three steps; does the C function have a body):

| system | substance before | after | cut mid-sentence | max bytes |
|---|---|---|---|---|
| **Gemma 4 E2B — SUBJECT** | **1/5** | **5/5** | 3/12 → 6/12 | 1233 → 1268 |
| Gemma 4 E4B — family | 4/5 | 4/5 | 4/12 → 2/12 | 1179 → 1381 |
| **Llama 3.1 8B — CONTROL** | **5/5** | **5/5** | 4/12 → 3/12 | 1313 → **1404** |

The subject gained **every** fatally-cut question. Cuts went UP while substance went UP — answers are
now cut in the *elaboration* instead of before the substance. **The structural hypothesis is
confirmed as a mechanism.** The control was already 5/5 and stayed there, confirming the premise that
it front-loads (it could not rise — a ceiling effect, so this leg alone is weak evidence).

### 8.2 The SCORES say do not ship it

Judged **paired inside one run** (subject before/after + control before/after), because comparing
across judging runs would confound the effect with judge drift — and it does: the re-measured
baselines came in at E2B **7.0** (was 6.1) and Llama **8.0** (was 7.7).

| system | quality | length violations /12 | open unanswered /5 |
|---|---|---|---|
| Llama — CONTROL, before | **8.0** | 0.0 | 0.0 |
| E2B — SUBJECT, before | **7.0** | 0.6 | 3.0 |
| E2B — SUBJECT, **after** | **6.4** | 3.2 | **1.0** |
| Llama — CONTROL, **after** | **4.0** | **7.0** | 0.0 |

> **SUBJECT −0.6. CONTROL −4.0.**

**PRE-REGISTERED VERDICT APPLIED: "E2B falls → report it. Instructions consume prompt budget and can
distort."** That is the outcome. Not the structural-confirmed row.

### 8.3 Why — and the two halves of the instruction did opposite things

The instruction conflated two directives, and the data separates them cleanly:

- **"answer directly in your first sentence"** did its job: open-unanswered **3.0 → 1.0** (judges),
  matching the 1/5 → 5/5 substance measurement.
- **"then elaborate"** overrode *"in one line"*: length violations **0.6 → 3.2** on the subject and
  **0.0 → 7.0** on the control — Llama padded **every** length-constrained question, answering
  *"in one line, what is a mutex"* with a 150-word essay.

**The control had nothing to gain (0/5 unanswered already) and everything to lose, so it lost 4
points.** That is the cleanest possible demonstration that this instruction is not free.

**7 of the 12 questions carry an explicit length instruction**, and all four systems obeyed them
perfectly before the instruction. Trading that for truncation-survival is a bad trade on this
workload.

### 8.4 Recommendation

**The model question STANDS. This did not dissolve it.** But it is not a dead end either:

- **Do not ship this instruction.** It is net-negative for both systems tested.
- **An instruction WITHOUT "then elaborate" is UNTESTED** and is the obvious next experiment — the
  decomposition above says the damage came entirely from that clause. §5 forbade tuning mid-run, so
  it was not tried. **Do not read this as "a better wording would work"; it is a hypothesis with one
  supporting decomposition, nothing more.**
- **A cap raise remains ruled out** on the arithmetic above, independent of this result.

### 8.5 A harness bug this run exposed — and it touches §1's results too

**The chrome-strip was corrupting content.** It ran on EVERY response line rather than only the one
carrying the spinner, so `/**` became `**`, `// x` became `x`, markdown `- item` lost its bullet, and
**all code indentation was removed**. Its character class was also malformed (awk warned
``regexp escape sequence `\ ' is not a known regexp operator``), so it never stripped the backslash it
was written for. A blind judge spotted the mangled C comments across three systems and correctly
called it a pipeline artifact rather than a model failure.

**Fixed** (strip the first line only; `index()` against a plain set, since getting a literal backslash
into an awk bracket expression failed three ways) and verified on fresh output.

**Scope, stated rather than glossed:** the corruption applied **symmetrically to all arms** in both
§1 and §8, the verdicts rest on length-compliance and substance-delivery (neither involves comment
markers), and the judge excluded Q12 from differential scoring. **The rankings stand; Q12 alone
cannot be scored from the committed outputs.** Anyone wanting a clean Q12 must re-run — the harness
is now correct.

**A second trap, worth more than it looks:** an earlier `sudo systemd-run` bench left the result files
**root-owned**, so later non-sudo runs **silently failed to overwrite them** while still printing
"Saved to …". Verification was reading stale files. `chown -R jarvis:jarvis` before trusting a re-run.

### 8.6 Artifact provenance — the front-load-only per-model outputs did NOT survive

**This is a provenance note. It restates no score and re-derives none.**

`bench_models.sh` built its output path with `${PREFIX:+.frontload}`, which collapses **every**
non-empty prefix to a single filename. The two-clause run and the later front-load-only run therefore
wrote to the same seven paths, and the second **overwrote the first in place**.

| artifact | status |
|---|---|
| two-clause per-model `.txt` | **safe** — committed at `1df800a` |
| front-load-only **responses** | **survive in `models/quality_results_v2/BLIND_SET.md`** (4 systems × 12 questions, full text) |
| front-load-only **per-model `.txt`** | **gone.** With them: the per-answer `[speed]` tok/s, the `template_applied`/`think_leak` header, and the built-prompt block |

The label→system mapping was **recovered, not re-run**, and is recorded in
`models/quality_results_v2/BLIND_SET_KEY.md`. It is **two arms proven and two inferred**, and the key
states which is which:

- **PROVEN byte-exact** — SYS-1 = E2B baseline, SYS-3 = Llama baseline. The front-load-only round
  re-ran only the front-loaded arms, so the baselines carried over unchanged and each Q1 answer
  matches exactly one committed output and no other.
- **INFERRED** — SYS-2 = E2B front-loaded, SYS-4 = Llama front-loaded, from label continuity plus a
  byte-exact cross-set match in the two-clause blind set.

The premise that makes the recovery valid was **tested rather than assumed**: the two front-loaded
probe strings match **zero** committed files. Had either matched, "only the front-loaded arms were
re-run" would have been false and the recovery invalid.

**The collision now FAILS CLOSED.** A run with `QUALITY_PREFIX` set but no `QUALITY_PREFIX_TAG`
aborts with exit 2 and writes nothing, rather than guessing a filename — guessing is what consumed
the artifacts. The output name derives from the tag, so two prefixes can no longer collide. An
unprefixed run is unaffected. Verified by execution in all three directions (blocked without a tag /
proceeds with one / unprefixed path unchanged).

---

*Harness: `phase3/scripts/bench_models.sh` (quality phase). Raw outputs:
`models/quality_results_v2/`. Blind set: `models/quality_results_v2/BLIND_SET.md`.
Companion to `THINKING_MODE_RESEARCH.md` (why the re-bench was needed) and
`MODEL_OPTIONS_2026-07.md` (what a bigger model costs — §1's linear size↔speed projection is
CORRECTED by §1 above: E4B is larger on disk than Llama 3.1 8B yet 51% faster, because active
parameters per token, not file size, set the bandwidth cost).*
