"""jarvis_voice — Phase 7 goal 8, M0a: the owner-voice tooling on the Main PC.

record / enroll / verify / transcribe / evaluate / selftest. Everything personal (recordings,
embeddings, transcripts, corpora, models) lives OUTSIDE the repo under %USERPROFILE%\\.jarvis\\voice\\
(see paths.py). The pure logic modules (enroll, verify, evaluate, transcribe's deletion rule) import
nothing beyond the standard library at module level so `test_voice_logic.py` runs on a bare python3.

Honesty: this tooling recognises the owner's voice against others at a MEASURED rate. It never
"knows" or "understands" anyone.
"""

__version__ = "0.1.0"
