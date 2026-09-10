"""Transcription with the consent mechanism: the input WAV is deleted AFTER the transcript JSON is
written and fsync'd, unless keep=True. The JSON records the input's sha256 and `deleted: true|false`.

Since M1b.2 the deletion has a second gate: `segmentations_agree` compares two ASR passes over the
same audio, and `ingest` keeps the WAV when they differ. The audio is the only thing that can
regenerate a segmentation, so a segmentation that does not reproduce must not be the last one that
ever existed.

The deletion rule (`finalize`) is standard library only and is what test_voice_logic.py exercises;
the ASR call (`run_asr`) imports faster-whisper lazily.
"""
import datetime as _dt
import json
import re as _re
import os
import time
from pathlib import Path
from typing import Callable, Optional

from .paths import ensure

DEFAULT_MODEL = "large-v3"
DEFAULT_COMPUTE = "float16"

# The M0a `transcribe` command's settings, UNCHANGED: the M0a self-test's RTF 0.132 and its
# transcripts were measured with them, and moving them would silently re-base a recorded result.
M0A_ASR_OPTIONS = {"beam_size": 5, "vad_filter": False}

# The PIPELINE's settings, pinned at M1b.2. M1b transcribed a byte-identical input twice and got
# 3 segments once and 9 the other time; everything downstream follows the segmentation, and the
# audio is deleted after ingest, so an unreproducible segmentation makes the spine a one-shot
# record. `temperature=0.0` removes the sampling fallback that fires on a low-confidence window;
# `condition_on_previous_text=False` stops one window's output from being the next window's prompt,
# so a difference cannot propagate forward; `vad_filter=True` cuts the near-silent tails that
# produced M1b's 47.98 s segment on a 20.0 s file. Determinism is the goal, not accuracy: none of
# these is claimed to make the transcript better, only to make it the same twice.
PIPELINE_ASR_OPTIONS = {
    "temperature": 0.0,
    "beam_size": 5,
    "vad_filter": True,
    "condition_on_previous_text": False,
}

# Two ASR passes agree only if every boundary is within this. 10 ms is a quarter of Whisper's own
# 40 ms frame - tight enough that a genuinely different segmentation cannot slip through, loose
# enough that float rounding cannot fail a run.
ASR_GUARD_TOL_S = 0.010


def vad_defaults() -> dict:
    """The VAD settings a `vad_filter=True` run will actually use, read from faster-whisper itself.

    Recorded rather than re-declared: passing no `vad_parameters` means the library's Silero
    defaults apply, and reading them back from `VadOptions()` records the values in force instead of
    a copy that could drift from the library at the next upgrade. A non-finite value is stored as a
    string because JSON has no infinity and a transcript has to round-trip through strict JSON.
    """
    try:
        import dataclasses
        import math
        from faster_whisper.vad import VadOptions
        d = dataclasses.asdict(VadOptions())
    except Exception as exc:                       # no faster-whisper here (CI, the test suite)
        return {"unavailable": str(exc)}
    return {k: (str(v) if isinstance(v, float) and not math.isfinite(v) else v)
            for k, v in d.items()}


def segmentations_agree(first: dict, second: dict, tol_s: float = ASR_GUARD_TOL_S):
    """Do two ASR passes over the same audio agree? -> (bool, detail).

    Identical means the same segment count, every start and end within `tol_s`, and identical text.
    The detail carries counts and deltas and DELIBERATELY NO TEXT: it is written to the transcript
    JSON and printed to a terminal, and a mismatch report is not a licence to quote what was said.
    """
    a = first.get("segments") or []
    b = second.get("segments") or []
    detail = {"tol_s": tol_s, "n_segments": [len(a), len(b)],
              "max_start_delta_s": None, "max_end_delta_s": None, "text_identical": None}
    if len(a) != len(b):
        return False, detail
    ds = [abs(float(x["start"]) - float(y["start"])) for x, y in zip(a, b)]
    de = [abs(float(x["end"]) - float(y["end"])) for x, y in zip(a, b)]
    detail["max_start_delta_s"] = max(ds) if ds else 0.0
    detail["max_end_delta_s"] = max(de) if de else 0.0
    detail["text_identical"] = all((x.get("text") or "") == (y.get("text") or "")
                                   for x, y in zip(a, b))
    ok = bool(detail["text_identical"]) and detail["max_start_delta_s"] <= tol_s \
        and detail["max_end_delta_s"] <= tol_s
    return ok, detail


def write_json_fsync(path: Path, payload: dict) -> None:
    """Write via a temp file, fsync, then atomically replace — a partial JSON is never left behind."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with open(tmp, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, indent=1, ensure_ascii=False)
        fh.flush()
        os.fsync(fh.fileno())
    os.replace(tmp, path)


def finalize(input_path, json_path, payload: dict, keep: bool = False,
             writer: Callable[[Path, dict], None] = write_json_fsync,
             kept_by_guard: bool = False) -> dict:
    """Write the transcript JSON, then delete the input unless keep. Deletion happens ONLY after the
    writer returned without raising. On a write failure the input is left in place and the error
    propagates. The returned payload carries `deleted`.

    `kept_by_guard` is a SECOND, independent reason to keep the audio, recorded separately rather
    than folded into `keep`: `kept_by_request` has always meant "the operator asked", and a reader
    of a kept file has to be able to tell which of the two happened. Either one suppresses the
    deletion; only the operator's sets `kept_by_request`.
    """
    input_path = Path(input_path)
    json_path = Path(json_path)
    payload = dict(payload)
    payload["deleted"] = False
    payload["kept_by_request"] = bool(keep)
    payload["kept_by_guard"] = bool(kept_by_guard)
    writer(json_path, payload)          # raises on failure -> input untouched
    if not (keep or kept_by_guard):
        os.remove(input_path)
        payload["deleted"] = True
        writer(json_path, payload)      # record the deletion in the same JSON
    return payload


STAMP_RE = _re.compile(r"(20\d{2})(\d{2})(\d{2})[_-](\d{2})(\d{2})(\d{2})")


def resolve_started_at(path, argument=None, duration_s=None, mtime=None):
    """When the recording STARTED -> (iso string, source). Pure; the caller supplies mtime.

    Precedence argument > filename stamp > mtime - duration, and the SOURCE is recorded beside the
    value because the three are not equally trustworthy: the argument is the operator's own
    statement, the `rec_YYYYmmdd_HHMMSS` stamp is written by `record` at the moment it starts, and
    mtime - duration is an inference from when the file stopped being written. A span's `said_at`
    is derived from this, and MS2 will reason about days from it, so a reader must be able to tell
    which of the three produced it.
    """
    if argument:
        return str(argument), "argument"
    m = STAMP_RE.search(Path(path).stem)
    if m:
        y, mo, d, h, mi, sec = (int(x) for x in m.groups())
        return _dt.datetime(y, mo, d, h, mi, sec).astimezone().isoformat(timespec="seconds"), "filename"
    if mtime is None:
        mtime = os.path.getmtime(path)
    start = _dt.datetime.fromtimestamp(mtime) - _dt.timedelta(seconds=float(duration_s or 0.0))
    return start.astimezone().isoformat(timespec="seconds"), "mtime"


class ASR:
    """faster-whisper on CUDA (CTranslate2). Loaded once."""

    def __init__(self, model: str = DEFAULT_MODEL, compute_type: str = DEFAULT_COMPUTE, device: str = "cuda"):
        from .paths import set_hf_home
        set_hf_home()
        from faster_whisper import WhisperModel
        import faster_whisper
        self.model_name, self.compute_type, self.device = model, compute_type, device
        self.version = getattr(faster_whisper, "__version__", "?")
        self.model = WhisperModel(model, device=device, compute_type=compute_type,
                                  download_root=os.environ.get("HF_HOME"))
        self.vad_parameters = vad_defaults()

    def run(self, wav_path, options: Optional[dict] = None) -> dict:
        """One decode. `options` are the transcribe kwargs; None means the M0a command's settings.

        The caller names the settings rather than the method choosing them, because two callers
        want different ones and both are recorded results: `transcribe` is M0a's command and its
        RTF was measured at `M0A_ASR_OPTIONS`, while `ingest` runs the pipeline and needs
        `PIPELINE_ASR_OPTIONS`. Whichever ran is written into the payload.
        """
        opts = dict(M0A_ASR_OPTIONS if options is None else options)
        t0 = time.perf_counter()
        segments, info = self.model.transcribe(str(wav_path), **opts)
        # avg_logprob and no_speech_prob are kept because the span row stores an ASR confidence and
        # the audio will not exist to re-check it: a low-confidence span has to be recognisable as
        # one from the spine alone, forever.
        segs = [{"start": s.start, "end": s.end, "text": s.text,
                 "avg_logprob": getattr(s, "avg_logprob", None),
                 "no_speech_prob": getattr(s, "no_speech_prob", None)}
                for s in segments]  # generator -> list
        wall = time.perf_counter() - t0
        return {"model": self.model_name, "compute_type": self.compute_type, "device": self.device,
                "asr_options": opts,
                "vad_parameters": getattr(self, "vad_parameters", None),
                "faster_whisper_version": self.version, "language": info.language,
                "language_probability": info.language_probability, "duration_s": info.duration,
                "wall_s": wall, "rtf": (wall / info.duration) if info.duration else None,
                "segments": segs, "text": "".join(s["text"] for s in segs).strip()}


def transcribe(path, keep: bool = False, asr: Optional[ASR] = None, out_dir: Optional[Path] = None) -> dict:
    """Transcribe one WAV under raw\\ (or anywhere), write transcripts\\<stem>.json, delete the input
    unless keep. Returns the payload (with `deleted`)."""
    from .audio import sha256_file
    import torch

    path = Path(path)
    out_dir = Path(out_dir) if out_dir else ensure("transcripts")
    engine = asr or ASR()
    if torch.cuda.is_available():
        torch.cuda.reset_peak_memory_stats()
    result = engine.run(path)
    vram = torch.cuda.max_memory_allocated() if torch.cuda.is_available() else None
    payload = {
        "input": str(path), "input_sha256": sha256_file(path),
        "created": _dt.datetime.now().isoformat(timespec="seconds"),
        "torch_vram_peak_bytes": vram,
        **result,
    }
    json_path = out_dir / (path.stem + ".json")
    return finalize(path, json_path, payload, keep=keep)
