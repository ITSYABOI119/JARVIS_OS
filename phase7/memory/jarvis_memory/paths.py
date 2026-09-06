"""The data layout — all of it outside the repo (the voice package's paths.py precedent).

%USERPROFILE%\\.jarvis\\memory\\
    household.sqlite   the store; WAL mode, so -wal and -shm sit beside it
    bench\\            benchmark corpora and per-run outputs
    exports\\          nightly copies with their md5s (the design's §12 restore path)

`JARVIS_MEMORY_HOME` overrides the root; the tests point it at a temp directory and the benchmark
uses in-memory databases, so a test run never touches the operator's household file.

Standard library only. Nothing here creates the root unless asked: importing the package on a
machine that has never run the store must not leave a directory behind.
"""
import os
from pathlib import Path

SUBDIRS = ("bench", "exports")
DB_NAME = "household.sqlite"


def memory_home() -> Path:
    env = os.environ.get("JARVIS_MEMORY_HOME")
    if env:
        return Path(env)
    return Path.home() / ".jarvis" / "memory"


def ensure(name: str) -> Path:
    """Return the sub-directory, creating it (and the root) on demand."""
    if name not in SUBDIRS:
        raise ValueError(f"unknown memory sub-directory {name!r}; one of {SUBDIRS}")
    p = memory_home() / name
    p.mkdir(parents=True, exist_ok=True)
    return p


def default_db() -> Path:
    """The household database path. Creates the root but not the file."""
    root = memory_home()
    root.mkdir(parents=True, exist_ok=True)
    return root / DB_NAME
