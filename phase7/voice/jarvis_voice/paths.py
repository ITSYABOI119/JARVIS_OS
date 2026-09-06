"""The data layout — all of it outside the repo.

%USERPROFILE%\\.jarvis\\voice\\
    raw\\          recordings; deleted after transcription unless --keep
    enroll\\       the owner's enrollment clips + owner.json / owner.npy
    heldout\\      the owner's later-day clips (the M0b band is measured here only)
    heldout_sameday\\  same-day sanity pieces from the second natural recording (never the band)
    heldout_neg\\  consented household negatives, if the owner records any (evaluated separately)
    public\\       the downloaded public corpus (LibriSpeech dev-clean) + throwaway enrollments
    transcripts\\  one JSON per input (sha256, duration, model, timings, segments, deleted flag)
    models\\       the Hugging Face cache (HF_HOME) so nothing lands in the repo
    venv\\         the Python venv (created by hand, see the goal doc)
    enroll\\long\\ where `split --move-source-to` parks a long recording after its pieces are written

`JARVIS_VOICE_HOME` overrides the root (the tests point it at a temp dir). Standard library only.
"""
import os
from pathlib import Path

SUBDIRS = ("raw", "enroll", "heldout", "heldout_sameday", "heldout_neg", "public", "transcripts", "models")


def voice_home() -> Path:
    env = os.environ.get("JARVIS_VOICE_HOME")
    if env:
        return Path(env)
    return Path.home() / ".jarvis" / "voice"


def ensure(name: str) -> Path:
    """Return the sub-directory, creating it (and the root) on demand."""
    if name not in SUBDIRS:
        raise ValueError(f"unknown voice sub-directory {name!r}; one of {SUBDIRS}")
    p = voice_home() / name
    p.mkdir(parents=True, exist_ok=True)
    return p


def set_hf_home() -> str:
    """Point the Hugging Face cache under models\\ unless the operator set HF_HOME already.

    Also disable the hub cache's symlinks: Windows refuses to create them without a privilege
    (WinError 1314, measured 2026-09-06 on the faster-whisper-large-v3 snapshot), and the hub's
    documented fallback is HF_HUB_DISABLE_SYMLINKS=1 (files are moved/copied into the snapshot).
    """
    target = str(ensure("models"))
    os.environ.setdefault("HF_HOME", target)
    os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS", "1")
    os.environ.setdefault("HF_HUB_DISABLE_SYMLINKS_WARNING", "1")
    return os.environ["HF_HOME"]
