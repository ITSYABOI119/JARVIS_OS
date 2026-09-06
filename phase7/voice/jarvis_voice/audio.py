"""Audio I/O: 16 kHz mono 16-bit WAV in and out. numpy / soundfile / sounddevice imported lazily."""
import hashlib
from pathlib import Path

TARGET_SR = 16000


def sha256_file(path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def load_wav(path):
    """Return (float32 mono ndarray, sample_rate)."""
    import numpy as np
    import soundfile as sf
    data, sr = sf.read(str(path), dtype="float32", always_2d=True)
    return np.ascontiguousarray(data.mean(axis=1)), sr


def resample_to_16k(wav, sr: int):
    if sr == TARGET_SR:
        return wav, sr
    import numpy as np
    import torch
    import torchaudio.functional as F
    out = F.resample(torch.from_numpy(np.asarray(wav, dtype="float32")), sr, TARGET_SR).numpy()
    return out, TARGET_SR


def duration_s(wav, sr: int) -> float:
    return float(len(wav)) / float(sr)


def write_wav(path, wav, sr: int) -> Path:
    import soundfile as sf
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(path), wav, sr, subtype="PCM_16")
    return path


def list_devices() -> str:
    import sounddevice as sd
    return str(sd.query_devices())


def record(seconds: float, device, out_path) -> Path:
    """Record from `device` (index or None = default input) at 16 kHz mono and write a PCM_16 WAV."""
    import numpy as np
    import sounddevice as sd
    frames = int(seconds * TARGET_SR)
    audio = sd.rec(frames, samplerate=TARGET_SR, channels=1, dtype="float32", device=device)
    sd.wait()
    return write_wav(out_path, np.ascontiguousarray(audio[:, 0]), TARGET_SR)
