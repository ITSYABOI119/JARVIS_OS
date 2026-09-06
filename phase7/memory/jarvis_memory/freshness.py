"""R3's comparison — which of two rows is the newer, decided by arithmetic and never by a model.

The design's §4.3 R3: single-valued predicates resolve by the newest valid_from, ties broken on
said_at then recorded_at. The research measured deterministic freshness beating LLM adjudication by
about ten points where it was tested; keeping the comparison here, pure and total, is what lets the
store apply it inside one transaction with no model in the loop.

`valid_from` is about_time when the extractor or the speaker gave one, else said_at (§4.2, last
paragraph) — occurrence time when it is known, dialogue time otherwise. A row that already carries
an explicit valid_from keeps it; the fallback exists for a raw candidate that has not been through
the store yet.

The three keys are compared as ISO 8601 STRINGS, which sort chronologically for a fixed offset
format. A missing component sorts as '' — earliest — so a row lacking a said_at loses a tie to one
that has it rather than raising.
"""


def valid_from(row: dict) -> str:
    """The row's valid_from, or the §4.2 fallback: about_time, else said_at, else ''."""
    for field in ("valid_from", "about_time", "said_at"):
        v = row.get(field)
        if v:
            return str(v)
    return ""


def key(row: dict) -> tuple:
    """(valid_from, said_at, recorded_at) — the R3 ordering key."""
    return (valid_from(row), str(row.get("said_at") or ""), str(row.get("recorded_at") or ""))


def newer(a: dict, b: dict) -> bool:
    """True iff a is strictly newer than b. Identical keys are NOT newer in either direction, which
    is what leaves an existing row standing when a duplicate arrives."""
    return key(a) > key(b)
