"""The extractor's JSON schema — GENERATED from the registry, never typed by hand.

Every enum in this file is read from `registry` at call time: the predicate ids, the relation ids,
the source kinds. If a predicate is added to the registry the schema gains it on the next call, and
if one is removed the model can no longer emit it. There is exactly one source of truth, and the
test asserts the enums equal the registry's own sorted keys (T35a) rather than a copied list — a
hand-typed enum that drifted from the registry is precisely the failure this arrangement removes.

`additionalProperties: false` at every object level: a model that invents a field fails the schema
rather than smuggling an unvalidated key into the store's write path.
"""
import hashlib
import json

from ..registry import PREDICATES, RELATIONS

# The four polarity values the preference predicate uses (registry `candidate.validate` requires one
# for owner.prefers). Kept beside the registry import so the list is visible at the point of use.
POLARITIES = ("likes", "dislikes", "wants", "avoids")


def candidate_schema() -> dict:
    """The schema for `{"candidates": [<what only the model can judge>]}` — the DECISION set.

    NARROWED at MS1b after the first Llama run (L0) measured the contract instead of the model: it
    was asked for `subject.ref`, `source_kind`, `speaker_cluster`, `object_norm` and `span_ids`, and
    every one of those is DERIVABLE from the span the caller already holds. All 105 of that run's
    invalid calls were speaker mismatches — a field the model was made to restate and the code knew
    for certain. So the model now decides only what a reader of the utterance must judge:

        predicate_id   which registry predicate, if any
        about          "speaker" (the person talking) or a lower-cased name
        object         the value AS SAID - "a nurse"; the code normalises it
        stated         said outright, or implied
        relation_id / polarity / strength / ended / about_time  when the predicate needs them

    `extract.derive` turns that plus the span into the store's §4.2 candidate. `about` is a string
    for the same reason `subject.ref` was: one type, so the model never chooses between two shapes.
    """
    candidate = {
        "type": "object",
        "additionalProperties": False,
        "required": ["predicate_id", "about", "object", "stated"],
        "properties": {
            "predicate_id": {"type": "string", "enum": sorted(PREDICATES)},
            "about": {"type": "string"},
            "object": {"type": "string"},
            "stated": {"type": "boolean"},
            "relation_id": {"type": ["string", "null"], "enum": sorted(RELATIONS) + [None]},
            "polarity": {"type": ["string", "null"], "enum": list(POLARITIES) + [None]},
            "strength": {"type": ["integer", "null"], "minimum": 1, "maximum": 3},
            "ended": {"type": "boolean"},
            "about_time": {"type": ["string", "null"]},
        },
    }
    return {
        "type": "object",
        "additionalProperties": False,
        "required": ["candidates"],
        "properties": {"candidates": {"type": "array", "items": candidate}},
    }


def schema_sha256() -> str:
    """A stable fingerprint of the schema, recorded in every run's JSON.

    Two runs comparable only if this matches: a changed registry changes the schema, which changes
    what the model was allowed to say, which makes the F1 numbers a different measurement.
    """
    return hashlib.sha256(
        json.dumps(candidate_schema(), sort_keys=True).encode("utf-8")).hexdigest()
