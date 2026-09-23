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
import re

from ..registry import (
    EDGE_PREDICATE, PREDICATES, PREFERENCE_PREDICATE, normalise_object,
)

SPEAKER = "speaker"

# CONTRACT 4 (MS2a-3, 2026-09-20). `cluster 2` IS cluster 2 - a derivation from the span exactly
# like the first-person remap below it, and applied under EVERY contract for the same reason.
#
# Measured, which is why it is here rather than only in the prompt: the contract-3 run of the chosen
# extractor produced 7 subject refs and 4 relation objects spelled `cluster N`, against ZERO in the
# same model's contract-2 run, and `score.resolve_person` cannot resolve that form - it tries int()
# and then a name lookup, and `cluster 2` fails both. So one edge was counted once as a miss AND
# once as a false positive, which is the whole of the reported `person.relation_to` fall.
#
# EXACTLY one space and digits only: `cluster two`, `cluster` and `cluster 2b` are left alone,
# because each is a guess rather than a reading and the point of a derivation is that it is not a
# guess. This moves no committed number - a run JSON stores candidates that are ALREADY derived -
# and T46e asserts that over every committed run rather than arguing it.
_CLUSTER_REF = re.compile(r"^cluster (\d+)$", re.IGNORECASE)


def cluster_ref(value):
    """`cluster 2` -> `"2"`; anything else -> None. Pure, case-insensitive, one space, digits only."""
    m = _CLUSTER_REF.match(str(value if value is not None else "").strip())
    return m.group(1) if m else None


def cluster_ref_map(names_by_cluster) -> dict:
    """`{1: "alex", 2: "tess"}` -> `{"cluster 1": 1, "cluster 2": 2}` — the SENSITIVITY map only.

    This exists for the MS2a-3 re-score and for nothing else. `score.resolve_person` is deliberately
    NOT edited: the committed runs were scored with it as it stands, and re-scoring them means
    handing the scorer a wider `clusters_by_name` for one measurement, never changing what scoring
    means. The map it returns EXTENDS the caller's name map; it never replaces it, so a name still
    resolves exactly as it did.

    Only the `cluster <n>` spelling is added. Extending the map for every ref would resolve words
    the scorer is right to refuse - `she` names somebody without identifying them, and crediting
    that would be scoring an extraction that never named a person (M17 is that mutant).
    """
    return {"cluster %s" % c: c for c in (names_by_cluster or {})}

# CONTRACT 2 (MS1b, the field). A first-person pronoun spoken by cluster N IS cluster N — a
# derivation from the span, not a judgement about it, and the corpus's own convention ("we live in
# perth" is recorded as the speaker's fact, `corpus.py:152-153`).
#
# Measured, which is why it is here rather than in the prompt: under contract 1 both models answered
# `about` with the pronoun they heard instead of the literal token the prompt asked for — Gemma on
# 167 of 250 person-subject predictions (`i` 152, `we` 14, `me` 1), Llama on 36 of 246 (`i` 22,
# `us` 14) — and `derive` read each as a name that resolves to nobody, so the candidate could not
# match however right its predicate and object were. The prompt still asks for "speaker"; the
# derivation now tolerates the habit instead of scoring it.
#
# The PLURAL forms are load-bearing and were the difference between two numbers: remapping the
# stored contract-1 predictions with the full set gives Gemma 227 matches, with `i`/`me`/`myself`
# alone 213 (T38b pins both ends). A THIRD-person pronoun is deliberately absent — "she" refers to
# somebody the span does not identify, and resolving that is the people layer's work, not a
# derivation.
FIRST_PERSON = frozenset({
    "i", "me", "my", "mine", "myself",
    "we", "us", "our", "ours", "ourselves",
})


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
    if about == SPEAKER or about in FIRST_PERSON or not about:
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
    # `cluster 2` -> `2`, before `_subject` reads it as a name that resolves to nobody.
    about = cluster_ref(about) or about
    stated = bool(raw.get("stated"))
    obj = raw.get("object") if raw.get("object") is not None else ""
    rel = raw.get("relation_id")
    # The same reading on a relation's FAR END, and only there: for every other predicate `object`
    # is a value, not a person, and rewriting it would corrupt the thing being measured.
    if pid == EDGE_PREDICATE:
        obj = cluster_ref(obj) or obj

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
