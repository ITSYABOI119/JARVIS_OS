# Phase 7 — Memory Store: the measurement log

Every measured run of the household memory store, newest milestone last. The spec is
`phase7/docs/PHASE_7_MEMORY_DESIGN.md`; the bands quoted here are its §8, pre-registered before the
code that measures them existed. A number in this file was produced by a command in this file, on
the machine named, and a band that is missed is recorded as a miss with its cause — never retuned.

---

## MS0 — 2026-09-06 — the store, the rules and the benchmark on oracle candidates

**What landed.** The pure decision core (`registry`, `candidate`, `confidence`, `freshness`,
`rules` R1–R4, `retrieve`, `people`), the SQLite store with FTS5 (`schema`, `store`) carrying ingest
through the rules in one transaction with its audit rows, R5 recompute from spans, the R7 purge
cascade, personhood, and the full-text query with the spans attached; the seeded template corpus and
the harness over the five §8 sets. Standard library only — no model, no GPU, no numpy, no audio.

### The recorded run

Main PC, `%USERPROFILE%\.jarvis\voice\venv\Scripts\python.exe` (Python 3.12.6, SQLite 3.45.3 with
FTS5), 14.3 s wall:

```
python3 phase7/memory/bench_ms0.py --households 10 --days 14 --seed 1 \
    --latency-facts 100000 --out phase7/memory/bench/results/ms0_run.json --assert-bands
```

```
households : 10  seeds 1..10  days 14
aggregate  :
    coexist_recall                   1.0
    growth_drop_points               46.25
    growth_update_acc                0.5
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
    spouse_surfaced_households       10/10
    transfer_recall5                 0.0
    update_acc                       0.9625
latency    : p50 0.0985 ms  p99 0.354 ms over 100000 ingests, 100000 facts in the store
audit      : 0 violations
bands      :
    PASS audit==0
    PASS coexist_recall>=0.95
    FAIL growth_drop<=5
    PASS p99<=50ms
    PASS update_acc>=0.95
reported   : {"relation_precision": 1.0, "spouse_surfaced_day_mean": 8.0, "transfer_recall5": 0.0}
```

`--assert-bands` exited 1 on the missed band. The full per-household detail is
`phase7/memory/bench/results/ms0_run.json` (5,828 bytes, synthetic).

**Four of five bands met. One missed, and it is the finding of this milestone.**

| band (design §8, MS0 column) | measured | verdict |
|---|---|---|
| update accuracy ≥ 95 % | **96.25 %** | PASS |
| coexisting recall ≥ 95 % | **100 %** | PASS |
| audit completeness 100 % | **0 violations** | PASS |
| write p99 ≤ 50 ms at 100 k facts | **0.354 ms** (p50 0.0985 ms) | PASS, by two orders of magnitude |
| growth drop ≤ 5 points | **46.25 points** | **MISS** |
| transfer recall@5 | 0.0 | reported, not banded (moved to MS1) |
| relation precision at ≥ 0.80 | 1.0 over 2 surfaced edges per household | reported, not banded |

### The guess behaved exactly as the design predicts

The owner→partner `spouse` edge is built only from INFERRED evidence and its confidence is
recomputed from spans every time. Across **10 of 10** households it crossed the 0.80 surfacing
threshold on **day 8** — the fifth distinct supporting day (1, 3, 5, 6, 8), `1 − e^(−5/3) = 0.8111` —
dipped to 0.7364 after the day-11 contradiction, and finished at **0.8647** on day 14 (7 supporting
days against 1 contradicting, `1 − e^(−6/3)`). Identical in every household, which is what a
deterministic rule over dated evidence should give. The partner→owner edge, which she states, sits
at 1.0 from its first day; the contrast between the two is the point.

The design's amended §3.4 is load-bearing here: the partner earns personhood on day 3, and the edge
counts her day-1 spans as well, so the count is five supporting days by day 8 rather than seven.

### The missed band, and its cause

`growth_drop_points` measures how much update accuracy falls once 30× unrelated transcript is added
(1,110 filler candidates per household against ~37 gold ones). It fell **46.25 points** — from
96.25 % to 50 % — against a ≤ 5 band.

The cause was measured, not guessed. After growth, all four *lives_in* questions still answer
correctly and all four *works_as* questions return the **`lives_in`** fact about the same person.
Reading the BM25 values directly for `what does alex do for work` in a grown store of 1,126 current
facts:

```
  bm25=  -5.7317  person.lives_in    Bendigo        <- returned
  bm25=  -5.3411  person.works_as    a plumber      <- wanted
  docs matching 'alex'  : 4
  docs matching 'work'  : 557
  docs matching 'live'  : 557
  docs matching 'person': 1118
```

(SQLite's `bm25()` is negative-better.) The filler is about disjoint people in disjoint places, so
it never competes on the rare term `alex` — but it does put `works as` and `lives in` into half the
documents each, collapsing those terms' IDF to ≈ 0.02. Once the predicate word stops discriminating,
the only thing separating two facts about the same person is BM25's document-length normalisation,
and `alex person lives in Bendigo` (5 tokens) beats `alex person works as a plumber` (6 tokens).

A diagnostic pins it: rerunning one household with the filler moved onto a predicate the update set
never asks about gives **growth_drop_points = 0.0** with update accuracy 1.0 before and after. So
the drop is entirely predicate-term dilution in the full-text lane, not index size, not the ranker,
and not a collision with the gold facts.

**Nothing was retuned.** The corpus, the rendering (`"<subject display name> <predicate words>
<object_text>"`), the ranker weights, `TAU_DAYS` and the surfacing threshold are all as the design
specifies. This is a pre-registered prediction that the measurement falsified, which is what the
benchmark is for. It is the same shape as the transfer band the design already moved to MS1: the
full-text lane alone cannot carry a question whose discriminating word is common. The disposition —
whether the band belongs at MS1 beside the embedding lane, or whether MS0 owes a fix — is the
strategist's.

One further honest number: update accuracy is 96.25 %, not 100 %, and the single failure is real.
In seed 2 the partner's habit is *"cycles to work"*, so `what does juno do for work` returns that
habit rather than her `works_as` fact. A word-level lane cannot tell the two apart.

### The CI-sized run

```
python3 phase7/memory/bench_ms0.py --households 3 --days 14 --seed 1 \
    --latency-facts 20000 --out /tmp/ms0_ci.json
```

4.3 s wall: `update_acc 0.9583 · coexist_recall 1.0 · growth_drop_points 45.8333 ·
relation_precision 1.0 · spouse_surfaced_day_mean 8.0 (3/3) · transfer_recall5 0.0 ·
latency p50 0.1272 ms p99 0.4905 ms over 20,000 ingests · 0 audit violations`. This is the shape
CI runs; `--assert-bands` is deliberately not passed there while `growth_drop<=5` stands as a
published miss, and the reason is written above the step in `ci.yml`.

### Tests

`phase7/memory/test_memory_logic.py` — **114 checks, all passing** in three venues: WSL Python
3.12.3 / SQLite 3.45.1 (the CI form), the voice venv's Python 3.12.6 / SQLite 3.45.3, and the
GitHub runner's Python 3.12.14. T1–T8 cover the pure core, T9–T16 the store on in-memory databases,
T17 the corpus.

Mutation evidence — the unmutated control ran first every time, and every mutant was applied to a
throwaway copy outside the repo:

| mutant | expected | result |
|---|---|---|
| A: T5b expectation `supersede` → `history` | fail by name | `FAIL T5b newer stated_owner -> supersede`, 66/67 |
| B: `rules.decide` lets a history candidate keep its closes | fail by name | `FAIL T5j a history candidate closes nothing…`, 66/67 |
| C: T10c expects the superseded row still current | fail by name | `FAIL T10c exactly one current row, the new one`, 101/102 |
| D: the superseded fact is left in the full-text index | fail by name | **survived at first — 102/102** |

Mutant D is worth recording rather than hiding. It survived because two independent mechanisms keep
a superseded fact out of an answer — the `valid_to is null` join filter *and* the FTS delete — and
the test only exercised the first. A check on the index itself (`T14c2`) was added, after which
mutant D fails by name (`FAIL T14c2 the superseded row is gone from the full-text index itself`).
The gap was in the test, not the store.

### Honest scope

MS0 measures **the store, not a memory system**. The candidates are ORACLE — written by the corpus
generator, not extracted from speech — so nothing here says anything about extraction quality; that
is MS1's bake-off and it is the binding constraint the research names. The retrieval is the
**full-text lane alone**; the embedding lane lands at MS1, which is why transfer recall is 0.0 and
is reported rather than banded. The corpus is a **seeded template**, not language-model output and
not a transcript: its sentences are short and regular, which flatters BM25 on the update set and
makes the growth result, if anything, generous. Relation precision is 1.0 **by construction** — the
oracle plants only true relations, so the number says the store surfaced what it was given and
nothing about whether a real extractor would propose a wrong one.

Nothing in this milestone touched the box, the Pi, a GPU, a recording, a transcript, or anything
about the owner or his household. No row here supports the words "knows", "understands" or
"remembers your life"; what MS0 shows is that a belief can be written, superseded, contradicted,
recalled with its evidence, and purged, with every loss auditable — and that at household scale the
write path costs about a tenth of a millisecond.

---

## MS0.1 — 2026-09-07 — the predicate-aware full-text lane; the growth band re-measured against its negative control

**The disposition.** MS0 met four of five bands and missed `growth_drop<=5` at 46.25 points, with the cause
measured rather than argued: 30× filler about disjoint people and places put `works as` and `lives in` into
roughly half of all rows, collapsing those terms' IDF to ≈ 0.02, after which BM25's length normalisation
returned the SHORTER of two facts about the same person. The strategist's disposition, now in the design
(§6), is the K-b instinct one level out: the registry carries a static, human-reviewed QUERY VOCABULARY per
predicate, and a question matching **exactly one** predicate's vocabulary restricts the fact and preference
lookups to that predicate; none or several leaves the lookup open. The rule SELECTS a predicate the store
already types and can never invent one. The same slice puts preference rows into the index for the first
time — MS0's transfer number was 0 for a structural reason before the vocabulary one.

### The vocabulary (`registry.QUERY_VOCAB`, nine pairwise-disjoint sets, matched on raw lower-cased tokens)

| predicate | words |
|---|---|
| `person.name` | name, named, called, call |
| `person.relation_to` | wife, husband, married, marry, spouse, partner, related, relationship, relation, sister, brother, mother, father, mum, dad, son, daughter, friend, friends, colleague, family |
| `person.lives_in` | live, lives, living, lived, home, address, reside, resides, residing |
| `person.works_as` | work, works, working, worked, job, jobs, occupation, employed, employer, career, profession |
| `person.habit` | habit, habits, routine, routines, usually, regularly |
| `person.trait` | trait, traits, personality, tends, tend, tendency |
| `household.topic` | talk, talks, talked, talking, discuss, discussed, discussing, topic, topics, conversation, conversations |
| `household.routine` | schedule, schedules, household, weekly, chores, chore |
| `owner.prefers` | like, likes, liked, love, loves, loved, prefer, prefers, preferred, favourite, favorite, hate, hates, hated, dislike, dislikes, avoid, avoids, enjoy, enjoys, want, wants |

Inflections are spelled out because matching is on raw tokens, never stemmed. Disjointness and key-validity
are asserted by the suite (T18l, T18m), so a word added to two sets fails the build rather than quietly
making the hint ambiguous.

### Run 1 — THE NEGATIVE CONTROL (the hint off), Main PC, the voice venv, 14.5 s

```
python.exe phase7/memory/bench_ms0.py --households 10 --days 14 --seed 1 \
    --latency-facts 100000 --no-predicate-hint --out phase7/memory/bench/results/ms0_1_control_off.json
```

```
households : 10  seeds 1..10  days 14  predicate_hint OFF (negative control)
aggregate  :
    coexist_recall                   1.0
    growth_drop_points               46.25
    growth_update_acc                0.5
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
    spouse_surfaced_households       10/10
    transfer_recall5                 0.0
    update_acc                       0.9625
latency    : p50 0.0972 ms  p99 0.3468 ms over 100000 ingests, 100000 facts in the store
audit      : 0 violations
bands      :
    PASS audit==0
    PASS coexist_recall>=0.95
    FAIL growth_drop<=5
    PASS p99<=50ms
    PASS update_acc>=0.95
```

**This is the load-bearing result of the milestone.** Every band-relevant field reproduces MS0's recorded run
exactly — `update_acc 0.9625`, `coexist_recall 1.0`, `growth_update_acc 0.5`, `growth_drop_points 46.25`,
`transfer_recall5 0.0`, `relation_precision 1.0`, `spouse_surfaced_day_mean 8.0`, 0 audit violations — with
only latency free to differ (p50 0.0972 against MS0's 0.0985, p99 0.3468 against 0.354). Nothing but the hint
changed, so the ON numbers below measure the hint and nothing else.

### Run 2 — the predicate-aware lane (the hint on), the same command plus `--assert-bands`, 14.5 s

```
households : 10  seeds 1..10  days 14  predicate_hint ON
aggregate  :
    coexist_recall                   1.0
    growth_drop_points               0.0
    growth_update_acc                1.0
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
    spouse_surfaced_households       10/10
    transfer_recall5                 0.0
    update_acc                       1.0
latency    : p50 0.099 ms  p99 0.3558 ms over 100000 ingests, 100000 facts in the store
audit      : 0 violations
bands      :
    PASS audit==0
    PASS coexist_recall>=0.95
    PASS growth_drop<=5
    PASS p99<=50ms
    PASS update_acc>=0.95
```

`--assert-bands` exited 0. **All five bands are met.**

| band (design §8, MS0 column) | hint OFF | hint ON | verdict |
|---|---|---|---|
| update accuracy ≥ 95 % | 96.25 % | **100 %** | PASS |
| coexisting recall ≥ 95 % | 100 % | **100 %** | PASS |
| audit completeness 100 % | 0 violations | **0 violations** | PASS |
| write p99 ≤ 50 ms at 100 k | 0.3468 ms | **0.3558 ms** | PASS |
| growth drop ≤ 5 points | **46.25** (MISS) | **0.0** | PASS |
| transfer recall@5 | 0.0 | 0.0 | reported, not banded |
| relation precision at ≥ 0.80 | 1.0 | 1.0 | reported (oracle) |

### Per household, both runs

```
seed |  OFF update  drop  |  ON update  drop  | spouse day OFF/ON | ended leaks
  1  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  2  |    0.875    37.5   |    1.0      0.0   |       8/8         |  0/0
  3  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  4  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  5  |    0.875    37.5   |    1.0      0.0   |       8/8         |  0/0
  6  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  7  |    0.875    37.5   |    1.0      0.0   |       8/8         |  0/0
  8  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
  9  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
 10  |    1.0      50.0   |    1.0      0.0   |       8/8         |  0/0
```

The three seeds that lost an update question at MS0 (2, 5, 7 — the habit *"cycles to work"* answering a work
question) now answer all eight, and the write path is untouched: the spouse edge still surfaces on day 8 in
10/10 households either way, which is the check that the hint changed retrieval and nothing else.

### Tests

`phase7/memory/test_memory_logic.py` — **144 checks, all passing** in WSL Python 3.12.3 / SQLite 3.45.1 (the
CI form), the voice venv's 3.12.6 / 3.45.3, and the GitHub runner. T18 covers the hint and the vocabulary
(including `does alex live near where he works` → None, two predicates, and the disjointness assertion),
T19 the MS0 F2 collision resolved in the store, T20 preferences through index / close / purge, T21 the
negative control in-process.

One mutant, control first (144/144), applied to a throwaway copy outside the repo: **`predicate_hint`
returning `None` always** →

```
FAIL T18 hint('where does alex live') -> person.lives_in got None      (and seven more T18 rows)
FAIL T21c hint ON lifts seed-2 update accuracy to 1.0   0.875
FAIL T21d hint ON removes the growth drop entirely      37.5
134/144 checks passed
```

The mutant's T21 values fall back to exactly MS0's seed-2 figures, which is the sharpest available evidence
that the hint is the whole mechanism and that nothing else moved.

### A defect fixed on the way, latent since MS0

Purging a cluster whose preference had already been ENDED raised `sqlite3.DatabaseError: database disk image
is malformed`. FTS5's `'delete'` command on a contentless table does not tolerate being asked twice: the row
had left the index at close time, and deleting an absent rowid corrupts the index outright. The same hazard
was **latent for facts since MS0** — no test had yet purged a *superseded* fact. Both paths now go through
`_drop_from_index`, which deletes only while `valid_to is null`, and T20i purges a superseded fact and reads
the index back.

### Honest scope

The hint is a **rule over a fixed vocabulary**, measured on a **template corpus with fixed query shapes**. It
is the deterministic baseline of the design's "route by fact type"; the general mechanism is the embedding
lane at MS1. A real question whose words fall outside the vocabulary gets exactly the MS0 behaviour —
unrestricted — which is why the rule is "exactly one match" rather than "best guess": a wrong restriction
hides the answer completely, while no restriction only leaves MS0 in place.

Three corpus queries hint other than their own predicate, and none was patched by adding a word (the table is
human-reviewed, not a tuning knob):

- `what household routine do we keep` → **None**, because *household* is in `household.routine` while
  *routine* is in `person.habit`. It falls back to the MS0 behaviour and still scores 1.0, but the most
  natural household-routine question straddles two sets.
- `when should we schedule the appointment` → **`household.routine`** and `booking a flight time that works`
  → **`person.works_as`**; both are transfer scenarios, so the hint restricts them away from the preference
  table they are meant to find. Neither changes the reported number (transfer is 0.0 regardless — the
  scenarios share no word with their preference), but they show the rule's real failure mode: a scenario
  query that happens to contain one vocabulary word is restricted to the wrong predicate.

`transfer_recall5` stays **0.0** and is still REPORTED, never banded. The structural zero is gone —
preferences are in the index now, and T20a proves a preference question reaches one — so the remaining zero
is purely the vocabulary gap the embedding lane exists for. `relation_precision` stays 1.0 **by
construction**: the oracle plants only true relations, so it says the store surfaced what it was given.
Nothing here is measured on real speech, on an extractor, or on the owner.

---

## MS1a — 2026-09-08 — the embedding lane, fused; the transfer band measured

### The first attempt, and why the design changed

MS1a was pre-registered with reciprocal-rank fusion AS the `lane_score`, then multiplied by the ranker's
weights. Run with **no embedder at all**, that regressed the update set from 100 % to **50 % in all ten
households**, and the mechanism was measured rather than argued. For `where does juno live`, the fts_fact lane:

```
 rank | source_kind  | MS0.1 lane | MS0.1 score | RRF lane | RRF score  | text
   1  | stated_other |     1.0000 |      0.8000 |  0.01639 | 0.013115 | juno person lives in Launceston   <- CORRECT
   2  | stated_other |     0.5000 |      0.4000 |  0.01613 | 0.012903 | juno person habit cycles to work
   3  | stated_other |     0.3333 |      0.2667 |  0.01587 | 0.012698 | juno person habit bakes on fridays
   4  | stated_other |     0.2500 |      0.2000 |  0.01562 | 0.012500 | juno person works as a pharmacist
   5  | stated_owner |     0.2000 |      0.2000 |  0.01538 | 0.015385 | jo person lives in Sydney         <- WRONG PERSON, won
```

RRF's output is deliberately flat — five ranks span 6.6 % — while `w_source` differs by 20–40 %, so a
`stated_owner` fact about ANOTHER PERSON at rank 5 beat the correct `stated_other` fact at rank 1. An
isolation probe changed only the fused constant's spread, kept every other change, and every MS0.1 number
returned exactly: the rewrite was faithful and the specification was the defect. The design was corrected to
three steps — weights order a lane's own candidates, reciprocal rank is the FINAL relevance multiplied by
nothing, the weighted score breaks ties — and this milestone is the second attempt.

### The venue

Main PC, the voice venv (`%USERPROFILE%\.jarvis\voice\venv\Scripts\python.exe`): Python 3.12.6,
sentence-transformers 5.2.0, torch 2.5.1+cu121, CUDA on an RTX 2070, numpy 2.2.6. The model loads from the
DEFAULT Hugging Face cache with `local_files_only=True` — **2.267–3.167 s**, dim 1024. Embeddings are written
by `embed_pending` AFTER ingest and never during it, so the write path stays embedding-free and the p99 band
still measures the store rather than a GPU.

### Run C — the control, no embedder (11.8 s)

```
households : 10  seeds 1..10  days 14  predicate_hint ON  embedder none
    coexist_recall                   1.0
    growth_drop_paraphrase_points    0.0
    growth_drop_points               0.0
    growth_update_acc                1.0
    growth_update_acc_paraphrase     0.5
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
    spouse_surfaced_households       10/10
    transfer_recall5                 0.0
    update_acc                       1.0
    update_acc_paraphrase            0.5
latency    : p50 0.0666 ms  p99 0.2801 ms over 100000 ingests, 100000 facts in the store
audit      : 0 violations
bands      : PASS audit==0 · PASS coexist_recall>=0.95 · PASS growth_drop<=5 · PASS p99<=50ms · PASS update_acc>=0.95
```

`--assert-bands` exited 0. **Every band-relevant aggregate equals MS0.1's** — `update_acc 1.0`,
`coexist_recall 1.0`, `growth_update_acc 1.0`, `growth_drop_points 0.0`, `transfer_recall5 0.0`,
`relation_precision 1.0`, `spouse_surfaced_day_mean 8.0`, audit 0. The correction holds, and the ordering
change is isolated to the merge exactly as the design says.

### Run A — the embedding lane, symmetric query, BANDED (2 m 54 s)

```
embedder   : Qwen/Qwen3-Embedding-0.6B dim 1024 loaded in 3.167 s on cuda (sentence-transformers 5.2.0)
households : 10  seeds 1..10  days 14  predicate_hint ON  embedder qwen
    coexist_recall                   1.0
    growth_drop_paraphrase_points    1.25
    growth_drop_points               2.5
    growth_update_acc                0.8875
    growth_update_acc_paraphrase     0.4625
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
    spouse_surfaced_households       10/10
    transfer_recall5                 0.05
    update_acc                       0.9125
    update_acc_paraphrase            0.475
latency    : p50 0.0667 ms  p99 0.2681 ms over 100000 ingests, 100000 facts in the store
audit      : 0 violations
bands      : PASS audit==0 · PASS coexist_recall>=0.95 · PASS growth_drop<=5 · PASS p99<=50ms
             FAIL transfer_recall5>=0.60 · FAIL update_acc>=0.95
transfer/topic: {early mornings 0, long drives 5, loud music 0, spicy food 1} of 30 scenarios each
```

**Two bands missed, and they share ONE cause.**

| band | run C (no lane) | run A (with the lane) | verdict |
|---|---|---|---|
| update accuracy ≥ 95 % | 100 % | **91.25 %** | **MISSED** |
| transfer recall@5 ≥ 60 % | 0 % (reported) | **5 %** | **MISSED** |
| coexisting recall ≥ 95 % | 100 % | 100 % | PASS |
| audit completeness | 0 violations | 0 violations | PASS |
| write p99 ≤ 50 ms at 100 k | 0.2801 ms | 0.2681 ms | PASS |
| growth drop ≤ 5 points | 0.0 | 2.5 | PASS (read beside the absolute: 100 % → 88.75 %) |

### The one cause: cross-lane source weighting no longer exists

Every update failure has the same shape — **a SPAN beats the FACT extracted from it**:

```
BAD what job does jo work as   want teacher
    span  rel=0.032522 tie=0.5513 lanes={fts_span: 1, vec: 2}  i work as a teacher
    fact  rel=0.032266 tie=1.0000 lanes={fts_fact: 1, vec: 3}  jo person works as a teacher

BAD what job does kit work as  want librarian
    span  rel=0.032787 tie=0.5513 lanes={fts_span: 1, vec: 1}  i work as a librarian
    fact  rel=0.032522 tie=1.0000 lanes={fts_fact: 1, vec: 2}  kit person works as a librarian
```

The span is rank 1 in its full-text lane and one rank HIGHER in the vector lane, so its fused relevance beats
the fact's by 0.00026 — and the tiebreak that would have chosen the fact (1.0 against 0.55) never runs,
because it only applies when relevance is equal.

The design says *"a stated fact outranks the raw utterance it came from"*. That property used to live in the
multiplicative ranker, where `w_source` 1.0 against 0.6 compared a fact with a span directly. The correction
moved the weights INSIDE each lane — and a fact and a span are never in the same lane, so nothing compares
them any more. **Run C passes only because of an accident:** with no vector lane each row sits in exactly one
lane, both at rank 1, so the relevances TIE at 1/61 and the tiebreak does choose the fact. Adding the vector
lane gives both rows a second rank, the tie breaks on cosine order first, and the utterance wins.

The transfer failure is the same cause seen from the other side. The gold preference's cosine is respectable
but generic chatter spans out-rank it:

```
scenario                                   | topic          | cos   | gold rank | what won instead
what should i order at the restaurant …    | spicy food     | 0.414 |   miss    | span  we should decide
planning dinner for our anniversary        | spicy food     | 0.451 |   miss    | span  we planned the week together
which cuisine would suit us both           | spicy food     | 0.531 |   miss    | span  we are both on the lease
picking a venue for the party              | loud music     | 0.426 |   miss    | span  remember the bins go out on tu…
is that bar going to be too much           | loud music     | 0.516 |   miss    | fact  household household routine bi…
choosing background sound for the evening  | loud music     | 0.532 |   miss    | span  sounds good to me
when should we schedule the appointment    | early mornings | 0.505 |   miss    | span  we should decide
is a sunrise start reasonable              | early mornings | 0.599 |   miss    | fact  alex person works as a plumber
booking a flight time that works           | early mornings | 0.498 |   miss    | fact  alex person works as a plumber
how should we travel to the coast          | long drives    | 0.476 |   miss    | span  we should decide
would a train be better than the car       | long drives    | 0.533 |   miss    | span  we shared the school run
planning the route for our holiday         | long drives    | 0.544 |   miss    | span  we planned the week together
```

A household carries roughly seventy spans against four preferences, and generic conversational filler
(*"we should decide"*, *"sounds good to me"*) embeds close to any conversational question. With no mechanism
ranking a typed belief above a raw utterance across lanes, the spans crowd the top five out.

### Run B — the instruction-prefixed query, REPORTED, never adopted (2 m 28 s)

```
embedder   : Qwen/Qwen3-Embedding-0.6B+instruct dim 1024 loaded in 2.267 s on cuda
    transfer_recall5                 0.2833          (against 0.05 symmetric)
    update_acc                       0.9125          (identical)
    update_acc_paraphrase            0.45
    growth_drop_points               5.0
    coexist_recall                   1.0
    relation_precision               1.0
    spouse_surfaced_day_mean         8.0
transfer/topic: {early mornings 12, long drives 12, loud music 0, spicy food 10} of 30 each
```

The instruction prefix is **5.7× better on transfer** (28.3 % against 5.0 %) and identical on the update set.
It is reported, not adopted: the design fixes the symmetric form, C/M0.5 measured that form better for
query-to-query retrieval, and one corpus is not grounds to change it. It is, however, the strongest single
signal in this milestone about where the remaining transfer headroom is. `loud music` scores 0 in both arms.

### Per household, run A

```
seed |  update  para   growth  growth-para  transfer  spouse
   1 |  1.000   0.500   1.000     0.500       0.000      8
   2 |  0.750   0.500   0.750     0.375       0.083      8
   3 |  0.875   0.500   0.875     0.500       0.167      8
   4 |  1.000   0.500   1.000     0.500       0.083      8
   5 |  1.000   0.500   0.875     0.500       0.000      8
   6 |  1.000   0.500   1.000     0.500       0.000      8
   7 |  1.000   0.500   1.000     0.625       0.083      8
   8 |  1.000   0.500   0.875     0.375       0.000      8
   9 |  0.500   0.250   0.500     0.250       0.083      8
  10 |  1.000   0.500   1.000     0.500       0.000      8
```

The spouse edge still surfaces on day 8 in 10/10 households in every run — the write path is untouched by any
of this, which is the check that the lane changed retrieval and nothing else.

### The paraphrase set

Two paraphrases per updated slot, worded entirely outside `QUERY_VOCAB` so the predicate hint cannot fire
(T27 asserts both the disjointness and that `predicate_hint` returns None for every one). Reported, never
banded: **50.0 %** with the full-text lane alone, **47.5 %** with the vector lane. The lane does not help here
either, for the same reason — spans win the top slots.

### The vocabulary move

`routine` and `routines` moved from `person.habit` to `household.routine`, MS0.1's report F1. The corpus's own
question, *"what household routine do we keep"*, previously matched two sets and was left unrestricted; it now
resolves to `household.routine`. The nine sets remain pairwise disjoint (T18m, T26f).

### Tests

`phase7/memory/test_memory_logic.py` — **190 checks**, all passing in WSL Python 3.12.3 (the CI form), the
voice venv's 3.12.6. The GitHub runner reports ONE FEWER, because the numpy-agreement check skips with a
printed note where numpy is absent — measured at commit 1, where the runner logged 177 against 178 locally. New at MS1a: T22 (vectors), T23 (RRF maths), T23h (`lane_order` — the
weights inside a lane), T23i (`rank` — relevance first, tiebreak second), T24/T24e (the vector lane and the
hint never restricting it), T25 (the embedding lifecycle), T26 (the vocabulary move), T27 (the paraphrase set).
T8h–T8j were retargeted from `rank` to `lane_order`, because the behaviour they asserted moved there by design.

Mutants, control first each time, on throwaway copies outside the repo:

| mutant | result |
|---|---|
| `rank` sorts by tiebreak before relevance | `FAIL T23i4 relevance outranks the tiebreak` · 177/178 |
| a paraphrase reintroduces a vocabulary word (`live`) | `FAIL T27b`, `T27c`, `T27e` · 187/190 |

### Honest scope

A **template corpus** with fixed query shapes, ORACLE candidates, and no extractor — MS1b is the bake-off, and
until it runs nothing here says anything about extraction quality. The transfer cosines measured tonight are
**thin** (0.41–0.60 to the gold preference) and the corpus's generic chatter sits in the same range, so the
transfer number is as much a statement about span density as about the embedder. The vector lane does do the
job the design asked of it — T24e shows a question the hint mis-routed still reaching its row by meaning — but
at the top of the result list that gain is currently spent competing with raw utterances. Relation precision
stays 1.0 **by construction** (the oracle plants only true relations). Nothing here is measured on real speech
or on the owner; every store was in memory.


## MS1a.2 — 2026-09-08 — one vector lane, the claim weight on the row, function words out, evidence collapsed; the bands re-measured

**MS1a.1 was withdrawn before any GPU run, and that is a finding rather than a tidy-up.** It specified TYPED lanes —
the vector lane split into `vec_fact` / `vec_pref` / `vec_span` — so that R2's source rank could act across lanes.
Partitioning a cosine-ordered lane restarts the ranks, so the best row of EVERY table earns a full rank-1 term however
unlike the query it is. The coder's T24c trace: on the untouched MS0.1 fixture a preference at cosine **1.000** and a
fact at cosine **0.000** both landed at rank 1 of their own partition, tied at `1/61 = 0.016393`, and the tie-break
(the within-lane weighted score, 1.000000 vs 0.994879 — a stated fact does not decay, a preference does) handed first
place to the **cosine-0.0 fact**. Reciprocal rank is ordinal; it cannot see the 0.000. The design amendment `b166e22`
stands in the record as what was proposed; the code was never committed. Its two test ideas are reused here: read the
weight from the shipped constant, and give the fixture a decoy row so the weight alone decides.

**What MS1a.2 does instead — four rules, each with a measured reason.**

1. **ONE vector lane**, exactly as at MS1a. No partition, so no row earns rank-1 credit for being the best of an
   irrelevant table. T24c passes unchanged (`lanes == {"vec": 1}`).
2. **The claim-status weight sits on the ROW:** `relevance = Σ over lanes w_claim(row) / (60 + rank)`, `w_claim` 1.0
   for a belief and `W_SOURCE["inferred"]` = 0.6 for a span — R2's own constant one level up, never a new knob.
   Measured reason: MS1a §3.8, where every update miss was a span beating the fact extracted from it.
3. **Function words leave the FULL-TEXT query** (`registry.STOPWORDS`, 125 words, disjoint from every `QUERY_VOCAB`
   set by assertion). Measured reason: MS1a §3.9. **Kept as the default by the measurement below, not by argument.**
4. **Evidence collapse:** a span that is an evidence span of a belief already in the fused set is not a second
   result — the belief carries it in `spans`, with its verbatim text. The utterance and the fact extracted from it
   never compete.

### Rule 3, decided by measurement

The rule was **pre-registered before any GPU number was seen** (`PROMPT-MEMORY-MS1A2-RESUME.md` R2), verbatim:

> Run A (qwen, symmetric, rule 3 ON) and A′ (qwen, symmetric, function words KEPT) on the 10 households.
> **KEEP rule 3 iff `transfer_recall5(A) − transfer_recall5(A′) > 0.05` AND `update_acc(A) ≥ update_acc(A′)`.**
> Otherwise **DROP** it from the default (the switch and the list stay, so the arm remains measurable).

Measured, and the verdict computed by code from those two conditions:

| quantity | value |
|---|---|
| `transfer_recall5(A)` — function words dropped | **0.3583** |
| `transfer_recall5(A′)` — function words kept | **0.2833** |
| delta | **0.0750** > 0.05 ✓ |
| `update_acc(A)` / `update_acc(A′)` | **1.0000 / 1.0000** — `≥` holds ✓ |
| **verdict** | **KEEP** |

**The cost is real, was measured first, and is confined.** Dropping function words costs one of the eight update
questions in the HINT-OFF control on 3 of 10 seeds (mean update accuracy there 0.9625 → 0.9250, −3.75 points), and
the mechanism is a single word. The question is `what job does juno work as`; with the words kept it returns
`juno person works as a pharmacist`, with them dropped it returns `juno person habit cycles to work`. The
discriminator is **"as"** — the store renders the predicate as `"… works **as** a …"` while the competing habit
renders `"… cycles to work"`; both contain "work", only the answer contains "as". So rule 3 drops part of the
predicate's own rendered surface form from the query while leaving it in the indexed text. With the hint ON — the
deployed path, and every run below — the lookup is already restricted to `person.works_as`, the competitor never
enters the lane, and both numbers stay perfect. `T21a`/`T21b` are re-pinned to the measured hint-OFF values (0.75 /
25.0, from MS0's 0.875 / 37.5) with that mechanism in a comment; `T21c`/`T21d` keep their job of showing the hint
lifting them to 1.0 / 0.0.

**A′ and A have IDENTICAL vector-lane diagnostics** (`transfer_gold_pref_rank1` 0.6917, `transfer_gold_vec_rank_mean`
78.483 in both) because rule 3 touches the full-text lane only. Its whole 0.075 of transfer therefore comes from
removing full-text competitors from the fused set, not from changing what the embedder sees.

### Run C — no embedder — equals MS0.1

`--households 10 --days 14 --seed 1 --latency-facts 100000 --embedder none --assert-bands`, exit 0
(`ms1a2_control_none.json`):

| aggregate | MS0.1 | MS1a.2 run C |
|---|---|---|
| update_acc | 1.0 | **1.0** |
| coexist_recall | 1.0 | **1.0** |
| growth_update_acc | 1.0 | **1.0** |
| growth_drop_points | 0.0 | **0.0** |
| transfer_recall5 | 0.0 | **0.0** |
| relation_precision | 1.0 | **1.0** |
| spouse_surfaced_day_mean / households | 8.0, 10/10 | **8.0, 10/10** |
| audit violations | 0 | **0** |

All five bands PASS; p50 0.0981 ms, p99 **0.3698 ms** over 100,000 ingests. **All four rules leave the no-embedder
path exactly where MS0.1 left it.**

### Run A — the embedding lane, the default — two bands MISSED

`--embedder qwen … --assert-bands` → exit 1, `BANDS MISSED: growth_drop<=5, transfer_recall5>=0.60`
(`ms1a2_run.json`).

| band | expected | measured | verdict |
|---|---|---|---|
| update_acc | ≥ 0.95 | **1.0000** | **MET** — MS1a measured 0.9125; the cross-lane loss is closed |
| coexist_recall | ≥ 0.95 | **1.0000** | MET |
| audit violations | 0 | **0** | MET |
| p99 write latency | ≤ 50 ms | **0.3796 ms** | MET |
| growth drop | ≤ 5 pts | **7.5** (absolute 1.0000 → 0.9250) | **MISSED** |
| transfer recall@5 | ≥ 0.60 | **0.3583** | **MISSED** — MS1a measured 0.0500 |
| `transfer_gold_pref_rank1` | reported | **0.6917** (mean pref rank 1.525) | reported |
| `transfer_gold_vec_rank_mean` | reported | **78.483** | reported |

Per household, read from the JSON, every mean recomputed and checked against the stored aggregate (MS1a's near-miss
does not repeat — all six recomputations matched to 5e-5):

| seed | update | coexist | growth_upd | drop | transfer | pref_rank1 | pref mean | vec mean |
|---|---|---|---|---|---|---|---|---|
| 1 | 1.0000 | 1.0000 | 1.0000 | 0.0 | 0.3333 | 0.6667 | 1.500 | 87.500 |
| 2 | 1.0000 | 1.0000 | 0.8750 | 12.5 | 0.5000 | 0.5833 | 1.667 | 71.250 |
| 3 | 1.0000 | 1.0000 | 0.8750 | 12.5 | 0.5833 | 0.5833 | 1.750 | 44.583 |
| 4 | 1.0000 | 1.0000 | 1.0000 | 0.0 | 0.3333 | 0.6667 | 1.500 | 67.417 |
| 5 | 1.0000 | 1.0000 | 1.0000 | 0.0 | 0.3333 | 0.6667 | 1.500 | 80.167 |
| 6 | 1.0000 | 1.0000 | 1.0000 | 0.0 | 0.3333 | 0.6667 | 1.583 | 93.417 |
| 7 | 1.0000 | 1.0000 | 1.0000 | 0.0 | 0.2500 | 0.7500 | 1.583 | 90.833 |
| 8 | 1.0000 | 1.0000 | 0.8750 | 12.5 | 0.3333 | 0.7500 | 1.500 | 76.500 |
| 9 | 1.0000 | 1.0000 | 0.6250 | **37.5** | 0.2500 | 0.7500 | 1.500 | 92.500 |
| 10 | 1.0000 | 1.0000 | 1.0000 | 0.0 | 0.3333 | 0.8333 | 1.167 | 80.667 |
| **mean** | **1.0000** | **1.0000** | **0.9250** | **7.5** | **0.3583** | **0.6917** | 1.525 | 78.483 |

**Every update miss traced: there are none.** `update_acc` is 1.0000 in all ten households, so the §3.8 shape this
milestone was built to close leaves no residue in the update set.

**The growth drop, traced.** It is 0.0 in six households and comes from four: seeds 2, 3 and 8 at 12.5 points and
seed 9 at 37.5. It **arrives with the vector lane** — run C measures 0.0, A′ 7.5, B 3.75 — so 30× unrelated
transcript hurts only once embeddings are in play, by adding rows that crowd the fused top five. It is the same
crowding the transfer diagnostic measures, seen from the other side.

### The two diagnostics, per topic — what they say about the transfer miss

Run A (all 120 scenarios), then run B beside it:

| topic | n | recalled (A) | pref_rank==1 | pref mean | vec mean | vec range |
|---|---|---|---|---|---|---|
| early mornings | 30 | 13 | 0.800 | 1.200 | 51.5 | 17..106 |
| long drives | 30 | 17 | 1.000 | 1.000 | 42.0 | 1..113 |
| **loud music** | 30 | **0** | **0.367** | 2.267 | **132.9** | 17..178 |
| spicy food | 30 | 13 | 0.600 | 1.633 | 87.5 | 1..170 |

| arm | transfer | pref_rank==1 | vec_rank mean | vec_rank ≤ 5 |
|---|---|---|---|---|
| **A** (symmetric, default) | 0.3583 | 0.6917 | **78.5** | 8/120 = 6.7 % |
| **B** (instruction) | **0.7000** | 0.7750 | **16.4** | **71/120 = 59.2 %** |
| **A′** (function words kept) | 0.2833 | 0.6917 | 78.5 | 8/120 = 6.7 % |

**The diagnostics discriminate, and they point at the query form rather than at the fusion.** The embedder can
already pick the right preference out of the household's other preferences 69 % of the time (mean rank 1.525) — that
is not the failure. What fails is crowding: the planted preference sits a mean of **78 rows deep in the whole vector
lane**, behind spans and facts. The instruction arm moves exactly that number — 78.5 → 16.4, and rows within the top
five 6.7 % → 59.2 % — and transfer follows it from 0.3583 to 0.7000. `loud music` is the topic the embedder itself
struggles with (rank-1 among preferences only 36.7 %, vec mean 132.9) and it recalls 0/30 in A and only 5/30 in B.

### Run B — the instruction arm — REPORTED, never adopted

`--embedder qwen --query-instruction` (`ms1a2_instruction.json`): transfer **0.7000** (the band MET), growth drop
**3.75** (met), `pref_rank1` 0.7750, `vec_rank_mean` 16.367 — **but `update_acc` falls to 0.9375, below its 0.95
band**. So the asymmetric query form buys the transfer band and spends the update band. It is recorded, not adopted;
adopting it is a separate decision with its own pre-registration.

### The residual, and whether the diagnostics point at it

`T23j3` pins the residual the design states: evidence found in BOTH lanes (0.6/61 + 0.6/62 = **0.019513**) still
outranks a belief found in ONE (1/61 = **0.016393**). **The diagnostics do not point at it as the binding
constraint.** If the residual were binding, A and B would differ mainly in how often a two-lane span beat the
preference; instead they differ 4.8× in how deep the preference sits in the vector lane before any fusion happens,
and A′ — which changes the full-text lane and leaves the vector lane identical — moves transfer by only 0.075. The
residual is real and stated; the crowding is what is costing the band.

### Honest scope

Synthetic seeded corpora, ORACLE candidates, in-memory stores, one embedder on one GPU. Nothing here is measured on
real speech or on the owner. `relation_precision` is 1.0 **by construction** — the oracle plants only true relations.
Two bands are MISSED and are written as MISSED. MS1b, the extractor bake-off, is what replaces the oracle.

### Tests

`test_memory_logic.py` **212 checks** in the file, **211 on the CI runner** (T22g skips without numpy) — CI green.
New at MS1a.2: T23j0–j5 (the row weight, the §3.8 shape both ways, the residual, the 1.0 default), T23k1–k3 (the
collapse and a span no belief stands on), T25g retargeted to pin the collapse, T29a–f (the list disjoint and
tokeniser-stable, the filter on and off), T28a–e (the two rank diagnostics, None with no embedder). Three mutants
bite, control green first: `W_CLAIM["span"] = 1.0` kills T23j0/j1/j3/j5 and T23k3; the collapse disabled kills
T23k1/k2 and T25g; `query_terms` never dropping kills T21a/b and T29c/d/d2/e.


## MS1a.3 + MS1a.4 — 2026-09-09 — the preference lane, the growth trace, one vector vote, the subject gate; the bands re-measured

**All six bands are MET in one run for the first time.** Update 100 %, coexist 100 %, transfer recall@5 **91.67 %**,
growth drop **3.75 pts**, audit 0, p99 0.2399 ms — `ms1a4_run.json`, exit 0, no `BANDS MISSED:` line. The road here
was two milestones and one rule that was ruled out by its own pre-registered trace.

### MS1a.3 — the preference lane, and the trace that killed the union rule

MS1a.2 left transfer at 35.83 % and located the miss precisely: the embedder already ranked the planted preference
first among the household's preferences in 69 % of scenarios, but it sat a mean **78 rows deep** in the mixed vector
lane. Crowding, not fusion. MS1a.3 gave the preference model **its own vector lane (`vec_pref`) under the
instruction-prefixed query**, leaving the mixed lane symmetric so two query forms never share one cosine scale
(`e5094fe` design, `fbd48b8` lane).

**The growth trace, run on the unedited MS1a.2 code before any MS1a.3 edit**, against a pre-registered rule: the
union rule would apply only if, in at least half of the growth misses, the answer sat outside the mixed lane's 50-row
cut while the winner was a row the vector lane ALONE found, tying at 1/61 and winning on recency. Measured:

```
n_miss                                     = 6
n_outside_cut_and_vector_only_winner_tie   = 0
VERDICT: UNION RULE DOES NOT APPLY
```

Not one of the six had that shape. **Every winner carried BOTH lanes and nothing tied** — the tie-break path was
"relevance (no tie)" in all six, so recency never ran. The real mechanism was different and uniform: a **filler fact
about a DIFFERENT person**, sitting at `fts_fact` 4–11 *and* `vec` 4–37, collected two reciprocal-rank terms
(0.026–0.030) and out-summed the correct answer standing at **`fts_fact` rank 1** — top of the lane that actually
identifies the subject — which took 1/61 = 0.0164. Reciprocal rank rewards agreement over authority. The within-lane
score knew which row was right (0.8–1.0 against the winners' 0.09–0.25) and is never consulted unless relevance ties.

| seed | question | answer | winner | winner text |
|---|---|---|---|---|
| 2 | what does jo do for work | `fts_fact` 1 + `vec` 34 = 0.02703 | `fts_fact` 4 + `vec` 20 = 0.02813 | `opal1 person works as a cooper` |
| 3 | what does kit do for work | `fts_fact` 1 = 0.01639 | `fts_fact` 4 + `vec` 31 = 0.02661 | `opal1 person works as a cooper` |
| 8 | what does lena do for work | `fts_fact` 1 = 0.01639 | `fts_fact` 4 + `vec` 37 = 0.02593 | `opal1 person works as a cooper` |
| 9 | which city does ava live in | `fts_fact` 1 + `vec` 39 = 0.02649 | `fts_fact` 11 + `vec` 14 = 0.02760 | `verity8 person lives in Reykjavik` |
| 9 | what does ava do for work | `fts_fact` 1 = 0.01639 | `fts_fact` 11 + `vec` 5 = 0.02947 | `verity8 person works as a glassblower` |
| 9 | what job does ava work as | `fts_fact` 1 = 0.01639 | `fts_fact` 11 + `vec` 4 = 0.02971 | `verity8 person works as a glassblower` |

The union rule would not have closed even the three where the answer *was* outside the cut: seed 8's best case gains
1/121 = 0.0083, reaching 0.0247 against a 0.0259 winner — still a loss.

**A temp run then measured the preference lane at transfer 92.5 %** (`loud music` 0 → 21 of 30, a topic that had
never recalled once) **and update 93.75 %, breaking that band.** The cause was arithmetic: a preference found by BOTH
vector lanes took `vec` 2 + `vec_pref` 1 = 1/62 + 1/61 = **0.032522** against the answer fact's `fts_fact` 1 + `vec` 3
= **0.032266**. Five of five update misses were won by a preference; in two households four of the five results were
preferences. MS1a.1's withdrawn defect in a narrower place — a second lane over a subset of rows handing those rows a
rank-1 term they had not earned against the full field. MS1a.3 was STOPPED there for a ruling, and wrote no log
section of its own; this one carries its findings.

### MS1a.4 — the two rules

1. **ONE VECTOR VOTE PER ROW.** `vec` and `vec_pref` are two views of one mechanism — the same embedder over the same
   rows — so a row takes its BEST rank among them, never their sum. Every other lane still sums. The preference lane
   keeps its job: a preference at `vec_pref` 1 earns 1/61 whether or not the mixed lane found it; it simply cannot be
   counted twice for being the same row seen twice.
2. **THE SUBJECT GATE.** When a question names a known person — every token of a person's normalised `display_name`
   present among the question's tokens, taken BEFORE stopword removal since a name is never a function word — belief
   candidates about a DIFFERENT known person leave every lane. Spans and household/topic rows carry no person and are
   never gated: evidence is not a claim about anybody, and gating it would hide the utterance a belief rests on.
   **Exact tokens only — aliases and nicknames are MS2's people layer, and that limit is stated in the code.**

### Run C — no embedder — equals MS0.1

`--households 10 --days 14 --seed 1 --latency-facts 100000 --embedder none --assert-bands`, exit 0: update 1.0,
coexist 1.0, growth_update 1.0, drop 0.0, transfer 0.0, relation 1.0, spouse 8.0 and 10/10, audit 0; p99 0.2701 ms.
All five bands PASS. **Both rules leave the no-embedder path exactly where MS0.1 left it** — the gate removes only
wrong-person rows, which are never the answer, and with one vector lane nothing can double-count.

### Run A — every band MET

| band | expected | measured | verdict |
|---|---|---|---|
| `update_acc` | ≥ 0.95 | **1.0000** | **MET** (0.9375 in MS1a.3's temp run) |
| `transfer_recall5` | ≥ 0.60 | **0.9167** | **MET** (0.3583 at MS1a.2) |
| `growth_drop_points` | ≤ 5 | **3.75** | **MET** (7.5 at MS1a.2) |
| `coexist_recall` | ≥ 0.95 | **1.0000** | MET |
| audit violations | 0 | **0** | MET |
| p99 write latency | ≤ 50 ms | **0.2399 ms** | MET |
| `pref_in_top5_rate` (the lane's price) | reported | **0.05** | reported |
| `transfer_gold_pref_lane_rank_mean` | reported | **1.3417** | reported |
| `transfer_gold_pref_rank1` / `_vec_rank_mean` | reported | 0.6917 / 78.4833 | reported |

Per household, read from the JSON, every mean recomputed and checked against the stored aggregate (all seven matched
to 5e-5):

| seed | update | coexist | growth_upd | drop | transfer | price | pref-lane rank |
|---|---|---|---|---|---|---|---|
| 1 | 1.0000 | 1.0000 | 1.0000 | 0.00 | 0.9167 | 0.00 | 1.333 |
| 2 | 1.0000 | 1.0000 | 1.0000 | 0.00 | 0.9167 | 0.00 | 1.500 |
| 3 | 1.0000 | 1.0000 | 0.8750 | 12.50 | 0.9167 | 0.50 | 1.750 |
| 4 | 1.0000 | 1.0000 | 1.0000 | 0.00 | 0.9167 | 0.00 | 1.167 |
| 5 | 1.0000 | 1.0000 | 1.0000 | 0.00 | 0.9167 | 0.00 | 1.500 |
| 6 | 1.0000 | 1.0000 | 1.0000 | 0.00 | 0.9167 | 0.00 | 1.250 |
| 7 | 1.0000 | 1.0000 | 1.0000 | 0.00 | 0.9167 | 0.00 | 1.417 |
| 8 | 1.0000 | 1.0000 | 1.0000 | 0.00 | 0.9167 | 0.00 | 1.167 |
| 9 | 1.0000 | 1.0000 | 0.7500 | 25.00 | 0.9167 | 0.00 | 1.333 |
| 10 | 1.0000 | 1.0000 | 1.0000 | 0.00 | 0.9167 | 0.00 | 1.000 |
| **mean** | **1.0000** | **1.0000** | **0.9625** | **3.75** | **0.9167** | **0.05** | 1.3417 |

`transfer/topic`: early mornings **30/30**, long drives **30/30**, spicy food **30/30**, `loud music` **20/30**.

**The price is small and measured, not assumed: 0.05.** A preference occupies one of a fact question's five results
in 5 % of them, and it is concentrated — seed 3 accounts for 0.50 and every other household for 0.00. The MS1a.3
design claimed the price was "one preference among every question's five results"; measured under one vote it is
rarer than that, and it never takes the top slot (update is 1.0000).

### Every remaining miss traced

**Update: none.** 1.0000 in all ten households.

**Growth: three, in seeds 3 and 9 — and their character has CHANGED.** The subject gate removed MS1a.3's
wrong-person fact winners entirely; not one remaining winner is a belief about another person. What wins now is a
**span**:

| seed | question | answer | winner | winner text |
|---|---|---|---|---|
| 3 | what does kit do for work | `fts_fact` 1 = 0.016393, wscore 1.0 | span `fts_span` 10 + `vec` 7 = 0.017527, wscore 0.0786 | `quilla3 works as a thatcher` |
| 9 | what does ava do for work | `fts_fact` 1 = 0.016393, wscore 0.8 | span `fts_span` 1 + `vec` 2 = 0.019513, wscore 0.5513 | `i work as a teacher` |
| 9 | what job does ava work as | `fts_fact` 1 = 0.016393, wscore 0.8 | span `fts_span` 1 + `vec` 1 = 0.019672, wscore 0.5513 | `i work as a teacher` |

The gate named the right person in all three (`kit`, `ava`), and the winners carry no person at all — they are
evidence, which is never gated by design. **This is exactly the residual the design states and `T23j3` pins:**
evidence found in BOTH lanes (0.6 × (1/61 + 1/62) = 0.019513) still outranks a belief found in ONE (1/61 = 0.016393).
The claim weight of 0.6 is applied and is not enough at this rank spread. The band is MET at 3.75 with the residual
live; closing it would mean revisiting the residual itself, which is a ranker decision and not this milestone's.

**Transfer: ten, one per household, every one in `loud music`.** That topic recalls 20 of 30 while the other three
recall 30 of 30. It is the topic the embedder itself struggles with — measured at MS1a.2 with rank-1-among-preferences
of only 0.367 against 0.60–1.00 elsewhere — so it is a corpus/model limit rather than a ranking one, and no
re-ranking recovers a preference the embedder cannot pick out of three others.

### Run B — the instruction arm — REPORTED, never adopted

`--query-instruction` (every table instructed): transfer **0.9167**, update **1.0000**, coexist 1.0, growth drop
3.75, price 0.00, `pref_rank1` 0.775, `vec_rank_mean` 16.3667. **Its update band, which MS1a.3 measured breaking at
0.9375, is restored by the one-vote rule** — the double count was the cause there too. It now matches run A on every
band and differs only in the diagnostics. Reported for continuity; not adopted, because A reaches the same bands
without instructing the mixed lane.

### Honest scope

Synthetic seeded corpora, ORACLE candidates, in-memory stores, one embedder on one GPU. Nothing here is measured on
real speech or on the owner. `relation_precision` is 1.0 **by construction** — the oracle plants only true relations.
The subject gate matches **exact `display_name` tokens**: a nickname, an alias or a pronoun names nobody and leaves
the gate inactive, which is safe (it never removes a candidate it should keep) but limited, and the people layer that
fixes it is MS2's. MS1b, the extractor bake-off, replaces the oracle and is what these bands must survive next.

### Tests

`test_memory_logic.py` **231 checks** in the file, **230 on the CI runner** (T22g skips without numpy) — CI green.
New at MS1a.4: T32a/a2/b/c (one vote, with the summed form pinned as the defect), T33a–g (the subject gate: the named
set, the wrong-person removal, nobody named, both named, an unknown name, evidence never gated, a person-less row
never gated), T34 (the preference-lane rank diagnostic), and **T30d rebuilt with teeth** — its MS1a.3 fixture put the
preference low in the mixed lane so the summed form never got the chance to win; the new one is the exact corpus
shape (`fts_fact` 1 + `vec` 3 against `vec` 2 + `vec_pref` 1) with the lane ranks asserted, and it fails under the
summed mutant. Two mutants bite, control green first: `fuse` summing the vector lanes kills T32a, T32c, T24c, T30c
and T30d; the subject gate disabled kills T33a.

## MS1b — 2026-09-09 — the extractor bake-off: the contract measured first (L0), then narrowed; Gemma 4 E2B against Llama 3.1 8B

**Verdict, by the pre-registered rule, applied in code (`bench_ms1b.py --verdict`): NONE. Ceiling F1 0.4073
(Llama 3.1 8B). Neither extractor reached the 0.60 floor, and Gemma did not reach the 0.99 validity band
either. The MS1 row therefore reads MEASURED, not DONE — the embedding half of MS1 was met at MS1a.4
(`c8f3d22`) and stands; this half records its ceiling.**

### Venue

llama.cpp on the RTX 2070 (8,192 MiB), one server at a time, started and stopped by the harness. The
strategist's note records the build as `b8721-7-g5e9c63546`; **the server itself reports
`b8728-5e9c63546`** on every run in this entry — same git sha `5e9c63546`, a later build number. The
measured string is the one recorded here.

| model | file | bytes | sha256 |
|---|---|---|---|
| Llama 3.1 8B Instruct Q4_K_M (bartowski) | `models/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf` | 4,920,739,232 | `7b064f5842bf9532c91456deda288a1b672397a54fa729aa665952863033557c` |
| Gemma 4 E2B it Q4_K_M | `models/gemma-4-E2B-it-Q4_K_M.gguf` | 3,106,736,256 | `9378bc471710229ef165709b62e34bfb62231420ddaf6d729e727305b5b8672d` |

Corpus: 10 synthetic households, 14 days, seed 1, 1,670 spans, 370 oracle candidates. Temperature 0,
seed 1, JSON constrained by a schema generated from the registry.

### The 2048-token budget, and why it is the same for both

Ruled before either run and recorded in the design: Gemma 4 E2B spends hundreds of tokens of its thinking
channel before any JSON — the `JARVIS_THINKING` finding again — and at 512 it returned `finish=length`
with empty content on every candidate-bearing span, which would have scored the budget rather than the
extractor. Llama finishes an empty answer in 6 tokens and needs none of the headroom. A per-model budget
would have been tuning one of them. The measurement bears the asymmetry out: for the same 1,670 calls
Gemma emitted **604,827** output tokens against Llama's **21,954**, a factor of 27.

### L0 — the WIDE contract, kept as `ms1b_llama_8b_contract0.json`, REPORTED and never re-run

```
contract wide   schema 732ef50e01fc   max_tokens 2048
validity 0.9371 (1565/1670)   F1 0.1209   P 0.1875  R 0.0892   lenient 0.1429
n_pred 176  n_gold 370  n_match 33   relation recall 0.0000
492.9 s   tokens in 1,064,269  out 22,209
invalid reasons: {'stated': 105}
```

L0 measured the CONTRACT, not the model, in three classes:

1. **All 105 invalid calls were speaker mismatches** — a `stated_*` candidate whose `speaker_cluster`
   disagreed with the span's. The caller knew that value exactly and made the model restate it.
2. **Every `person.works_as` miss was an article**: the model answered `object_norm = "a plumber"` where
   the oracle holds `"plumber"` — because this repository's own prompt taught it to, with a worked
   example that spelled the normal form as `"a nurse"`.
3. **Every relation miss was a shape mismatch**: free text where the oracle keys an edge by its relation id.

None of those is a judgement about an utterance. So the contract was narrowed to what only a reader can
judge, and both models were re-run. L0 was renamed, not regenerated; it is the measurement of the wide
contract and never a band. It was already running at `max_tokens` 2048, so the budget is not a confound
between L0 and L — the contract is the only difference.

### The narrowed contract — who decides what

| field | decided by | how |
|---|---|---|
| `predicate_id` | **the model** | enum generated from the registry |
| `about` | **the model** | `"speaker"`, or a lower-cased name |
| `object` | **the model** | the value AS SAID — the code normalises it |
| `stated` | **the model** | said outright, or implied |
| `relation_id`, `polarity`, `strength`, `ended`, `about_time` | **the model** | enums + null |
| `span_ids` | code | `[span.sid]` |
| `speaker_cluster` | code | `span.cluster` |
| `subject` | code | household predicate → the household; `owner.prefers` → the owner; `about == "speaker"` → the speaker's cluster; else the name |
| `source_kind` | code | stated by the owner's cluster → `stated_owner`; stated by another → `stated_other`; not stated → `inferred` |
| `object_norm` | code | ONE normaliser (`registry.normalise_object`), or the `relation_id` for an edge |

Schema sha256 `6f8eac439f27961d76152b12d17ab02d10acf91879b9bcefbd23cab2c3e01397`; `required` is exactly
`["predicate_id", "about", "object", "stated"]`, and `subject` / `object_norm` / `source_kind` /
`speaker_cluster` / `span_ids` do not appear in the schema at all. `Store.ingest` overwrites
`object_norm` through the same normaliser before any rule runs, so no caller's value is trusted. Checked
first, because the gate says stop if it is not true: every one of the corpus's **370** oracle candidates
already carries exactly that normal form (T36c), so the overwrite moves no gold value and no MS0/MS1a
number changes — confirmed independently by the MS0 band step, which still passes.

### The two runs, at the narrow contract

| | Llama 3.1 8B | Gemma 4 E2B |
|---|---|---|
| validity | **0.9946** (1661/1670) | 0.9820 (1640/1670) |
| F1 (strict) | **0.4073** | 0.3419 |
| precision / recall | 0.4240 / 0.3919 | 0.3614 / 0.3243 |
| lenient F1 | 0.4129 | 0.3419 |
| relation recall, STATED | **0.9667** (29 of 30) | 0.0000 (0 of 30) |
| relation recall, INFERRED | 0.0000 (0 of 80) | 0.0000 (0 of 80) |
| preference precision / recall (household mean) | 1.0000 / 0.2000 | 0.9857 / **0.7333** |
| preference polarity agreement | 0.1000 | 0.3783 |
| invalid reasons | `relation_id` 9 | `polarity` 12, `relation_id` 18 |
| throughput | 1,670 calls in **489.9 s** | 1,670 calls in **7,951.7 s** |
| tokens in / out | 1,117,709 / 21,954 | 1,141,391 / 604,827 |

Narrowing the contract moved Llama from validity 0.9371 to 0.9946, F1 0.1209 to 0.4073, and stated-relation
recall from 0 to 29 of 30. The inferred half of the relation recall is 0 for both and is expected to be:
those gold edges are pronoun hints the people layer accrues over days, and MS1b has no people layer. The
two halves are reported separately for that reason and are never averaged into one number.

### Per predicate, all ten households

| predicate | gold | L pred | L match | L F1 | G pred | G match | G F1 |
|---|---|---|---|---|---|---|---|
| `household.routine` | 20 | 56 | 0 | 0.000 | 20 | 0 | 0.000 |
| `household.topic` | 60 | 40 | 8 | 0.160 | 62 | 60 | **0.984** |
| `owner.prefers` | 60 | 12 | 12 | 0.333 | 45 | 44 | **0.838** |
| `person.habit` | 60 | 57 | 47 | **0.803** | 97 | 8 | 0.102 |
| `person.lives_in` | 30 | 34 | 30 | **0.938** | 24 | 7 | 0.259 |
| `person.name` | 0 | 16 | 0 | 0.000 | 13 | 0 | 0.000 |
| `person.relation_to` | 110 | 89 | 29 | 0.291 | 31 | 0 | 0.000 |
| `person.trait` | 0 | 8 | 0 | 0.000 | 9 | 0 | 0.000 |
| `person.works_as` | 30 | 30 | 19 | **0.633** | 31 | 1 | 0.033 |

The two models fail in mirror images, and the split is not random. **Gemma scores on exactly the two
predicates whose subject the code derives without consulting `about`** — `household.topic` (0.984) and
`owner.prefers` (0.838) — and collapses on every predicate whose subject comes from `about`. Llama is the
opposite: strong on the person predicates, weak on the household ones.

### The finding: `about` is answered with a pronoun

The dominant residual failure in both runs is that the model writes the pronoun it heard where the prompt
asked for the literal token `"speaker"`. `derive` then treats `"i"` as a NAME, which resolves to nobody,
and the candidate cannot match however right the rest of it is. Measured over the person-subject
predictions:

| | pronoun as `about` | a cluster | a name |
|---|---|---|---|
| Llama | 63 | 141 | 42 |
| Gemma | **172** | 48 | 30 |

Every one of Gemma's first ten false positives is this and nothing else — the predicate and the object are
correct in all ten:

```
we live in perth at the moment       -> person.lives_in  person:we = perth
i work as a carpenter                -> person.works_as  person:i  = carpenter
i started as a pharmacist this month -> person.works_as  person:i  = pharmacist
i still reads before bed             -> person.habit     person:i  = reads before bed
```

**A counterfactual, computed post-hoc on the stored predictions and REPORTED — it is not the band and it
did not change the verdict:** remapping a first-person pronoun (`i` / `me` / `myself`) to the speaker's own
cluster, which is a derivation and not a judgement, moves Llama from F1 0.4073 to 0.4382 and **Gemma from
0.3419 to 0.6068**. Gemma would still not be chosen — its validity is 0.9820, below the 0.99 band, and the
remap does not touch validity — but the number says plainly where this milestone's ceiling actually sits:
in a convention the prompt states and the model does not follow, not in the models' ability to read the
utterance. Changing `derive` and re-running after seeing these numbers would be tuning on the test set, so
it was not done; the next contract revision is the strategist's call.

Two smaller classes, both real: `household.routine` is used as a catch-all for a personal habit (Llama
emitted 56 routines and matched none, despite an explicit registry line saying a routine is something the
household does together), and both models invent `person.name` and `person.trait` candidates for which the
corpus has no gold at all (Llama 24 such predictions, Gemma 22).

### Failure examples, verbatim (seed 1)

```
LLAMA 3.1 8B - false positives
  i work as a plumber              -> person.name       person:i = i
  i work as a plumber              -> person.works_as   person:i = plumber
  i work as a carpenter            -> person.name       person:i = i
  i work as a carpenter            -> person.works_as   person:i = carpenter
  i still bakes on fridays         -> household.routine household:household = bakes on fridays
  i walks the dog at seven         -> household.routine household:household = walks the dog at seven
  she was here all evening again   -> person.habit      person:she = here all evening again
  she was here all evening again   -> person.relation_to person:she = None
  we sorted the bills together     -> household.routine household:household = sorted the bills together
  we sorted the bills together     -> household.topic   household:household = bills
LLAMA 3.1 8B - false negatives
  i work as a plumber              -> person.works_as   person:owner   = plumber
  i work as a carpenter            -> person.works_as   person:partner = carpenter
  i runs at dawn                   -> person.habit      person:partner = runs at dawn
  remember the bins go out on tuesday     -> household.routine household:None = bins go out on tuesday
  remember the groceries arrive thursday  -> household.routine household:None = groceries arrive thursday
  we were talking about cycling again     -> household.topic household:None = cycling
  we were talking about pottery again     -> household.topic household:None = pottery
  we were talking about gardening again   -> household.topic household:None = gardening
  we were talking about camping again     -> household.topic household:None = camping
  we were talking about renovations again -> household.topic household:None = renovations

GEMMA 4 E2B - false positives
  we live in perth at the moment          -> person.lives_in person:we = perth
  we moved to bendigo last week           -> person.lives_in person:we = bendigo
  i work as a carpenter                   -> person.works_as person:i = carpenter
  i started as a pharmacist this month    -> person.works_as person:i = pharmacist
  i still reads before bed                -> person.habit    person:i = reads before bed
  i still bakes on fridays                -> person.habit    person:i = bakes on fridays
  i still swims on sundays                -> person.habit    person:i = swims on sundays
  i stopped, i no longer reads before bed -> person.habit    person:i = reads before bed
  i runs at dawn                          -> person.habit    person:i = runs at dawn
  i walks the dog at seven                -> person.habit    person:i = walks the dog at seven
```

### The store on extracted candidates

NOT RUN, and deliberately: base §4.3 conditions it on a model being chosen, and none was.
`ms1b_store_on_extracted.json` does not exist. The oracle-driven run `ms1a4_run.json` remains the store's
measurement.

### Honest scope

Synthetic seeded utterances that always name people, scored against ORACLE candidates; nothing here was
measured on real speech, on household audio, or on the owner. The exact-name gate of MS1a.4 still applies —
aliases are MS2's people layer. The encoder-style third candidate stays deferred with its reason. The
inferred relation half is a people-layer task and is reported at 0 for both models rather than excused.
The counterfactual above is arithmetic on stored predictions, not a run.

### Tests

`test_memory_logic.py` 245 → **254 checks** (T36, T36a–g and the retargeted T35a2/a3/a4/d2). Three mutants,
control green first, each failing by name and restored byte-identical: the normaliser not stripping the
article kills T36/T36a/T36c/T36d/T35d2 **and** the MS1a.2 store tests T21a/b/c — which is the evidence that
one normaliser is now load-bearing on the write path, not only in the scorer; `derive` always returning
`stated_owner` kills T36a/T36b; a hand-typed `required` list carrying a derived field kills T35a4. No new
CI step: the MS1b scaffolding step landed with `c38a8a8` and covers the narrowed CLI unchanged.

### The field — contract 2 — 2026-09-10

**Verdict, by the pre-registered rule applied in code (`bench_ms1b.py --verdict`) over the WHOLE
field: CHOSEN — `gemma-e4b` (Gemma 4 E4B it Q4_K_M), validity 1.0000, strict F1 0.7426.** Three of
the eleven models measured cleared the 0.60 floor at ≥ 99 % validity: gemma-e4b 0.7426, gemma-e2b
0.7201, qwen3-8b 0.6196. MS1's other half was met at MS1a.4 (`c8f3d22`), so the MS1 row flips DONE.

The operator's instruction that set the field, verbatim (2026-09-09): *"idc about my gpu hours thats
fine, i just want the best state of the art remember, this is a jarvis project."*

### Why contract 2, and what it was expected to do

Contract 1 (`6946874`) chose nothing — Llama 3.1 8B F1 0.4073, Gemma 4 E2B 0.3419 — and its report
found the residual was the CONTRACT again, in two classes the strategist then verified from the
stored predictions with the real scorer:

1. **`about` came back as the pronoun heard.** Gemma put 167 of 250 person-subject predictions on a
   first-person pronoun (`i` 152, `we` 14, `me` 1), Llama 36 of 246 (`i` 22, `us` 14), and `derive`
   read each as a name that resolves to nobody. A first-person pronoun spoken by cluster N IS
   cluster N — a derivation from the span, not a judgement about it, and the corpus's own convention.
2. **Every one of the 39 invalid calls was a null `relation_id` on an edge or a null `polarity` on a
   preference** (Llama 9, Gemma 12 + 18) — fields that are optional only because one flat candidate
   object has to make them optional for the predicates that do not use them.

Contract 2 was pre-registered in the design (`32c5a0e`) BEFORE any contract-2 number existed:
`FIRST_PERSON` (ten words) derives to the speaker's cluster in `extract/derive.py`, and
`candidate_schema()` becomes a `oneOf` of four predicate-family branches (`oneOf` because llama.cpp's
grammar converter supports it and does NOT support `if`/`then`), each making its own family's fields
REQUIRED and NON-NULLABLE:

| branch | predicate_id enum | extra properties | required |
|---|---|---|---|
| EDGE | `person.relation_to` | `about`, `relation_id` (enum, non-nullable) | predicate_id, about, relation_id, object, stated |
| PREFERENCE | `owner.prefers` | `polarity` (enum, non-nullable), `strength` | predicate_id, polarity, object, stated |
| HOUSEHOLD | `household.routine`, `household.topic` | — | predicate_id, object, stated |
| PERSON | the other five | `about` | predicate_id, about, object, stated |

`predicate_id` is FIRST in every branch's property order, so the model commits to a family on its
first key and constrained decoding holds it there. Schema sha256
`846ad08857eb37f8175f0aa24fbbfdfe2c7edf89c5565b2521209a91792dbc49` (contract 1's was `6f8eac439f27…`,
L0's `732ef50e01fc…`).

**The remap EXPECTATION, reproduced in the suite (T38b) and never a result:** rewriting only the
first-person subjects of the STORED contract-1 predictions gives Llama 156 matches (from 145) and
Gemma 227 (from 120). The re-runs were not obliged to land there, because contract 2 also changed
the schema — and they did not (below).

**What the branches actually did, and it is the cleanest result of this milestone: contract 1's
entire invalid class is GONE.** Across all eleven contract-2 runs there is not one null-`relation_id`
or null-`polarity` invalid call. The only invalid calls anywhere are 33 unparsed on `llama-1b` and 7
on `llama-8b`, both a different failure (below). Nine of the eleven models scored validity 1.0000.

### The venue

llama.cpp **`version: 8728 (5e9c63546)`** — the measured string from `llama-server --version` on this
PC, not the note's `b8721-7-g5e9c63546`. CUDA on the RTX 2070 (8,192 MiB), one server at a time,
started and stopped by the harness. Corpus: 10 synthetic households, 14 days, seeds 1–10, 1,670
spans, 370 oracle candidates. `temperature 0`, `seed 1`, `max_tokens` 2048 for every model,
`--jinja` for every server. Thinking OFF via `chat_template_kwargs {"enable_thinking": false}`
wherever the template offers the switch (the four Qwen keys); Gemma 4's channel has no switch and
keeps the headroom. **[CORRECTED 2026-09-12, field addendum 2: FALSE — Gemma 4's template reads `enable_thinking` and both builds' `--reasoning auto` default turned thinking ON for every Gemma run in this field; see `### Thinking, measured at the renderer` and `### The field addendum 2`.]**

| key | model | bytes | sha256 | think switch |
|---|---|---|---|---|
| `gemma-e4b` | Gemma 4 E4B it Q4_K_M | 5,405,163,520 | `6dfbdb0fff82025ef88a6ff912f91d141f722b5d95f14d61b10f0e08839185c8` | n/a |
| `gemma-e2b` | Gemma 4 E2B it Q4_K_M | 3,106,736,256 | `9378bc471710229ef165709b62e34bfb62231420ddaf6d729e727305b5b8672d` | n/a |
| `qwen3-8b` | Qwen3 8B Q4_K_M (fetched) | 5,027,783,488 | `d98cdcbd03e17ce47681435b5150e34c1417f50b5c0019dd560e4882c5745785` | applied |
| `qwen35-9b` | Qwen3.5 9B Q4_K_M | 5,680,522,464 | `03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8` | applied |
| `qwen35-4b` | Qwen3.5 4B Q4_K_M | 2,740,937,888 | `00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4` | applied |
| `qwen3-4b` | Qwen3 4B Q4_K_M | 2,497,280,256 | `7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5` | applied |
| `llama-8b` | Llama 3.1 8B Instruct Q4_K_M | 4,920,739,232 | `7b064f5842bf9532c91456deda288a1b672397a54fa729aa665952863033557c` | n/a |
| `phi3-mini` | Phi-3 mini 4k instruct Q4 | 2,393,231,072 | `8a83c7fb9049a9b2e92266fa7ad04933bb53aa1e85136b7b30f1b8000ff2edef` | n/a |
| `phi4-mini` | Phi-4-mini Instruct Q4_K_M (fetched) | 2,491,874,272 | `88c00229914083cd112853aab84ed51b87bdf6b9ce42f532d8c85c7c63b1730a` | n/a |
| `llama-3b` | Llama 3.2 3B Instruct Q4_K_M | 2,019,377,696 | `6c1a2b41161032677be168d354123594c0e6e67d2b9227c84f296ad037c728ff` | n/a |
| `llama-1b` | Llama 3.2 1B Instruct Q4_K_M | 807,694,464 | `6f85a640a97cf2bf5b8e764087b1e83da0fdb51d7c9fab7d0fece9385611df83` | n/a — the FLOOR reference |

**Fetched** (`huggingface_hub`, outside git, never staged): `qwen3-8b` from `Qwen/Qwen3-8B-GGUF`;
`phi4-mini` from **`unsloth/Phi-4-mini-instruct-GGUF`** — the prompt's two named sources
(`microsoft/*-gguf`, `bartowski/*Phi-4-mini*`) do not exist, verified by a Hub search whose
`microsoft/*` hits are all Phi-**3**-mini, so the remaining reputable community GGUF was taken and
recorded rather than dropping a model on a naming technicality; `nuextract` from
`numind/NuExtract3-GGUF` (the newest `numind/*` repo carrying a GGUF, Q4_K_M 2.78 GB).

**SKIPPED, with the measured reason: `nuextract`.** `llama-server` never became healthy within
300 s, and running it by hand gives the cause — `llama_model_load: error loading model: missing
tensor 'blk.32.ssm_conv1d.weight'`, i.e. NuExtract3 is a hybrid SSM architecture this llama.cpp
build cannot load from that GGUF. Not a contract failure and not a model result: the purpose-built
extractor could not be run at all here, and no JSON was written for it. **Nothing else was skipped.**

### The field, all eleven runs

| key | validity | F1 | lenient | scorable | rel. STATED | rel. INFERRED | zero-gold preds | invalid | s | tok out |
|---|---|---|---|---|---|---|---|---|---|---|
| **gemma-e4b** | **1.0000** | **0.7426** | 0.7595 | 0.8368 | 0.9333 | 0.0000 | 10 | — | 8,575 | 459,258 |
| gemma-e2b | 1.0000 | 0.7201 | 0.7201 | 0.8114 | 0.8667 | 0.0000 | 15 | — | 6,979 | 591,585 |
| qwen3-8b | 1.0000 | 0.6196 | 0.7059 | 0.6920 | **1.0000** | 0.0750 | 10 | — | **541** | 22,989 |
| qwen35-9b | 1.0000 | 0.5801 | 0.5987 | 0.6489 | 0.9333 | 0.0375 | 43 | — | 1,298 | 22,252 |
| qwen35-4b | 1.0000 | 0.5729 | 0.5796 | 0.6615 | 0.9333 | 0.0000 | 0 | — | 878 | 20,848 |
| qwen3-4b | 1.0000 | 0.3098 | 0.3442 | 0.3327 | 0.0667 | 0.0000 | 124 | — | 506 | 33,461 |
| llama-8b | 0.9958 | 0.2944 | 0.3115 | 0.3186 | 0.7333 | 0.0000 | 77 | unparsed 7 | 875 | 46,873 |
| phi3-mini | 1.0000 | 0.0798 | 0.1077 | 0.0830 | 0.0000 | 0.0250 | 44 | — | 1,331 | 112,070 |
| phi4-mini | 1.0000 | 0.0233 | 0.0233 | 0.0287 | 0.0000 | 0.0000 | 0 | — | 226 | 14,900 |
| llama-3b | 1.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0 | — | 151 | 10,020 |
| llama-1b | 0.9802 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0 | unparsed 33 | 832 | 100,083 |

`scorable` is F1 over the 290 gold a per-span extractor can reach (the 80 inferred edges removed
from the RECALL denominator only; precision unchanged) — REPORTED beside the band, never in it, as
is the zero-gold count (predictions on `person.name` / `person.trait`, which this corpus has no gold
for at all). The two relation recalls are reported split and never averaged: the STATED half is an
extraction task, the INFERRED half is the people layer's accrual over days and MS1b has no people
layer.

**Contract 1 → contract 2, the two models that ran under both:**

| | contract 1 | contract 2 |
|---|---|---|
| Gemma 4 E2B | validity 0.9820, F1 0.3419 | validity **1.0000**, F1 **0.7201** |
| Llama 3.1 8B | validity 0.9946, F1 0.4073 | validity 0.9958, F1 **0.2944** |

Gemma more than doubled and crossed the floor; **Llama went DOWN**, against a remap expectation of
0.4382. The pre-registration anticipated exactly this — the schema changed too, so the expectation
was never a prediction — and the cause is visible in its runs: 7 unparsed calls where the model
looped near-identical relation candidates inside the unbounded array until the 2048-token cap, plus
77 zero-gold predictions. This is recorded as a finding, not repaired: contract 2 was pre-registered
and every model ran under it.

### Per predicate, the top three, all ten households

| predicate | gold | gemma-e4b F1 | gemma-e2b F1 | qwen3-8b F1 |
|---|---|---|---|---|
| `person.works_as` | 30 | **1.000** | **1.000** | **1.000** |
| `owner.prefers` | 60 | **1.000** | 0.938 | 0.833 |
| `person.habit` | 60 | 0.992 | 0.727 | 0.724 |
| `household.topic` | 60 | 0.945 | 0.976 | **1.000** |
| `person.lives_in` | 30 | 0.800 | 0.909 | 0.667 |
| `person.relation_to` | 110 | 0.371 | 0.364 | 0.364 |
| `household.routine` | 20 | 0.167 | 0.108 | 0.000 |
| `person.name` / `person.trait` | 0 | 0.000 | 0.000 | 0.000 |

The winner is at or near ceiling on five of eight. Its two weaknesses are structural rather than
careless: `person.relation_to` carries 80 inferred edges no per-span call can produce (it gets 28 of
the 30 STATED ones), and `household.routine` is confused with `household.topic` in both directions.

### Failure examples, verbatim (seed 1)

```
gemma-e4b - false positives
  remember the groceries arrive thursday -> household.topic   household = groceries arrive thursday
  we were talking about astronomy again  -> household.routine household = talking about astronomy
  we sorted the bills together           -> household.routine household = sorted the bills together
  she called me love                     -> person.relation_to person:1 = partner
  we shared the school run                -> household.routine household = school run
gemma-e4b - false negatives
  we moved to bendigo last week          -> person.lives_in    person:owner = bendigo
  remember the groceries arrive thursday -> household.routine  household = groceries arrive thursday
  she was here all evening again         -> person.relation_to person:owner = spouse [inferred]
  we sorted the bills together           -> person.relation_to person:owner = spouse [inferred]
  she picked the kids up                 -> person.relation_to person:owner = spouse [inferred]

gemma-e2b - false positives
  remember the bins go out on tuesday    -> household.topic   household = bins go out on tuesday
  we shared the school run                -> household.routine household = school run
  we planned the week together           -> household.topic   household = week
  my husband alex and i decided          -> person.name       person:alex = alex
  my husband alex and i decided          -> person.name       person:alex = alex
gemma-e2b - false negatives
  we moved to bendigo last week          -> person.lives_in   person:owner = bendigo
  remember the bins go out on tuesday    -> household.routine household = bins go out on tuesday
  remember the groceries arrive thursday -> household.routine household = groceries arrive thursday
  she was here all evening again         -> person.relation_to person:owner = spouse [inferred]
```

Note the shape of the commonest miss: the same span, the same subject, the right value — filed under
`household.topic` where the oracle says `household.routine`, or the reverse. It is a boundary between
two adjacent household predicates, not a failure to read the sentence.

### The winner's candidates through the store (REPORTED, nothing banded here)

`bench_ms0.py --households 10 --days 14 --seed 1 --latency-facts 20000 --embedder qwen
--candidates-from bench/results/ms1b_gemma-e4b.json` → `ms1b_store_on_extracted.json`. The harness
gained `candidates_from`; the ORACLE path is the default and is unchanged (the MS0 band step still
passes 5/5 and the suite is green). One model on the GPU at a time: every llama.cpp server was
stopped before the embedder loaded.

| band / reported | ORACLE (`ms1a4_run.json`) | EXTRACTED (gemma-e4b) |
|---|---|---|
| update accuracy | 1.0000 | **0.7500** |
| coexisting recall | 1.0000 | **0.4167** |
| transfer recall@5 | 0.9167 | 0.9084 |
| growth drop (points) | 3.75 | 5.00 |
| growth update accuracy | 0.9625 | 0.7000 |
| relation precision | 1.0000 | 0.6167 |
| spouse surfaced, day mean | 8.0 | 6.0 |
| audit violations | 0 | **0** |
| p99 write latency | — | 0.3209 ms over 20,000 ingests |

Read honestly: the store's own machinery is unharmed by a noisier input — audit violations stay at
zero, the write path stays fast, and semantic transfer barely moves (0.9167 → 0.9084) because the
embedding lane retrieves whatever was written. What degrades is what the extractor did not get
right in the first place: update accuracy and coexisting recall fall because facts it missed were
never available to supersede or accumulate. MS2's ≥ 85 % is pre-registered THERE, not here.

### Honest scope

Synthetic seeded utterances that always name people, scored against ORACLE candidates, seeds 1–10,
14 days. Nothing here was measured on real speech, on household audio, or on the owner. The
exact-name gate of MS1a.4 still applies — aliases are MS2's people layer. The inferred relation half
is reported at 0.00–0.075 for every model rather than excused: it is a people-layer task. The
purpose-built extractor could not be loaded and is a gap in the field, not a result. `nuextract` and
the two contract-1 runs excepted, every model in the pre-registered field ran.

### Renames and tests

The two contract-1 runs are KEPT and were renamed rather than overwritten:
`ms1b_llama_8b.json` → `ms1b_llama_8b_contract1.json`, `ms1b_gemma_e2b.json` →
`ms1b_gemma_e2b_contract1.json`. Their contents are untouched and still carry `"contract": "narrow"`
— that label means contract 1. `--verdict` reads only `contract2` runs, ignores its own output, and
refuses a mislabelled file by name.

`test_memory_logic.py` 254 → **262 checks**. T35a–T35a4 rewritten for the four branches; T37a–T37d
(the field table, the queue's resumability, the thinking switch, the rule); T38a–T38d (the
first-person derivation, the remap expectation, the verdict's file selection, `f1_scorable` and the
zero-gold count). Four mutants, control green first (262/262), each failing BY NAME and restored
from a byte-copy: `FIRST_PERSON` without the plurals kills T38a and T38b; a nullable `relation_id`
kills T35a2; a `--verdict` that stops excluding `_contract1` kills T38c; the validity gate removed
from the rule kills T37d. No new CI step.

### The field addendum — 2026-09-12

**Verdict, by the pre-registered rule applied in code WITHIN llama.cpp build b10809
(`bench_ms1b.py --verdict --build b10809`, written to `ms1b_field_verdict_b10809.json`): CHOSEN —
`gemma-e4b-v040`, validity 1.0000, strict F1 0.7695.** That key IS the incumbent — Gemma 4 E4B it
Q4_K_M, the field's own file, re-run on the new build — so **no addendum arm beat it and the MS1
winner stands.** Of the seven new arms, one cleared the 0.60 floor at ≥ 99 % validity: NuExtract3 at
0.6467. The frozen field's verdict (`--verdict --build b8728`, eleven runs, written to
`ms1b_field_verdict_b8728.json`) is unchanged and was re-derived here: `gemma-e4b` CHOSEN at validity
1.0000 / F1 0.7426.

### Why an addendum

The eleven-model field (`8c4cd2b`) ran on a llama.cpp checkout dated 2026-04-09, reporting build 8728,
while v0.4.0 shipped 2026-09-04 with hybrid-SSM work in between — so NuExtract3's "could not load" might
have been the build's rather than the model's. It also never fetched four dense candidates with
day-one GGUFs that the briefings had dismissed against the BOX's bandwidth budget, which is the wrong
denominator for an extractor hosted on the Main PC's GPU. And it ran Q4_K_M only, so whether 0.7426 is
a quantisation ceiling was untested. The addendum was pre-registered in the design and the plan
(`f914d85`) before any of its numbers.

### The second venue — llama.cpp v0.4.0 = build b10809

The `v0.4.0` release carries one asset, `nightly-tag.txt`, whose seven bytes read `b10809\n`; the
binaries are on the `b10809` pre-release (published 2026-09-04T17:55:12Z). Installed at
`~/llama.cpp-v0.4.0/bin` (55 files) beside the frozen `~/llama.cpp`, which was never touched, and
selected for the queue's process only through `JARVIS_LLAMA_BIN`.

| asset | bytes | sha256 (computed at install = GitHub's published digest) |
|---|---|---|
| `llama-b10809-bin-win-cuda-12.4-x64.zip` | 253,938,543 | `c77bfcd9ed8d91e8721a2d6a290b907fddd4fa5412a47b21c6fa1709116b85f9` |
| `cudart-llama-bin-win-cuda-12.4-x64.zip` | 391,443,627 | `8c79a9b226de4b3cacfd1f83d24f962d0773be79f1e7b75c6af4ded7e32ae1d6` |

| binary | reports | sha256 |
|---|---|---|
| `~/llama.cpp-v0.4.0/bin/llama-server.exe` | `version: 0.4.0-dev (build 10809, commit 5266f24da)`, Clang 20.1.8; over HTTP `b10809-5266f24da` | `cb29f66008d4d73cce17cab2c2569ab318eb244b0b2ca2a15024b44d90dfcd3f` |
| `~/llama.cpp/build/bin/Release/llama-server.exe` (frozen) | `version: 8728 (5e9c63546)`, MSVC 19.44; over HTTP `b8728-5e9c63546` | `380303615265d4fabbf40823ecc70cdf085bfde5bade5724a05dc6c89f059c22` |

**The verdict filter is `b10809`, not `v0.4.0`:** no run records the string `v0.4.0`, so the prompt's
`--build v0.4.0` would have selected nothing and returned NONE. `--n-cpu-ffn` exists on this build but
no arm declares it: `-ngl` placement is automatic on v0.4.0 and even the Q8_0 file loads and generates
without it. Measured during the arms (GPU memory is the card's total in use, including the desktop —
WDDM does not attribute VRAM to a process; the rate is the per-call median from each arm's own server
log):

| arm | GPU memory in use | median generation per call | arm wall time |
|---|---|---|---|
| `gemma-e4b-v040` (Q4_K_M) | 4,223 MiB | 74.4 tok/s | 6,018 s |
| `gemma-e4b-q6k` (Q6_K) | 5,216 MiB | 61.1 tok/s | 7,565 s |
| `gemma-e4b-q8` (Q8_0) | 6,209 MiB (run 1) / 6,558 MiB (run 2) | 56.2 tok/s | 8,506 s |

**A pre-run probe of the Q8_0 file read 5.28 tok/s and does NOT hold on the workload** — that number
came from a single 40-token request; over the arm's own 1,670 calls the median is 56.2 tok/s and the
longest call took 15.0 s. The Q8_0 arm was expected to take about a day and took 2 h 17 m, and 2 h 22 m on its re-run, which also hashed the 8 GB file twice mid-arm.

### The arms

Every file from the named repo, no substitutions; every recorded sha256 was re-computed on this PC on
2026-09-12 and equals the hub's published digest. The Q8_0 arm's FIRST run recorded a different value
and the arm was re-run — see "The Q8_0 file's provenance" below.

| key | model | repo | bytes | sha256 | template sha256 (16) | thinking, as rendered |
|---|---|---|---|---|---|---|
| `granite-3b` | Granite 4.2 3B Q4_K_M | `ibm-granite/granite-4.2-3b-GGUF` | 2,244,011,552 | `e0406663965846ae22a403456eb826ccce5f450840491f71952f18a7cb78e7d5` | `9d573773df6d9435` | OFF — `enable_thinking: false` sent; `<think></think>` closed |
| `lfm25-2.6b` | LFM2.5-2.6B Q4_K_M | `LiquidAI/LFM2.5-2.6B-GGUF` | 1,674,455,040 | `02a8b7e17487d326e46d68ce0ba24211e1b80a14c4cd0597fa73c1cd697f52ed` | `0dcc734022e1a9e8` | **ON, no switch** — the template opens `<think>` unconditionally |
| `nuextract3` | NuExtract3 Q4_K_M | `numind/NuExtract3-GGUF` | 2,783,445,984 | `7ee3c0ee9e5699a4391624ae758487f583f73b3242aa4e73dc2bb33e508d703e` | `6c0a83aee85a1bdb` | OFF — `enable_thinking: false` sent; think block closed |
| `granite-8b` | Granite 4.2 8B Q4_K_M | `ibm-granite/granite-4.2-8b-GGUF` | 5,347,917,952 | `16a9369d0805f80b7377d25d87f937a90c05dc04ad79173a52001e42c9aab311` | `9d573773df6d9435` | OFF — as `granite-3b` |
| `ministral-8b` | Ministral 3 8B Instruct Q4_K_M | `mistralai/Ministral-3-8B-Instruct-2512-GGUF` | 5,198,911,904 | `33e7a72cf5e6e2cfc2f2847075acc013d68bba023e35310cef86b5cf8fdca761` | `4ce79eaed7cdf458` | none — no thinking in the template |
| `gemma-e4b-v040` | Gemma 4 E4B it Q4_K_M — the field's own file | (on disk since 2026-09-09) | 5,405,163,520 | `6dfbdb0fff82025ef88a6ff912f91d141f722b5d95f14d61b10f0e08839185c8` | `55572b8d3c834204` | **ON** — `<|think|>` injected |
| `gemma-e4b-q6k` | Gemma 4 E4B it Q6_K | `bartowski/google_gemma-4-E4B-it-GGUF` | 6,329,954,784 | `bea1c94443c91fff61a722d9da5cd3f1cf0196d1e914798ee89d3c00ea365df9` | `603a42db292c2527` | **ON** |
| `gemma-e4b-q8` | Gemma 4 E4B it Q8_0 | `bartowski/google_gemma-4-E4B-it-GGUF` | 8,031,242,720 | `6a6eba0d36a051b5d924211a889c1436717006e7c5d413830c47caa1d46cb598` (run 2's recorded hash = the published digest; run 1's read was wrong — see below) | `603a42db292c2527` | **ON** |

NuExtract3's sha256 equals the hash the field recorded before the local copy was deleted, which is
what makes this a retry: the same bytes the April build refused.

### Thinking, measured at the renderer rather than read from the template

Every request of household 1 (167 spans, built by the harness's own `build_request`) was rendered
through llama.cpp's own chat-template engine — a CPU-only `llama-server` per model
(`CUDA_VISIBLE_DEVICES=-1`, `--device none`, `-ngl 0`, `--no-warmup`, never the queue's port),
`POST /apply-template`, nothing generated.

- **Gemma 4 HAS a thinking switch, and every Gemma run is ON.** The template emits `<|think|>` at the
  start of the system turn when `enable_thinking` is true, and both builds default `--reasoning` to
  `'auto' (detect from template)`, which supplies it whenever a request carries no kwarg — the Gemma
  keys send none. Rendering the same file with `chat_template_kwargs {"enable_thinking": false}`
  removes `<|think|>` from all 167 prompts. So the frozen field's Gemma runs, its winner included,
  also thought, while its Qwen keys did not; the record's "Gemma 4's channel has no switch" (this log's
  `### The venue` above, `bench_ms1b.py:45/78`, `client.py:50`) is measured wrong. It is left as
  written there and flagged here rather than edited, and whether a thinking-OFF Gemma arm should be
  pre-registered is the strategist's, not this run's.
- **LFM2.5 thinks with no switch:** its generation prompt is `<|im_start|>assistant\n<think>`, always.
- **The venue does not move the incumbent's prompt:** b8728 and b10809 render byte-identical prompts
  from the Q4_K_M file, 167 of 167, and the two runs read an identical 1,171,451 prompt tokens.
- **The quantisation arms carry a NEWER template.** bartowski changed the Gemma 4 E4B chat template
  twice after our Q4_K_M file was taken (2026-05-03, 2026-07-26); the repo's current Q4_K_M is
  `d35a3aa7…` / 5,405,170,144 B, not our `6dfbdb0f…`. Rendered, the Q6_K and Q8_0 prompts differ from
  the Q4_K_M prompts by exactly one newline after `<|think|>`, in all 167, and Q8_0's prompts are
  byte-identical to Q6_K's.

### The runs, within build b10809

| key | validity | F1 | lenient | scorable | rel. STATED | rel. INFERRED | zero-gold preds | invalid | s | tok out |
|---|---|---|---|---|---|---|---|---|---|---|
| **gemma-e4b-v040** | **1.0000** | **0.7695** | 0.7781 | 0.8697 | 0.9333 | 0.0000 | 3 | — | 6,018 | 424,937 |
| gemma-e4b-q8 | 1.0000 | 0.7428 | 0.7601 | 0.8399 | 0.6000 | 0.0000 | 1 | — | 8,506 | 449,095 |
| gemma-e4b-q6k | 1.0000 | 0.6943 | 0.7114 | 0.7839 | 0.3333 | 0.0000 | 0 | — | 7,565 | 439,397 |
| nuextract3 | 1.0000 | 0.6467 | 0.6929 | 0.7256 | 0.1000 | 0.0000 | 0 | — | 817 | 22,129 |
| ministral-8b | 1.0000 | 0.5425 | 0.5830 | 0.6082 | 1.0000 | 0.0750 | 30 | — | 729 | 37,405 |
| lfm25-2.6b | 0.9359 | 0.4223 | 0.4291 | 0.4883 | 0.0000 | 0.0000 | 0 | unparsed 107 | 9,979 | 1,451,222 |
| granite-8b | 1.0000 | 0.2976 | 0.3175 | 0.3538 | 0.8000 | 0.0000 | 0 | — | 457 | 20,353 |
| granite-3b | 1.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0.0000 | 0 | — | 217 | 16,700 |

Every run records `llama_version b10809-5266f24da`, `contract2` and schema
`846ad08857eb37f8175f0aa24fbbfdfe2c7edf89c5565b2521209a91792dbc49`; every arm ran 1,670 calls over
seeds 1–10 at `max_tokens` 2048. `QUEUE done=8 skipped=0`.

**Two arms are worth reading before the table is quoted.** `granite-3b` returned the empty candidate
list on all 1,670 calls — valid JSON every time (validity 1.0000), exactly 10 generated tokens per
call, zero predictions. That is the 3B model's own result, not a harness effect: `granite-8b`, with the
same template and the same switch, extracts (134 predictions, 75 matches). `lfm25-2.6b` is the only arm
to miss the validity band, and its 107 invalid calls are exactly its 107 calls that hit the 2,048-token
cap — 104 returned empty content because its forced thinking never closed, 3 returned truncated JSON.
It also wrote 1,451,222 output tokens, 3.2× the next arm.

### Per predicate, all ten households

F1 = 2m / (p + g) over the summed per-household counts. The method was validated first by reproducing
the frozen field's published `gemma-e4b` column exactly (1.000 / 1.000 / 0.992 / 0.945 / 0.800 / 0.371
/ 0.167).

| predicate | gold | v040 | q8 | q6k | nuextract3 | ministral-8b | lfm25-2.6b | granite-8b | granite-3b |
|---|---|---|---|---|---|---|---|---|---|
| `person.works_as` | 30 | 1.000 | 0.933 | 0.933 | 1.000 | 0.500 | 0.571 | 0.378 | 0.000 |
| `owner.prefers` | 60 | 1.000 | 1.000 | 1.000 | 0.915 | 0.500 | 0.421 | 0.261 | 0.000 |
| `person.habit` | 60 | 0.983 | 0.944 | 0.896 | 0.882 | 0.758 | 0.491 | 0.000 | 0.000 |
| `household.topic` | 60 | 0.945 | 0.976 | 0.945 | 1.000 | 1.000 | 0.830 | 0.653 | 0.000 |
| `person.lives_in` | 30 | 0.769 | 0.875 | 0.758 | 1.000 | 0.571 | 0.250 | 0.158 | 0.000 |
| `person.relation_to` | 110 | 0.371 | 0.237 | 0.133 | 0.037 | 0.444 | 0.000 | 0.304 | 0.000 |
| `household.routine` | 20 | 0.328 | 0.170 | 0.154 | 0.000 | 0.000 | 0.140 | 0.000 | 0.000 |

### The venue delta

The incumbent's own file (sha256 `6dfbdb0f…` in both runs) through the same 1,670 requests on the two
builds. The prompts are the same at two independent levels: byte-identical at the renderer (167 of
167) and identical prompt-token totals in the two runs (1,171,451 each).

| | b8728 (the field) | b10809 |
|---|---|---|
| validity | 1.0000 | 1.0000 |
| **F1** | **0.7426** | **0.7695** |
| lenient / scorable | 0.7595 / 0.8368 | 0.7781 / 0.8697 |
| precision / recall | 0.7742 / 0.7135 | 0.8241 / 0.7216 |
| predictions / matches | 341 / 264 | 324 / 267 |
| predictions on no-gold predicates | 10 | 3 |
| relation STATED recall | 0.9333 | 0.9333 |
| tokens out | 459,258 | 424,937 |
| seconds | 8,575 | 6,018 |

| seed | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| b8728 | 0.7222 | 0.7324 | 0.7606 | 0.7324 | 0.7606 | 0.7123 | 0.7397 | 0.7429 | 0.7606 | 0.7647 |
| b10809 | 0.7042 | 0.7647 | 0.8000 | 0.8000 | 0.8000 | 0.7324 | 0.7536 | 0.7941 | 0.7647 | 0.7826 |
| delta | −0.0180 | +0.0323 | +0.0394 | +0.0676 | +0.0394 | +0.0201 | +0.0139 | +0.0513 | +0.0041 | +0.0179 |

**+0.0269 F1, nine households up and one down.** The gain is precision — 17 fewer predictions, 3 more
matches, predictions on no-gold predicates 10 → 3 — with recall +0.0081 and the stated-relation recall
unchanged. Per predicate the move is mostly `household.routine` (0.167 → 0.328), with `person.habit`
0.992 → 0.983 and `person.lives_in` 0.800 → 0.769 slightly down and `works_as`, `prefers`, `topic`,
`relation_to` unchanged. The same file writes 7.5 % fewer tokens and the arm runs 30 % faster.

Read honestly: a new engine changes greedy decoding — kernels and the grammar engine — so the same
model's outputs move, here by +0.027, all of it precision. The pre-registration expected ≈ equal and
said a large move would be "a finding about the grammar engine, not about the model". **+0.027 is
larger than the frozen field's own margin between its winner and runner-up (0.7426 − 0.7201 = 0.0225):
the venue alone moves a score by more than the field's winning margin**, which is exactly why a verdict
is taken only within one build.

### NuExtract3 — the retry answered

`llama-server` loaded it on b10809 in 6 s and it completed all 1,670 calls: **validity 1.0000, F1
0.6467, scorable 0.7256, 817 s** — the fastest qualifying arm, 7.4× quicker than the incumbent, and
the only new model over the 0.60 floor. **The field's `missing tensor 'blk.32.ssm_conv1d.weight'` was
the BUILD, not the model**, and the file is byte-identical to the one the April build refused. Its
shape is lopsided: at or above 0.88 on every per-person and household predicate (`works_as` 1.000,
`lives_in` 1.000, `household.topic` 1.000, `owner.prefers` 0.915, `person.habit` 0.882) and nearly
blind to relations (`person.relation_to` 0.037, stated-relation recall 0.1000). That single predicate
carries 110 of the 370 gold and is the whole of its gap to the incumbent.

### The quantisation question — asked, NOT answered

Same model, same build, three quantisations — and a one-newline difference in the chat template
between the Q4_K_M file and the Q6_K / Q8_0 files. The token totals confirm it at the token level: the
Q6_K and Q8_0 arms each read 1,173,121 prompt tokens against Q4_K_M's 1,171,451 — exactly 1,670 more,
one per call.

| | Q4_K_M | Q6_K | Q8_0 |
|---|---|---|---|
| validity | 1.0000 | 1.0000 | 1.0000 |
| **F1** | **0.7695** | **0.6943** | **0.7428** |
| lenient / scorable | 0.7781 / 0.8697 | 0.7114 / 0.7839 | 0.7601 / 0.8399 |
| precision / recall | 0.8241 / 0.7216 | 0.7364 / 0.6568 | 0.7981 / 0.6946 |
| predictions / matches | 324 / 267 | 330 / 243 | 322 / 257 |
| relation STATED recall | 0.9333 | 0.3333 | 0.6000 |
| tokens out | 424,937 | 439,397 | 449,095 |
| seconds | 6,018 | 7,565 | 8,506 |

**Both higher-precision quantisations scored BELOW Q4_K_M**, and Q6_K scored below it in all ten
households (mean paired delta −0.0750). The loss is concentrated in one predicate:
`person.relation_to` 0.371 → 0.133 (Q6_K) and → 0.237 (Q8_0), the stated relations falling from 28 of
30 to 10 and 18. It is not a change in how much the model thinks: paired span by span over household 1,
Q6_K generated a mean of 268 tokens against Q4_K_M's 274, shorter on 101 spans and longer on 66, and
both thought on every span. Q6_K and Q8_0 — same template, different quantisation — agree exactly on 3
of 10 households and disagree elsewhere, so quantisation clearly moves the result too.

Read honestly: this comparison CANNOT separate the quantisation from the template's extra token,
because the two changed together. The rule does not care — it compares the numbers the arms produced,
and neither beat the incumbent — but **the question the two arms were added to answer ("is 0.7426 a
quantisation ceiling?") is not answered by them.** A controlled arm (one file, both templates, through
the per-model server-argument seam that already exists) would answer it; that is a pre-registration,
not a run added here.

### The Q8_0 file's provenance — a transient MISREAD, not a corrupt file

The `gemma-e4b-q8` run JSON records `model_sha256
0b456435106b0c3ae7e8c5e4123f7aaac8332c1474a3cdd51688a451b992992d`, which is **not** that file's hash.
The harness hashes at the END of an arm, so that read happened at 09:20, minutes after the arm's 8 GB
model had been mapped. Three further reads at 09:25 — `sha256sum` twice and `certutil`, three separate
processes — returned the same wrong digest, so it was not one flaky read.

The file is intact. The published Q8_0 GGUF was re-downloaded to a scratch directory, hashes
`6a6eba0d36a051b5d924211a889c1436717006e7c5d413830c47caa1d46cb598` (the repo's published digest), and
a byte-for-byte comparison against the local file found **0 differing bytes across all 8,031,242,720**.
After that comparison — roughly 16 GB of fresh I/O — the local file reads `6a6eba0d…` again from both
tools. Every other arm file re-hashed unchanged against the record taken at 00:03. Windows logged no
WHEA, machine-check or bugcheck events in two days; the PC carries 2 × 16 GB of non-ECC DDR4-3200.

The honest reading: the bytes on disk were always the published ones, and reads in that window came
back wrong — the shape of corrupted pages in the file cache while the arm's mapping was resident.
**What cannot be established after the fact is whether the same corruption touched the pages the arm
was decoding from while it ran.** The arm's own behaviour gives no sign of it — validity 1.0000 on all
1,670 calls, no truncation, no invalid JSON, coherent extractions, and a score that sits between its
two neighbours — but absence of a sign is not evidence.

**One event, two hypotheses, and this run cannot choose between them.** The Q8_0 arm was the only arm
whose model did not fit the card outright — the only host-plus-device split placement in the field —
and this PC's memory is non-ECC. Either could produce a page that reads wrong and later reads right.
What the machine's own history says, read over the System log's whole retention (2026-06-18 to now,
nothing scheduled and nothing rebooted): **no WHEA-Logger event at all**, and no memory-diagnostic
result has ever been written on this machine. A memory test is the operator's to run, not this run's.

**So the arm was re-run on the verified file, pre-registered before its number existed** (the
strategist's ruling of 2026-09-12), and the re-run's number is the arm's number. Run 1 is KEPT, never
overwritten: `ms1b_gemma-e4b-q8.json` became `ms1b_gemma-e4b-q8_run1.json`, and `load_field` now
excludes a `_run1.json` suffix beside `_contract0`/`_contract1` — a loud rule in the results directory
rather than a hidden folder, pinned by **T42f**, whose stub is given the highest F1 in its build so
that dropping the exclusion makes the superseded run win the verdict (mutant verified: it fails by
name, `['delta', 'delta', 'epsilon'] … 'delta'`).

Before the re-run, a **determinism control**: household 1 of `gemma-e4b-v040` through the same b10809
server, written to the scratchpad so no verdict can ever see it, compared with its recorded run
prediction for prediction. Identical means any Q8_0 delta between the two runs belongs to run 1;
different means the harness is not run-to-run deterministic and every delta is read against that.

**The control says the harness IS run-to-run deterministic here.** Household 1 of `gemma-e4b-v040`
was run again through the same b10809 server, written to the scratchpad. All **34 predictions are
identical to the recorded run's, in order and as a set**, and every scored field matches at full
precision: n_pred 34, n_match 25, n_gold 37, precision 0.7352941176470589, recall 0.6756756756756757,
F1 0.7042253521126761, lenient F1 0.7042253521126761, scorable 0.7936507936507937, validity 1.0000
over 167 of 167 calls, tokens in 116,993, tokens out 45,733. The only field that moved is wall-clock
(636.5 s recorded, 650.4 s in the control), which is not part of any score.

**So a difference between Q8_0 run 1 and run 2 — if one appears — belongs to run 1, not to run-to-run
variation.** That is what makes the re-run below a measurement rather than a second opinion.

**The re-run reproduces run 1 exactly.** `gemma-e4b-q8` ran again 15:38:51 → 18:00:44 in run 1's
configuration — the same queue command, no extra server arguments, nothing about mmap changed — and the
hash the harness recorded this time is `6a6eba0d…`, the published digest.

| | run 1 | run 2 |
|---|---|---|
| validity | 1.0000 | 1.0000 |
| F1 / lenient / scorable | 0.7428 / 0.7601 / 0.8399 | 0.7428 / 0.7601 / 0.8399 |
| precision / recall | 0.7981 / 0.6946 | 0.7981 / 0.6946 |
| predictions / matches | 322 / 257 | 322 / 257 |
| relation STATED recall | 0.6000 | 0.6000 |
| tokens in / out | 1,173,121 / 449,095 | 1,173,121 / 449,095 |
| recorded `model_sha256` | `0b456435…` | `6a6eba0d…` — the published digest |
| seconds | 8,226 | 8,506 |

Of the 22 scored aggregate fields, **21 are identical and the only one that moved is wall-clock** —
this run's own doing, since it hashed the 8 GB file twice mid-arm. All ten households match on F1,
predictions and matches, and **all 322 predictions are identical in order**. Per-household F1, both
runs: 0.7536 · 0.7059 · 0.7429 · 0.6761 · 0.8235 · 0.7536 · 0.6667 · 0.7826 · 0.7941 · 0.7324.

Read against the control, which showed this harness reproduces a household prediction for prediction:
**the misread never reached the decode.** Run 1's outputs are what the model produced from the correct
bytes. The re-run's number is the arm's number by the ruling — and it is the same number.

**The four hash points did NOT reproduce the misread, and one of them is weaker than intended:**

| point | when | llama-server | sha256 |
|---|---|---|---|
| (a) before the server starts | 15:38:45 | down | `6a6eba0d…` the published digest |
| (b) after household 1, mapping resident | 15:53:31 | **up** | `6a6eba0d…` the published digest |
| (c) at the end of the arm | 18:00:39 | down | `6a6eba0d…` the published digest |
| (d) just after the server exits | 18:00:45 | down | `6a6eba0d…` the published digest |

(c) was meant to be taken with the mapping still resident, but the harness prints its last household
line only as the arm ends and the poll that triggered (c) runs on a 20-second cycle, so the server had
already exited. **(c) is therefore a second post-exit reading rather than a second resident one, and
the resident-state sampling is n = 1** — it came back clean. GPU memory at (b) was 6,558 MiB of 8,192
at 95 % utilisation: the same host-plus-device split placement as run 1's 6,209 MiB, so the condition
under which run 1 misread was present again and produced nothing.

So the event stands **unreproduced and unexplained**: one occurrence, two live hypotheses — the split
placement's host-side mapping, or a non-ECC memory fault — with no WHEA event and no memory-diagnostic
result ever recorded on this machine. A memory test is the operator's next step, not this run's.

### Skips, deviations and the rules that did not fire

- **Nothing was skipped:** `QUEUE done=8 skipped=0`, every arm wrote its JSON, and `granite-3b` was
  SKIPPED-as-already-done only in the resume sense (it had completed at 14:42 on 2026-09-11).
- **`--build b10809` was added to the queue command** (the prompt's §5 line does not carry it). It only
  arms the per-arm assertion that the server reports the intended build before an arm runs; every arm
  passed it.
- **The prompt's §6 `--verdict --build v0.4.0` was corrected to `--build b10809`** — the release tag is
  not a build string and would have selected no runs at all.
- **The Q8_0 skip rule never fired.** The prompt allows skipping that arm if it "cannot load or times
  out per call"; measured over its 1,670 calls, zero were within reach of the client's 180-s timeout
  (longest 15.0 s) and zero hit the token cap.
- **No store-benchmark run and no design amendment**, both of which §6 conditions on a NEW winner;
  there is none.

### Honest scope

Synthetic seeded utterances that always name people, scored against ORACLE candidates, seeds 1–10, 14
days, 1,670 spans per arm — the field's corpus, contract and rule, unchanged. Nothing here was measured
on real speech, on household audio, or on the owner. Numbers are compared only within a build; the two
venues' tables sit side by side and are never merged. The addendum adds seven arms and one re-run to
the field's eleven; it does not re-open the frozen field, whose verdict is unchanged.

### The field addendum 2 — 2026-09-15 — thinking states measured, the choice rule corrected, the Qwen venue, the controlled template

**Verdict, by the corrected rule applied in code WITHIN llama.cpp build b10809: CHOSEN
`gemma-e4b-q8-q4tpl` at validity 1.0000 / F1 0.7907** — Gemma 4 E4B Q8_0 weights rendered through
the Q4_K_M file's own chat template, thinking ON. The OFF-only reading is reported beside it for
both builds, and the operating state — the configuration MS2 runs — is `gemma-e4b-q8-q4tpl`.

#### Why, and when the rule was corrected

The addendum (`3a98d48`) measured two things that made this run necessary.

**Every Gemma run in both fields had thought.** Every Gemma 4 GGUF's chat template reads
`enable_thinking` and emits `<|think|>` in the system turn when it is true; both llama.cpp builds
default `--reasoning auto`, which supplies `enable_thinking=true` whenever a request carries no
kwarg — and the Gemma keys sent none, because the rule's premise was that Gemma 4's channel has no
switch. That premise came from the BOX's hand-built prompt template (`JARVIS_THINKING`) and was
never checked against the Jinja engine that actually rendered these prompts. Measured at the
renderer, 167 of 167. Meanwhile the Qwen, Granite and NuExtract3 keys ran with thinking OFF.

So the field's "thinking OFF wherever a template offers a switch" clause was applied on a false
premise — and it is incoherent as a fairness rule in any case: LFM2.5 thinks with no switch to turn
off, and a switchable model that thinks WELL is handicapped by it. Applied as intended, that clause
would have made the frozen field's choice Qwen3 8B at 0.6196 rather than Gemma 4 E4B at 0.7426.

**The quantisation arms moved two things at once.** The Q6_K and Q8_0 files carry a newer chat
template than the Q4_K_M file, so those arms answered nothing about quantisation.

**The correction is dated, and the date is the point: it was made AFTER the Gemma thinking-ON
numbers were known and BEFORE any OFF number existed.** Pre-registered in the design and the plan at
`1b2927f`, 2026-09-12; the twelve arms ran 2026-09-13 to 2026-09-15. The rule now reads:

1. **The choice.** Every run under contract 2, the 2048-token budget, temperature 0, seed 1 and ONE
   venue is a legitimate configuration, and its thinking state is part of its identity. The highest
   F1 among runs at validity ≥ 99 % is CHOSEN. A switchable model is measured in BOTH states.
2. **The OFF-only reading.** The same rule over only the runs that did not think by choice,
   computed and REPORTED beside the choice for both builds.
3. **The operating state.** The chosen model's OFF key unless its ON key beats it by MORE than 0.01
   F1, with its cost in output tokens and seconds reported beside it.
4. **The render check.** A run's thinking state is MEASURED before its arm starts: the arm's own
   server renders household 1's 167 requests through `/apply-template`, the marker pattern is
   asserted against the expectation declared for that key from its own template, and a mismatch
   STOPS the queue. It is a finding, never a spec edit after the fact.

#### The arms — twelve, all on b10809, contract 2, 2048 tokens, temperature 0, seed 1, seeds 1–10

Every state was asserted at the renderer before its arm's first call; all thirteen renders are
`render_ok` over 167 of 167 prompts, and every run's `thinking_consistent` is True — the state that
was declared is the state that happened, at both the prompt end and the response end.

| key | file sha256 | kwarg sent | rendered | template | reasoning_calls | validity | F1 | lenient | scorable | zeroGP | tokens out | seconds |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `gemma-e4b-q8-q4tpl` | `6a6eba0d` | none | on | `55572b8d3c834204` | 1670 | 1.0000 | **0.7907** | 0.8052 | 0.8947 | 0 | 382,932 | 7,127.3 |
| `gemma-e4b-q8-q4tpl-nothink` | `6a6eba0d` | off | off | `55572b8d3c834204` | 0 | 1.0000 | 0.7744 | 0.7911 | 0.8715 | 0 | 35,152 | 873.5 |
| `qwen3-8b-v040-think` | `d98cdcbd` | on | on | `57f1fd00f0013a2b` | 1670 | 0.9958 | 0.7721 | 0.7925 | 0.8736 | 7 | 691,048 | 11,577.0 |
| `gemma-e4b-v040-nothink` | `6dfbdb0f` | off | off | `55572b8d3c834204` | 0 | 1.0000 | 0.7585 | 0.7749 | 0.8515 | 0 | 38,288 | 829.7 |
| `gemma-e2b-v040` | `9378bc47` | none | on | `33204f1acb5bd000` | 1670 | 1.0000 | 0.7069 | 0.7069 | 0.7962 | 12 | 595,032 | 4,848.5 |
| `qwen3-8b-v040` | `d98cdcbd` | off | off | `57f1fd00f0013a2b` | 0 | 1.0000 | 0.6281 | 0.7122 | 0.7019 | 10 | 22,824 | 516.3 |
| `qwen35-9b-v040` | `03b74727` | off | off | `7f0e529032c25183` | 0 | 1.0000 | 0.5775 | 0.5960 | 0.6459 | 47 | 22,194 | 655.6 |
| `gemma-e2b-v040-nothink` | `9378bc47` | off | off | `33204f1acb5bd000` | 0 | 1.0000 | 0.5755 | 0.5784 | 0.6504 | 0 | 38,268 | 487.6 |
| `qwen35-4b-v040` | `00fe7986` | off | off | `7f0e529032c25183` | 0 | 1.0000 | 0.5300 | 0.5433 | 0.6115 | 0 | 17,358 | 437.7 |
| `nuextract3-think` | `7ee3c0ee` | on | on | `6c0a83aee85a1bdb` | 1670 | 0.9988 | 0.4251 | 0.4475 | 0.4566 | 121 | 791,681 | 10,115.1 |
| `qwen35-4b-v040-think` | `00fe7986` | on | on | `7f0e529032c25183` | 1670 | 0.5060 | **0.0000** | 0.0000 | 0.0000 | 0 | 2,498,399 | 29,586.7 |
| `qwen35-9b-v040-think` | `03b74727` | on | on | `7f0e529032c25183` | 1670 | 0.5886 | **0.0000** | 0.0000 | 0.0000 | 0 | 2,330,058 | 42,597.6 |

`gemma-e4b-v040` (ON, 0.7695) is the addendum's own arm and carries into this field unchanged.
**The three kwarg spellings are distinct and the distinction is the subject of this addendum:** `off`
sends `{"enable_thinking": false}`, `on` sends `true`, and **`none` sends no kwarg at all** — which
`--reasoning auto` then resolves to thinking ON. Arms 10 and 12 send nothing on purpose, because the
frozen field's Gemma runs sent nothing; sending an explicit `true` would have moved the request shape
as well as the venue. Measured: an explicit `true` renders byte-identically to no kwarg on b10809,
167 of 167.

**The fetches.** Qwen3-8B from `Qwen/Qwen3-8B-GGUF` (`d98cdcbd…`, 5,027,783,488 B); Qwen3.5-4B
(`00fe7986…`, 2,740,937,888 B) and Qwen3.5-9B (`03b74727…`, 5,680,522,464 B) copied from the box.
Every sha256 equals the field's recorded digest. The other six files were already on disk unchanged.

#### The three readings

**THE CHOICE — `--verdict --build b10809`, all 20 runs in the build:**

| key | validity | F1 |
|---|---|---|
| **`gemma-e4b-q8-q4tpl`** | **1.0000** | **0.7907** |
| `gemma-e4b-q8-q4tpl-nothink` | 1.0000 | 0.7744 |
| `qwen3-8b-v040-think` | 0.9958 | 0.7721 |
| `gemma-e4b-v040` | 1.0000 | 0.7695 |
| `gemma-e4b-v040-nothink` | 1.0000 | 0.7585 |
| `gemma-e4b-q8` | 1.0000 | 0.7428 |
| `gemma-e2b-v040` | 1.0000 | 0.7069 |
| `gemma-e4b-q6k` | 1.0000 | 0.6943 |
| `nuextract3` | 1.0000 | 0.6467 |
| `qwen3-8b-v040` | 1.0000 | 0.6281 |
| `qwen35-9b-v040` | 1.0000 | 0.5775 |
| `gemma-e2b-v040-nothink` | 1.0000 | 0.5755 |
| `ministral-8b` | 1.0000 | 0.5425 |
| `qwen35-4b-v040` | 1.0000 | 0.5300 |
| `nuextract3-think` | 0.9988 | 0.4251 |
| `lfm25-2.6b` | 0.9359 | 0.4223 |
| `granite-8b` | 1.0000 | 0.2976 |
| `granite-3b` | 1.0000 | 0.0000 |
| `qwen35-4b-v040-think` | 0.5060 | 0.0000 |
| `qwen35-9b-v040-think` | 0.5886 | 0.0000 |

**THE OFF-ONLY READING, b10809 — 11 of 20 runs did not think by choice: CHOSEN
`gemma-e4b-q8-q4tpl-nothink` 0.7744**, then `gemma-e4b-v040-nothink` 0.7585, `nuextract3` 0.6467,
`qwen3-8b-v040` 0.6281, `qwen35-9b-v040` 0.5775, `gemma-e2b-v040-nothink` 0.5755, `ministral-8b`
0.5425, `qwen35-4b-v040` 0.5300, `lfm25-2.6b` 0.4223, `granite-8b` 0.2976, `granite-3b` 0.0000.

**THE OFF-ONLY READING, b8728 — 9 of 11 runs: CHOSEN `qwen3-8b` 0.6196**, then `qwen35-9b` 0.5801,
`qwen35-4b` 0.5729, `qwen3-4b` 0.3098, `llama-8b` 0.2944, `phi3-mini` 0.0798, `phi4-mini` 0.0233,
`llama-1b` 0.0000, `llama-3b` 0.0000. **Recorded, never re-based** — the MS1 row's `DONE 8c4cd2b`
stands as the measurement it was, and the plain frozen verdict still reproduces `gemma-e4b` 0.7426.

#### The operating state, and what it costs

`gemma-e4b-q8-q4tpl` — the ON key, because **ON beats OFF by 163 ten-thousandths (0.7907 vs
0.7744), more than the 100-ten-thousandth band.**

| | ON `gemma-e4b-q8-q4tpl` | OFF `gemma-e4b-q8-q4tpl-nothink` |
|---|---|---|
| matches / predictions / gold | 272 / 318 / 370 | 278 / 348 / 370 |
| F1, full precision | 0.79069767 | 0.77437326 |
| output tokens | 382,932 | 35,152 |
| seconds | 7,127.3 | 873.5 |

Gap **0.01632442**; per household ON − OFF mean **+0.0163**, standard deviation 0.0175, **ON higher
in 9 of 10**. The cost of thinking here is **×10.9 output tokens and ×8.2 wall time** for
+0.0163 F1. Read the mechanism, not only the total: thinking makes the extractor **more precise and
slightly less complete** — 30 fewer predictions and 6 fewer matches, which is a precision gain of
0.7989 → 0.8553 against a recall loss of 0.7514 → 0.7351.

**The band is compared in the aggregates' own 4-decimal precision, and that is not a detail.** The
runs are written rounded to 4 dp, so comparing the raw doubles asks a question the numbers cannot
answer — `0.7695 - 0.7595` is `0.010000000000000009`, which is "more than 0.01", and a pair sitting
exactly on the pre-registered band would be sent to ON by an artefact of the representation. This
was found and fixed before any verdict was computed (`c43cbee`). **The deciding pair clears the band
by 63 ten-thousandths, about 1.1 standard errors of the paired difference** — firmer than the E4B
Q4 pair, which would have decided ON by only 0.0009 (110 against the band's 100, mean +0.0111, sd
0.0387, ON higher in 7 of 10). Both are reported; neither is presented as decisive on its own.

#### The quantisation question, answered at ONE variable

The addendum could not answer it because its Q6_K and Q8_0 arms moved quantisation and template
together. The two controlled arms load Q8_0 weights with the Q4_K_M file's own template, read out of
that pinned GGUF at run time and passed as `--chat-template-file`:

| state | Q4_K_M | Q8_0 with the Q4 template | delta |
|---|---|---|---|
| thinking ON | `gemma-e4b-v040` 0.7695 | `gemma-e4b-q8-q4tpl` **0.7907** | **+0.0212** |
| thinking OFF | `gemma-e4b-v040-nothink` 0.7585 | `gemma-e4b-q8-q4tpl-nothink` **0.7744** | **+0.0159** |

**Q8_0 beats Q4_K_M in both states, on the same template.** The control is provably not vacuous: each
controlled arm's 167 prompts hash **identically** to its Q4 reference (167 of 167, both states),
while the ON and OFF references differ from **each other in all 167** — so the only thing that moved
is the quantisation.

#### The template, also at one variable — and it is worth MORE than the quantisation

The same Q8_0 file (`model_sha256` `6a6eba0d`, identical in both arms), both thinking ON, differing
only in which template rendered it:

| template | F1 | prompt tokens per call |
|---|---|---|
| the Q8_0 file's own, newer | `gemma-e4b-q8` 0.7428 | 702.468 |
| the Q4_K_M file's, borrowed | `gemma-e4b-q8-q4tpl` **0.7907** | 701.468 |

**+0.0479 from one newline after `<|think|>`.** The one-token claim is verified here rather than
cited: 1,173,121 − 1,171,451 = **1,670 prompt tokens over 1,670 calls = exactly 1.0000 per call**.
The addendum was right to refuse the quantisation question while the two moved together — the
template is the larger effect of the two.

**CONSEQUENCE FOR MS2, and it is load-bearing: the operating key BORROWS a template.** Its identity
is not the model file alone. MS2 must pin it by **`template_from_sha256` = `55572b8d3c834204`**
alongside the Q8_0 weights, or it will run a different configuration than the one chosen here.

#### Thinking, arm by arm, against its OFF twin

| pair | F1 | output tokens | seconds |
|---|---|---|---|
| Qwen3 8B | 0.6281 → **0.7721** (+0.1440) | 22,824 → 691,048 | 516 → 11,577 |
| Gemma E2B | 0.5755 → **0.7069** (+0.1314) | 38,268 → 595,032 | 488 → 4,849 |
| Gemma E4B Q8 (Q4 tpl) | 0.7744 → **0.7907** (+0.0163) | 35,152 → 382,932 | 874 → 7,127 |
| Gemma E4B Q4 | 0.7585 → **0.7695** (+0.0110) | 38,288 → 424,937 | 830 → 6,018 |
| NuExtract3 | 0.6467 → **0.4251** (−0.2216) | 22,129 → 791,681 | 817 → 10,115 |
| Qwen3.5 4B | 0.5300 → **0.0000** (−0.5300) | 17,358 → 2,498,399 | 438 → 29,587 |
| Qwen3.5 9B | 0.5775 → **0.0000** (−0.5775) | 22,194 → 2,330,058 | 656 → 42,598 |

**Thinking is not uniformly good, and it is never cheap:** every ON arm costs an order of magnitude
in output tokens, it helps four models, it halves NuExtract3 — the purpose-built extractor — and it
destroys both Qwen3.5 keys.

#### The two zeros, with their cause MEASURED

Both Qwen3.5 thinking-ON arms scored 0.0000 at validity far below the band. The cause is not
inferred from the score; it is read off each arm's own server log, with the extraction validated
against the run's recorded `tokens_out` before being believed:

| | `qwen35-4b-v040-think` | `qwen35-9b-v040-think` |
|---|---|---|
| calls | 1,670 | 1,670 |
| generated tokens per call, min / median / mean / **max** | 222 / 1,936.5 / 1,496.0 / **2,048** | 688 / 1,106.0 / 1,395.2 / **2,048** |
| **calls at the 2,048 generation cap** | **825 = 49.40 %** | **687 = 41.14 %** |
| `invalid_reasons_total` | `{'unparsed': 825}` | `{'unparsed': 687}` |
| validity | 0.5060 | 0.5886 |
| `n_pred` | 0 | 0 |
| sum of generated tokens vs recorded `tokens_out` | 2,498,399 = 2,498,399 | 2,330,058 = 2,330,058 |

**The correspondence is exact in both arms: every call that hit the cap returned nothing parseable,
and every unparsed call hit the cap.** The model opens a thinking block, never closes it inside
2,048 tokens, and emits no JSON at all. The `invalid_raw` samples are empty strings.

**And the cap is only half of it.** `n_pred` is **0** in both arms, so even the calls that *did*
parse produced an empty candidate list. Neither zero is explained by truncation alone.

**A trap avoided, recorded because the obvious marker is the wrong one.** `stop processing:
n_tokens = N` gives 944 calls (56.5 %) at or over 2,048 for the 4B — but `n_tokens` is the whole
slot span, prompt included: its maximum is 2,742, above the cap, and its mean 2,179.8 reconciles as
684.8 prompt + 1,496.0 generated. The generated count comes from the final `eval time = … / N
tokens` line, and was validated against `tokens_out` before use.

#### The Qwen venue delta, OFF on both builds

| key | b8728 | b10809 | delta |
|---|---|---|---|
| Qwen3 8B | 0.6196 | 0.6281 | **+0.0085** |
| Qwen3.5 4B | 0.5729 | 0.5300 | **−0.0429** |
| Qwen3.5 9B | 0.5801 | 0.5775 | **−0.0026** |

The venue moves a model in either direction and by more than the operating band in one case, which
is exactly why numbers are compared only within a build.

#### Per predicate, all ten households

| predicate | operating `…q8-q4tpl` | its OFF twin | `gemma-e4b-v040` | `qwen3-8b-v040-think` | `nuextract3` |
|---|---|---|---|---|---|
| `person.works_as` | 1.0000 | 1.0000 | 1.0000 | 1.0000 | 1.0000 |
| `household.topic` | 0.9917 | 1.0000 | 0.9449 | 1.0000 | 1.0000 |
| `owner.prefers` | 0.9836 | 1.0000 | 1.0000 | 0.9153 | 0.9147 |
| `person.habit` | 1.0000 | 0.9449 | 0.9833 | 0.9344 | 0.8819 |
| `person.lives_in` | 0.9474 | 0.8571 | 0.7692 | 0.8364 | 1.0000 |
| `person.relation_to` | **0.4054** | 0.4224 | 0.3709 | 0.3922 | **0.0375** |
| `household.routine` | **0.1667** | 0.1333 | 0.3279 | 0.4444 | 0.0000 |

The shape is the field's, unchanged: at or near ceiling on the person and household predicates, and
weak exactly where MS1b has no people layer. For the operating key, `person.relation_to` holds 110
gold against 38 predictions (30 matched) and `household.routine` 20 gold against 40 predictions
(5 matched) — under-prediction on relations, over-prediction on routines.

#### The store on the operating key's candidates (REPORTED, never banded here)

update **0.925** · coexisting **0.4167** · relation precision **0.75** · transfer recall@5 **0.0** ·
growth drop **0.0** points · audit **0 violations** · spouse surfaced **0/10**. Against the MS1
winner's recorded run (update 0.75, coexisting 0.4167, relation precision 0.6167, transfer 0.9084,
audit 0): update and relation precision improve on a better extractor, coexisting is unmoved, and
transfer collapses because this run has `embedder: none` — the MS0 full-text lane alone.

**[CORRECTED 2026-09-16 in MS2a-1: the run above had the embedding lane OFF because the command dictated to it omitted --embedder qwen - the strategist's defect - so it is not comparable to the MS1 winner's store run, which had the lane ON, and its attribution of the update gain to the extractor is withdrawn. Like for like (embedder qwen, 20,000 latency facts), ms1b_store_on_extracted_gemma-e4b-q8-q4tpl_qwen.json: update 0.925, coexisting 0.4167, relation precision 0.75, transfer 0.9167, growth drop 5.0, audit 0, p99 0.312 ms.]**

#### Two interruptions, and why the queue log shows two STARTs without an END between them

The queue ran detached across three days and was stopped twice, neither time by a defect in the run:

1. **2026-09-13 05:56** — Claude Code's background-task memory guard stopped the queue during
   `qwen3-8b-v040-think` at 826 of 1,670 calls, because it was a background command of a session that
   had run out of context. Not Windows: no `Resource-Exhaustion-Detector` event. The queue was
   relaunched through WMI, parented to `WmiPrvSE.exe`, outside any session's process tree.
2. **2026-09-14 ~14:40** — the operator stopped it for a game, again during `qwen3-8b-v040-think`
   (1,247 of 1,670).

A killed arm re-runs whole, so neither partial run produced a JSON. That is why the queue log carries
**three** `qwen3-8b-v040-think START` lines and only the last has an `END`, at 2026-09-15 02:43:50.
Ten finished keys `SKIP already done` on each relaunch, each checked against `--build b10809` rather
than skipped blindly.

#### A provenance field is wrong in one run JSON, and is left as written

`ms1b_qwen3-8b-v040-think.json` records `model_sha256` `91968a41…`, while two other runs of the same
file record `d98cdcbd…`, the pre-registered digest. Measured, by the test this project's precedent
demands — re-fetch and compare, never re-read and hope:

- a fresh download from `Qwen/Qwen3-8B-GGUF` and the copy on disk differ in **0 of 5,027,783,488
  bytes**, the full file, so the comparison is not vacuous; both equal `d98cdcbd…`;
- four later reads through two independent tools all return `d98cdcbd…`, and the file's mtime never
  moved.

**The file was correct the whole time; three earlier readings were page-cache misreads, and what
evicted the bad pages was the 5 GB download itself.** All three Qwen3-8B arms therefore ran
byte-identical weights, and their venue delta and thinking cost stand. The run JSON is **not
edited**: it records what was measured, and correcting a provenance field to make it look right is
the one thing that must never be done. **This is the second time this mechanism has poisoned a
provenance read here** — the first was `gemma-e4b-q8` run 1 on 2026-09-12, kept as `_run1.json` and
excluded from every verdict. The harness computes that hash immediately after a 5 GB mapping is torn
down, which is the defect; hashing before the server starts, or twice with the readings compared,
would make it self-detecting. That fix is a separate reviewed change and is not made here.

#### Skips, and the rules that did not fire

Q6_K — with Q8_0 controlled it answers no question MS2 asks. Qwen3 4B (0.3098), Llama 3.1 8B
(0.2944), Phi-3 mini, Phi-4 mini, Llama 3.2 3B and 1B — outside the pre-registered admission margin.
LFM2.5 and Ministral — no switch, already measured on b10809. Granite 3B and 8B — already OFF at
0.0000 and 0.2976; a thinking-ON arm 0.3 below the floor cannot become the choice. No render
mismatch stopped the queue; no arm was skipped; `QUEUE done=12 skipped=0`.

**One spec was set from a template before its arm ran, and that is recorded rather than glossed:**
the three Qwen thinking-ON keys were first declared with one spec — that no think marker appears —
which is true of Qwen3 and false of Qwen3.5, whose template emits an OPEN `<think>\n` when
`enable_thinking is true`. The render pre-flight caught it 167 of 167 before either Qwen3.5 arm ran,
and the spec was corrected from the model's own template (`30a880f`), before any number of theirs
existed. A spec read from a template is not a threshold moved after a result.

#### Honest scope

Synthetic seeded utterances that always name people, scored against ORACLE candidates, seeds 1–10,
14 days, 1,670 spans per arm — the field's corpus, contract, budget and rule, unchanged. Nothing
here was measured on real speech, on household audio, or on the owner. Numbers are compared only
within a build. **A rule was corrected mid-stream**, and that is stated rather than smoothed: the
premise it rested on was measured false, the correction was pre-registered before any new number
existed, and the frozen field's `DONE 8c4cd2b` stands as the measurement it was rather than being
re-based.

## MS2a-1 — 2026-09-16 — contract 3 beside contract 2, the base controls, the chosen extractor under contract 3

**What this milestone is: the setup MS2a-2 measures on.** Contract 3 exists beside contract 2, the
two ORACLE base controls reproduce MS1a.4 exactly with the embedding lane off and on, the
addendum-2 store run is re-taken with the lane it was always supposed to have, and the chosen
extractor is re-run over the extended corpus. **No MS2 band is scored here** — MS1 is closed and
this is MS2a-2's input, not a re-verdict.

### Two read-only reviews shaped this, and one of their findings was mine to own

The first MS2a prompt could not have run. Reviews `wf_1d619338-cca` and `wf_b6930383-224`, each
high finding tried by two skeptics, measured why: flipping `CONTRACT` to `contract3` aborts the
suite inside T37b, because the queue's contract-2 guard raises an uncaught `SystemExit`; the corpus
change could not keep the growth filler at 30× the gold count; the new spans move what the ORACLE
control measures; the chosen key's render check would compare a contract-3 prompt against a
contract-2 digest; the hearsay rule needs the household's names, because an extractor names a
speaker by their own name.

**And the same reviews found a defect in a number already published.** The command dictated for
addendum 2's store run omitted `--embedder qwen`, so
`ms1b_store_on_extracted_gemma-e4b-q8-q4tpl.json` recorded `embedder: none` and transfer 0.0 — while
the run it was compared against, `ms1b_store_on_extracted.json`, had the lane ON. CLAUDE.md line 215
printed those numbers beside the choice. The correction is above, in place, beside the sentence it
corrects.

### Contract 3 COEXISTS with contract 2

The MS1 field is closed. A tree that could no longer reproduce it would make its verdict
uncheckable, so contract 2 stays the default everywhere and a contract-3 run asks for it by name.
**The literals were measured at HEAD before any edit**, which is what makes them a check on the
change rather than a restatement of it:

| what | measured before the edit | after |
|---|---|---|
| contract-2 schema | `846ad08857eb37f8…` | unmoved |
| `system_prompt()` | `31d99140f87105ac…`, 2,601 chars | unmoved |
| default request body | `987689165c1edc71…` | unmoved |
| households 1 / 2 / 3 | `2f51271d…` / `71e8d774…` / `4cac801a…` | unmoved |

Contract 3 differs from contract 2 by **exactly one schema key** — `maxItems: 4` on the candidates
array — plus the prompt's three changes (the `ended` line; a change is a new current value on
`lives_in` and `works_as`; routine and topic re-drawn on the schedule boundary), and it **appends**
four spans and their gold: 171 spans whose first 167 are contract 2's, 41 gold whose first 37 are,
the identical 1,110-row filler, every scored set untouched. The filler is sized from the 37 gold
before the appends, which is what keeps it at 1,110 and not 1,230.

Also landed: render digests now record their contract, schema, seed and days, and a controlled arm's
reference must match all four — strictly under contract 3, while a legacy contract-2 digest missing
them reads as the values every pre-MS2a digest was taken at, so the committed digests keep working.
The model file is hashed **before** its server starts and again after it exits (two page-cache
misreads have already put a wrong digest into a run JSON). Finish reasons are counted. And on
contract-3 EXTRACTED runs only, a stated `person.relation_to` whose subject is not the speaker is
demoted to `inferred` — hearsay accrues by day instead of arriving at confidence 1.0.

`test_memory_logic.py` 288 → **301 checks** (T44a–T44m), the suite's diff **additions only, all after
T43q**. Five mutants, control green first and each restored from a byte-copy verified by md5: M1
contract 3's `maxItems` under contract 2 → the **queue guard**, which stops the suite inside T37b
reporting `93af3702870ea822…` instead of `846ad08857eb37f8…`; M2 the corpus extension under contract
2 → T44d **and the pre-existing T17g, T21b, T36c** — wider than predicted, and right, because the
extension under contract 2 changes the corpus every earlier milestone was measured on; M3 → T44e;
M4 → T44f and T44m; M5 → T44g.

### The contract-3 renders

Both on `b10809-5266f24da`, CPU-only: `gemma-e4b-v040` and `gemma-e4b-q8-q4tpl`, **171 prompts each,
identical 171 of 171**, `contract3`, schema `93af3702870ea822…`, seed 1, days 14, the controlled arm
carrying `template_from_sha256` `55572b8d3c834204…`. No committed contract-2 digest changed.

### The addendum-2 store run, re-taken like for like

Both runs `embedder: qwen` with 20,000 latency facts:

| field | MS1 winner's run | the corrected run | the defective first run |
|---|---|---|---|
| update | 0.75 | **0.925** | 0.925 |
| coexisting | 0.4167 | **0.4167** | 0.4167 |
| relation precision | 0.6167 | **0.75** | 0.75 |
| transfer recall@5 | 0.9084 | **0.9167** | **0.0** |
| growth drop | 5.0 | **5.0** | 0.0 |
| audit | 0 | **0** | 0 |
| p99 | 0.3209 ms | **0.312 ms** | not measured |

**Transfer 0.0 → 0.9167 from restoring one flag.** Stated rather than glossed: the MS1 winner's run
recorded sentence-transformers **5.0.0** and this one records **5.2.0**, so the pair is like-for-like
on flags and corpus but not on library version.

### The two base ORACLE controls — both EXACT

| control | result |
|---|---|
| lane **OFF** vs `ms1a4_control_none.json` | **0 moved fields** over every reference key (timing excluded), identical bands |
| lane **ON** vs `ms1a4_run.json` | **0 moved fields**, and all sixteen pre-registered values plus `audit_violations` matched one by one |

The lane-ON control reproduced update 1.0, update-paraphrase 0.5125, coexisting 1.0, transfer
0.9167, growth update 0.9625, growth drop 3.75, growth-update-paraphrase 0.45,
growth-drop-paraphrase 6.25, `transfer_gold_pref_rank1` 0.6917, `pref_rank_mean` 1.525,
`pref_lane_rank_mean` 1.3417, `vec_rank_mean` 78.4833, relation precision 1.0, spouse **10/10 on day
8.0**, preference-in-top-5 0.05, audit 0, all six bands true. **The §0 disposition never fired**, so
§6 was allowed to run. Both controls add only `contract` and `env`.

### The contract-3 control — REPORTED, and it moved

This is the reference MS2a-2's extracted bands are read beside, and the design predicted it could
move: the four new spans can change retrieval by construction.

| field | lane-ON base | contract 3 |
|---|---|---|
| `update_acc` | 1.0 | **1.0** (unmoved) |
| `update_acc_paraphrase` | 0.5125 | **0.225** |
| `growth_update_acc` | 0.9625 | 0.9375 |
| `growth_update_acc_paraphrase` | 0.45 | 0.2125 |
| `growth_drop_points` | 3.75 | **6.25** (the `growth_drop<=5` band flips to FAIL) |
| `growth_drop_paraphrase_points` | 6.25 | 1.25 |
| `transfer_gold_vec_rank_mean` | 78.4833 | 79.9833 |

Coexisting, transfer, relation precision, spouse 10/10 on day 8.0, preference-in-top-5, audit and
p99 are all unmoved.

**The cause, measured in memory and written nowhere.** Re-running the moved queries on both corpora
names two mechanisms, both confined to the **paraphrase** set — the set worded outside the predicate
hint's vocabulary, so those queries run unrestricted:

1. **The new `person.name` fact outranks `person.lives_in`.** On *"which town is {partner} based in
   these days"* and *"where is {partner} settled now"*, contract 2 returned the correct
   `person.lives_in` fact and contract 3 returns `person.name={partner}`. The answer is beaten by a
   fact about the **same person**, which the subject gate cannot exclude.
2. **A chatter span wins outright** on the owner's two paraphrase questions — `SPAN: i will sort it
   out` — where contract 2 returned the right fact.

`update_acc` stays 1.0 because the hinted questions restrict to the right predicate; only the
unhinted paraphrases move. **And the growth band must be read beside its absolutes**, because a drop
is a difference of two terms and this project has been caught before by watching only the delta:
base 1.0 → 0.9625 (drop 3.75) against contract 3's 1.0 → 0.9375 (drop 6.25); while the paraphrase
drop *falls* 6.25 → 1.25 only because its starting term collapsed.

### The chosen extractor under contract 3

`gemma-e4b-q8-q4tpl`, **validity 1.0000, F1 0.8021** on the 410 gold. **That is NOT comparable to
contract 2's 0.7907, which was measured on 370 gold** — a different denominator and four extra gold
per household. The gate held: `model_sha256` `6a6eba0d36a051b5…` and `template_from_sha256`
`55572b8d3c834204…` both byte-equal to `ms1b_gemma-e4b-q8-q4tpl.json`, `model_sha256_agree` **True**
with no re-read needed, build `b10809-5266f24da`, 1,710 calls all valid.

| predicate | contract 3 | contract 2 |
|---|---|---|
| `household.topic` | 1.0000 | 0.9917 |
| `owner.prefers` | 1.0000 | 0.9836 |
| `person.trait` | **1.0000** (20/20) | — no gold |
| `person.works_as` | 0.9667 | 1.0000 |
| `person.lives_in` | 0.9524 | 0.9474 |
| `person.habit` | 0.9500 | 1.0000 |
| `person.name` | **0.5714** (8/20) | — no gold |
| `household.routine` | **0.4912** | **0.1667** |
| `person.relation_to` | **0.3467** | 0.4054 |

**What contract 3 was written to fix, and what it did:**

- **`household.routine` 0.1667 → 0.4912** — the schedule boundary was the largest recoverable loss
  and it moved most, though it is still the second-weakest predicate.
- **`ended` on the ten "i stopped, i no longer" spans: 0/10 → 4/10.** Better, not fixed.
- **"we moved to … last week": 7 → 11 `lives_in` predictions over the ten spans** (11 because one
  span drew two candidates).
- **`finish_length` 0**, `finish_reasons {"stop": 1710}` — `maxItems: 4` did its job; not one call
  hit the token cap.
- **The new gold: 28 of 40 matched** — `person.trait` 20/20, `person.name` 8/20.

**What it did not fix, stated plainly:** `person.relation_to` **fell** 0.4054 → 0.3467, and
`person.habit` and `person.works_as` each slipped from 1.0000. The relation predicate is the one
MS2a-2's people layer exists for, and it is the weakest thing the extractor does.

Throughput: 1,681 of 1,710 calls returned reasoning content, 378,026 output tokens, 7,056.5 s.

### Honest scope

Synthetic seeded template households, ORACLE gold, seeds 1–10, 14 days. Nothing here was measured on
real speech, on household audio, or on the owner. No MS2 band is scored: the extracted bands are
MS2a-2's, read beside the contract-3 control above. The contract-2 field is untouched and remains
reproducible byte for byte.

---

## MS2a-2 — 2026-09-18 — the people layer and the MS2 bands

Commits `0ab53d1` (the layer, test-first) and this one (the measurement).

### What shaped it

A second read-only review simulated the first draft of this milestone on the real store and found
five things it left open, each of which is a rule below rather than a note: the extractor's STATED
edges were being lost into the rules' inferred rows (merge by rank); extracted `she` candidates were
left pending forever (candidate-level resolution); four of the design's MS2 bands had been dropped
(the nine-band block); M9 had nothing to bite (`support_dates`); and the reject rows, the aggregation
and the harness order were unspecified. Every pinned expectation below was computed by the strategist
over the real generator before this file was written, and re-computed here in the test rather than
typed.

### The rules as built

A third-person pronoun resolves to the unique non-owner cluster heard on its own day; if nobody was
heard that day, through the MOST RECENT day of the previous three that holds exactly one; two or more
on the deciding day is unresolved and does NOT fall further back. **A union over the window would be
wrong and the corpus says so:** seed 1's day 11 holds nobody, day 10 the partner alone, day 9 both
the partner and the visitor — the union is ambiguous where the most-recent-day rule resolves, which
is the difference between the day-11 contradiction landing and being thrown away.

ER1 is co-presence at ≥ 3 spans each. ER2 is a HOUSEHOLD_CUES word with `we`/`us`/`our` or a resolved
pronoun; ER3 a KIN_CUES word; both give one supporting DATE to `partner` and `spouse` respectively.
ER-C does not ingest a candidate — a contradiction-only day cites no supporting span and the
validator refuses it — it LINKS the firing span with role `contradict` to the existing current
`partner` and `spouse` edges and recomputes them. A cue that fires with no target, and a
contradiction with no edge to link or a non-person target, each write ONE `audit` row op `reject`
rule `people` keyed by the span and checked before writing, so a fourteen-day replay writes it once.
A span carrying a pronoun that does not resolve takes NO target and never falls through to the ER1
cluster. Lexicons are frozen in `registry.py` and matched as whole words — `lovely` is not `love`,
`weekday` is not `week` — with CONTRA entries matched as whole-word sequences.

**The switch, and why it is one.** The evidence rules and candidate-level resolution run only under
`people_layer` (`bench_ms0.py --people-layer`); merge by rank and the new scoring FIELDS are
unconditional. Design (4): every store run taken before this milestone must stay re-runnable. That is
measured, not asserted — see the switch-off regression below.

**As built, so the log cannot claim more:** a NAME ref resolves through the harness's alias over the
household's names; an ER candidate cites the spans that FIRED, a narrowing of the design's wording
that cannot move a confidence because R5 counts distinct dates. Name-earning from a vocative is
DEFERRED to MS2b — the harness stamps the partner's `display_name` at promotion on every path, so
MS2a cannot exercise it.

### The stop gate

Seed 1's spans alone, fed day by day, every expectation computed from `confidence()` in the test and
never typed. `partner` owner→partner reads `confidence(4,0)` on day 6, `(4,1)` on day 11 through the
day-10 fallback, `(5,1)` on day 12 and `(6,1)` on day 13. `spouse` is absent through day 7, `(1,0)`
on day 8 and `(1,1)` = 0.0 from day 11 — the contradiction applies to it too. The two-day visitor
never becomes a person and no edge is ever computed for it; zero reject rows. **Identical on the
contract-2 and contract-3 corpora**, which is what the four appended spans carrying no lexicon word
were for. The edge first appears on day 3, not day 1: the day-1 and day-3 spans both count, but an
edge cannot exist until the partner earns personhood — the amended §3.4 doing exactly what it says.

### Tests and mutants

T45a–T45n, the suite 301 → **315** local, **314** on the CI runner (T22g announces its numpy skip
there; both are the suite's own output). Eight mutants from byte-copies, control green first, each
restore verified byte-identical, each producing EXACTLY its predicted failing set:

| mutant | what it breaks | failed |
|---|---|---|
| M6 | `love` out of `KIN_CUES` | T45b T45f T45j |
| M7 | the fallback resolves over the UNION of the window | T45a T45f T45h T45m |
| M8 | ER-C replaced by an ingest the validator refuses | T45f T45g T45h |
| M9 | `support_dates` counted per firing span, not per date | T45c |
| M10 | a `spouse` over-claimed on a gold `partner` scored coarse | T45e |
| M11 | merge by rank removed | T45k T45l |
| M12 | an unresolved candidate pronoun held pending | T45m |
| M13 | `people_layer` ignored, the layer always on | T44m T45l T45m T45n |

**M9 is invisible to every confidence assertion BY CONSTRUCTION, and the log says so rather than
leaving a thin-looking mutant unexplained:** R5 counts `distinct_days`, so two spans on one date give
`confidence(1,0)` = 0.2835 and a per-span count would imply 0.4866 — a value no edge in this store
reaches from a single date. `support_dates` is the only observable that can bite it, which is why
T45c exists. **M13's T44m failure is M13's doing, never a defect:** T44m's recorder records every
ingested `person.relation_to` candidate, so the rules' own inferred candidates lengthen its list and
its `['inferred']` / `['stated_owner']` equality cannot hold. T21 is unaffected — both of its runs
carry the layer equally — and it appeared 0 times in M13's failing set.

### The switch-off regression

With `--people-layer` absent the shipped code reproduces both committed runs field for field:
`ms2a_control_base_none.json` EXACT over 919 leaves, and
`ms1b_store_on_extracted_gemma-e4b-q8-q4tpl.json` EXACT over 909. Zero diffs either side, timing
excluded.

### Both controls, and the one relation move that is pre-registered

| control | leaves | sanctioned moves | unsanctioned | bands |
|---|---|---|---|---|
| base, lane OFF, layer on | 929 | 22 | **0** | 8 true, transfer None |
| base, lane ON, layer on | 930 | 22 | **0** | all nine true |
| contract 3, lane ON, layer on | 930 | 22 | **0** | eight true, growth drop MISS |

The 22 sanctioned moves are exactly the pre-registered ones: aggregate and `reported`
`relation_precision` 1.0 → 0.6667, and per household `relation_precision` 1.0 → 0.6667 with
`relations_surfaced` 2 → 3. Nothing else moved on any control. **The lane-ON disposition never fired**
— no retrieval, growth or preference field moved, so no re-run is owed and
`ms2a_control_base_rules_rerun.json` does not exist. `transfer_recall5` 0.9167,
`update_acc_paraphrase` 0.5125, `growth_drop_points` 3.75 and `transfer_gold_vec_rank_mean` 78.4833
all reproduce MS1a.4 to the digit, on an identical stack (torch 2.5.1+cu121, sentence-transformers
5.2.0, transformers 4.57.3, CUDA 12.1, driver 610.88).

**THE TWO RELATION FIGURES ARE DIFFERENT QUESTIONS AND MUST NEVER BE READ AS ONE.** The historical
per-edge `relation_precision` moves 1.0 → **0.6667** on the oracle path BY CONSTRUCTION: the rules add
a CORRECT `partner` edge beside the oracle's `spouse`, so each household surfaces three edges where it
surfaced two and the denominator grows. The MS2 band reads `relation_precision_pairs` — the finest
surfaced edge per ordered pair, pooled — which stays **1.0** with 20 fine of 20 pairs and 0 wrong.
The layer got MORE specific and the per-edge number went down; that is the figure's shape, not a
regression.

### The nine bands, on the chosen extractor's contract-3 candidates

`ms2a_run.json`, ten households, 14 days, embedder qwen, layer on; the contract-3 control with the
layer beside each.

| band | run | verdict | control |
|---|---|---|---|
| update_acc ≥ 0.85 | 0.925 | MET | 1.0 |
| coexist_recall ≥ 0.85 | **0.6667** | **MISSED** | 1.0 |
| transfer_recall5 ≥ 0.60 | 0.9167 | MET | 0.9167 |
| growth drop ≤ 5 pts | **6.25** | **MISSED** | 6.25 |
| relationship surfaced ≥ 8/10 | **10/10** | MET | 10/10 |
| relation_precision_pairs ≥ 0.90 | **1.0** | MET | 1.0 |
| relations_wrong == 0 | **0** | MET | 0 |
| audit == 0 | 0 | MET | 0 |
| p99 ≤ 50 ms | 0.219 ms | MET | 0.2178 ms |

**Seven of nine MET.** Beside them: `spouse` surfaced **1/10** at day mean 8.0 (the control 10/10) —
REPORTED as the design requires; relationship day mean 8.5; per-edge `relation_precision` 0.5167;
fine 11, coarse 9, wrong 0 over 20 pairs; `pronouns_resolved` 8; `rank_upgrades` 8;
`people_rejects` 0 (`{span: 0, candidate: 0}`); `edges_reopened` 0; `pending_at_end` 7;
`hearsay_demoted` 5. Per household the finest owner→partner edge is `partner` in nine and `spouse` in
seed 5, at confidence 1.0 everywhere except seed 8 at 0.8111, first surfaced on day 8 in nine
households and day 13 in seed 8.

**The fine/coarse split is the honest shape the design predicted.** The partner→owner `spouse` edge
is stated by her and scores fine in all ten; the owner→partner pair surfaces `spouse` in seed 5 alone
and the coarser `partner` in the other nine. 11 + 9 = 20, precision 1.0, **0 wrong**. The rules see
cohabitation; only a kin cue sees marriage; the corpus plants one kin cue in fourteen days — and a
system that said `spouse` on that would be over-claiming, which is why `partner` for a gold `spouse`
is coarse-correct and `spouse` for a gold `partner` would be wrong.

### The two misses, traced

**`coexist_recall` 0.6667 — the extractor never marks `ended`, and this is not the layer.** The corpus
plants `i stopped, i no longer <habit>` on day 10. Exactly **4 of 10** households carry any
`ended=True` candidate at all, and exactly those 4 have zero leaks; the other 6 leak the ended value
back and the scorer's hard-zero rule fires on that question. That reproduces MS2a-1's independently
measured `ended` 4/10 from two directions. **Seed 1 is the specific shape:** the extractor DID cite
the right span (sid 139), got the predicate `person.habit` and the object `reads before bed` right —
and marked it **`ended=False`**. It read a statement that something stopped and recorded it as
current. A second, smaller contributor: `school run` is extracted as a `household.routine` in **all
ten** households and `washing` in six, both mis-predicated from hint spans, crowding the two gold
routines. The control on oracle candidates reads 1.0 with 0 leaks, so neither the store nor the layer
is implicated.

**`growth drop` 6.25 — inherited from the contract-3 corpus, not from this milestone.** The
contract-3 control with the layer reads the SAME 6.25 and misses the same band, while the contract-2
base control with the layer reads 3.75; this is MS2a-1's recorded contract-3 move. **The two 6.25s
are not the same measurement and a drop is a difference of two terms:** the run falls 0.925 → 0.8625,
the control 1.0 → 0.9375.

### The no-layer arm — what the rules supplied and what the extractor supplied

`ms2a_run_nolayer.json`, REPORTED, never a band.

| figure | layer ON | layer OFF |
|---|---|---|
| relationship surfaced | **10/10 (MET)** | **4/10 (MISSED)** |
| `spouse` surfaced | 1/10 | 0/10 |
| surfaced pairs | 20 | 14 |
| fine / coarse / wrong | 11 / 9 / 0 | 10 / 4 / 0 |
| pair precision | 1.0 | 1.0 |
| per-edge precision | 0.5167 | 0.8 |
| pronouns resolved / rank upgrades / pending | 8 / 8 / 7 | 0 / 0 / 14 |

**The relationship band is the rules' doing.** Six households have NO owner→partner edge at all
without them; the four that do are ones the extractor's own candidates supplied. The per-edge figure
is BETTER without the layer (0.8) only because the OFF arm surfaces 14 pairs to the ON arm's 20 —
pair precision is 1.0 and wrong is 0 either way. Both missed bands miss identically with the layer
off, so neither is the layer's doing.

**The layer has a measured cost, and it is recorded rather than glossed.** `update_acc` falls
0.95 → 0.925 and `growth_update_acc` 0.8875 → 0.8625; `coexist_recall`, `transfer_recall5`,
`growth_drop_points` and `update_acc_paraphrase` are unmoved. The whole difference is **seed 3**,
which went 8/8 → 6/8 on its update questions — the aggregate is a mean of per-household accuracies,
so −0.25 on one household is −0.025 overall. The mechanism is worth reading twice: the extractor
turned the day-11 contradiction span `she said she is just staying with us for now` into a
`person.lives_in` = `with us` candidate about the partner. Without the layer its `she` never resolves
and it sits pending; WITH the layer it resolves, lands, and — `person.lives_in` being single-valued
and day 11 newer than the day-4 `i live in <city>` — R3 freshness correctly supersedes the right
answer with a wrong one. **The same sentence the rules read correctly as a contradiction, the model
read as an address.** Of the 8 pronoun-bearing candidates in the corpus all 8 resolve: 6 are the
useful `person.relation_to` from `she called me love`, and 2 are this `lives_in`, in seeds 3 and 9.
It cost questions in seed 3 only — seed 9's partner-city questions were already being missed for
other reasons. So: present in 2 of 10 households, biting in 1.

### Honest scope

Synthetic seeded template households, seeds 1–10, 14 days, ORACLE gold for the controls and the
chosen extractor's contract-3 candidates for the banded run. The rules are DETERMINISTIC: they see
co-presence and household vocabulary, and one kin cue in fourteen days is not marriage — which is why
nine households surface the coarser `partner` and the log reports `spouse` separately at 1/10.
Name-earning from a vocative is deferred to MS2b. Nothing here was measured on real speech, on
household audio, or on the owner's household; MS2b is the first run that will be, and the row on the
board does not read DONE until it is.
