"""How long a span has to be before the owner's threshold means anything.

M0b measured the owner's verification threshold (0.358503175300161) at an EER of 0.00 % on thirteen
held-out pieces of roughly ten seconds each. M1's pipeline then scored his own voice at 0.11 / 0.32 /
0.26 on one-to-three-second Whisper segments — under his own threshold — while nine-second segments
of the SAME file scored 0.44. A threshold is not a property of a voice on its own; it is a property
of a voice at a duration, and the pipeline had been applying a long-window threshold to short spans.

This module measures that dependence on data the owner has already given (his thirteen held-out
pieces against the self-test's 78 public negatives) and turns it into `MIN_EMBED_S`: the shortest
window length at which the STORED threshold still separates him from strangers. Nothing here moves
the threshold, and nothing is fitted to the throwaway that exposed the problem — the measurement is
on held-out data and the reading rule was fixed before the numbers existed.

The windows are SPEECH-PACKED: silence is removed first (the `split` frame mask) and D seconds of
speech are taken in order, because a Whisper segment's nominal duration counts pauses that carry no
voice. A window of "3 seconds" that is half silence would measure the wrong thing.

The first bench (2026-09-10, kept as `duration_bench_2026-09-10_atoms.json`) packed WHOLE RUNS and
was a STOP, not a result: every one of the owner's thirteen held-out pieces is a single continuous
10-17 s run after the 500 ms gap merge, so at every D up to 8 s the thirteen positives were the same
thirteen ten-second windows, the table was flat by construction and the rule returned the grid's
floor. `speech_windows_stream` replaces it here - the cut falls at exactly D along the concatenated
stream, splitting runs - and `latest_duration_bench` refuses any bench file that does not declare
that rule.

The pure parts are standard library only; the embedder, numpy and the corpus are imported inside the
bench, so the test suite exercises the window rule and the reading rule with no GPU (the M1 lesson).
"""
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

# Pre-registered before any number was seen (§0): the grid, and the rule that reads it.
DURATION_GRID = (1.0, 2.0, 3.0, 5.0, 8.0, 12.0)
MAX_FRR = 0.05          # the owner rejected at most 1 time in 20 …
MAX_FAR = 0.01          # … and a stranger accepted at most 1 time in 100

# The `split` mask settings the M0b pieces were cut with, reused so the windows are the same kind of
# speech the threshold was measured on.
FRAME_S = 0.05
FRAME_DBFS = -45.0
PAD_MS = 200.0
MIN_GAP_MS = 500.0


def speech_windows(mask_runs: Sequence[Tuple[int, int]], frame_s: float, d_s: float) -> List[List[int]]:
    """Consecutive speech-packed windows of at least `d_s` seconds, as lists of run INDICES.

    Walks `split.runs`'s output in order, accumulating speech until the window reaches `d_s`, then
    closes it. A trailing partial window is DROPPED rather than padded or kept short: a window that
    is not the length under test would contaminate the very measurement the length is being varied
    for. A window may overshoot when a single run is longer than `d_s` — the run is the atom, and
    splitting one would cut a word.
    """
    windows: List[List[int]] = []
    cur: List[int] = []
    acc = 0.0
    for i, (a, b) in enumerate(mask_runs):
        cur.append(i)
        acc += (b - a) * frame_s
        if acc >= d_s:
            windows.append(cur)
            cur, acc = [], 0.0
    return windows


def speech_windows_stream(mask_runs: Sequence[Tuple[int, int]], frame_s: float,
                          d_s: float) -> List[List[Tuple[int, int, int]]]:
    """Consecutive windows of EXACTLY `d_s` seconds of speech, cut along the concatenated stream.

    Each window is a list of `(run_index, frame_start, frame_end)` slices whose frame total is
    exactly `round(d_s / frame_s)`. A window may straddle a removed pause and a run may be SPLIT
    across two windows. The trailing partial window is dropped, for the same reason the atom rule
    drops it: a window that is not the length under test would contaminate the very measurement the
    length is being varied for.

    This is the bench's rule and it is deliberately not `speech_windows`'s. That one treats a run as
    an atom, which is right where it lives - packing enrollment pieces, where splitting one would
    cut a word out of the owner's voice. Applied to the bench it made every one of the owner's
    thirteen held-out pieces a SINGLE window at every D up to 8 s (each is one continuous 10-17 s
    run once the 500 ms gap merge has done its work), so the table was flat by construction and the
    reading rule returned the grid's floor from thirteen identical measurements.

    A Whisper segment is D seconds of CONTINUOUS speech, so D seconds of stream is the quantity
    under test. The limit is stated rather than hidden: a stream cut can fall inside a word, which
    can only lower a score, so the minimum this measures is conservative in the safe direction.
    """
    need = int(round(d_s / frame_s))
    if need <= 0:
        return []
    windows: List[List[Tuple[int, int, int]]] = []
    cur: List[Tuple[int, int, int]] = []
    have = 0
    for i, (a, b) in enumerate(mask_runs):
        pos = a
        while pos < b:
            take = min(b - pos, need - have)
            cur.append((i, pos, pos + take))
            pos += take
            have += take
            if have == need:
                windows.append(cur)
                cur, have = [], 0
    return windows


def choose_min_embed_s(table: Sequence[dict]) -> Optional[float]:
    """The SMALLEST D whose FRR and FAR at the stored threshold are both inside the bands.

    `table` is the per-D rows of the bench (`d_s`, `frr_at_stored`, `far_at_stored`). Smallest, not
    best: every extra second of required speech is a span the pipeline cannot attribute, so the rule
    takes the shortest length that still works rather than the safest one. Returns None when no D on
    the grid qualifies — which is a STOP, not a default, because it would mean the stored threshold
    itself is the question.
    """
    ok = [r for r in table
          if r.get("frr_at_stored") is not None and r.get("far_at_stored") is not None
          and r["frr_at_stored"] <= MAX_FRR and r["far_at_stored"] <= MAX_FAR]
    if not ok:
        return None
    return min(r["d_s"] for r in ok)


def latest_duration_bench(voice_home_dir=None) -> dict:
    """The newest date-named `duration_bench_<date>.json` — the pipeline's ONLY source of `MIN_EMBED_S`.

    The τ\\* precedent: a literal in the pipeline would be a number nobody could trace to a
    measurement, and this one exists precisely because an untraced assumption (that one threshold
    fits every span length) was wrong.
    """
    import json as _json
    from .paths import voice_home
    home = Path(voice_home_dir) if voice_home_dir else voice_home()
    # Date-shaped, not `duration_bench_*.json`: a superseded run is kept beside the live one under a
    # suffixed name (`..._atoms.json`), and a suffixed name must not be able to sort last and be
    # read as the answer. The refusal below is the second lock, on the file's own declared rule.
    files = sorted(home.glob("duration_bench_????-??-??.json"))
    if not files:
        raise SystemExit(
            "no duration_bench_<date>.json under %s - run `python -m jarvis_voice duration-bench` "
            "first; the minimum embedding duration is measured, never guessed" % home)
    payload = _json.loads(files[-1].read_text(encoding="utf-8"))
    rule = payload.get("window_rule")
    if rule != "stream":
        raise SystemExit(
            "%s declares window_rule=%r, not 'stream' - the pipeline will not read a minimum "
            "embedding duration from it. The atom-rule bench treated a speech run as an "
            "indivisible window, which made every held-out piece one window and the table flat; "
            "re-run `python -m jarvis_voice duration-bench` to measure on stream cuts."
            % (files[-1], rule))
    return payload


# ------------------------------------------------------------------ the bench

def _windows_of(path, d_s, embedder):
    """Every stream-cut window of one file, embedded. Returns a list of vectors."""
    import numpy as np
    from .audio import load_wav
    from .split import frame_rms_dbfs, runs, speech_mask

    wav, sr = load_wav(path)
    wav = np.asarray(wav, dtype="float32")
    dbfs = frame_rms_dbfs(wav, sr, FRAME_S)
    mask = speech_mask(dbfs, FRAME_DBFS, int(round(PAD_MS / 1000.0 / FRAME_S)),
                       int(round(MIN_GAP_MS / 1000.0 / FRAME_S)))
    rr = runs(mask)
    out = []
    fl = int(sr * FRAME_S)
    for win in speech_windows_stream(rr, FRAME_S, d_s):
        pieces = [wav[fs * fl: fe * fl] for (_i, fs, fe) in win]
        samples = np.concatenate(pieces) if pieces else np.zeros(0, dtype="float32")
        if len(samples) == 0:
            continue
        out.append(embedder.embed(samples, sr))
    return out


def run_duration_bench(out_path=None) -> dict:
    """The owner's threshold measured at every duration on the grid. Held-out data only."""
    import datetime as _dt
    import json as _json
    import time

    from .enroll import EnrollmentStore
    from .evaluate import eer, far_frr_at
    from .paths import ensure, voice_home
    from .speaker import SpeakerEmbedder
    from .verify import score as _score

    t_start = time.perf_counter()
    enr = EnrollmentStore().load()
    centroid, stored = enr["centroid"], float(enr["threshold"])

    heldout = sorted(p for p in ensure("heldout").glob("owner_heldout2_*.wav"))
    if not heldout:
        raise SystemExit("no owner_heldout2_*.wav under heldout\\ - M0b's admitted pieces are the "
                         "positives for this measurement")
    st_files = sorted(voice_home().glob("selftest_*.json"))
    if not st_files:
        raise SystemExit("no selftest_*.json - its sets.negatives are the public negatives")
    negatives = _json.loads(st_files[-1].read_text(encoding="utf-8"))["sets"]["negatives"]

    emb = SpeakerEmbedder()
    table = []
    for d in DURATION_GRID:
        t0 = time.perf_counter()
        # Per-file counts, kept because their absence is what let the first bench read as a
        # measurement: thirteen positives yielding thirteen windows at five different D values is
        # only visible one level down from n_pos.
        pos, wpf_pos = [], {}
        for p in heldout:
            vecs = _windows_of(p, d, emb)
            wpf_pos[Path(p).stem] = len(vecs)
            pos.extend(_score(centroid, v) for v in vecs)
        neg, wpf_neg = [], {}
        for p in negatives:
            vecs = _windows_of(p, d, emb)
            wpf_neg[Path(p).stem] = len(vecs)
            neg.extend(_score(centroid, v) for v in vecs)
        row = {"d_s": d, "n_pos": len(pos), "n_neg": len(neg),
               "windows_per_file": {"positives": wpf_pos, "negatives": wpf_neg},
               "seconds": round(time.perf_counter() - t0, 1)}
        if pos and neg:
            e, e_thr = eer(pos, neg)
            far, frr = far_frr_at(stored, pos, neg)
            row.update({"eer": e, "eer_threshold": e_thr,
                        "far_at_stored": far, "frr_at_stored": frr,
                        "pos_min": min(pos), "pos_max": max(pos),
                        "neg_min": min(neg), "neg_max": max(neg)})
        else:
            row.update({"eer": None, "eer_threshold": None, "far_at_stored": None,
                        "frr_at_stored": None, "pos_min": None, "pos_max": None,
                        "neg_min": None, "neg_max": None})
        table.append(row)

    min_embed = choose_min_embed_s(table)
    payload = {
        "created": _dt.datetime.now().isoformat(timespec="seconds"),
        "stored_threshold": stored,
        "window_rule": "stream",
        "grid": list(DURATION_GRID),
        "bands": {"max_frr": MAX_FRR, "max_far": MAX_FAR,
                  "rule": "MIN_EMBED_S = the smallest D with FRR <= 5 % and FAR <= 1 % at the "
                          "STORED threshold; None means no D on the grid qualifies (a STOP)"},
        "mask": {"frame_s": FRAME_S, "frame_dbfs": FRAME_DBFS, "pad_ms": PAD_MS,
                 "min_gap_ms": MIN_GAP_MS},
        "window_rule_note": ("Windows are cut at exactly D seconds along the CONCATENATED speech "
                             "stream, so a run may be split. A cut can fall inside a word, which "
                             "can only lower a score, so the minimum read from this table is "
                             "conservative in the safe direction."),
        "positives_files": len(heldout), "negatives_files": len(negatives),
        "ecapa_model": emb.model_id, "ecapa_device": emb.device,
        "ecapa_load_s": round(emb.load_s, 2), "speechbrain_version": emb.version,
        "table": table,
        "min_embed_s": min_embed,
        "wall_s": round(time.perf_counter() - t_start, 1),
        "scope": ("The owner's 13 M0b-admitted held-out pieces against the self-test's 78 public "
                  "negatives, speech-packed. Held-out data only; the stored threshold is measured "
                  "against, never moved, and nothing here is fitted to the M1 throwaway that "
                  "exposed the dependence."),
    }
    if out_path:
        out_path = Path(out_path)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(_json.dumps(payload, indent=1), encoding="utf-8")
    return payload
