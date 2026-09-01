# Beyond Phase 7 — Ambient Voice Wearable ("the bracelet")

**STATUS: IDEA STAGE — recorded 2026-09-01. Not a plan, not a commitment, no code implied.**
This is a Beyond-Phase-7 vision item (see `ROADMAP.md` §Beyond Phase 7). It is written down so no
future session loses it. Starting this arc is the OWNER's call, after Phase 7 exit criteria are met
(or earlier if the owner says so — the pipeline-first slice below has zero box dependencies).

---

## 1. The idea (owner's intent, verbatim in spirit)

A bracelet-style wearable that listens to speech around the owner, learns their voice and what they
talk about — "like JARVIS, he's always listening" — and eventually takes voice commands. The owner's
stated priorities, from the 2026-09-01 discussion:

1. **Ambient learning is the point**, not just commands — the device exists so JARVIS knows more
   about the owner over time.
2. Battery life of a few hours is acceptable if needed; wake-word can coexist.
3. Scope: **the owner's own voice, mainly at home.**
4. Transport idea (owner's): no live link at first — the bracelet **records to local storage like a
   USB drive**, and is plugged into the Main PC over USB-C to upload.

## 2. Two premise corrections (agreed in discussion — they make the idea EASIER)

- **Learning the owner's voice does NOT need 24/7 capture.** Speaker recognition is an ENROLLMENT
  problem: ~30 seconds to a few minutes of clean speech trains a speaker-verification embedding.
  The 24/7 half serves only ambient topic-learning.
- **Continuous capture is battery-cheap; STREAMING is what kills wearables.** An ESP32-S3-class
  device recording I2S mic audio to microSD with all radios OFF draws ~20–40 mA → a ~500 mAh cell
  runs a full waking day. 16 kHz mono ≈ 115 MB/hour raw, so a 32 GB card holds weeks. The owner's
  USB-C batch-sync instinct is therefore the RIGHT v1 architecture, not a compromise: no wireless
  attack surface, no live protocol, nothing touches the box in real time.

## 3. The consent line (non-negotiable design rule, not a disclaimer)

Continuous recording of OTHER people's private conversations without consent is illegal in most
Australian states (listening-device laws) and many other jurisdictions. The agreed mitigation is
an **INGEST RULE, implemented in code**: the Main-PC pipeline runs diarization + speaker
verification against the owner's enrolled voice, **discards all non-owner speech BEFORE anything is
stored**, and **deletes raw audio after transcription**. "Just my voice at home" is enforced by the
pipeline, not promised by the owner. A wake-word-only command device stores no third-party speech
by construction.

## 4. Staged arc

| Stage | What | Hardware |
|-------|------|----------|
| **V-pipe (first, provable now)** | The ingest pipeline: audio file → Whisper ASR (RTX 2070) → own-voice filter (diarization + speaker verify vs the enrolled voice) → transcript → topic/semantic distill → JARVIS memory. Capture-device-agnostic — provable with a cheap voice recorder or an old phone before ANY hardware is designed. **The pipeline is the reusable asset; the bracelet is a peripheral.** | none new |
| **V0 commands** | Desk mic + Whisper → the existing receiver-as-signer → **control-IN**. Proves voice→JARVIS end to end with zero new hardware. | none new |
| **V1 recorder wearable** | Offline recorder: ESP32-S3-class + I2S mic + microSD + USB-C mass-storage/charge. Radios off. Nightly plug-in → V-pipe ingest → wipe card. | new (small) |
| **V2 speaker-verified commands** | Wake-word on-device (mW-class micro model) → captured utterance → V-pipe verify (only the OWNER's voice may command) → control-IN. | V1 + firmware |
| **V3 ambient learning, live** | Only if still wanted after V-pipe proves value: wireless/continuous variants, opt-in sessions for guests, etc. Decided LAST. | TBD |

## 5. Hard rules this arc inherits (already-standing project law)

- **ALL training/ASR on the Main PC or cloud — NEVER on the box** (recorded rule, 2026-07-25). The
  box is a CPU-only seL4 appliance; it receives DISTILLED facts, not audio, not transcripts-in-bulk
  (its semantic store is 4096 × 512 B records — a distillation target, not an archive). The corpus
  lives on the Main PC.
- **Commands enter ONLY through control-IN** — voice becomes another signer in front of the existing
  HMAC + replay-floor + rate-limit + query-SHIELD channel, and inherits K-b: a voice command can
  never mint an action that is not on the static allowlist. No new inbound path to the box.
- Honest-ceiling wording discipline applies: this learns **observable things the owner said** —
  never "understands you", never "knows your preferences".

## 6. What this is NOT

Not a always-on cloud assistant; not a surveillance archive of other people (§3 forbids it
structurally); not a box feature (the box's only new surface would be distilled facts arriving over
the already-existing channels); not started — nothing in this document authorizes code.

## 7. Pointers

- Owner-side memory note: `project-wearable-voice-idea` (session memory, 2026-09-01).
- Related: `ROADMAP.md` §Beyond Phase 7 ("Mobile / edge", "Distributed JARVIS"); the control-IN
  security arc (`phase6/docs/PHASE_6_GOAL_6-5_CONTROL_IN.md`) — the door voice commands would use.
