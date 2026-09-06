"""CLI: python -m jarvis_voice <record|enroll|verify|transcribe|evaluate|split|selftest> ...

split: extract speech from a long 16 kHz recording (energy gate, padded, short gaps merged) and pack
whole runs into pieces so no word is cut at a boundary; evaluate: --neg-dir (WAV + FLAC) or --neg-json
(the self-test's sets.negatives)."""
import argparse
import json
import sys
from pathlib import Path

from .paths import ensure, voice_home


def cmd_record(a):
    from .audio import list_devices, record, duration_s, load_wav
    if a.list_devices:
        print(list_devices())
        return 0
    out = Path(a.out) if a.out else ensure("raw") / f"rec_{__import__('datetime').datetime.now():%Y%m%d_%H%M%S}.wav"
    print(f"recording {a.seconds}s from device {a.device if a.device is not None else 'default'} -> {out}")
    p = record(a.seconds, a.device, out)
    wav, sr = load_wav(p)
    print(f"wrote {p} ({duration_s(wav, sr):.1f}s @ {sr} Hz)")
    return 0


def cmd_enroll(a):
    """Build the owner's enrollment from WAVs under enroll\\ (or --clips); threshold from --threshold
    (the value chosen at the self-test's or M0b's EER point)."""
    from .audio import load_wav, duration_s, sha256_file
    from .enroll import EnrollmentStore, build_centroid, MIN_ENROLL_SECONDS, MIN_ENROLL_CLIPS
    from .speaker import SpeakerEmbedder
    clips = [Path(c) for c in a.clips] if a.clips else sorted(ensure("enroll").glob("*.wav"))
    if len(clips) < MIN_ENROLL_CLIPS:
        print(f"REFUSED: {len(clips)} clips, need >= {MIN_ENROLL_CLIPS}")
        return 2
    emb = SpeakerEmbedder()
    vecs, meta, total = [], [], 0.0
    for c in clips:
        wav, sr = load_wav(c)
        d = duration_s(wav, sr)
        total += d
        vecs.append(emb.embed(wav, sr))
        meta.append({"path": str(c), "sha256": sha256_file(c), "duration_s": d})
        print(f"  {c.name}: {d:.1f}s")
    if total < MIN_ENROLL_SECONDS:
        print(f"REFUSED: {total:.1f}s of speech, need >= {MIN_ENROLL_SECONDS}s")
        return 2
    store = EnrollmentStore(name=a.name)
    p = store.save(build_centroid(vecs), meta, vecs, a.threshold, emb.model_id)
    print(f"enrolled {a.name}: {len(clips)} clips, {total:.1f}s, dim={emb.dim}, threshold={a.threshold} -> {p}")
    return 0


def cmd_verify(a):
    from .verify import verify_clip
    from .enroll import EnrollmentStore
    from .speaker import SpeakerEmbedder
    enr = EnrollmentStore(name=a.name).load()
    emb = SpeakerEmbedder()
    rc = 0
    for c in a.clips:
        r = verify_clip(c, enrollment=enr, embedder=emb)
        if r["refused"]:
            print(f"{c}: REFUSED ({r['reason']}, {r['duration_s']:.2f}s) threshold={r['threshold']:.4f}")
            rc = 2
        else:
            print(f"{c}: score={r['score']:.4f} threshold={r['threshold']:.4f} owner={r['owner']} ({r['duration_s']:.1f}s)")
    return rc


def cmd_transcribe(a):
    from .transcribe import transcribe, ASR
    asr = ASR(model=a.model, compute_type=a.compute_type)
    for c in a.inputs:
        r = transcribe(c, keep=a.keep, asr=asr)
        print(f"{c}: {r['duration_s']:.1f}s wall={r['wall_s']:.1f}s RTF={r['rtf']:.3f} deleted={r['deleted']}")
        print(f"  {r['text']}")
    return 0


def cmd_evaluate(a):
    from .enroll import EnrollmentStore
    from .evaluate import evaluate, paths_from_json
    if (a.neg_dir is None) == (a.neg_json is None):
        print("ERROR: give exactly one of --neg-dir or --neg-json")
        return 2
    enr = EnrollmentStore(name=a.name).load()
    neg_paths = paths_from_json(a.neg_json) if a.neg_json else None
    if a.neg_json and not neg_paths:
        print(f"ERROR: no negatives found in {a.neg_json}")
        return 2
    r = evaluate(enr, a.pos_dir, neg_dir=a.neg_dir, neg_paths=neg_paths)
    print(json.dumps({k: v for k, v in r.items() if k not in ("pos", "neg")}, indent=1))
    for name, s, d in r["pos"]:
        print(f"  POS {name}: {s:.4f} ({d:.1f}s)")
    for name, s, d in r["neg"]:
        print(f"  NEG {name}: {s:.4f} ({d:.1f}s)")
    return 0


def cmd_split(a):
    """Extract speech from a long 16 kHz recording and pack whole runs into pieces (see split.py)."""
    import shutil
    import numpy as np
    from .audio import load_wav, write_wav, TARGET_SR
    from .split import frame_rms_dbfs, speech_mask, runs, pack_runs
    src = Path(a.wav)
    out_dir = Path(a.out_dir)
    first = out_dir / f"{a.prefix}_001.wav"
    if first.exists():
        print(f"REFUSED: {first} already exists (a re-run must never double the set)")
        return 2
    wav, sr = load_wav(src)
    if sr != TARGET_SR:
        print(f"REFUSED: {src} is {sr} Hz, need {TARGET_SR}")
        return 2
    frame_s = 0.05
    fl = int(sr * frame_s)
    dbfs = frame_rms_dbfs(wav, sr, frame_s)
    pad_frames = round(a.pad_ms / 50)
    min_gap_frames = round(a.min_gap_ms / 50)
    mask = speech_mask(dbfs, a.frame_dbfs, pad_frames, min_gap_frames)
    rr = runs(mask)
    lengths = [(b - s) * frame_s for s, b in rr]
    pieces = pack_runs(lengths, a.target, a.min_keep)
    out_dir.mkdir(parents=True, exist_ok=True)
    kept = 0.0
    for n, piece in enumerate(pieces, start=1):
        spans = [wav[rr[i][0] * fl: rr[i][1] * fl] for i in piece]
        audio = np.concatenate(spans) if spans else np.zeros(0, dtype="float32")
        p = out_dir / f"{a.prefix}_{n:03d}.wav"
        write_wav(p, audio, sr)
        secs = sum(lengths[i] for i in piece)
        kept += secs
        print(f"piece {n:03d}: runs={len(piece)} {secs:.1f}s (from {rr[piece[0]][0] * frame_s:.1f}s)")
    speech = sum(lengths)
    print(f"summary: total {len(wav) / sr:.1f}s speech {speech:.1f}s in {len(rr)} runs; pieces {len(pieces)} kept {kept:.1f}s; "
          f"remainder dropped {speech - kept:.1f}s")
    if a.move_source_to:
        dest = Path(a.move_source_to)
        dest.mkdir(parents=True, exist_ok=True)
        target = dest / src.name
        if target.exists():
            print(f"REFUSED to move: {target} already exists (never overwrite a recording)")
            return 2
        shutil.move(str(src), str(target))
        print(f"moved {src} -> {target}")
    return 0


def cmd_selftest(a):
    from .selftest import main
    return main()


def build_parser():
    p = argparse.ArgumentParser(prog="jarvis_voice", description=f"Phase 7 goal 8 voice tooling; data under {voice_home()}")
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("record"); r.add_argument("--seconds", type=float, default=60.0); r.add_argument("--device", type=int)
    r.add_argument("--out"); r.add_argument("--list-devices", action="store_true"); r.set_defaults(fn=cmd_record)
    e = sub.add_parser("enroll"); e.add_argument("--clips", nargs="*"); e.add_argument("--threshold", type=float, required=True)
    e.add_argument("--name", default="owner"); e.set_defaults(fn=cmd_enroll)
    v = sub.add_parser("verify"); v.add_argument("clips", nargs="+"); v.add_argument("--name", default="owner"); v.set_defaults(fn=cmd_verify)
    t = sub.add_parser("transcribe"); t.add_argument("inputs", nargs="+"); t.add_argument("--keep", action="store_true")
    t.add_argument("--model", default="large-v3"); t.add_argument("--compute-type", default="float16"); t.set_defaults(fn=cmd_transcribe)
    ev = sub.add_parser("evaluate"); ev.add_argument("--pos-dir", required=True); ev.add_argument("--neg-dir")
    ev.add_argument("--neg-json", help="a self-test JSON whose sets.negatives lists the negative paths")
    ev.add_argument("--name", default="owner"); ev.set_defaults(fn=cmd_evaluate)
    sp = sub.add_parser("split"); sp.add_argument("wav"); sp.add_argument("--target", type=float, required=True)
    sp.add_argument("--min-keep", type=float, required=True); sp.add_argument("--out-dir", required=True)
    sp.add_argument("--prefix", required=True); sp.add_argument("--frame-dbfs", type=float, default=-45.0)
    sp.add_argument("--pad-ms", type=float, default=200.0); sp.add_argument("--min-gap-ms", type=float, default=500.0)
    sp.add_argument("--move-source-to"); sp.set_defaults(fn=cmd_split)
    s = sub.add_parser("selftest"); s.set_defaults(fn=cmd_selftest)
    return p


def main(argv=None):
    a = build_parser().parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
