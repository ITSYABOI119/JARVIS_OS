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
from .schema import C3_FAMILY, CONTRACTS, POLARITIES

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
  A first-person pronoun ("i", "we") is read as the speaker.

STATED:
  stated is true only when the utterance says it OUTRIGHT.
  An address term is stated: calling someone "my husband" or "love" states the relation.
  A hint is NOT stated: "she picked the kids up" only suggests one. Emit a hint as a candidate
  only if you are confident, with stated false.
%(ended)s
OBJECT:
  object is the value AS SAID, e.g. "a nurse", "Sydney". Do NOT normalise it, do not strip words,
  do not lower-case it. That is done for you.
%(object_extra)s"""


# The two predicates L0 showed being used as a catch-all: a fact about a person was filed as
# something the household talks about. The clarification is APPENDED to the registry's own
# description rather than added as a separate line, so each predicate id still appears exactly once
# in the prompt (T35b) - a second mention is a second thing for the model to match against.
_EXTRA_DESC = {
    "household.topic": "a SUBJECT the household talks about, never a fact about one person",
    "household.routine": "something the household regularly does TOGETHER",
}

# CONTRACT 3 (MS2a, 2026-09-15). Three differences from contract 2, each answering a MEASURED loss
# of the chosen extractor on the contract-2 corpus, and no fourth:
#   (a) the STATED block gains the `ended` line - the winner set `ended` on none of the ten
#       "i stopped, i no longer ..." spans and once in 1,670 calls. It is a judgement about the
#       utterance, so it is an instruction; it cannot be derived from the span.
#   (b) `person.lives_in` and `person.works_as` say that a change is a NEW CURRENT VALUE - the
#       winner missed every "we moved to ... last week" span. The registry already declares
#       current-value semantics; the prompt never said so.
#   (c) `household.routine` and `household.topic` are re-drawn on the SCHEDULE boundary - the
#       winner's largest recoverable loss (routine F1 0.167 against topic 0.945) is that boundary
#       failing in both directions.
# The `ended` line is spliced where an EMPTY substitution reproduces contract 2 byte for byte, so
# the closed field's prompt hash never moves.
_ENDED_LINE_C3 = ('  ended is true when the speaker says a value no longer holds ("i stopped", '
                  '"no longer", "not any more", "used to").\n')
_CHANGE_DESC_C3 = ('a change is a new current value ("we moved to X last week" is the new value X, '
                   "with about_time when the utterance gives one)")
_EXTRA_DESC_C3 = {
    "person.lives_in": _CHANGE_DESC_C3,
    "person.works_as": _CHANGE_DESC_C3,
    "household.routine": "a recurring household chore or event on a schedule (a weekday or a time)",
    "household.topic": "a subject or hobby the household talks about, with no schedule",
}


# CONTRACT 4 (MS2a-3, 2026-09-20). Three edits to contract 3's text, each answering a defect
# MEASURED on the committed contract-3 run, and no fourth. The corpus and gold do not move.
#
#   (a) `ended` LEAVES the STATED block and becomes its own, with a worked example in the corpus's
#       own phrasing. The model cited all ten "i stopped, i no longer ..." spans, chose the right
#       predicate and the right object, and set `ended` true on FOUR: a polarity error on the one
#       field that closes a row, and it is what costs the coexisting band. Buried as the last line
#       of a block about whether something was SAID OUTRIGHT, it was being read as part of that
#       question rather than as its own.
#
#       THE EXAMPLE NAMES NO PREDICATE ID, and that is load-bearing rather than stylistic (MS2a-3
#       R2). A first draft ended it with `-> person.habit, ...`, which is the very predicate the
#       `ended` defect lives in - so any rise in habit extraction afterwards would have been
#       unreadable, the fix or the nudge. T46b's per-predicate count equality is the guard that
#       caught it, and the guard stays while the example changes.
#   (b) the OBJECT block gains a scheduled-routine example. `washing on saturday` came back as
#       `washing` in six households - six of the seven coexisting gold values never extracted. The
#       block already said "do not strip words"; what it lacked was the example.
#
# Both are spliced through slots whose EMPTY substitution reproduces contracts 2 and 3 byte for
# byte, so `846ad088...`, `31d99140...` and `5c0387e5...` never move.
_ENDED_BLOCK_C4 = """
ENDED:
  ended is true when the utterance says a value STOPPED or no longer holds.
  "i stopped, i no longer read before bed" says a value ended: emit the candidate for what stopped,
  with ended true.
  A value the speaker still holds is ended false. Getting this wrong is worse than omitting the
  candidate: a value recorded as current when the speaker said it stopped never closes.
"""
_OBJECT_EXTRA_C4 = ('  Keep the whole value, including its day or time: "washing on saturday", '
                    'never "washing".\n')


# CONTRACT 5 (MS2a-4, 2026-09-24): contract 4's two blocks with ONLY their example strings replaced.
# MS2a-3's verification found both examples were drawn from the scored corpus - `washing on saturday`
# is itself gold in six households, and the ENDED sentence is the corpus's own frame - so contract 4's
# gains could not separate a prompt that fixes a behaviour from one that shows the model the scored
# items. These replacements occur nowhere in the corpus text of seeds 1-20 (T47b): of their words only
# `the` and `last` do. Contract 4 minus contract 5 is therefore the premium of those two examples.
_ENDED_BLOCK_C5 = """
ENDED:
  ended is true when the utterance says a value STOPPED or no longer holds.
  "we gave up the allotment last spring" says a value ended: emit the candidate for what stopped,
  with ended true.
  A value the speaker still holds is ended false. Getting this wrong is worse than omitting the
  candidate: a value recorded as current when the speaker said it stopped never closes.
"""
_OBJECT_EXTRA_C5 = ('  Keep the whole value, including its day or time: '
                    '"choir on wednesday evenings", never "choir".\n')

# The contracts whose people header drops the word `cluster` - the form the scorer resolves. Contract 3
# KEEPS it: its committed run was measured with that header, and T46c pins it.
NO_CLUSTER_WORD = ("contract4", "contract5", "contract6")

# CONTRACT 6 (MS2a-4, 2026-09-24): contract 5 with every REMAINING corpus-drawn example held out, built
# from contract 5's text through this ONE static ordered table of (old, new, count). A read-only audit
# found contract 2's prompt - under which the whole MS1 field ran - already printed three gold values
# of the corpus, two scored utterances verbatim and two scored relation frames, and contract 3 added
# the frame of its own update set. What stays is the RULE, not an answer: pronouns, function words,
# and the instruction words the corpus also uses (STOPPED, no longer, still, `she` as a far end). Each
# `old` must occur exactly `count` times or `system_prompt` raises - a silent partial replacement
# would be a different contract measured under this one's name.
_C6_SUBSTITUTIONS = (
    ('("i work as a nurse")', '("i play the cello")', 1),
    ('("sam is a teacher" -> "sam")', '("wyatt is a locksmith" -> "wyatt")', 1),
    ('calling someone "my husband" or "love" states the relation',
     'calling someone "my fiance" or "sweetheart" states the relation', 1),
    ('"she picked the kids up" only suggests one',
     '"he kissed me goodnight" only suggests one', 1),
    ('e.g. "a nurse", "Sydney"', 'e.g. "a beekeeper", "Wellington"', 1),
    ('("we moved to X last week" is the new value X',
     '("we relocated to X in may" is the new value X', 2),
)


def system_prompt(contract: str = "contract2") -> str:
    """Every predicate, relation and polarity the registry knows, listed once each.

    `contract2` is the default and returns HEAD's string byte for byte: the MS1 field is closed and
    its prompt is part of what those numbers were measured under.
    """
    if contract not in CONTRACTS:
        raise ValueError("unknown contract %r - known: %s" % (contract, ", ".join(CONTRACTS)))
    if contract == "contract6":
        text = system_prompt("contract5")
        for old, new, count in _C6_SUBSTITUTIONS:
            found = text.count(old)
            if found != count:
                raise ValueError("contract 6: %r occurs %d time(s) in contract 5's prompt, the table "
                                 "says %d - refusing a partial substitution" % (old, found, count))
            text = text.replace(old, new)
        return text
    extra = _EXTRA_DESC_C3 if contract in C3_FAMILY else _EXTRA_DESC
    lines = []
    for pid in sorted(PREDICATES):
        p = PREDICATES[pid]
        subj = "/".join(sorted(p.subject_kinds))
        desc = "; ".join(x for x in (p.description, extra.get(pid)) if x)
        lines.append("  %-22s arity=%-6s subject=%-9s object=%-6s  %s"
                     % (pid, p.arity, subj, p.object_kind, desc))
    return _RULES % {
        "predicates": "\n".join(lines),
        "relations": ", ".join(sorted(RELATIONS)),
        "polarities": ", ".join(POLARITIES),
        "speaker": SPEAKER,
        "ended": (_ENDED_BLOCK_C4 if contract == "contract4"
                  else _ENDED_BLOCK_C5 if contract == "contract5"
                  else _ENDED_LINE_C3 if contract == "contract3" else ""),
        "object_extra": (_OBJECT_EXTRA_C4 if contract == "contract4"
                         else _OBJECT_EXTRA_C5 if contract == "contract5" else ""),
    }


def user_prompt(span_text, speaker_cluster, day, names, span_id, contract="contract2") -> str:
    """One span, with the context the contract allows and nothing else.

    `names` maps a cluster id to a display name for the household's KNOWN people (the owner and the
    partner). It is what lets a third-person reference resolve to a name; it is not the store.

    `span_id` and `speaker_cluster` are CONTEXT, not instructions: the model is no longer asked to
    cite a span or to name a speaker — `derive` takes both from the span object itself — but a
    reader still benefits from knowing which utterance and whose it is, and T35b2 pins that they
    are carried. The removal of the citation rule is why T36e checks the SYSTEM prompt: that is
    where instructions live.
    """
    # CONTRACT 4 (c): the header is written in the form the SCORER resolves. Under contracts 2 and 3
    # this line reads `cluster 1 = alex`, and the contract-3 run copied that spelling into 7 subject
    # refs and 4 relation objects - a form `score.resolve_person` cannot resolve, so the same edge
    # counted once as a miss and once as a false positive. Contract 4 drops the word from THIS join
    # only; the `speaker_cluster:` line and the SUBJECT block are untouched, and `derive` normalises
    # the old spelling under every contract so a model that says it anyway is still read correctly.
    # Contracts 5 and 6 inherit it (NO_CLUSTER_WORD); contract 3 keeps the word.
    fmt = "%s = %s" if contract in NO_CLUSTER_WORD else "cluster %s = %s"
    known = ", ".join(fmt % (c, n) for c, n in sorted((names or {}).items()))
    return ("span_id: %s\nday: %s\nspeaker_cluster: %s\nknown people: %s\n\nutterance: %s"
            % (span_id, day, speaker_cluster, known or "(none known)", span_text))
