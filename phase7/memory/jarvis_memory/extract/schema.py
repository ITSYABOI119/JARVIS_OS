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

from ..registry import EDGE_PREDICATE, PREDICATES, PREFERENCE_PREDICATE, RELATIONS

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

    CONTRACT 2 (the field, 2026-09-09) splits that one object into a `oneOf` of FOUR
    predicate-family branches. The reason is measured: every one of contract 1's 39 invalid calls
    across both models was a null `relation_id` on an edge or a null `polarity` on a preference —
    fields that are optional only because a single flat object has to make them optional for the
    predicates that do not use them. Per-family branches make each one REQUIRED and NON-NULLABLE
    exactly where it applies, so constrained decoding cannot produce the invalid shape at all; the
    fields stay absent from the families they do not belong to. `oneOf` is used because llama.cpp's
    grammar converter handles `oneOf`/`anyOf` and does NOT support `if`/`then`.
    """
    household = sorted(p for p in PREDICATES if "household" in PREDICATES[p].subject_kinds)
    person = sorted(p for p in PREDICATES
                    if p not in household and p not in (EDGE_PREDICATE, PREFERENCE_PREDICATE))
    # The four branches must PARTITION the registry: every predicate reachable, none reachable twice
    # (a predicate in two branches makes `oneOf` ambiguous and the grammar non-deterministic).
    # Asserted here rather than trusted, so adding a predicate to the registry without placing it
    # breaks the build instead of silently vanishing from what the model may say.
    cover = sorted([EDGE_PREDICATE, PREFERENCE_PREDICATE] + household + person)
    assert cover == sorted(PREDICATES), (cover, sorted(PREDICATES))

    def branch(pids, extra_props, extra_required):
        props = {"predicate_id": {"type": "string", "enum": list(pids)}}
        props.update(extra_props)
        props.update({
            "object": {"type": "string"},
            "stated": {"type": "boolean"},
            "ended": {"type": "boolean"},
            "about_time": {"type": ["string", "null"]},
        })
        return {
            "type": "object",
            "additionalProperties": False,
            # `predicate_id` FIRST is deliberate, not cosmetic: llama.cpp compiles the schema to a
            # grammar in property order, so the model commits to a branch on its first key and the
            # rest of the object is constrained to that family from then on.
            "required": ["predicate_id"] + extra_required + ["object", "stated"],
            "properties": props,
        }

    EDGE = branch([EDGE_PREDICATE],
                  {"about": {"type": "string"},
                   # NOT nullable, and this is the whole point of the branch: under contract 1 an
                   # edge could carry relation_id null and every such call was invalid. It is now
                   # unproducible under constrained decoding rather than rejected after the fact.
                   "relation_id": {"type": "string", "enum": sorted(RELATIONS)}},
                  ["about", "relation_id"])
    PREFERENCE = branch([PREFERENCE_PREDICATE],
                        {"polarity": {"type": "string", "enum": list(POLARITIES)},
                         "strength": {"type": ["integer", "null"], "minimum": 1, "maximum": 3}},
                        ["polarity"])
    HOUSEHOLD = branch(household, {}, [])
    PERSON = branch(person, {"about": {"type": "string"}}, ["about"])

    return {
        "type": "object",
        "additionalProperties": False,
        "required": ["candidates"],
        "properties": {
            "candidates": {
                "type": "array",
                "items": {"oneOf": [EDGE, PREFERENCE, HOUSEHOLD, PERSON]},
            },
        },
    }


def schema_sha256() -> str:
    """A stable fingerprint of the schema, recorded in every run's JSON.

    Two runs comparable only if this matches: a changed registry changes the schema, which changes
    what the model was allowed to say, which makes the F1 numbers a different measurement.
    """
    return hashlib.sha256(
        json.dumps(candidate_schema(), sort_keys=True).encode("utf-8")).hexdigest()
