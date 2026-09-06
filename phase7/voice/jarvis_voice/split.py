"""Speech extraction + packing for long natural recordings (the pure logic; I/O lives in __main__).

The algorithm, exactly:
  Frames:  frame_rms_dbfs(wav, sr, frame_s=0.05): fl = int(sr * frame_s) samples per frame (800 at
           16 kHz), nf = len(wav) // fl frames, the partial tail frame dropped; RMS per frame in float32;
           dBFS = 20 * log10(max(rms, 1e-9)).
  Mask:    speech_mask(frame_dbfs, threshold_dbfs, pad_frames, min_gap_frames): a frame is loud iff
           dbfs > threshold_dbfs (STRICT). Step 1: every maximal run of loud frames [i, j) is extended
           to [max(0, i - pad), min(n, j + pad)). Step 2: every maximal run of non-speech frames that
           lies STRICTLY between two speech runs (never a leading or trailing stretch) and is shorter
           than min_gap_frames (length < min_gap) becomes speech.
  Runs:    runs(mask): maximal True stretches as half-open frame ranges, in order.
  Packing: pack_runs(run_lengths_s, target_s, min_keep_s): greedy, in order: append run indices to the
           current piece, adding their lengths; the moment the piece's total is >= target_s the piece
           is closed and a new one starts. After the last run, the open piece is kept iff its total is
           >= min_keep_s, else dropped. Returns the pieces as lists of run indices.
Assembly (in __main__): run (a, b) covers samples [a * fl, b * fl); a piece is the concatenation of its
runs' sample spans in order. Run length in seconds = frames * frame_s.

Standard library only at module level; frame_rms_dbfs imports numpy inside the function.
"""
from typing import List, Sequence, Tuple


def frame_rms_dbfs(wav, sr: int, frame_s: float = 0.05) -> List[float]:
    import math
    import numpy as np
    x = np.asarray(wav, dtype="float32")
    fl = int(sr * frame_s)
    nf = len(x) // fl
    if nf == 0:
        return []
    frames = x[: nf * fl].reshape(nf, fl)
    rms = np.sqrt((frames.astype("float32") ** 2).mean(axis=1))
    return [20.0 * math.log10(max(float(r), 1e-9)) for r in rms]


def speech_mask(frame_dbfs: Sequence[float], threshold_dbfs: float, pad_frames: int, min_gap_frames: int) -> List[bool]:
    n = len(frame_dbfs)
    loud = [d > threshold_dbfs for d in frame_dbfs]
    mask = [False] * n
    # step 1: pad every maximal loud run
    i = 0
    while i < n:
        if loud[i]:
            j = i
            while j < n and loud[j]:
                j += 1
            for k in range(max(0, i - pad_frames), min(n, j + pad_frames)):
                mask[k] = True
            i = j
        else:
            i += 1
    # step 2: merge interior gaps shorter than min_gap_frames
    speech_runs = runs(mask)
    for (a0, b0), (a1, _b1) in zip(speech_runs, speech_runs[1:]):
        gap = a1 - b0
        if gap < min_gap_frames:
            for k in range(b0, a1):
                mask[k] = True
    return mask


def runs(mask: Sequence[bool]) -> List[Tuple[int, int]]:
    out: List[Tuple[int, int]] = []
    i, n = 0, len(mask)
    while i < n:
        if mask[i]:
            j = i
            while j < n and mask[j]:
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def pack_runs(run_lengths_s: Sequence[float], target_s: float, min_keep_s: float) -> List[List[int]]:
    pieces: List[List[int]] = []
    cur: List[int] = []
    total = 0.0
    for idx, length in enumerate(run_lengths_s):
        cur.append(idx)
        total += length
        if total >= target_s:
            pieces.append(cur)
            cur, total = [], 0.0
    if cur and total >= min_keep_s:
        pieces.append(cur)
    return pieces
