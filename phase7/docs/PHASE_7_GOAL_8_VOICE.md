# Phase 7 Goal 8 — Ambient Voice Wearable (household voice learning) — Plan

**Status:** ACTIVE — M0a landed 2026-09-06 (`feat(phase7): goal 8 M0a - the owner-voice tooling on the Main PC (record, enroll, verify, transcribe, evaluate) with a public-speaker self-test; goal doc PHASE_7_GOAL_8_VOICE.md; audio formats ignored by git`). The owner's own enrollment is M0b, a later prompt; the board row for the owner's voice stays `NOT STARTED` until M0b's band passes.
**Prerequisite:** goal 8 canon `phase4/docs/ROADMAP.md:122` (done-when `:130-132`); the scope the owner set, `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §8; the board `phase7/docs/PHASE_7_PLAN.md` §0 (ten 7.8 rows).
**Sources:** those three files at `49ac1c7`; `phase6/docs/PHASE_6_GOAL_6-1_MONITORS.md` (the shape this doc mirrors); the M0a self-test run of 2026-09-06 (`%USERPROFILE%\.jarvis\voice\selftest_2026-09-06.json`, quoted in §6); the venv freeze (`%USERPROFILE%\.jarvis\voice\freeze.txt`).

---

## 1. Scope + honesty

**What this goal is, in the owner's words** (idea doc §8, recorded 2026-09-06): *"The owner's voice first. Enrolled from the Main PC headset mic and mastered — owner-versus-not speaker verification measured on held-out recordings — before any household learning."* — *"Everything is transcribed; nothing is discarded before storage."* — *"speech that is neither the owner's nor his wife's is the OWNER'S to delete, by hand, after transcription"* — *"a HOUSEHOLD PROFILE in a purpose-built store on the Main PC — who is who and to whom, the owner's style and preferences, habits and routines, topics, how each person speaks — every fact carrying its source recording, date, stated-or-inferred and a confidence."* — *"The store is to be STATE OF THE ART."* — *"Only the owner is enrolled: JARVIS is not told who the second voice is and must work out, over days of recordings, that the recurring second voice is the owner's wife and who she is to him."*

This document is the goal's plan. M0a, landed with it, is the tooling for the first two board rows — record, enroll, verify, transcribe, evaluate — proven on PUBLIC speakers. Nothing about the owner has been recorded, embedded or stored.

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
2. **Code lives at `phase7/voice/`** as the package `jarvis_voice`, one CLI (`python -m jarvis_voice record|enroll|verify|transcribe|evaluate|selftest`), and one stdlib-only test `phase7/voice/test_voice_logic.py` (35 checks) with the CI step `"Phase 7: goal 8 voice logic (Python, stdlib-only)"`. The pure modules (`evaluate`, `verify`, `enroll`, the deletion rule in `transcribe`) import nothing beyond the standard library at module level; GPU code is never imported by the test.
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
9. **Owner-only enrollment; no confirmation gate; everything transcribed** — the owner's rules (idea doc §8). The tooling has no "wife" concept; speaker clustering of non-owner speech is M1.
10. **The owner records his own enrollment (M0b) in a later prompt**, from the runbook in §6. M0a's evidence is public speakers only.

Sources: `%USERPROFILE%\.jarvis\voice\freeze.txt`; the M0a run (§6); `phase7/voice/jarvis_voice/*.py`; `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §8.

---

## 3. Milestones

| Milestone | What | Done-when (numbers where one exists) | Board row |
|---|---|---|---|
| **M0a** (this commit) | The tooling + the public-speaker self-test | EER ≤ 5 % on ≥ 20 positives and ≥ 40 negatives from ≥ 20 speakers; ASR on the GPU at RTF < 1.0; raw-audio deletion proven; the stdlib test green in CI — **all met, §6** | a new "M0a" row above the owner's-voice row |
| **M0b** | The owner's enrollment + held-out measurement (the operator records; a later prompt runs `evaluate`) | EER ≤ 3 % on the owner's held-out clips against the public negatives (plus, if he chooses, consented clips of his wife as the hardest negative); threshold chosen at M0b's own EER point; every held-out owner clip ≥ 3 s | the first 7.8 row flips only on this band |
| **M1** | Transcribe-everything + speaker clustering of non-owner speech + the one-action purge per cluster | clusters measured against the corpus's known speakers before any household audio; the purge is one command per cluster | the second 7.8 row |
| **M2** | The memory store — AFTER the strategist's research lands (the board's research row) — and the guess | the guess named with a confidence, over days of recordings, with only the owner enrolled | the research row, then the store-and-guess row |
| **M3** | The console profile view (designed in Claude Design, real source only) | the UI–feature-parity rule met: every rendered field has a live source | the profile-view row |
| **M4** | The digest of new learning | a learned-this-week digest from the same store | the digest row |
| **V0** | Headset command → Whisper → the receiver-as-signer → control-IN | one owner-voice command answered over control-IN | the V0 row |
| **V1 / V2** | The recorder wearable; speaker-verified wake-word commands | per the idea doc §4 | the V1 / V2 rows |
| **V3** | Live ambient variants | decided last, only if still wanted | BLOCKED |

None is dated; the first-arc choice (7.1 vs 7.8) is the operator's (`PHASE_7_PLAN.md` §1).

Sources: `phase7/docs/PHASE_7_PLAN.md` §0 (the ten 7.8 rows); `phase4/docs/BEYOND_PHASE7_VOICE_WEARABLE.md` §4, §8; §4 of the prompt that landed M0a (its pre-registered bands, reproduced here).

---

## 4. Storage / state

**In the repo:** `phase7/voice/jarvis_voice/` (nine modules: `__init__`, `__main__`, `paths`, `audio`, `speaker`, `enroll`, `verify`, `transcribe`, `evaluate`, `selftest`), `phase7/voice/test_voice_logic.py`, this document, the CI step, and the `.gitignore` block (`*.wav *.flac *.mp3 *.m4a *.ogg *.opus *.webm`, `phase7/voice/.venv/`, `phase7/voice/**/*.npy`, `phase7/voice/**/*.pt`) — proven with a throwaway `phase7/voice/x.wav` showing `!!`.

**Never in the repo:** every recording, embedding, transcript, corpus, the venv and the Hugging Face cache — all under `C:\Users\jluca\.jarvis\voice\` (`.jarvis/` is itself ignored at `.gitignore:115`). At M0a the cache holds the ECAPA model and the 2.9 GB `large-v3` snapshot; `public\` holds the 338 MB tarball and its extraction; `public\enroll_S1\` the throwaway enrollment of the pseudo-owner; `transcripts\` one JSON; `raw\` is empty (the copy was deleted); `enroll\` and `heldout\` are empty — nothing of the owner exists yet.

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

### The M0b runbook — the owner's enrollment (a later prompt; the operator records)

What "PASS" means: **EER ≤ 3 %** on the owner's held-out clips against the public negatives, with the threshold chosen at M0b's own EER point, and every held-out owner clip ≥ 3 s. The board's first 7.8 row (the owner's voice) flips to DONE only on that band, in that prompt's commit B.

1. **Enrollment, day 1** — ≥ 3 × 60 s on the headset (device 1 today: `record --list-devices` to confirm), reading text, at different times of day, into `enroll\`:
   `python -m jarvis_voice record --seconds 60 --device 1 --out %USERPROFILE%\.jarvis\voice\enroll\owner_enroll_01.wav` (repeat `_02`, `_03`, …).
2. **Held-out, a LATER day** — ≥ 10 × 10 s clips into `heldout\`:
   `python -m jarvis_voice record --seconds 10 --device 1 --out %USERPROFILE%\.jarvis\voice\heldout\owner_heldout_01.wav` (repeat).
3. **Optional hardest negative** — a few consented clips of his wife into a `heldout_neg\` folder of the operator's choosing.
4. **The later prompt** runs `python -m jarvis_voice enroll --threshold <t>` (the threshold from M0b's own EER sweep, computed by `evaluate` first with a provisional value) and `python -m jarvis_voice evaluate --pos-dir heldout --neg-dir <public negatives + the optional wife clips>`, records EER / threshold / FAR / FRR / counts, and flips the row only on PASS.
5. Nothing under `enroll\` or `heldout\` ever enters the repo (`.gitignore`); the transcripts of any owner recording stay under `transcripts\`.

Sources: the run above; `%USERPROFILE%\.jarvis\voice\selftest_2026-09-06.json`; `%USERPROFILE%\.jarvis\voice\transcripts\selftest_422_422-122949-0005.json`; `%USERPROFILE%\.jarvis\voice\freeze.txt`; `phase7/voice/test_voice_logic.py`.

---

## 7. Done-when (canon, `phase4/docs/ROADMAP.md:130-132`, verbatim)

- [ ] The owner's voice, mastered first: enrolled from the Main PC headset mic; owner-versus-not speaker verification measured on held-out recordings at a pre-registered rate; everything transcribed, raw audio deleted after transcription — proven on the pipeline before any hardware
- [ ] Household learning, the guess: with only the owner enrolled, days of recordings yield a household profile in a purpose-built, state-of-the-art memory store — who is who and to whom, the owner's style and preferences, habits, topics; each fact sourced, dated, stated-or-inferred with a confidence, used without confirmation — in which JARVIS has identified the recurring second voice as the owner's wife and who she is to him; surfaced by recall over control-IN, a clean console view, and a digest of new learning
- [ ] Voice as a command front-end: one owner-voice command reaches JARVIS through control-IN and is answered (the headset first; speaker-verified from the wearable later)

M0a advances the first line's pipeline half (verification measured, transcription with deletion proven) on public speakers; none of the three is ticked.

Sources: `phase4/docs/ROADMAP.md:130-132`.

---

## 8. Honest ceiling (authored)

What exists after M0a is a set of command-line tools on the Main PC that can record from a headset, turn a clip into a 192-number vector, compare it to a stored centroid, transcribe it on the GPU, and delete the audio afterwards — proven on forty audiobook readers, not on the owner. A 0.00 % EER on clean read speech from one enrolled speaker against thirty-nine others says the chain works; it says nothing yet about the owner's voice through a headset in a room, and nothing about telling him from his wife. No voice of the owner has been recorded. No speaker has been clustered, nothing has been guessed, no profile exists, no console screen exists, and the memory store this goal turns on has not been designed. The consent mechanism shipped is raw-audio deletion plus a manual purge the owner will have to perform; it is weaker than the rule the idea first wrote down, and it says so.
