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

from ..registry import PREDICATES, RELATIONS, SOURCE_RANK

# The four polarity values the preference predicate uses (registry `candidate.validate` requires one
# for owner.prefers). Kept beside the registry import so the list is visible at the point of use.
POLARITIES = ("likes", "dislikes", "wants", "avoids")

SUBJECT_KINDS = ("person", "household", "topic")


def candidate_schema() -> dict:
    """The schema for `{"candidates": [<the design's §4.2 object>]}`.

    `subject.ref` is a STRING on purpose: it carries either a cluster id ("2") for a first-person
    statement or a normalised name ("sam") for a third-person one, and `score.resolve_subject`
    turns both into the same resolved subject. Keeping it one type means the model never has to
    choose between two shapes, which is one less way for a call to fail the schema.
    """
    candidate = {
        "type": "object",
        "additionalProperties": False,
        "required": ["predicate_id", "subject", "object", "object_norm", "source_kind",
                     "span_ids"],
        "properties": {
            "predicate_id": {"type": "string", "enum": sorted(PREDICATES)},
            "subject": {
                "type": "object",
                "additionalProperties": False,
                "required": ["kind", "ref"],
                "properties": {
                    "kind": {"type": "string", "enum": list(SUBJECT_KINDS)},
                    "ref": {"type": "string"},
                },
            },
            "object": {"type": "string"},
            "object_norm": {"type": "string"},
            "source_kind": {"type": "string", "enum": sorted(SOURCE_RANK)},
            "span_ids": {"type": "array", "items": {"type": "integer"}, "minItems": 1},
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
