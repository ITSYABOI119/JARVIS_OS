"""The extractor's two prompts, built from the registry so the model is told exactly what it may say.

The system prompt enumerates every predicate with its arity, subject kind and description — read
from `PREDICATES`, not transcribed — plus the relation set, the polarities, the source-kind rule and
the citation rule. The user prompt carries one span and its context. **Neither ever contains the
store's contents**: the extractor must not be able to agree with a belief it was shown, because an
extraction that echoes the store would look like corroboration and be nothing of the kind.

Both are module constants plus formatting, so the test can pin them (T35b) without a model.
"""
from ..registry import PREDICATES, RELATIONS
from .schema import POLARITIES

_RULES = """\
You extract structured candidates from ONE utterance of a household conversation.

Return JSON only, matching the given schema: {"candidates": [...]}.
Return an EMPTY list when nothing in the utterance states or implies one of the predicates below.
Most utterances are small talk and yield an empty list. Do not guess.

PREDICATES (use the id exactly):
%(predicates)s

RELATIONS (relation_id, required for person.relation_to): %(relations)s
POLARITIES (polarity, required for owner.prefers): %(polarities)s

SOURCE KIND:
  stated_owner  - the speaker is cluster 1 (the owner) and says it outright
  stated_other  - the speaker is any other cluster and says it outright
  inferred      - it is implied rather than said outright

SUBJECT:
  subject.kind is person, household or topic.
  subject.ref for a FIRST-PERSON statement ("i work as a nurse") is the SPEAKER'S CLUSTER ID as a
    string - "1" for the owner, "2" for the partner, and so on.
  subject.ref for a THIRD-PERSON statement ("my husband sam is a teacher") is the person's NAME,
    lower-cased.
  subject.ref for a household predicate is "household".

OBJECT:
  object      - the value as said, e.g. "a nurse", "Sydney".
  object_norm - the same value lower-cased and trimmed, e.g. "a nurse", "sydney".

CITATION:
  span_ids must be exactly [<the span id you were given>]. Never cite a span you were not given.
"""


def system_prompt() -> str:
    """Every predicate, relation and polarity the registry knows, listed once each."""
    lines = []
    for pid in sorted(PREDICATES):
        p = PREDICATES[pid]
        subj = "/".join(sorted(p.subject_kinds))
        lines.append("  %-22s arity=%-6s subject=%-9s object=%-6s  %s"
                     % (pid, p.arity, subj, p.object_kind, p.description))
    return _RULES % {
        "predicates": "\n".join(lines),
        "relations": ", ".join(sorted(RELATIONS)),
        "polarities": ", ".join(POLARITIES),
    }


def user_prompt(span_text, speaker_cluster, day, names, span_id) -> str:
    """One span, with the context the contract allows and nothing else.

    `names` maps a cluster id to a display name for the household's KNOWN people (the owner and the
    partner). It is what lets a third-person reference resolve to a name; it is not the store.

    `span_id` is appended to the signature the prompt's §3.2 lists, because §3.6's T35b requires the
    span id to appear here and the citation rule requires the model to echo it: an extractor that is
    not told which span it is reading cannot cite one.
    """
    known = ", ".join("cluster %s = %s" % (c, n) for c, n in sorted((names or {}).items()))
    return ("span_id: %s\nday: %s\nspeaker_cluster: %s\nknown people: %s\n\nutterance: %s"
            % (span_id, day, speaker_cluster, known or "(none known)", span_text))
