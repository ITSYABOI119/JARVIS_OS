"""jarvis_memory — the household memory store (Phase 7 goal 8).

The spec is phase7/docs/PHASE_7_MEMORY_DESIGN.md. MS0 is the standard-library half: the pure
decision core (registry, candidate validation, confidence, freshness, rules R1-R7, the ranker,
the personhood and purge rules) and the SQLite store with its full-text lane.

Nothing here imports a model, a GPU runtime, numpy or any third-party package. The extractor and
the embedding lane land at MS1 and live behind their own modules; importing this package must stay
free of them so the tests and CI can run on a bare python3.
"""

__all__ = [
    "registry",
    "candidate",
    "confidence",
    "freshness",
    "rules",
    "retrieve",
    "people",
]
