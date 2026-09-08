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

from .registry import QUERY_VOCAB, STOPWORDS

W_SOURCE = {"stated_owner": 1.0, "stated_other": 0.8, "inferred": 0.6}
HALF_LIFE_DAYS = 90.0

# Reciprocal-rank fusion, from MS1a. 60 is the constant the method was published with, not a
# tuned value: it moves only with a measured reason written into the design. RRF is used
# because the lanes are not comparable in magnitude - BM25 is negative-better and unbounded,
# cosine is bounded - so only their ORDER can honestly be combined.
RRF_K = 60

# MS1a.2: the claim-status weight sits on the ROW. A belief (a fact, a preference) weighs 1.0; a
# span is evidence and weighs the inferred source rank - R2's own constant one level up, never a new
# one - so an utterance can no longer outrank the fact extracted from it. The measured reason is
# MS1a §3.8: every update miss was a span beating its own fact, because R2 orders WITHIN a lane and
# a span and its fact are never in the same lane.
#
# Why on the row and not on the lane: MS1a.1 specified a typed-lane partition (vec_fact/vec_pref/
# vec_span) and it was WITHDRAWN before any GPU run. Partitioning restarts the ranks, so the best
# row of every table earns a full rank-1 term however unlike the query it is - a cosine-0.0 fact
# tied a cosine-1.0 preference and won the tie-break. Weighting the row keeps one cosine order and
# still puts R2 across lanes.
W_CLAIM = {"fact": 1.0, "preference": 1.0, "span": W_SOURCE["inferred"]}


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


def query_terms(text: str, drop_stopwords: bool) -> list:
    """The words of a question that reach the FULL-TEXT index.

    `drop_stopwords` True removes the registry's STOPWORDS (design rule 3, MS1a.2); False keeps
    every token, which is the MS0.1 behaviour and the arm the benchmark measures against. The flag
    is carried by the store, never a module-level switch: the two settings must be runnable side by
    side in one process for the A / A-prime comparison to mean anything.

    The vector lane and `predicate_hint` never call this - the hint matches its own vocabulary,
    which is disjoint from STOPWORDS by assertion (T29a), and meaning is not made of content words.
    """
    toks = tokens(text)
    return [t for t in toks if t not in STOPWORDS] if drop_stopwords else toks


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


def fuse(lanes: dict, row_weights=None) -> dict:
    """Reciprocal-rank fusion over any number of lanes, each row weighed by its claim status.

    Each lane is an ORDERED list of keys, best first; a key at 1-based rank r contributes
    `w_claim(key) / (RRF_K + r)`, and a key found by several lanes sums its terms — which is the
    whole point: agreement between the full-text and vector lanes outranks a strong showing in one.
    A key in no lane is simply absent.

    `row_weights` maps a KEY to its weight (the caller looks each row's table up in W_CLAIM). **A
    key with no weight given weighs 1.0** — a row the caller did not classify is a belief until it
    says otherwise, never silently 0 — so a plain `fuse(lanes)` is byte-for-byte the MS1a behaviour
    and a new table cannot be dropped by forgetting to weigh it.
    """
    out = {}
    w = row_weights or {}
    for keys in (lanes or {}).values():
        for i, key in enumerate(keys or (), start=1):
            out[key] = out.get(key, 0.0) + w.get(key, 1.0) / (RRF_K + i)
    return out


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


def lane_order(members: list, now: str) -> list:
    """Order ONE lane's own candidates by the weighted score — step (1) of the design's §6.

    A member arrives in the lane's raw order (bm25 or cosine) and is scored at its position with
    MS0.1's formula, `1/(1+i) x w_source x w_recency x confidence`. R2's source rank and R6's decay
    belong HERE, where every competitor is a candidate for the same question — not on the fused
    value, which is what MS1a's first attempt measured to be wrong: RRF's output spans about 6 % over
    five ranks, so multiplying it by weights that differ by 20-40 % let a rank-5 fact about another
    person beat the correct rank-1 fact.

    Equal scores keep the lane's raw order (Python's sort is stable), so a tie never reshuffles what
    bm25 or cosine already decided.
    """
    out = []
    for i, m in enumerate(members):
        row = dict(m)
        row["wscore"] = score(
            1.0 / (1 + i),
            m.get("source_kind", ""),
            age_days(m.get("newest_span_at"), now),
            m.get("confidence", 1.0),
            is_stated_profile_fact(m),
        )
        out.append(row)
    out.sort(key=lambda r: r["wscore"], reverse=True)
    return out


def rank(rows: list, now: str) -> list:
    """Merge the lanes — step (3) of the design's §6.

    Each row carries `relevance` (the fused reciprocal-rank value), `tiebreak` (the best weighted
    score the row earned in any lane) and `recorded_at`. The ordering is relevance, then tiebreak,
    then recorded_at newest first. **The relevance is published as `score` and multiplied by
    nothing** — the weights already did their work inside the lanes, and applying them twice is the
    defect MS1a measured.

    `now` is unused and kept so the signature is stable for callers.
    """
    out = []
    for r in rows:
        row = dict(r)
        row["score"] = float(r.get("relevance", 0.0))
        row["tiebreak"] = float(r.get("tiebreak", 0.0))
        out.append(row)
    out.sort(key=lambda r: (r["score"], r["tiebreak"], str(r.get("recorded_at") or "")),
             reverse=True)
    return out
