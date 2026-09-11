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

# The sample floor, added at M1a.4 after the rule without one returned 12.0 s from a row of TWO
# positive windows. A rate needs a denominator to be a rate: at n_pos = 14 the finest non-zero FRR a
# row can express is 1/14 = 0.0714, already outside MAX_FRR, so such a row can only pass at exactly
# zero — and the D with the fewest windows is mechanically the likeliest to manage it. Twenty is the
# smallest n at which a single rejection (1/20 = 0.05) still sits inside the band, so it is the
# smallest floor at which the band means what it says rather than "no failures were observed".
# A row under the floor is REPORTED in the table and never read by the rule.
MIN_N = 20

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


def row_has_sample_floor(row: dict, min_n: int = MIN_N) -> bool:
    """Does this row have enough windows on BOTH sides for its rates to be read?

    Both sides, not either: FRR needs positives and FAR needs negatives, and a row that meets the
    bands on one side while the other is a handful of windows is not a measurement of the pair.
    """
    return (row.get("n_pos") or 0) >= min_n and (row.get("n_neg") or 0) >= min_n


# The run-length buckets M1d.1 reports. They are the two-level owner rule's own boundaries read
# back onto real speech: a run of 10 s or more is a turn the owner's threshold can be ASKED about,
# 2-10 s is embedded and clustered but never judged alone, and under 2 s is attributed by adjacency
# and carries no evidence at all. So the three shares answer, in one measurement, how much of six
# hours of real life each level of the rule can reach.
SHAPE_LONG_S = 10.0
SHAPE_SHORT_S = 2.0


def speech_shape(mask_runs, frame_s: float) -> dict:
    """The run-length shape of one recording's speech. Pure, stdlib only.

    Shares are of SPEECH SECONDS, not of runs: twenty half-second runs and one ten-second run are
    the same count and very different amounts of attributable speech.
    """
    lens = [max(0.0, (b - a) * float(frame_s)) for a, b in mask_runs]
    total = sum(lens)
    ge10 = sum(x for x in lens if x >= SHAPE_LONG_S)
    lt2 = sum(x for x in lens if x < SHAPE_SHORT_S)
    mid = total - ge10 - lt2
    share = (lambda x: (x / total) if total else 0.0)
    return {"speech_s": round(total, 3), "runs": len(lens),
            "longest_run_s": round(max(lens), 3) if lens else 0.0,
            "share_ge_10": share(ge10), "share_2_10": share(mid), "share_lt_2": share(lt2),
            "seconds_ge_10": round(ge10, 3), "seconds_2_10": round(mid, 3),
            "seconds_lt_2": round(lt2, 3)}


def collect_positive_files(dirs=None, globs=None, default=None):
    """The positive WAVs a run was told to use, sorted, with duplicates REFUSED.

    Refused rather than de-duplicated: a file named twice would carry twice its weight in every
    rate the bench computes, and the caller meant something by naming it twice. `default` is used
    only when neither dirs nor globs is given, so the thirteen M0b pieces stay the default set.
    """
    import glob as _glob
    if not dirs and not globs:
        return sorted(Path(p) for p in (default or []))
    seen, out = {}, []
    for d in (dirs or []):
        for f in sorted(Path(d).glob("*.wav")):
            out.append(f)
    for g in (globs or []):
        for f in sorted(Path(x) for x in _glob.glob(str(g))):
            out.append(f)
    for f in out:
        key = str(Path(f).resolve()).lower()
        if key in seen:
            raise SystemExit(
                "%s is named twice by --pos-dirs/--pos-files - refusing, because a file counted "
                "twice carries twice its weight in every rate this bench computes" % f)
        seen[key] = True
    return sorted(out)


# How far below the others a file's mean may sit before it is FLAGGED (never dropped). M1d.2 scores
# twelve 15-minute chunks recorded unattended; if one of them is mostly someone else, its mean sits
# well below its siblings and that must be visible rather than averaged in silently.
FILE_FLAG_MARGIN = 0.15


def flag_outlier_files(means: dict, margin: float = FILE_FLAG_MARGIN) -> list:
    """Files whose mean score sits more than `margin` below the mean of the OTHER files' means.

    The comparison is against the MEAN of the others, not their maximum: one unusually strong file
    would otherwise drag every merely-average file over the line, which is the opposite of what a
    flag for "possibly not the owner" is for. Sorted, so the report is stable.
    """
    keys = [k for k, v in means.items() if v is not None]
    if len(keys) < 3:
        return []
    out = []
    for k in keys:
        others = [means[o] for o in keys if o != k]
        if means[k] < (sum(others) / len(others)) - float(margin):
            out.append(k)
    return sorted(out)


def choose_min_embed_s(table: Sequence[dict], min_n: int = MIN_N) -> Optional[float]:
    """The SMALLEST D whose FRR and FAR at the stored threshold are both inside the bands.

    `table` is the per-D rows of the bench (`d_s`, `n_pos`, `n_neg`, `frr_at_stored`,
    `far_at_stored`). Smallest, not best: every extra second of required speech is a span the
    pipeline cannot attribute, so the rule takes the shortest length that still works rather than
    the safest one. Returns None when no D on the grid qualifies — which is a STOP, not a default,
    because it would mean the stored threshold itself is the question.

    **A row under the sample floor is not eligible, whatever its rates.** M1a.3's table returned
    12.0 s from a row holding two positive windows: FRR 0/2 is a true zero and no evidence, and
    the rule as written preferred it precisely because it was the sparsest row on the grid. The
    floor is applied BEFORE the bands so a zero on nothing can never win.
    """
    ok = [r for r in table
          if row_has_sample_floor(r, min_n)
          and r.get("frr_at_stored") is not None and r.get("far_at_stored") is not None
          and r["frr_at_stored"] <= MAX_FRR and r["far_at_stored"] <= MAX_FAR]
    if not ok:
        return None
    return min(r["d_s"] for r in ok)


def latest_duration_bench_path(voice_home_dir=None) -> Path:
    """The newest date-named bench FILE, or a refusal.

    Date-shaped, not `duration_bench_*.json`: a superseded run is kept beside the live one under a
    suffixed name (`..._atoms.json`), and a suffixed name must not be able to sort last and be read
    as the answer. Split out from `latest_duration_bench` at M1a.4 so the re-read can rewrite the
    very file the reader would have read, rather than a file it found by its own second glob.
    """
    from .paths import voice_home
    home = Path(voice_home_dir) if voice_home_dir else voice_home()
    files = sorted(home.glob("duration_bench_????-??-??.json"))
    if not files:
        raise SystemExit(
            "no duration_bench_<date>.json under %s - run `python -m jarvis_voice duration-bench` "
            "first; the minimum embedding duration is measured, never guessed" % home)
    return files[-1]


def latest_duration_bench(voice_home_dir=None) -> dict:
    """The newest date-named `duration_bench_<date>.json` — the pipeline's ONLY source of `MIN_EMBED_S`.

    The τ\\* precedent: a literal in the pipeline would be a number nobody could trace to a
    measurement, and this one exists precisely because an untraced assumption (that one threshold
    fits every span length) was wrong.
    """
    import json as _json
    path = latest_duration_bench_path(voice_home_dir)
    payload = _json.loads(path.read_text(encoding="utf-8"))
    rule = payload.get("window_rule")
    if rule != "stream":
        raise SystemExit(
            "%s declares window_rule=%r, not 'stream' - the pipeline will not read a minimum "
            "embedding duration from it. The atom-rule bench treated a speech run as an "
            "indivisible window, which made every held-out piece one window and the table flat; "
            "re-run `python -m jarvis_voice duration-bench` to measure on stream cuts."
            % (path, rule))
    return payload


def reread_duration_bench(voice_home_dir=None, min_n: int = MIN_N) -> dict:
    """Re-apply the CURRENT reading rule to the newest stream-rule bench and rewrite its verdict.

    The TABLE is never touched — it is the measurement, and a measurement does not change when the
    rule for reading it does. Only `min_embed_s` is recomputed, `reading_rule` is written beside it
    so the file says which rule produced it, and a value that changes is preserved as `retracted`
    rather than deleted: a number that was published and withdrawn is more useful in the record than
    a number that quietly stopped existing.

    Returns the summary the CLI prints. Re-running it is idempotent: a second pass recomputes the
    same verdict and, because the value no longer changes, leaves `retracted` as it stands.
    """
    import json as _json
    path = latest_duration_bench_path(voice_home_dir)
    payload = latest_duration_bench(voice_home_dir)
    before = payload.get("min_embed_s")
    table_before = _json.dumps(payload.get("table"), sort_keys=True)
    after = choose_min_embed_s(payload.get("table") or [], min_n)
    payload["min_embed_s"] = after
    payload["reading_rule"] = {"max_frr": MAX_FRR, "max_far": MAX_FAR, "min_n": min_n}
    if before != after and before is not None:
        payload["retracted"] = before
    if _json.dumps(payload.get("table"), sort_keys=True) != table_before:
        raise SystemExit("the re-read must not touch the table - refusing to write")
    path.write_text(_json.dumps(payload, indent=1), encoding="utf-8")
    return {"path": str(path), "before": before, "after": after,
            "retracted": payload.get("retracted"),
            "reading_rule": payload["reading_rule"],
            "rows_under_floor": [r["d_s"] for r in (payload.get("table") or [])
                                 if not row_has_sample_floor(r, min_n)]}


# ------------------------------------------------------------------ the bench

def load_and_mask(path):
    """(wav, sr, runs) for one file under the bench's fixed mask. Decoded and masked ONCE.

    Separated from the embedding at M1d.2 because the positive set grew from 147 seconds to six
    hours: decoding and masking every file once per D on the grid would read the same three hours
    of audio six times over for no new information. The mask is a property of the recording; only
    the window length varies.
    """
    import numpy as np
    from .audio import load_wav
    from .split import frame_rms_dbfs, runs, speech_mask

    wav, sr = load_wav(path)
    wav = np.asarray(wav, dtype="float32")
    dbfs = frame_rms_dbfs(wav, sr, FRAME_S)
    mask = speech_mask(dbfs, FRAME_DBFS, int(round(PAD_MS / 1000.0 / FRAME_S)),
                       int(round(MIN_GAP_MS / 1000.0 / FRAME_S)))
    return wav, sr, runs(mask)


def embed_windows(wav, sr, mask_runs, d_s, embedder):
    """Every stream-cut window of `d_s` seconds from an already-masked file, embedded."""
    import numpy as np
    out = []
    fl = int(sr * FRAME_S)
    for win in speech_windows_stream(mask_runs, FRAME_S, d_s):
        pieces = [wav[fs * fl: fe * fl] for (_i, fs, fe) in win]
        samples = np.concatenate(pieces) if pieces else np.zeros(0, dtype="float32")
        if len(samples) == 0:
            continue
        out.append(embedder.embed(samples, sr))
    return out


def _windows_of(path, d_s, embedder):
    """Every stream-cut window of one file, embedded. The one-shot form of the two above."""
    wav, sr, rr = load_and_mask(path)
    return embed_windows(wav, sr, rr, d_s, embedder)


# The grid row whose per-file means are compared. Eight seconds is the longest length at which
# every M0b piece still yields a window, so every positive file has a mean to compare.
FLAG_D_S = 8.0


def run_duration_bench(out_path=None, pos_dirs=None, pos_files=None,
                       positives_source=None) -> dict:
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

    heldout = collect_positive_files(
        pos_dirs, pos_files,
        default=sorted(p for p in ensure("heldout").glob("owner_heldout2_*.wav")))
    if not heldout:
        raise SystemExit("no positive files - the default is M0b's admitted pieces under "
                         "heldout\\; name others with --pos-dirs / --pos-files")
    for f in heldout:
        if not Path(f).exists():
            raise SystemExit("no such positive file: %s" % f)
    st_files = sorted(voice_home().glob("selftest_*.json"))
    if not st_files:
        raise SystemExit("no selftest_*.json - its sets.negatives are the public negatives")
    negatives = _json.loads(st_files[-1].read_text(encoding="utf-8"))["sets"]["negatives"]

    emb = SpeakerEmbedder()
    # Per-file counts, kept because their absence is what let the first bench read as a
    # measurement: thirteen positives yielding thirteen windows at five different D values is
    # only visible one level down from n_pos.
    acc = {d: {"pos": [], "neg": [], "wpf_pos": {}, "wpf_neg": {}, "seconds": 0.0}
           for d in DURATION_GRID}
    per_file_scores = {}                       # at FLAG_D_S, for the outlier flag
    shapes = {}
    for side, files in (("pos", heldout), ("neg", negatives)):
        for p in files:
            t0 = time.perf_counter()
            wav, sr, rr = load_and_mask(p)
            if side == "pos":
                shapes[Path(p).stem] = speech_shape(rr, FRAME_S)
            for d in DURATION_GRID:
                t1 = time.perf_counter()
                vecs = embed_windows(wav, sr, rr, d, emb)
                sc = [_score(centroid, v) for v in vecs]
                acc[d][side].extend(sc)
                acc[d]["wpf_" + side][Path(p).stem] = len(vecs)
                acc[d]["seconds"] += time.perf_counter() - t1
                if side == "pos" and d == FLAG_D_S:
                    per_file_scores[Path(p).stem] = (sum(sc) / len(sc)) if sc else None
            del wav
            _ = t0

    table = []
    for d in DURATION_GRID:
        pos, neg = acc[d]["pos"], acc[d]["neg"]
        wpf_pos, wpf_neg = acc[d]["wpf_pos"], acc[d]["wpf_neg"]
        row = {"d_s": d, "n_pos": len(pos), "n_neg": len(neg),
               "windows_per_file": {"positives": wpf_pos, "negatives": wpf_neg},
               "seconds": round(acc[d]["seconds"], 1)}
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
        "positives_source": positives_source or "M0b's 13 admitted pieces (the default)",
        "positives_list": [str(Path(f)) for f in heldout],
        "per_file_mean_score": {"d_s": FLAG_D_S, "margin": FILE_FLAG_MARGIN,
                                "means": per_file_scores,
                                "flagged": flag_outlier_files(per_file_scores)},
        "positives_speech_shape": shapes,
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
