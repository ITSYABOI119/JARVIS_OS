"""The extractor's two prompts, built from the registry so the model is told exactly what it may say.

The system prompt enumerates every predicate with its arity, subject kind and description — read
from `PREDICATES`, not transcribed — plus the relation set, the polarities, and the three rules the
model must actually JUDGE: who the utterance is about, whether it was said outright, and what value
was said. The user prompt carries one span and its context. **Neither ever contains the store's
contents**: the extractor must not be able to agree with a belief it was shown, because an
extraction that echoes the store would look like corroboration and be nothing of the kind.

NARROWED at MS1b: the SOURCE KIND, SUBJECT, OBJECT-normalisation and CITATION sections were removed
outright. Each told the model to restate something the caller already knew, and L0 measured the
price — 105 invalid calls, every one a speaker mismatch on a field the code holds, and every
`works_as` miss an article this file's own example had taught. `extract.derive` computes them now,
and T36e asserts no instruction for a derived field can come back.

Both are module constants plus formatting, so the test can pin them (T35b) without a model.
"""
from ..registry import PREDICATES, RELATIONS
from .derive import SPEAKER
from .schema import POLARITIES

_RULES = """\
You extract structured candidates from ONE utterance of a household conversation.

Return JSON only, matching the given schema: {"candidates": [...]}.
Return an EMPTY list when nothing in the utterance states or implies one of the predicates below.
MOST utterances are small talk and yield an empty list. Do not guess, and do not emit a candidate
you would not defend. Never turn a fact about a person into a topic or a routine.

PREDICATES (use the id exactly):
%(predicates)s

RELATIONS (relation_id, required for person.relation_to): %(relations)s
POLARITIES (polarity, required for owner.prefers): %(polarities)s

ABOUT:
  about is "%(speaker)s" when the utterance is about the person talking ("i work as a nurse").
  about is the NAME, lower-cased, when it is about someone named ("sam is a teacher" -> "sam").
  For a relation, about is the person the relation belongs TO and object is the OTHER person -
  a name if one is given, otherwise the word used for them, e.g. "she".

STATED:
  stated is true only when the utterance says it OUTRIGHT.
  An address term is stated: calling someone "my husband" or "love" states the relation.
  A hint is NOT stated: "she picked the kids up" only suggests one. Emit a hint as a candidate
  only if you are confident, with stated false.

OBJECT:
  object is the value AS SAID, e.g. "a nurse", "Sydney". Do NOT normalise it, do not strip words,
  do not lower-case it. That is done for you.
"""


# The two predicates L0 showed being used as a catch-all: a fact about a person was filed as
# something the household talks about. The clarification is APPENDED to the registry's own
# description rather than added as a separate line, so each predicate id still appears exactly once
# in the prompt (T35b) - a second mention is a second thing for the model to match against.
_EXTRA_DESC = {
    "household.topic": "a SUBJECT the household talks about, never a fact about one person",
    "household.routine": "something the household regularly does TOGETHER",
}


def system_prompt() -> str:
    """Every predicate, relation and polarity the registry knows, listed once each."""
    lines = []
    for pid in sorted(PREDICATES):
        p = PREDICATES[pid]
        subj = "/".join(sorted(p.subject_kinds))
        desc = "; ".join(x for x in (p.description, _EXTRA_DESC.get(pid)) if x)
        lines.append("  %-22s arity=%-6s subject=%-9s object=%-6s  %s"
                     % (pid, p.arity, subj, p.object_kind, desc))
    return _RULES % {
        "predicates": "\n".join(lines),
        "relations": ", ".join(sorted(RELATIONS)),
        "polarities": ", ".join(POLARITIES),
        "speaker": SPEAKER,
    }


def user_prompt(span_text, speaker_cluster, day, names, span_id) -> str:
    """One span, with the context the contract allows and nothing else.

    `names` maps a cluster id to a display name for the household's KNOWN people (the owner and the
    partner). It is what lets a third-person reference resolve to a name; it is not the store.

    `span_id` and `speaker_cluster` are CONTEXT, not instructions: the model is no longer asked to
    cite a span or to name a speaker — `derive` takes both from the span object itself — but a
    reader still benefits from knowing which utterance and whose it is, and T35b2 pins that they
    are carried. The removal of the citation rule is why T36e checks the SYSTEM prompt: that is
    where instructions live.
    """
    known = ", ".join("cluster %s = %s" % (c, n) for c, n in sorted((names or {}).items()))
    return ("span_id: %s\nday: %s\nspeaker_cluster: %s\nknown people: %s\n\nutterance: %s"
            % (span_id, day, speaker_cluster, known or "(none known)", span_text))
