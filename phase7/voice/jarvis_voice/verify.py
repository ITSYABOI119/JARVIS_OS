"""Verification = one decision per clip: cos(centroid, clip) >= threshold -> owner.

The pure parts (score, decide, the short-clip rule) are standard library only; verify_clip imports
the audio and speaker stacks lazily.
"""
import math
from typing import Sequence

MIN_CLIP_S = 2.0   # clips shorter than this are REFUSED (a finding), never silently scored


def _l2(v: Sequence[float]) -> float:
    return math.sqrt(sum(x * x for x in v))


def score(centroid: Sequence[float], emb: Sequence[float]) -> float:
    """Cosine similarity; both inputs are re-normalised so the caller's normalisation cannot bias it."""
    if len(centroid) != len(emb):
        raise ValueError(f"dimension mismatch: centroid {len(centroid)} vs embedding {len(emb)}")
    na, nb = _l2(centroid), _l2(emb)
    if na == 0.0 or nb == 0.0:
        raise ValueError("zero vector")
    return sum(a * b for a, b in zip(centroid, emb)) / (na * nb)


def decide(score_value: float, threshold: float) -> bool:
    """owner iff score >= threshold (the boundary is accepted, matching far_frr_at)."""
    return score_value >= threshold


def refused_for_length(duration_s: float, min_s: float = MIN_CLIP_S) -> bool:
    """True when the clip is too short to score (strictly shorter than min_s)."""
    return duration_s < min_s


def verify_clip(path, enrollment=None, embedder=None) -> dict:
    """Score one WAV against the stored enrollment. Always returns score, threshold and decision together."""
    from .audio import load_wav, duration_s
    from .enroll import EnrollmentStore
    from .speaker import SpeakerEmbedder

    enr = enrollment or EnrollmentStore().load()
    wav, sr = load_wav(path)
    dur = duration_s(wav, sr)
    if refused_for_length(dur):
        return {"path": str(path), "duration_s": dur, "refused": True,
                "reason": f"clip shorter than {MIN_CLIP_S} s", "score": None,
                "threshold": enr["threshold"], "owner": None}
    emb = (embedder or SpeakerEmbedder()).embed(wav, sr)
    s = score(enr["centroid"], emb)
    return {"path": str(path), "duration_s": dur, "refused": False, "score": s,
            "threshold": enr["threshold"], "owner": decide(s, enr["threshold"])}
