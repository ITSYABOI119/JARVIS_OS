"""Enrollment = a centroid (mean of L2-normalised clip embeddings) + the per-clip embeddings.

Stored as enroll\\owner.json (metadata: model, dim, clip list with sha256 + durations, threshold,
created, plus the centroid and clip vectors as plain lists) and, when numpy is importable,
enroll\\owner.npy (the same vectors as a float32 array for fast reload). Standard library only at
module level.
"""
import datetime as _dt
import json
import math
import os
from pathlib import Path
from typing import List, Optional, Sequence

from .paths import ensure

MIN_ENROLL_SECONDS = 60.0
MIN_ENROLL_CLIPS = 3


def l2_normalise(v: Sequence[float]) -> List[float]:
    n = math.sqrt(sum(x * x for x in v))
    if n == 0.0:
        raise ValueError("zero vector cannot be normalised")
    return [x / n for x in v]


def build_centroid(embeddings: Sequence[Sequence[float]]) -> List[float]:
    """Mean of the L2-normalised embeddings, itself L2-normalised."""
    if not embeddings:
        raise ValueError("no embeddings")
    dim = len(embeddings[0])
    acc = [0.0] * dim
    for e in embeddings:
        if len(e) != dim:
            raise ValueError("embedding dimension mismatch")
        for i, x in enumerate(l2_normalise(e)):
            acc[i] += x
    return l2_normalise([x / len(embeddings) for x in acc])


class EnrollmentStore:
    """owner.json (+ owner.npy when numpy is available) under enroll\\ — or any directory given."""

    def __init__(self, directory: Optional[Path] = None, name: str = "owner"):
        self.dir = Path(directory) if directory else ensure("enroll")
        self.name = name

    @property
    def json_path(self) -> Path:
        return self.dir / f"{self.name}.json"

    @property
    def npy_path(self) -> Path:
        return self.dir / f"{self.name}.npy"

    def save(self, centroid: Sequence[float], clips: Sequence[dict], clip_vectors: Sequence[Sequence[float]],
             threshold: float, model: str, extra: Optional[dict] = None) -> Path:
        self.dir.mkdir(parents=True, exist_ok=True)
        payload = {
            "name": self.name,
            "model": model,
            "dim": len(centroid),
            "threshold": threshold,
            "created": _dt.datetime.now().isoformat(timespec="seconds"),
            "clips": list(clips),                       # each: {path, sha256, duration_s}
            "centroid": [float(x) for x in centroid],
            "clip_vectors": [[float(x) for x in v] for v in clip_vectors],
        }
        if extra:
            payload.update(extra)
        tmp = self.json_path.with_suffix(".json.tmp")
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(payload, fh, indent=1)
            fh.flush()
            os.fsync(fh.fileno())
        os.replace(tmp, self.json_path)
        try:
            import numpy as np  # optional fast path; the JSON is the record
            np.save(self.npy_path, np.asarray([payload["centroid"]] + payload["clip_vectors"], dtype="float32"))
        except ImportError:
            pass
        return self.json_path

    def load(self) -> dict:
        with open(self.json_path, "r", encoding="utf-8") as fh:
            return json.load(fh)

    def exists(self) -> bool:
        return self.json_path.exists()
