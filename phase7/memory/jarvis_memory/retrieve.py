"""The ranker — R6's demotion, and the reason a stated fact never rots.

The design's §6:

    score = lane_score * w_source * w_recency * confidence

with w_source 1.0 / 0.8 / 0.6 for stated_owner / stated_other / inferred, and w_recency a 90-day
half-life on the age of the newest SUPPORTING span. Stated profile facts do not decay at all: the
owner saying where he lives does not become less true because he has not said it lately. Events and
inferred rows do decay, and that decay IS R6 - it demotes, it never deletes, and a row whose
confidence is recomputed upward from fresh spans rises again.

Pure: no database, no clock of its own. `rank` takes `now` from the caller so a benchmark can ask
what the store believed on a given day, which is what the spouse-surfacing measurement needs.
"""
import datetime as _dt

from .registry import QUERY_VOCAB

W_SOURCE = {"stated_owner": 1.0, "stated_other": 0.8, "inferred": 0.6}
HALF_LIFE_DAYS = 90.0


def tokens(text: str) -> list:
    """The query words: lower-cased alphanumeric runs, nothing else.

    One tokeniser serves both the hint and the FTS5 MATCH the store builds, so a word that steers
    the hint is the same word that reaches the index. FTS5 syntax characters never survive this,
    which is also what stops an operator's question being read as a MATCH expression.
    """
    out, cur = [], []
    for ch in str(text).lower():
        if ch.isalnum():
            cur.append(ch)
        elif cur:
            out.append("".join(cur))
            cur = []
    if cur:
        out.append("".join(cur))
    return out


def predicate_hint(text: str):
    """Which predicate a question is about, when exactly one is unambiguous — else None.

    Zero matches means the question uses none of the registry's words; several means it straddles
    predicates ("does alex live near where he works"). Both are left UNRESTRICTED rather than
    guessed at, because a wrong restriction hides the answer completely while no restriction only
    leaves the MS0 behaviour in place. That asymmetry is the whole reason the rule is 'exactly one'.
    """
    words = set(tokens(text))
    if not words:
        return None
    hits = [pid for pid, vocab in QUERY_VOCAB.items() if words & vocab]
    return hits[0] if len(hits) == 1 else None


def recency_weight(age_days: float, decays: bool) -> float:
    """1.0 when the row does not decay, else a 90-day half-life on its age."""
    if not decays:
        return 1.0
    return 0.5 ** (float(age_days) / HALF_LIFE_DAYS)


def score(lane_score: float, source_kind: str, age_days: float,
          confidence: float, is_stated_profile_fact: bool) -> float:
    """The §6 product. An unknown source_kind weighs 0, so a malformed row sinks rather than
    raising in the middle of a query."""
    w_source = W_SOURCE.get(source_kind, 0.0)
    return (float(lane_score) * w_source
            * recency_weight(age_days, decays=not is_stated_profile_fact)
            * float(confidence))


def is_stated_profile_fact(row: dict) -> bool:
    """A stated row in the `fact` table. Events, preferences, edges and every inferred row decay."""
    return row.get("table") == "fact" and str(row.get("source_kind", "")).startswith("stated")


def age_days(newest_span_at: str, now: str) -> float:
    """Days between a row's newest supporting span and `now`. Never negative — a span dated after
    `now` (a clock skew, or a benchmark asking about an earlier day) is treated as fresh rather
    than being rewarded with a weight above 1.0."""
    if not newest_span_at or not now:
        return 0.0
    try:
        a = _dt.datetime.fromisoformat(str(newest_span_at))
        b = _dt.datetime.fromisoformat(str(now))
    except ValueError:
        return 0.0
    return max(0.0, (b - a).total_seconds() / 86400.0)


def rank(rows: list, now: str) -> list:
    """Score every row and sort by score desc, then recorded_at desc.

    Each row carries lane_score, source_kind, newest_span_at, confidence, table and recorded_at.
    The rows are returned as new dicts with a 'score' key added; the inputs are not mutated.
    """
    out = []
    for r in rows:
        row = dict(r)
        row["score"] = score(
            r.get("lane_score", 0.0),
            r.get("source_kind", ""),
            age_days(r.get("newest_span_at"), now),
            r.get("confidence", 1.0),
            is_stated_profile_fact(r),
        )
        out.append(row)
    out.sort(key=lambda r: (r["score"], str(r.get("recorded_at") or "")), reverse=True)
    return out
