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


# The query vocabulary — the K-b instinct applied to RETRIEVAL (added at MS0.1).
#
# A question that uses exactly one predicate's words is asking about that predicate, so the lane may
# restrict its lookup to it; a question that uses none, or several, is left unrestricted. The rule
# SELECTS a predicate the registry already types and can never invent one, exactly as the extractor
# selects a predicate id and never synthesises one.
#
# The measured reason it exists (design §6): MS0's growth set missed its band by 46.25 points because
# 30x filler put `works as` and `lives in` into roughly half of all rows, collapsing those terms' IDF
# to ~0.02, after which BM25's length normalisation returned the SHORTER of two facts about the same
# person. Narrowing the lookup by predicate removes the competitor rather than reweighting it.
#
# Matching is on RAW lower-cased tokens, never stemmed — which is why the inflections are spelled
# out. The nine sets are pairwise disjoint and every key is a predicate id; both are asserted by the
# test suite, so a word added to two sets fails the build rather than silently making the hint
# ambiguous. This table is human-reviewed: adding a word is a reviewed code change, never a tuning
# knob turned to move a benchmark number.
QUERY_VOCAB = {
    "person.name": frozenset({"name", "named", "called", "call"}),
    "person.relation_to": frozenset({
        "wife", "husband", "married", "marry", "spouse", "partner", "related", "relationship",
        "relation", "sister", "brother", "mother", "father", "mum", "dad", "son", "daughter",
        "friend", "friends", "colleague", "family"}),
    "person.lives_in": frozenset({
        "live", "lives", "living", "lived", "home", "address", "reside", "resides", "residing"}),
    "person.works_as": frozenset({
        "work", "works", "working", "worked", "job", "jobs", "occupation", "employed", "employer",
        "career", "profession"}),
    "person.habit": frozenset({"habit", "habits", "routine", "routines", "usually", "regularly"}),
    "person.trait": frozenset({"trait", "traits", "personality", "tends", "tend", "tendency"}),
    "household.topic": frozenset({
        "talk", "talks", "talked", "talking", "discuss", "discussed", "discussing", "topic",
        "topics", "conversation", "conversations"}),
    "household.routine": frozenset({
        "schedule", "schedules", "household", "weekly", "chores", "chore"}),
    "owner.prefers": frozenset({
        "like", "likes", "liked", "love", "loves", "loved", "prefer", "prefers", "preferred",
        "favourite", "favorite", "hate", "hates", "hated", "dislike", "dislikes", "avoid",
        "avoids", "enjoy", "enjoys", "want", "wants"}),
}


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
