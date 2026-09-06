"""Pure evaluation functions (standard library only) + the evaluate runner.

Scores are cosine similarities between an enrollment centroid and a clip embedding; the decision rule
is `score >= threshold -> owner` (verify.decide). Positives are the enrolled speaker's held-out clips,
negatives are other speakers' clips.

eer(pos, neg) -> (eer, threshold): the classic sweep over every distinct score as a candidate
threshold; FAR(t) = fraction of negatives with score >= t, FRR(t) = fraction of positives with
score < t. FAR falls and FRR rises with t; the EER is where they meet. If they meet exactly at a
candidate, that is the answer. If the sets are perfectly separable (a candidate with FAR = FRR = 0)
the threshold is the MIDPOINT of the gap between the highest negative and the lowest positive, so it
sits strictly between the sets rather than on a sample. Otherwise the crossing is linearly
interpolated between the two neighbouring candidates.
"""
from typing import Iterable, List, Sequence, Tuple


def far_frr_at(threshold: float, pos: Sequence[float], neg: Sequence[float]) -> Tuple[float, float]:
    """False-accept rate (negatives accepted) and false-reject rate (positives rejected) at threshold."""
    if not pos or not neg:
        raise ValueError("far_frr_at needs at least one positive and one negative score")
    far = sum(1 for s in neg if s >= threshold) / len(neg)
    frr = sum(1 for s in pos if s < threshold) / len(pos)
    return far, frr


def accuracy_at(threshold: float, pos: Sequence[float], neg: Sequence[float]) -> float:
    if not pos or not neg:
        raise ValueError("accuracy_at needs at least one positive and one negative score")
    correct = sum(1 for s in pos if s >= threshold) + sum(1 for s in neg if s < threshold)
    return correct / (len(pos) + len(neg))


def eer(pos: Sequence[float], neg: Sequence[float]) -> Tuple[float, float]:
    """Return (eer, threshold). See the module docstring for the rule."""
    if not pos or not neg:
        raise ValueError("eer needs at least one positive and one negative score")
    cands = sorted(set(list(pos) + list(neg)))
    rates = [far_frr_at(t, pos, neg) for t in cands]
    prev_t, prev_far, prev_frr = None, None, None
    for t, (far, frr) in zip(cands, rates):
        if far <= frr:
            if far == frr:
                if far == 0.0 and prev_t is not None:
                    # perfectly separable: sit strictly between max(neg) and min(pos)
                    hi_neg = max(neg)
                    lo_pos = min(s for s in pos if s > hi_neg) if any(s > hi_neg for s in pos) else t
                    return 0.0, (hi_neg + lo_pos) / 2.0
                return far, t
            if prev_t is None:
                return (far + frr) / 2.0, t
            # linear interpolation between (prev_t: prev_far > prev_frr) and (t: far < frr)
            d_prev = prev_far - prev_frr   # > 0
            d_cur = far - frr              # < 0
            alpha = d_prev / (d_prev - d_cur)
            thr = prev_t + alpha * (t - prev_t)
            e = prev_far + alpha * (far - prev_far)
            return e, thr
        prev_t, prev_far, prev_frr = t, far, frr
    # FAR never fell to FRR within the candidates: everything is accepted above the top score
    return (rates[-1][0] + rates[-1][1]) / 2.0, cands[-1]


def mean(xs: Iterable[float]) -> float:
    xs = list(xs)
    return sum(xs) / len(xs) if xs else float("nan")


def evaluate(enrollment, pos_dir, neg_dir, embedder=None) -> dict:
    """Score every WAV under pos_dir / neg_dir against the enrollment centroid and report EER + rates.

    Imports the GPU stack lazily (never at module level) so the pure functions above stay
    importable on a bare interpreter.
    """
    from pathlib import Path
    from .speaker import SpeakerEmbedder
    from .verify import score as cos_score
    from .audio import load_wav, duration_s

    emb = embedder or SpeakerEmbedder()
    centroid = enrollment["centroid"]

    def scores_for(d: Path) -> List[Tuple[str, float, float]]:
        out = []
        for p in sorted(Path(d).glob("*.wav")):
            wav, sr = load_wav(p)
            out.append((p.name, cos_score(centroid, emb.embed(wav, sr)), duration_s(wav, sr)))
        return out

    pos = scores_for(pos_dir)
    neg = scores_for(neg_dir)
    ps = [s for _, s, _ in pos]
    ns = [s for _, s, _ in neg]
    e, thr = eer(ps, ns)
    far, frr = far_frr_at(thr, ps, ns)
    return {
        "n_pos": len(ps), "n_neg": len(ns),
        "eer": e, "threshold": thr, "far_at_threshold": far, "frr_at_threshold": frr,
        "accuracy_at_threshold": accuracy_at(thr, ps, ns),
        "pos_mean": mean(ps), "neg_mean": mean(ns),
        "pos": pos, "neg": neg,
    }
