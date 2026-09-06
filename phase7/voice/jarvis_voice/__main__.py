"""CLI: python -m jarvis_voice <record|enroll|verify|transcribe|evaluate|selftest> ..."""
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
    from .evaluate import evaluate
    enr = EnrollmentStore(name=a.name).load()
    r = evaluate(enr, a.pos_dir, a.neg_dir)
    print(json.dumps({k: v for k, v in r.items() if k not in ("pos", "neg")}, indent=1))
    for name, s, d in r["pos"]:
        print(f"  POS {name}: {s:.4f} ({d:.1f}s)")
    for name, s, d in r["neg"]:
        print(f"  NEG {name}: {s:.4f} ({d:.1f}s)")
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
    ev = sub.add_parser("evaluate"); ev.add_argument("--pos-dir", required=True); ev.add_argument("--neg-dir", required=True)
    ev.add_argument("--name", default="owner"); ev.set_defaults(fn=cmd_evaluate)
    s = sub.add_parser("selftest"); s.set_defaults(fn=cmd_selftest)
    return p


def main(argv=None):
    a = build_parser().parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
