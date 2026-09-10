"""The memory store as the voice pipeline's sink — recordings, spans, clusters, span vectors.

The design's §3.1: the transcript is not the record. The RECORD is the store's spine — one
`recording` row per audio file, one `span` row per Whisper segment, a `cluster` row per voice, and
one `embedding` row per embedded span. The audio is deleted; the spine is what remains, and it is
what the memory store's people layer (MS2) will later reason over.

Two rules of the owner's live here rather than in a comment:

  * **There is no "wife".** A voice that is not the owner's becomes a numbered cluster with a
    centroid and nothing else — no name, no relation, no person created by hand. Personhood is
    earned from evidence by `promote_persons`, and who a cluster IS remains MS2's inference. The
    tooling is deliberately unable to assert it.
  * **The purge is one action per cluster.** Because every span carries `cluster_id` and every
    speaker vector is an `embedding` row keyed to its span, `MemoryStore.purge_cluster` already
    removes a whole voice — utterances, vectors and the beliefs resting only on them — in one call.
    Nothing here re-implements that; the pipeline's job is to make the cluster id meaningful.

`jarvis_memory` is standard library + sqlite3, so the voice venv imports it directly; the sibling
directory is resolved from THIS file's location so the import works from any working directory.
"""
import sys
from pathlib import Path

_MEMORY_DIR = Path(__file__).resolve().parents[2] / "memory"
if str(_MEMORY_DIR) not in sys.path:
    sys.path.insert(0, str(_MEMORY_DIR))

OWNER_DISPLAY_NAME = "owner"


def open_store(db_path=None):
    """The household store (default `%USERPROFILE%\\.jarvis\\memory\\household.sqlite`)."""
    from jarvis_memory.store import MemoryStore
    return MemoryStore(db_path) if db_path else MemoryStore()


def find_owner_cluster(store):
    """The owner's cluster id, or None. Identified by its person row's `kind`, never by id order."""
    row = store.conn.execute(
        "select c.id from cluster c join person p on p.id = c.person_id "
        "where p.kind = 'owner' order by c.id limit 1").fetchone()
    return None if row is None else row[0]


def owner_cluster(store, enrollment) -> int:
    """The owner's cluster, created exactly ONCE and found by its person row ever after.

    Created with the ENROLLMENT centroid rather than with the first span that happens to match: the
    owner's identity was measured at M0b against 78 public negatives, and that measurement is what
    the cluster should carry. It is never re-derived from household audio, so a bad day of recording
    cannot drift the owner's own identity.
    """
    existing = find_owner_cluster(store)
    if existing is not None:
        return existing
    cid = store.add_cluster()
    store.set_cluster_centroid(cid, enrollment["centroid"])
    store.bind_owner(cid, OWNER_DISPLAY_NAME)
    return cid


def load_clusters(store) -> list:
    """Every NON-owner cluster as {"id", "centroid", "n"} — the online rule's working set.

    Clusters with no centroid yet are skipped rather than defaulted: a cluster the assignment cannot
    measure a distance to must not silently become the nearest one.
    """
    from jarvis_memory import embed as _embed
    owner = find_owner_cluster(store)
    out = []
    for row in store.conn.execute(
            "select c.id, c.centroid, "
            "  (select count(*) from span s where s.cluster_id = c.id) as n "
            "from cluster c order by c.id").fetchall():
        if row[0] == owner or row[1] is None:
            continue
        out.append({"id": row[0], "centroid": _embed.unpack(row[1], 192), "n": int(row[2])})
    return out


def add_span_embedding(store, span_id, vec, model=None) -> None:
    """One speaker vector for one span, in the store's own encoding."""
    from jarvis_memory.store import SPAN_EMBED_MODEL
    store.add_span_embedding(span_id, vec, model or SPAN_EMBED_MODEL)


def cluster_rows(store) -> list:
    """One row per cluster for the purge decision surface: id, owner flag, person, counts, dates.

    Returns data; the CLI prints it. Span TEXT is fetched by the caller through
    `longest_span_texts` and is never returned here, so no code path can write a listing to a file
    by accident.
    """
    owner = find_owner_cluster(store)
    rows = []
    for r in store.conn.execute(
            "select c.id, c.person_id, "
            "  (select count(*) from span s where s.cluster_id=c.id) as n_spans, "
            "  (select count(distinct date(s.said_at)) from span s where s.cluster_id=c.id) as days, "
            "  (select min(s.said_at) from span s where s.cluster_id=c.id) as first_heard, "
            "  (select max(s.said_at) from span s where s.cluster_id=c.id) as last_heard "
            "from cluster c order by c.id").fetchall():
        rows.append({"id": r[0], "owner": r[0] == owner, "person_id": r[1], "n_spans": int(r[2]),
                     "days_heard": int(r[3]), "first_heard": r[4], "last_heard": r[5]})
    return rows


def longest_span_texts(store, cluster_id, count=3, chars=60) -> list:
    """The `count` longest spans of a cluster, each truncated to `chars`.

    The operator has to RECOGNISE a voice to decide whether to purge it, and the quickest handle is
    a few words it said. Truncation is the compromise: enough to recognise, not a transcript. These
    strings are printed to a terminal and never written anywhere.
    """
    rows = store.conn.execute(
        "select text from span where cluster_id=? and text is not null "
        "order by (t_end_s - t_start_s) desc limit ?", (cluster_id, int(count))).fetchall()
    return [(r[0] or "").strip()[:chars] for r in rows]


# ----------------------------------------------------------------- the pipeline

SPAN_KEYS = ("span_id", "cluster_id", "cluster_source", "score_owner",
             "t_start_s", "t_end_s", "text", "asr_conf")


def rollback_recording(store, recording_id) -> None:
    """Undo a partially written recording: its span vectors, its spans, then the recording row.

    The store's own writers commit as they go (every earlier milestone wanted that), so a failure
    part-way through a recording cannot be undone by a rollback of an open transaction. It is
    compensated instead, in the reverse of the write order, so the observable rule the owner was
    promised still holds: a recording either lands whole or leaves nothing behind, and the audio is
    still on disk to try again.
    """
    ids = [r[0] for r in store.conn.execute(
        "select id from span where recording_id=?", (recording_id,)).fetchall()]
    for sid in ids:
        store.conn.execute("delete from embedding where owner_table='span' and owner_id=?", (sid,))
    store.conn.execute("delete from span where recording_id=?", (recording_id,))
    store.conn.execute("delete from recording where id=?", (recording_id,))
    store.conn.commit()


def ingest_one(path, store, enrollment, tau, asr, embedder, keep=False, started_at=None,
               out_dir=None, writer=None, min_clip_s=None, device="unknown", now=None,
               load_wav=None) -> dict:
    """One WAV -> the spine, a transcript JSON, and (unless keep) no audio.

    The ORDER is the owner's rule made mechanical, and it is the reason this is one function rather
    than three: the store commits FIRST, the JSON is written and fsync'd SECOND, and only then is
    the audio deleted. A failure at any earlier point leaves the WAV exactly where it was, which is
    the only recoverable state — everything else can be recomputed from audio, and nothing can be
    recomputed from a deleted file.

    `asr`, `embedder`, `store`, `writer` and `load_wav` are injected so the whole order is
    exercisable with no GPU, no model and no audio decoder — which is what the deletion-order test
    does, and why that test can run in CI where numpy and soundfile are not installed.
    """
    import time as _time

    from .audio import load_wav as _real_load_wav, sha256_file
    load_wav = load_wav or _real_load_wav
    from .cluster import assign, fill_adjacent, update_centroid
    from .transcribe import (PIPELINE_ASR_OPTIONS, finalize, resolve_started_at,
                             segmentations_agree, write_json_fsync)
    from .verify import MIN_CLIP_S, score as _score

    path = Path(path)
    min_clip_s = MIN_CLIP_S if min_clip_s is None else min_clip_s
    writer = writer or write_json_fsync
    out_dir = Path(out_dir) if out_dir else None
    t0 = _time.perf_counter()

    # 1. the sha256 FIRST: it is the only thing that outlives the audio and identifies it.
    sha = sha256_file(path)

    # 3. transcription, TWICE (2. is resolved after it, because mtime - duration needs the duration)
    #
    # The double run costs a second decode and buys the one thing the pipeline cannot otherwise
    # have. M1b transcribed a byte-identical input twice under identical settings and got 3 segments
    # once and 9 the other time; every span, every embedding, every cluster follows the
    # segmentation, and the audio is deleted at step 7, so an unreproducible segmentation makes the
    # spine a one-shot record with no way back. Pinning the decoder (PIPELINE_ASR_OPTIONS) is the
    # fix; running it twice is how we find out whether the fix held THIS TIME, on THIS audio, while
    # the audio still exists. On a disagreement the WAV is kept and the run is reported.
    #
    # The STORE gets the FIRST pass. Not a merge and not the second: with two segmentations in
    # hand there is no principled way to pick, and a merged one is a third thing neither pass
    # produced. First-pass-plus-kept-audio is a state the operator can act on.
    asr_result = asr.run(path, PIPELINE_ASR_OPTIONS)
    asr_second = asr.run(path, PIPELINE_ASR_OPTIONS)
    asr_agreed, asr_guard = segmentations_agree(asr_result, asr_second)
    asr_guard["agreed"] = asr_agreed
    asr_guard["wall_s_pass1"] = asr_result.get("wall_s")
    asr_guard["wall_s_pass2"] = asr_second.get("wall_s")
    asr_guard["wall_s_total"] = round(float(asr_result.get("wall_s") or 0.0)
                                      + float(asr_second.get("wall_s") or 0.0), 3)
    duration = asr_result.get("duration_s") or 0.0
    started, started_source = resolve_started_at(path, started_at, duration)

    segments = asr_result.get("segments") or []
    wav = sr = None
    owner_centroid = enrollment["centroid"]
    owner_threshold = float(enrollment["threshold"])
    owner_cid = owner_cluster(store, enrollment)
    clusters = load_clusters(store)

    # 4. one embedding per span >= min_clip_s, then the ONLINE assignment
    per_span, embedded = [], 0
    for seg in segments:
        s_start, s_end = float(seg["start"]), float(seg["end"])
        # CLAMP to the recording's own duration. Measured on the first real run: Whisper returned a
        # final segment ending at 47.98 s for a 20.0 s file (2 words over a near-silent tail), and
        # an unclamped end would be written to the spine as fact - a span the store believes lasted
        # 30 s inside a 20 s recording, with a said_at derived from it. The audio is deleted after
        # this, so nothing downstream could ever notice the impossibility, let alone correct it.
        if duration:
            s_end = min(s_end, float(duration))
            s_start = min(s_start, s_end)
        dur = s_end - s_start
        row = {"t_start_s": s_start, "t_end_s": s_end, "text": (seg.get("text") or "").strip(),
               "asr_conf": seg.get("avg_logprob"), "no_speech_prob": seg.get("no_speech_prob"),
               "cluster_id": None, "cluster_source": None, "score_owner": None, "vec": None}
        if dur >= min_clip_s:
            if wav is None:
                wav, sr = load_wav(path)
            a = int(round(s_start * sr))
            b = min(int(round(s_end * sr)), len(wav))
            vec = embedder.embed(wav[a:b], sr)
            row["vec"] = vec
            row["score_owner"] = _score(owner_centroid, vec)
            idx, kind = assign(vec, owner_centroid, owner_threshold, clusters, tau)
            if kind == "owner":
                row["cluster_id"], row["cluster_source"] = owner_cid, "owner"
            elif kind == "join":
                c = clusters[idx]
                c["centroid"] = update_centroid(c["centroid"], c["n"], vec)
                c["n"] += 1
                row["cluster_id"], row["cluster_source"] = c["id"], "join"
            else:
                row["cluster_id"], row["cluster_source"] = None, "new"
            embedded += 1
        per_span.append(row)

    # 5. the store: the recording, then the spans in time order, then the vectors.
    rec_id = None
    payload = {}
    try:
        rec_id = store.add_recording(sha, started, duration, device)
        # a "new" cluster is created only now, so a failure during ASR never leaves an empty one
        for row in per_span:
            if row["cluster_source"] == "new":
                cid = store.add_cluster()
                store.set_cluster_centroid(cid, row["vec"])
                row["cluster_id"] = cid
                clusters.append({"id": cid, "centroid": list(row["vec"]), "n": 1})
        filled = fill_adjacent([r["cluster_id"] for r in per_span])
        for row, cid in zip(per_span, filled):
            if row["cluster_id"] is None and cid is not None:
                row["cluster_id"], row["cluster_source"] = cid, "adjacent"
            else:
                row["cluster_id"] = cid
        for row in per_span:
            row["span_id"] = store.add_span(rec_id, row["t_start_s"], row["t_end_s"],
                                            row["cluster_id"], row["text"], row["asr_conf"])
        for row in per_span:
            if row["vec"] is not None:
                add_span_embedding(store, row["span_id"], row["vec"])
        for row in per_span:
            if row["cluster_source"] == "join" and row["cluster_id"] is not None:
                c = next((c for c in clusters if c["id"] == row["cluster_id"]), None)
                if c is not None:
                    store.set_cluster_centroid(c["id"], c["centroid"])
        store.promote_persons()
        store.conn.commit()
        committed = True
    except Exception:
        if rec_id is not None:
            try:
                rollback_recording(store, rec_id)
            except Exception:
                pass
        raise

    # 6. the transcript JSON, then 7. the deletion — in that order, inside `finalize`.
    counts = {}
    for row in per_span:
        if row["cluster_id"] is not None:
            counts[row["cluster_id"]] = counts.get(row["cluster_id"], 0) + 1
    payload = {
        "input": str(path), "input_sha256": sha,
        "started_at": started, "started_at_source": started_source,
        "recording_id": rec_id, "store_path": getattr(store, "path", None),
        "store_committed": committed,
        "tau": tau, "owner_threshold": owner_threshold, "owner_cluster_id": owner_cid,
        "spans": [{k: row[k] for k in SPAN_KEYS} for row in per_span],
        "clusters_after": {str(k): v for k, v in sorted(counts.items())},
        "embedded_spans": embedded,
        "pipeline_wall_s": round(_time.perf_counter() - t0, 2),
        **{k: v for k, v in asr_result.items() if k != "segments"},
    }
    # after the spread, so a future ASR field can never quietly overwrite the guard's verdict.
    # `wall_s` and `rtf` above are the FIRST pass alone; the pair is in asr_guard.
    payload["asr_guard"] = asr_guard
    json_path = (out_dir or _transcripts_dir()) / (path.stem + ".json")
    return finalize(path, json_path, payload, keep=keep, writer=writer,
                    kept_by_guard=not asr_agreed)


def _transcripts_dir():
    from .paths import ensure
    return ensure("transcripts")
