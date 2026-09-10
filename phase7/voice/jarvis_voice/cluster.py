"""Speaker clustering: the owner by his enrolled threshold, everyone else by distance.

The design's §3.1 and §5, in code. Every span >= 2 s carries a 192-d ECAPA embedding. A span scoring
>= the owner's M0b threshold against the enrollment centroid is the OWNER's; every other span joins
the nearest existing cluster within tau, or starts a new one. Nothing here knows what a cluster IS —
there is no "wife", no "visitor", no name. A cluster is an id and a centroid until the memory store's
people layer earns it a person (MS2). That ordering is the owner's rule, not an implementation
detail: the tooling must not be able to assert a relationship it has not been given evidence for.

tau is MEASURED on the public corpus before any household audio is clustered (`cluster-bench`), and
the pipeline reads it back from that run's JSON. It is never a literal in the pipeline.

The pure parts are standard library only (the `verify`/`split` precedent) so the test suite exercises
the whole decision surface with no GPU, no numpy and no corpus; scipy and numpy are imported INSIDE
`agglomerative_average`, which is the only function that needs them.
"""
import math
from pathlib import Path
from typing import List, Sequence, Tuple

from .verify import MIN_CLIP_S as EMBED_MIN_S, score as _cos

# ------------------------------------------------- the two-level owner decision (M1b.4)
# Each of these names where it comes from, because the rule they encode replaced a number that WAS
# read from a measurement and had to be retracted (M1a.4: the reading rule without a sample floor
# returned 12.0 s from a row of two windows). None of them is derived from the duration table.
#
#   EMBED_MIN_S          2.0 s, IMPORTED from verify.MIN_CLIP_S rather than retyped - the existing
#                        refusal length for a verification clip, unchanged since M0a.
#   OWNER_TURN_MIN_S     10.0 s, the length of the M0b held-out pieces the stored threshold was
#                        measured on. The threshold is a property of a voice AT A DURATION, so it is
#                        only asked of a turn that is on the footing it was measured at.
#   OWNER_CLUSTER_MIN_S  10.0 s, the same footing for a CENTROID. The enrollment centroid is itself
#                        a mean of clip embeddings, so a cluster centroid over enough speech is
#                        compared like with like.
OWNER_TURN_MIN_S = 10.0
OWNER_CLUSTER_MIN_S = 10.0

# The grid is pre-registered: 0.20 .. 0.60 in steps of 0.02, 21 values. Written as integers and
# divided so the values are exact decimals rather than accumulated float error, because tau is
# recorded in a JSON, compared against, and quoted in the docs.
TAU_GRID = tuple(round(0.20 + 0.02 * i, 2) for i in range(21))


def cosine_distance(a: Sequence[float], b: Sequence[float]) -> float:
    """1 - cosine similarity. Both sides are re-normalised by `verify.score`, so a caller's
    normalisation (or lack of one) cannot change the distance."""
    return 1.0 - _cos(a, b)


def _counts(labels_true, labels_pred):
    """{pred_label: {true_label: count}} plus the reverse, in one pass."""
    by_pred, by_true = {}, {}
    for t, p in zip(labels_true, labels_pred):
        by_pred.setdefault(p, {}).setdefault(t, 0)
        by_pred[p][t] += 1
        by_true.setdefault(t, {}).setdefault(p, 0)
        by_true[t][p] += 1
    return by_pred, by_true


def purity(labels_true: Sequence, labels_pred: Sequence) -> float:
    """Sum over CLUSTERS of the cluster's majority-speaker count, over N.

    1.0 means no cluster mixes speakers. It is trivially maximised by putting every item in its own
    cluster, which is why it is never read without completeness beside it.
    """
    n = len(list(labels_true))
    if n == 0:
        return 0.0
    by_pred, _ = _counts(labels_true, labels_pred)
    return sum(max(d.values()) for d in by_pred.values()) / n


def completeness(labels_true: Sequence, labels_pred: Sequence) -> float:
    """Sum over SPEAKERS of the speaker's largest-cluster count, over N.

    1.0 means no speaker was split across clusters. It is trivially maximised by putting everything
    in one cluster - the exact opposite failure to purity's, which is why tau is chosen on the
    PRODUCT of the two.
    """
    n = len(list(labels_true))
    if n == 0:
        return 0.0
    _, by_true = _counts(labels_true, labels_pred)
    return sum(max(d.values()) for d in by_true.values()) / n


def choose_tau(grid_scores: Sequence[Tuple[float, float, float]]) -> float:
    """(tau, purity, completeness)* -> the tau maximising purity x completeness.

    Ties go to the SMALLER tau, pre-registered before the numbers existed. The direction is not
    arbitrary: a smaller tau splits rather than merges, and a split cluster is recoverable (the
    operator purges two ids instead of one) while a merged one has already put two people's speech
    under a single purge action. The tie rule therefore fails toward the recoverable error.
    """
    best = None
    for tau, p, c in grid_scores:
        product = p * c
        if best is None or product > best[1] or (product == best[1] and tau < best[0]):
            best = (tau, product)
    if best is None:
        raise ValueError("empty grid")
    return best[0]


def assign(emb: Sequence[float], owner_centroid: Sequence[float], owner_threshold: float,
            clusters: Sequence[dict], tau: float, owner_eligible: bool = True) -> Tuple[int, str]:
    """The ONLINE rule, pure. -> (cluster_index, kind).

    kind is one of:
      "owner"  the span scores >= the owner's threshold against his enrollment centroid;
               index is -1 because the owner's cluster is not in `clusters` - the caller holds it.
      "join"   the nearest cluster whose cosine distance is <= tau; index is its position.
      "new"    no cluster is within tau; index is -1 and the caller creates one.

    THE OWNER IS CHECKED FIRST and that ordering is the whole safety property: his threshold was
    measured against 78 public negatives at EER 0.00 % (M0b), so it is a far stronger discriminator
    than an unsupervised distance, and a span that clears it must never be captured by a drifting
    neighbour cluster instead. The boundary is inclusive, matching `verify.decide`.

    Ties on distance go to the LOWEST index, so the assignment is deterministic and a re-run over
    the same spans in the same order reproduces the same clusters.

    `owner_eligible` (M1b.4) is the caller's statement that this embedding is on the footing the
    threshold was measured at - at ingest, a turn holding at least OWNER_TURN_MIN_S of speech. When
    it is false the owner check is SKIPPED and the embedding is clustered like any other, even if it
    would have scored above the threshold: M1a.3 measured a 74 % false-reject rate on one-second
    windows, so a short window's score is not evidence in either direction and acting on the half of
    it that happens to look right would be reading the noise selectively. Such a voice can still
    become the owner's, later and on better evidence, through the cluster-centroid rule.
    """
    if owner_eligible and _cos(owner_centroid, emb) >= owner_threshold:
        return -1, "owner"
    best_i, best_d = -1, None
    for i, c in enumerate(clusters):
        d = cosine_distance(c["centroid"], emb)
        if best_d is None or d < best_d:
            best_i, best_d = i, d
    if best_d is not None and best_d <= tau:
        return best_i, "join"
    return -1, "new"


def update_centroid(centroid: Sequence[float], n: int, emb: Sequence[float]) -> List[float]:
    """The running mean over n + 1 embeddings, re-normalised.

    Re-normalising matters because every distance here is a cosine against this vector: an
    un-normalised running mean still points the right way, but `verify.score` re-normalises its
    inputs anyway, so the only thing an un-normalised centroid changes is what gets STORED and
    compared elsewhere. Keeping it unit-length means the stored centroid and the compared centroid
    are the same object (T7d).
    """
    if n < 0:
        raise ValueError("n must be >= 0")
    if len(centroid) != len(emb):
        raise ValueError(f"dimension mismatch: {len(centroid)} vs {len(emb)}")
    total = n + 1
    mean = [(c * n + e) / total for c, e in zip(centroid, emb)]
    norm = math.sqrt(sum(x * x for x in mean))
    if norm == 0.0:
        raise ValueError("zero vector cannot be normalised")
    return [x / norm for x in mean]


def linkage_backend() -> str:
    """Which average-linkage implementation `agglomerative_average` will use.

    Deterministic and side-effect free, so the bench can RECORD it beside the numbers. A silent
    fallback between two implementations would make a tau incomparable across runs, and the point of
    recording it is that the reader can tell which one produced the value the pipeline now uses.
    """
    try:
        import scipy.cluster.hierarchy  # noqa: F401
    except Exception:
        return "numpy-average-linkage"
    return "scipy.cluster.hierarchy"


def agglomerative_average(embs, tau: float) -> List[int]:
    """Average-linkage agglomerative clustering on cosine distance, cut at tau -> labels.

    scipy when it imports (it does in the voice venv, 1.16.3), else a numpy implementation written
    out rather than approximated. Both are O(N^2) memory at N <= 400, which the pre-registered
    scenario A is.
    """
    if linkage_backend() == "scipy.cluster.hierarchy":
        return _agglomerative_scipy(embs, tau)
    return _agglomerative_numpy(embs, tau)


def _agglomerative_scipy(embs, tau):
    import numpy as np
    from scipy.cluster.hierarchy import fcluster, linkage
    x = np.asarray(embs, dtype="float64")
    z = linkage(x, method="average", metric="cosine")
    return [int(v) for v in fcluster(z, t=tau, criterion="distance")]


def _agglomerative_numpy(embs, tau):
    """The fallback, written out rather than approximated: repeatedly merge the two clusters with the
    smallest AVERAGE pairwise cosine distance until the smallest is above tau."""
    import numpy as np
    x = np.asarray(embs, dtype="float64")
    x = x / np.linalg.norm(x, axis=1, keepdims=True)
    d = 1.0 - (x @ x.T)
    n = len(x)
    groups = {i: [i] for i in range(n)}
    while len(groups) > 1:
        keys = sorted(groups)
        best, bi, bj = None, None, None
        for a_i in range(len(keys)):
            for b_i in range(a_i + 1, len(keys)):
                ga, gb = groups[keys[a_i]], groups[keys[b_i]]
                avg = float(d[np.ix_(ga, gb)].mean())
                if best is None or avg < best:
                    best, bi, bj = avg, keys[a_i], keys[b_i]
        if best is None or best > tau:
            break
        groups[bi] = groups[bi] + groups[bj]
        del groups[bj]
    labels = [0] * n
    for new_id, key in enumerate(sorted(groups), start=1):
        for idx in groups[key]:
            labels[idx] = new_id
    return labels


# ---------------------------------------------------------------- the bench
# Everything below needs the corpus and the GPU and is never imported by the pure tests.

SCENARIO_A_SPEAKERS = 10          # the ten dev-clean speakers with the most utterances, minus S1
SCENARIO_A_PER_SPEAKER = 30       # the first N in file order with duration >= MIN_CLIP_S
SCENARIO_B_SHAPE = (("partner", 40), ("visitor", 6),
                    ("stranger", 2), ("stranger", 2), ("stranger", 2))


def _bench_speakers(idx, exclude, want):
    """The `want` speakers with the most utterances >= MIN_CLIP_S, excluding `exclude`.

    Ordered by utterance COUNT (the pre-registered wording), ties broken by speaker id so the
    selection is reproducible rather than dict-order dependent.
    """
    from .verify import MIN_CLIP_S
    usable = {s: [p for p, d in items if d >= MIN_CLIP_S]
              for s, items in idx.items() if s != exclude}
    ranked = sorted(usable, key=lambda s: (-len(usable[s]), int(s)))
    return [(s, usable[s]) for s in ranked[:want]], usable


def run_bench(out_path=None, seed: int = 1) -> dict:
    """Scenario A (the threshold) and scenario B (the household shape), exactly as pre-registered.

    Nothing here touches household audio: it is public read speech with known speaker labels, run
    BEFORE the pipeline is pointed at anything real, because a threshold chosen on the household's
    own recordings would be fitted to the very thing it is meant to measure.
    """
    import datetime as _dt
    import json as _json
    import random
    import time

    from .audio import load_wav
    from .enroll import EnrollmentStore
    from .paths import ensure, voice_home
    from .selftest import corpus_root, index_corpus
    from .speaker import SpeakerEmbedder
    from .verify import MIN_CLIP_S, score as _score

    t_start = time.perf_counter()
    root = corpus_root()
    idx = index_corpus(root)

    # S1 is the self-test's pseudo-owner; scenario A must not measure a threshold on the speaker
    # scenario B then treats as the owner.
    st_files = sorted(voice_home().glob("selftest_*.json"))
    if not st_files:
        raise SystemExit("no selftest_*.json under the voice home; run `selftest` first")
    selftest = _json.loads(st_files[-1].read_text(encoding="utf-8"))
    s1 = str(selftest["sets"]["s1"])
    owner_threshold = float(selftest["verify"]["threshold"])

    emb = SpeakerEmbedder()
    cache = {}

    def vec(p):
        key = str(p)
        if key not in cache:
            wav, sr = load_wav(p)
            cache[key] = emb.embed(wav, sr)
        return cache[key]

    # ---- scenario A: the threshold ------------------------------------------------
    chosen, usable = _bench_speakers(idx, exclude=s1, want=SCENARIO_A_SPEAKERS)
    a_paths, a_labels = [], []
    for spk, paths in chosen:
        for p in paths[:SCENARIO_A_PER_SPEAKER]:
            a_paths.append(p)
            a_labels.append(spk)
    t0 = time.perf_counter()
    a_vecs = [vec(p) for p in a_paths]
    a_embed_s = time.perf_counter() - t0

    grid = []
    for tau in TAU_GRID:
        labels = agglomerative_average(a_vecs, tau)
        grid.append({"tau": tau, "purity": purity(a_labels, labels),
                     "completeness": completeness(a_labels, labels),
                     "clusters": len(set(labels))})
    tau_star = choose_tau([(g["tau"], g["purity"], g["completeness"]) for g in grid])
    at_star = next(g for g in grid if g["tau"] == tau_star)
    band_met = at_star["purity"] >= 0.95 and at_star["completeness"] >= 0.90

    # ---- scenario B: the household shape, ONLINE -----------------------------------
    s1_enr = EnrollmentStore(ensure("public") / "enroll_S1", name="S1").load()
    owner_centroid = s1_enr["centroid"]
    owner_items = [(p, "owner") for p in selftest["sets"]["positives"]]

    others, used = [], {s1}
    pool = [s for s, _ in chosen]
    for i, (role, n) in enumerate(SCENARIO_B_SHAPE):
        spk = pool[i]
        used.add(spk)
        others.extend([(str(p), spk) for p in usable[spk][:n]])
    b_items = [(str(p), lab) for p, lab in owner_items] + others
    random.Random(seed).shuffle(b_items)

    clusters, b_rows = [], []
    owner_hits = owner_total = far = non_owner_total = 0
    t0 = time.perf_counter()
    for path, label in b_items:
        e = vec(path)
        i, kind = assign(e, owner_centroid, owner_threshold, clusters, tau_star)
        if kind == "owner":
            pred = "OWNER"
        elif kind == "join":
            clusters[i]["centroid"] = update_centroid(clusters[i]["centroid"], clusters[i]["n"], e)
            clusters[i]["n"] += 1
            pred = i
        else:
            clusters.append({"centroid": list(e), "n": 1})
            pred = len(clusters) - 1
        b_rows.append({"label": label, "pred": pred, "kind": kind,
                       "score_owner": _score(owner_centroid, e)})
        if label == "owner":
            owner_total += 1
            owner_hits += (kind == "owner")
        else:
            non_owner_total += 1
            far += (kind == "owner")
    b_online_s = time.perf_counter() - t0

    non_owner = [(r["label"], r["pred"]) for r in b_rows if r["label"] != "owner"
                 and r["pred"] != "OWNER"]
    b_purity = purity([t for t, _ in non_owner], [p for _, p in non_owner]) if non_owner else 0.0
    b_complete = (completeness([t for t, _ in non_owner], [p for _, p in non_owner])
                  if non_owner else 0.0)

    payload = {
        "created": _dt.datetime.now().isoformat(timespec="seconds"),
        "corpus_root": str(root),
        "linkage_impl": linkage_backend(),
        "min_clip_s": MIN_CLIP_S,
        "ecapa_model": emb.model_id, "ecapa_load_s": round(emb.load_s, 2),
        "ecapa_device": emb.device, "speechbrain_version": emb.version,
        "embeddings_computed": len(cache),
        "tau_grid": list(TAU_GRID),
        "tau_star": tau_star,
        "scenario_a": {
            "speakers": [s for s, _ in chosen], "excluded_s1": s1,
            "per_speaker": SCENARIO_A_PER_SPEAKER, "n": len(a_vecs),
            "embed_s": round(a_embed_s, 1),
            "grid": grid,
            "at_tau_star": at_star,
            "band": "purity >= 0.95 AND completeness >= 0.90",
            "band_met": bool(band_met),
        },
        "scenario_b": {
            "owner_speaker": s1, "owner_threshold": owner_threshold,
            "shape": [[r, n] for r, n in SCENARIO_B_SHAPE],
            "speakers": pool[:len(SCENARIO_B_SHAPE)],
            "seed": seed, "n": len(b_items), "online_s": round(b_online_s, 1),
            "owner_spans": owner_total, "owner_recall": (owner_hits / owner_total) if owner_total else 0.0,
            "non_owner_spans": non_owner_total,
            "owner_far": (far / non_owner_total) if non_owner_total else 0.0,
            "non_owner_clusters": len(clusters),
            "non_owner_purity": b_purity, "non_owner_completeness": b_complete,
            "expectation": "FAR 0 and owner recall >= 0.95 follow from M0a's EER 0.00 %; purity >= 0.95",
        },
        "wall_s": round(time.perf_counter() - t_start, 1),
        "scope": ("Public read speech with known speaker labels (LibriSpeech dev-clean), measured "
                  "BEFORE any household audio. Whisper segments are not turn-aligned and ECAPA on "
                  "conversational fragments is weaker than on read speech, so these are an UPPER "
                  "BOUND on household behaviour."),
    }
    if out_path:
        out_path = Path(out_path)
        out_path.parent.mkdir(parents=True, exist_ok=True)
        out_path.write_text(_json.dumps(payload, indent=1), encoding="utf-8")
    return payload


def latest_bench(voice_home_dir=None) -> dict:
    """The newest cluster_bench_*.json — the pipeline's ONLY source of tau.

    A literal tau in the pipeline would be a number nobody could trace to a measurement; reading it
    back from the run that produced it means the value in use and the evidence for it are the same
    artifact.
    """
    import json as _json
    from .paths import voice_home
    home = Path(voice_home_dir) if voice_home_dir else voice_home()
    files = sorted(home.glob("cluster_bench_*.json"))
    if not files:
        raise SystemExit(
            "no cluster_bench_*.json under %s - run `python -m jarvis_voice cluster-bench` first; "
            "the clustering threshold is measured on the public corpus, never guessed" % home)
    return _json.loads(files[-1].read_text(encoding="utf-8"))


# The turn rule, pre-registered at M1b.3. Consecutive segments closer than TURN_MAX_GAP_S belong to
# one stretch of talking; a turn closes once it holds TURN_MAX_SPEECH_S of speech so one long
# monologue does not become a single unbounded embedding.
TURN_MAX_GAP_S = 0.5
TURN_MAX_SPEECH_S = 10.0


def build_turns(bounds: Sequence[Tuple[float, float]],
                max_gap_s: float = TURN_MAX_GAP_S,
                max_speech_s: float = TURN_MAX_SPEECH_S) -> List[List[int]]:
    """Group consecutive segments into TURNS. `bounds` is (start, end) per segment, in time order.

    Returns a list of turns, each a list of segment INDICES. A segment opens a new turn when the
    silence before it exceeds `max_gap_s`, or when the turn it would join already holds
    `max_speech_s` of speech. A turn's speech is the SUM of its segments' durations, not its
    wall span, because the samples that get embedded are the segments concatenated - the silence
    between them is removed, exactly as the duration bench's windows are speech-packed.

    **The cap bounds MERGING, not a segment.** A turn closes once it REACHES `max_speech_s`, so the
    segment that crosses the cap is inside it and a turn is at least that long at close; and a
    single segment longer than the cap is a turn on its own, because the spine's spans stay the
    Whisper segments and nothing here may split one. So a turn can exceed `max_speech_s`, and that
    is the reason a minimum embedding duration above the cap is still reachable.

    Why turns at all: M1a.3 measured the owner's own false-reject rate against his own threshold at
    0.74 on one-second windows and 0.00 at twelve, so a per-segment embedding asks the threshold a
    question it cannot answer. A turn is the longest unit that is still one person talking.
    """
    turns: List[List[int]] = []
    cur: List[int] = []
    acc = 0.0
    prev_end = None
    for i, (a, b) in enumerate(bounds):
        gap = None if prev_end is None else float(a) - float(prev_end)
        if cur and (gap is not None and gap > max_gap_s or acc >= max_speech_s):
            turns.append(cur)
            cur, acc = [], 0.0
        cur.append(i)
        acc += max(0.0, float(b) - float(a))
        prev_end = b
    if cur:
        turns.append(cur)
    return turns


def fill_adjacent(assignments: Sequence) -> List:
    """Give every unembedded span a cluster by ADJACENCY, in time order.

    `assignments` is one entry per unit in time order: a cluster id, or None for a unit that was not
    embedded. A None takes the PREVIOUS unit's cluster; a leading None takes the next assigned one.
    If the recording embedded nothing at all, every entry stays None and the spans are stored
    unassigned rather than invented into a cluster. Since M1b.3 the unit is the TURN, and each
    turn's segments inherit its answer; before that it was the span.

    Previous, not nearest-in-time, and that is a decision rather than an accident: conversation runs
    in turns, so a short utterance ("yeah", "mm") almost always continues the turn it follows rather
    than opening the one that comes next. It is a heuristic either way — the honest limit is that a
    short span carries no evidence of its own, and the goal doc says so.
    """
    out = list(assignments)
    last = None
    for i, v in enumerate(out):
        if v is None:
            out[i] = last
        else:
            last = v
    # a leading run of Nones: the first assigned cluster reaches backwards
    first = next((v for v in out if v is not None), None)
    for i, v in enumerate(out):
        if v is None:
            out[i] = first
        else:
            break
    return out
