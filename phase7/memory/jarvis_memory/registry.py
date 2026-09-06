"""The predicate registry — the K-b select-never-synthesise instinct applied to memory.

The design's §4.1: the extractor SELECTS a predicate id from this static, human-reviewed table; it
never invents one. A candidate carrying an unknown predicate_id is not written, it becomes an audit
row with op 'reject', rule 'registry'. Adding a predicate is a reviewed code change with a test,
never a runtime act — so this table is a module-level constant with no loader and no override.

`owner.style` is deliberately absent: style is DERIVED from the owner's own spans into
style_snapshot rows and is never extracted as a candidate (the design §4.1, last table row, and §7).
"""
from dataclasses import dataclass, field


@dataclass(frozen=True)
class Predicate:
    id: str
    arity: str                       # 'single' (current-value semantics) | 'multi' (accumulates)
    subject_kinds: frozenset         # which subject.kind values may carry it
    object_kind: str                 # 'text' | 'person' | 'topic'
    description: str = ""
    extra: tuple = field(default=(), repr=False)   # reserved; keeps the dataclass extensible


_PERSON = frozenset({"person"})
_HOUSEHOLD = frozenset({"household"})
_OWNER = frozenset({"person"})

_DEFS = (
    Predicate("person.name", "single", _PERSON, "text",
              "a display name; the owner's is set at enrollment, a cluster's is earned"),
    Predicate("person.relation_to", "multi", _PERSON, "person",
              "the people layer's edges; carries a relation_id from RELATIONS"),
    Predicate("person.lives_in", "single", _PERSON, "text", "current-value semantics"),
    Predicate("person.works_as", "single", _PERSON, "text", ""),
    Predicate("person.habit", "multi", _PERSON, "text", "routines"),
    Predicate("person.trait", "multi", _PERSON, "text",
              "how they speak, recurring tendencies - always inferred"),
    Predicate("household.topic", "multi", _HOUSEHOLD, "topic", "what is talked about"),
    Predicate("household.routine", "multi", _HOUSEHOLD, "text", ""),
    Predicate("owner.prefers", "multi", _OWNER, "topic",
              "routed to the preference table, not to fact"),
)

PREDICATES = {p.id: p for p in _DEFS}

RELATIONS = frozenset({
    "spouse", "partner", "child", "parent", "sibling",
    "friend", "colleague", "housemate", "other",
})

# R2's ordering: an owner's own statement outranks another speaker's, which outranks an inference.
SOURCE_RANK = {"stated_owner": 3, "stated_other": 2, "inferred": 1}

# Predicates whose rows do not live in `fact` (the store routes them; kept here so one table
# describes the routing as well as the arity).
EDGE_PREDICATE = "person.relation_to"
PREFERENCE_PREDICATE = "owner.prefers"


def is_known(predicate_id: str) -> bool:
    """True iff the predicate is in the registry. Never raises."""
    return predicate_id in PREDICATES


def arity(predicate_id: str) -> str:
    """'single' or 'multi'. Raises KeyError on an unknown predicate — callers that must not
    raise ask is_known() first; the raise is deliberate so a typo cannot silently behave as
    multi-valued and accumulate rows that should have superseded each other."""
    return PREDICATES[predicate_id].arity


def predicate_words(predicate_id: str) -> str:
    """The predicate rendered for the full-text index: 'person.lives_in' -> 'person lives in'."""
    return predicate_id.replace(".", " ").replace("_", " ")
