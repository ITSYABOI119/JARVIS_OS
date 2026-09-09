"""Everything the model is NOT asked to decide — derived in code from the span it was given.

MS1b's finding, and the reason this module exists: the first Llama 3.1 8B run (L0) was scored at
validity 93.7 % and F1 0.12, and reading the failures showed they were the CONTRACT's, not the
model's. All 105 invalid calls were `stated_*` candidates whose `speaker_cluster` disagreed with the
span's — a value the caller knew exactly and made the model restate. Every `person.works_as` miss
was an article, because the prompt's worked example spelled `object_norm` as `"a nurse"` while the
oracle strips the determiner. Every relation miss was a shape mismatch: free text where the oracle
keys by relation id.

None of those are judgements about the utterance. They are consequences of the span, and a span is
data the code holds. So the model decides `predicate_id`, `about`, `object`, `stated` and the
predicate-specific fields, and `derive` computes the rest deterministically. K-b to its end: the
model selects what only a reader can judge, and nothing that can be looked up.

`derive` is pure and does not validate — `score.validity` runs `candidate.validate` on the DERIVED
candidate, which is the shape that would actually reach the store.
"""
from ..registry import (
    EDGE_PREDICATE, PREDICATES, PREFERENCE_PREDICATE, normalise_object,
)

SPEAKER = "speaker"


def _span_field(span, name):
    """Spans arrive as the corpus's dicts; an attribute-style span is accepted too."""
    if isinstance(span, dict):
        return span.get(name)
    return getattr(span, name, None)


def _subject(pid, about, cluster, owner_cluster):
    """The subject the store will key on, from the predicate's own subject kind and `about`.

    Order is load-bearing. A household predicate has no person subject however the model phrased
    `about`, so it is decided FIRST; `owner.prefers` is by definition the owner's, whoever said it
    (the partner reporting "he loves jazz" is still a fact about the owner); only then does `about`
    choose between the speaker and a named third person.
    """
    pred = PREDICATES.get(pid)
    if pred is not None and "household" in pred.subject_kinds:
        return {"kind": "household", "ref": "household"}
    if pid == PREFERENCE_PREDICATE:
        return {"kind": "person", "ref": str(owner_cluster)}
    if about == SPEAKER or not about:
        return {"kind": "person", "ref": str(cluster)}
    return {"kind": "person", "ref": about}


def _source_kind(stated, cluster, owner_cluster):
    """R2's rank, decided by who spoke rather than by what the model called it.

    `candidate.validate` rejects a `stated_*` candidate whose speaker differs from its span's
    cluster. Deriving the source kind FROM that cluster makes the check unfailable by construction,
    which is exactly the 105-call failure class L0 spent its validity on.
    """
    if not stated:
        return "inferred"
    return "stated_owner" if cluster == owner_cluster else "stated_other"


def derive(raw, span, owner_cluster=1, names=None) -> dict:
    """A raw model candidate plus its span -> the store's §4.2 candidate.

    `names` is accepted for symmetry with the prompt's context and is deliberately unused here: a
    name is resolved to a person by the people layer (and, in the bench, by `score.resolve_subject`),
    not by the extractor's derivation — resolving it here would bake the bench's own cluster map
    into a candidate that MS2 must be able to write before the person exists.
    """
    pid = raw.get("predicate_id")
    sid = _span_field(span, "sid")
    cluster = _span_field(span, "cluster")
    about = str(raw.get("about") or "").strip().lower()
    stated = bool(raw.get("stated"))
    obj = raw.get("object") if raw.get("object") is not None else ""
    rel = raw.get("relation_id")

    # A relation's value key is the relation id on BOTH sides - the corpus's own convention and the
    # store's `_cand_value_key` for the edge table. The fallback keeps a malformed relation
    # candidate well-formed enough to be REJECTED by validate for the right reason (no relation_id)
    # rather than crashing here.
    if pid == EDGE_PREDICATE and rel:
        object_norm = rel
    else:
        object_norm = normalise_object(obj)

    return {
        "predicate_id": pid,
        "subject": _subject(pid, about, cluster, owner_cluster),
        "object": obj,
        "object_norm": object_norm,
        "source_kind": _source_kind(stated, cluster, owner_cluster),
        "speaker_cluster": cluster,
        "span_ids": [sid],
        "relation_id": rel,
        "polarity": raw.get("polarity"),
        "strength": raw.get("strength"),
        "ended": bool(raw.get("ended")),
        "about_time": raw.get("about_time"),
    }
