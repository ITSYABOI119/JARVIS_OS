"""M0a self-test on PUBLIC speakers (LibriSpeech dev-clean), end to end, printing every number.

Protocol (the prompt's §4, pre-registered):
 1. corpus under public\\ (URL, size, sha256, licence recorded); pick the speaker S1 with the most
    audio as the pseudo-owner; >= 60 s of S1 across >= 3 utterances for ENROLLMENT; >= 20 other S1
    utterances (>= 3 s) as POSITIVES; >= 40 utterances (>= 3 s) from >= 20 OTHER speakers as NEGATIVES.
 2. embed everything; EER + threshold on the held-out set; a throwaway enrollment under
    public\\enroll_S1\\ (never enroll\\owner.*).
 3. print EER, threshold, FAR/FRR at it, accuracy, score means, model + dim + version, wall per clip,
    VRAM peak.
 4. transcribe ONE positive clip: a 16 kHz WAV COPY under raw\\ (the corpus file survives); the copy is
    deleted after transcription (the consent mechanism) — raw\\ listed before and after.
 5. write selftest_<date>.json with everything.
Nothing here tunes anything: the numbers are reported as measured.
"""
import datetime as _dt
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from .paths import ensure, voice_home

CORPUS_URL = "https://www.openslr.org/resources/12/dev-clean.tar.gz"
CORPUS_TGZ = "dev-clean.tar.gz"
MIN_ENROLL_S, MIN_ENROLL_CLIPS = 60.0, 3
MIN_POS, MIN_NEG, MIN_NEG_SPEAKERS, MIN_CLIP_S = 20, 40, 20, 3.0
NEG_PER_SPEAKER = 2


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def gpu_mem_used_mib():
    try:
        out = subprocess.run(["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
                             capture_output=True, text=True, timeout=20).stdout.strip().splitlines()
        return int(out[0]) if out else None
    except Exception:  # noqa: BLE001 — a missing nvidia-smi is reported as None, never faked
        return None


def corpus_root() -> Path:
    pub = ensure("public")
    root = pub / "LibriSpeech" / "dev-clean"
    if not root.exists():
        raise SystemExit(f"corpus not found at {root}; download {CORPUS_URL} into {pub} and extract it")
    return root


def index_corpus(root: Path):
    """{speaker_id: [(path, duration_s), ...]} using soundfile headers only (no decode)."""
    import soundfile as sf
    idx = {}
    for spk in sorted(root.iterdir(), key=lambda p: int(p.name)):
        if not spk.is_dir():
            continue
        items = []
        for f in sorted(spk.rglob("*.flac")):
            info = sf.info(str(f))
            items.append((f, info.frames / info.samplerate))
        idx[spk.name] = items
    return idx


def pick_sets(idx):
    totals = {s: sum(d for _, d in items) for s, items in idx.items()}
    s1 = max(totals, key=totals.get)
    s1_items = idx[s1]
    enroll, acc = [], 0.0
    for p, d in s1_items:
        enroll.append((p, d))
        acc += d
        if acc >= MIN_ENROLL_S and len(enroll) >= MIN_ENROLL_CLIPS:
            break
    rest = [(p, d) for p, d in s1_items[len(enroll):] if d >= MIN_CLIP_S]
    positives = rest[:max(MIN_POS, len(rest))] if len(rest) >= MIN_POS else rest
    negatives, neg_speakers = [], []
    for s, items in idx.items():
        if s == s1:
            continue
        cands = [(p, d) for p, d in items if d >= MIN_CLIP_S][:NEG_PER_SPEAKER]
        if cands:
            negatives.extend(cands)
            neg_speakers.append(s)
    return s1, totals[s1], enroll, positives, negatives, neg_speakers


def main(argv=None) -> int:
    import numpy as np
    import torch
    from .audio import load_wav, resample_to_16k, write_wav, sha256_file as sha_f
    from .enroll import EnrollmentStore, build_centroid
    from .evaluate import eer, far_frr_at, accuracy_at, mean
    from .speaker import SpeakerEmbedder
    from .transcribe import ASR, transcribe
    from .verify import score

    date = _dt.datetime.now().strftime("%Y-%m-%d")
    out = {"date": _dt.datetime.now().isoformat(timespec="seconds"), "voice_home": str(voice_home())}
    pub = ensure("public")

    # 1. corpus
    tgz = pub / CORPUS_TGZ
    out["corpus"] = {"url": CORPUS_URL, "tarball": str(tgz),
                     "size_bytes": tgz.stat().st_size if tgz.exists() else None,
                     "sha256": sha256_file(tgz) if tgz.exists() else None}
    lic = pub / "LibriSpeech" / "LICENSE.TXT"
    out["corpus"]["licence"] = lic.read_text(encoding="utf-8", errors="replace").strip().splitlines()[:4] if lic.exists() else None
    print(f"[corpus] {CORPUS_URL} size={out['corpus']['size_bytes']} sha256={out['corpus']['sha256']}")
    print(f"[corpus] licence: {' | '.join(out['corpus']['licence'] or ['?'])}")
    root = corpus_root()
    t0 = time.perf_counter()
    idx = index_corpus(root)
    s1, s1_total, enroll, positives, negatives, neg_speakers = pick_sets(idx)
    print(f"[corpus] speakers={len(idx)} files={sum(len(v) for v in idx.values())} indexed in {time.perf_counter()-t0:.1f}s")
    print(f"[sets] S1={s1} total={s1_total:.1f}s | enroll {len(enroll)} clips {sum(d for _, d in enroll):.1f}s | "
          f"positives {len(positives)} | negatives {len(negatives)} from {len(neg_speakers)} speakers")
    out["sets"] = {"s1": s1, "s1_total_s": s1_total,
                   "enroll": [{"path": str(p), "duration_s": d} for p, d in enroll],
                   "n_pos": len(positives), "n_neg": len(negatives), "neg_speakers": neg_speakers,
                   "positives": [str(p) for p, _ in positives], "negatives": [str(p) for p, _ in negatives]}
    if len(positives) < MIN_POS or len(negatives) < MIN_NEG or len(neg_speakers) < MIN_NEG_SPEAKERS:
        print("[sets] BAND FAIL: counts below the pre-registered minimums")

    # 2. embed
    emb = SpeakerEmbedder()
    print(f"[model] {emb.model_id} speechbrain={emb.version} device={emb.device} load_s={emb.load_s:.1f}")
    if torch.cuda.is_available():
        torch.cuda.reset_peak_memory_stats()

    def embed_path(p):
        wav, sr = load_wav(p)
        return emb.embed(wav, sr)

    t0 = time.perf_counter()
    enroll_vecs = [embed_path(p) for p, _ in enroll]
    pos_vecs = [embed_path(p) for p, _ in positives]
    neg_vecs = [embed_path(p) for p, _ in negatives]
    n_clips = len(enroll_vecs) + len(pos_vecs) + len(neg_vecs)
    wall_per_clip = (time.perf_counter() - t0) / n_clips
    centroid = build_centroid(enroll_vecs)
    ps = [score(centroid, v) for v in pos_vecs]
    ns = [score(centroid, v) for v in neg_vecs]
    e, thr = eer(ps, ns)
    far, frr = far_frr_at(thr, ps, ns)
    acc = accuracy_at(thr, ps, ns)
    vram = torch.cuda.max_memory_allocated() / 2**20 if torch.cuda.is_available() else None
    print(f"[verify] EER={e*100:.2f}% threshold={thr:.4f} FAR={far*100:.2f}% FRR={frr*100:.2f}% accuracy={acc*100:.2f}%")
    print(f"[verify] pos_mean={mean(ps):.4f} neg_mean={mean(ns):.4f} pos_min={min(ps):.4f} neg_max={max(ns):.4f}")
    print(f"[verify] model={emb.model_id} dim={emb.dim} version={emb.version} wall_per_clip_s={wall_per_clip:.4f} torch_vram_peak_MiB={vram}")
    out["verify"] = {"model": emb.model_id, "speechbrain_version": emb.version, "device": emb.device, "dim": emb.dim,
                     "eer": e, "threshold": thr, "far_at_threshold": far, "frr_at_threshold": frr,
                     "accuracy_at_threshold": acc, "pos_mean": mean(ps), "neg_mean": mean(ns),
                     "pos_min": min(ps), "neg_max": max(ns), "pos_scores": ps, "neg_scores": ns,
                     "wall_per_clip_s": wall_per_clip, "torch_vram_peak_mib": vram,
                     "band_eer_le_5pct": e <= 0.05}
    store = EnrollmentStore(pub / "enroll_S1", name="S1")
    store.save(centroid, [{"path": str(p), "sha256": sha_f(p), "duration_s": d} for p, d in enroll],
               enroll_vecs, thr, emb.model_id, extra={"pseudo_owner": s1, "public_selftest": True})
    print(f"[enroll] throwaway enrollment -> {store.json_path}")

    # 4. transcribe one positive clip: a WAV copy under raw\, deleted after
    raw = ensure("raw")
    src, src_d = positives[0]
    wav, sr = load_wav(src)
    wav16, sr16 = resample_to_16k(wav, sr)
    copy = write_wav(raw / f"selftest_{s1}_{src.stem}.wav", wav16, sr16)
    before = sorted(p.name for p in raw.iterdir())
    print(f"[raw] before: {before}")
    gpu0 = gpu_mem_used_mib()
    asr = ASR()
    gpu_loaded = gpu_mem_used_mib()
    res = transcribe(copy, keep=False, asr=asr)
    gpu_after = gpu_mem_used_mib()
    after = sorted(p.name for p in raw.iterdir())
    print(f"[raw] after:  {after}  copy_exists={copy.exists()} json_deleted_flag={res['deleted']}")
    print(f"[asr] model={res['model']} compute={res['compute_type']} device={res['device']} faster_whisper={res['faster_whisper_version']} "
          f"audio_s={res['duration_s']:.2f} wall_s={res['wall_s']:.2f} RTF={res['rtf']:.3f} "
          f"gpu_used_MiB before={gpu0} loaded={gpu_loaded} after={gpu_after}")
    print(f"[asr] text: {res['text']}")
    out["asr"] = {k: res[k] for k in ("model", "compute_type", "device", "faster_whisper_version", "duration_s", "wall_s", "rtf", "text", "deleted", "input_sha256")}
    out["asr"].update({"gpu_used_mib_before": gpu0, "gpu_used_mib_loaded": gpu_loaded, "gpu_used_mib_after": gpu_after,
                       "raw_before": before, "raw_after": after, "source_clip": str(src), "source_duration_s": src_d,
                       "band_rtf_lt_1": (res["rtf"] or 9) < 1.0, "band_deleted": res["deleted"] and not copy.exists()})

    # 5. write
    outp = voice_home() / f"selftest_{date}.json"
    with open(outp, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=1, ensure_ascii=False)
    print(f"[selftest] written {outp}")
    ok = out["verify"]["band_eer_le_5pct"] and out["asr"]["band_rtf_lt_1"] and out["asr"]["band_deleted"] \
        and len(positives) >= MIN_POS and len(negatives) >= MIN_NEG and len(neg_speakers) >= MIN_NEG_SPEAKERS
    print(f"[selftest] BANDS: eer<=5%={out['verify']['band_eer_le_5pct']} rtf<1={out['asr']['band_rtf_lt_1']} "
          f"deleted={out['asr']['band_deleted']} counts_ok={len(positives) >= MIN_POS and len(negatives) >= MIN_NEG and len(neg_speakers) >= MIN_NEG_SPEAKERS} -> {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1
