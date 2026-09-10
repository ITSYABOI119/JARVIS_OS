"""Transcription with the consent mechanism: the input WAV is deleted AFTER the transcript JSON is
written and fsync'd, unless keep=True. The JSON records the input's sha256 and `deleted: true|false`.

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
             writer: Callable[[Path, dict], None] = write_json_fsync) -> dict:
    """Write the transcript JSON, then delete the input unless keep. Deletion happens ONLY after the
    writer returned without raising. On a write failure the input is left in place and the error
    propagates. The returned payload carries `deleted`."""
    input_path = Path(input_path)
    json_path = Path(json_path)
    payload = dict(payload)
    payload["deleted"] = False
    payload["kept_by_request"] = bool(keep)
    writer(json_path, payload)          # raises on failure -> input untouched
    if not keep:
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

    def run(self, wav_path) -> dict:
        t0 = time.perf_counter()
        segments, info = self.model.transcribe(str(wav_path), beam_size=5, vad_filter=False)
        # avg_logprob and no_speech_prob are kept because the span row stores an ASR confidence and
        # the audio will not exist to re-check it: a low-confidence span has to be recognisable as
        # one from the spine alone, forever.
        segs = [{"start": s.start, "end": s.end, "text": s.text,
                 "avg_logprob": getattr(s, "avg_logprob", None),
                 "no_speech_prob": getattr(s, "no_speech_prob", None)}
                for s in segments]  # generator -> list
        wall = time.perf_counter() - t0
        return {"model": self.model_name, "compute_type": self.compute_type, "device": self.device,
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
