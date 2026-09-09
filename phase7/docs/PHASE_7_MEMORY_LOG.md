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
keeps the headroom.

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
