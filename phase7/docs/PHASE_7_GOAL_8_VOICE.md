# Phase 7 Goal 8 — Ambient Voice Wearable (household voice learning) — Plan

**Status:** ACTIVE — M0a landed 2026-09-06 (`feat(phase7): goal 8 M0a - the owner-voice tooling on the Main PC (record, enroll, verify, transcribe, evaluate) with a public-speaker self-test; goal doc PHASE_7_GOAL_8_VOICE.md; audio formats ignored by git`). M0b prep landed 2026-09-06 (the owner enrolled from read plus natural speech; a same-day sanity EER, NOT the band — §6); the board row for the owner's voice reads `MEASURED BASELINE` until M0b's later-day band passes. M0b band measured 2026-09-08 — MISS, §6. Second take 2026-09-08 — PASS under the admission rule M0b-R1, §6.
**Prerequisite:** goal 8 canon `phase4/docs/ROADMAP.md:122` (done-when `:130-132`); the scope the owner set, `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §8; the board `phase7/docs/PHASE_7_PLAN.md` §0 (ten 7.8 rows).
**Sources:** those three files at `49ac1c7`; `phase6/docs/PHASE_6_GOAL_6-1_MONITORS.md` (the shape this doc mirrors); the M0a self-test run of 2026-09-06 (`%USERPROFILE%\.jarvis\voice\selftest_2026-09-06.json`, quoted in §6); the venv freeze (`%USERPROFILE%\.jarvis\voice\freeze.txt`).

---

## 1. Scope + honesty

**What this goal is, in the owner's words** (idea doc §8, recorded 2026-09-06): *"The owner's voice first. Enrolled from the Main PC headset mic and mastered — owner-versus-not speaker verification measured on held-out recordings — before any household learning."* — *"Everything is transcribed; nothing is discarded before storage."* — *"speech that is neither the owner's nor his wife's is the OWNER'S to delete, by hand, after transcription"* — *"a HOUSEHOLD PROFILE in a purpose-built store on the Main PC — who is who and to whom, the owner's style and preferences, habits and routines, topics, how each person speaks — every fact carrying its source recording, date, stated-or-inferred and a confidence."* — *"The store is to be STATE OF THE ART."* — *"Only the owner is enrolled: JARVIS is not told who the second voice is and must work out, over days of recordings, that the recurring second voice is the owner's wife and who she is to him."*

This document is the goal's plan. M0a, landed with it, is the tooling for the first two board rows — record, enroll, verify, transcribe, evaluate — proven on PUBLIC speakers; at M0a nothing about the owner had been recorded. The M0b prep of 2026-09-06 (§6) recorded and enrolled the owner's voice; every recording and embedding lives outside the repo.

### The honest signal set (real, measurable)

- **Owner-versus-not verification rate** on held-out clips: EER, and FAR/FRR at the chosen threshold, on a named clip set. M0a measured it on LibriSpeech speakers; M0b measures it on the owner.
- **Transcription** with a measured real-time factor and the GPU it ran on; the transcript JSON carries the input's sha256, the model and compute type, and the timings.
- **Raw-audio deletion proven**: the input WAV is gone after the transcript JSON is fsync'd, and the JSON says `deleted: true`. Listed before and after in every run that claims it.
- Later, each measured on its own slice: speaker clusters of non-owner speech (M1); the guess about the recurring second voice, with its confidence (M2); the household profile as a console view (M3); the digest of new learning (M4).

### The fiction we will NEVER write

- **"knows you" / "understands you" / "knows your preferences"** as claims. The tooling recognises the owner's voice against others at a measured rate and transcribes speech; every shipped slice claims only what was measured (idea doc §5, §8).
- **A verification rate presented as identity certainty.** An EER is a rate over a clip set at a threshold; a single decision is a score compared to that threshold, printed together, never a fact about who is speaking.
- **A transcript presented as a fact.** A transcript is the ASR model's output for one recording; its errors are the model's, and the profile's facts carry stated-or-inferred with a confidence.
- **A structural consent claim that the pipeline no longer makes.** The idea doc's §3 discard-before-storage rule is superseded (§8); the mitigation is raw-audio deletion after transcription plus the owner's manual purge — weaker, and written as his accepted risk.

Sources: `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §3, §5, §8; `phase4/docs/ROADMAP.md:122,130-132`; §6 below.

---

## 2. Locked decisions

1. **Venue:** a dedicated venv at `C:\Users\jluca\.jarvis\voice\venv` created from `py -3.12` (Python 3.12.6) with `--system-site-packages`, so the system torch **2.5.1+cu121** (CUDA true on the RTX 2070) and torchaudio 2.5.1+cu121 are reused. Every command runs with that venv's `python.exe`. Not the miniconda 3.13 interpreter (that venue served the embedding work); not WSL (no torch there).
2. **Code lives at `phase7/voice/`** as the package `jarvis_voice`, one CLI (`python -m jarvis_voice record|enroll|verify|transcribe|evaluate|split|selftest`), and one stdlib-only test `phase7/voice/test_voice_logic.py` (53 checks since the M0b prep) with the CI step `"Phase 7: goal 8 voice logic (Python, stdlib-only)"`. The pure modules (`evaluate`, `verify`, `enroll`, the deletion rule in `transcribe`) import nothing beyond the standard library at module level; GPU code is never imported by the test.
3. **Data layout, all outside the repo, under `%USERPROFILE%\.jarvis\voice\`:** `raw\` (recordings, deleted after transcription unless `--keep`), `enroll\` (the owner's enrollment clips + `owner.json`/`owner.npy`), `heldout\` (the owner's later clips), `public\` (the downloaded corpus and throwaway enrollments), `transcripts\` (one JSON per input), `models\` (the Hugging Face cache — `HF_HOME` is set there by `paths.set_hf_home()`), `venv\`. `JARVIS_VOICE_HOME` overrides the root for tests.
4. **Audio format:** 16 kHz mono 16-bit WAV (PCM_16).
5. **Tools kept, measured 2026-09-06** (freeze at `%USERPROFILE%\.jarvis\voice\freeze.txt`):
   - Speaker embeddings: **SpeechBrain 1.1.1 (Apache-2.0), `speechbrain/spkrec-ecapa-voxceleb`** (ECAPA-TDNN, 192-d, L2-normalised). Loaded with `local_strategy=LocalStrategy.COPY` because Windows refuses the fetcher's default symlink without a privilege (WinError 1314, measured). WeSpeaker and `pyannote/embedding` were not tried: the first candidate met its band.
   - ASR: **faster-whisper 1.2.1 (MIT) on CTranslate2 4.8.2 (MIT), model `large-v3`, `float16`, CUDA** — CTranslate2 reported 1 CUDA device and compute types `float16, float32, int8, int8_float16, int8_float32`. No fallback was needed. The hub cache runs with `HF_HUB_DISABLE_SYMLINKS=1` (the same Windows privilege, measured on the `large-v3` snapshot; huggingface_hub 0.36.0).
   - Recording: **sounddevice 0.5.6 + soundfile 0.14.0 (BSD-3)**; `record --list-devices` printed the headset as input device 1 (`Microphone (Logitech G733 Gamin, MME`).
   - Public corpus: **LibriSpeech `dev-clean`** from `https://www.openslr.org/resources/12/dev-clean.tar.gz`, 337,926,286 bytes, sha256 `76f87d090650617fca0cac8f88b9416e0ebf80350acb97b343a85fa903728ab3`, licence "Creative Commons Attribution 4.0 International License" (its `LICENSE.TXT`); 40 speakers, 2,703 FLAC utterances.
6. **Enrollment = a centroid** (mean of L2-normalised clip embeddings, itself normalised) over ≥ 60 s of speech in ≥ 3 clips, plus the per-clip embeddings; stored as `owner.json` (model, dim, clips with sha256 and durations, threshold, created, the vectors as lists) + `owner.npy`.
7. **Verification = one decision per clip:** `cos(centroid, clip) ≥ threshold → owner`; clips shorter than 2 s are REFUSED and reported; score, threshold and decision are always printed together.
8. **`transcribe` deletes the input WAV after the transcript JSON is written and fsync'd, unless `--keep`;** the JSON records the input's sha256 and `deleted: true|false`; a write failure leaves the input in place (test T6).
9. **Owner-only enrollment; no confirmation gate; everything transcribed** — the owner's rules (idea doc §8). The tooling has no "wife" concept; speaker clustering of non-owner speech is M1. **Commands are obeyed from the owner's verified voice only, and no wake word is a gate** (the operator, 2026-09-07): JARVIS is always listening — eventually 24/7 and live — learns from everyone, infers when it is being addressed (its name a cue, never a requirement), and acts for the owner alone; another voice's command is declined and logged; delegation is an explicit owner act designed later; verification says who spoke, not that it was live, so the signed channel and the allowlist remain the bound. When unsure whether it was addressed, JARVIS stays silent and logs the utterance (the console shows what it nearly acted on); it answers in text for now — speaking back is not designed (the operator, 2026-09-07).
10. **The owner records his own enrollment (M0b) in a later prompt**, from the runbook in §6. M0a's evidence is public speakers only.

11. **`split` extracts speech and packs whole runs (M0b prep, 2026-09-06).** Frames of 50 ms with RMS in dBFS; a frame is speech iff louder than −45 dBFS (strict), every loud run padded by 200 ms each side, then every interior gap shorter than 500 ms merged; the maximal speech runs are packed greedily in order into pieces, a piece closing the moment its total reaches the target (60 s for enrollment pieces, 10 s for sanity pieces) and the last open piece kept only if it reaches the minimum (20 s / 3 s); a piece is the concatenation of its runs' samples, so no word is cut at a boundary and no listening silence is embedded. The parameters are locked; `split` refuses to overwrite an existing first piece and refuses a non-16 kHz source; `--move-source-to` parks the long recording under `enroll\long\` after its pieces are written.
12. **`evaluate --neg-json <selftest JSON>`** scores the self-test's `sets.negatives` (78 LibriSpeech FLACs from 39 speakers) as the negatives; `--neg-dir` (WAV and FLAC) remains, and exactly one of the two is given. The result carries `pos_min`, `neg_max` and `neg_speakers`.
13. **Two new folders:** `heldout_sameday\` (same-day sanity pieces — never the band) and `heldout_neg\` (consented household negatives, if the owner records any — evaluated separately, never merged).
14. **Two recordings, two roles:** `owner_natural_01.wav` → enrollment pieces (target 60 s, min-keep 20 s) into `enroll\`; `owner_natural_02.wav` → same-day sanity pieces (target 10 s, min-keep 3 s) into `heldout_sameday\`; both sources parked in `enroll\long\`; the read clips stay whole in the enrollment. The enrollment is built provisionally at threshold 0.5 so `evaluate` can run, then finally at the same-day EER threshold; M0b re-chooses the threshold at its own EER point on a later day.

15. **The memory store is the pipeline's sink, and the transcript is not the record (M1, 2026-09-10).**
    `ingest` writes the design's §3.1 spine into `%USERPROFILE%\.jarvis\memory\household.sqlite` — one
    `recording` row per file, one `span` row per Whisper segment, a `cluster` row per voice, one
    `embedding` row (`owner_table='span'`) per embedded span. The transcript JSON is a by-product for the
    operator; the store is what survives the audio. **`started_at` precedence is `--started-at` > the
    `rec_YYYYmmdd_HHMMSS` filename stamp > file mtime − duration, and the SOURCE is recorded beside the
    value** (`started_at_source`), because a `said_at` derived from an inference must be distinguishable
    from one the operator stated. **The order is commit → write and fsync the JSON → delete the audio**,
    and a failure at any earlier point leaves the WAV untouched: everything else can be recomputed from
    audio and nothing can be recomputed from a deleted file. A part-written recording is compensated away
    (spans, vectors, recording row) so it lands whole or not at all.
16. **The online assignment, and τ read from the bench (M1, 2026-09-10).** The owner is checked FIRST,
    by his M0b threshold against the enrollment centroid, and only then is the nearest cluster within τ
    considered — the threshold was measured against 78 public negatives and is a far stronger
    discriminator than an unsupervised distance. τ is **never a literal in the pipeline**: it is read at
    run time from the newest `cluster_bench_*.json`, so the value in use and the evidence for it are one
    artifact, and `ingest` refuses to run if no bench exists. A span shorter than 2 s carries no
    embedding and takes the PREVIOUS span's cluster (`cluster_source = "adjacent"`); a leading short run
    takes the first assigned cluster; a recording that embedded nothing leaves its spans unassigned
    rather than inventing a voice. **A segment whose end exceeds the recording is clamped to it** —
    measured on the first real run, where Whisper returned a segment ending at 47.98 s for a 20.0 s file.
17. **The purge is the memory store's one action, and the listing never becomes a file (M1, 2026-09-10).**
    `python -m jarvis_voice clusters` PRINTS one line per cluster (id, owner flag, person, spans, days,
    first/last heard) plus the three longest spans truncated to 60 characters, so the operator can
    recognise a voice; it returns data and writes nothing. The purge itself is
    `py -3 -m jarvis_memory purge <cluster_id>` — one command per cluster, which removes that voice's
    spans, their speaker vectors and the beliefs resting only on them, and writes the audit rows. **The
    tooling has no "wife" concept and cannot create one:** a non-owner voice is a numbered cluster with a
    centroid, and personhood is earned from evidence by the store's own rule, never asserted here.

18. **The pipeline's ASR is pinned deterministic, and every run checks the pin (M1b.2, 2026-09-10).**
    `ingest` decodes with `temperature=0.0`, `beam_size=5`, `vad_filter=True` and
    `condition_on_previous_text=False`, and the settings plus the VAD parameters in force are written
    into the transcript JSON. The M0a `transcribe` command keeps its own recorded settings
    (`beam_size=5`, `vad_filter=False`) — its RTF and its transcripts were measured with them, and
    changing them would silently re-base a recorded result — so the caller names the settings and
    `ASR.run` never chooses them. **Every ingest decodes TWICE and compares:** the passes agree only
    if the segment count matches, every start and end is within 10 ms and every text is identical;
    on a disagreement **the audio is KEPT** (`kept_by_guard: true`, recorded separately from
    `kept_by_request` so a reader can tell which reason held the file), the store gets the FIRST pass
    — not a merge, which is a third segmentation neither pass produced — and the run is reported. The
    guard exists because M1 transcribed a byte-identical input twice under identical settings and got
    3 segments once and 9 the other time: every span, embedding and cluster follows the
    segmentation, and the audio is deleted at the end, so an unreproducible segmentation would make
    the spine a one-shot record with no way back. Pinning is the fix; the second pass is how a run
    finds out whether the fix held on THIS audio, while the audio still exists to try again.
19. **A store holding only throwaways may be reset; one holding a real recording never is (M1b.2,
    2026-09-10).** A reset is `household.sqlite`, `-wal` and `-shm` deleted, and it is permitted only
    after the store's contents have been READ and recorded — the row counts and every recording's
    sha256 — and found to be exactly the throwaways the milestone created. Once the store holds one
    span of household speech there is no reset: the audio is already gone, so the spine is the only
    copy, and the owner's purge (one action per cluster, decision 17) is the only removal.

20. **The TURN is the embedding unit, and its minimum length is a measurement (M1b.3, 2026-09-10).**
    Consecutive Whisper segments merge into one turn while the silence between them is at most
    **0.5 s** and the turn holds less than **10 s** of speech; a turn's samples are its segments
    concatenated, and its speech is their durations summed rather than its wall span. **The cap
    bounds MERGING, not a segment** — a turn closes once it reaches 10 s, so a single segment longer
    than that is a turn on its own, because the spine's spans stay the Whisper segments and nothing
    may split one. **Only a turn holding at least `MIN_EMBED_S` of speech is embedded, scored
    against the owner's threshold and assigned**; every segment inherits its turn's answer and
    carries its `turn_id`, and a turn under the minimum takes the PREVIOUS turn's cluster
    (`cluster_source: adjacent`) — attributed, never identified. `MIN_EMBED_S` is **never a literal
    in the pipeline**: `duration.required_min_embed_s` reads it from the newest date-named bench
    that declares the stream window rule and produced a value, and refuses in three distinct ways
    (no bench, a bench under the superseded atom rule, a bench whose reading rule found no
    qualifying duration) before any model is loaded. The unit changed because M1a.3 measured the
    owner's false-reject rate against his OWN threshold at 0.74 on one-second windows and 0.00 at
    twelve: a per-segment embedding asks the threshold a question it cannot answer, which is exactly
    how M1b opened three new clusters for the owner's own voice. **A turn's vector is written
    against its FIRST span**, because the store keys an embedding to a span and duplicating one
    measurement across a turn's spans would make any later count of speaker vectors wrong; whether
    the store grows a `turn` row is its own design's decision.

21. **The owner decision is made at TWO levels, and each only where the threshold has a measured
    footing (M1b.4, 2026-09-10).** At ingest, a turn holding at least **`OWNER_TURN_MIN_S` = 10.0 s**
    of speech is checked against the M0b threshold; every other turn of at least
    **`EMBED_MIN_S` = 2.0 s** is embedded and clustered at τ\*, its owner score recorded and **never
    acted on**. After ingest, a non-owner cluster whose centroid over at least
    **`OWNER_CLUSTER_MIN_S` = 10.0 s** of accumulated EMBEDDED speech clears the same threshold is
    MERGED into the owner's cluster — every span relabelled, the cluster row removed, one `audit`
    row (`op = merge`, `rule = people`). Each constant names its provenance in `cluster.py`:
    `EMBED_MIN_S` is IMPORTED from `verify.MIN_CLIP_S` rather than retyped; the two ten-second
    values are the length of the M0b held-out pieces the threshold was measured on, and a cluster
    centroid is a mean of embeddings like the enrollment centroid itself, so the comparison is like
    with like. **None is read from the duration table** — M1a.4 retracted the single-window minimum
    that was. A short turn scoring ABOVE the threshold is still not claimed: M1a.3 measured a 74 %
    false-reject rate on the owner's own one-second windows, so such a score is not evidence in
    either direction, and accumulated speech accrues only from turns that were actually embedded.
    Whether the cluster rule holds on real conversational speech is M1c's to report.
22. **The spine records WHEN its audio stopped existing (M1b.4, 2026-09-10).** `deleted_audio_at` is
    written on the `recording` row after the delete succeeded and never before — a timestamp on a
    file that still exists is worse than none, because the audio is the only thing that could
    contradict it and it would still be there. A recording kept by the operator or held back by the
    double-run guard leaves it NULL, and a run whose deletion raises leaves it NULL. It lives in the
    STORE and not in the transcript JSON: `finalize` writes that JSON before the deletion, and the
    JSON is deleted afterwards anyway — the spine is what survives the audio.
23. **Two conventions recorded rather than changed (M1b.4, 2026-09-10).** A turn's speaker vector is
    written against its **first span**, because the store keys an `embedding` row to a span and a
    turn is not a row of its own; writing it against every span of the turn would duplicate one
    measurement into several and make any later count of speaker vectors, or any centroid built from
    them, silently wrong. **A `turn` row in the store is DEFERRED** — it is the store design's
    decision, not the pipeline's. And a merged cluster that had already earned personhood leaves its
    `person` row in place with the audit note naming it: facts and edges may reference a person, so
    reconciling people belongs to the MS2 layer and the dangling row is visible in the audit trail
    rather than silently removed.

Sources: `%USERPROFILE%\.jarvis\voice\freeze.txt`; the M0a run (§6); `phase7/voice/jarvis_voice/*.py`; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §8.

---

## 3. Milestones

| Milestone | What | Done-when (numbers where one exists) | Board row |
|---|---|---|---|
| **M0a** (this commit) | The tooling + the public-speaker self-test | EER ≤ 5 % on ≥ 20 positives and ≥ 40 negatives from ≥ 20 speakers; ASR on the GPU at RTF < 1.0; raw-audio deletion proven; the stdlib test green in CI — **all met, §6** | a new "M0a" row above the owner's-voice row |
| **M0b** | The owner's enrollment + held-out measurement (the operator records; a later prompt runs `evaluate`) | EER ≤ 3 % on the owner's held-out clips against the public negatives (plus, if he chooses, consented clips of his wife as the hardest negative); threshold chosen at M0b's own EER point; every held-out owner clip ≥ 3 s | the first 7.8 row flips only on this band — MISSED 2026-09-08, §6; second take PASS 2026-09-08 under M0b-R1 |
| **M1** | Transcribe-everything + speaker clustering of non-owner speech + the one-action purge per cluster | clusters measured against the corpus's known speakers before any household audio; the purge is one command per cluster | the second 7.8 row |
| **M2** | The memory store — AFTER the strategist's research lands (the board's research row) — and the guess | the guess named with a confidence, over days of recordings, with only the owner enrolled | the research row, then the store-and-guess row |
| **M3** | The console profile view (designed in Claude Design, real source only) | the UI–feature-parity rule met: every rendered field has a live source | the profile-view row |
| **M4** | The digest of new learning | a learned-this-week digest from the same store | the digest row |
| **V0** | Headset command → Whisper → the receiver-as-signer → control-IN | one owner-voice command answered over control-IN | the V0 row |
| **V1 / V2** | The recorder wearable; speaker-verified commands with no wake word as a gate (addressee inference; the owner's voice verified) | per the idea doc §4 as superseded by its §8 (2026-09-07) | the V1 / V2 rows |
| **V3** | The always-on live listener — the bracelet itself, in real time; the operator's stated end state (2026-09-07), reached by iterating record → upload → learn → update the bracelet | decided last in order, designed after V2 | WANTED |

None is dated; the first-arc choice (7.1 vs 7.8) is the operator's (`PHASE_7_PLAN.md` §1).

Sources: `phase7/docs/PHASE_7_PLAN.md` §0 (the ten 7.8 rows); `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §4, §8; §4 of the prompt that landed M0a (its pre-registered bands, reproduced here).

---

## 4. Storage / state

**In the repo:** `phase7/voice/jarvis_voice/` (eleven modules: `__init__`, `__main__`, `paths`, `audio`, `speaker`, `enroll`, `verify`, `transcribe`, `evaluate`, `selftest`, `split`), `phase7/voice/test_voice_logic.py`, this document, the CI step, and the `.gitignore` block (`*.wav *.flac *.mp3 *.m4a *.ogg *.opus *.webm`, `phase7/voice/.venv/`, `phase7/voice/**/*.npy`, `phase7/voice/**/*.pt`) — proven with a throwaway `phase7/voice/x.wav` showing `!!`.

**Never in the repo:** every recording, embedding, transcript, corpus, the venv and the Hugging Face cache — all under `C:\Users\jluca\.jarvis\voice\` (`.jarvis/` is itself ignored at `.gitignore:115`). At M0a the cache holds the ECAPA model and the 2.9 GB `large-v3` snapshot; `public\` holds the 338 MB tarball and its extraction; `public\enroll_S1\` the throwaway enrollment of the pseudo-owner; `transcripts\` one JSON; `raw\` is empty (the copy was deleted). The folders are created on first use: after the M0b prep of 2026-09-06, `enroll\` holds the owner's 3 read clips, 3 natural pieces and `owner.json`/`owner.npy`, `enroll\long\` the two 600 s natural recordings, and `heldout_sameday\` the 14 sanity pieces; `heldout\` (the later-day clips the M0b band is measured on) and `heldout_neg\` (consented household negatives) do not exist yet.

**The memory store (M1, 2026-09-10):** the spine lands in `%USERPROFILE%\.jarvis\memory\household.sqlite`
(`jarvis_memory.paths.default_db()`) — outside the repo like everything else, and `*.sqlite*` is ignored.
It holds recordings, spans, clusters, persons, span vectors and the audit trail; it is the only thing that
outlives the audio.

**The clustering threshold (M1, 2026-09-10):** `%USERPROFILE%\.jarvis\voice\cluster_bench_<date>.json`
carries τ\*, the whole grid, both scenarios' numbers, the speaker ids, the linkage implementation and the
library versions. `ingest` reads τ\* from the newest of these at run time.

Sources: `.gitignore`; `phase7/voice/jarvis_voice/paths.py`; the M0a run (§6).

---

## 5. Risks

- **The manual purge is the only guard for non-household speech.** The owner's words: *"speech that is neither the owner's nor his wife's is the OWNER'S to delete, by hand, after transcription"*; *"A weaker mitigation than §3's structural rule, because it depends on the owner doing it — recorded here as the owner's accepted risk, not hidden."* The pipeline must make that purge one clean action per speaker cluster (M1).
- **A verification EER on read speech is optimistic for conversational speech.** M0a's 0.00 % is LibriSpeech audiobook speech, clean and read; the owner's held-out clips (M0b) and, later, wearable audio are the honest tests.
- **Public-speaker negatives are easier than a household member's voice.** The hardest negative — the wife — enters only if the owner chooses to record consented clips for M0b.
- **An 8 GB GPU bounds the ASR model.** `large-v3` in fp16 loaded with the GPU at 6,605 MiB used (from 2,870 MiB before), so it fits with headroom on an otherwise idle card; a busier GPU would need `int8_float16` or `distil-large-v3`, both available.
- **A headset enrollment may not transfer to a wearable mic** (different microphone, distance, noise). Record it as the M0b → V1 re-measurement; never assume the threshold carries over.
- **Windows symlink privileges** bit twice (SpeechBrain's fetcher, the hub cache); both are handled in code (`LocalStrategy.COPY`, `HF_HUB_DISABLE_SYMLINKS=1`) and would recur on any tool that assumes symlinks.

Sources: `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §8; the M0a run (§6).

---

## 6. Milestone log

### M0a — 2026-09-06 — the tooling + the public-speaker self-test — PASS on every pre-registered band

Run: `python -m jarvis_voice selftest` from the venv, 13:20:01–13:20:16 AEST (the model downloads had happened in an earlier run that stopped on the hub-cache symlink error, then fixed). Output, verbatim:

```
[corpus] https://www.openslr.org/resources/12/dev-clean.tar.gz size=337926286 sha256=76f87d090650617fca0cac8f88b9416e0ebf80350acb97b343a85fa903728ab3
[corpus] licence: LibriSpeech (c) 2014 by Vassil Panayotov |  | LibriSpeech ASR corpus is licensed under a | Creative Commons Attribution 4.0 International License.
[corpus] speakers=40 files=2703 indexed in 0.6s
[sets] S1=422 total=503.0s | enroll 5 clips 63.1s | positives 28 | negatives 78 from 39 speakers
[model] speechbrain/spkrec-ecapa-voxceleb speechbrain=1.1.1 device=cuda:0 load_s=0.3
[verify] EER=0.00% threshold=0.5179 FAR=0.00% FRR=0.00% accuracy=100.00%
[verify] pos_mean=0.8569 neg_mean=0.0947 pos_min=0.7174 neg_max=0.3184
[verify] model=speechbrain/spkrec-ecapa-voxceleb dim=192 version=1.1.1 wall_per_clip_s=0.0293 torch_vram_peak_MiB=494.1044921875
[enroll] throwaway enrollment -> C:\Users\jluca\.jarvis\voice\public\enroll_S1\S1.json
[raw] before: ['selftest_422_422-122949-0005.wav']
[raw] after:  []  copy_exists=False json_deleted_flag=True
[asr] model=large-v3 compute=float16 device=cuda faster_whisper=1.2.1 audio_s=9.77 wall_s=1.29 RTF=0.132 gpu_used_MiB before=2870 loaded=6605 after=6743
[asr] text: Probably a pessimistic suspicion with regard to the entire situation of man will find expression, perhaps a condemnation of man, together with his situation.
[selftest] written C:\Users\jluca\.jarvis\voice\selftest_2026-09-06.json
[selftest] BANDS: eer<=5%=True rtf<1=True deleted=True counts_ok=True -> PASS
```

| band | expected | measured |
|---|---|---|
| EER on the public held-out set | ≤ 5 % | **0.00 %** (threshold 0.5179; FAR 0.00 %, FRR 0.00 %; positives min 0.7174 vs negatives max 0.3184 — a 0.40 gap) |
| positives / negatives | ≥ 20 / ≥ 40 from ≥ 20 speakers | **28 / 78 from 39 speakers**; pseudo-owner S1 = speaker 422 (503.0 s available; 5 clips = 63.1 s enrolled) |
| ASR on the GPU | CUDA confirmed, VRAM printed, RTF < 1.0 | **`large-v3` float16 on CUDA, RTF 0.132** (9.77 s of audio in 1.29 s); GPU memory used 2,870 MiB before the model, 6,605 MiB loaded, 6,743 MiB after transcription (nvidia-smi, whole GPU) |
| raw-audio deletion | the `raw\` copy gone, JSON `deleted: true` | **`raw\` before `['selftest_422_422-122949-0005.wav']`, after `[]`; `deleted: true`**; the corpus FLAC untouched |
| `test_voice_logic.py` | all PASS locally and in CI | **35/35** under WSL `python3` and Windows `py -3`; the throwaway mutant (T2 expecting 0.30) fails by name, 34/35, exit 1 |
| audio in git | none | `git ls-files` audio grep empty |

Speaker-embedding cost: 0.0293 s per clip (111 clips), torch VRAM peak 494.1 MiB. The ECAPA model's first load took 9.3 s (with the copy from the hub cache); 0.3 s once cached. The transcript JSON records `torch_vram_peak_bytes` 98,707,968 — that is torch's allocator only; CTranslate2 allocates outside it, which is why the ASR figure above is the nvidia-smi delta.

Tool table (candidate → kept/dropped, version, licence): SpeechBrain ECAPA → **kept**, 1.1.1, Apache-2.0 · WeSpeaker → not tried (first candidate met the band) · `pyannote/embedding` → not tried (gated; not needed) · faster-whisper → **kept**, 1.2.1, MIT, on CTranslate2 4.8.2, MIT · openai-whisper → not tried (CTranslate2 CUDA worked) · sounddevice 0.5.6 + soundfile 0.14.0 → **kept**. Two Windows findings, both fixed in code: SpeechBrain's default `LocalStrategy.SYMLINK` and the hub cache's symlinks both fail with WinError 1314; `LocalStrategy.COPY` and `HF_HUB_DISABLE_SYMLINKS=1` are the fixes.

### M0b same-day sanity — 2026-09-06 — NOT the band

The owner recorded three 60 s read-voice clips and two 600 s natural recordings (his side of a call through the headset boom mic). The natural recordings are ~20 % speech and ~80 % near-silence — a call, mostly listening — which is why `split` extracts speech (an energy gate, padded, short gaps merged) and packs whole runs into pieces rather than gating fixed windows: at 40 % speech a fixed-window split kept zero 60 s windows from either file. Everything below was measured 2026-09-06 20:35–20:38 AEST; nothing was tuned.

Inventory of `enroll\` before the split (duration, RMS, peak, fraction of 50 ms frames below −45 dBFS, clipped samples):

```
owner_enroll_01.wav: 60.0s sr=16000 PCM_16 1ch size=1920044 rms=-35.9dBFS peak=-7.6dBFS below-45=76% clipped=0
owner_enroll_02.wav: 60.0s sr=16000 PCM_16 1ch size=1920044 rms=-32.1dBFS peak=-6.6dBFS below-45=42% clipped=0
owner_enroll_03.wav: 60.0s sr=16000 PCM_16 1ch size=1920044 rms=-32.4dBFS peak=-8.8dBFS below-45=36% clipped=0
owner_natural_01.wav: 600.0s sr=16000 PCM_16 1ch size=19200044 rms=-31.3dBFS peak=-5.6dBFS below-45=78% clipped=0
owner_natural_02.wav: 600.0s sr=16000 PCM_16 1ch size=19200044 rms=-32.8dBFS peak=-5.7dBFS below-45=81% clipped=0
```

The two splits (threshold −45 dBFS, pad 200 ms, min gap 500 ms; locked):

```
split owner_natural_01.wav --target 60 --min-keep 20 --out-dir enroll --prefix owner_natural_e --move-source-to enroll\long
piece 001: runs=24 63.1s (from 4.9s)
piece 002: runs=36 61.8s (from 131.6s)
piece 003: runs=31 60.4s (from 371.0s)
summary: total 600.0s speech 201.5s in 100 runs; pieces 3 kept 185.3s; remainder dropped 16.2s
split owner_natural_02.wav --target 10 --min-keep 3 --out-dir heldout_sameday --prefix owner_sameday --move-source-to enroll\long
summary: total 600.0s speech 176.8s in 81 runs; pieces 14 kept 176.7s; remainder dropped 0.0s   (14 pieces of 10.1–18.1 s)
```

Enrollment: the 3 read clips + the 3 natural pieces — `enrolled owner: 6 clips, 365.3s, dim=192` — built first at a provisional threshold 0.5, then finally at the same-day EER threshold **0.3461** (`enroll\owner.json` + `owner.npy`). The same-day evaluation, the 14 sanity pieces against the self-test's 78 public negatives (`evaluate --pos-dir heldout_sameday --neg-json selftest_2026-09-06.json`):

```
{
 "n_pos": 14,
 "n_neg": 78,
 "neg_speakers": 39,
 "eer": 0.0,
 "threshold": 0.34614428192992575,
 "far_at_threshold": 0.0,
 "frr_at_threshold": 0.0,
 "accuracy_at_threshold": 1.0,
 "pos_mean": 0.7376042379371902,
 "neg_mean": 0.06493026456725112,
 "pos_min": 0.45631370096207474,
 "neg_max": 0.2359748628977767
}
  POS owner_sameday_001.wav: 0.6950 (12.8s)
  POS owner_sameday_002.wav: 0.7317 (10.3s)
  POS owner_sameday_003.wav: 0.7622 (12.5s)
  POS owner_sameday_004.wav: 0.7664 (10.1s)
  POS owner_sameday_005.wav: 0.8297 (13.1s)
  POS owner_sameday_006.wav: 0.8377 (10.8s)
  POS owner_sameday_007.wav: 0.7388 (12.0s)
  POS owner_sameday_008.wav: 0.5887 (18.1s)
  POS owner_sameday_009.wav: 0.7398 (10.9s)
  POS owner_sameday_010.wav: 0.8027 (11.1s)
  POS owner_sameday_011.wav: 0.4563 (12.6s)
  POS owner_sameday_012.wav: 0.8359 (15.8s)
  POS owner_sameday_013.wav: 0.7941 (16.5s)
  POS owner_sameday_014.wav: 0.7476 (10.2s)
```
(The 78 NEG scores, one per LibriSpeech FLAC from 39 speakers, range −0.0872 … 0.2360; all are in the M0b-prep report.)

Smoke at the stored threshold: `owner_sameday_001.wav` → score 0.6950, owner **True**; the first public negative `84-121123-0001.flac` → score 0.1071, owner **False**; neither refused.

**A same-day number is optimistic by construction — same session, same mic state; the M0b band (EER ≤ 3 %) is measured only on a later-day recording.** The weakest positive is piece 011 at 0.4563 (piece 008 at 0.5887 next); the gap to the strongest negative (0.2360) is 0.22 — narrower than the public self-test's 0.40, as natural speech against a read-speech-heavy centroid would predict. No consented household negatives exist (`heldout_neg\` was not created), so that harder comparison has not been made.

Sources: the M0b-prep run of 2026-09-06 (`REPORT-VOICE-M0B-PREP-V2.md`); `enroll\owner.json`; `selftest_2026-09-06.json` (`sets.negatives`).

### M0b — 2026-09-08 — the later-day band — MISS

The operator recorded one 600 s natural take on the headset (device 1) on 2026-09-08, two days after the enrollment,
with his wife in the room — `record --seconds 600 --device 1 --out …\enroll\owner_natural_03.wav` — moved it out of
`enroll\` before anything else ran (a bare `enroll` globs that folder), then split it with the runbook's command
(`--target 10 --min-keep 3`, absolute `--out-dir` and `--move-source-to` — they are CWD-relative):

```
piece 001: runs=6 10.2s (from 2.8s)
piece 002: runs=7 10.1s (from 25.7s)
piece 003: runs=7 10.4s (from 68.8s)
piece 004: runs=9 10.3s (from 98.6s)
piece 005: runs=8 10.7s (from 187.3s)
piece 006: runs=12 10.1s (from 229.0s)
piece 007: runs=4 10.8s (from 306.1s)
piece 008: runs=5 10.2s (from 324.9s)
summary: total 600.0s speech 85.4s in 62 runs; pieces 8 kept 82.8s; remainder dropped 2.6s
```

He listened to all 8 pieces: his voice only, none of hers — so `heldout_neg\` stays empty and the negatives are the
78 public LibriSpeech clips from 39 speakers of `selftest_2026-09-06.json`. The pieces were transcribed with `--keep`
the same day (Whisper large-v3; two pieces yielded no words; the transcripts stay under `transcripts\`, the audio
was kept).

`evaluate --pos-dir heldout --neg-json selftest_2026-09-06.json` (2026-09-08, `m0b_eval_2026-09-08.txt`):

```
{
 "n_pos": 8,
 "n_neg": 78,
 "neg_speakers": 39,
 "eer": 0.25,
 "threshold": 0.10038628450437456,
 "far_at_threshold": 0.24358974358974358,
 "frr_at_threshold": 0.25,
 "accuracy_at_threshold": 0.7558139534883721,
 "pos_mean": 0.3390570995533304,
 "neg_mean": 0.06493026456725112,
 "pos_min": -0.07863157343364274,
 "neg_max": 0.2359748628977767
}
  POS owner_heldout_001.wav: 0.6100 (10.2s)
  POS owner_heldout_002.wav: 0.5339 (10.1s)
  POS owner_heldout_003.wav: 0.3565 (10.3s)
  POS owner_heldout_004.wav: -0.0786 (10.3s)
  POS owner_heldout_005.wav: -0.0385 (10.7s)
  POS owner_heldout_006.wav: 0.1026 (10.1s)
  POS owner_heldout_007.wav: 0.6590 (10.8s)
  POS owner_heldout_008.wav: 0.5676 (10.2s)
NEG: 78 clips, scores -0.0872 … 0.2360
```

| band | expected | measured |
|---|---|---|
| EER on the later-day held-out set vs the public negatives | ≤ 3 % | **25.00 %** (threshold 0.1004; FAR 24.36 %, FRR 25.00 %; positives min −0.0786 vs negatives max 0.2360 — an overlap of 0.3146, not a gap) |
| every held-out owner clip ≥ 3 s | yes | 8 clips, 10.1–10.8 s |
| the threshold chosen at M0b's own EER point | stored in `enroll\owner.json` | not stored, 0.3461 kept |

At the previously stored same-day threshold 0.3461, 3 of the 8 pieces score below it (reported, not a band).

**Stated limits.** Eight positives: FRR moves in steps of 12.5 %, so this band is met only by clean separation. One
recording, one room, two days after enrollment; the same-day 0.00 % of 2026-09-06 stays the baseline. A further
later-day take adds pieces under a new `--prefix` (`split` refuses to overwrite `owner_heldout_001.wav`).

Sources: `%USERPROFILE%\.jarvis\voice\m0b_eval_2026-09-08.txt`; `heldout\long\owner_natural_03.wav` (19,200,044 B); `enroll\owner.json`; `transcripts\owner_heldout_001..008.json`.

### M0b, second take — 2026-09-08 — the admission rule pre-registered, the band re-measured — PASS

**The rule, pre-registered before the take was split (M0b-R1, 2026-09-08 ~19:10):** a held-out piece counts for the band
only if the transcriber returns at least one word for it; pieces without words are kept under `heldout\no_words\` and
reported beside the band. The reason is the first take's F4, measured not inferred: its three pieces below the stored
threshold — 004 (−0.0786), 005 (−0.0385), 006 (0.1026) — were exactly the three with no transcribed words (0, 0 and 11
characters), while the five with speech scored 0.3565–0.6590, all above the threshold and clear of every public negative.
The energy gate admits sound; the band measures verification on speech. The operator confirmed every admitted piece by
ear before the number (his words below). The first take's MISS stands.

The take: 600 s on the headset (device 1), 2026-09-08 18:52, moved out of `enroll\` before anything else ran; split with
the runbook's command under the prefix `owner_heldout2`:

```
piece 001: runs=10 10.1s (from 1.1s)
piece 002: runs=11 10.2s (from 66.2s)
piece 003: runs=8 10.4s (from 155.5s)
piece 004: runs=8 10.1s (from 226.4s)
piece 005: runs=5 17.4s (from 303.1s)
piece 006: runs=10 11.9s (from 344.1s)
piece 007: runs=9 10.3s (from 390.5s)
piece 008: runs=5 11.4s (from 433.9s)
piece 009: runs=5 12.3s (from 479.8s)
piece 010: runs=5 10.3s (from 505.6s)
piece 011: runs=6 11.1s (from 525.8s)
piece 012: runs=4 10.5s (from 559.7s)
piece 013: runs=4 11.5s (from 577.0s)
summary: total 600.0s speech 147.3s in 90 runs; pieces 13 kept 147.3s; remainder dropped 0.0s
```

147.3 s of speech in 13 pieces, against the first take's 85.4 s in 8. Transcripts (`--keep`, faster-whisper large-v3;
every line reported `deleted=False`) and admission:

| piece | s | chars | first words | admitted |
|---|---|---|---|---|
| 001 | 10.1 | 44 | the viper holy fuck but they're so fast yeah | yes |
| 002 | 10.2 | 58 | I don't know, I kept like leaving a joint and it's cooked. | yes |
| 003 | 10.4 | 70 | uh what button is fucking emma oh my god like i just can't f… | yes |
| 004 | 10.1 | 93 | why are you why are we so slow like you can't run them at al… | yes |
| 005 | 17.4 | 120 | We should definitely, I'm going to go around and look for li… | yes |
| 006 | 11.8 | 44 | I'm just searching just for like attachments | yes |
| 007 | 10.3 | 93 | anything up here no hello mother yeah you saw what i done oh… | yes |
| 008 | 11.4 | 76 | Oh, faster reloading. I'm grabbing a pill. I'm at the next s… | yes |
| 009 | 12.3 | 160 | m27 oh now that might be i can't do that while holding this … | yes |
| 010 | 10.3 | 142 | Do what? Oh, you upgrade your pistol. Oh, it actually gives … | yes |
| 011 | 11.1 | 125 | I'm gonna go in and search first. Oh, I got a skin on my gun… | yes |
| 012 | 10.5 | 102 | one time wonder one time hit wonder yeah one hit time wonder… | yes |
| 013 | 11.4 | 108 | That's what it says. Oh, the pee-pee bison. The pee-pee biso… | yes |

**All 13 carried words, so M0b-R1 admitted every one and `heldout\no_words\` was never created.**

The operator's confirmation (verbatim): "All the transcribed words are mine. My wife is faintly audible in the
background in some pieces (I'm not sure which), never as the main voice and nothing she said was transcribed. Keep all
13 as positives and record that." Moves: **none** — neither branch applied, so `heldout_neg\` stays empty and
`heldout\mixed\` was not created. **That answer is a stated limit, not a clean-room claim: the positives may carry a
faint second household voice in the background. It is recorded as his judgement, in his words, and it is what the band
was measured on.**

`evaluate --pos-dir heldout --neg-json selftest_2026-09-06.json` (`m0b2_eval_2026-09-08.txt`):

```
{
 "n_pos": 13,
 "n_neg": 78,
 "neg_speakers": 39,
 "eer": 0.0,
 "threshold": 0.358503175300161,
 "far_at_threshold": 0.0,
 "frr_at_threshold": 0.0,
 "accuracy_at_threshold": 1.0,
 "pos_mean": 0.5775710941230787,
 "neg_mean": 0.06493026456725112,
 "pos_min": 0.48103148770254534,
 "neg_max": 0.2359748628977767
}
  POS owner_heldout2_001.wav: 0.6246 (10.1s)
  POS owner_heldout2_002.wav: 0.5959 (10.2s)
  POS owner_heldout2_003.wav: 0.5663 (10.4s)
  POS owner_heldout2_004.wav: 0.6552 (10.1s)
  POS owner_heldout2_005.wav: 0.6467 (17.4s)
  POS owner_heldout2_006.wav: 0.4830 (11.8s)
  POS owner_heldout2_007.wav: 0.4810 (10.3s)
  POS owner_heldout2_008.wav: 0.5119 (11.4s)
  POS owner_heldout2_009.wav: 0.6451 (12.3s)
  POS owner_heldout2_010.wav: 0.6573 (10.3s)
  POS owner_heldout2_011.wav: 0.6314 (11.1s)
  POS owner_heldout2_012.wav: 0.5064 (10.5s)
  POS owner_heldout2_013.wav: 0.5035 (11.4s)
NEG: 78 clips from 39 speakers, scores -0.0872 … 0.2360
```

| band | expected | measured |
|---|---|---|
| EER on the admitted later-day pieces vs the public negatives | ≤ 3 % | **0.00 %** (threshold 0.3585; FAR 0.00 %, FRR 0.00 %; positives min 0.4810 vs negatives max 0.2360 — a 0.2450 gap, every positive above every negative) |
| every admitted piece ≥ 3 s | yes | 13 pieces, 10.1–17.4 s |
| the threshold chosen at this measurement's own EER point | stored on PASS | stored `0.358503175300161` (was 0.3461); the re-run reproduced every score exactly, and `owner.npy` is md5-unchanged — the same six clips give the same centroid, only the threshold moved |

Beside the band, reported: **0** pieces without words (so nothing was set aside); **0** of the 13 below the previously
stored 0.3461 (the lowest is 0.4810); no household-negative evaluation — `heldout_neg\` is empty by the operator's
answer. The first take under M0b-R1 (`m0b_first_take_rule_2026-09-08.txt`, computed with the deployed
`jarvis_voice.evaluate.eer`): **the rule as written admits SIX of its eight pieces, not five** — piece 006 transcribes
as "What? What?", 11 characters, which is not empty — and on those six the first take scores **EER 16.67 %**, still a
miss. On the five pieces with more than a fragment (001, 002, 003, 007, 008) it scores 0.00 % at threshold 0.29625.
Both are reported; neither is a band, and **the first take's recorded MISS at EER 25.00 % stands.** The rule does not
rescue it, which is the honest reading and the one that matters: M0b-R1 was pre-registered on new data, not fitted to
a past failure.

**Stated limits.** Thirteen positives, so FRR moves in steps of 1/13 = 7.69 %; a 0.00 % EER means only that no
positive fell below any negative on this set, not that the rate is zero. One room, one microphone, one day, one
speaker style (a gaming session, conversational and profane). The rule counts words, not speech quality, and would
admit singing (the first take's piece 003 was a sung line at 0.3565). The household negative set is **empty** — the
harder comparison, the owner against his wife, still has not been made. The operator reports her faintly audible in the
background of some admitted pieces.

Sources: `%USERPROFILE%\.jarvis\voice\m0b2_eval_2026-09-08.txt` and `m0b2_eval_2026-09-08_rerun.txt`;
`m0b_first_take_rule_2026-09-08.txt`; `heldout\long\owner_natural_04.wav` (19,200,044 B);
`transcripts\owner_heldout2_001..013.json`; `enroll\owner.json`; the first take kept at
`heldout\first_take_2026-09-08\`.

### The M0b runbook — the owner's enrollment (a later prompt; the operator records)

What "PASS" means: **EER ≤ 3 %** on the owner's held-out clips against the public negatives, with the threshold chosen at M0b's own EER point, and every held-out owner clip ≥ 3 s. The board's first 7.8 row (the owner's voice) flips to DONE only on that band, in that prompt's commit B.

1. **Enrollment, day 1 — DONE 2026-09-06** (3 × 60 s read clips + 3 natural pieces; `enroll\owner.json` at threshold 0.3461). The command, for a re-enrollment: ≥ 3 × 60 s on the headset (device 1 today: `record --list-devices` to confirm), reading text, at different times of day, into `enroll\`:
   `python -m jarvis_voice record --seconds 60 --device 1 --out %USERPROFILE%\.jarvis\voice\enroll\owner_enroll_01.wav` (repeat `_02`, `_03`, …).
2. **Held-out, a LATER day** — either ≥ 10 × 10 s clips into `heldout\`
   (`python -m jarvis_voice record --seconds 10 --device 1 --out %USERPROFILE%\.jarvis\voice\heldout\owner_heldout_01.wav`, repeated), or ONE long natural recording (≥ 10 min, e.g. his side of a call) split into pieces with
   `python -m jarvis_voice split <wav> --target 10 --min-keep 3 --out-dir %USERPROFILE%\.jarvis\voice\heldout --prefix owner_heldout --move-source-to %USERPROFILE%\.jarvis\voice\heldout\long`. — **DONE 2026-09-08** (the long-recording path: one 600 s take, 8 pieces, 82.8 s). Second take 2026-09-08 under M0b-R1, prefix `owner_heldout2`.
3. **Optional hardest negative** — a few consented clips of his wife into a `heldout_neg\` folder of the operator's choosing. — none recorded; his listening found none of her voice in the 8 pieces (2026-09-08).
4. **The later prompt** runs `python -m jarvis_voice enroll --threshold <t>` (the threshold from M0b's own EER sweep, computed by `evaluate` first with a provisional value) and `python -m jarvis_voice evaluate --pos-dir heldout --neg-dir <public negatives + the optional wife clips>`, records EER / threshold / FAR / FRR / counts, and flips the row only on PASS. — **DONE 2026-09-08**, the section above.
5. Nothing under `enroll\` or `heldout\` ever enters the repo (`.gitignore`); the transcripts of any owner recording stay under `transcripts\`.

Sources: the run above; `%USERPROFILE%\.jarvis\voice\selftest_2026-09-06.json`; `%USERPROFILE%\.jarvis\voice\transcripts\selftest_422_422-122949-0005.json`; `%USERPROFILE%\.jarvis\voice\freeze.txt`; `phase7/voice/test_voice_logic.py`.

### M1a — 2026-09-10 — the clustering threshold on the public corpus — BAND MET

**τ\* = 0.56** (average-linkage cosine distance), chosen by the pre-registered rule on LibriSpeech
dev-clean **before any household audio was clustered**. At τ\*: **purity 1.0000, completeness
0.9967**, 11 clusters over 300 clips. The band — purity ≥ 0.95 AND completeness ≥ 0.90 — is **MET**.

Measured before the numbers existed and unchanged since: the grid (0.20…0.60 step 0.02, 21 values),
the metrics, the argmax-of-the-product rule, the tie direction, and both scenarios' composition.

**Venue:** LibriSpeech dev-clean at `%USERPROFILE%\.jarvis\voice\public\LibriSpeech\dev-clean`;
ECAPA `speechbrain/spkrec-ecapa-voxceleb` (192-d) on `cuda:0`, speechbrain 1.1.1, load 0.61 s;
linkage `scipy.cluster.hierarchy` (scipy 1.16.3 — the numpy fallback exists and did not run, and
which one ran is recorded in the JSON); 338 embeddings computed in total, 17.9 s wall for the whole
bench. Output: `%USERPROFILE%\.jarvis\voice\cluster_bench_2026-09-10.json`.

#### Scenario A — the threshold (balanced, offline agglomerative)

The ten dev-clean speakers with the most utterances ≥ 2 s, **excluding 422** (S1, the self-test's
pseudo-owner, which scenario B then uses as the owner — measuring the threshold on the same speaker
would fit it to the thing it is meant to judge): `3752, 6313, 1462, 2277, 3081, 2428, 5694, 777,
5895, 6241`. The first 30 utterances of each in file order → 300 clips, embedded in 9.6 s.

| τ | purity | completeness | product | clusters |
|---|---|---|---|---|
| 0.20 | 1.0000 | 0.3167 | 0.3167 | 190 |
| 0.22 | 1.0000 | 0.3600 | 0.3600 | 166 |
| 0.24 | 1.0000 | 0.4300 | 0.4300 | 143 |
| 0.26 | 1.0000 | 0.5300 | 0.5300 | 123 |
| 0.28 | 1.0000 | 0.5933 | 0.5933 | 107 |
| 0.30 | 1.0000 | 0.6467 | 0.6467 | 92 |
| 0.32 | 1.0000 | 0.6900 | 0.6900 | 76 |
| 0.34 | 1.0000 | 0.7767 | 0.7767 | 56 |
| 0.36 | 1.0000 | 0.8067 | 0.8067 | 46 |
| 0.38 | 1.0000 | 0.8600 | 0.8600 | 36 |
| 0.40 | 1.0000 | 0.9233 | 0.9233 | 30 |
| 0.42 | 1.0000 | 0.9433 | 0.9433 | 26 |
| 0.44 | 1.0000 | 0.9633 | 0.9633 | 21 |
| 0.46 | 1.0000 | 0.9667 | 0.9667 | 20 |
| 0.48 | 1.0000 | 0.9733 | 0.9733 | 18 |
| 0.50 | 1.0000 | 0.9800 | 0.9800 | 16 |
| 0.52 | 1.0000 | 0.9833 | 0.9833 | 14 |
| 0.54 | 1.0000 | 0.9933 | 0.9933 | 12 |
| **0.56** | **1.0000** | **0.9967** | **0.9967** | **11 ← τ\*** |
| 0.58 | 1.0000 | 0.9967 | 0.9967 | 11 |
| 0.60 | 1.0000 | 0.9967 | 0.9967 | 11 |

**Purity is 1.0000 at every τ on the grid** — not one cluster ever mixed two speakers, even at 0.60.
The grid therefore measures only how far a speaker is SPLIT, and completeness rises monotonically
with τ. Two things follow, and both are worth saying rather than leaving implied:

1. **The tie rule fired and it decided τ\*.** 0.56, 0.58 and 0.60 all score 0.9967; the
   pre-registered direction (ties to the SMALLER τ) picked 0.56. The direction was chosen before
   any number for a reason that holds here: a smaller τ splits rather than merges, a split cluster
   costs the operator a second purge action, and a merged one has already put two people's speech
   under a single purge. The tie fails toward the recoverable error.
2. **τ\* sits at the top of the measured grid's plateau, not inside it.** Purity never degraded, so
   the grid never showed the merge failure the upper bound exists to catch. On conversational
   household speech it will: this is read speech, one speaker per file, clean channel.

**Cluster count 11 against an expected 10.** Completeness 0.9967 = 299/300, so exactly ONE utterance
of one speaker sits alone in an eleventh cluster. That is the split direction, and it is the
harmless one.

#### Scenario B — the household SHAPE, online, at τ\*

S1 (422) as the owner through the self-test's own enrollment (`public\enroll_S1\S1.json`, 5 clips,
threshold **0.5178911126741584**) with its 28 `sets.positives` as the owner's spans; the largest of
scenario A's speakers as a frequent second voice (3752, 40 utterances), the next as a visitor (6313,
6), and three more as strangers (1462, 2277, 3081 — 2 each). 80 spans in a seed-1 shuffle, fed one
at a time to the SAME `assign` rule the pipeline uses, 1.8 s.

| what | value | expectation |
|---|---|---|
| owner recall | **1.0000** (28/28) | ≥ 0.95, from M0a's EER 0.00 % |
| owner FAR | **0.0000** (0/52) | 0 |
| non-owner clusters | 6 | 5 |
| non-owner purity | **1.0000** | ≥ 0.95 |
| non-owner completeness | 0.9808 | — |

Every owner span was claimed by the owner branch and **no other speaker's span ever was**, which is
the property the whole design rests on: the owner is identified by a threshold measured against 78
public negatives, never by proximity to a drifting cluster. Completeness 0.9808 = 51/52 — again one
utterance alone, giving the sixth cluster against an expected five. Same split, same direction,
harmless.

#### Limits — these numbers are an upper bound

Stated before the run and unchanged by it. Whisper's segments are **not turn-aligned**: a real span
can contain two speakers, which is a failure mode this corpus cannot exhibit at all because every
file holds one speaker reading. ECAPA on conversational fragments is weaker than on read speech.
Spans under 2 s carry no embedding and inherit a cluster by adjacency. The visitor/stranger split is
the corpus's shape, not the household's. **Real-data numbers are reported and set no band** (M1c).

Tests: `test_voice_logic.py` 53 → **58 checks** (T10a–T10e; the prompt's T7a–T7d labels were already
taken by the `split` tests, so this block is T10). Three mutants, control green first, each failing
by name and restored from a byte-copy: `assign` checking clusters before the owner → T10c;
`choose_tau` tying to the larger τ → T10b; `update_centroid` not re-normalised → T10d.

### M1b — 2026-09-10 — the pipeline, proven on throwaways — the order holds, the clustering MISSES on short spans

`ingest` runs end to end: Whisper → an ECAPA embedding per span ≥ 2 s → the online assignment at τ\* →
the spine written to `household.sqlite` → the transcript JSON fsync'd → **the audio deleted, and only
then**. Two throwaways went through it (the owner's own read speech, and a public-corpus stranger);
both WAVs are gone, both stores committed, and the purge removed a whole voice in one command.

**What PASSED, and it is the half that cannot be undone if wrong:**

| property | evidence |
|---|---|
| the audio is deleted only after the commit AND the JSON | both runs `store_committed: true`, `deleted: true`, no WAV left in `raw\` |
| a failure before the commit leaves the audio | T11c: a store whose `promote_persons` raises → the WAV survives, 0 spans, 0 recordings |
| a failure writing the JSON leaves the audio | T11c: a writer that raises → the WAV survives |
| τ comes from the measurement, never a literal | `tau : 0.56 (from cluster_bench)` printed by the run; `ingest` refuses to start with no bench JSON |
| the purge is ONE action per cluster | `py -3 -m jarvis_memory purge 5 --yes` → spans 10 → 9, span embeddings 4 → 3, audit 0 → 1 (`op=purge`, `rule=R7`) |
| a purged voice's speaker vector goes with it | the embedding count fell with the span; asserted in the memory suite as T39 |
| the listing never becomes a file | T11e; `clusters` returns data and the CLI prints it |

**Throwaway 1 — the owner** (the first 20 s of `owner_enroll_01.wav`, copied; sha256
`4ee13a0a3b7ff0fc…`): 20.0 s, wall 14.1 s, RTF 0.706, **9 spans, 3 embedded, owner 0, new 3,
adjacent 6**. **Throwaway 2 — a stranger** (LibriSpeech speaker 1272, 20 s concatenated; sha256
`e3829a7410ebeb18…`): 1 span, 1 embedded, 1 new cluster. Store after both: 2 recordings, 10 spans,
5 clusters, 1 person (the owner); after purging the stranger, 9 spans and 3 span vectors.

#### MISS — the owner's own audio did not reach his own threshold, and the cause is span length

§3.5 expected every embedded span of the owner throwaway to come back `cluster_source = "owner"` and
no other cluster to appear. **Neither held.** The three embedded spans scored **0.1113, 0.3232 and
0.2598** against the owner centroid, all below his M0b threshold of **0.358503**, so each opened a
new cluster. The stranger, on 20 unbroken seconds, scored **0.1992** — *higher than the owner's
5.66 s span*.

The measurement is the explanation. In an earlier run of the **same file** Whisper produced three
9-second segments and the first two scored **0.4423 and 0.4374 — comfortably the owner**. The
threshold was measured at M0b on pieces of natural speech several seconds long and up; ECAPA on a
1–3 s fragment simply does not reach it. §0 predicted the direction ("ECAPA on conversational
fragments is weaker than on read speech, so the corpus numbers are an upper bound") and this is
stronger than predicted: on the owner's own clean read audio, the identification fails when the
segmentation is fine-grained.

**Nothing was tuned in response.** The threshold is M0b's measured value and τ\* is M1a's; changing
either after seeing this would be fitting them to a throwaway. It is recorded as the finding it is,
and it is the first thing M1c's real recording will test.

#### A reproducibility finding: the same audio segmented two different ways

The two runs above used a **byte-identical** input (`input_sha256` equal, verified) and the same
model, `beam_size=5`, `vad_filter=False` — and produced **3 segments** the first time and **9** the
second. Everything downstream follows the segmentation: how many spans exist, which get embedded,
what they score, how many clusters appear. A game was using the GPU throughout, so float16 CTranslate2
kernel selection under varying free VRAM is the likely cause, but the cause is not established here.

It matters more than a normal flake would, because **the audio is deleted after ingest**: a
segmentation that cannot be reproduced makes the spine a one-shot record. Re-running is not a
recovery path. Carried as an open item, not repaired.

#### A defect found by the first real run, and fixed

Whisper returned a final segment ending at **47.98 s for a 20.0 s file** (2 words over a near-silent
tail). Unclamped, that end time entered the spine as fact — a span the store believes lasted 30 s
inside a 20 s recording, with `said_at` derived from it, and no audio left to contradict it.
`ingest` now clamps a segment's end to the recording's own duration; T11d pins it with a segment that
deliberately overruns.

#### Tests

`test_voice_logic.py` 58 → **63 checks** (T11a–T11e; the prompt's T8a–T8e labels were already taken
by `pack_runs`). `test_memory_logic.py` 262 → **264** (T39, T39b: a span's speaker vector round-trips
byte-exact and the purge takes it; a cluster centroid persists in the same encoding). Two mutants,
control green first, each failing by name and restored from a byte-copy: deleting the audio before
the commit → T11c; adjacency taking the NEXT span's cluster instead of the previous → T11b.

The suite runs with no GPU, no model and no audio decoder — the ASR, the embedder, the store, the
JSON writer and the WAV reader are all injected — which is what lets CI exercise the deletion order,
the one rule whose failure cannot be undone.

### M1a.2 — 2026-09-10 — the owner's threshold by span duration — STOP: the windows never varied

**The reading rule returned `MIN_EMBED_S` = 1.0 s, and that value must not be used.** The measurement
ran exactly as pre-registered and its output is an artefact of the window rule meeting this data:
every duration on the grid measured the SAME ~10-second windows, so the grid varied nothing. Adopting
1.0 s would tell the pipeline that one-second spans clear the owner's threshold, which is the precise
opposite of the M1 finding that motivated this milestone. **The table is reported; the value is not
adopted; the window rule is the strategist's to rule on.**

**Venue:** the owner's 13 M0b-admitted held-out pieces (`heldout\owner_heldout2_001..013.wav`)
against the self-test's 78 public negatives, speech-packed with the `split` mask (−45 dBFS, 200 ms
pad, 500 ms gap), ECAPA `speechbrain/spkrec-ecapa-voxceleb` on `cuda:0` (load 0.31 s), scored against
`enroll\owner.json`'s centroid. The stored threshold **0.358503175300161 was measured against and
never moved**; nothing under `enroll\` was touched. Output:
`%USERPROFILE%\.jarvis\voice\duration_bench_2026-09-10.json`.

| D (s) | n_pos | n_neg | EER | EER threshold | FAR @ 0.3585 | FRR @ 0.3585 | pos_min | neg_max | band |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 13 | 79 | 0.0000 | 0.3642 | 0.0000 | 0.0000 | 0.4810 | 0.2475 | PASS |
| 2 | 13 | 79 | 0.0000 | 0.3642 | 0.0000 | 0.0000 | 0.4810 | 0.2475 | PASS |
| 3 | 13 | 73 | 0.0000 | 0.3642 | 0.0000 | 0.0000 | 0.4810 | 0.2475 | PASS |
| 5 | 13 | 56 | 0.0000 | 0.3454 | 0.0000 | 0.0000 | 0.4810 | 0.2098 | PASS |
| 8 | 13 | 44 | 0.0000 | 0.3454 | 0.0000 | 0.0000 | 0.4810 | 0.2098 | PASS |
| 12 | 2 | 20 | 0.0000 | 0.4097 | 0.0000 | 0.0000 | 0.6451 | 0.1744 | PASS |

#### Why the table cannot be read as a duration curve

`n_pos` is 13 for every D up to 8 — one window per FILE, not per duration — and `pos_min`, `neg_max`
and the EER threshold are identical across D = 1, 2 and 3. That is the signature of a measurement
whose independent variable never moved. The cause is in the data, and it was measured rather than
assumed:

```
file                       runs  speech_s   run durations
owner_heldout2_001.wav        1      10.1   [10.1]
owner_heldout2_005.wav        1      17.4   [17.4]
owner_heldout2_009.wav        1      12.3   [12.3]
…  all thirteen: exactly ONE run, 10.1–17.4 s
windows per D for file 001:  {1: 1, 2: 1, 3: 1, 5: 1, 8: 1, 12: 0}
```

Each held-out piece is a single continuous speech run — the 500 ms gap merge joins the whole piece —
and `speech_windows` treats a run as an ATOM, so the first run alone already satisfies every D ≤ its
length. A "1-second window" was therefore a 10-second window. The falling `n_neg` (79 → 73 → 56 → 44
→ 20) and the collapse of `n_pos` to 2 at D = 12 are whole FILES dropping out as D exceeds their
speech length, not windows getting shorter.

**The atom rule is the pre-registered one, and its own test says so.** T12a fixes, before any number,
that three runs of 2, 3 and 4 s at D = 2 give **three** windows `[[0], [1], [2]]`. Cutting the
concatenated speech stream at exactly D instead would give **four** (9 s ÷ 2 s, tail dropped). The
implementation reproduces T12a exactly; it is the rule that degenerates when a file is one run, and
changing the rule now — after seeing the numbers it produced — would be fitting the measurement to
its own result. So it was not changed.

#### What is still true, and what is still unknown

The table does establish one thing worth keeping: **on ~10-second speech-packed windows the stored
threshold separates the owner from 78 public negatives perfectly** — `pos_min` 0.4810 against
`neg_max` 0.2475, a margin of 0.23, EER 0.0000. That is M0b's result reproduced on a different
windowing, and it says the threshold is sound for the length it was measured at.

What remains unmeasured is the thing this milestone needed: **how the score falls as the window
shortens.** M1's evidence stands unexplained by this table — 0.4423 and 0.4374 on nine-second Whisper
segments, and 0.1113 / 0.3232 / 0.2598 on segments of 5.66 / 3.00 / 2.00 s from the same voice.

Two candidate repairs, neither taken here because both change a pre-registered rule and the choice is
the strategist's: (a) cut the concatenated speech stream at exactly D, splitting runs, which makes
the grid real but contradicts T12a's D = 2 expectation; (b) keep the atom rule and cut each held-out
piece into shorter FILES first, as `split` already does for enrollment, so the runs themselves are of
the length under test.

Tests: `test_voice_logic.py` 64 → **66 checks** (T12a the window rule, T12b the reading rule). One
mutant, control green first, failing by name and restored from a byte-copy: the rule taking the
LARGEST qualifying duration → T12b.




### M1a.3 — 2026-09-10 — the window rule re-registered as a stream cut; the threshold by duration

**`MIN_EMBED_S` = 12.0 s by the pre-registered rule — and the number needs its denominator read
beside it: the qualifying row holds TWO positive windows.** The table now varies with D, which is
what M1a.2 stopped for, so the re-registration did its job; what it produced is a duration curve
whose top end is measured on almost nothing. Both facts are recorded here, and neither band, rule nor
threshold was moved to make the result nicer.

**The rule, re-registered before the re-run** (`PROMPT-VOICE-M1C-PREP-2.md` §0, the coder's option
(a)): `speech_windows_stream` cuts the CONCATENATED speech stream at exactly D seconds, so a run may
be split across two windows and a window may straddle a removed pause. The atom rule
(`speech_windows`) is untouched and stays where it belongs — packing enrollment pieces in `split`,
where cutting a run would cut a word out of the owner's voice — and its test T12a is untouched.
**The limit, stated rather than hidden: a stream cut can fall inside a word, which can only lower a
score, so a minimum read from this table is conservative in the safe direction.**

**Venue:** identical to M1a.2 — the owner's 13 M0b-admitted held-out pieces
(`heldout\owner_heldout2_001..013.wav`) against the self-test's 78 public negatives, the same
`split` mask (−45 dBFS, 200 ms pad, 500 ms gap), ECAPA `speechbrain/spkrec-ecapa-voxceleb` on
`cuda:0` (load 0.42 s), scored against `enroll\owner.json`'s centroid, same grid, same reading rule.
The stored threshold **0.358503175300161 was measured against and never moved**; nothing under
`enroll\` was touched; τ\* 0.56 was not read or written. 31.6 s wall. Output:
`%USERPROFILE%\.jarvis\voice\duration_bench_2026-09-10.json` (`window_rule: "stream"`). The atom-rule
run is kept beside it as `duration_bench_2026-09-10_atoms.json` — the record of the stop.

| D (s) | n_pos | n_neg | EER | EER threshold | FAR @ 0.3585 | FRR @ 0.3585 | pos_min | pos_max | neg_max | band |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 143 | 702 | 0.3007 | 0.0759 | 0.0000 | 0.7413 | −0.1407 | 0.6090 | 0.2817 | fail |
| 2 | 69 | 333 | 0.2319 | 0.0980 | 0.0000 | 0.4783 | −0.1056 | 0.6070 | 0.2597 | fail |
| 3 | 42 | 209 | 0.1429 | 0.1332 | 0.0000 | 0.3571 | −0.0383 | 0.6150 | 0.2348 | fail |
| 5 | 27 | 104 | 0.0741 | 0.1729 | 0.0000 | 0.2222 | 0.0320 | 0.6367 | 0.2311 | fail |
| 8 | 14 | 54 | 0.0714 | 0.1806 | 0.0000 | 0.1429 | −0.0001 | 0.6620 | 0.2075 | fail |
| 12 | 2 | 22 | 0.0000 | 0.3985 | 0.0000 | 0.0000 | 0.6086 | 0.6445 | 0.1883 | **PASS** |

**The table varies, and the per-file counts are what prove it.** `windows_per_file` is now recorded
per D per file precisely because its absence is what let M1a.2 read as a measurement. For
`owner_heldout2_001.wav` the counts are **10, 5, 3, 2, 1, 0** across the grid (they were 1, 1, 1, 1,
1, 0 under the atom rule), and across all thirteen positives the D = 1 row is
`[10, 10, 10, 10, 17, 11, 10, 11, 12, 10, 11, 10, 11]` — 143 windows, against the pre-registered
expectation of ≈ 140. `n_pos` falls 143 → 69 → 42 → 27 → 14 → 2 with no repeats, and the extrema move
in every row.

#### The duration dependence, which is now visible and is the point of the milestone

The owner's own voice against his own threshold, by window length: **FRR 0.7413 at 1 s, 0.4783 at
2 s, 0.3571 at 3 s, 0.2222 at 5 s, 0.1429 at 8 s, 0.0000 at 12 s.** `pos_min` is **negative** at
1, 2, 3 and 8 seconds — the owner's own speech scoring below zero cosine against his own centroid —
while `pos_max` barely moves (0.6090 → 0.6445). So short windows do not shift the distribution; they
grow a long low tail. That is M1's unexplained evidence explained: the pipeline was applying a
threshold measured on ten-second pieces to one-to-three-second segments, and three quarters of them
fall under it at one second.

**FAR is 0.0000 at every D, and `neg_max` never exceeds 0.2817.** No stranger ever crossed 0.3585 at
any window length, so the FAR band never binds and the entire decision is the FRR band. The stored
threshold is not letting strangers in at any duration; it is locking the owner out at short ones.

#### Two things the number rests on, stated because the rule does not state them

**(1) `MIN_EMBED_S` = 12.0 is read from a row with `n_pos` = 2.** Only two of the thirteen held-out
pieces contain 12 s of speech (files 005 and 009), so `FRR = 0/2`. That is a true zero and a very
thin one; the 0.23-margin separation M1a.2 recorded on ~10 s windows is the stronger statement about
the threshold's soundness at length, and this row is not evidence beyond it.

**(2) The reading rule has no minimum sample size, and at these counts that biases it toward the
sparsest row.** The finest non-zero FRR a row can express is `1/n_pos`: 1/14 = 0.0714 at D = 8 and
1/27 = 0.0370 at D = 5. So at D = 8 a single rejected window already exceeds the 5 % band and the row
can only pass at exactly zero — which the D with the fewest windows is mechanically the likeliest to
achieve. **This is reported, not repaired:** adding an `n_pos` floor to `choose_min_embed_s` after
seeing the table would be fitting the rule to its own output, which is what M1a.2 refused to do and
is the strategist's ruling, not the coder's.

#### The consequence for M1b.3 — **and a claim of this entry's own, corrected in place**

`PROMPT-VOICE-M1C-PREP-2.md` §4 pre-registers turns that **close at 10 s of speech** and embeds
**only turns ≥ `MIN_EMBED_S`**. This entry first read those two numbers as mutually exclusive at
`MIN_EMBED_S` = 12.0 s and concluded that no turn could ever qualify, so M1b.3 was not implementable.
**That was wrong.** The cap closes a turn once it REACHES 10 s of accumulated speech; it never
truncates a single segment, because the spine's spans stay the Whisper segments and nothing may
split one. So a turn can exceed 10 s, and M1b.3's own run proved it on the first file: the owner
throwaway's single 12.48-second segment formed a 12.48-second turn that cleared the 12-second
minimum and scored 0.5862. The wrong sentence is corrected rather than deleted, because the reason
it was written is worth keeping — the interaction between a pre-registered cap and a measured
minimum is real, and it was reasoned about instead of being run.

**What survives the correction is the practical shape, and it is the honest limit of the number
above:** at `MIN_EMBED_S` = 12.0 s only long uninterrupted stretches are attributable. A turn
assembled from ordinary conversational segments will often close near 10 s and fall under the
minimum, and every such turn inherits its neighbour's cluster by adjacency rather than being
identified. Whether 12.0 s is the right operating point — given that it is read from a row with two
positive windows — remains the strategist's ruling.

Tests: `test_voice_logic.py` 66 → **68 checks** (T12c the stream cut — window count, per-window frame
span and the straddling window all derived from the fixture's run lengths rather than typed; T12d the
pipeline reads a minimum only from a bench declaring `window_rule: "stream"`, and a superseded run
parked under a suffixed name is invisible to the date-shaped glob). Two mutants, the control green
first, each failing BY NAME and each restored from a byte-copy verified by md5: the trailing partial
window kept instead of dropped → T12c; the `window_rule` refusal removed → T12d.

**One correction to the prompt's own premise, because it would otherwise have been silently false:**
§2 says the renamed atom-rule file "no longer matches the glob the pipeline reads". It did —
`duration_bench_2026-09-10_atoms.json` matches `duration_bench_*.json` and sorts AFTER the date-named
file, so a rename alone would have made the superseded run the one the pipeline read. The glob is
narrowed to `duration_bench_????-??-??.json` to make the claim true, and T12d pins both halves.


### M1b.2 — 2026-09-10 — deterministic ASR and the double-run guard; the throwaways re-run

**Both re-runs agreed to the millisecond, and the pinning also reversed M1b's clustering MISS — on
this audio, and by a mechanism worth naming rather than celebrating.** The audio was rebuilt
byte-identical (both sha256s equal to the ones the store recorded at M1b, verified before the run),
so this is the same input measured twice under two decoder configurations.

| property | M1b | M1b.2 |
|---|---|---|
| decoder | `beam_size=5`, `vad_filter=False` | `temperature=0.0`, `beam_size=5`, `vad_filter=True`, `condition_on_previous_text=False` |
| owner throwaway | 9 spans, 3 embedded | **1 span (12.48 s), 1 embedded** |
| owner's score(s) | 0.1113 / 0.3232 / 0.2598 — all **below** 0.358503 | **0.5862 — above it** |
| owner's cluster | 3 new clusters, `owner` 0 | **`cluster_source: owner`** |
| stranger throwaway | 1 span, 1 new cluster, 0.1992 | 1 span (19.82 s), 1 new cluster, **0.2012** |
| reproducibility | 3 segments once, 9 the other time on byte-identical input | **passes agree: `[1, 1]` segments, max start delta 0.0 s, max end delta 0.0 s, text identical — both files** |
| RTF (first pass) | 0.706 | 0.097 / 0.100 (two passes 3.38 s / 3.89 s total for 20 s each) |

**Why the miss reversed, stated as mechanism and not as a fix:** `vad_filter=True` drops the
near-silent stretches before the decoder sees them, so the owner's read speech came back as ONE
12.48-second segment instead of nine fragments of one to three seconds. M1a.3 measured exactly what
that is worth — the owner's FRR against his own threshold is 0.36 at three seconds and 0.00 at
twelve — so a longer span scoring 0.5862 is the duration curve, not a better embedder. **Nothing was
tuned to make this happen: the threshold is M0b's, τ\* is M1a's, and the four decoder settings were
chosen for determinism before this run existed.** It is one file of clean read speech; conversational
audio with real pauses will segment differently, and M1c's real recording is what tests that.

**The guard, and what it can and cannot say.** Both files: `agreed: true`, `n_segments [1, 1]`,
`max_start_delta_s 0.0`, `max_end_delta_s 0.0`, `text_identical: true`, `kept_by_guard: false`,
`deleted: true`. That is two identical decodes of each file **in one session, back to back, on an
otherwise idle GPU**. M1b's divergence happened with a game running, so the conditions that produced
it were not reproduced here and are not claimed to be excluded — what the guard promises is not that
divergence cannot happen but that when it does the audio survives it. The cost is one extra decode
per file, measured: 1.94 s + 1.45 s and 2.01 s + 1.89 s.

**The VAD parameters are recorded, not re-declared.** `vad_filter=True` with no `vad_parameters`
uses faster-whisper's Silero defaults, so `ASR.__init__` reads them back from `VadOptions()` and
writes them into every transcript: `threshold 0.5, neg_threshold null, min_speech_duration_ms 0,
max_speech_duration_s "inf", min_silence_duration_ms 2000, speech_pad_ms 400` (the infinity is
stored as a string because JSON has none and a transcript must round-trip through strict JSON). A
library upgrade that moved a default would show as a changed record rather than as an unexplained
change in segmentation.

**M1b's 47.98-second segment did not recur** — VAD removed the near-silent tail that produced it. The
end clamp stays regardless (T11d): it guards the spine against a class of ASR output, not against one
decoder setting.

#### The reset, with the store read before it was deleted

The throwaway store was verified to hold exactly the two throwaways before anything was removed:
**2 recordings** (`4ee13a0a3b7ff0fc…`, `e3829a7410ebeb18…`, 20.0 s each), **9 spans, 5 clusters,
1 person, 3 span embeddings, 1 audit row** (`op=purge`, `rule=R7`, cluster 5 — M1b's own purge
proof). Deleted: `household.sqlite` (184,320 B); no `-wal` or `-shm` existed. After the re-run the
store held **2 recordings, 2 spans, 2 clusters** (the owner's and one new voice), **1 person,
2 embeddings, 0 audit rows**; both transcript JSONs were then deleted and the store reset again, so
the milestone leaves nothing behind.

**One observation, reported and not fixed:** the `recording` row carries a `deleted_audio_at` column
and `ingest` never sets it — both rows read NULL although both WAVs were deleted, and the transcript
JSON that does record the deletion is itself deleted afterwards. The spine is meant to be what
survives the audio, so a column that would say when the audio stopped existing is worth either
filling or removing; it is left alone here because this prompt's scope is the safety half and the
field's semantics belong to the store's own design.

Tests: `test_voice_logic.py` 68 → **70 checks** (T13a the pipeline's four pinned kwargs and the M0a
command's two, pinned through a fake model that captures what `run` forwards; T13b the guard's truth
table both ways — a boundary inside the tolerance agrees, one outside it does not, identical times
with different text do not, a different count does not — plus `ingest_one` keeping the audio on a
disagreement, writing the FIRST pass exactly once, running both passes under the pinned settings,
and still deleting on agreement unless `--keep`). The detail the guard records is asserted to carry
no span text: it is written into the transcript JSON and printed to a terminal, and a mismatch report
is not a licence to quote what was said. Two mutants, the control green first, each failing BY NAME
and each restored from a byte-copy verified by md5: the comparison always returning agreement →
T13b; `finalize` ignoring `kept_by_guard` and deleting the audio anyway → T13b (`deleted: true`,
the WAV gone — the irreversible failure this guard exists to prevent).


### M1b.3 — 2026-09-10 — turns as the embedding unit; the throwaways re-run

**The band is met: the owner throwaway's one qualifying turn scored 0.5862 against his 0.358503
threshold and came back `owner`; the stranger's never did.** The embedding unit is now the TURN —
consecutive Whisper segments with no real silence between them — and only a turn holding at least
`MIN_EMBED_S` of speech is embedded and assigned. **The spine's spans stay the Whisper segments;**
each carries `turn_id` and inherits its turn's cluster.

| | owner throwaway | stranger throwaway |
|---|---|---|
| input | `4ee13a0a3b7ff0fc…`, 20.0 s | `e3829a7410ebeb18…`, 20.0 s |
| turns | 1 (1 segment, **12.48 s** of speech, 4.18–16.66 s) | 1 (1 segment, **19.82 s**, 0.18–20.00 s) |
| `speech_s` ≥ `MIN_EMBED_S` 12.0 | yes → embedded | yes → embedded |
| owner score | **0.5862** (threshold 0.358503) | **0.2012** |
| assignment | `cluster_source: owner`, cluster 1 | `cluster_source: new`, cluster 2 |
| two ASR passes | agreed, `[1, 1]`, deltas 0.0 s, text identical | agreed, `[1, 1]`, deltas 0.0 s, text identical |
| audio | deleted after the commit and the JSON | deleted after the commit and the JSON |

Store after both: 2 recordings, 2 spans, 2 clusters, 1 person, **2 span embeddings** (one per
embedded turn, not one per span), 0 audit rows. Both transcript JSONs were then deleted and the
store reset, so the milestone leaves nothing behind. Both WAVs were rebuilt sha256-identical to
M1b's before the run, so this is the same audio M1b and M1b.2 measured.

**The segmentation reproduced across SESSIONS, not only within one.** M1b.2's run of the same file
and this one — separate processes, separate model loads — both produced the single 12.48-second
segment at 4.18–16.66 s and the same owner score to four decimals. That is a stronger statement than
the in-run guard can make, and it is the first evidence that the pinned decoder is stable across
loads rather than merely twice in a row.

#### The rule, and the one place it is easy to misread

A turn opens when the silence before a segment exceeds **0.5 s**, or when the turn it would join
already holds **10 s** of speech. A turn's speech is the SUM of its segments' durations, not its wall
span, because what is embedded is those segments concatenated — the same speech-packing the duration
bench measures. **The 10-second cap bounds MERGING, not a segment:** a turn closes once it REACHES
10 s, so the segment that crosses the cap is inside it, and a single segment longer than 10 s is a
turn on its own, because the spine's spans stay the Whisper segments and nothing may split one.

A turn under the minimum is never embedded, never scored, and takes the PREVIOUS turn's cluster
(`cluster_source: adjacent`) — the same heuristic and the same honest limit the span rule had: such a
turn is attributed, never identified. A recording that embedded nothing leaves its spans unassigned
rather than inventing a voice.

**A turn's vector is written against its FIRST span.** The store keys an `embedding` row to a span
and a turn is not a row of its own; writing the same vector against every span of the turn would
duplicate one measurement into several and make any later count of speaker vectors — or any centroid
built from them — silently wrong. Whether the store should grow a `turn` row is its design's
decision, not this pipeline's, and it is flagged rather than taken here.

#### A correction to M1a.3, made where it was written

M1a.3's entry above said the 10-second cap and a 12-second minimum were "mutually exclusive" and
that no turn could ever qualify. **That was wrong, and this milestone's own run disproves it:** the
cap closes a turn at 10 s of ACCUMULATED speech, so it never truncates a single segment, and the
owner throwaway's 12.48-second segment formed a 12.48-second turn that cleared the 12-second minimum
on the first try. The paragraph has been corrected in place rather than deleted. What survives the
correction is the practical shape, which is real and is the honest limit of this milestone: at
`MIN_EMBED_S` = 12.0 s only long uninterrupted stretches are attributable, and short conversational
turns will inherit by adjacency rather than be identified.

#### What this does not show

Two files of clean speech, one voice each, one turn each. Nothing here exercises a turn built from
several segments, a speaker change inside a recording, or the adjacency path on real audio — those
are covered by tests with synthetic segments and by nothing else. **The 12.0 s minimum itself rests
on a bench row with two positive windows** (M1a.3), so the number the pipeline now enforces is the
rule's faithful output and thin evidence at the same time; M1c's real recording is what tests both.

Tests: `test_voice_logic.py` 70 → **72 checks** (T13c the turn rule end to end — a gap equal to the
0.5 s limit still merges and one just over it does not, the cap closes on the segment that reaches
it, a single over-long segment is a turn on its own, and through the pipeline a 12.0 s turn is
embedded while a 1.0 s turn is not, scores nothing, and takes the previous turn's cluster while its
segments inherit; T13d the minimum comes only from a stream-rule bench that produced one — no bench,
an atom-rule bench and a `min_embed_s: null` bench are three distinct refusals — and `ingest_one`
refuses outright without one, leaving the audio in place). T11d was rewritten rather than kept: its
three back-to-back segments are now ONE turn with ONE embedding, which is the change. Two mutants,
the control green first, each failing BY NAME and restored from a byte-copy verified by md5: the gap
comparison made exclusive so a 0.5 s gap splits a turn → T13c; the minimum-duration check removed →
T13c.

**A mutant found a vacuous assertion, which is the reason to run them.** The minimum-duration mutant
did NOT bite at first: the suite's stub WAV reader returned one second of samples, so the short turn
sliced to nothing and was skipped for want of audio rather than by the rule under test — the
assertion would have passed with the rule deleted. A 40-second stub was added for that test and the
mutant then failed by name. The one-second stub is kept where its length is not load-bearing, with
the reason written beside both.

**The CLI refusal was exercised for real, not only in the suite:** `ingest` pointed at a
`JARVIS_VOICE_HOME` with no bench printed `no duration_bench_<date>.json under … - run
python -m jarvis_voice duration-bench first; the minimum embedding duration is measured, never
guessed` and returned in under a second, before either model was loaded.


### M1a.4 — 2026-09-10 — the reading rule's sample floor; 12.0 s retracted

**`MIN_EMBED_S` = 12.0 s is RETRACTED. With a floor of twenty windows on each side, NO duration on
the grid meets the bands at the stored threshold — and that is the measurement, not a failure of
it.** M1a.3 reported the value and refused to build on it; this entry withdraws it in the file that
published it.

**Why a floor, computed rather than asserted.** A rate needs a denominator to be a rate. The finest
non-zero FRR a row can express is `1/n_pos`: at D = 8 that is `1/14 = 0.0714`, already outside the
5 % band, so such a row can only pass at **exactly zero** — and the D with the fewest windows is
mechanically the likeliest place to find one. The rule without a floor therefore did not merely
tolerate the sparsest row, it **preferred** it. `MIN_N = 20` is the smallest n at which a single
rejection (`1/20 = 0.05`) still sits inside `MAX_FRR`, so it is the smallest floor at which the band
means what it says rather than "no failures were observed"; T14a computes both halves of that
(`1/20 ≤ 0.05` and `1/19 > 0.05`) rather than taking 20 on trust. The floor applies to `n_pos` AND
`n_neg`, because FRR needs positives and FAR needs negatives and a row solid on one side is not a
measurement of the pair.

**The measured table, re-read.** The rows are untouched — a measurement does not change when the
rule for reading it does.

| D (s) | n_pos | n_neg | FRR @ 0.3585 | FAR @ 0.3585 | floor (n ≥ 20 both) | bands | read? |
|---|---|---|---|---|---|---|---|
| 1 | 143 | 702 | 0.7413 | 0.0000 | ✅ | ✗ | no |
| 2 | 69 | 333 | 0.4783 | 0.0000 | ✅ | ✗ | no |
| 3 | 42 | 209 | 0.3571 | 0.0000 | ✅ | ✗ | no |
| 5 | 27 | 104 | 0.2222 | 0.0000 | ✅ | ✗ | no |
| 8 | 14 | 54 | 0.1429 | 0.0000 | ✗ | ✗ | never eligible |
| 12 | **2** | 22 | 0.0000 | 0.0000 | ✗ | ✅ | **excluded — this is the retraction** |

**The only row that meets the bands is the only row the floor excludes.** That single sentence is
the whole finding, and T14a pins it both ways: at `min_n = 1` the rule reproduces **12.0 exactly**,
so the retraction is caused by the floor and by nothing else that changed.

**The re-read, run for real** (`python -m jarvis_voice duration-bench --reread`, no model loaded):

```
file        : C:\Users\jluca\.jarvis\voice\duration_bench_2026-09-10.json
reading rule: FRR <= 0.05, FAR <= 0.01, n_pos and n_neg >= 20
rows under the sample floor (reported, never read): [8.0, 12.0]
min_embed_s : 12.0 -> None
retracted   : 12.0 s - recorded in the file, not deleted
```

`min_embed_s` is now `null`, `reading_rule: {"max_frr": 0.05, "max_far": 0.01, "min_n": 20}` sits
beside it, and `retracted: 12.0` preserves the withdrawn value — a number that was published and
then withdrawn is more useful in the record than one that quietly stopped existing. **The table was
verified byte-identical across the rewrite** (sha256 of the canonicalised `table`, `d1814211431db6df`
before and after), the only new keys are `reading_rule` and `retracted`, and the function refuses to
write at all if the table has moved. Re-running is idempotent.

#### What this leaves standing, and what it does not

**Standing, and it is the milestone's result:** the owner's EER against 78 public negatives on a
single speech-packed window is 0.30 / 0.23 / 0.14 / 0.07 / 0.07 / 0.00 at 1 / 2 / 3 / 5 / 8 / 12 s,
FRR at the stored threshold falls 0.74 → 0.00, and FAR is 0.0000 at every length with `neg_max`
never above 0.2817. **The threshold is a ~ten-second property**, and a single window under about
five seconds cannot be attributed by its embedding at any threshold.

**Not standing:** any single-window minimum. There is no D on this grid at which one window clears
the bands with a denominator behind it, so the pipeline cannot ask "is this span the owner?" of a
short span at all. That is what forces the two-level rule of M1b.4 — a long turn at ingest, or a
cluster's centroid over accumulated speech afterwards.

Tests: `test_voice_logic.py` 72 → **73 checks** (T14a — the floor, the smallest-n justification, the
measured table reading to `None`, the `min_n = 1` reproduction of 12.0, and the re-read's round trip
including the untouched table and its idempotence). T12b's hand-built rows gained `n_pos`/`n_neg` at
the floor so that check stays about the BANDS alone; its assertions are otherwise unchanged. One
mutant, the control green first, failing BY NAME and restored from a byte-copy verified by md5: the
sample floor removed → T14a (and T14a alone — T12b stays green, which is what makes the two checks
separable).


### M1b.4 — 2026-09-10 — the two-level owner decision; the throwaways re-run

**The owner decision is no longer one question asked of one span. It is asked at two levels, and
each is asked only where the stored threshold has a measured footing.** At ingest, a turn holding at
least **10 s of speech** — the length of the M0b pieces the threshold was measured on — is checked
against the threshold; every other turn of at least **2 s** is embedded and clustered at τ\*, its
owner score recorded and never acted on. After ingest, a non-owner cluster whose centroid over at
least **10 s of accumulated embedded speech** clears the same threshold is **merged** into the
owner's cluster: every span relabelled, the cluster row removed, one `audit` row.

**Why a cluster centroid is the second level and not a second span.** M1a.3 measured the threshold
as a ~ten-second property of a voice, and conversation is mostly shorter turns than that. A cluster
accumulates speech across turns and days and its centroid is a MEAN of embeddings — the same shape
as the enrollment centroid the threshold was measured against — so the comparison is like with like
at a length no single conversational turn reaches. **Whether that holds on real conversational
speech is M1c's to report; here it is a rule with a measured footing, not a proven one.**

Every constant names where it comes from, because the number they replace was read from a
measurement and had to be retracted (M1a.4):

| constant | value | provenance |
|---|---|---|
| `EMBED_MIN_S` | 2.0 s | **imported** from `verify.MIN_CLIP_S`, not retyped — the existing refusal length, unchanged since M0a |
| `OWNER_TURN_MIN_S` | 10.0 s | the length of the M0b held-out pieces the stored threshold was measured on |
| `OWNER_CLUSTER_MIN_S` | 10.0 s | the same footing, for a centroid |

**None is derived from the duration table**, and `ingest` reads no bench for a duration at all —
T13d proves it by running a full ingest with `JARVIS_VOICE_HOME` pointed at an empty directory.

#### The throwaways, re-run

Both WAVs were rebuilt **sha256-identical** to the ones M1b recorded, verified before the run.

| | owner | stranger |
|---|---|---|
| turn | 1 segment, **12.48 s**, 4.18–16.66 s | 1 segment, **19.82 s**, 0.18–20.00 s |
| `owner_eligible` (≥ 10 s) | yes | yes |
| score against the owner centroid | **0.5862** | **0.2012** |
| at ingest | **`cluster_source: owner`**, cluster 1 | `new`, cluster 2 |
| owner-merge | — (already his) | compared at 19.82 s, **0.2012 < 0.358503 → left as its own voice** |
| guard | agreed, `[1, 1]`, deltas 0.0 s, text identical | same |
| `deleted_audio_at` | `2026-09-10T19:42:02` | `2026-09-10T19:42:06` |

Store after both: 2 recordings (both with `deleted_audio_at` set), 2 spans, 2 clusters,
`embedded_speech_s` **12.48** and **19.82**, 1 person, 2 span embeddings, **0 audit rows** — nothing
was merged, which is the expected outcome and not a null result: the stranger's cluster WAS compared
(it holds enough speech) and was left, so the rule ran and declined. Transcripts deleted, store
reset.

**Nothing was tuned.** The threshold is M0b's `0.358503175300161`, τ\* is M1a's `0.56`, and the three
durations were fixed in §0 of the prompt before this run existed.

#### What the rule is deliberately unable to do

A turn under `OWNER_TURN_MIN_S` is **not** claimed for the owner even when it scores above the
threshold. T14b pins exactly that with a fixture where the short turn scores **higher** than the long
one that wins: 9.9 s at 0.50 is clustered, 10.0 s at 0.36 is the owner's. That is not a rounding
choice — M1a.3 measured a **74 % false-reject rate** on the owner's own one-second windows, so a
short window's score is not evidence in either direction, and acting on the half of it that happens
to look right would be reading noise selectively. Such a voice can still become the owner's, later,
from the cluster centroid.

Accumulated speech accrues **only from embedded turns**. A turn attributed by adjacency carries no
evidence of its own — that is what adjacency means — so it must not make a cluster look better
measured than it is.

#### Two conventions recorded rather than changed

**A turn's vector is written against its FIRST span.** The store keys an `embedding` row to a span
and a turn is not a row of its own; writing the vector against every span of the turn would
duplicate one measurement into several and make any later count of speaker vectors — or any centroid
built from them — silently wrong. A `turn` row is the store's design decision, deferred.

**A merged cluster that had earned personhood leaves its `person` row in place**, and the audit note
names it. Deleting a person is not the merge's call: facts and edges may reference it, and
reconciling people is the MS2 layer's job. The dangling row is visible in the audit trail rather
than silently removed. (It cannot arise yet in practice — personhood needs three distinct days.)

**`deleted_audio_at` is written to the STORE, not to the transcript JSON.** `finalize` writes the
JSON before it deletes the audio, so the timestamp cannot be in it; and the JSON is deleted after
the run anyway. The spine is what survives the audio, which is exactly why the column belongs there.

#### Tests

`test_voice_logic.py` 73 → **76 checks**; `test_memory_logic.py` 264 → **265**.

* **T14b** — the ingest rule, with the short turn scoring higher than the long one that wins;
  adjacency for a turn under `EMBED_MIN_S`; accumulated speech coming only from embedded turns.
* **T14c** — `owner_merge` over four clusters: one merged (11 s at 0.40), one **never compared**
  (9 s at 0.99 — it would merge trivially if the minimum were dropped), one left (11 s at 0.30), and
  the owner's own cluster excluded by id although it would score 1.0 against itself.
* **T14d** — `deleted_audio_at` written iff the audio was deleted, and only after: an ordering
  wrapper records whether the WAV still existed at the moment the spine claimed it was gone (it did
  not), and a run whose deletion RAISES leaves the row NULL.
* **T40** (memory suite) — `merge_cluster` relabels every span, folds `n_spans`, `first_heard`,
  `embedded_speech_s` and a **distinct-days** recount, removes the loser's row, leaves the
  span-keyed speaker vectors untouched, writes exactly one audit row, and refuses an unknown or
  self merge.
* **T13d** was rewritten rather than deleted: it used to pin that `ingest` refuses without a measured
  minimum, and that minimum no longer exists.

Four mutants, the control green first, each failing BY NAME and each restored from a byte-copy
verified by md5: the owner check run on every turn → T14b; the accumulated-speech minimum removed →
T14c; `deleted_audio_at` written before the deletion → T14d.

**The fourth mutant had to be re-run to be honest, and the reason is worth keeping.** Written as an
ADDED call before `finalize`, it left the correct call in place too — so the ordering probe recorded
the second call (after the delete) and read as if nothing were wrong, while the check failed on other
clauses. Re-run as a MOVE rather than an addition, the probe reads `existed_at_set: True` and the
ordering clause is the one that fails. A mutant that bites for the wrong reason proves the check less
than it appears to.

**The store gained a column and a forward-only migration.** `cluster.embedded_speech_s` is added by
`_add_missing_columns` when absent, because `CREATE TABLE IF NOT EXISTS` cannot widen a table that
already exists and the household store is the one file in this project that must survive its own
schema changing — the audio it describes is already gone.


---

## 7. Done-when (canon, `phase4/docs/ROADMAP.md:130-132`, verbatim)

- [ ] The owner's voice, mastered first: enrolled from the Main PC headset mic; owner-versus-not speaker verification measured on held-out recordings at a pre-registered rate; everything transcribed, raw audio deleted after transcription — proven on the pipeline before any hardware
- [ ] Household learning, the guess: with only the owner enrolled, days of recordings yield a household profile in a purpose-built, state-of-the-art memory store — who is who and to whom, the owner's style and preferences, habits, topics; each fact sourced, dated, stated-or-inferred with a confidence, used without confirmation — in which JARVIS has identified the recurring second voice as the owner's wife and who she is to him; surfaced by recall over control-IN, a clean console view, and a digest of new learning
- [ ] Voice as a command front-end: one owner-voice command reaches JARVIS through control-IN and is answered (the headset first; speaker-verified from the wearable later); no wake word is required — JARVIS infers that it is being addressed; a command in any other voice is declined and logged, never executed

M0a advances the first line's pipeline half (verification measured, transcription with deletion proven) on public speakers; none of the three is ticked.

Sources: `phase4/docs/ROADMAP.md:130-132`.

---

## 8. Honest ceiling (authored)

What exists after M0a is a set of command-line tools on the Main PC that can record from a headset, turn a clip into a 192-number vector, compare it to a stored centroid, transcribe it on the GPU, and delete the audio afterwards — proven on forty audiobook readers, not on the owner. A 0.00 % EER on clean read speech from one enrolled speaker against thirty-nine others says the chain works; it says nothing yet about the owner's voice through a headset in a room, and nothing about telling him from his wife. No voice of the owner has been recorded. No speaker has been clustered, nothing has been guessed, no profile exists, no console screen exists, and the memory store this goal turns on has not been designed. The consent mechanism shipped is raw-audio deletion plus a manual purge the owner will have to perform; it is weaker than the rule the idea first wrote down, and it says so.
