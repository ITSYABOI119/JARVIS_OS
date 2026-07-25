# Answer Quality — separate think/answer budgets + a thinking toggle: RESEARCH + DESIGN

**Status: PLAN-FIRST. No code, no flag, no wire change.** Authored 2026-07-26 after the boot-40
bare-metal validation of image `9362c756` came back with thought-channel output on a FRESH question.

**Read §1 first.** The research changed the problem. Three of the five design questions below are
smaller than they looked, and two defects were found that no amount of budget tuning would fix.

---

## 0. The correction that got us here

The recall fix (`f1fc84b`) was diagnosed from a controlled experiment: question held fixed, preamble
varied, byte-identical reproduction of both the good and the bad box output. That experiment was
valid and its finding is real — **a mid-sentence preamble does change the outcome**. The conclusion
drawn from it was one layer too shallow.

The operator's question 3 — *"what's my favorite color?"* — is FRESH. No exact-key match, no
preamble possible, recall cannot have fired. It produced thought-channel output anyway. So the
preamble is a **modulator of how long the model thinks**, not the trigger. The recall fix stays (it
removes one source of over-thinking, and injecting a mid-clause fragment is wrong regardless), but
it was treating a symptom.

---

## 1. RESEARCH — ground truth from the model file

Read directly out of the deployed model, `models/gemma-4-E2B-it-Q4_K_M.gguf` (the same GGUF that is
on the box's `JARVIS_DATA` as `GEMMA2B.GUF`).

### 1.1 The hand-rolled token ids are ALL CORRECT

Verified against `tokenizer.ggml.tokens`:

| id | token | `inference_server.c` claim | verdict |
|---|---|---|---|
| 1 | `<eos>` | `<eos>` | correct |
| 2 | `<bos>` | `<bos>` | correct |
| **98** | **`<\|think\|>`** | `<\|think\|>` | **correct** |
| 100 | `<\|channel>` | *(not used)* | — |
| 101 | `<channel\|>` | *(not used)* | — |
| 105 | `<\|turn>` | `<\|turn>` | correct |
| 106 | `<turn\|>` | `<turn\|>` | correct |
| 107 | `\n` | `\n` | correct |
| 2364 | `user` | `user` | correct |
| 4368 | `model` | `model` | correct |

**So the premise "maybe token 98 isn't a think token" is dead — it is exactly what the code says it
is, and this IS a thinking-capable model.** The bug is not the id. It is *where the token is put*.

### 1.2 The model's own chat template — and the divergence

`tokenizer.chat_template` EXISTS in the GGUF. `gguf_vocab.c` reads only `bos_token_id` and
`eos_token_id` (`:187`, `:193`) — **the template has never been read or compared against.** The
full template is ~300 lines, mostly tool-calling macros. The load-bearing parts, verbatim:

```jinja
{%- if (enable_thinking is defined and enable_thinking) or tools or messages[0]['role'] in ['system', 'developer'] -%}
    {{- '<|turn>system\n' -}}
    {#- Inject Thinking token at the very top of the FIRST system turn -#}
    {%- if enable_thinking is defined and enable_thinking -%}
        {{- '<|think|>\n' -}}
        {%- set ns.prev_message_type = 'think' -%}
    {%- endif -%}
    ...
    {{- '<turn|>\n' -}}
{%- endif %}
```

```jinja
{%- if add_generation_prompt -%}
    {%- if ns.prev_message_type != 'tool_response' and ns.prev_message_type != 'tool_call' -%}
        {{- '<|turn>model\n' -}}
    {%- endif -%}
{%- endif -%}
```

**THE DIVERGENCE, and it is the root cause:**

| | model's own template | `inference_server.c:287-429` |
|---|---|---|
| `<\|think\|>` placement | inside a **leading `<\|turn>system … <turn\|>` block**, before the user turn | **appended LAST**, immediately after `<\|turn>model\n` |
| gated by | `enable_thinking` | **nothing — unconditional** |
| generation prompt | bare `<\|turn>model\n` | `<\|turn>model\n` **+ `<\|think\|>`** |

The deployed prompt ends by handing the model a think token **at the position where the answer is
supposed to begin** — a position the template never produces. The model responds the only sensible
way: it opens a thought channel. That is why *every* question, fresh or repeated, can produce
`<|channel>thought`.

**Corroboration from the bench-off corpus.** The 8.40/10 quality score was produced by **llama.cpp
on the RTX 2070 using the model's own template** (`models/quality_results/gemma-4-E2B-it-Q4_K_M.txt`
— llama-cli sampler dumps, `[end of text]`). That file contains **zero** occurrences of
channel/thought/think across all 10 prompts; the answers are direct. Across the whole
`models/quality_results/` tree the ONLY file containing `<|channel>` is
`qat_google_q4_0_responses.txt` — a **JARVIS-engine** run (identifiable by its `<turn|>` markers) —
and it emits the identical `<|channel>thought / Here's a thinking process` pattern the box produces.

So the thinking channel correlates with **our prompt**, not with the model.

### 1.3 THE THOUGHT BOUNDARY EXISTS — item (4) answered

This was the gating question and the answer is yes. The template defines the wrapper explicitly:

```jinja
{{- '<|channel>thought\n' + thinking_text + '\n<channel|>' -}}
```

and strips it by splitting on the closer:

```jinja
{%- macro strip_thinking(text) -%}
    {%- for part in text.split('<channel|>') -%}
        {%- if '<|channel>' in part -%}
            {%- set ns.result = ns.result + part.split('<|channel>')[0] -%}
```

**A thought is `<|channel>`(100) `thought\n` … `\n` `<channel|>`(101). The END is token 101.**
Separate think/answer budgets are therefore implementable: generate under the think budget until
token 101, then switch to the answer budget. There is a real boundary to switch on.

### 1.4 BONUS DEFECT — the generation loop stops on the WRONG token

`tokenizer.ggml.eos_token_id` = **106** (`<turn|>`). `inference_server.c:500` stops only on token
**1** (`<eos>`), and its comment says so deliberately: *"Stop on <eos>=1, NOT eos_id=106 (that's
<turn|> which model emits first)."*

That comment has it backwards. `<turn|>` is the model's declared end-of-turn — **emitting it IS the
model saying it is finished.** Ignoring it means the loop keeps generating past a completed answer
until it hits token 1 or the cap. This is directly visible in the stored records and in a measured
probe: an answer that completed at token ~7 then emitted **23 consecutive `<turn|>` tokens** before
the 50-token cap — **46% of the budget spent on padding after the answer was done**, and that
padding is what puts `<turn|><turn|><turn|><eos>` into the episodic store and the console reply.

**Implication for the whole exercise: some of the "we need a bigger budget" problem is actually
"we are wasting the budget we have."** The stop-token fix must land before any budget is sized,
or the new budgets will be sized against wasted tokens.

### 1.5 What this means

The honest summary is that the thinking path is **misimplemented, not misused**:

- Thinking OFF = simply do not append token 98. The prompt then matches the model's own
  `add_generation_prompt` output exactly, and behaviour should match the llama.cpp bench-off —
  direct answers, no channel.
- Thinking ON = emit `<|turn>system\n<|think|>\n<turn|>\n` **before the user turn**, as the template
  does, and budget the `<|channel>…<channel|>` block separately from the answer.

---

## 2. D1 — latency

**Measured inputs, not assumed:**

- Live throughput, from the box's own JACT status digests this session: `tok=541 / 542 / 545 / 548 /
  560` → **5.41–5.60 tok/s**. Consistent with the 5.46 benchmark.
- Control-IN poll budget: `poll_max = 5000000` spin-iterations (`main_x86.c:3151`), documented at
  `:3050` as **"~60-120 s"**. **This is a RANGE IN A COMMENT, not a measurement.**

| config | tokens | time @5.46 tok/s |
|---|---|---|
| today | 50 combined | ~9 s |
| answer-only 192 | 192 | ~35 s |
| think 128 + answer 192 | 320 | ~59 s |
| the "200/200" illustration | 400 | **~73 s** |

**Finding: 400 tokens fits at the 120 s end of the poll budget and BREAKS at the 60 s end.** The
budget is an iteration count whose wall-time nobody has measured; sizing a 73 s inference against it
is exactly the kind of assumption that has bitten this project twice this week.

**Recommendation.**
1. **Measure `poll_max`'s real wall-time first** (cheap: one KVM run, timestamp the poll loop). Do
   not size budgets against a comment.
2. Then set `poll_max` **explicitly** from the chosen budget (`answer_tokens / tok_s × safety`),
   rather than inheriting the wake lane's number by accident.
3. Land §1.4's stop-token fix first, so the budget is spent on content.

**A UX point the operator should decide with real numbers in hand:** thinking ON at these caps means
**~1 minute per answer**. Thinking OFF at answer-192 is ~35 s. The operator prefers thinking ON;
that is a legitimate choice, but it is a 6–7× latency increase over today and should be a decision,
not a side effect.

## 3. D2 — transport

**Three ceilings, not one.** All three bite before 200 answer tokens (~800 bytes):

| ceiling | value | where | at 192 answer tokens |
|---|---|---|---|
| PB decode buffer | `text_out[512]` | `inference_server.c:541` | **overflows** |
| reply text | `CTRL_REPLY_TEXT_MAX 512` | `control_reply.h:60` | **clamped** |
| ring chunking | 15 slots × 240 B | `shmem_ipc.h:18,20` | 4 chunks — fits |

And the failure is **silent**: the chunk loop does `(void)rc` then `offset += chunk` regardless
(`inference_server.c:562-578`), while `shmem_ipc_send` returns −1 on a full ring. Today that is
latent only because 512 B can never exceed 3 chunks.

**What has to grow, and the cost:**
- `text_out[512] → [1024]`: +512 B on `handle_query`'s frame. The documented budget is <8 KB and
  the current frame is well under; needs a re-check, not a redesign.
- `CTRL_REPLY_TEXT_MAX 512 → 1024`: `CTRL_REPLY_MAX_LEN` 558 → 1070, so the `reply_frame` buffer and
  its `_Static_assert(558+42 <= 640)` must grow to ~1152. Total UDP frame ~1112 B — **still inside a
  1500 B MTU**, so no fragmentation and no new failure mode on the wire.
- **Fix the discarded `rc`** in the same change. Growing the payload without checking the return
  turns a latent silent-drop into a reachable one.

**Recommendation: do NOT put the thought on the wire in v1** (see D4). That keeps the answer the only
thing that has to fit, and 1024 B covers a 192-token answer with margin.

## 4. D3 — which lanes? (the brief's premise is wrong)

The brief states *"~14% of 12.2M queries are real inferences"*. **Measured, it is ~0.1%:**

- KVM gate this session: `[STATS] q=14,700 hits=12,451 infer=20` → **0.14%**.
- CLAUDE.md, #6/M3 bare metal: *"infer FROZEN at 17 while q reached 283,400"* → **0.006%**.

Cache growth serves essentially the whole synthetic workload; real inferences are rare. So the
*cost* argument for keeping the workload lane modest is much weaker than assumed.

**Recommendation: per-lane caps anyway — control-IN generous, workload stays at 50 — but for a
different and better reason than cost.** The workload lane exists to generate load and to produce
the `infer_last_tok_x100` figure; every historical benchmark, telemetry series and soak baseline was
produced at 50 tokens. Changing it silently re-bases all of them. Keep it at 50 for comparability,
and give control-IN — the lane a human actually reads — the real budget.

*(Bare-metal confirmation of the 0.1% figure is pending; the box is currently booted into JARVIS so
the durable log is not readable. The two independent measurements above already contradict the 14%
premise, so this does not gate the recommendation.)*

## 5. D4 — is the thought shown?

**Recommendation: hidden in v1, but COUNTED and surfaced as a number.**

- Sending the thought costs wire budget (D2) for text the operator mostly does not want.
- The model's own template strips it (`strip_thinking`) when re-encoding history — the model itself
  treats thought as non-durable. Storing it in the episodic/control-IN store would also pollute the
  recall corpus with reasoning scratch.
- But "thinking is on and I cannot tell" is exactly the visibility complaint in §7. So report
  `think_tokens` as a field: the console can say *"thought for 96 tokens"* without carrying them.
- Full thought text behind a console toggle is a clean v2 once the field exists.

**Corollary: the thought must be stripped before the answer is stored and replied.** With the §1.3
boundary this is deterministic — drop everything up to and including token 101.

## 6. D5 — toggle mechanism

**Recommendation: compile-time `JARVIS_THINKING`, default-ON, per project convention** — matching
`JARVIS_ROUTING` / `JARVIS_CONTROL_IN` / every other capability flag, with the OFF build compiling
the system-turn emission out entirely.

Runtime/per-query is deferrable and, notably, **the model supports it** (`enable_thinking` is a
template parameter). A per-query toggle would need a control-IN command verb or a JCTL field — that
is a 6-5-shaped change with its own security surface (a new inbound field is new untrusted input),
and it should not ride this milestone.

---

## 7. Surfacing the answer's SOURCE

With routing live there are three sources — SYSFACTS (box state), DECLINE (canned), INFER (Gemma) —
and the reply does not say which. The console therefore hedges: *"one bounded inference or a cache
hit on the box"*. The operator cannot tell whether the model spoke.

**The box already knows.** At all four `ctrl_send_reply` call sites, `rc` (the `route_class_t`) and
`served_locally` are in scope (`main_x86.c:2991, 3028, 3036, 3235`). Nothing needs to be computed —
only carried.

**JRPL v2's 10-byte header is fully allocated** (`control_reply.h:23-33`) — magic/version/verdict/
seq/tlen, no spare. So this needs **JRPL v3**, in receiver+console lockstep (the receiver
deliberately has no version fallback, by the M4b anti-downgrade rule).

**Recommendation: one v3 bump carrying BOTH new fields** — `route` (u8) and `think_tokens` (u16) —
rather than two lockstep changes a milestone apart. Both are small, both are needed for the same
validation task ("is this the model talking, and did it think?"), and the version-bump cost is
almost entirely fixed overhead.

Honesty constraint for the console: `route` is the **classifier's decision**, not proof of what
produced the text. A SYSFACTS reply is genuinely rendered from box state; an INFER reply genuinely
came from Gemma. But the label must not be rendered as a quality claim.

---

## 8. Milestone split

**M0 — host, no box.** The thought-boundary scanner and the answer/thought split as pure functions
(the `km2b_trigger.c` / `wake.c` precedent): given a token stream, find token 101, split, strip.
Host-tested + a CI step. Also the stop-token predicate (§1.4).

**M1 — the template correction + the stop-token fix, box, gated.** Move `<|think|>` from the tail to
a leading `<|turn>system\n<|think|>\n<turn|>\n` block behind `JARVIS_THINKING`; stop on `<turn|>`
(106) as well as `<eos>`. **This milestone alone may resolve the reported defect** — it is worth
gating and measuring *before* any budget change, precisely because it is the change that makes the
prompt well-formed. Gate: a fresh question returns prose, not a channel; `<turn|>` padding gone.

**M2 — separate budgets.** `think_max` / `answer_max`, sized from the D1 measurement, with the
poll budget set explicitly. Force-close the channel if the think budget is exhausted so an answer is
always produced.

**M3 — transport (D2).** `text_out` and `CTRL_REPLY_TEXT_MAX` growth, the `_Static_assert` update,
and the discarded-`rc` fix.

**M4 — JRPL v3**: `route` + `think_tokens`, receiver, console. Includes the §7 source display.

**M5 — flip** `JARVIS_THINKING` default-ON after a bare-metal validation.

Ordering rationale: M1 is first because it is the only one that fixes a **malformed prompt**; every
other milestone is sizing or plumbing around a prompt that should be correct first.

---

## 9. Honest ceiling

- Bigger budgets do not make the model better; they stop truncating it. The 8.40/10 bench-off figure
  was produced by llama.cpp on a GPU with the model's own template — matching that prompt shape is
  what this work is chasing, and matching it is **not** the same as reproducing that score.
- Nothing here has been demonstrated on seL4 yet. §1 is model-file and corpus evidence; §1.4's 46%
  padding figure is from a native-engine probe on the box, not from the deployed image.
- The `poll_max` wall-time is still a comment, not a measurement. Every latency number in D1 is
  arithmetic on a measured tok/s, not an observed end-to-end answer time.

## 10. Open questions for the strategist

1. **Does M1 alone fix it?** My prior is that it substantially does, and that budgets are then a
   quality knob rather than a bug fix. Recommend gating M1 alone and re-testing the three failing
   questions before committing to M2's numbers.
2. **Thinking default ON at ~1 min/answer, or OFF at ~35 s?** The operator asked for ON. I recommend
   shipping the toggle with ON as the default **only after** M1+M2 show the real end-to-end time on
   the box — if it lands near 90 s, that is a different product than they asked for.
3. **Workload lane at 50 for comparability** (§4) — confirm that re-basing the historical tok/s
   series is unacceptable, which is my assumption.
4. **One v3 bump for `route` + `think_tokens`** (§7), or route now and think later?
5. Should the §1.4 stop-token fix be split out and shipped on its own? It is small, it is a pure
   improvement, and it is independent of the thinking design. My prior: yes, fold it into M1 but
   gate it separately so a regression is attributable.
