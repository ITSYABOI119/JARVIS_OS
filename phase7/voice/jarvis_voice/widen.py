"""Widening the owner's enrollment from six clips to whatever confident speech his recordings hold.

The v1 enrollment is six clips, 6 × ~60 s, recorded to order on 2026-09-06. Everything the pipeline
does with the owner's voice rests on the centroid built from them, and M1a.3 measured that centroid
rejecting 74 % of his own one-second windows. The obvious question — would MORE of his voice help —
is answered here by measurement, under a rule fixed before any of its numbers:

  * CANDIDATES are speech runs of at least `MIN_RUN_S` from recordings that are NOT held out,
    scoring at least `MIN_SCORE` against the CURRENT centroid. Confident, not merely admitted: a
    candidate at the threshold would be as likely to drag the centroid as to sharpen it, and there
    is no second voiceprint to check it against.
  * v2 is the centroid over v1's clip vectors PLUS the candidates', so v1's deliberate, quiet,
    read-and-natural speech is never discarded — only added to.
  * v2 REPLACES v1 only if it is better on data neither of them was built from, at every window
    length with enough windows to say so. Otherwise it is reported and thrown away.

Nothing here deletes or moves a recording. The one file it may replace is the enrollment itself,
and only after the current pair has been copied aside.

The pure parts are standard library only, so the selection rule, the centroid arithmetic and the
adoption rule are all testable with no GPU, no model and no audio.
"""
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

from .enroll import build_centroid

# Pre-registered (PROMPT-VOICE-M1D-DATA.md §0) before any candidate was scored.
MIN_RUN_S = 10.0          # the length at which the owner's threshold means something (M1a.3)
MIN_SCORE = 0.50          # 0.14 above the stored threshold: confident, not merely admitted
MAX_CANDIDATES = 30       # a cap, so one long evening cannot outweigh the deliberate enrollment


def select_candidates(scored_runs: Sequence[dict], min_s: float = MIN_RUN_S,
                      min_score: float = MIN_SCORE, max_n: int = MAX_CANDIDATES) -> List[dict]:
    """The confident long runs, in the order they were spoken, capped.

    `scored_runs` is `{"chunk", "offset_s", "duration_s", "score", …}` per run. Chronological by
    (chunk, offset) and NOT by score: taking the highest-scoring runs would select exactly the
    speech that already looks like the centroid, which is the one way to widen an enrollment while
    learning nothing. The cap is applied after the filters, to the earliest survivors.
    """
    keep = [r for r in scored_runs
            if float(r.get("duration_s", 0.0)) >= min_s and float(r.get("score", -1.0)) >= min_score]
    keep.sort(key=lambda r: (str(r.get("chunk", "")), float(r.get("offset_s", 0.0))))
    return keep[:max(0, int(max_n))]


def build_v2(v1_vectors: Sequence[Sequence[float]],
             candidate_vectors: Sequence[Sequence[float]]) -> List[float]:
    """The v2 centroid: v1's clip vectors and the candidates', weighted one vote each.

    One vote each, not one per second: a fifteen-minute chunk would otherwise outvote the entire
    deliberate enrollment, and the enrollment is the part that was recorded under known conditions.
    """
    if not candidate_vectors:
        raise ValueError("no candidates - v2 would be v1, and calling that a new enrollment would "
                         "record a widening that did not happen")
    return build_centroid(list(v1_vectors) + list(candidate_vectors))


def adoption_rule(footing_eer_v2: Optional[float], per_d: Sequence[dict],
                  n_candidates: int) -> Tuple[bool, str]:
    """The pre-registered three-condition rule -> (adopt, reason). Pure.

    `per_d` is one row per window length that has enough windows to speak: `d_s`, `n_pos`, `n_neg`,
    `eer_v1`, `eer_v2`, `far_v1`, `far_v2`. Rows below the sample floor are the caller's to exclude,
    for the same reason M1a.4 gave: a rate on a handful of windows is not a rate.

    All three must hold, and each is there for its own reason:
      (i)   v2 still meets the M0b band with margin on whole pieces — a widened centroid that lost
            the band would be a different voiceprint, not a better one;
      (ii)  v2's EER is no worse at EVERY qualifying length — better on average is not good enough
            when the average hides the short windows this exists to fix;
      (iii) v2's FAR is no worse at every qualifying length — an enrollment that widened until it
            accepted strangers would improve every owner metric on the way.
    """
    if n_candidates <= 0:
        return False, ("no candidates: there is no v2 to adopt, and adopting v1 under a new name "
                       "would record a widening that did not happen")
    if footing_eer_v2 is None or footing_eer_v2 != 0.0:
        return False, ("condition (i): v2's EER on the M0b whole-piece footing is %s, not 0.0000"
                       % ("None" if footing_eer_v2 is None else "%.4f" % footing_eer_v2))
    if not per_d:
        return False, "no window length had enough windows on both sides to compare v1 and v2"
    worse_eer = [r["d_s"] for r in per_d if r["eer_v2"] > r["eer_v1"]]
    if worse_eer:
        return False, ("condition (ii): v2's EER is worse than v1's at D = %s"
                       % ", ".join("%g" % d for d in worse_eer))
    worse_far = [r["d_s"] for r in per_d if r["far_v2"] > r["far_v1"]]
    if worse_far:
        return False, ("condition (iii): v2's FAR is worse than v1's at D = %s"
                       % ", ".join("%g" % d for d in worse_far))
    return True, ("all three conditions met over %d window length(s) from %d candidate run(s)"
                  % (len(per_d), n_candidates))


def compare_by_duration(pos_v1, pos_v2, neg_v1, neg_v2, thr_v1: float, thr_v2: float,
                        min_n: int) -> List[dict]:
    """Per window length: EER and FAR for both enrollments, over the SAME windows.

    The same windows on both sides is the whole point — a comparison of two voiceprints measured on
    different audio would be a comparison of the audio. Each argument is `{d_s: [scores]}`.
    """
    from .evaluate import eer, far_frr_at
    rows = []
    for d in sorted(set(pos_v1) & set(pos_v2) & set(neg_v1) & set(neg_v2)):
        p1, p2, n1, n2 = pos_v1[d], pos_v2[d], neg_v1[d], neg_v2[d]
        if not (p1 and n1 and p2 and n2):
            continue
        e1, t1 = eer(p1, n1)
        e2, t2 = eer(p2, n2)
        far1, frr1 = far_frr_at(thr_v1, p1, n1)
        far2, frr2 = far_frr_at(thr_v2, p2, n2)
        rows.append({"d_s": d, "n_pos": len(p1), "n_neg": len(n1),
                     "eer_v1": e1, "eer_v2": e2, "eer_thr_v1": t1, "eer_thr_v2": t2,
                     "far_v1": far1, "far_v2": far2, "frr_v1": frr1, "frr_v2": frr2,
                     "qualifies": len(p1) >= min_n and len(n1) >= min_n})
    return rows


def backup_paths(store) -> Tuple[Path, Path]:
    """Where v1 is copied before v2 replaces it, named by v1's own creation date."""
    import json as _json
    created = "unknown"
    try:
        created = str(_json.loads(store.json_path.read_text(encoding="utf-8"))
                      .get("created", "unknown"))[:10]
    except Exception:                                          # noqa: BLE001
        pass
    return (store.json_path.with_name("%s.v1.%s.json" % (store.name, created)),
            store.npy_path.with_name("%s.v1.%s.npy" % (store.name, created)))


def backup_v1(store) -> Tuple[Path, Path]:
    """Copy the current enrollment aside. REFUSES over an existing backup.

    Refuses rather than overwrites because the second run of an adoption is exactly when the first
    version would be lost: a backup that can be overwritten by the thing it protects against is not
    a backup.
    """
    import shutil
    js, npy = backup_paths(store)
    if js.exists() or npy.exists():
        raise SystemExit("a v1 backup already exists (%s) - refusing to overwrite it; move it "
                         "aside by hand if this is a second widening" % js)
    shutil.copy2(store.json_path, js)
    if store.npy_path.exists():
        shutil.copy2(store.npy_path, npy)
    return js, npy
