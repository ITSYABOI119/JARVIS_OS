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
