"""CLI: python -m jarvis_voice <record|enroll|verify|transcribe|evaluate|split|selftest|cluster-bench|ingest|clusters|owner-merge|duration-bench> ...

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


def cmd_duration_bench(a):
    """Measure the owner's threshold at every span duration on the grid, on held-out data."""
    import datetime as _dt
    from .duration import run_duration_bench
    if a.reread:
        return _duration_bench_reread()
    out = Path(a.out) if a.out else voice_home() / f"duration_bench_{_dt.date.today():%Y-%m-%d}.json"
    r = run_duration_bench(out_path=out)
    print(f"stored threshold : {r['stored_threshold']:.15f}")
    print(f"positives {r['positives_files']} files ; negatives {r['negatives_files']} files ; "
          f"ECAPA {r['ecapa_model']} on {r['ecapa_device']} (load {r['ecapa_load_s']}s)")
    print(f"{'D s':>5} {'n_pos':>6} {'n_neg':>6} {'EER':>8} {'EER thr':>9} "
          f"{'FAR@thr':>8} {'FRR@thr':>8} {'pos_min':>8} {'neg_max':>8}  band")
    for row in r["table"]:
        if row["eer"] is None:
            print(f"{row['d_s']:>5.0f} {row['n_pos']:>6} {row['n_neg']:>6}   (no windows)")
            continue
        ok = row["frr_at_stored"] <= 0.05 and row["far_at_stored"] <= 0.01
        print(f"{row['d_s']:>5.0f} {row['n_pos']:>6} {row['n_neg']:>6} {row['eer']:>8.4f} "
              f"{row['eer_threshold']:>9.4f} {row['far_at_stored']:>8.4f} "
              f"{row['frr_at_stored']:>8.4f} {row['pos_min']:>8.4f} {row['neg_max']:>8.4f}  "
              f"{'PASS' if ok else 'fail'}")
    if r["min_embed_s"] is None:
        print("MIN_EMBED_S : NONE - no duration on the grid meets FRR <= 5 % and FAR <= 1 % at the "
              "stored threshold. This is a STOP: the threshold itself is the question.")
    else:
        print(f"MIN_EMBED_S : {r['min_embed_s']} s (the smallest qualifying duration)")
    print(f"written    : {out}")
    return 0


def _duration_bench_reread():
    """Re-apply the current reading rule to the newest bench. The GPU is never touched."""
    from .duration import reread_duration_bench
    r = reread_duration_bench()
    print(f"file        : {r['path']}")
    print(f"reading rule: FRR <= {r['reading_rule']['max_frr']}, FAR <= "
          f"{r['reading_rule']['max_far']}, n_pos and n_neg >= {r['reading_rule']['min_n']}")
    print(f"rows under the sample floor (reported, never read): {r['rows_under_floor']}")
    print(f"min_embed_s : {r['before']} -> {r['after']}")
    if r["retracted"] is not None:
        print(f"retracted   : {r['retracted']} s - recorded in the file, not deleted")
    if r["after"] is None:
        print("NO minimum exists on this grid at the stored threshold once the sample floor is "
              "applied. That is the measurement, not a failure of it: the pipeline must decide "
              "attribution some other way.")
    print("the table itself is untouched")
    return 0


def cmd_ingest(a):
    """Transcribe, cluster, write the spine, delete the audio — in that order, per WAV."""
    # torch FIRST, before CTranslate2 is loaded by ASR(). Measured: without it faster-whisper dies
    # with "Could not load symbol cudnnGetLibConfig. Error code 127" - CTranslate2 finds cuDNN
    # through the DLLs torch has already loaded. `transcribe()` has always imported torch ahead of
    # the engine for the same reason; this path has to do it too.
    import torch  # noqa: F401
    from .cluster import EMBED_MIN_S, OWNER_CLUSTER_MIN_S, OWNER_TURN_MIN_S, latest_bench
    from .enroll import EnrollmentStore
    from .speaker import SpeakerEmbedder
    from .spine import ingest_one, open_store, owner_merge
    from .transcribe import ASR

    enrollment = EnrollmentStore(name=a.name).load()
    if a.tau is not None:
        tau, tau_source = a.tau, "argument"
    else:
        bench = latest_bench()
        tau, tau_source = bench["tau_star"], "cluster_bench"
    store = open_store(a.db)
    asr = ASR(model=a.model, compute_type=a.compute_type)
    emb = SpeakerEmbedder()
    print(f"tau        : {tau} (from {tau_source}); owner threshold {enrollment['threshold']:.6f}; "
          f"store {store.path}")
    # The three durations, printed so a run says on its face what rule it applied. None is read
    # from the duration bench - M1a.4 retracted the number that was.
    print(f"durations  : embed >= {EMBED_MIN_S} s (verify.MIN_CLIP_S); owner asked of a turn "
          f">= {OWNER_TURN_MIN_S} s; owner-merge of a cluster >= {OWNER_CLUSTER_MIN_S} s "
          f"(the M0b piece length)")
    for wav in a.inputs:
        r = ingest_one(wav, store, enrollment, tau, asr, emb, keep=a.keep,
                       started_at=a.started_at, device=a.device)
        spans = r["spans"]
        owner = sum(1 for s in spans if s["cluster_source"] == "owner")
        joined = sum(1 for s in spans if s["cluster_source"] == "join")
        new = sum(1 for s in spans if s["cluster_source"] == "new")
        adjacent = sum(1 for s in spans if s["cluster_source"] == "adjacent")
        # never the span text - only counts
        g = r.get("asr_guard") or {}
        print(f"{Path(wav).name}: {r.get('duration_s', 0):.1f}s wall {r.get('wall_s', 0):.1f}s "
              f"RTF {r.get('rtf') or 0:.3f} | spans {len(spans)} turns {len(r.get('turns') or [])} "
              f"embedded {r['embedded_turns']} "
              f"| owner {owner} join {joined} new {new} adjacent {adjacent} "
              f"| committed {r['store_committed']} deleted {r['deleted']}")
        # one line per turn: the unit that was actually embedded and scored
        for t in r.get("turns") or []:
            print(f"    turn {t['turn_id']:>3}: segments {t['n_segments']:>3} "
                  f"speech {t['speech_s']:>6.2f}s {t['t_start_s']:>7.2f}-{t['t_end_s']:<7.2f} "
                  f"embedded {str(t['embedded']):>5} score "
                  f"{('%.4f' % t['score_owner']) if t['score_owner'] is not None else '   -  '} "
                  f"-> cluster {t['cluster_id']} ({t['cluster_source']})")
        # the guard's verdict on its own line: it decides whether the audio still exists.
        print(f"    asr guard: agreed {g.get('agreed')} segments {g.get('n_segments')} "
              f"max start delta {g.get('max_start_delta_s')}s max end delta "
              f"{g.get('max_end_delta_s')}s text identical {g.get('text_identical')} "
              f"| two passes {g.get('wall_s_total')}s | kept_by_guard {r.get('kept_by_guard')} "
              f"| deleted_audio_at {r.get('deleted_audio_at')}")
    # The second level of the owner decision, over every cluster this store now holds.
    _print_owner_merge(owner_merge(store, enrollment))
    return 0


def _print_owner_merge(m):
    """The owner-merge decision surface. Counts and scores only - never a span's text."""
    if m["owner_cluster"] is None:
        print("owner-merge: no owner cluster in this store - nothing to compare against")
        return
    print(f"owner-merge: threshold {m['threshold']:.6f}; a cluster is compared once it holds "
          f"{m['min_speech_s']} s of embedded speech")
    for c in m["skipped"]:
        print(f"    cluster {c['cluster']:>3}: {c['speech_s']:>6.2f}s NOT compared ({c['why']})")
    for c in m["compared"]:
        merged = any(x["cluster"] == c["cluster"] for x in m["merged"])
        print(f"    cluster {c['cluster']:>3}: {c['speech_s']:>6.2f}s score {c['score']:.4f} -> "
              f"{'MERGED into the owner' if merged else 'left as its own voice'}")
    for c in m["merged"]:
        print(f"    merged cluster {c['cluster']}: {c['spans_moved']} spans relabelled, "
              f"audit row {c['audit_id']}"
              + (f", person {c['person_left']} left for the people layer"
                 if c["person_left"] is not None else ""))
    if not m["compared"] and not m["skipped"]:
        print("    no non-owner clusters")


def cmd_owner_merge(a):
    """Run the second level of the owner decision on its own, over an existing store."""
    from .enroll import EnrollmentStore
    from .spine import open_store, owner_merge
    store = open_store(a.db)
    print(f"store      : {store.path}")
    _print_owner_merge(owner_merge(store, EnrollmentStore(name=a.name).load()))
    return 0


def cmd_clusters(a):
    """The purge decision surface: one line per cluster, printed, never written."""
    from .spine import cluster_rows, longest_span_texts, open_store
    store = open_store(a.db)
    rows = cluster_rows(store)
    if not rows:
        print("no clusters yet")
        return 0
    print(f"store {store.path}")
    for r in rows:
        tag = "OWNER" if r["owner"] else "     "
        print(f"cluster {r['id']:>3} {tag} person {str(r['person_id'] or '-'):>4} "
              f"spans {r['n_spans']:>5} days {r['days_heard']:>3} "
              f"first {str(r['first_heard'])[:19]} last {str(r['last_heard'])[:19]}")
        for t in longest_span_texts(store, r["id"]):
            print(f"        | {t}")
    print("purge one cluster with:  py -3 -m jarvis_memory purge <cluster_id>   (from phase7/memory)")
    return 0


def cmd_cluster_bench(a):
    """Measure the speaker-clustering threshold on the PUBLIC corpus, before any household audio.

    Scenario A fixes tau on ten known speakers; scenario B replays the household's SHAPE (one owner,
    a frequent second voice, a visitor, three strangers) through the same online rule the pipeline
    uses. The JSON it writes is what the pipeline reads tau back from - the value in use and the
    evidence for it are one artifact.
    """
    import datetime as _dt
    from .cluster import run_bench
    out = Path(a.out) if a.out else voice_home() / f"cluster_bench_{_dt.date.today():%Y-%m-%d}.json"
    r = run_bench(out_path=out, seed=a.seed)
    sa, sb = r["scenario_a"], r["scenario_b"]
    print(f"corpus     : {r['corpus_root']}")
    print(f"linkage    : {r['linkage_impl']}  ECAPA {r['ecapa_model']} on {r['ecapa_device']} "
          f"(load {r['ecapa_load_s']}s, {r['embeddings_computed']} embeddings)")
    print(f"scenario A : {len(sa['speakers'])} speakers x {sa['per_speaker']} = {sa['n']} clips "
          f"(S1 {sa['excluded_s1']} excluded), embed {sa['embed_s']}s")
    for g in sa["grid"]:
        star = " <- tau*" if g["tau"] == r["tau_star"] else ""
        print(f"   tau {g['tau']:.2f}  purity {g['purity']:.4f}  completeness "
              f"{g['completeness']:.4f}  product {g['purity'] * g['completeness']:.4f}  "
              f"clusters {g['clusters']}{star}")
    print(f"tau*       : {r['tau_star']}  purity {sa['at_tau_star']['purity']:.4f}  "
          f"completeness {sa['at_tau_star']['completeness']:.4f}  "
          f"clusters {sa['at_tau_star']['clusters']}  band "
          f"{'MET' if sa['band_met'] else 'MISSED'} ({sa['band']})")
    print(f"scenario B : owner {sb['owner_speaker']} threshold {sb['owner_threshold']:.4f}; "
          f"{sb['n']} spans, seed {sb['seed']}")
    print(f"   owner recall {sb['owner_recall']:.4f} over {sb['owner_spans']} owner spans; "
          f"owner FAR {sb['owner_far']:.4f} over {sb['non_owner_spans']} non-owner spans")
    print(f"   non-owner clusters {sb['non_owner_clusters']}  purity "
          f"{sb['non_owner_purity']:.4f}  completeness {sb['non_owner_completeness']:.4f}")
    print(f"written    : {out}")
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
    ig = sub.add_parser("ingest"); ig.add_argument("inputs", nargs="+")
    ig.add_argument("--keep", action="store_true"); ig.add_argument("--started-at")
    ig.add_argument("--db"); ig.add_argument("--tau", type=float); ig.add_argument("--name", default="owner")
    ig.add_argument("--model", default="large-v3"); ig.add_argument("--compute-type", default="float16")
    ig.add_argument("--device", default="headset"); ig.set_defaults(fn=cmd_ingest)
    cl = sub.add_parser("clusters"); cl.add_argument("--db"); cl.set_defaults(fn=cmd_clusters)
    om = sub.add_parser("owner-merge"); om.add_argument("--db")
    om.add_argument("--name", default="owner"); om.set_defaults(fn=cmd_owner_merge)
    db = sub.add_parser("duration-bench"); db.add_argument("--out")
    db.add_argument("--reread", action="store_true",
                    help="re-apply the current reading rule to the newest bench JSON and rewrite "
                         "its verdict; the table is never touched and no model is loaded")
    db.set_defaults(fn=cmd_duration_bench)
    cb = sub.add_parser("cluster-bench"); cb.add_argument("--out")
    cb.add_argument("--seed", type=int, default=1); cb.set_defaults(fn=cmd_cluster_bench)
    s = sub.add_parser("selftest"); s.set_defaults(fn=cmd_selftest)
    return p


def main(argv=None):
    a = build_parser().parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
