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
