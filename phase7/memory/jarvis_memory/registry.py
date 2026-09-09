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
    "person.habit": frozenset({"habit", "habits", "usually", "regularly"}),
    "person.trait": frozenset({"trait", "traits", "personality", "tends", "tend", "tendency"}),
    "household.topic": frozenset({
        "talk", "talks", "talked", "talking", "discuss", "discussed", "discussing", "topic",
        "topics", "conversation", "conversations"}),
    # `routine`/`routines` moved here from person.habit at MS1a (MS0.1 report F1): a household's
    # routine IS household.routine's predicate, and while those words sat under person.habit the
    # most natural phrasing - "what household routine do we keep" - matched two sets and was
    # left unrestricted, so the predicate was unreachable by its own obvious question.
    "household.routine": frozenset({
        "schedule", "schedules", "household", "weekly", "chores", "chore",
        "routine", "routines"}),
    "owner.prefers": frozenset({
        "like", "likes", "liked", "love", "loves", "loved", "prefer", "prefers", "preferred",
        "favourite", "favorite", "hate", "hates", "hated", "dislike", "dislikes", "avoid",
        "avoids", "enjoy", "enjoys", "want", "wants"}),
}


STOPWORDS = frozenset("""
a an the and or but if then than that this these those
i me my mine we us our ours you your yours he him his she her hers it its they them their theirs
what which who whom whose where when why how
is are was were be been being am do does did doing have has had having
of to in on at by for with from as into onto about over under again further there here
so no not only own same too very can will just should would could may might must shall also once
all any both each few more most other some such up down out off
yes yeah yep nope ok okay oh um uh well right
""".split())
"""English function words, dropped from the FULL-TEXT query only (design §6, MS1a.2).

The measured reason is MS1a §3.9: the rows that outranked the planted preference in the transfer
set shared only function words or stems with the scenario — `we should decide` on "should",
`sounds good to me` on "sound", `we planned the week together` on "plan" — which made generic
chatter a TWO-lane row and let it crowd the planted preference out of the top five. Dropping the
function words removes that half of the overlap; the stem half is content and stays, which is what
the `--keep-stopwords` arm measures.

STATIC and human-reviewed, the K-b instinct: a list the code SELECTS from, never derives, and never
a knob turned to move a benchmark number. It is disjoint from every `QUERY_VOCAB` set by assertion
(T29a), so removing a function word can never remove a word the predicate hint steers on, and every
entry survives `tokens()` unchanged (T29b).

Applied to `_fts_match` alone: `predicate_hint` matches its own vocabulary, and the vector lane
never sees this list — a query of only function words still reaches its row by meaning.
"""


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


# The ONE object normaliser (MS1b). Every candidate's `object_norm` is computed by this function and
# by nothing else: the corpus's oracle values already agree with it by construction (T36c, checked
# over ten households), and `Store.ingest` OVERWRITES whatever a caller supplied before any rule
# runs (T36d), so an extractor's own idea of a normal form can never reach a stored row.
#
# The measured reason it exists: MS1b's first Llama run (L0) scored F1 0.12, and every `works_as`
# miss was an ARTICLE — the model said "a plumber" where the oracle says "plumber", because the
# prompt's own worked example taught it to. Asking a model to normalise is asking it to reproduce a
# convention it cannot see; deriving the convention in code removes the question.
_ARTICLES = ("a", "an", "the")


def normalise_object(text) -> str:
    """Lower-case, trim, collapse internal whitespace, strip ONE leading article.

    'A Plumber' -> 'plumber'; '  Perth ' -> 'perth'; 'an early bird' -> 'early bird';
    'reads before bed' -> unchanged. A LONE article is returned as itself: 'the' -> 'the', never
    the empty string, because an object that normalises to nothing would collide with every other
    empty object in the value key and merge two unrelated beliefs.
    """
    s = " ".join(str(text if text is not None else "").split()).lower()
    head, _, rest = s.partition(" ")
    return rest if (head in _ARTICLES and rest) else s
